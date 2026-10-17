/*
 * osk - the on-screen keyboard (layer 2 service), for when no keyboard is connected.
 *
 * A keyboard window at the bottom of the screen, right above the window manager's task bar.
 * It never takes the keyboard focus: its keys go to the program whose window has it, as the
 * key events a real keyboard sends (GFX_EV_KEY with Linux key codes, Shift and Ctrl pressed
 * as keys of their own), so every program that takes keys can be typed into. The task bar's
 * keyboard button shows and hides it through the port "osk" (osk_proto.h); its own "hide"
 * key hides it too.
 *
 * It is offered only while no keyboard is connected: when an input device that can report
 * the letter keys appears (INPUT_IOC_GET_KEYBITS; the devices are looked at every few
 * seconds), it hides and its state says it is not available.
 *
 * Its size follows the interface scale (ui_px, GFX_EV_SETTINGS), as the task bar's does.
 */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/input.h>
#include "gfx.h"
#include "gfx_ui.h"
#include "osk_proto.h"

#define BAR_H       ui_px(26)   /* the window manager's task bar */
#define CORNER_W    ui_px(48)   /* the "hide" key, at the right end of the bottom row */
#define SCAN_MS     3000        /* how often the input devices are looked at */
#define MAX_INPUTS  16

static struct gfx *s_g;
static struct gfx_win *s_w;
static struct ui_keyboard s_kbd;
static int s_input = -1;        /* the graphics server's port: key events go there */
static bool s_visible, s_keyboard, s_hide_pressed;
static int W;

/* Is a keyboard connected: an input device with the letter keys? */
static bool keyboard_connected(void)
{
    char path[24];
    for (int i = 0; i < MAX_INPUTS; i++) {
        snprintf(path, sizeof(path), "/dev/event%d", i);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        uint8_t bits[INPUT_KEYBITS_SIZE];
        bool kbd = ioctl(fd, INPUT_IOC_GET_KEYBITS, bits) == 0 && input_has_key(bits, KEY_A) &&
                   input_has_key(bits, KEY_Z) && input_has_key(bits, KEY_SPACE);
        close(fd);
        if (kbd)
            return true;
    }
    return false;
}

static struct ui_rect hide_rect(void)
{
    int rh = s_kbd.r.h / UI_KEYBOARD_ROWS;
    struct ui_rect r = { s_kbd.r.w - CORNER_W, (UI_KEYBOARD_ROWS - 1) * rh, CORNER_W, rh };
    return r;
}

static void draw(void)
{
    const struct ui_theme *t = ui_theme;
    struct gfx_surface *s = &s_w->s;
    ui_keyboard_draw(&s_kbd, s);
    struct ui_rect r = hide_rect();
    gfx_round_rect_aa(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, t->radius ? (t->radius + 1) / 2 : 0,
                      s_hide_pressed ? t->button_pressed : t->accent, 0, GFX_CORNERS_ALL);
    ui_text_center(s, &r, t->font, "hide", t->accent_text);
    gfx_present(s_w);
}

static void show(bool on)
{
    if (s_keyboard)
        on = false;
    if (on == s_visible)
        return;
    s_visible = on;
    if (on) {
        s_kbd.mode = 0;
        s_kbd.ctrl = false;
        s_kbd.pressed = -1;
        draw();
    }
    gfx_win_show(s_w, on);
    if (on)
        gfx_win_raise(s_w);
}

/* One key event to the graphics server, as the input service sends them */
static void send_key(uint16_t code, int32_t value)
{
    struct gfx_input in = { { GFX_INPUT, sizeof(in) }, GFX_KEY, code, 0, 0, value,
                            (uint32_t)(crtos_time_us() / 1000u) };
    crtos_msg_send(s_input, &in, sizeof(in), -1, 1000);
}

/* A character from the keyboard: its key, with Shift and Ctrl held around it as needed */
static void type(int c)
{
    uint16_t code;
    unsigned mods;
    if (ui_char_key(c, &code, &mods))
        return;
    if (mods & UI_MOD_CTRL)
        send_key(KEY_LEFTCTRL, 1);
    if (mods & UI_MOD_SHIFT)
        send_key(KEY_LEFTSHIFT, 1);
    send_key(code, 1);
    send_key(code, 0);
    if (mods & UI_MOD_SHIFT)
        send_key(KEY_LEFTSHIFT, 0);
    if (mods & UI_MOD_CTRL)
        send_key(KEY_LEFTCTRL, 0);
}

static void pointer(const struct gfx_event *ev)
{
    struct ui_rect hr = hide_rect();
    if (ev->kind == GFX_PTR_DOWN && ui_inside(&hr, ev->x, ev->y)) {
        s_hide_pressed = true;
        draw();
        return;
    }
    if (s_hide_pressed) {
        if (ev->kind == GFX_PTR_UP) {
            s_hide_pressed = false;
            if (ui_inside(&hr, ev->x, ev->y))
                show(false);
            else
                draw();
        }
        return;
    }
    bool redraw;
    int c = ui_keyboard_pointer(&s_kbd, ev->kind, ev->x, ev->y, &redraw);
    if (redraw)
        draw();
    if (c >= 0)
        type(c);
}

/* The appearance changed: the keyboard at the new scale, above the task bar's new height */
static void restyle(void)
{
    int kh = ui_keyboard_height(W);
    if (gfx_win_resize(s_w, W, kh) < 0)
        return;
    gfx_win_move(s_w, 0, gfx_screen_height(s_g) - BAR_H - kh);
    s_kbd.r.w = W;
    s_kbd.r.h = kh;
    s_kbd.corner = CORNER_W;
    draw();
}

/* Requests on the port: show, hide, toggle; every one is answered with the state */
static void serve(int port)
{
    struct osk_req req;
    struct crtos_msginfo info;
    while (crtos_msg_recv(port, &req, sizeof(req), &info, 0) >= 0) {
        if (info.len >= sizeof(req)) {
            if (req.op == OSK_SHOW)
                show(true);
            else if (req.op == OSK_HIDE)
                show(false);
            else if (req.op == OSK_TOGGLE)
                show(!s_visible);
        }
        if (info.token) {
            struct osk_state st = { !s_keyboard, s_visible, (uint16_t)s_kbd.r.h };
            crtos_msg_reply(info.token, &st, sizeof(st), -1);
        }
        if (info.handle >= 0)
            close(info.handle);
    }
}

int main(void)
{
    for (int i = 0; !(s_g = gfx_open()); i++) {     /* the graphics server may still be starting */
        if (i == 40) {
            printf("osk: no graphics server\n");
            return 1;
        }
        crtos_sleep_ms(250);
    }
    s_input = crtos_port_connect(GFX_PORT_NAME, 2000);
    int port = crtos_port_create(OSK_PORT_NAME);
    if (s_input < 0 || port < 0) {
        printf("osk: no ports\n");
        return 1;
    }
    W = gfx_screen_width(s_g);
    int kh = ui_keyboard_height(W);
    s_w = gfx_win_create(s_g, 0, gfx_screen_height(s_g) - BAR_H - kh, W, kh,
                         GFX_WIN_TOPMOST | GFX_WIN_NOFOCUS | GFX_WIN_NOFRAME | GFX_WIN_HIDDEN, "keyboard");
    if (!s_w) {
        printf("osk: no window\n");
        return 1;
    }
    s_kbd.r.w = W;
    s_kbd.r.h = kh;
    s_kbd.corner = CORNER_W;
    s_kbd.pressed = -1;
    s_keyboard = keyboard_connected();
    printf("osk: %s\n", s_keyboard ? "a keyboard is connected" : "ready (no keyboard connected)");
    uint64_t next_scan = crtos_time_us() + SCAN_MS * 1000u;
    for (;;) {
        uint64_t now = crtos_time_us();
        int wait = now >= next_scan ? 0 : (int)((next_scan - now) / 1000u);
        struct pollfd pf[2] = { { gfx_event_handle(s_g), POLLIN, 0 }, { port, POLLIN, 0 } };
        poll(pf, 2, wait);
        serve(port);
        struct gfx_event ev;
        while (gfx_next_event(s_g, &ev, 0) == 0)
            if (ev.h.type == GFX_EV_POINTER)
                pointer(&ev);
            else if (ev.h.type == GFX_EV_SETTINGS)
                restyle();
        if (crtos_time_us() >= next_scan) {
            bool k = keyboard_connected();
            if (k != s_keyboard) {
                printf("osk: keyboard %s\n", k ? "connected - the on-screen one is off" : "gone - the on-screen one is back");
                s_keyboard = k;
                if (k)
                    show(false);
            }
            next_scan = crtos_time_us() + SCAN_MS * 1000u;
        }
    }
}
