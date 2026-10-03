/*
 * wm - the window manager (layer 3).
 *
 * Decorates the windows of the other programs with a title bar - the program's icon and the
 * window's title; drag it to move the window, [_] minimises, [x] asks the program to close (a
 * second [x] a moment later ends it) - and keeps a task bar at the bottom: the programs menu,
 * the icon of each window (tap: bring it up, tap the active one: minimise) and a clock. The
 * mouse resting on a task bar item (or a finger held on it) shows a tooltip: a window's title,
 * the date over the clock.
 *
 * Icons: the icon of program NAME is /sd/crtos/share/icons/NAME.pam (libgfx gfx_icon_load;
 * NAME as ps shows the program: its file name without ".app"), "application" for programs
 * without one; each is read once per size.
 *
 * Windows whose programs can change their size (GFX_WIN_RESIZABLE) also get [ ] to fill
 * the screen above the task bar and back (or a double tap on the title), and a grip in the
 * bottom right corner: drag it to resize. The program is asked for the new size while the
 * finger moves (ten times a second) and once more when it lifts; the frame follows the size
 * the program actually takes.
 *
 * It is an ordinary client of gfxd registered as the window manager (see gfx_proto.h), so
 * it can be ended, restarted or replaced by another manager while the programs keep
 * running: their windows lose the decorations meanwhile and get them back from the next one.
 *
 * The programs menu comes from /sd/crtos/etc/launcher.cfg, one entry per line:
 *     Title: [caps=spawn,kill,sys,dev,module] [icon=NAME] /path/program.app [args...]
 * (the icon defaults to the program's; an entry whose program is not on the card is left out:
 * a system built without the optional programs). The Windows key opens and closes it too (gfxd gives
 * the manager that key); while it is open the keyboard is its own: arrows, Page Up/Down,
 * Home/End, a letter (the next title with it), Enter starts, Esc closes. The mouse wheel
 * scrolls it, the mouse over an entry selects it.
 * The time zone (POSIX TZ string) is read from /sd/crtos/etc/timezone.
 *
 * Appearance (Settings > Appearance, gfx_ui.h): every size here is meant for 100 % and taken
 * at the interface scale (ui_px); with transparency the task bar, the menu and the tooltips
 * are translucent (ARGB windows, which gfxd blends over what is below); with rounded corners
 * the menu and tooltips are rounded, and so are the title bars at the top (their frames are
 * ARGB windows with transparent corners). When the settings change (GFX_EV_SETTINGS) the task
 * bar and every frame are made anew at the new size and look, the windows stay where they are.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/keys.h>
#include "gfx.h"
#include "gfx_ui.h"
#include "osk_proto.h"

#define DOUBLE_TAP_US 400000u
#define RESIZE_EVERY_US 100000u
#define MAXW 24
#define MAXAPPS 24
#define MAXICONS 96
#define ICON_NAME 24  /* an icon's name: a program's */
#define POOL_BYTES (2u * 1024u * 1024u) /* our windows' pixels (ARGB ones at 200 % are big) */

/* Sizes at the interface scale (metrics(): their values at 100 % times the scale) */
static struct
{
    int title_h, bar_h, grip, min_w, min_h, menu_w, menu_row;
    int icon_menu, icon_bar, icon_title; /* icon sizes: programs menu, task bar, title bars */
    int tip_h, menu_btn_w, clock_w, kbd_w, item_w;
    int radius;    /* corners (0: square) */
    uint8_t alpha; /* the task bar, the menu and tooltips (255: opaque) */
} M;
#define TITLE_H (M.title_h)
#define BAR_H (M.bar_h)
#define GRIP (M.grip) /* resize grip, in the window's bottom right corner */
#define MIN_W (M.min_w)
#define MIN_H (M.min_h)
#define MENU_W (M.menu_w)
#define MENU_ROW (M.menu_row)
#define ICON_MENU (M.icon_menu)
#define ICON_BAR (M.icon_bar)
#define ICON_TITLE (M.icon_title)
#define TIP_H (M.tip_h)
#define TIP_DELAY_US 450000u /* the mouse rests that long on an item: its tooltip */
#define TIP_HOLD_US 550000u  /* a finger held that long */
#define APPS_CFG "/sd/crtos/etc/launcher.cfg"
#define TZ_CFG "/sd/crtos/etc/timezone"
#define DEFAULT_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

struct managed
{
    bool used;
    int id, pid; /* the client window and its program */
    int w, h;
    char title[32];
    char icon[ICON_NAME]; /* its program's name: the icon */
    struct gfx_win *frame; /* our title bar */
    int x, y;              /* where the frame is */
    bool mapped;           /* its program shows it */
    bool minimized, focused;
    uint64_t close_us;    /* [x] pressed at */
    uint32_t flags;       /* of the client window (GFX_WIN_*) */
    struct gfx_win *grip; /* resize grip (resizable windows) */
    bool maximized;
    int save_x, save_y, save_w, save_h; /* before it was maximised */
    uint64_t title_tap_us;              /* last tap on the title bar */
};

struct app
{
    char title[32];
    char cmd[96];
    char icon[ICON_NAME];
    uint32_t caps;
};

struct icon
{
    char name[ICON_NAME];
    int size;
    struct gfx_image *img; /* NULL: the program has none */
};

static struct gfx *g;
static int W, H;
static struct managed s_m[MAXW];
static int s_order[MAXW], s_norder; /* task bar order */
static struct gfx_win *s_bar, *s_menu;
static int s_cascade;
static struct app s_apps[MAXAPPS];
static int s_napps;
static uint64_t s_apps_loaded_us; /* the list is read again after a while */
static uint64_t s_menu_toggled_us;
static struct ui_list s_menu_list;
static struct icon s_icons[MAXICONS];
static int s_nicons;

/* tooltips of the task bar: items as bar_hit() gives them */
#define ITEM_NONE (-1)
#define ITEM_MENU (-2)
#define ITEM_KBD (-3)
#define ITEM_CLOCK (-4)
static struct gfx_win *s_tip;
static int s_tip_item = ITEM_NONE;  /* the item it shows */
static int s_tip_want = ITEM_NONE;  /* the item it is due for at s_tip_due_us */
static uint64_t s_tip_due_us;       /* 0: none due */
static int s_hover = ITEM_NONE;     /* the item under the mouse */
static int s_tip_quiet = ITEM_NONE; /* clicked: no tooltip for it until the mouse leaves it */

/* touch state */
static struct managed *s_drag; /* title bar being dragged */
static int s_drag_dx, s_drag_dy;
static struct managed *s_btn_win; /* title bar button pressed */
static int s_btn;                 /* 1 close, 2 minimise, 3 maximise */
static struct managed *s_resize;  /* grip being dragged */
static int s_rs_x0, s_rs_y0, s_rs_w0, s_rs_h0, s_rs_w, s_rs_h;
static uint64_t s_rs_sent_us;
static int s_bar_press = -1; /* task bar item pressed: -2 menu, -3 keyboard, >= 0 window */
static int s_osk_port = -1;  /* the on-screen keyboard (system/services/osk) */
static struct osk_state s_osk; /* its last state: the task bar offers it while it is available */
static uint64_t s_osk_asked_us, s_osk_toggled_us;
static uint32_t s_uid_caps;
static bool s_trace; /* -t: log touches */

static void restyle(void);

/* ---- helpers ---------------------------------------------------------------------------------- */

static struct managed *by_id(int id)
{
    for (int i = 0; i < MAXW; i++)
        if (s_m[i].used && s_m[i].id == id)
            return &s_m[i];
    return NULL;
}

static struct managed *by_frame(int frame_id)
{
    for (int i = 0; i < MAXW; i++)
        if (s_m[i].used && s_m[i].frame && s_m[i].frame->id == frame_id)
            return &s_m[i];
    return NULL;
}

static struct managed *by_grip(int grip_id)
{
    for (int i = 0; i < MAXW; i++)
        if (s_m[i].used && s_m[i].grip && s_m[i].grip->id == grip_id)
            return &s_m[i];
    return NULL;
}

static bool resizable(const struct managed *m)
{
    return (m->flags & GFX_WIN_RESIZABLE) != 0;
}

static int work_h(void)
{
    return H - BAR_H;
}

static void metrics(void)
{
    M.title_h = ui_px(20);
    M.bar_h = ui_px(26);
    M.grip = ui_px(24);
    M.min_w = ui_px(100);
    M.min_h = ui_px(60);
    M.menu_w = ui_px(180);
    M.menu_row = ui_px(26);
    M.icon_menu = ui_px(20);
    M.icon_bar = ui_px(18);
    M.icon_title = ui_px(16);
    M.tip_h = ui_px(20);
    M.menu_btn_w = ui_px(58);
    M.clock_w = ui_px(54);
    M.kbd_w = ui_px(34);
    M.item_w = ui_px(34);
    M.radius = ui_theme->radius;
    M.alpha = ui_theme->alpha;
}

/* An ARGB window for the menu and tooltips: translucent, or with round corners */
static uint32_t popup_flags(void)
{
    return M.alpha < 255 || M.radius ? GFX_WIN_ALPHA : 0u;
}

/* @rgb on a translucent surface: as opaque as the surface, or more (@more of 255) */
static uint32_t over(uint32_t rgb, int more)
{
    int a = M.alpha + more;
    return ((uint32_t)(a > 255 ? 255 : a) << 24) | (rgb & 0xFFFFFFu);
}

/* ---- icons ------------------------------------------------------------------------------------ */

/* The name ps shows for process @pid (its program file without ".app"), "" if it is gone */
static void proc_name(int pid, char *out, size_t size)
{
    struct crtos_procinfo pi;
    out[0] = 0;
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++)
        if (pi.pid == pid)
        {
            snprintf(out, size, "%s", pi.name);
            return;
        }
}

/* The same for a command line: its program file without the directory and ".app" */
static void program_name(const char *cmd, char *out, size_t size)
{
    size_t n = strcspn(cmd, " \t");
    const char *b = cmd;
    for (size_t i = 0; i < n; i++)
        if (cmd[i] == '/')
            b = cmd + i + 1;
    n -= (size_t)(b - cmd);
    if (n > 4 && !strncmp(b + n - 4, ".app", 4))
        n -= 4;
    snprintf(out, size, "%.*s", (int)n, b);
}

/* Program @name's own icon at @size, read once (also that it has none), or NULL */
static const struct gfx_image *icon_own(const char *name, int size)
{
    for (int i = 0; i < s_nicons; i++)
        if (s_icons[i].size == size && !strcmp(s_icons[i].name, name))
            return s_icons[i].img;
    struct gfx_image *img = name[0] ? gfx_icon_load(name, size) : NULL;
    if (s_nicons == MAXICONS)
    { /* (more names than programs could have): not kept */
        free(img);
        return NULL;
    }
    struct icon *ic = &s_icons[s_nicons++];
    snprintf(ic->name, sizeof(ic->name), "%s", name);
    ic->size = size;
    ic->img = img;
    return img;
}

/* Its own, else the icon of programs without their own, else NULL */
static const struct gfx_image *icon_get(const char *name, int size)
{
    const struct gfx_image *img = icon_own(name, size);
    return img ? img : icon_own(GFX_ICON_DEFAULT, size);
}

/* @name's icon at (x, y); without any icon files the first letter of @title on a tile */
static void draw_icon(const struct gfx_surface *s, int x, int y, int size, const char *name, const char *title)
{
    const struct gfx_image *img = icon_get(name, size);
    if (img)
    {
        gfx_image_draw(s, x, y, img);
        return;
    }
    const struct ui_theme *t = ui_theme;
    gfx_round_rect_aa(s, x, y, size, size, M.radius ? size / 5 : 0, t->accent, 0, GFX_CORNERS_ALL);
    char c[2] = {(char)toupper((unsigned char)(title[0] ? title[0] : '?')), 0};
    struct ui_rect r = {x, y, size, size};
    ui_text_center(s, &r, t->bold, c, t->accent_text);
}

/* ---- title bars ---------------------------------------------------------------------------------- */

static struct ui_rect close_rect(const struct managed *m)
{
    int b = TITLE_H + ui_px(2);
    struct ui_rect r = {m->frame->s.w - b - M.radius / 2, 0, b, TITLE_H};
    return r;
}

static struct ui_rect max_rect(const struct managed *m)
{
    struct ui_rect r = close_rect(m);
    r.x -= r.w;
    return r;
}

static struct ui_rect min_rect(const struct managed *m)
{
    struct ui_rect r = close_rect(m);
    r.x -= (resizable(m) ? 2 : 1) * r.w;
    return r;
}

static uint32_t frame_flags(void)
{
    return M.radius ? GFX_WIN_ALPHA : 0u; /* transparent top corners */
}

static void draw_frame(struct managed *m)
{
    const struct ui_theme *t = ui_theme;
    struct gfx_surface *s = &m->frame->s;
    uint32_t bg = m->focused ? t->accent : GFX_RGB(62, 70, 88);
    uint32_t fg = m->focused ? t->accent_text : GFX_RGB(200, 206, 216);
    if (M.radius)
    { /* the top corners rounded over what is below (an ARGB frame) */
        gfx_fill(s, 0, 0, s->w, s->h, GFX_TRANSPARENT);
        gfx_round_rect_aa(s, 0, 0, s->w, s->h, M.radius, bg, 0, GFX_CORNERS_TOP);
    }
    else
    {
        gfx_fill(s, 0, 0, s->w, s->h, bg);
        gfx_hline(s, 0, 0, s->w, m->focused ? GFX_RGB(110, 165, 240) : GFX_RGB(90, 98, 118));
    }
    struct ui_rect c = close_rect(m), n = min_rect(m), x = max_rect(m);
    bool pc = s_btn_win == m && s_btn == 1, pn = s_btn_win == m && s_btn == 2, px = s_btn_win == m && s_btn == 3;
    int in = ui_px(2);
    if (pc)
        gfx_round_rect_aa(s, c.x + in, in, c.w - 2 * in, c.h - 2 * in, M.radius / 2, GFX_RGB(200, 60, 60), 0,
                          GFX_CORNERS_ALL);
    if (pn)
        gfx_round_rect_aa(s, n.x + in, in, n.w - 2 * in, n.h - 2 * in, M.radius / 2, GFX_RGB(90, 100, 124), 0,
                          GFX_CORNERS_ALL);
    if (px)
        gfx_round_rect_aa(s, x.x + in, in, x.w - 2 * in, x.h - 2 * in, M.radius / 2, GFX_RGB(90, 100, 124), 0,
                          GFX_CORNERS_ALL);
    int cx = c.x + c.w / 2, cy = TITLE_H / 2, k = ui_px(4), lw = ui_px(2) > 1 ? ui_px(2) : 2;
    for (int d = 0; d < lw; d++)
    { /* x */
        gfx_line(s, cx - k + d, cy - k, cx + k + d, cy + k, fg);
        gfx_line(s, cx - k + d, cy + k, cx + k + d, cy - k, fg);
    }
    gfx_fill(s, n.x + n.w / 2 - ui_px(5), cy + ui_px(3), ui_px(10), lw, fg); /* _ */
    if (resizable(m))
    {
        int mx = x.x + x.w / 2, a = ui_px(8), b = ui_px(7);
        if (m->maximized)
        { /* two windows: back to the old size */
            gfx_rect(s, mx - ui_px(3), cy - ui_px(5), a, b, fg);
            gfx_fill(s, mx - ui_px(5), cy - ui_px(2), a, b, bg);
            gfx_rect(s, mx - ui_px(5), cy - ui_px(2), a, b, fg);
            gfx_hline(s, mx - ui_px(5), cy - ui_px(1), a, fg);
        }
        else
        { /* one big window */
            gfx_rect(s, mx - ui_px(5), cy - ui_px(5), ui_px(11), ui_px(10), fg);
            gfx_hline(s, mx - ui_px(5), cy - ui_px(4), ui_px(11), fg);
        }
    }
    int asc = gfx_font_ascent(t->bold), ix = ui_px(3) + M.radius / 3;
    draw_icon(s, ix, (TITLE_H - ICON_TITLE) / 2, ICON_TITLE, m->icon, m->title);
    ui_text_fit(s, t->bold, ix + ICON_TITLE + ui_px(5), (TITLE_H + asc) / 2, n.x - ix - ICON_TITLE - ui_px(9), m->title,
                fg);
    gfx_present(m->frame);
}

/* ---- sizes --------------------------------------------------------------------------------------- */

static void draw_grip(struct managed *m)
{
    struct gfx_surface *s = &m->grip->s;
    gfx_fill(s, 0, 0, GRIP, GRIP, GFX_TRANSPARENT);
    for (int i = 0; i < 3; i++)
    { /* three diagonal strokes in the corner */
        int d = ui_px(7 + i * 5), e = ui_px(2);
        gfx_line(s, GRIP - e, GRIP - d, GRIP - d, GRIP - e, GFX_ARGB(230, 255, 255, 255));
        gfx_line(s, GRIP - e, GRIP - d + 1, GRIP - d + 1, GRIP - e, GFX_ARGB(160, 0, 0, 0));
    }
    gfx_present(m->grip);
}

/* The grip sits in the corner of a window of w x h */
static void place_grip(struct managed *m, int w, int h)
{
    if (m->grip)
        gfx_wm_attach(g, m->grip->id, m->frame, w - GRIP, TITLE_H + h - GRIP);
}

static void show_grip(struct managed *m)
{
    if (m->grip)
        gfx_win_show(m->grip, !m->maximized);
}

static void clamp_size(const struct managed *m, int *w, int *h)
{
    int maxw = W - (m->x > 0 ? m->x : 0), maxh = work_h() - m->y - TITLE_H;
    *w = *w < MIN_W ? MIN_W : *w > maxw ? maxw
                                        : *w;
    *h = *h < MIN_H ? MIN_H : *h > maxh ? maxh
                                        : *h;
}

/* A window that fills the work area counts as maximised: no grip over its corner, and
 * [ ] brings it down to three quarters of the screen */
static void check_full(struct managed *m)
{
    if (!resizable(m) || m->maximized || m->x > 0 || m->y > 0 || m->w < W || TITLE_H + m->h < work_h())
        return;
    m->maximized = true;
    m->save_w = W * 3 / 4;
    m->save_h = (work_h() - TITLE_H) * 3 / 4;
    m->save_x = (W - m->save_w) / 2;
    m->save_y = (work_h() - TITLE_H - m->save_h) / 2;
    show_grip(m);
}

static void toggle_maximize(struct managed *m)
{
    if (!resizable(m))
        return;
    if (!m->maximized)
    {
        m->save_x = m->x;
        m->save_y = m->y;
        m->save_w = m->w;
        m->save_h = m->h;
        m->maximized = true;
        m->x = m->y = 0;
        gfx_win_move(m->frame, 0, 0);
        gfx_wm_configure(g, m->id, W, work_h() - TITLE_H);
    }
    else
    {
        m->maximized = false;
        m->x = m->save_x;
        m->y = m->save_y;
        gfx_win_move(m->frame, m->x, m->y);
        int w = m->save_w, h = m->save_h;
        clamp_size(m, &w, &h);
        gfx_wm_configure(g, m->id, w, h);
    }
    show_grip(m);
    draw_frame(m);
}

/* Its program gave the window another size: fit the frame and the grip to it */
static void resized(struct managed *m, int w, int h)
{
    m->w = w;
    m->h = h;
    /* grown past the screen (a larger scale): moved back onto it, as far as it fits */
    int x = m->x + w > W ? W - w : m->x, y = m->y + TITLE_H + h > work_h() ? work_h() - TITLE_H - h : m->y;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    if ((x != m->x || y != m->y) && !m->maximized && s_resize != m)
    {
        m->x = x;
        m->y = y;
        gfx_win_move(m->frame, x, y);
    }
    check_full(m);
    if (gfx_win_resize(m->frame, w, TITLE_H) == 0 || m->maximized)
        draw_frame(m);
    if (s_resize != m)
        place_grip(m, w, h);
}

static void grip_pointer(struct managed *m, const struct gfx_event *ev)
{
    int sx = GFX_EV_SX(ev), sy = GFX_EV_SY(ev);
    uint64_t now = crtos_time_us();
    switch (ev->kind)
    {
    case GFX_PTR_DOWN:
        s_resize = m;
        s_rs_x0 = sx;
        s_rs_y0 = sy;
        s_rs_w0 = s_rs_w = m->w;
        s_rs_h0 = s_rs_h = m->h;
        s_rs_sent_us = 0;
        break;
    case GFX_PTR_MOVE:
    case GFX_PTR_UP:
    {
        if (s_resize != m)
            break;
        int w = s_rs_w0 + (sx - s_rs_x0), h = s_rs_h0 + (sy - s_rs_y0);
        clamp_size(m, &w, &h);
        if (w != s_rs_w || h != s_rs_h)
        { /* the grip follows the finger at once */
            s_rs_w = w;
            s_rs_h = h;
            place_grip(m, w, h);
        }
        bool last = ev->kind == GFX_PTR_UP;
        if ((last || now - s_rs_sent_us >= RESIZE_EVERY_US) && (w != m->w || h != m->h))
        {
            s_rs_sent_us = now;
            gfx_wm_configure(g, m->id, w, h);
        }
        if (last)
        { /* back to the corner the window has; it moves on when the program resizes */
            s_resize = NULL;
            place_grip(m, m->w, m->h);
        }
        break;
    }
    default:
        break;
    }
}

/* ---- task bar -------------------------------------------------------------------------------- */

#define MENU_BTN_W (M.menu_btn_w) /* the Apps button with the word "Apps" */
#define APPS_ICON "wm-apps"         /* its icon (share/icons/wm-apps.pam): then a button like a window's */
#define CLOCK_W (M.clock_w)
#define KBD_W (M.kbd_w)

/* Ask the on-screen keyboard for @op (osk_proto.h) and keep its state; true if it changed */
static bool osk_request(uint32_t op)
{
    if (s_osk_port < 0)
        s_osk_port = crtos_port_connect(OSK_PORT_NAME, 0);
    struct osk_req req = {op};
    struct osk_state st;
    memset(&st, 0, sizeof(st));
    if (s_osk_port >= 0 && crtos_msg_call(s_osk_port, &req, sizeof(req), &st, sizeof(st), 300) < (int)sizeof(st))
    {
        close(s_osk_port); /* it ended: connect again next time */
        s_osk_port = -1;
        memset(&st, 0, sizeof(st));
    }
    s_osk_asked_us = crtos_time_us();
    bool changed = st.available != s_osk.available || st.visible != s_osk.visible;
    s_osk = st;
    return changed;
}

/* The keyboard button, left of the clock (only while the keyboard is offered) */
static struct ui_rect kbd_button(void)
{
    struct ui_rect r = {W - CLOCK_W - KBD_W + ui_px(2), ui_px(3), KBD_W - ui_px(4), BAR_H - ui_px(6)};
    return r;
}

static void draw_kbd_icon(struct gfx_surface *s, const struct ui_rect *r, uint32_t c)
{
    int u = ui_px(3), w = 6 * u, h = 4 * u - ui_px(1);
    int x = r->x + (r->w - w) / 2, y = r->y + (r->h - h) / 2, k2 = ui_px(2);
    gfx_rect(s, x, y, w, h, c);
    for (int row = 0; row < 2; row++)
        for (int k = 0; k < 5; k++)
            gfx_fill(s, x + k2 + k * u, y + k2 + row * u, k2, k2, c);
    gfx_fill(s, x + 5 * u / 3, y + h - k2 - ui_px(1), 8 * u / 3, ui_px(1), c); /* the space bar */
}

#define ITEM_W (M.item_w) /* a window's button on the task bar: its program's icon */

/* The Apps button: its icon in a button like a window's, or the word without the icon */
static struct ui_rect menu_btn(void)
{
    struct ui_rect r = {ui_px(3), ui_px(3), MENU_BTN_W - ui_px(3), BAR_H - ui_px(6)};
    if (icon_own(APPS_ICON, ICON_BAR))
    {
        r.y = ui_px(2);
        r.w = ITEM_W - ui_px(2);
        r.h = BAR_H - ui_px(3);
    }
    return r;
}

/* where the windows' buttons may start */
static int menu_btn_end(void)
{
    struct ui_rect r = menu_btn();
    return r.x + r.w;
}

static struct ui_rect bar_item(int k)
{
    int avail = W - menu_btn_end() - CLOCK_W - (s_osk.available ? KBD_W : 0) - ui_px(8);
    int n = s_norder ? s_norder : 1;
    int bw = avail / n;
    if (bw > ITEM_W)
        bw = ITEM_W;
    struct ui_rect r = {menu_btn_end() + ui_px(4) + k * bw, ui_px(2), bw - ui_px(2), BAR_H - ui_px(3)};
    return r;
}

static void clock_text(char *buf, size_t size, const char *format)
{
    time_t now = (time_t)(crtos_wall_us() / 1000000);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, size, format, &tm);
}

static void draw_bar(void)
{
    const struct ui_theme *t = ui_theme;
    struct gfx_surface *s = &s_bar->s;
    gfx_fill(s, 0, 0, s->w, s->h, over(GFX_RGB(22, 26, 34), 0));
    gfx_hline(s, 0, 0, s->w, over(GFX_RGB(70, 80, 100), 40));
    struct ui_rect mb = menu_btn();
    bool menu_open = s_menu != NULL;
    uint32_t idle = s_hover == ITEM_MENU ? over(GFX_RGB(62, 72, 92), 60) : over(GFX_RGB(48, 56, 72), 40);
    gfx_round_rect_aa(s, mb.x, mb.y, mb.w, mb.h, M.radius, s_bar_press == ITEM_MENU || menu_open ? t->accent : idle, 0,
                      GFX_CORNERS_ALL);
    const struct gfx_image *apps = icon_own(APPS_ICON, ICON_BAR);
    if (apps)
        gfx_image_draw(s, mb.x + (mb.w - ICON_BAR) / 2, mb.y + (mb.h - ICON_BAR) / 2, apps);
    else
        ui_text_center(s, &mb, t->bold, "Apps", t->accent_text);
    for (int k = 0; k < s_norder; k++)
    {
        struct managed *m = &s_m[s_order[k]];
        struct ui_rect r = bar_item(k);
        bool active = m->focused && !m->minimized;
        uint32_t bg = s_bar_press == k ? t->button_pressed
                      : active         ? over(GFX_RGB(50, 62, 86), 60)
                      : s_hover == k   ? over(GFX_RGB(40, 47, 62), 50)
                                       : 0u;
        if (bg)
            gfx_round_rect_aa(s, r.x, r.y, r.w, r.h, M.radius, bg, 0, GFX_CORNERS_ALL);
        draw_icon(s, r.x + (r.w - ICON_BAR) / 2, r.y + ui_px(2), ICON_BAR, m->icon, m->title);
        /* under the icon: the window's mark, long and bright while it is the active one */
        int mw = active ? ui_px(12) : ui_px(6), mh = ui_px(2);
        uint32_t mc = active ? t->accent : m->minimized ? GFX_RGB(84, 92, 110) : GFX_RGB(140, 150, 170);
        gfx_round_rect_aa(s, r.x + (r.w - mw) / 2, r.y + r.h - mh, mw, mh, M.radius ? mh / 2 : 0, mc, 0, GFX_CORNERS_ALL);
    }
    if (s_osk.available)
    {
        struct ui_rect kr = kbd_button();
        idle = s_hover == ITEM_KBD ? over(GFX_RGB(62, 72, 92), 60) : over(GFX_RGB(48, 56, 72), 40);
        gfx_round_rect_aa(s, kr.x, kr.y, kr.w, kr.h, M.radius,
                          s_bar_press == ITEM_KBD ? t->button_pressed : s_osk.visible ? t->accent : idle, 0, GFX_CORNERS_ALL);
        draw_kbd_icon(s, &kr, s_osk.visible ? t->accent_text : t->text);
    }
    char clk[8];
    clock_text(clk, sizeof(clk), "%H:%M");
    struct ui_rect cr = {W - CLOCK_W, 0, CLOCK_W, BAR_H};
    ui_text_center(s, &cr, t->bold, clk, t->text);
    gfx_present(s_bar);
}

/* ---- tooltips ---------------------------------------------------------------------------------- */

static struct ui_rect item_rect(int item)
{
    struct ui_rect r = menu_btn(); /* ITEM_MENU */
    if (item >= 0)
        r = bar_item(item);
    else if (item == ITEM_KBD)
        r = kbd_button();
    else if (item == ITEM_CLOCK)
    {
        r.x = W - CLOCK_W;
        r.y = 0;
        r.w = CLOCK_W;
        r.h = BAR_H;
    }
    return r;
}

static bool tip_text(int item, char *buf, size_t size)
{
    buf[0] = 0;
    if (item >= 0 && item < s_norder)
    {
        const struct managed *m = &s_m[s_order[item]];
        snprintf(buf, size, "%s", m->title[0] ? m->title : m->icon);
    }
    else if (item == ITEM_MENU && !s_menu) /* (not over the open menu) */
        snprintf(buf, size, "Apps (Windows key)");
    else if (item == ITEM_KBD)
        snprintf(buf, size, "On-screen keyboard");
    else if (item == ITEM_CLOCK)
        clock_text(buf, size, "%A, %d %B %Y");
    return buf[0] != 0;
}

static void tip_hide(void)
{
    s_tip_due_us = 0;
    s_tip_item = ITEM_NONE;
    if (s_tip)
    {
        gfx_win_destroy(s_tip);
        s_tip = NULL;
    }
}

/* The tooltip of task bar @item, just above it */
static void tip_show(int item)
{
    char text[64];
    tip_hide();
    if (!tip_text(item, text, sizeof(text)))
        return;
    const struct ui_theme *t = ui_theme;
    int w = gfx_text_width(t->font, text) + ui_px(14);
    if (w > W - 4)
        w = W - 4;
    struct ui_rect r = item_rect(item);
    int x = r.x + r.w / 2 - w / 2;
    if (x > W - w - 2)
        x = W - w - 2;
    if (x < 2)
        x = 2;
    /* above everything, and the mouse goes through it to the task bar */
    s_tip = gfx_win_create(g, x, H - BAR_H - TIP_H - ui_px(3), w, TIP_H,
                           GFX_WIN_TOPMOST | GFX_WIN_NOFOCUS | GFX_WIN_NOFRAME | GFX_WIN_NOINPUT | popup_flags(),
                           "tooltip");
    if (!s_tip)
        return;
    struct gfx_surface *s = &s_tip->s;
    gfx_fill(s, 0, 0, w, TIP_H, GFX_TRANSPARENT);
    gfx_round_rect_aa(s, 0, 0, w, TIP_H, M.radius, over(GFX_RGB(40, 44, 54), 50), 0, GFX_CORNERS_ALL);
    gfx_round_rect_aa(s, 0, 0, w, TIP_H, M.radius, GFX_RGB(100, 110, 132), 1, GFX_CORNERS_ALL);
    int asc = gfx_font_ascent(t->font);
    ui_text_fit(s, t->font, ui_px(7), (TIP_H + asc) / 2, w - ui_px(12), text, t->text);
    gfx_present(s_tip);
    s_tip_item = item;
}

/* @item's tooltip in @us, unless the mouse or the finger moves on */
static void tip_later(int item, uint32_t us)
{
    s_tip_want = item;
    s_tip_due_us = crtos_time_us() + us;
}

/* The mouse is now over task bar @item (ITEM_NONE: none, or it left the task bar) */
static void bar_hover(int item)
{
    if (item == s_hover)
        return;
    s_hover = item;
    if (item != s_tip_quiet)
        s_tip_quiet = ITEM_NONE;
    if (item == ITEM_NONE || item == s_tip_quiet)
        tip_hide();
    else if (s_tip)
        tip_show(item); /* one is up: the next one follows at once */
    else
        tip_later(item, TIP_DELAY_US);
    draw_bar(); /* the highlight under the mouse */
}

static void bar_remove(int idx)
{
    int k = 0;
    for (int i = 0; i < s_norder; i++)
        if (s_order[i] != idx)
            s_order[k++] = s_order[i];
    s_norder = k;
}

static void bar_update(struct managed *m)
{
    int idx = (int)(m - s_m);
    bool listed = false;
    for (int i = 0; i < s_norder; i++)
        listed |= s_order[i] == idx;
    bool want = m->used && (m->mapped || m->minimized);
    if (want == listed)
        return;
    if (want)
        s_order[s_norder++] = idx;
    else
        bar_remove(idx);
    /* the items moved: what the mouse is over is found anew at its next move */
    tip_hide();
    s_hover = s_tip_quiet = ITEM_NONE;
}

/* ---- programs menu ----------------------------------------------------------------------------- */

static uint32_t parse_caps(const char *s)
{
    static const struct
    {
        const char *name;
        uint32_t cap;
    } names[] = {{"spawn", CAP_SPAWN}, {"kill", CAP_KILL}, {"module", CAP_MODULE}, {"sys", CAP_SYS}, {"dev", CAP_DEV}, {"all", CAP_ALL}};
    uint32_t caps = 0;
    while (*s && *s != ' ')
    {
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        {
            size_t n = strlen(names[i].name);
            if (!strncmp(s, names[i].name, n) && (s[n] == ',' || s[n] == ' ' || !s[n]))
                caps |= names[i].cap;
        }
        while (*s && *s != ',' && *s != ' ')
            s++;
        if (*s == ',')
            s++;
    }
    return caps;
}

static void load_apps(void)
{
    uint64_t now = crtos_time_us();
    if (s_apps_loaded_us && now - s_apps_loaded_us < 10000000u)
        return;
    s_apps_loaded_us = now;
    s_napps = 0;
    FILE *f = fopen(APPS_CFG, "r");
    if (!f)
    {
        printf("wm: %s: %s\n", APPS_CFG, strerror(errno));
        return;
    }
    char line[160];
    while (s_napps < MAXAPPS && fgets(line, sizeof(line), f))
    {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || !*p)
            continue;
        char *colon = strchr(p, ':');
        if (!colon)
            continue;
        *colon = 0;
        struct app *a = &s_apps[s_napps];
        snprintf(a->title, sizeof(a->title), "%.31s", p);
        p = colon + 1;
        while (*p == ' ' || *p == '\t')
            p++;
        a->caps = 0;
        a->icon[0] = 0;
        for (;;)
        { /* the options, in any order */
            if (!strncmp(p, "caps=", 5))
                a->caps = parse_caps(p + 5);
            else if (!strncmp(p, "icon=", 5))
                snprintf(a->icon, sizeof(a->icon), "%.*s", (int)strcspn(p + 5, " \t\r\n"), p + 5);
            else
                break;
            while (*p && *p != ' ' && *p != '\t')
                p++;
            while (*p == ' ' || *p == '\t')
                p++;
        }
        size_t n = strcspn(p, "\r\n");
        p[n] = 0;
        if (!*p)
            continue;
        snprintf(a->cmd, sizeof(a->cmd), "%s", p);
        p[strcspn(p, " \t")] = 0; /* (the program, without its arguments) */
        struct stat st;
        if (stat(p, &st))
            continue; /* not on the card: a system built without the optional programs */
        if (!a->icon[0])
            program_name(a->cmd, a->icon, sizeof(a->icon));
        s_napps++;
    }
    fclose(f);
}

static void launch(const struct app *a)
{
    char buf[sizeof(a->cmd)];
    snprintf(buf, sizeof(buf), "%s", a->cmd);
    const char *argv[12];
    int argc = 0;
    for (char *tok = strtok(buf, " \t"); tok && argc < 11; tok = strtok(NULL, " \t"))
        argv[argc++] = tok;
    argv[argc] = NULL;
    if (!argc)
        return;
    const char *prog = strrchr(argv[0], '/');
    const char *path = argv[0];
    argv[0] = prog ? prog + 1 : argv[0];
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = path;
    sp.argv = argv;
    sp.stdio[0] = sp.stdio[1] = sp.stdio[2] = -1;
    sp.caps = a->caps & s_uid_caps;
    int pid = crtos_spawn(&sp);
    if (pid < 0)
        printf("wm: cannot start %s: %s\n", path, strerror(errno));
    else
        printf("wm: started %s (pid %d)\n", path, pid);
}

static void menu_row(const struct gfx_surface *s, const struct ui_rect *r, int row, bool sel, void *ctx)
{
    (void)ctx;
    const struct ui_theme *t = ui_theme;
    int asc = gfx_font_ascent(t->font);
    int pad = ui_px(8);
    draw_icon(s, r->x + pad, r->y + (r->h - ICON_MENU) / 2, ICON_MENU, s_apps[row].icon, s_apps[row].title);
    ui_text_fit(s, t->font, r->x + ICON_MENU + 2 * pad, r->y + (r->h + asc) / 2, r->w - ICON_MENU - 3 * pad + 2,
                s_apps[row].title, sel ? t->accent_text : t->text);
    if (!M.radius) /* (rounded: the rows are marked by the selection alone) */
        gfx_hline(s, r->x + pad, r->y + r->h - 1, r->w - 2 * pad, GFX_RGB(50, 58, 74));
}

static void menu_close(void)
{
    if (!s_menu)
        return;
    gfx_win_destroy(s_menu);
    s_menu = NULL;
    gfx_wm_keys(g, false); /* the keys go to the programs again */
    draw_bar();
}

static void menu_draw(void)
{
    struct gfx_surface *s = &s_menu->s;
    gfx_fill(s, 0, 0, s->w, s->h, GFX_TRANSPARENT);
    gfx_round_rect_aa(s, 0, 0, s->w, s->h, M.radius, over(ui_theme->panel, 0), 0, GFX_CORNERS_ALL);
    s_menu_list.no_bg = true;
    ui_list_draw(&s_menu_list, s);
    gfx_round_rect_aa(s, 0, 0, s->w, s->h, M.radius, ui_theme->border, 1, GFX_CORNERS_ALL);
    gfx_present(s_menu);
}

static void menu_open(void)
{
    uint64_t t0 = crtos_time_us();
    load_apps();
    uint64_t t1 = crtos_time_us();
    int rows = s_napps ? s_napps : 1, pad = ui_px(4) + M.radius / 2;
    int h = rows * MENU_ROW + 2 * pad;
    if (h > work_h() - 8)
        h = work_h() - 8;
    tip_hide();
    s_menu = gfx_win_create(g, ui_px(2), work_h() - h - ui_px(2), MENU_W, h,
                            GFX_WIN_TOPMOST | GFX_WIN_NOFOCUS | GFX_WIN_NOFRAME | GFX_WIN_HOVER | popup_flags(), "menu");
    uint64_t t2 = crtos_time_us();
    printf("wm: menu: apps %lu us, window %lu us%s\n", (unsigned long)(t1 - t0), (unsigned long)(t2 - t1),
           s_menu ? "" : " - failed");
    if (!s_menu)
        return;
    memset(&s_menu_list, 0, sizeof(s_menu_list));
    s_menu_list.r.x = 1;
    s_menu_list.r.y = pad;
    s_menu_list.r.w = MENU_W - 2;
    s_menu_list.r.h = h - 2 * pad;
    s_menu_list.row_h = MENU_ROW;
    s_menu_list.count = s_napps;
    s_menu_list.sel = -1;
    s_menu_list.draw_row = menu_row;
    menu_draw();
    gfx_wm_keys(g, true); /* the menu has the keyboard while it is open */
    draw_bar();
}

/* A key while the menu is open (gfxd gives them all to us then), or a Windows key */
static void menu_key(const struct gfx_event *ev)
{
    static struct ui_keys keys;
    int c = ui_key_char(&keys, ev); /* (follows Shift) */
    if (ev->code == KEY_LEFTMETA || ev->code == KEY_RIGHTMETA)
    {
        if (ev->value == 1)
        {
            if (s_menu)
                menu_close();
            else
            {
                menu_open();
                if (s_menu && s_napps)
                {
                    s_menu_list.sel = 0; /* opened from the keyboard: something to start with Enter */
                    menu_draw();
                }
            }
        }
        return;
    }
    if (!s_menu || !ev->value || !s_napps)
        return;
    int sel = s_menu_list.sel, n = s_napps, page = s_menu_list.r.h / MENU_ROW;
    if (page < 1)
        page = 1;
    switch (ev->code)
    {
    case KEY_UP:
        sel = sel <= 0 ? n - 1 : sel - 1;
        break;
    case KEY_DOWN:
        sel = sel < 0 || sel >= n - 1 ? 0 : sel + 1;
        break;
    case KEY_PAGEUP:
        sel = sel - page < 0 ? 0 : sel - page;
        break;
    case KEY_PAGEDOWN:
        sel = sel + page > n - 1 ? n - 1 : sel < 0 ? page - 1 : sel + page;
        break;
    case KEY_HOME:
        sel = 0;
        break;
    case KEY_END:
        sel = n - 1;
        break;
    case KEY_ENTER:
    case KEY_KPENTER:
        if (sel >= 0 && sel < n)
        {
            struct app a = s_apps[sel];
            menu_close();
            launch(&a);
        }
        return;
    case KEY_ESC:
        menu_close();
        return;
    default:
        if (c > ' ' && c < 127)
        { /* the next title that starts with it */
            for (int k = 1; k <= n; k++)
            {
                int i = ((sel < 0 ? -1 : sel) + k) % n;
                char t = s_apps[i].title[0];
                if ((t | 0x20) == (c | 0x20))
                {
                    sel = i;
                    break;
                }
            }
            break;
        }
        return;
    }
    s_menu_list.sel = sel;
    ui_list_show(&s_menu_list, sel);
    menu_draw();
}

static void menu_pointer(const struct gfx_event *ev)
{
    int r = ui_list_pointer(&s_menu_list, ev->kind, ev->x, ev->y);
    if (r == UI_NONE)
        return;
    menu_draw();
    if (r == UI_ACTIVATED && s_menu_list.sel >= 0 && s_menu_list.sel < s_napps)
    {
        struct app a = s_apps[s_menu_list.sel];
        menu_close();
        launch(&a);
    }
}

/* ---- windows ------------------------------------------------------------------------------------ */

/* The resize grip of a resizable window */
static void make_grip(struct managed *m)
{
    if (!resizable(m))
        return;
    m->grip = gfx_win_create(g, m->x + m->w - GRIP, m->y + TITLE_H + m->h - GRIP, GRIP, GRIP,
                             GFX_WIN_ALPHA | GFX_WIN_NOFOCUS | GFX_WIN_HIDDEN, "grip");
    if (m->grip)
    {
        draw_grip(m);
        place_grip(m, m->w, m->h);
        check_full(m);
        show_grip(m);
        draw_frame(m);
    }
}

static void place(struct managed *m, int *x, int *y)
{
    int fw = m->w, fh = m->h + TITLE_H;
    *x = ui_px(12) + (s_cascade % 6) * ui_px(26);
    *y = ui_px(4) + (s_cascade % 6) * ui_px(20);
    s_cascade++;
    if (*x + fw > W)
        *x = W - fw > 0 ? W - fw : 0;
    if (*y + fh > work_h())
        *y = work_h() - fh > 0 ? work_h() - fh : 0;
}

static void manage(const struct gfx_event *ev)
{
    if (by_id(ev->win))
        return;
    struct managed *m = NULL;
    for (int i = 0; i < MAXW && !m; i++)
        if (!s_m[i].used)
            m = &s_m[i];
    if (!m)
    {
        gfx_wm_attach(g, ev->win, NULL, ev->x, ev->y); /* no room: show it as it is */
        return;
    }
    memset(m, 0, sizeof(*m));
    m->used = true;
    m->id = ev->win;
    m->pid = ev->pid;
    m->w = ev->w;
    m->h = ev->hgt;
    m->mapped = ev->value != 0;
    m->flags = ev->flags;
    memcpy(m->title, ev->title, sizeof(m->title) - 1);
    proc_name(m->pid, m->icon, sizeof(m->icon));
    if (ev->kind == 1)
    {
        place(m, &m->x, &m->y);
    }
    else
    { /* keep the window where it is, the title bar above it */
        m->x = ev->x;
        m->y = ev->y - TITLE_H < 0 ? 0 : ev->y - TITLE_H;
    }
    m->frame = gfx_win_create(g, m->x, m->y, m->w, TITLE_H, GFX_WIN_HIDDEN | GFX_WIN_NOFOCUS | frame_flags(), "frame");
    if (!m->frame)
    {
        m->used = false;
        gfx_wm_attach(g, ev->win, NULL, ev->x, ev->y);
        return;
    }
    draw_frame(m);
    gfx_wm_attach(g, m->id, m->frame, 0, TITLE_H);
    make_grip(m);
    if (m->mapped)
        gfx_win_show(m->frame, true);
    bar_update(m);
    draw_bar();
}

/* The look changed (size, corners): a new title bar at the new size for @m's window, which
 * moves into it where it is, then the old one goes */
static void rebuild_frame(struct managed *m)
{
    struct gfx_win *old = m->frame;
    if (m->maximized)
        m->x = m->y = 0;
    struct gfx_win *f =
        gfx_win_create(g, m->x, m->y, m->w, TITLE_H, GFX_WIN_HIDDEN | GFX_WIN_NOFOCUS | frame_flags(), "frame");
    if (!f)
        return; /* (the old one stays: the old size, still usable) */
    m->frame = f;
    draw_frame(m);
    gfx_wm_attach(g, m->id, f, 0, TITLE_H); /* (gfxd shows it with its window) */
    if (m->grip)
    {
        gfx_win_destroy(m->grip);
        m->grip = NULL;
    }
    make_grip(m);
    if (m->mapped && !m->minimized)
        gfx_win_show(f, true);
    gfx_win_destroy(old);
    if (m->maximized) /* the work area changed with the task bar */
        gfx_wm_configure(g, m->id, W, work_h() - TITLE_H);
}

static void unmanage(struct managed *m)
{
    if (s_drag == m)
        s_drag = NULL;
    if (s_btn_win == m)
        s_btn_win = NULL;
    if (s_resize == m)
        s_resize = NULL;
    if (m->grip)
        gfx_win_destroy(m->grip);
    gfx_win_destroy(m->frame);
    m->used = false;
    bar_update(m);
    draw_bar();
}

static void minimize(struct managed *m)
{
    m->minimized = true;
    gfx_win_show(m->frame, false);
    draw_bar();
}

static void restore(struct managed *m)
{
    if (m->minimized)
    {
        m->minimized = false;
        if (m->mapped)
            gfx_win_show(m->frame, true);
    }
    gfx_wm_focus(g, m->id);
    draw_bar();
}

static void close_window(struct managed *m)
{
    uint64_t now = crtos_time_us();
    if (m->close_us && now - m->close_us > 700000u && now - m->close_us < 6000000u)
    {
        printf("wm: '%s' (pid %d) does not close - ending it\n", m->title, m->pid);
        crtos_kill(m->pid, -1);
        return;
    }
    m->close_us = now;
    gfx_wm_close(g, m->id);
}

static void frame_pointer(struct managed *m, const struct gfx_event *ev)
{
    int sx = GFX_EV_SX(ev), sy = GFX_EV_SY(ev);
    switch (ev->kind)
    {
    case GFX_PTR_DOWN:
    {
        struct ui_rect c = close_rect(m), n = min_rect(m), x = max_rect(m);
        bool on_max = resizable(m) && ui_inside(&x, ev->x, ev->y);
        if (ui_inside(&c, ev->x, ev->y) || ui_inside(&n, ev->x, ev->y) || on_max)
        {
            s_btn_win = m;
            s_btn = ui_inside(&c, ev->x, ev->y) ? 1 : on_max ? 3
                                                             : 2;
            draw_frame(m);
        }
        else
        {
            uint64_t now = crtos_time_us();
            if (resizable(m) && m->title_tap_us && now - m->title_tap_us < DOUBLE_TAP_US)
            {
                m->title_tap_us = 0;
                toggle_maximize(m);
                break;
            }
            m->title_tap_us = now;
            s_drag = m;
            s_drag_dx = sx - m->x;
            s_drag_dy = sy - m->y;
        }
        break;
    }
    case GFX_PTR_MOVE:
        if (s_drag == m)
        {
            int x = sx - s_drag_dx, y = sy - s_drag_dy;
            if (y < 0)
                y = 0;
            if (y > work_h() - TITLE_H)
                y = work_h() - TITLE_H;
            if (x < ui_px(40) - m->w)
                x = ui_px(40) - m->w;
            if (x > W - ui_px(40))
                x = W - ui_px(40);
            if (x != m->x || y != m->y)
            {
                m->x = x;
                m->y = y;
                gfx_win_move(m->frame, x, y);
                if (m->maximized)
                { /* moved away: an ordinary window of that size again */
                    m->maximized = false;
                    show_grip(m);
                    draw_frame(m);
                }
            }
        }
        break;
    case GFX_PTR_UP:
        if (s_btn_win == m)
        {
            int b = s_btn;
            struct ui_rect r = b == 1 ? close_rect(m) : min_rect(m);
            if (b == 3)
                r = max_rect(m);
            s_btn_win = NULL;
            s_btn = 0;
            draw_frame(m);
            if (ui_inside(&r, ev->x, ev->y))
            {
                if (b == 1)
                    close_window(m);
                else if (b == 3)
                    toggle_maximize(m);
                else
                    minimize(m);
            }
        }
        s_drag = NULL;
        break;
    default:
        break;
    }
}

static int bar_hit(int x, int y)
{
    struct ui_rect mb = {0, 0, menu_btn_end() + 2, BAR_H};
    if (ui_inside(&mb, x, y))
        return ITEM_MENU;
    struct ui_rect kr = kbd_button();
    if (s_osk.available && ui_inside(&kr, x, y))
        return ITEM_KBD;
    struct ui_rect cr = item_rect(ITEM_CLOCK);
    if (ui_inside(&cr, x, y))
        return ITEM_CLOCK;
    for (int k = 0; k < s_norder; k++)
    {
        struct ui_rect r = bar_item(k);
        if (ui_inside(&r, x, y))
            return k;
    }
    return ITEM_NONE;
}

static void bar_pointer(const struct gfx_event *ev)
{
    int hit = bar_hit(ev->x, ev->y);
    bool mouse = (ev->flags & GFX_PTR_MOUSE) != 0;
    if (ev->kind == GFX_PTR_HOVER || ev->kind == GFX_PTR_LEAVE)
    {
        bar_hover(ev->kind == GFX_PTR_HOVER ? hit : ITEM_NONE);
        return;
    }
    if (ev->kind == GFX_PTR_DOWN)
    {
        tip_hide();
        if (mouse)
            s_tip_quiet = hit; /* clicked: no tooltip until the mouse leaves it */
        else if (hit != ITEM_NONE && hit != ITEM_MENU)
            tip_later(hit, TIP_HOLD_US); /* a finger held on it: its tooltip */
        s_bar_press = hit;
        draw_bar();
        return;
    }
    if (ev->kind != GFX_PTR_UP)
        return;
    int was = s_bar_press;
    bool peeked = !mouse && s_tip; /* held to see the tooltip: that was all */
    tip_hide();
    s_bar_press = ITEM_NONE;
    if (was == ITEM_NONE || was == ITEM_CLOCK || hit != was || peeked)
    {
        draw_bar();
        return;
    }
    if (was == ITEM_KBD)
    {
        uint64_t now = crtos_time_us();
        if (now - s_osk_toggled_us >= 300000u) /* one tap, reported twice */
        {
            s_osk_toggled_us = now;
            osk_request(OSK_TOGGLE);
        }
        draw_bar();
        return;
    }
    if (was == ITEM_MENU)
    {
        uint64_t now = crtos_time_us();
        if (now - s_menu_toggled_us < 300000u) /* one tap, reported twice */
            return;
        s_menu_toggled_us = now;
        if (s_menu)
            menu_close();
        else
            menu_open();
        return;
    }
    menu_close();
    struct managed *m = &s_m[s_order[was]];
    if (!m->minimized && m->focused)
        minimize(m);
    else
        restore(m);
    draw_bar();
}

static void handle(const struct gfx_event *ev)
{
    struct managed *m;
    switch (ev->h.type)
    {
    case GFX_EV_WM_CREATE:
        manage(ev);
        break;
    case GFX_EV_WM_DESTROY:
        if ((m = by_id(ev->win)))
            unmanage(m);
        break;
    case GFX_EV_WM_TITLE:
        if ((m = by_id(ev->win)))
        {
            memcpy(m->title, ev->title, sizeof(m->title) - 1);
            draw_frame(m);
            draw_bar();
            if (s_tip && s_tip_item >= 0 && s_tip_item < s_norder && &s_m[s_order[s_tip_item]] == m)
                tip_show(s_tip_item); /* it shows the old title */
        }
        break;
    case GFX_EV_WM_MAP:
        if ((m = by_id(ev->win)))
        {
            m->mapped = ev->value != 0;
            if (!m->minimized)
                gfx_win_show(m->frame, m->mapped);
            bar_update(m);
            draw_bar();
        }
        break;
    case GFX_EV_WM_RESIZE:
        if ((m = by_id(ev->win)))
            resized(m, ev->w, ev->hgt);
        break;
    case GFX_EV_WM_FOCUS:
        if ((m = by_id(ev->win)))
        {
            m->focused = ev->value != 0;
            if (m->focused)
                menu_close();
            draw_frame(m);
            draw_bar();
        }
        break;
    case GFX_EV_KEY:
        menu_key(ev);
        break;
    case GFX_EV_SETTINGS:
        restyle();
        break;
    case GFX_EV_WHEEL:
        if (s_menu && ev->win == s_menu->id && ev->code == GFX_WHEEL_VERTICAL &&
            ui_list_wheel(&s_menu_list, ev->value) != UI_NONE)
            menu_draw();
        break;
    case GFX_EV_POINTER:
        if (s_trace && (ev->kind == GFX_PTR_DOWN || ev->kind == GFX_PTR_UP))
            printf("wm: %s win %ld at %d,%d (screen %d,%d)\n", ev->kind == GFX_PTR_DOWN ? "down" : "up", (long)ev->win,
                   ev->x, ev->y, GFX_EV_SX(ev), GFX_EV_SY(ev));
        if (!ev->win)
        { /* the desktop */
            menu_close();
        }
        else if (s_bar && ev->win == s_bar->id)
        {
            bar_pointer(ev);
        }
        else if (s_menu && ev->win == s_menu->id)
        {
            menu_pointer(ev);
        }
        else if ((m = by_frame(ev->win)))
        {
            if (ev->kind == GFX_PTR_DOWN)
                menu_close();
            frame_pointer(m, ev);
        }
        else if ((m = by_grip(ev->win)))
        {
            if (ev->kind == GFX_PTR_DOWN)
                menu_close();
            grip_pointer(m, ev);
        }
        break;
    default:
        break;
    }
}

static struct gfx_win *bar_create(void)
{
    uint32_t f = GFX_WIN_TOPMOST | GFX_WIN_NOFOCUS | GFX_WIN_NOFRAME | GFX_WIN_HOVER | (M.alpha < 255 ? GFX_WIN_ALPHA : 0u);
    return gfx_win_create(g, 0, H - BAR_H, W, BAR_H, f, "taskbar");
}

/* The menu's icons now: it opens without reading them */
static void preload_icons(void)
{
    load_apps();
    for (int i = 0; i < s_napps; i++)
        icon_get(s_apps[i].icon, ICON_MENU);
}

/* The appearance settings changed (libgfx follows them in ui_theme already): everything at the
 * new size and look */
static void restyle(void)
{
    metrics();
    menu_close();
    tip_hide();
    s_hover = s_tip_quiet = ITEM_NONE;
    for (int i = 0; i < s_nicons; i++) /* (other sizes now) */
        free(s_icons[i].img);
    s_nicons = 0;
    struct gfx_win *bar = bar_create();
    if (bar)
    {
        gfx_win_destroy(s_bar);
        s_bar = bar;
    }
    for (int i = 0; i < MAXW; i++)
        if (s_m[i].used)
            rebuild_frame(&s_m[i]);
    draw_bar();
    s_apps_loaded_us = 0;
    preload_icons();
    printf("wm: new look: scale %d %%, corners %d, opacity %d\n", ui_theme->scale, M.radius, M.alpha);
}

static void load_timezone(void)
{
    char tz[64] = DEFAULT_TZ;
    FILE *f = fopen(TZ_CFG, "r");
    if (f)
    {
        if (fgets(tz, sizeof(tz), f))
            tz[strcspn(tz, "\r\n")] = 0;
        fclose(f);
    }
    setenv("TZ", tz, 1);
    tzset();
}

int main(int argc, char **argv)
{
    s_trace = argc > 1 && !strcmp(argv[1], "-t");
    struct crtos_procinfo pi;
    s_uid_caps = CAP_ALL;
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++)
        if (pi.pid == getpid())
            s_uid_caps = pi.caps;
    g = gfx_open();
    if (!g)
    {
        printf("wm: no graphics server\n");
        return 1;
    }
    if (gfx_wm_register(g))
    {
        printf("wm: %s\n", errno == EBUSY ? "another window manager runs" : strerror(errno));
        return 1;
    }
    W = gfx_screen_width(g);
    H = gfx_screen_height(g);
    gfx_pool_reserve(g, POOL_BYTES);
    load_timezone();
    metrics();
    s_bar = bar_create();
    if (!s_bar)
    {
        printf("wm: no task bar\n");
        return 1;
    }
    osk_request(OSK_STATE);
    draw_bar();
    preload_icons();
    printf("wm: managing windows (%d icons)\n", s_nicons);
    time_t shown_min = (time_t)(crtos_wall_us() / 60000000);
    for (;;)
    {
        time_t now = (time_t)(crtos_wall_us() / 1000000);
        uint32_t wait = (uint32_t)(60 - now % 60) * 1000u + 50u; /* next minute: the clock */
        uint32_t osk_every = s_osk.visible ? 500u : 3000u;       /* and the keyboard's state */
        if (wait > osk_every)
            wait = osk_every;
        if (s_tip_due_us)
        { /* and a tooltip */
            uint64_t t = crtos_time_us();
            uint32_t due = s_tip_due_us > t ? (uint32_t)((s_tip_due_us - t) / 1000u) + 1u : 0u;
            if (wait > due)
                wait = due;
        }
        struct gfx_event ev;
        if (gfx_next_event(g, &ev, wait) == 0)
            handle(&ev);
        if (s_tip_due_us && crtos_time_us() >= s_tip_due_us)
            tip_show(s_tip_want);
        time_t min = (time_t)(crtos_wall_us() / 60000000);
        bool redraw = false;
        if (min != shown_min)
        {
            shown_min = min;
            load_timezone();
            redraw = true;
        }
        if (crtos_time_us() - s_osk_asked_us >= osk_every * 1000u && osk_request(OSK_STATE))
            redraw = true; /* offered or hidden meanwhile (a keyboard came or went, "hide") */
        if (redraw)
            draw_bar();
        int status;
        while (crtos_wait(-1, &status, 0) > 0)
        {
        }
    }
}
