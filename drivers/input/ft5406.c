/*
 * ft5406.ko - FocalTech FT5406 capacitive touch controller (I2C), e.g. on the RK043FN02H-CT
 * panel of the EVKB.
 *
 * The controller pulls its interrupt line low when new touch data is available; a thread
 * then reads the touch points and reports them. Multi-touch (Linux protocol B): each finger
 * keeps its slot while it is on the glass (the controller's touch id picks it), with
 * ABS_MT_TRACKING_ID -1 when it lifts; ABS_X/ABS_Y follow the finger in the lowest slot and
 * BTN_TOUCH tells whether any is down. Positions are reported when they change. While a
 * finger is down the thread also polls, so the release is never missed. Without an
 * interrupt line it polls every 20 ms.
 */
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/i2c.h>
#include <crtos/input.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/sched.h>
#include <crtos/sync.h>

#define MAX_POINTS  5
#define POLL_MS     20

struct ft5406
{
    struct device *dev;
    struct i2c_client *client;
    struct input_dev *input;
    int irq;
    struct semaphore sem;
    task_t *thread;
    int swap_xy;
    uint32_t size_x, size_y;
    int down;               /* a finger is on the glass */
    int dirty;              /* reported since the last sync */
    int32_t id[MAX_POINTS]; /* the finger (touch id) in each slot, -1: none */
    int32_t x[MAX_POINTS], y[MAX_POINTS];
    uint32_t reports;
};

static void ft_irq(int irq, void *ctx)
{
    (void)irq;
    struct ft5406 *ft = ctx;
    sem_give(&ft->sem);
}

static void report(struct ft5406 *ft, uint16_t type, uint16_t code, int32_t value)
{
    input_report(ft->input, type, code, value);
    ft->dirty = 1;
}

/* The slot of finger @id: the one it has, or a free one (-1: all taken) */
static int slot_of(struct ft5406 *ft, int32_t id)
{
    int free_slot = -1;
    for (int s = 0; s < MAX_POINTS; s++)
    {
        if (ft->id[s] == id)
            return s;
        if (ft->id[s] < 0 && free_slot < 0)
            free_slot = s;
    }
    return free_slot;
}

static void ft_report(struct ft5406 *ft, const uint8_t *buf)
{
    unsigned n = buf[2] & 0x0Fu;
    if (n > MAX_POINTS)
        n = 0;
    uint32_t seen = 0;
    for (unsigned i = 0; i < n; i++)
    {
        const uint8_t *pt = buf + 3 + 6 * i;
        uint32_t x = ((uint32_t)(pt[0] & 0x0Fu) << 8) | pt[1];
        uint32_t y = ((uint32_t)(pt[2] & 0x0Fu) << 8) | pt[3];
        uint32_t id = pt[2] >> 4;
        if (ft->swap_xy)
        {
            uint32_t t = x;
            x = y;
            y = t;
        }
        if (x >= ft->size_x)
            x = ft->size_x - 1;
        if (y >= ft->size_y)
            y = ft->size_y - 1;
        int s = slot_of(ft, (int32_t)id);
        if (s < 0 || (seen >> s & 1u))
            continue;
        seen |= 1u << s;
        int fresh = ft->id[s] != (int32_t)id;
        if (!fresh && ft->x[s] == (int32_t)x && ft->y[s] == (int32_t)y)
            continue; /* (nothing new about this finger) */
        report(ft, EV_ABS, ABS_MT_SLOT, s);
        if (fresh)
            report(ft, EV_ABS, ABS_MT_TRACKING_ID, (int32_t)id);
        report(ft, EV_ABS, ABS_MT_POSITION_X, (int32_t)x);
        report(ft, EV_ABS, ABS_MT_POSITION_Y, (int32_t)y);
        ft->id[s] = (int32_t)id;
        ft->x[s] = (int32_t)x;
        ft->y[s] = (int32_t)y;
    }
    /* the fingers not in the report are up */
    int first = -1;
    for (int s = 0; s < MAX_POINTS; s++)
    {
        if (ft->id[s] >= 0 && !(seen >> s & 1u))
        {
            report(ft, EV_ABS, ABS_MT_SLOT, s);
            report(ft, EV_ABS, ABS_MT_TRACKING_ID, -1);
            ft->id[s] = -1;
        }
        if (ft->id[s] >= 0 && first < 0)
            first = s;
    }
    /* the single-touch view: the finger in the lowest slot */
    if (first >= 0 && ft->dirty)
    {
        report(ft, EV_ABS, ABS_X, ft->x[first]);
        report(ft, EV_ABS, ABS_Y, ft->y[first]);
    }
    if ((first >= 0) != ft->down)
    {
        ft->down = first >= 0;
        report(ft, EV_KEY, BTN_TOUCH, ft->down);
    }
    if (!ft->dirty)
        return;
    input_sync(ft->input);
    ft->dirty = 0;
    ft->reports++;
}

static void ft_thread(void *arg)
{
    struct ft5406 *ft = arg;
    uint8_t buf[3 + 6 * MAX_POINTS];
    while (!task_should_stop())
    {
        uint32_t wait = (ft->irq < 0 || ft->down) ? POLL_MS : WAIT_FOREVER;
        if (sem_take(&ft->sem, wait) == -EINTR)
            break;
        uint8_t reg = 0;
        if (i2c_write_read(ft->client, &reg, 1, buf, sizeof(buf)) == 0)
            ft_report(ft, buf);
    }
}

static int ft_probe(struct device *dev)
{
    struct i2c_client *client = i2c_client_get(dev);
    if (!client)
        return -ENODEV;
    struct ft5406 *ft = devm_kzalloc(dev, sizeof(*ft), 0);
    if (!ft)
        return -ENOMEM;
    ft->dev = dev;
    for (int s = 0; s < MAX_POINTS; s++)
        ft->id[s] = -1;
    ft->client = client;
    ft->size_x = 480;
    ft->size_y = 272;
    of_property_read_u32(dev->of_node, "touchscreen-size-x", &ft->size_x);
    of_property_read_u32(dev->of_node, "touchscreen-size-y", &ft->size_y);
    ft->swap_xy = of_property_read_bool(dev->of_node, "touchscreen-swapped-x-y");

    ft->irq = device_get_irq(dev, 0);
    if (ft->irq == -EPROBE_DEFER)
        return ft->irq;

    /* the controller boots with the panel: give it a moment to answer */
    uint8_t reg = 0xA3, chip = 0; /* chip id register */
    int r = -EIO;
    for (int tries = 0; tries < 4 && r; tries++)
    {
        r = i2c_write_read(client, &reg, 1, &chip, 1);
        if (r)
            task_sleep_ms(30);
    }
    if (r)
        return -ENODEV; /* not this panel (the EVKB has one of two touch controllers) */

    ft->input = input_register("ft5406");
    if (!ft->input)
        return -ENOMEM;
    input_set_abs(ft->input, ABS_X, 0, (int32_t)ft->size_x - 1);
    input_set_abs(ft->input, ABS_Y, 0, (int32_t)ft->size_y - 1);
    input_set_key(ft->input, BTN_TOUCH);
    sem_init(&ft->sem, 0, 1);
    if (ft->irq >= 0 && irq_request(ft->irq, ft_irq, ft, 0, "ft5406"))
        ft->irq = -1;
    ft->thread = kthread_create("ft5406", ft_thread, ft, PRIO_HIGH, 1536);
    if (!ft->thread)
    {
        if (ft->irq >= 0)
            irq_free(ft->irq);
        input_unregister(ft->input);
        return -ENOMEM;
    }
    dev_set_drvdata(dev, ft);
    dev_info(dev, "FT5406 touch (chip id 0x%02x), %lux%lu%s, %s\n", chip, (unsigned long)ft->size_x,
             (unsigned long)ft->size_y, ft->swap_xy ? " swapped" : "", ft->irq >= 0 ? "interrupt" : "polled");
    return 0;
}

static void ft_remove(struct device *dev)
{
    struct ft5406 *ft = dev_get_drvdata(dev);
    kthread_stop(ft->thread);
    if (ft->irq >= 0)
        irq_free(ft->irq);
    input_unregister(ft->input);
}

static const struct of_device_id ft_ids[] = {
    {"focaltech,ft5406", NULL},
    {NULL, NULL},
};

static struct driver ft_driver = {
    .name = "ft5406",
    .of_match_table = ft_ids,
    .probe = ft_probe,
    .remove = ft_remove,
};

static int init(void)
{
    return driver_register(&ft_driver);
}

static void fini(void)
{
    driver_unregister(&ft_driver);
}

MODULE("ft5406", "FT5406 capacitive touch screen", init, fini);
