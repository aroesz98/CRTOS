/*
 * usb-hid.c - the host side of drivers/usb: HID keyboards and mice as input devices.
 *
 * Keyboards use the boot protocol: a keyboard becomes an input device reporting Linux key
 * codes, with all its keys declared (so the on-screen keyboard steps aside while it is
 * connected) and held keys repeating. A mouse reports relative motion (REL_X/Y), its wheels
 * (REL_WHEEL, REL_HWHEEL) and its buttons. The boot protocol has no wheel (only some mice add
 * it as a fourth byte), so a mouse whose report descriptor has one is switched to the report
 * protocol and its reports are read by the layout the descriptor gives (report ids, field
 * positions and sizes); the switch waits until the whole device is configured, as the control
 * pipe is busy until then. Hubs work. The TinyUSB callbacks below run in the host
 * controller's USB thread.
 */
#include <crtos/input.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <string.h>
#include "tusb.h"
#include "usb.h"

#define REPEAT_DELAY_MS     500     /* a held key starts repeating after this... */
#define REPEAT_EVERY_MS     33      /* ... and then repeats this often */
#define IDLE_WAIT_MS        100     /* the USB thread looks at "stop" at least this often */
#define PENDING_WAIT_MS     10      /* ... and at mice waiting for the report protocol */
#define REPORT_IDS          8       /* report ids of a mouse followed */
#define USAGES              16      /* usages of one main item kept */

/* keyboard usages (HID usage page 7) -> Linux key codes */
static const uint8_t s_usage[0x66] = {
    [0x04] = KEY_A, [0x05] = KEY_B, [0x06] = KEY_C, [0x07] = KEY_D, [0x08] = KEY_E, [0x09] = KEY_F,
    [0x0a] = KEY_G, [0x0b] = KEY_H, [0x0c] = KEY_I, [0x0d] = KEY_J, [0x0e] = KEY_K, [0x0f] = KEY_L,
    [0x10] = KEY_M, [0x11] = KEY_N, [0x12] = KEY_O, [0x13] = KEY_P, [0x14] = KEY_Q, [0x15] = KEY_R,
    [0x16] = KEY_S, [0x17] = KEY_T, [0x18] = KEY_U, [0x19] = KEY_V, [0x1a] = KEY_W, [0x1b] = KEY_X,
    [0x1c] = KEY_Y, [0x1d] = KEY_Z, [0x1e] = KEY_1, [0x1f] = KEY_2, [0x20] = KEY_3, [0x21] = KEY_4,
    [0x22] = KEY_5, [0x23] = KEY_6, [0x24] = KEY_7, [0x25] = KEY_8, [0x26] = KEY_9, [0x27] = KEY_0,
    [0x28] = KEY_ENTER, [0x29] = KEY_ESC, [0x2a] = KEY_BACKSPACE, [0x2b] = KEY_TAB, [0x2c] = KEY_SPACE,
    [0x2d] = KEY_MINUS, [0x2e] = KEY_EQUAL, [0x2f] = KEY_LEFTBRACE, [0x30] = KEY_RIGHTBRACE,
    [0x31] = KEY_BACKSLASH, [0x32] = KEY_BACKSLASH, [0x33] = KEY_SEMICOLON, [0x34] = KEY_APOSTROPHE,
    [0x35] = KEY_GRAVE, [0x36] = KEY_COMMA, [0x37] = KEY_DOT, [0x38] = KEY_SLASH, [0x39] = KEY_CAPSLOCK,
    [0x3a] = KEY_F1, [0x3b] = KEY_F2, [0x3c] = KEY_F3, [0x3d] = KEY_F4, [0x3e] = KEY_F5, [0x3f] = KEY_F6,
    [0x40] = KEY_F7, [0x41] = KEY_F8, [0x42] = KEY_F9, [0x43] = KEY_F10, [0x44] = KEY_F11, [0x45] = KEY_F12,
    [0x46] = KEY_SYSRQ, [0x47] = KEY_SCROLLLOCK, [0x48] = KEY_PAUSE, [0x49] = KEY_INSERT, [0x4a] = KEY_HOME,
    [0x4b] = KEY_PAGEUP, [0x4c] = KEY_DELETE, [0x4d] = KEY_END, [0x4e] = KEY_PAGEDOWN, [0x4f] = KEY_RIGHT,
    [0x50] = KEY_LEFT, [0x51] = KEY_DOWN, [0x52] = KEY_UP, [0x53] = KEY_NUMLOCK, [0x54] = KEY_KPSLASH,
    [0x55] = KEY_KPASTERISK, [0x56] = KEY_KPMINUS, [0x57] = KEY_KPPLUS, [0x58] = KEY_KPENTER,
    [0x59] = KEY_KP1, [0x5a] = KEY_KP2, [0x5b] = KEY_KP3, [0x5c] = KEY_KP4, [0x5d] = KEY_KP5,
    [0x5e] = KEY_KP6, [0x5f] = KEY_KP7, [0x60] = KEY_KP8, [0x61] = KEY_KP9, [0x62] = KEY_KP0,
    [0x63] = KEY_KPDOT, [0x64] = KEY_102ND, [0x65] = KEY_COMPOSE,
};

/* the modifier bits of a keyboard report */
static const uint8_t s_modkeys[8] = {
    KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT, KEY_LEFTMETA, KEY_RIGHTCTRL, KEY_RIGHTSHIFT, KEY_RIGHTALT, KEY_RIGHTMETA,
};

static const uint16_t s_buttons[3] = { BTN_LEFT, BTN_RIGHT, BTN_MIDDLE };

/* Where a value is in a report of the report protocol (size 0: the mouse has no such field) */
struct field {
    uint8_t id;                 /* report id (0: the device uses none) */
    uint8_t size;               /* bits */
    uint16_t bit;               /* position after the id byte */
    bool sign;                  /* the logical minimum is negative */
};

struct mouse_layout {
    bool ids;                   /* each report starts with its id */
    struct field button[3];     /* left, right, middle */
    struct field x, y, wheel, pan;
};

enum { PROTO_BOOT, PROTO_WANT_REPORT, PROTO_SWITCHING, PROTO_REPORT };

struct hid_dev {                /* one HID interface in use */
    bool used, keyboard;
    uint8_t daddr, idx;
    struct input_dev *input;
    uint8_t prev[8];            /* the last report (keyboard) or buttons (mouse) */
    uint8_t proto;              /* PROTO_* (mouse) */
    struct mouse_layout m;
};

static struct {
    struct hid_dev hid[CFG_TUH_HID];
    uint16_t repeat_code;       /* the key that repeats, 0: none */
    struct input_dev *repeat_input;
    uint64_t repeat_at_us;
} s_hid;

static struct hid_dev *hid_find(uint8_t daddr, uint8_t idx)
{
    for (int i = 0; i < CFG_TUH_HID; i++)
        if (s_hid.hid[i].used && s_hid.hid[i].daddr == daddr && s_hid.hid[i].idx == idx)
            return &s_hid.hid[i];
    return NULL;
}

static void key_event(struct hid_dev *h, uint8_t usage, int value)
{
    uint16_t code = usage < sizeof(s_usage) ? s_usage[usage] : 0;
    if (!code)
        return;
    input_report(h->input, EV_KEY, code, value);
    if (value) {
        s_hid.repeat_code = code;
        s_hid.repeat_input = h->input;
        s_hid.repeat_at_us = time_us() + REPEAT_DELAY_MS * 1000u;
    } else if (s_hid.repeat_code == code) {
        s_hid.repeat_code = 0;
    }
}

/* A boot protocol keyboard report: modifiers, reserved, up to six keys held */
static void keyboard_report(struct hid_dev *h, const uint8_t *r, uint16_t len)
{
    if (len < 8)
        return;
    uint8_t changed = r[0] ^ h->prev[0];
    for (int b = 0; b < 8; b++)
        if (changed & (1u << b))
            input_report(h->input, EV_KEY, s_modkeys[b], (r[0] >> b) & 1);
    if (r[2] == 1) /* "error roll over": too many keys, the list says nothing */
        return;
    for (int i = 2; i < 8; i++)
        if (h->prev[i] > 3 && !memchr(r + 2, h->prev[i], 6))
            key_event(h, h->prev[i], 0);
    for (int i = 2; i < 8; i++)
        if (r[i] > 3 && !memchr(h->prev + 2, r[i], 6))
            key_event(h, r[i], 1);
    input_sync(h->input);
    memcpy(h->prev, r, 8);
}

/* The buttons that changed, then the motion */
static void mouse_input(struct hid_dev *h, uint8_t buttons, int dx, int dy, int wheel, int pan)
{
    uint8_t changed = buttons ^ h->prev[0];
    for (int b = 0; b < 3; b++)
        if (changed & (1u << b))
            input_report(h->input, EV_KEY, s_buttons[b], (buttons >> b) & 1);
    if (dx)
        input_report(h->input, EV_REL, REL_X, dx);
    if (dy)
        input_report(h->input, EV_REL, REL_Y, dy);
    if (wheel)
        input_report(h->input, EV_REL, REL_WHEEL, wheel);
    if (pan)
        input_report(h->input, EV_REL, REL_HWHEEL, pan);
    input_sync(h->input);
    h->prev[0] = buttons;
}

/* A boot protocol mouse report: buttons, x, y, (wheel) */
static void mouse_boot_report(struct hid_dev *h, const uint8_t *r, uint16_t len)
{
    if (len < 3)
        return;
    mouse_input(h, r[0] & 7u, (int8_t)r[1], (int8_t)r[2], len > 3 ? (int8_t)r[3] : 0, 0);
}

/* ---- the report protocol: the report descriptor gives the layout ------------------------------- */

static int32_t field_value(const uint8_t *r, uint16_t len, const struct field *f)
{
    uint32_t v = 0;
    for (unsigned b = 0; b < f->size && b < 32u; b++) {
        unsigned pos = f->bit + b;
        if (pos / 8u >= len)
            break;
        v |= (uint32_t)((r[pos / 8u] >> (pos % 8u)) & 1u) << b;
    }
    if (f->sign && f->size && f->size < 32u && ((v >> (f->size - 1u)) & 1u))
        v |= ~0u << f->size;
    return (int32_t)v;
}

static bool field_in(const struct field *f, uint8_t id)
{
    return f->size && f->id == id;
}

static void mouse_report(struct hid_dev *h, const uint8_t *r, uint16_t len)
{
    if (h->proto != PROTO_REPORT) {
        mouse_boot_report(h, r, len);
        return;
    }
    const struct mouse_layout *m = &h->m;
    uint8_t id = 0;
    if (m->ids) {
        if (!len)
            return;
        id = r[0];
        r++;
        len--;
    }
    uint8_t buttons = h->prev[0];
    bool any = false;
    for (int b = 0; b < 3; b++) {
        if (!field_in(&m->button[b], id))
            continue;
        any = true;
        if (field_value(r, len, &m->button[b]))
            buttons |= (uint8_t)(1u << b);
        else
            buttons &= (uint8_t)~(1u << b);
    }
    int dx = field_in(&m->x, id) ? field_value(r, len, &m->x) : 0;
    int dy = field_in(&m->y, id) ? field_value(r, len, &m->y) : 0;
    int wheel = field_in(&m->wheel, id) ? field_value(r, len, &m->wheel) : 0;
    int pan = field_in(&m->pan, id) ? field_value(r, len, &m->pan) : 0;
    if (any || dx || dy || wheel || pan)
        mouse_input(h, buttons, dx, dy, wheel, pan);
}

/* The next input bit of report @id (the table grows as ids show up) */
static uint16_t *report_bits(uint8_t *ids, uint16_t *bits, int *n, uint8_t id)
{
    for (int i = 0; i < *n; i++)
        if (ids[i] == id)
            return &bits[i];
    if (*n == REPORT_IDS)
        return NULL;
    ids[*n] = id;
    bits[*n] = 0;
    return &bits[(*n)++];
}

static void take(struct field *f, uint8_t id, uint16_t bit, uint32_t size, bool sign)
{
    if (!f->size && size && size <= 32u) {
        f->id = id;
        f->bit = bit;
        f->size = (uint8_t)size;
        f->sign = sign;
    }
}

/* The mouse fields of a report descriptor (HID 1.11, 6.2.2): short items only, the usages of
 * each Input main item in order (a list, repeating its last usage, or a range) */
static void parse_mouse(const uint8_t *d, uint16_t len, struct mouse_layout *m)
{
    memset(m, 0, sizeof(*m));
    uint8_t ids[REPORT_IDS];
    uint16_t bits[REPORT_IDS];
    int nids = 0;
    uint32_t page = 0, rsize = 0, rcount = 0, usage_min = 0, usage_max = 0, usages[USAGES];
    int32_t lmin = 0;
    uint8_t rid = 0;
    int nusages = 0;
    for (uint16_t i = 0; i < len;) {
        uint8_t prefix = d[i++];
        if (prefix == 0xFE) { /* a long item: size, tag, data */
            if (i + 1u >= len)
                break;
            i = (uint16_t)(i + 2u + d[i]);
            continue;
        }
        unsigned size = prefix & 3u, type = (prefix >> 2) & 3u, tag = prefix >> 4;
        if (size == 3u)
            size = 4u;
        if (i + size > len)
            break;
        uint32_t v = 0;
        for (unsigned k = 0; k < size; k++)
            v |= (uint32_t)d[i + k] << (8u * k);
        i = (uint16_t)(i + size);
        int32_t sv = size == 1u ? (int8_t)v : size == 2u ? (int16_t)v : (int32_t)v;
        if (type == 1u) { /* global */
            if (tag == 0u)
                page = v;
            else if (tag == 1u)
                lmin = sv;
            else if (tag == 7u)
                rsize = v;
            else if (tag == 8u) {
                rid = (uint8_t)v;
                m->ids = true;
            } else if (tag == 9u)
                rcount = v;
            continue;
        }
        if (type == 2u) { /* local */
            if (tag == 0u && nusages < USAGES)
                usages[nusages++] = size == 4u ? v : (page << 16) | v;
            else if (tag == 1u)
                usage_min = size == 4u ? v : (page << 16) | v;
            else if (tag == 2u)
                usage_max = size == 4u ? v : (page << 16) | v;
            continue;
        }
        if (type == 0u && tag == 8u) { /* Input */
            uint16_t *pos = report_bits(ids, bits, &nids, rid);
            if (!pos)
                break;
            bool data_var = !(v & 1u) && (v & 2u);
            for (uint32_t n = 0; n < rcount && n < 256u; n++) {
                uint32_t u = 0;
                if (nusages)
                    u = usages[n < (uint32_t)nusages ? n : (uint32_t)nusages - 1u];
                else if (usage_max >= usage_min)
                    u = usage_min + n <= usage_max ? usage_min + n : usage_max;
                if (data_var) {
                    bool sign = lmin < 0;
                    if (u == 0x00010030u)
                        take(&m->x, rid, *pos, rsize, sign);
                    else if (u == 0x00010031u)
                        take(&m->y, rid, *pos, rsize, sign);
                    else if (u == 0x00010038u)
                        take(&m->wheel, rid, *pos, rsize, sign);
                    else if (u == 0x000C0238u)
                        take(&m->pan, rid, *pos, rsize, sign);
                    else if (u >= 0x00090001u && u <= 0x00090003u)
                        take(&m->button[u - 0x00090001u], rid, *pos, rsize, false);
                }
                *pos = (uint16_t)(*pos + rsize);
            }
        }
        if (type == 0u) { /* every main item ends the local state */
            nusages = 0;
            usage_min = usage_max = 0;
        }
    }
}

/* ---- the USB thread ---------------------------------------------------------------------------------- */

/* Mice waiting for the report protocol, once their device is ready (true: some still wait) */
static bool switch_protocols(void)
{
    bool waiting = false;
    for (int i = 0; i < CFG_TUH_HID; i++) {
        struct hid_dev *h = &s_hid.hid[i];
        if (!h->used || h->proto != PROTO_WANT_REPORT)
            continue;
        if (tuh_mounted(h->daddr) && tuh_hid_set_protocol(h->daddr, h->idx, HID_PROTOCOL_REPORT))
            h->proto = PROTO_SWITCHING;
        else
            waiting = true;
    }
    return waiting;
}

uint32_t usb_hid_repeat(void)
{
    uint32_t wait = switch_protocols() ? PENDING_WAIT_MS : IDLE_WAIT_MS;
    if (!s_hid.repeat_code)
        return wait;
    uint64_t now = time_us();
    if (now >= s_hid.repeat_at_us) {
        input_report(s_hid.repeat_input, EV_KEY, s_hid.repeat_code, 2);
        input_sync(s_hid.repeat_input);
        s_hid.repeat_at_us = now + REPEAT_EVERY_MS * 1000u;
    }
    uint32_t ms = (uint32_t)((s_hid.repeat_at_us - now) / 1000u) + 1u;
    return ms < wait ? ms : wait;
}

void usb_hid_stop(void)
{
    for (int i = 0; i < CFG_TUH_HID; i++)
        if (s_hid.hid[i].used)
            input_unregister(s_hid.hid[i].input);
    memset(&s_hid, 0, sizeof(s_hid));
}

void tuh_mount_cb(uint8_t daddr)
{
    uint16_t vid = 0, pid = 0;
    tuh_vid_pid_get(daddr, &vid, &pid);
    printk("usb: device %u connected (%04x:%04x)\n", daddr, vid, pid);
}

void tuh_umount_cb(uint8_t daddr)
{
    printk("usb: device %u disconnected\n", daddr);
}

void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *desc, uint16_t desc_len)
{
    uint8_t proto = tuh_hid_interface_protocol(daddr, idx);
    if (proto != HID_ITF_PROTOCOL_KEYBOARD && proto != HID_ITF_PROTOCOL_MOUSE) {
        printk("usb: device %u: a HID interface without the keyboard/mouse boot protocol (not used)\n", daddr);
        return;
    }
    struct hid_dev *h = NULL;
    for (int i = 0; i < CFG_TUH_HID && !h; i++)
        if (!s_hid.hid[i].used)
            h = &s_hid.hid[i];
    if (!h)
        return;
    bool kbd = proto == HID_ITF_PROTOCOL_KEYBOARD;
    h->input = input_register(kbd ? "usb-keyboard" : "usb-mouse");
    if (!h->input)
        return;
    if (kbd) {
        for (unsigned u = 0; u < sizeof(s_usage); u++)
            if (s_usage[u])
                input_set_key(h->input, s_usage[u]);
        for (int b = 0; b < 8; b++)
            input_set_key(h->input, s_modkeys[b]);
    } else {
        for (int b = 0; b < 3; b++)
            input_set_key(h->input, s_buttons[b]);
    }
    h->used = true;
    h->keyboard = kbd;
    h->daddr = daddr;
    h->idx = idx;
    h->proto = PROTO_BOOT;
    memset(h->prev, 0, sizeof(h->prev));
    const char *how = "";
    if (!kbd && desc && desc_len) {
        parse_mouse(desc, desc_len, &h->m);
        if ((h->m.wheel.size || h->m.pan.size) && h->m.x.size && h->m.y.size) {
            h->proto = PROTO_WANT_REPORT; /* receiving starts once it is switched */
            how = " (wheel: report protocol)";
        }
    }
    printk("usb: device %u: %s as /dev/%s%s\n", daddr, kbd ? "keyboard" : "mouse", input_devname(h->input), how);
    if (h->proto == PROTO_BOOT)
        tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_set_protocol_complete_cb(uint8_t daddr, uint8_t idx, uint8_t protocol)
{
    struct hid_dev *h = hid_find(daddr, idx);
    if (!h || h->proto != PROTO_SWITCHING)
        return;
    h->proto = protocol == HID_PROTOCOL_REPORT ? PROTO_REPORT : PROTO_BOOT;
    if (h->proto != PROTO_REPORT)
        printk("usb: device %u: the mouse stays in the boot protocol (no wheel)\n", daddr);
    tuh_hid_receive_report(daddr, idx);
}

void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
    struct hid_dev *h = hid_find(daddr, idx);
    if (!h)
        return;
    if (s_hid.repeat_input == h->input)
        s_hid.repeat_code = 0;
    input_unregister(h->input);
    h->used = false;
}

void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *report, uint16_t len)
{
    struct hid_dev *h = hid_find(daddr, idx);
    if (h) {
        if (h->keyboard)
            keyboard_report(h, report, len);
        else
            mouse_report(h, report, len);
    }
    tuh_hid_receive_report(daddr, idx);
}
