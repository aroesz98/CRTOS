/*
 * touchpaint.ko - hardware check for the display, touch screen and user button.
 *
 * Draws colour bars and a canvas on /dev/fb0, paints where the screen is touched and clears
 * the canvas when SW8 (KEY_HOME) is pressed. It binds to the test node "crtos,touchpaint"
 * of the device tree; remove that node once the graphics server takes over the display.
 */
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/fb.h>
#include <crtos/gpu2d.h>
#include <crtos/input.h>
#include <crtos/module.h>
#include <crtos/sched.h>
#include <crtos/vfs.h>
#include <string.h>

#define BAR_H 24

struct paint
{
    task_t *thread;
    struct fb_info fi;
    struct gpu2d_surface s;
};

static void rect(struct paint *p, int x, int y, int w, int h, uint32_t rgb)
{
    struct gpu2d_rect r = {(int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h};
    if (gpu2d_fill(&p->s, &r, rgb) == 0)
        return;
    /* no accelerator: draw with the CPU */
    uint16_t c = (uint16_t)(((rgb >> 8) & 0xF800u) | ((rgb >> 5) & 0x07E0u) | ((rgb >> 3) & 0x001Fu));
    for (int j = y; j < y + h; j++)
    {
        if (j < 0 || j >= p->fi.height)
            continue;
        uint16_t *row = (uint16_t *)(p->fi.buffer[0] + (uint32_t)j * p->fi.stride);
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < p->fi.width)
                row[i] = c;
    }
}

static void draw_screen(struct paint *p)
{
    static const uint32_t bars[8] = {0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x000000};
    int w = p->fi.width, h = p->fi.height;
    for (int i = 0; i < 8; i++)
        rect(p, i * w / 8, 0, w / 8 + 1, BAR_H, bars[i]);
    rect(p, 0, BAR_H, w, h - BAR_H, 0x102040);
    rect(p, 0, BAR_H, w, 2, 0xFFFFFF);
}

static struct file *open_input(const char *want)
{
    for (int i = 0; i < 8; i++)
    {
        char path[20] = "/dev/event0";
        path[10] = (char)('0' + i);
        struct file *f;
        if (vfs_open(path, VFS_O_RDONLY | VFS_O_NONBLOCK, &f))
            continue;
        char name[32] = "";
        vfs_ioctl(f, INPUT_IOC_GET_NAME, name);
        if (strstr(want, name) && name[0])
            return f;
        vfs_close(f);
    }
    return NULL;
}

static void paint_thread(void *arg)
{
    struct paint *p = arg;
    /* the display driver may be loaded after us */
    while (fb_get_info(0, &p->fi))
    {
        if (task_sleep_ms(100))
            return;
    }
    if (p->fi.format != FB_FMT_RGB565)
        return;
    p->s.addr = p->fi.buffer[0];
    p->s.width = p->fi.width;
    p->s.height = p->fi.height;
    p->s.stride = p->fi.stride;
    p->s.format = GPU2D_FMT_RGB565;
    fb_show(0, 0);
    draw_screen(p);
    printk("touchpaint: drawing on fb0, touch to paint, SW8 clears\n");

    struct file *touch = NULL, *keys = NULL;
    int x = -1, y = -1, down = 0, color = 0;
    static const uint32_t palette[4] = {0xFFD000, 0x40FF40, 0xFF4080, 0x40C0FF};
    while (!task_should_stop())
    {
        if (!touch)
            touch = open_input("gt911 ft5406");
        if (!keys)
            keys = open_input("gpio-keys");
        struct input_event ev[16];
        int idle = 1;
        int r;
        while (touch && (r = vfs_read(touch, ev, sizeof(ev))) > 0)
        {
            idle = 0;
            for (int i = 0; i < r / (int)sizeof(ev[0]); i++)
            {
                if (ev[i].type == EV_ABS && ev[i].code == ABS_X)
                    x = ev[i].value;
                else if (ev[i].type == EV_ABS && ev[i].code == ABS_Y)
                    y = ev[i].value;
                else if (ev[i].type == EV_KEY && ev[i].code == BTN_TOUCH)
                {
                    down = ev[i].value;
                    if (down)
                        color = (color + 1) & 3;
                }
                else if (ev[i].type == EV_SYN && down && x >= 0 && y >= BAR_H + 3)
                    rect(p, x - 3, y - 3, 7, 7, palette[color]);
            }
        }
        while (keys && (r = vfs_read(keys, ev, sizeof(ev))) > 0)
        {
            idle = 0;
            for (int i = 0; i < r / (int)sizeof(ev[0]); i++)
                if (ev[i].type == EV_KEY && ev[i].value == 1)
                    draw_screen(p);
        }
        if (idle && task_sleep_ms(10))
            break;
    }
    if (touch)
        vfs_close(touch);
    if (keys)
        vfs_close(keys);
}

static int paint_probe(struct device *dev)
{
    struct paint *p = devm_kzalloc(dev, sizeof(*p), 0);
    if (!p)
        return -ENOMEM;
    p->thread = kthread_create("touchpaint", paint_thread, p, PRIO_NORMAL, 2048);
    if (!p->thread)
        return -ENOMEM;
    dev_set_drvdata(dev, p);
    return 0;
}

static void paint_remove(struct device *dev)
{
    struct paint *p = dev_get_drvdata(dev);
    kthread_stop(p->thread);
}

static const struct of_device_id paint_ids[] = {
    {"crtos,touchpaint", NULL},
    {NULL, NULL},
};

static struct driver paint_driver = {
    .name = "touchpaint",
    .of_match_table = paint_ids,
    .probe = paint_probe,
    .remove = paint_remove,
};

static int init(void)
{
    return driver_register(&paint_driver);
}

static void fini(void)
{
    driver_unregister(&paint_driver);
}

MODULE("touchpaint", "display/touch/button check", init, fini);
