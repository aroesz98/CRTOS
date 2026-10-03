/*
 * esp32-pad.ko - a DualSense / DualSense Edge pad over Bluetooth, through the ESP32 bridge.
 *
 * The bridge (esp32-s3-bt-mod/ in this repository, a Zephyr application on a classic ESP32,
 * not changed here) talks Bluetooth Classic HID to the pad and sends its state as a binary
 * frame on its UART2, wired to LPUART3 of the board (J22: ESP32 GPIO17 -> D0, GND -> GND):
 *
 *     a5 5a  buttons[7:0] buttons[15:8] buttons[23:16]  lx ly rx ry  l2 r2  seq  xor(2..11)
 *
 * 460800 baud 8N1; a frame whenever a button changes, stick movement at most every 8 ms, an
 * unchanged state every 200 ms (so a silent line means the bridge is gone), and an "all
 * released" frame when the pad disconnects.
 *
 * This driver reads the serial port (a file of lpuart-imxrt, /dev/ttyS3) from a kernel thread
 * and reports the buttons as an input device, /dev/eventN, with the Linux gamepad codes:
 *
 *     cross BTN_SOUTH, circle BTN_EAST, triangle BTN_NORTH, square BTN_WEST,
 *     L1 BTN_TL, R1 BTN_TR, L2 BTN_TL2, R2 BTN_TR2, L3 BTN_THUMBL, R3 BTN_THUMBR,
 *     create BTN_SELECT, options BTN_START, PS BTN_MODE, d-pad BTN_DPAD_*,
 *     touch pad click BTN_TRIGGER_HAPPY1, mute ..._HAPPY2, Edge: Fn left/right ..._HAPPY3/4,
 *     back left/right ..._HAPPY5/6.
 *
 * The sticks and the triggers are reported as axes too, the Linux gamepad way: left stick
 * ABS_X / ABS_Y and right stick ABS_RX / ABS_RY from -128 to 127 (right and down positive,
 * 0 at rest), L2 ABS_Z and R2 ABS_RZ from 0 to 255; a change of 1 (the sticks' noise at rest)
 * is not reported, a return to rest always. Within a report the axes come before the keys.
 *
 * The left stick pushed far enough counts as the d-pad as well (with some hysteresis): enough
 * for the menus and for games without analogue control. The input service passes the keys
 * and the sticks to the window that has the focus, like those of a keyboard.
 *
 * Device tree: compatible "crtos,esp32-pad"; port (default "/dev/ttyS3"), current-speed
 * (default 460800). The port belongs to this driver: other readers would take its bytes.
 */
#include <stdbool.h>
#include <string.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/input.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/tty.h>
#include <crtos/vfs.h>

#define FRAME_LEN 13
#define SYNC0 0xa5
#define SYNC1 0x5a
#define POLL_MS 2    /* between reads of an empty line */
#define QUIET_MS 700 /* no frame for this long: the bridge is gone, release all */
#define STICK_ON 56  /* stick as d-pad: pressed beyond this from the centre */
#define STICK_OFF 40 /* and released below this */
#define NAXES 6      /* lx ly rx ry l2 r2, as in the frame */

static const uint16_t AXES[NAXES] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};

/* the bridge's bits (enum ds_button in esp32-s3-bt-mod/src/dualsense.c) */
enum
{
    DS_DPAD_UP,
    DS_DPAD_RIGHT,
    DS_DPAD_DOWN,
    DS_DPAD_LEFT,
    DS_SQUARE,
    DS_CROSS,
    DS_CIRCLE,
    DS_TRIANGLE,
    DS_L1,
    DS_R1,
    DS_L2,
    DS_R2,
    DS_CREATE,
    DS_OPTIONS,
    DS_L3,
    DS_R3,
    DS_PS,
    DS_TOUCHPAD,
    DS_MUTE,
    DS_RESERVED,
    DS_FN_LEFT,
    DS_FN_RIGHT,
    DS_BACK_LEFT,
    DS_BACK_RIGHT,
};

/* bit of our state word -> key code; the d-pad first (merged with the stick) */
static const uint16_t KEYS[] = {
    BTN_DPAD_UP,        /* 0 */
    BTN_DPAD_RIGHT,     /* 1 */
    BTN_DPAD_DOWN,      /* 2 */
    BTN_DPAD_LEFT,      /* 3 */
    BTN_WEST,           /* 4 square */
    BTN_SOUTH,          /* 5 cross */
    BTN_EAST,           /* 6 circle */
    BTN_NORTH,          /* 7 triangle */
    BTN_TL,             /* 8 */
    BTN_TR,             /* 9 */
    BTN_TL2,            /* 10 */
    BTN_TR2,            /* 11 */
    BTN_SELECT,         /* 12 create */
    BTN_START,          /* 13 options */
    BTN_THUMBL,         /* 14 */
    BTN_THUMBR,         /* 15 */
    BTN_MODE,           /* 16 PS */
    BTN_TRIGGER_HAPPY1, /* 17 touch pad click */
    BTN_TRIGGER_HAPPY2, /* 18 mute */
    0,                  /* 19 (reserved) */
    BTN_TRIGGER_HAPPY3, /* 20 Fn left */
    BTN_TRIGGER_HAPPY4, /* 21 Fn right */
    BTN_TRIGGER_HAPPY5, /* 22 back left */
    BTN_TRIGGER_HAPPY6, /* 23 back right */
};
#define NKEYS ((int)(sizeof(KEYS) / sizeof(KEYS[0])))

struct pad
{
    struct device *dev;
    const char *port;
    uint32_t baud;
    struct input_dev *input;
    task_t *thread;
    struct file *f;
    uint8_t frame[FRAME_LEN];
    int len;
    uint32_t reported;   /* the state the input device shows */
    int16_t axis[NAXES]; /* the axes it shows */
    uint32_t stick;      /* left stick as d-pad bits (0..3) */
    uint32_t quiet_ms;
    uint32_t frames, bad;
    bool alive;
};

/* one axis of the left stick as two d-pad bits, with hysteresis */
static uint32_t stick_axis(uint32_t held, int v, uint32_t neg, uint32_t pos)
{
    int d = v - 0x80;
    uint32_t out = 0;
    if (d <= -STICK_ON || (d <= -STICK_OFF && (held & neg)))
        out |= neg;
    if (d >= STICK_ON || (d >= STICK_OFF && (held & pos)))
        out |= pos;
    return out;
}

/* the axes from a frame's bytes (NULL: all at rest), then the buttons; one report */
static void report(struct pad *p, const uint8_t *raw, uint32_t state)
{
    bool any = false;
    for (int i = 0; i < NAXES; i++)
    {
        int v = i < 4 ? (raw ? raw[i] - 0x80 : 0) : (raw ? raw[i] : 0);
        int d = v - p->axis[i];
        if (d == 0 || ((d == 1 || d == -1) && v != 0 && v != 255 && v != -128 && v != 127))
            continue; /* (noise; rest and the ends always count) */
        input_report(p->input, EV_ABS, AXES[i], v);
        p->axis[i] = (int16_t)v;
        any = true;
    }
    uint32_t changed = state ^ p->reported;
    for (int i = 0; i < NKEYS; i++)
        if ((changed >> i & 1) && KEYS[i])
            input_report(p->input, EV_KEY, KEYS[i], (int32_t)(state >> i & 1));
    if (any || changed)
        input_sync(p->input);
    p->reported = state;
}

static void frame_done(struct pad *p)
{
    const uint8_t *f = p->frame;
    uint8_t check = 0;
    for (int i = 2; i < FRAME_LEN - 1; i++)
        check ^= f[i];
    if (check != f[FRAME_LEN - 1])
    {
        p->bad++;
        return;
    }
    p->frames++;
    if (!p->alive)
    {
        p->alive = true;
        dev_info(p->dev, "pad link up (%lu frames, %lu bad)\n", (unsigned long)p->frames, (unsigned long)p->bad);
    }
    uint32_t buttons = (uint32_t)f[2] | (uint32_t)f[3] << 8 | (uint32_t)f[4] << 16;
    /* stick y: larger is down */
    p->stick = stick_axis(p->stick, f[5], 1u << 3, 1u << 1) | stick_axis(p->stick, f[6], 1u << 0, 1u << 2);
    report(p, f + 5, buttons | p->stick);
}

static void feed(struct pad *p, const uint8_t *buf, int n)
{
    for (int i = 0; i < n; i++)
    {
        uint8_t b = buf[i];
        if (p->len == 0)
        {
            if (b == SYNC0)
                p->frame[p->len++] = b;
        }
        else if (p->len == 1)
        {
            if (b == SYNC1)
                p->frame[p->len++] = b;
            else
                p->len = b == SYNC0; /* a5 a5 5a: the second a5 may start the frame */
        }
        else
        {
            p->frame[p->len++] = b;
            if (p->len == FRAME_LEN)
            {
                p->len = 0;
                frame_done(p);
            }
        }
    }
}

static void pad_thread(void *arg)
{
    struct pad *p = arg;
    uint8_t buf[64];
    while (!task_should_stop())
    {
        if (!p->f)
        {
            if (vfs_open(p->port, VFS_O_RDWR | VFS_O_NONBLOCK, &p->f) < 0)
            {
                p->f = NULL; /* the serial port driver may still be loading */
                task_sleep_ms(500);
                continue;
            }
            vfs_ioctl(p->f, TTY_IOC_SET_SPEED, (void *)(uintptr_t)p->baud);
            while (vfs_read(p->f, buf, sizeof(buf)) > 0)
            {
                /* what came before, at another speed */
            }
            dev_info(p->dev, "listening on %s at %lu baud\n", p->port, (unsigned long)p->baud);
        }
        int n = vfs_read(p->f, buf, sizeof(buf));
        if (n > 0)
        {
            feed(p, buf, n);
            p->quiet_ms = 0;
            continue;
        }
        if (n != -EAGAIN)
        {
            dev_err(p->dev, "%s: %d\n", p->port, n);
            vfs_close(p->f);
            p->f = NULL;
            task_sleep_ms(500);
            continue;
        }
        task_sleep_ms(POLL_MS);
        p->quiet_ms += POLL_MS;
        if (p->quiet_ms >= QUIET_MS && p->alive)
        {
            p->alive = false;
            p->stick = 0;
            report(p, NULL, 0); /* nothing stays pressed or pushed when the bridge goes away */
            dev_info(p->dev, "pad link quiet\n");
        }
    }
    if (p->f)
        vfs_close(p->f);
}

static int pad_probe(struct device *dev)
{
    struct pad *p = devm_kzalloc(dev, sizeof(*p), 0);
    if (!p)
        return -ENOMEM;
    p->dev = dev;
    if (of_property_read_string(dev->of_node, "port", &p->port))
        p->port = "/dev/ttyS3";
    if (of_property_read_u32(dev->of_node, "current-speed", &p->baud))
        p->baud = 460800;
    p->input = input_register("DualSense (ESP32 link)");
    if (!p->input)
        return -ENOMEM;
    for (int i = 0; i < NKEYS; i++)
        if (KEYS[i])
            input_set_key(p->input, KEYS[i]);
    for (int i = 0; i < NAXES; i++)
        input_set_abs(p->input, AXES[i], i < 4 ? -128 : 0, i < 4 ? 127 : 255);
    p->thread = kthread_create("esp32-pad", pad_thread, p, PRIO_HIGH, 1536);
    if (!p->thread)
    {
        input_unregister(p->input);
        return -ENOMEM;
    }
    dev_set_drvdata(dev, p);
    dev_info(dev, "%s, %s\n", input_devname(p->input), p->port);
    return 0;
}

static void pad_remove(struct device *dev)
{
    struct pad *p = dev_get_drvdata(dev);
    kthread_stop(p->thread);
    input_unregister(p->input);
}

static const struct of_device_id pad_ids[] = {
    {"crtos,esp32-pad", NULL},
    {NULL, NULL},
};

static struct driver pad_driver = {
    .name = "esp32-pad",
    .of_match_table = pad_ids,
    .probe = pad_probe,
    .remove = pad_remove,
};

static int init(void)
{
    return driver_register(&pad_driver);
}

static void fini(void)
{
    driver_unregister(&pad_driver);
}

MODULE("esp32-pad", "DualSense pad through the ESP32 Bluetooth bridge", init, fini);
