/*
 * gt911.ko - Goodix GT911 capacitive touch controller (I2C), e.g. on the RK043FN66HS-CTG
 * panel of the EVKB.
 *
 * The controller keeps its own configuration (resolution, interrupt mode); this driver
 * reads it and changes one field only: the number of touch points, when it is below
 * MAX_POINTS (the RK043FN66HS panel comes with 1, so a second finger was never reported).
 * The whole configuration goes back with that field, a new checksum and Config_Fresh; a
 * configuration whose checksum does not match is left alone. Touch data: status register
 * 0x814E (bit 7 = data ready, bits 3:0 = points), then 8 bytes per point from 0x814F;
 * writing 0 to the status register releases the buffer. Coordinates are scaled to the
 * display size (touchscreen-size-x/y, else the configured resolution). That resolution is
 * the panel's: it goes to the display (fb_suggest_size), which changes to that mode of its
 * panel if it has one and nobody uses it yet - how an 800x480 panel and the 480x272 one are
 * told apart.
 *
 * Multi-touch (Linux protocol B): each finger keeps its slot for as long as it is on the
 * glass (the controller's track id picks it), with ABS_MT_TRACKING_ID -1 when it lifts; ABS_X
 * and ABS_Y follow the finger in the lowest slot, BTN_TOUCH tells whether any is down.
 * Positions are reported when they change.
 *
 * The controller now and then reports a scan without a finger that stays on the glass (near
 * the edges most of all): a finger only counts as lifted after RELEASE_MS without it,
 * otherwise one tap would arrive as several.
 */
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/fb.h>
#include <crtos/i2c.h>
#include <crtos/input.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/sched.h>
#include <crtos/sync.h>

#define REG_ID 0x8140u
#define REG_CONFIG 0x8047u
#define CONFIG_LEN 184u /* 0x8047-0x80FE; then the checksum and Config_Fresh */
#define CFG_TOUCHES 5u  /* offset of Touch_Number (bits 3:0) */
#define REG_STATUS 0x814Eu
#define REG_POINTS 0x814Fu
#define MAX_POINTS 5
#define POLL_MS 20
#define RELEASE_MS 40

struct gt911
{
    struct device *dev;
    struct i2c_client *client;
    struct input_dev *input;
    int irq;
    struct semaphore sem;
    task_t *thread;
    uint32_t res_x, res_y;   /* controller resolution */
    uint32_t size_x, size_y; /* display size */
    int swap_xy;
    unsigned touches;             /* points the controller is configured for */
    int down;                     /* a finger is on the glass */
    int dirty;                    /* reported since the last sync */
    int32_t id[MAX_POINTS];       /* the finger (track id) in each slot, -1: none */
    uint32_t missing[MAX_POINTS]; /* tick of the first report without it, 0: seen */
    int32_t x[MAX_POINTS], y[MAX_POINTS];
};

static int rd(struct gt911 *g, uint16_t reg, void *buf, size_t len)
{
    uint8_t a[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_write_read(g->client, a, 2, buf, len);
}

static int wr8(struct gt911 *g, uint16_t reg, uint8_t v)
{
    uint8_t b[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, v};
    return i2c_write(g->client, b, 3);
}

/* Touch_Number of the configuration @cfg (CONFIG_LEN bytes, read) raised to MAX_POINTS */
static void set_touches(struct gt911 *g, uint8_t *cfg)
{
    uint8_t chk;
    if (rd(g, REG_CONFIG + CONFIG_LEN, &chk, 1))
        return;
    uint8_t sum = 0;
    for (unsigned i = 0; i < CONFIG_LEN; i++)
        sum += cfg[i];
    if ((uint8_t)(sum + chk))
    {
        dev_warn(g->dev, "configuration checksum does not match: left as it is\n");
        return;
    }
    static uint8_t buf[2 + CONFIG_LEN + 2]; /* (probe runs one at a time) */
    buf[0] = (uint8_t)(REG_CONFIG >> 8);
    buf[1] = (uint8_t)REG_CONFIG;
    for (unsigned i = 0; i < CONFIG_LEN; i++)
        buf[2 + i] = cfg[i];
    buf[2 + CFG_TOUCHES] = (uint8_t)((cfg[CFG_TOUCHES] & 0xF0u) | MAX_POINTS);
    sum = 0;
    for (unsigned i = 0; i < CONFIG_LEN; i++)
        sum += buf[2 + i];
    buf[2 + CONFIG_LEN] = (uint8_t)(~sum + 1u);
    buf[2 + CONFIG_LEN + 1] = 1; /* Config_Fresh: take it */
    if (i2c_write(g->client, buf, sizeof(buf)))
    {
        dev_warn(g->dev, "configuration not written\n");
        return;
    }
    task_sleep_ms(100); /* (it applies the new configuration) */
    rd(g, REG_CONFIG, cfg, CONFIG_LEN);
}

static void gt_irq(int irq, void *ctx)
{
    (void)irq;
    struct gt911 *g = ctx;
    sem_give(&g->sem);
}

static uint32_t scale(uint32_t v, uint32_t from, uint32_t to)
{
    uint32_t r = from ? v * to / from : v;
    return r >= to ? to - 1 : r;
}

static void report(struct gt911 *g, uint16_t type, uint16_t code, int32_t value)
{
    input_report(g->input, type, code, value);
    g->dirty = 1;
}

/* The slot of finger @track: the one it has, or a free one (-1: all taken) */
static int slot_of(struct gt911 *g, int32_t track)
{
    int free_slot = -1;
    for (int s = 0; s < MAX_POINTS; s++)
    {
        if (g->id[s] == track)
            return s;
        if (g->id[s] < 0 && free_slot < 0)
            free_slot = s;
    }
    return free_slot;
}

/* The fingers not in the last report (@seen: the slots that were): lifted after RELEASE_MS
 * without them; @fresh: a report came (a finger missing from it starts its time) */
static void release_missing(struct gt911 *g, uint32_t seen, int fresh)
{
    uint32_t now = tick_get() | 1u;
    for (int s = 0; s < MAX_POINTS; s++)
    {
        if (g->id[s] < 0 || (seen >> s & 1u))
            continue;
        if (!g->missing[s])
        {
            if (fresh)
                g->missing[s] = now;
        }
        else if (now - g->missing[s] >= RELEASE_MS)
        {
            report(g, EV_ABS, ABS_MT_SLOT, s);
            report(g, EV_ABS, ABS_MT_TRACKING_ID, -1);
            g->id[s] = -1;
            g->missing[s] = 0;
        }
    }
}

/* The single-touch view (the finger in the lowest slot) and the end of the report */
static void finish(struct gt911 *g)
{
    int first = -1;
    for (int s = 0; s < MAX_POINTS && first < 0; s++)
        if (g->id[s] >= 0)
            first = s;
    if (first >= 0 && g->dirty)
    {
        report(g, EV_ABS, ABS_X, g->x[first]);
        report(g, EV_ABS, ABS_Y, g->y[first]);
    }
    if ((first >= 0) != g->down)
    {
        g->down = first >= 0;
        report(g, EV_KEY, BTN_TOUCH, g->down);
    }
    if (g->dirty)
        input_sync(g->input);
    g->dirty = 0;
}

static void gt_poll(struct gt911 *g)
{
    uint8_t st;
    if (rd(g, REG_STATUS, &st, 1))
        return;
    if (!(st & 0x80u))
    { /* no new data: the fingers missing from the last report may be up */
        release_missing(g, 0, 0);
        finish(g);
        return;
    }
    unsigned n = st & 0x0Fu;
    if (n > MAX_POINTS)
        n = MAX_POINTS;
    uint8_t pts[8 * MAX_POINTS];
    if (n && rd(g, REG_POINTS, pts, 8u * n))
    {
        wr8(g, REG_STATUS, 0); /* a failed read is no release */
        return;
    }
    wr8(g, REG_STATUS, 0);
    uint32_t seen = 0;
    for (unsigned i = 0; i < n; i++)
    {
        const uint8_t *p = pts + 8 * i;
        uint32_t x = p[1] | ((uint32_t)p[2] << 8), y = p[3] | ((uint32_t)p[4] << 8);
        if (g->swap_xy)
        {
            uint32_t t = x;
            x = y;
            y = t;
        }
        x = scale(x, g->swap_xy ? g->res_y : g->res_x, g->size_x);
        y = scale(y, g->swap_xy ? g->res_x : g->res_y, g->size_y);
        int s = slot_of(g, p[0]);
        if (s < 0 || (seen >> s & 1u))
            continue;
        seen |= 1u << s;
        g->missing[s] = 0;
        int fresh = g->id[s] != p[0];
        if (!fresh && g->x[s] == (int32_t)x && g->y[s] == (int32_t)y)
            continue; /* (nothing new about this finger) */
        report(g, EV_ABS, ABS_MT_SLOT, s);
        if (fresh)
            report(g, EV_ABS, ABS_MT_TRACKING_ID, p[0]);
        report(g, EV_ABS, ABS_MT_POSITION_X, (int32_t)x);
        report(g, EV_ABS, ABS_MT_POSITION_Y, (int32_t)y);
        g->id[s] = p[0];
        g->x[s] = (int32_t)x;
        g->y[s] = (int32_t)y;
    }
    release_missing(g, seen, 1);
    finish(g);
}

static void gt_thread(void *arg)
{
    struct gt911 *g = arg;
    while (!task_should_stop())
    {
        uint32_t wait = (g->irq < 0 || g->down) ? POLL_MS : WAIT_FOREVER;
        if (sem_take(&g->sem, wait) == -EINTR)
            break;
        gt_poll(g);
    }
}

static int gt_probe(struct device *dev)
{
    struct i2c_client *client = i2c_client_get(dev);
    if (!client)
        return -ENODEV;
    struct gt911 *g = devm_kzalloc(dev, sizeof(*g), 0);
    if (!g)
        return -ENOMEM;
    g->dev = dev;
    g->client = client;
    g->irq = device_get_irq(dev, 0);
    if (g->irq == -EPROBE_DEFER)
        return g->irq;

    char id[5] = {0};
    int r = -EIO;
    for (int tries = 0; tries < 4 && r; tries++)
    {
        r = rd(g, REG_ID, id, 4);
        if (r)
            task_sleep_ms(30);
    }
    if (r)
        return -ENODEV; /* not this panel */
    if (id[0] != '9')
    {
        dev_err(dev, "unexpected product id '%s'\n", id);
        return -ENODEV;
    }
    static uint8_t cfg[CONFIG_LEN]; /* 0x8047: version, x lo/hi, y lo/hi, touch number, module switch 1... */
    r = rd(g, REG_CONFIG, cfg, sizeof(cfg));
    if (r)
        return r;
    if ((cfg[CFG_TOUCHES] & 0x0Fu) < MAX_POINTS)
        set_touches(g, cfg);
    g->res_x = cfg[1] | ((uint32_t)cfg[2] << 8);
    g->res_y = cfg[3] | ((uint32_t)cfg[4] << 8);
    unsigned int_mode = cfg[6] & 3u; /* 0 rising, 1 falling, 2 low level, 3 high level */
    g->size_x = g->res_x ? g->res_x : 480;
    g->size_y = g->res_y ? g->res_y : 272;
    of_property_read_u32(dev->of_node, "touchscreen-size-x", &g->size_x);
    of_property_read_u32(dev->of_node, "touchscreen-size-y", &g->size_y);
    g->swap_xy = of_property_read_bool(dev->of_node, "touchscreen-swapped-x-y");
    g->touches = cfg[CFG_TOUCHES] & 0x0Fu;
    if (g->res_x && g->res_y) /* (the display says what it did) */
        fb_suggest_size(g->swap_xy ? g->res_y : g->res_x, g->swap_xy ? g->res_x : g->res_y);
    for (int s = 0; s < MAX_POINTS; s++)
        g->id[s] = -1;

    g->input = input_register("gt911");
    if (!g->input)
        return -ENOMEM;
    input_set_abs(g->input, ABS_X, 0, (int32_t)g->size_x - 1);
    input_set_abs(g->input, ABS_Y, 0, (int32_t)g->size_y - 1);
    input_set_key(g->input, BTN_TOUCH);
    sem_init(&g->sem, 0, 1);
    wr8(g, REG_STATUS, 0);
    if (g->irq >= 0)
    {
        static const unsigned types[4] = {IRQ_TYPE_EDGE_RISING, IRQ_TYPE_EDGE_FALLING, IRQ_TYPE_EDGE_FALLING,
                                          IRQ_TYPE_EDGE_RISING};
        irq_set_type(g->irq, types[int_mode]);
        if (irq_request(g->irq, gt_irq, g, 0, "gt911"))
            g->irq = -1;
    }
    g->thread = kthread_create("gt911", gt_thread, g, PRIO_HIGH, 1536);
    if (!g->thread)
    {
        if (g->irq >= 0)
            irq_free(g->irq);
        input_unregister(g->input);
        return -ENOMEM;
    }
    dev_set_drvdata(dev, g);
    dev_info(dev, "GT%s touch, config v%u, %lux%lu -> %lux%lu, %u points, %s\n", id, cfg[0],
             (unsigned long)g->res_x, (unsigned long)g->res_y, (unsigned long)g->size_x, (unsigned long)g->size_y,
             g->touches, g->irq >= 0 ? "interrupt" : "polled");
    return 0;
}

static void gt_remove(struct device *dev)
{
    struct gt911 *g = dev_get_drvdata(dev);
    kthread_stop(g->thread);
    if (g->irq >= 0)
        irq_free(g->irq);
    input_unregister(g->input);
}

static const struct of_device_id gt_ids[] = {
    {"goodix,gt911", NULL},
    {NULL, NULL},
};

static struct driver gt_driver = {
    .name = "gt911",
    .of_match_table = gt_ids,
    .probe = gt_probe,
    .remove = gt_remove,
};

static int init(void)
{
    return driver_register(&gt_driver);
}

static void fini(void)
{
    driver_unregister(&gt_driver);
}

MODULE("gt911", "Goodix GT911 touch screen", init, fini);
