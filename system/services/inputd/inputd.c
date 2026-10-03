/*
 * inputd - input service (layer 2).
 *
 * Opens the input devices the device manager reports (/dev/eventN), turns touch screen
 * reports into pointer events (down / move / up) in screen coordinates and passes them,
 * and key events, to the graphics server. Devices may come and go; gfxd may start later
 * or restart - inputd reconnects.
 *
 * Multi-touch screens (ABS_MT_* slots, Linux protocol B) send every finger: from its press
 * to its lift it has a number (the code of its pointer events), the lowest one free at the
 * press, so the first finger on an empty screen is 0 (gfx_proto.h). Within a report the
 * lifts go first, then the presses and the moves. Screens without slots send finger 0.
 *
 * Mice (relative motion, USB) move a cursor - a small window that pointer input goes
 * through - and the left button acts as a finger: pressed, moved, lifted, marked as a mouse
 * (GFX_PTR_MOUSE: a program may select with it rather than scroll). A move without the
 * button goes as GFX_PTR_HOVER (tooltips, highlights under the cursor). The wheels turn
 * (GFX_WHEEL) at the cursor: gfxd gives them to the window under it.
 *
 * Gamepads (devices with BTN_SOUTH) send their buttons as keys and their sticks and triggers
 * (ABS_X/Y, ABS_RX/RY, ABS_Z/RZ) as GFX_STICK, scaled to +-32767 (triggers 0..32767). A
 * stick goes at most every STICK_US (the pad reports every 8 ms while it moves), its last
 * position always; within a report the sticks go before the buttons.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/input.h>
#include "devmgr_proto.h"
#include "gfx.h"
#include "gfx_proto.h"

#define MAX_DEV 8
#define MT_SLOTS 10         /* multi-touch slots of a device */
#define STICK_US 20000u     /* at most 50 positions a second a stick: the programs' event
                             * queues (32 messages) keep room for keys */

struct stick {
    int16_t x, y;
    bool dirty;                 /* moved since it was last sent */
    uint64_t sent_us;
};

struct slot {
    int32_t id;                 /* the device's tracking id, -1: no finger */
    int x, y;
    bool moved;                 /* its position changed in this report */
    bool renew;                 /* another finger took the slot in this report */
    int finger;                 /* its number sent to gfxd, -1: none */
};

struct dev {
    bool used;
    char name[24];
    int fd;
    bool touch;                 /* has absolute axes */
    struct input_absinfo ax, ay;
    int x, y;
    bool down, sent_down, moved;
    bool mt;                    /* reports multi-touch slots */
    int slot;                   /* the slot the ABS_MT_* events are about */
    struct slot mts[MT_SLOTS];
    bool mouse;                 /* reports relative motion or mouse buttons */
    int dx, dy, wheel, hwheel;  /* its motion since the last report */
    bool left;                  /* its left button, as last reported */
    bool pad;                   /* a gamepad */
    struct input_absinfo abs[INPUT_ABS_AXES];
    struct stick stick[GFX_STICKS];
    uint16_t kcode[16];         /* the buttons of the report being read */
    int32_t kvalue[16];
    int nkeys;
};

static void send_input(uint16_t kind, uint16_t code, int x, int y, int32_t value);

static struct dev s_dev[MAX_DEV];
static unsigned s_fingers;          /* the finger numbers in use, a bit each */
static int s_gfx = -1;
static int s_w = 480, s_h = 272;

/* ---- the mouse cursor: shared by all mice ---------------------------------------------------- */

#define CURSOR_W 12
#define CURSOR_H 19

static const char *const s_arrow[CURSOR_H] = {
    "X...........", "XX..........", "XoX.........", "XooX........", "XoooX.......", "XooooX......",
    "XoooooX.....", "XooooooX....", "XoooooooX...", "XooooooooX..", "XoooooooooX.", "XooooooXXXXX",
    "XoooXooX....", "XooXXooX....", "XoX..XooX...", "XX...XooX...", "X.....XooX..", "......XooX..",
    ".......XX...",
};

static struct gfx *s_cg;            /* our own connection for the cursor window */
static struct gfx_win *s_cursor;
static int s_mx = -1, s_my;         /* the cursor's hot spot (-1: no mouse used yet) */
static bool s_mdown;                /* the left button holds a "finger" down */

static void cursor_show(void)
{
    if (!s_cursor) {
        s_cg = gfx_open();
        if (!s_cg)
            return;
        s_cursor = gfx_win_create(s_cg, s_mx, s_my, CURSOR_W, CURSOR_H,
                                  GFX_WIN_ALPHA | GFX_WIN_TOPMOST | GFX_WIN_NOFOCUS | GFX_WIN_NOFRAME |
                                      GFX_WIN_NOINPUT, "cursor");
        if (!s_cursor)
            return;
        struct gfx_surface *s = &s_cursor->s;
        gfx_fill(s, 0, 0, CURSOR_W, CURSOR_H, GFX_TRANSPARENT);
        for (int y = 0; y < CURSOR_H; y++)
            for (int x = 0; x < CURSOR_W; x++)
                if (s_arrow[y][x] != '.')
                    gfx_pixel(s, x, y, s_arrow[y][x] == 'X' ? GFX_BLACK : GFX_WHITE);
        gfx_present(s_cursor);
        return;
    }
    gfx_win_move(s_cursor, s_mx, s_my);
    gfx_win_raise(s_cursor);    /* over the keyboard and menus that came up meanwhile */
}

/* A mouse report is complete: move the cursor, press or lift the "finger", scroll */
static void mouse_sync(struct dev *d)
{
    bool left = d->left;
    if (s_mx < 0) {
        s_mx = s_w / 2;
        s_my = s_h / 2;
    }
    bool moved = d->dx || d->dy;
    s_mx += d->dx;
    s_my += d->dy;
    s_mx = s_mx < 0 ? 0 : s_mx >= s_w ? s_w - 1 : s_mx;
    s_my = s_my < 0 ? 0 : s_my >= s_h ? s_h - 1 : s_my;
    d->dx = d->dy = 0;
    if (moved || !s_cursor)
        cursor_show();
    struct gfx_event ev;
    while (s_cg && gfx_next_event(s_cg, &ev, 0) == 0) { /* the cursor window's: none needed */
    }
    if (left && !s_mdown) {
        send_input(GFX_PTR_DOWN, 0, s_mx, s_my, GFX_PTR_MOUSE);
        s_mdown = true;
    } else if (!left && s_mdown) {
        send_input(GFX_PTR_UP, 0, s_mx, s_my, GFX_PTR_MOUSE);
        s_mdown = false;
    } else if (moved) { /* without the button: for windows that follow the cursor (tooltips) */
        send_input(s_mdown ? GFX_PTR_MOVE : GFX_PTR_HOVER, 0, s_mx, s_my, GFX_PTR_MOUSE);
    }
    if (d->wheel)
        send_input(GFX_WHEEL, GFX_WHEEL_VERTICAL, s_mx, s_my, d->wheel);
    if (d->hwheel)
        send_input(GFX_WHEEL, GFX_WHEEL_HORIZONTAL, s_mx, s_my, d->hwheel);
    d->wheel = d->hwheel = 0;
}

static void gfx_connect(void)
{
    int port = crtos_port_connect(GFX_PORT_NAME, 0);
    if (port < 0)
        return;
    struct gfx_hdr req = { GFX_HELLO, sizeof(req) };
    struct gfx_hello_rep rep;
    if (crtos_msg_call(port, &req, sizeof(req), &rep, sizeof(rep), 1000) == (int)sizeof(rep)) {
        s_w = rep.width;
        s_h = rep.height;
        s_gfx = port;
    } else {
        close(port);
    }
}

static void send_input(uint16_t kind, uint16_t code, int x, int y, int32_t value)
{
    if (s_gfx < 0)
        return;
    struct gfx_input in = { { GFX_INPUT, sizeof(in) }, kind, code, (int16_t)x, (int16_t)y, value,
                            (uint32_t)(crtos_time_us() / 1000u) };
    if (crtos_msg_send(s_gfx, &in, sizeof(in), -1, 100) < 0 && errno == EPIPE) {
        close(s_gfx); /* gfxd ended: connect again later */
        s_gfx = -1;
    }
}

/* ---- gamepads -------------------------------------------------------------------------------------- */

/* an axis value as -32767..32767 around 0 (a range with negative values: a stick) or
 * 0..32767 (a trigger) */
static int16_t axis_scale(int v, const struct input_absinfo *a)
{
    if (a->maximum <= a->minimum)
        return 0;
    long s;
    if (a->minimum < 0)
        s = v >= 0 ? (long)v * 32767 / (a->maximum > 0 ? a->maximum : 1) : -((long)v * 32767 / a->minimum);
    else
        s = (long)(v - a->minimum) * 32767 / (a->maximum - a->minimum);
    return (int16_t)(s > 32767 ? 32767 : s < -32767 ? -32767 : s);
}

static void pad_axis(struct dev *d, int code, int value)
{
    static const int8_t STICK[INPUT_ABS_AXES] = { 0, 0, 2, 1, 1, 2 };   /* X Y Z RX RY RZ */
    static const int8_t YAXIS[INPUT_ABS_AXES] = { 0, 1, 0, 0, 1, 1 };
    if (code < 0 || code >= INPUT_ABS_AXES)
        return;
    struct stick *s = &d->stick[STICK[code]];
    int16_t v = axis_scale(value, &d->abs[code]);
    if (YAXIS[code])
        s->y = v;
    else
        s->x = v;
    s->dirty = true;
}

/* send the sticks that moved (@force: now, else not sooner than STICK_US after the last) */
static void pad_flush(struct dev *d, uint64_t now, bool force)
{
    for (unsigned i = 0; i < GFX_STICKS; i++) {
        struct stick *s = &d->stick[i];
        if (!s->dirty || (!force && now - s->sent_us < STICK_US))
            continue;
        send_input(GFX_STICK, (uint16_t)i, s->x, s->y, 0);
        s->dirty = false;
        s->sent_us = now;
    }
}

/* ms until a held back stick is due, -1: none */
static int pad_due_ms(uint64_t now)
{
    int ms = -1;
    for (int i = 0; i < MAX_DEV; i++) {
        if (!s_dev[i].used || !s_dev[i].pad)
            continue;
        for (unsigned k = 0; k < GFX_STICKS; k++) {
            const struct stick *s = &s_dev[i].stick[k];
            if (!s->dirty)
                continue;
            uint64_t due = s->sent_us + STICK_US;
            int m = due > now ? (int)((due - now + 999u) / 1000u) : 0;
            if (ms < 0 || m < ms)
                ms = m;
        }
    }
    return ms;
}

static int scale(int v, const struct input_absinfo *a, int size)
{
    if (a->maximum <= a->minimum)
        return v;
    int s = (v - a->minimum) * (size - 1) / (a->maximum - a->minimum);
    return s < 0 ? 0 : s >= size ? size - 1 : s;
}

/* ---- multi-touch --------------------------------------------------------------------------------- */

static void mt_event(struct dev *d, uint16_t code, int32_t value)
{
    d->mt = true;
    if (code == ABS_MT_SLOT) {
        d->slot = value >= 0 && value < MT_SLOTS ? value : -1;
        return;
    }
    if (d->slot < 0)
        return;
    struct slot *t = &d->mts[d->slot];
    if (code == ABS_MT_TRACKING_ID) {
        if (value >= 0 && t->id >= 0 && value != t->id)
            t->renew = true;
        t->id = value;
    } else if (code == ABS_MT_POSITION_X) {
        t->x = value;
        t->moved = true;
    } else if (code == ABS_MT_POSITION_Y) {
        t->y = value;
        t->moved = true;
    }
}

static void finger_up(struct dev *d, struct slot *t)
{
    send_input(GFX_PTR_UP, (uint16_t)t->finger, scale(t->x, &d->ax, s_w), scale(t->y, &d->ay, s_h), 0);
    s_fingers &= ~(1u << t->finger);
    t->finger = -1;
}

/* A report is complete: the lifts, then the presses and the moves */
static void mt_sync(struct dev *d)
{
    for (int i = 0; i < MT_SLOTS; i++) {
        struct slot *t = &d->mts[i];
        if (t->finger >= 0 && (t->id < 0 || t->renew))
            finger_up(d, t);
        t->renew = false;
    }
    for (int i = 0; i < MT_SLOTS; i++) {
        struct slot *t = &d->mts[i];
        int x = scale(t->x, &d->ax, s_w), y = scale(t->y, &d->ay, s_h);
        if (t->id >= 0 && t->finger < 0) {
            int f = 0;
            while (f < (int)GFX_FINGERS && (s_fingers >> f & 1u))
                f++;
            if (f < (int)GFX_FINGERS) { /* (a sixth finger is not told) */
                s_fingers |= 1u << f;
                t->finger = f;
                send_input(GFX_PTR_DOWN, (uint16_t)f, x, y, 0);
            }
        } else if (t->id >= 0 && t->moved) {
            send_input(GFX_PTR_MOVE, (uint16_t)t->finger, x, y, 0);
        }
        t->moved = false;
    }
}

static void add_device(const char *name)
{
    struct dev *d = NULL;
    for (int i = 0; i < MAX_DEV; i++) {
        if (s_dev[i].used && !strcmp(s_dev[i].name, name))
            return;
        if (!s_dev[i].used && !d)
            d = &s_dev[i];
    }
    if (!d)
        return;
    char path[40];
    snprintf(path, sizeof(path), "/dev/%s", name);
    int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("inputd: %s: %s\n", path, strerror(errno));
        return;
    }
    memset(d, 0, sizeof(*d));
    d->used = true;
    d->fd = fd;
    for (int i = 0; i < MT_SLOTS; i++) {
        d->mts[i].id = -1;
        d->mts[i].finger = -1;
    }
    snprintf(d->name, sizeof(d->name), "%s", name);
    static uint8_t keybits[INPUT_KEYBITS_SIZE];
    memset(keybits, 0, sizeof(keybits));
    ioctl(fd, INPUT_IOC_GET_KEYBITS, keybits);
    d->pad = input_has_key(keybits, BTN_SOUTH);
    for (int a = 0; d->pad && a < INPUT_ABS_AXES; a++)
        ioctl(fd, INPUT_IOC_GET_ABS(a), &d->abs[a]);
    d->touch = !d->pad && ioctl(fd, INPUT_IOC_GET_ABS_X, &d->ax) == 0 &&
               ioctl(fd, INPUT_IOC_GET_ABS_Y, &d->ay) == 0 && d->ax.maximum > d->ax.minimum;
    char devname[INPUT_NAME_MAX] = "";
    ioctl(fd, INPUT_IOC_GET_NAME, devname);
    printf("inputd: %s (%s)%s\n", name, devname, d->touch ? ", touch screen" : d->pad ? ", gamepad" : "");
}

static void remove_device(const char *name)
{
    for (int i = 0; i < MAX_DEV; i++)
        if (s_dev[i].used && !strcmp(s_dev[i].name, name)) {
            for (int k = 0; k < MT_SLOTS; k++) /* its fingers are up */
                if (s_dev[i].mts[k].finger >= 0)
                    finger_up(&s_dev[i], &s_dev[i].mts[k]);
            close(s_dev[i].fd);
            s_dev[i].used = false;
        }
}

static void read_device(struct dev *d)
{
    struct input_event ev[16];
    int n;
    while ((n = read(d->fd, ev, sizeof(ev))) > 0) {
        for (int i = 0; i < n / (int)sizeof(ev[0]); i++) {
            const struct input_event *e = &ev[i];
            if (d->pad) {
                if (e->type == EV_ABS) {
                    pad_axis(d, e->code, e->value);
                } else if (e->type == EV_KEY && d->nkeys < 16) {
                    d->kcode[d->nkeys] = e->code;
                    d->kvalue[d->nkeys++] = e->value;
                } else if (e->type == EV_SYN) {
                    /* the buttons after the sticks of the same report (a program can tell a
                     * d-pad the pad makes of the left stick from a real one) */
                    pad_flush(d, crtos_time_us(), d->nkeys > 0);
                    for (int k = 0; k < d->nkeys; k++)
                        send_input(GFX_KEY, d->kcode[k], 0, 0, d->kvalue[k]);
                    d->nkeys = 0;
                }
                continue;
            }
            if (e->type == EV_ABS && d->touch &&
                (e->code == ABS_MT_SLOT || e->code == ABS_MT_TRACKING_ID || e->code == ABS_MT_POSITION_X ||
                 e->code == ABS_MT_POSITION_Y)) {
                mt_event(d, e->code, e->value);
            } else if (e->type == EV_ABS && e->code == ABS_X) {
                d->x = e->value;
                d->moved = true;
            } else if (e->type == EV_ABS && e->code == ABS_Y) {
                d->y = e->value;
                d->moved = true;
            } else if (e->type == EV_REL) {
                if (e->code == REL_X)
                    d->dx += e->value;
                else if (e->code == REL_Y)
                    d->dy += e->value;
                else if (e->code == REL_WHEEL)
                    d->wheel += e->value;
                else if (e->code == REL_HWHEEL)
                    d->hwheel += e->value;
                d->mouse = true;
            } else if (e->type == EV_KEY && e->code == BTN_TOUCH) {
                d->down = e->value != 0;
            } else if (e->type == EV_KEY && e->code == BTN_LEFT) {
                d->left = e->value != 0;
                d->mouse = true;
            } else if (e->type == EV_KEY && (e->code == BTN_RIGHT || e->code == BTN_MIDDLE)) {
                d->mouse = true; /* not used (yet) */
            } else if (e->type == EV_KEY) {
                send_input(GFX_KEY, e->code, 0, 0, e->value);
            } else if (e->type == EV_SYN && d->mouse) {
                mouse_sync(d);
            } else if (e->type == EV_SYN && d->touch && d->mt) {
                mt_sync(d);
            } else if (e->type == EV_SYN && d->touch) {
                int x = scale(d->x, &d->ax, s_w), y = scale(d->y, &d->ay, s_h);
                if (d->down && !d->sent_down) {
                    send_input(GFX_PTR_DOWN, 0, x, y, 0);
                    d->sent_down = true;
                } else if (d->down && d->moved) {
                    send_input(GFX_PTR_MOVE, 0, x, y, 0);
                } else if (!d->down && d->sent_down) {
                    send_input(GFX_PTR_UP, 0, x, y, 0);
                    d->sent_down = false;
                }
                d->moved = false;
            }
        }
    }
    if (n < 0 && errno == ENODEV)
        remove_device(d->name);
}

int main(void)
{
    int devmgr = crtos_port_connect(DEVMGR_PORT_NAME, 10000);
    int events = crtos_port_create(NULL);
    if (devmgr < 0 || events < 0) {
        printf("inputd: no device manager (%s)\n", strerror(errno));
        return 1;
    }
    struct devmgr_subscribe sub = { DEVMGR_SUBSCRIBE, sizeof(sub), "event" };
    int32_t r = -1;
    struct crtos_call call = { &sub, sizeof(sub), events, &r, sizeof(r), -1 };
    if (crtos_msg_call2(devmgr, &call, 2000) != (int)sizeof(r) || r) {
        printf("inputd: subscription failed\n");
        return 1;
    }
    for (;;) {
        if (s_gfx < 0)
            gfx_connect();
        struct pollfd pf[1 + MAX_DEV];
        int map[MAX_DEV], n = 1;
        pf[0].fd = events;
        pf[0].events = POLLIN;
        for (int i = 0; i < MAX_DEV; i++)
            if (s_dev[i].used) {
                pf[n].fd = s_dev[i].fd;
                pf[n].events = POLLIN;
                map[n - 1] = i;
                n++;
            }
        int r = poll(pf, (nfds_t)n, s_gfx < 0 ? 500 : pad_due_ms(crtos_time_us()));
        uint64_t now = crtos_time_us();
        for (int i = 0; i < MAX_DEV; i++)
            if (s_dev[i].used && s_dev[i].pad)
                pad_flush(&s_dev[i], now, false); /* the sticks held back that are due */
        if (r <= 0)
            continue;
        if (pf[0].revents & POLLIN) {
            struct devmgr_event ev;
            while (crtos_msg_recv(events, &ev, sizeof(ev), NULL, 0) >= (int)sizeof(ev)) {
                ev.name[sizeof(ev.name) - 1] = 0;
                if (ev.type == DEVMGR_EV_ADD)
                    add_device(ev.name);
                else if (ev.type == DEVMGR_EV_REMOVE)
                    remove_device(ev.name);
            }
        }
        if (pf[0].revents & POLLHUP)
            return 2; /* devmgr ended: init restarts us, we subscribe again */
        for (int k = 1; k < n; k++)
            if (pf[k].revents & (POLLIN | POLLHUP | POLLERR))
                read_device(&s_dev[map[k - 1]]);
    }
}
