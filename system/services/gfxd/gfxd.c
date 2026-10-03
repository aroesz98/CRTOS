/*
 * gfxd - the graphics server (layer 2).
 *
 * Owns the display: composes the desktop and the windows of its clients into the hidden
 * frame buffer with the 2D accelerator (one batch per frame), then shows it at the next
 * frame start (no tearing). Only what changed is recomposed - a list of rectangles: the
 * damage of this frame plus that of the previous one, which the hidden buffer has not seen
 * yet. Clients learn with GFX_EV_FRAME that their damage is on the screen and may draw the
 * next frame.
 *
 * Composition starts shortly before the next frame start (the margin follows the measured
 * composition time), not at the first damage: clients that answer GFX_EV_FRAME a moment
 * later still get into the same frame, so an animation keeps the full frame rate while
 * others draw too.
 *
 * Input from inputd goes to the window under the finger (which comes to the top and gets
 * the focus); a press keeps its window until the finger lifts. Each finger of a multi-touch
 * screen has its own window that way: finger 0 as above, the others only to windows made
 * for them (GFX_WIN_MULTITOUCH) and without raising or focusing. Keys and gamepad sticks go
 * to the focused window, the Windows keys (and every key while its menu wants them) to the
 * window manager; a key's release goes where its press went. Wheel turns go to the window
 * under the mouse cursor, and so does a mouse moved without the button when that window asks
 * for it (GFX_WIN_HOVER), with GFX_PTR_LEAVE when the cursor moves on. A client that ends (its
 * event port hangs up) loses its windows.
 *
 * The clipboard (one text for all programs) lives here, and so does the screen copy of a
 * remote desktop: what changes goes into its shared memory with the accelerator, in a batch
 * of its own after the frame's (a bad copy never stops the screen), see gfx_proto.h.
 *
 * Window management (see gfx_proto.h): the policy - decorations, placement, a task bar -
 * belongs to a window manager client. gfxd only keeps each client window together with the
 * frame the manager attached it to (and the frame's decoration, such as a resize grip: they
 * move, stack and hide as one), holds new windows back until the manager has framed them,
 * hands the windows over when a manager ends or a new one starts, and passes size requests
 * on to the owners (GFX_EV_CONFIGURE), whose new pixels it takes over in one step
 * (GFX_WIN_RESIZE). Windows flagged GFX_WIN_TOPMOST stay above the others.
 *
 * The desktop below all windows is the wallpaper of the appearance settings (gfx_ui.h,
 * /sd/crtos/etc/ui.cfg), drawn once into memory of its own (libgfx gfx_wallpaper_draw). When a
 * program says the settings changed (GFX_SETTINGS) it is drawn anew, and every client hears of
 * the change (GFX_EV_SETTINGS) to follow the scale, fonts and colours.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/fb.h>
#include <crtos/gpu2d.h>
#include <crtos/keys.h>
#include "gfx.h"
#include "gfx_ui.h"

#define MAX_CLIENTS 16
#define MAX_WINDOWS 48
#define KEYS_HELD   16          /* keys held at once whose presses are remembered */

struct rect {
    int x, y, w, h;             /* w or h <= 0: empty */
};

struct client {
    bool used;
    int pid;
    int evport;
    bool clip_watch;            /* wants GFX_EV_CLIP */
};

struct window {
    bool used;
    bool mapped;                /* its owner wants it shown */
    bool visible;               /* on the screen */
    bool managed;               /* the window manager has framed it (or let it be) */
    bool place;                 /* its owner left the placement to us */
    bool focus_new;             /* takes the focus when it first appears */
    int id;
    struct client *c;
    int shm;                    /* its pixels (our handle) */
    struct gpu2d_hsurface surf;
    int x, y, w, h;
    uint32_t flags;
    struct window *frame;       /* client window or decoration: the frame it hangs on */
    struct window *child;       /* frame: the client window in it */
    struct window *deco;        /* frame: the manager's decoration on it (above the client) */
    int fx, fy;                 /* client window: its position in the frame */
    uint64_t born_us;           /* waits at most GFX_WM_WAIT_MS for the manager */
    bool frame_wanted;          /* damaged, the client waits for GFX_EV_FRAME */
    bool in_flip;               /* its damage is in the frame waiting to be shown */
    char title[32];
};

static struct client s_client[MAX_CLIENTS];
static struct window s_win[MAX_WINDOWS];
static int s_z[MAX_WINDOWS], s_nz;          /* stacking order, bottom first */
static int s_next_id = 1;
static struct client *s_wm;                 /* the window manager */

static int s_fb = -1, s_gpu = -1, s_port = -1;
static struct fb_info s_info;
static int s_fbh[2];                        /* frame buffers as shared memory */
static int s_front, s_back;
static bool s_flip_pending;

#define MAX_RECTS 8
struct region {
    int n;
    struct rect r[MAX_RECTS];
};
static struct region s_damage, s_prev;

static uint64_t s_vsync_us;                 /* last frame start seen */
static uint32_t s_period_us;                /* display refresh period */
static uint32_t s_recent_us;                /* composition time, moving average */
static struct window *s_grab[GFX_FINGERS], *s_focus;   /* each finger's window */
static struct window *s_hover;              /* the GFX_WIN_HOVER window under the mouse cursor */
static int16_t s_stick[GFX_STICKS][2];     /* the gamepad's sticks as inputd last sent them */
static int s_place;

static int s_desk = -1;                     /* desktop picture */
static struct gpu2d_hsurface s_desk_surf;
static void draw_desktop(void);
static void settings_changed(void);

static struct gfx_stats s_st;
static uint64_t s_sum_us;

/* keys: the manager takes them all (its menu is open); where each held key's press went */
static bool s_wm_keys;
static struct {
    uint16_t code;
    bool wm;                    /* to the manager */
    struct window *w;           /* else to this window (NULL: it is gone) */
} s_held[KEYS_HELD];
static int s_nheld;

/* the clipboard, and a new text coming in pieces */
static char *s_clip, *s_clip_new;
static uint32_t s_clip_len, s_clip_serial, s_clip_new_len, s_clip_new_have;
static int s_clip_new_pid;

/* the screen copy of a remote desktop (GFX_SCREEN_WATCH) */
static struct {
    struct client *c;           /* NULL: none */
    int shm;
    struct gpu2d_hsurface surf;
    struct region pending;      /* changed since the last GFX_SCREEN_TAKE */
    bool told;                  /* GFX_EV_SCREEN sent for it */
} s_watch;

/* ---- rectangles --------------------------------------------------------------------------- */

static bool empty(const struct rect *r)
{
    return r->w <= 0 || r->h <= 0;
}

static void unite(struct rect *a, const struct rect *b)
{
    if (empty(b))
        return;
    if (empty(a)) {
        *a = *b;
        return;
    }
    int x0 = a->x < b->x ? a->x : b->x, y0 = a->y < b->y ? a->y : b->y;
    int x1 = a->x + a->w > b->x + b->w ? a->x + a->w : b->x + b->w;
    int y1 = a->y + a->h > b->y + b->h ? a->y + a->h : b->y + b->h;
    a->x = x0;
    a->y = y0;
    a->w = x1 - x0;
    a->h = y1 - y0;
}

static struct rect intersect(const struct rect *a, const struct rect *b)
{
    struct rect r;
    r.x = a->x > b->x ? a->x : b->x;
    r.y = a->y > b->y ? a->y : b->y;
    int x1 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
    int y1 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
    r.w = x1 - r.x;
    r.h = y1 - r.y;
    return r;
}

static struct rect win_rect(const struct window *w)
{
    struct rect r = { w->x, w->y, w->w, w->h };
    return r;
}

static int area(const struct rect *r)
{
    return empty(r) ? 0 : r->w * r->h;
}

/* Worth merging: they overlap, or together they waste little space */
static bool mergeable(const struct rect *a, const struct rect *b)
{
    struct rect u = *a;
    unite(&u, b);
    struct rect i = intersect(a, b);
    return !empty(&i) || area(&u) * 4 <= (area(a) + area(b)) * 5;
}

static void region_add(struct region *g, const struct rect *r)
{
    if (empty(r))
        return;
    struct rect cur = *r;
    for (;;) { /* merge with whatever it touches, repeatedly */
        int k = -1;
        for (int i = 0; i < g->n && k < 0; i++)
            if (mergeable(&g->r[i], &cur))
                k = i;
        if (k < 0)
            break;
        unite(&cur, &g->r[k]);
        g->r[k] = g->r[--g->n];
    }
    if (g->n == MAX_RECTS) { /* full: into the rectangle that grows least */
        int best = 0, grow = 0x7FFFFFFF;
        for (int i = 0; i < g->n; i++) {
            struct rect u = g->r[i];
            unite(&u, &cur);
            int d = area(&u) - area(&g->r[i]);
            if (d < grow) {
                grow = d;
                best = i;
            }
        }
        unite(&cur, &g->r[best]);
        g->r[best] = g->r[--g->n];
    }
    g->r[g->n++] = cur;
}

static void damage(const struct rect *r)
{
    struct rect screen = { 0, 0, s_info.width, s_info.height };
    struct rect c = intersect(r, &screen);
    region_add(&s_damage, &c);
}

static void damage_window(const struct window *w)
{
    if (w->visible) {
        struct rect r = win_rect(w);
        damage(&r);
    }
}

/* ---- composition ------------------------------------------------------------------------------ */

static struct gpu2d_hsurface screen_surface(int buffer)
{
    struct gpu2d_hsurface s = { s_fbh[buffer], 0, s_info.width, s_info.height, s_info.stride, GPU2D_FMT_RGB565 };
    return s;
}

static int s_nops;
static struct gpu2d_hop s_ops[GPU2D_BATCH_MAX];

static void flush_ops(void)
{
    if (!s_nops)
        return;
    struct gpu2d_hbatch b = { s_ops, (uint32_t)s_nops };
    if (ioctl(s_gpu, GPU2D_IOC_HBATCH, &b) < 0)
        printf("gfxd: composition failed: %s\n", strerror(errno));
    s_nops = 0;
}

static void op_blit(struct gpu2d_hsurface *dst, int dx, int dy, const struct gpu2d_hsurface *src, int sx, int sy, int w,
                    int h, uint32_t flags)
{
    if (s_nops >= GPU2D_BATCH_MAX)
        flush_ops();
    struct gpu2d_hop *op = &s_ops[s_nops++];
    op->op = GPU2D_OP_BLIT;
    op->blit.dst = *dst;
    op->blit.dx = (int16_t)dx;
    op->blit.dy = (int16_t)dy;
    op->blit.src = *src;
    op->blit.src_rect.x = (int16_t)sx;
    op->blit.src_rect.y = (int16_t)sy;
    op->blit.src_rect.w = (uint16_t)w;
    op->blit.src_rect.h = (uint16_t)h;
    op->blit.flags = flags;
}

static void op_fill(struct gpu2d_hsurface *dst, const struct rect *r, uint32_t argb)
{
    if (s_nops >= GPU2D_BATCH_MAX)
        flush_ops();
    struct gpu2d_hop *op = &s_ops[s_nops++];
    op->op = GPU2D_OP_FILL;
    op->fill.dst = *dst;
    op->fill.rect.x = (int16_t)r->x;
    op->fill.rect.y = (int16_t)r->y;
    op->fill.rect.w = (uint16_t)r->w;
    op->fill.rect.h = (uint16_t)r->h;
    op->fill.argb = argb;
}

/* ---- occlusion: every pixel is written once, from the topmost opaque window ------------------- */

#define MAX_PARTS 16
struct parts {
    int n;
    bool full;                  /* did not fit */
    struct rect r[MAX_PARTS];
};

static void parts_add(struct parts *p, const struct rect *r)
{
    if (empty(r))
        return;
    if (p->n == MAX_PARTS)
        p->full = true;
    else
        p->r[p->n++] = *r;
}

/* a minus b, as up to four rectangles */
static void subtract(const struct rect *a, const struct rect *b, struct parts *out)
{
    struct rect i = intersect(a, b);
    if (empty(&i)) {
        parts_add(out, a);
        return;
    }
    struct rect top = { a->x, a->y, a->w, i.y - a->y };
    struct rect bottom = { a->x, i.y + i.h, a->w, a->y + a->h - (i.y + i.h) };
    struct rect left = { a->x, i.y, i.x - a->x, i.h };
    struct rect right = { i.x + i.w, i.y, a->x + a->w - (i.x + i.w), i.h };
    parts_add(out, &top);
    parts_add(out, &bottom);
    parts_add(out, &left);
    parts_add(out, &right);
}

static struct parts s_vis[MAX_WINDOWS];     /* per stacking position: what is seen of it */

static void blit_window(struct gpu2d_hsurface *dst, const struct window *w, const struct rect *i)
{
    op_blit(dst, i->x, i->y, &w->surf, i->x - w->x, i->y - w->y, i->w, i->h,
            (w->flags & GFX_WIN_ALPHA) ? GPU2D_BLEND : 0);
}

static void desk_blit(struct gpu2d_hsurface *dst, const struct rect *r)
{
    if (s_desk >= 0)
        op_blit(dst, r->x, r->y, &s_desk_surf, r->x, r->y, r->w, r->h, 0);
    else
        op_fill(dst, r, GFX_RGB(24, 40, 64));
}

/* Compose one damaged rectangle: find top-down what each window shows of it (opaque windows
 * hide what is below them; translucent ones do not), then draw that bottom-up */
static void compose_rect(struct gpu2d_hsurface *dst, const struct rect *rr)
{
    struct parts open = { 1, false, { *rr } };
    bool ok = true;
    for (int z = s_nz - 1; z >= 0 && ok; z--) {
        struct window *w = &s_win[s_z[z]];
        struct parts *v = &s_vis[z];
        v->n = 0;
        v->full = false;
        if (!w->visible)
            continue;
        struct rect wr = win_rect(w);
        for (int k = 0; k < open.n; k++) {
            struct rect i = intersect(&open.r[k], &wr);
            parts_add(v, &i);
        }
        if (!v->n || (w->flags & GFX_WIN_ALPHA))
            continue;
        struct parts next = { 0, false, { { 0, 0, 0, 0 } } };
        for (int k = 0; k < open.n; k++)
            subtract(&open.r[k], &wr, &next);
        open = next;
        ok = !v->full && !next.full;
    }
    if (!ok) { /* too fragmented: plain painter's order */
        desk_blit(dst, rr);
        for (int z = 0; z < s_nz; z++) {
            struct window *w = &s_win[s_z[z]];
            if (!w->visible)
                continue;
            struct rect wr = win_rect(w);
            struct rect i = intersect(&wr, rr);
            if (!empty(&i))
                blit_window(dst, w, &i);
        }
        return;
    }
    for (int k = 0; k < open.n; k++)
        desk_blit(dst, &open.r[k]);
    for (int z = 0; z < s_nz; z++)
        for (int k = 0; k < s_vis[z].n; k++)
            blit_window(dst, &s_win[s_z[z]], &s_vis[z].r[k]);
}

static uint32_t s_show_frame;               /* display frame when the last flip was asked for */

static void stop_watch(void)
{
    if (s_watch.c)
        close(s_watch.shm);
    memset(&s_watch, 0, sizeof(s_watch));
}

/* The copy of a remote desktop: what this frame changed, from the buffer just composed (it
 * stays as it is until the next composition, after the flip). A batch of its own, so that a
 * copy the accelerator refuses (shared memory too small) stops only the copy. */
static void copy_to_watcher(const struct gpu2d_hsurface *src)
{
    if (!s_watch.c || !s_damage.n)
        return;
    s_nops = 0;
    for (int k = 0; k < s_damage.n; k++) {
        const struct rect *r = &s_damage.r[k];
        op_blit(&s_watch.surf, r->x, r->y, src, r->x, r->y, r->w, r->h, 0);
        region_add(&s_watch.pending, r);
    }
    struct gpu2d_hbatch b = { s_ops, (uint32_t)s_nops };
    s_nops = 0;
    if (ioctl(s_gpu, GPU2D_IOC_HBATCH, &b) < 0) {
        printf("gfxd: screen copy failed (%s): stopped\n", strerror(errno));
        stop_watch();
        return;
    }
    if (!s_watch.told && s_watch.c->evport >= 0) {
        struct gfx_event ev = { { GFX_EV_SCREEN, GFX_EVENT_SHORT }, 0, 0, 0, 0, 0, 0,
                                (uint32_t)(crtos_time_us() / 1000u), 0, 0, 0, 0, { 0 } };
        crtos_msg_send(s_watch.c->evport, &ev, GFX_EVENT_SHORT, -1, 0);
        s_watch.told = true;
    }
}

/* Show what changed: compose it into the hidden buffer and flip. That buffer still lacks
 * what the previous frame changed, so that area is redrawn as well - in the same frame, never
 * as a flip of its own (which would halve the frame rate of an animation). */
static void compose(void)
{
    if (!s_damage.n)
        return;
    struct region region = s_damage;
    for (int i = 0; i < s_prev.n; i++)
        region_add(&region, &s_prev.r[i]);
    uint64_t t0 = crtos_time_us();
    if (s_vsync_us && t0 - s_vsync_us < s_period_us) {
        uint32_t lag = (uint32_t)(t0 - s_vsync_us);
        s_st.lag_us = s_st.lag_us ? s_st.lag_us - s_st.lag_us / 8u + lag / 8u : lag;
    }
    struct gpu2d_hsurface dst = screen_surface(s_back);
    s_nops = 0;
    for (int k = 0; k < region.n; k++)
        compose_rect(&dst, &region.r[k]);
    flush_ops();
    ioctl(s_fb, FB_IOC_GET_FRAME, &s_show_frame);
    ioctl(s_fb, FB_IOC_SHOW, s_back);
    s_flip_pending = true;
    copy_to_watcher(&dst);
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (s_win[i].used && s_win[i].frame_wanted) {
            s_win[i].in_flip = true;
            s_win[i].frame_wanted = false;
        }
    s_prev = s_damage;
    s_damage.n = 0;
    uint32_t us = (uint32_t)(crtos_time_us() - t0);
    s_st.composed++;
    s_st.last_us = us;
    if (us > s_st.max_us)
        s_st.max_us = us;
    s_sum_us += us;
    s_st.avg_us = (uint32_t)(s_sum_us / s_st.composed);
    s_recent_us = s_recent_us ? s_recent_us - s_recent_us / 8u + us / 8u : us;
}

/* When to compose so that the flip catches the coming frame start */
static uint64_t compose_time(uint64_t now)
{
    /* nothing shown lately: no animation to wait for, and the phase may have drifted */
    if (!s_vsync_us || !s_period_us || now - s_vsync_us > 2u * s_period_us)
        return now;
    /* room for a composition and some slack; at least 3 ms, at most 2/3 of a frame */
    uint32_t margin = s_recent_us * 2u + 1500u;
    if (margin < 3000u)
        margin = 3000u;
    if (margin > s_period_us * 2u / 3u)
        margin = s_period_us * 2u / 3u;
    uint64_t next = s_vsync_us + ((now - s_vsync_us) / s_period_us + 1u) * s_period_us;
    uint64_t at = next - margin;
    return at > now ? at : now;
}

/* ---- clients and events ----------------------------------------------------------------------- */

static void send_event(struct window *w, uint16_t type, uint16_t kind, uint16_t code, int x, int y, int32_t value)
{
    if (!w || !w->c || w->c->evport < 0)
        return;
    struct gfx_event ev = { { type, GFX_EVENT_SHORT }, w->id, kind, code, (int16_t)x, (int16_t)y, value,
                            (uint32_t)(crtos_time_us() / 1000u), 0, 0, 0, 0, { 0 } };
    /* a client that does not read loses events */
    crtos_msg_send(w->c->evport, &ev, GFX_EVENT_SHORT, -1, 0);
}

/* A pointer or wheel event: with flags (GFX_PTR_MOUSE), to window @w, or to the window
 * manager as win 0 when @w is NULL */
static void send_ptr_event(struct window *w, uint16_t type, uint16_t kind, uint16_t code, int x, int y,
                           int32_t value, uint32_t flags)
{
    struct client *c = w ? w->c : s_wm;
    if (!c || c->evport < 0)
        return;
    struct gfx_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.h.type = type;
    ev.h.size = (uint16_t)GFX_EVENT_PTR;
    ev.win = w ? w->id : 0;
    ev.kind = kind;
    ev.code = code;
    ev.x = (int16_t)x;
    ev.y = (int16_t)y;
    ev.value = value;
    ev.time_ms = (uint32_t)(crtos_time_us() / 1000u);
    ev.flags = flags;
    crtos_msg_send(c->evport, &ev, (size_t)GFX_EVENT_PTR, -1, 0);
}

/* The cursor is no longer over @w (it hides, or the mouse moved on): tell it once */
static void hover_leave(struct window *w)
{
    if (!s_hover || s_hover != w)
        return;
    s_hover = NULL;
    send_ptr_event(w, GFX_EV_POINTER, GFX_PTR_LEAVE, 0, 0, 0, 0, GFX_PTR_MOUSE);
}

/* A key event for the window manager (win 0) */
static void send_wm_key(uint16_t code, int32_t value)
{
    if (!s_wm || s_wm->evport < 0)
        return;
    struct gfx_event ev = { { GFX_EV_KEY, GFX_EVENT_SHORT }, 0, 0, code, 0, 0, value,
                            (uint32_t)(crtos_time_us() / 1000u), 0, 0, 0, 0, { 0 } };
    crtos_msg_send(s_wm->evport, &ev, GFX_EVENT_SHORT, -1, 0);
}

/* A window the manager decorates: not its own, not a panel or a special one */
static bool wants_manager(const struct window *w)
{
    return w->c != s_wm && !(w->flags & (GFX_WIN_NOFRAME | GFX_WIN_TOPMOST));
}

static void wm_notify(uint16_t type, const struct window *w, uint16_t kind, int32_t value)
{
    if (!s_wm || s_wm->evport < 0 || !wants_manager(w))
        return;
    struct gfx_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.h.type = type;
    ev.h.size = sizeof(ev);
    ev.win = w->id;
    ev.kind = kind;
    ev.x = (int16_t)w->x;
    ev.y = (int16_t)w->y;
    ev.value = value;
    ev.time_ms = (uint32_t)(crtos_time_us() / 1000u);
    ev.pid = w->c ? w->c->pid : 0;
    ev.w = (uint16_t)w->w;
    ev.hgt = (uint16_t)w->h;
    ev.flags = w->flags;
    memcpy(ev.title, w->title, sizeof(ev.title));
    crtos_msg_send(s_wm->evport, &ev, sizeof(ev), -1, 0);
}

static struct client *client_of(int pid)
{
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (s_client[i].used && s_client[i].pid == pid)
            return &s_client[i];
    return NULL;
}

static struct window *window_by_id(int id)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (s_win[i].used && s_win[i].id == id)
            return &s_win[i];
    return NULL;
}

static struct window *window_of(int id, int pid)
{
    struct window *w = window_by_id(id);
    return w && w->c && w->c->pid == pid ? w : NULL;
}

/* ---- stacking --------------------------------------------------------------------------------- */

static int layer(const struct window *w)
{
    if (w->frame)
        w = w->frame;
    return (w->flags & GFX_WIN_TOPMOST) ? 1 : 0;
}

static void z_remove(int idx)
{
    int k = 0;
    for (int i = 0; i < s_nz; i++)
        if (s_z[i] != idx)
            s_z[k++] = s_z[i];
    s_nz = k;
}

/* On top of the windows of its layer */
static void z_insert_top(int idx)
{
    int l = layer(&s_win[idx]), pos = s_nz;
    while (pos > 0 && layer(&s_win[s_z[pos - 1]]) > l)
        pos--;
    memmove(&s_z[pos + 1], &s_z[pos], (size_t)(s_nz - pos) * sizeof(s_z[0]));
    s_z[pos] = idx;
    s_nz++;
}

/* Bring a window (with its frame, or the frame with its window) to the top of its layer */
static void raise_window(struct window *w)
{
    if (w->frame)
        w = w->frame;
    int before[MAX_WINDOWS];
    memcpy(before, s_z, sizeof(before));
    z_remove((int)(w - s_win));
    z_insert_top((int)(w - s_win));
    if (w->child) {
        z_remove((int)(w->child - s_win));
        z_insert_top((int)(w->child - s_win));
    }
    if (w->deco) {
        z_remove((int)(w->deco - s_win));
        z_insert_top((int)(w->deco - s_win));
    }
    if (memcmp(before, s_z, (size_t)s_nz * sizeof(s_z[0]))) {
        damage_window(w);
        if (w->child)
            damage_window(w->child);
        if (w->deco)
            damage_window(w->deco);
    }
}

/* ---- visibility and focus --------------------------------------------------------------------- */

static bool should_show(const struct window *w)
{
    if (!w->mapped)
        return false;
    if (w->frame)
        return should_show(w->frame);
    if (s_wm && wants_manager(w) && !w->managed)
        return false; /* the manager frames it first */
    return true;
}

static void set_focus(struct window *w);

/* May it have the keyboard? (The manager's own windows never do.) */
static bool focusable(const struct window *w)
{
    return w && w->visible && !(w->flags & GFX_WIN_NOFOCUS) && w->c != s_wm;
}

/* The topmost window that may have the keyboard */
static void focus_top(void)
{
    for (int z = s_nz - 1; z >= 0; z--) {
        struct window *w = &s_win[s_z[z]];
        if (w->child)
            w = w->child;
        if (focusable(w)) {
            set_focus(w);
            return;
        }
    }
    set_focus(NULL);
}

static void update_visible(struct window *w)
{
    bool v = should_show(w);
    if (v != w->visible) {
        if (w->visible)
            damage_window(w);
        w->visible = v;
        damage_window(w);
        for (unsigned f = 0; !v && f < GFX_FINGERS; f++)
            if (s_grab[f] == w)
                s_grab[f] = NULL;
        if (!v)
            hover_leave(w);
        if (!v && s_focus == w)
            focus_top();
        if (v && w->focus_new) { /* a new window: when it shows up (maybe framed first) */
            w->focus_new = false;
            if (focusable(w))
                set_focus(w);
        }
    }
    if (w->child)
        update_visible(w->child);
    if (w->deco)
        update_visible(w->deco);
}

static void set_focus(struct window *w)
{
    if (s_focus == w)
        return;
    struct window *old = s_focus;
    s_focus = w;
    if (old) {
        send_event(old, GFX_EV_FOCUS, 0, 0, 0, 0, 0);
        wm_notify(GFX_EV_WM_FOCUS, old, 0, 0);
    }
    if (w) {
        send_event(w, GFX_EV_FOCUS, 0, 0, 0, 0, 1);
        wm_notify(GFX_EV_WM_FOCUS, w, 0, 1);
        for (unsigned i = 0; i < GFX_STICKS; i++) /* a stick held while the focus moves */
            if (s_stick[i][0] || s_stick[i][1])
                send_event(w, GFX_EV_STICK, 0, (uint16_t)i, s_stick[i][0], s_stick[i][1], 0);
    }
}

/* Move a window with what hangs on it */
static void move_group(struct window *w, int x, int y)
{
    damage_window(w);
    w->x = x;
    w->y = y;
    damage_window(w);
    struct window *hang[2] = { w->child, w->deco };
    for (int i = 0; i < 2; i++) {
        if (!hang[i])
            continue;
        damage_window(hang[i]);
        hang[i]->x = x + hang[i]->fx;
        hang[i]->y = y + hang[i]->fy;
        damage_window(hang[i]);
    }
}

/* Take a client window or a decoration off its frame */
static void detach(struct window *cw)
{
    if (cw->frame) {
        if (cw->frame->child == cw)
            cw->frame->child = NULL;
        if (cw->frame->deco == cw)
            cw->frame->deco = NULL;
        cw->frame = NULL;
    }
}

static void destroy_window(struct window *w)
{
    damage_window(w);
    if (w->child) { /* a frame: its window stays where it is */
        struct window *c = w->child;
        detach(c);
        update_visible(c);
    }
    if (w->deco) { /* the manager removes it too */
        struct window *d = w->deco;
        detach(d);
        d->mapped = false;
        update_visible(d);
    }
    if (w->frame)
        detach(w);
    if (s_wm)
        wm_notify(GFX_EV_WM_DESTROY, w, 0, 0);
    z_remove((int)(w - s_win));
    for (unsigned f = 0; f < GFX_FINGERS; f++)
        if (s_grab[f] == w)
            s_grab[f] = NULL;
    if (s_hover == w)
        s_hover = NULL;
    for (int k = 0; k < s_nheld; k++) /* its keys' releases go nowhere */
        if (s_held[k].w == w)
            s_held[k].w = NULL;
    bool had_focus = s_focus == w;
    if (had_focus)
        s_focus = NULL;
    close(w->shm);
    memset(w, 0, sizeof(*w));
    s_st.windows--;
    if (had_focus)
        focus_top();
}

static void drop_client(struct client *c)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (s_win[i].used && s_win[i].c == c)
            destroy_window(&s_win[i]);
    close(c->evport);
    if (s_watch.c == c)
        stop_watch();
    if (s_clip_new && s_clip_new_pid == c->pid) { /* a text it did not finish */
        free(s_clip_new);
        s_clip_new = NULL;
    }
    if (s_wm == c) { /* the manager ended: the windows it held back show up */
        s_wm = NULL;
        s_wm_keys = false;
        for (int i = 0; i < MAX_WINDOWS; i++)
            if (s_win[i].used)
                update_visible(&s_win[i]);
        printf("gfxd: window manager (pid %d) gone\n", c->pid);
    }
    memset(c, 0, sizeof(*c));
    s_st.clients--;
}

/* ---- requests ---------------------------------------------------------------------------------- */

static void do_hello(const struct crtos_msginfo *mi)
{
    struct gfx_hello_rep rep = { { GFX_HELLO, sizeof(rep) }, s_info.width, s_info.height, GPU2D_FMT_RGB565, 0 };
    if (mi->handle >= 0) { /* an input-only peer (inputd) passes no port */
        struct client *c = client_of(mi->pid);
        if (c) {
            close(c->evport);
        } else {
            for (int i = 0; i < MAX_CLIENTS && !c; i++)
                if (!s_client[i].used)
                    c = &s_client[i];
            if (!c) {
                close(mi->handle);
                crtos_msg_reply(mi->token, NULL, 0, -1);
                return;
            }
            c->used = true;
            c->pid = mi->pid;
            s_st.clients++;
        }
        c->evport = mi->handle;
        rep.client = (int32_t)(c - s_client);
    }
    crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
}

static void do_create(const struct crtos_msginfo *mi, const struct gfx_win_create *q)
{
    struct gfx_win_rep rep = { { GFX_WIN_CREATE, sizeof(rep) }, -EINVAL, 0, 0 };
    struct client *c = client_of(mi->pid);
    struct window *w = NULL;
    for (int i = 0; i < MAX_WINDOWS && !w; i++)
        if (!s_win[i].used)
            w = &s_win[i];
    if (!c || !w || mi->handle < 0 || !q->width || !q->height ||
        (q->format != GPU2D_FMT_RGB565 && q->format != GPU2D_FMT_XRGB8888 && q->format != GPU2D_FMT_ARGB8888)) {
        if (mi->handle >= 0)
            close(mi->handle);
        rep.id = !w ? -ENOSPC : -EINVAL;
        crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
        return;
    }
    memset(w, 0, sizeof(*w));
    w->used = true;
    w->id = s_next_id++;
    w->c = c;
    w->shm = mi->handle;
    w->w = q->width;
    w->h = q->height;
    w->flags = q->flags;
    w->surf.handle = w->shm;
    w->surf.offset = q->offset;
    w->surf.width = q->width;
    w->surf.height = q->height;
    w->surf.stride = q->stride;
    w->surf.format = q->format;
    w->born_us = crtos_time_us();
    memcpy(w->title, q->title, sizeof(w->title) - 1);
    if (q->x < 0) { /* cascade, unless the manager places it */
        w->place = true;
        w->x = 16 + (s_place % 6) * 28;
        w->y = 12 + (s_place % 6) * 22;
        s_place++;
    } else {
        w->x = q->x;
        w->y = q->y;
    }
    w->mapped = !(q->flags & GFX_WIN_HIDDEN);
    w->focus_new = !(q->flags & GFX_WIN_NOFOCUS);
    z_insert_top((int)(w - s_win));
    s_st.windows++;
    update_visible(w);
    rep.id = w->id;
    rep.x = (int16_t)w->x;
    rep.y = (int16_t)w->y;
    crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
    if (s_wm)
        wm_notify(GFX_EV_WM_CREATE, w, w->place ? 1 : 0, w->mapped);
}

static void do_damage(const struct crtos_msginfo *mi, const struct gfx_win_damage *q)
{
    struct window *w = window_of(q->id, mi->pid);
    if (!w)
        return;
    struct rect r = { w->x + q->x, w->y + q->y, q->w, q->hgt };
    struct rect wr = win_rect(w);
    r = intersect(&r, &wr);
    if (w->visible)
        damage(&r);
    w->frame_wanted = true;
}

static void do_move(const struct crtos_msginfo *mi, const struct gfx_win_move *q)
{
    struct window *w = window_of(q->id, mi->pid);
    if (!w)
        return;
    if (w->frame) /* a framed window moves with its frame */
        move_group(w->frame, q->x - w->fx, q->y - w->fy);
    else
        move_group(w, q->x, q->y);
}

static void do_show(const struct crtos_msginfo *mi, const struct gfx_win_show *q)
{
    struct window *w = window_of(q->id, mi->pid);
    if (!w || w->mapped == (q->visible != 0))
        return;
    w->mapped = q->visible != 0;
    update_visible(w);
    if (s_wm)
        wm_notify(GFX_EV_WM_MAP, w, 0, w->mapped);
    struct window *f = w->child ? w->child : w;
    if (!s_focus && focusable(f))
        set_focus(f);
}

static void do_title(const struct crtos_msginfo *mi, const struct gfx_win_title *q)
{
    struct window *w = window_of(q->id, mi->pid);
    if (!w)
        return;
    memcpy(w->title, q->title, sizeof(w->title) - 1);
    w->title[sizeof(w->title) - 1] = 0;
    if (s_wm)
        wm_notify(GFX_EV_WM_TITLE, w, 0, 0);
}

static void do_wm_register(const struct crtos_msginfo *mi)
{
    struct gfx_wm_rep rep = { { GFX_WM_REGISTER, sizeof(rep) }, 0 };
    struct client *c = client_of(mi->pid);
    if (!c) {
        rep.status = -ENOTCONN;
    } else if (s_wm && s_wm != c) {
        rep.status = -EBUSY;
    } else {
        s_wm = c;
        printf("gfxd: window manager is pid %d\n", c->pid);
    }
    crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
    if (rep.status)
        return;
    /* hand over the windows: they stay as they are until the manager frames them */
    for (int z = 0; z < s_nz; z++) {
        struct window *w = &s_win[s_z[z]];
        if (w->c == c || !wants_manager(w))
            continue;
        w->managed = true;
        wm_notify(GFX_EV_WM_CREATE, w, 0, w->mapped);
        if (s_focus == w)
            wm_notify(GFX_EV_WM_FOCUS, w, 0, 1);
    }
}

/* One of the manager's windows hung on a frame (a resize grip): above the client, moving and
 * hiding with the frame */
static void attach_deco(struct window *d, struct window *f, int dx, int dy)
{
    if (!f || f == d || d->child || d->deco || f->frame)
        return;
    damage_window(d);
    detach(d);
    if (f->deco && f->deco != d)
        detach(f->deco);
    d->frame = f;
    f->deco = d;
    d->fx = dx;
    d->fy = dy;
    d->x = f->x + dx;
    d->y = f->y + dy;
    z_remove((int)(d - s_win));
    z_insert_top((int)(d - s_win));
    raise_window(f);
    update_visible(d);
    damage_window(d);
}

static void do_wm_attach(const struct crtos_msginfo *mi, const struct gfx_wm_attach *q)
{
    struct window *cw = window_by_id(q->id);
    if (s_wm && s_wm->pid == mi->pid && cw && cw->c == s_wm) {
        attach_deco(cw, q->frame ? window_of(q->frame, mi->pid) : NULL, q->dx, q->dy);
        return;
    }
    if (!s_wm || s_wm->pid != mi->pid || !cw || !wants_manager(cw))
        return;
    struct window *f = NULL;
    if (q->frame) {
        f = window_of(q->frame, mi->pid);
        if (!f || f->frame || f == cw)
            return;
    }
    bool was = cw->visible;
    damage_window(cw);
    detach(cw);
    if (f && f->child)
        detach(f->child);
    cw->managed = true;
    if (f) {
        cw->frame = f;
        f->child = cw;
        cw->fx = q->dx;
        cw->fy = q->dy;
        cw->x = f->x + q->dx;
        cw->y = f->y + q->dy;
        z_remove((int)(cw - s_win));
        z_insert_top((int)(cw - s_win));
        raise_window(f);
        /* a window on the screen stays there: its frame (drawn by now) shows with it, so it
         * does not vanish for a moment and lose the focus (a new manager taking over) */
        if (was && !f->mapped) {
            f->mapped = true;
            update_visible(f);
        }
    } else {
        cw->x = q->dx;
        cw->y = q->dy;
    }
    update_visible(cw);
    damage_window(cw);
    if (!s_focus && focusable(cw))
        set_focus(cw);
}

static void do_wm_ref(const struct crtos_msginfo *mi, uint16_t type, const struct gfx_win_ref *q)
{
    struct window *cw = window_by_id(q->id);
    if (!s_wm || s_wm->pid != mi->pid || !cw || cw->c == s_wm)
        return;
    if (type == GFX_WM_CLOSE) {
        send_event(cw, GFX_EV_CLOSE, 0, 0, 0, 0, 0);
    } else { /* GFX_WM_FOCUS */
        raise_window(cw);
        if (focusable(cw))
            set_focus(cw);
    }
}

/* The manager asks for another size: the owner of a resizable window gets GFX_EV_CONFIGURE */
static void do_wm_configure(const struct crtos_msginfo *mi, const struct gfx_wm_configure *q)
{
    struct window *cw = window_by_id(q->id);
    if (!s_wm || s_wm->pid != mi->pid || !cw || cw->c == s_wm || !(cw->flags & GFX_WIN_RESIZABLE) || !cw->c ||
        cw->c->evport < 0)
        return;
    struct gfx_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.h.type = GFX_EV_CONFIGURE;
    ev.h.size = sizeof(ev);
    ev.win = cw->id;
    ev.time_ms = (uint32_t)(crtos_time_us() / 1000u);
    ev.w = q->width < 16 ? 16 : q->width > s_info.width ? (uint16_t)s_info.width : q->width;
    ev.hgt = q->height < 16 ? 16 : q->height > s_info.height ? (uint16_t)s_info.height : q->height;
    crtos_msg_send(cw->c->evport, &ev, sizeof(ev), -1, 0);
}

/* New pixels for a window (the owner drew them already): take them over in one step */
static void do_resize(const struct crtos_msginfo *mi, const struct gfx_win_resize *q)
{
    struct gfx_status rep = { { GFX_WIN_RESIZE, sizeof(rep) }, -EINVAL };
    struct window *w = window_of(q->id, mi->pid);
    if (!w || mi->handle < 0 || !q->width || !q->height || q->width > 4096 || q->height > 4096 ||
        (q->format != GPU2D_FMT_RGB565 && q->format != GPU2D_FMT_XRGB8888 && q->format != GPU2D_FMT_ARGB8888)) {
        if (mi->handle >= 0)
            close(mi->handle);
        rep.status = w ? -EINVAL : -ENOENT;
        crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
        return;
    }
    damage_window(w);
    close(w->shm);
    w->shm = mi->handle;
    w->w = q->width;
    w->h = q->height;
    w->surf.handle = w->shm;
    w->surf.offset = q->offset;
    w->surf.width = q->width;
    w->surf.height = q->height;
    w->surf.stride = q->stride;
    w->surf.format = q->format;
    damage_window(w);
    w->frame_wanted = true;
    rep.status = 0;
    crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
    if (s_wm)
        wm_notify(GFX_EV_WM_RESIZE, w, 0, 0);
}

/* ---- input ---------------------------------------------------------------------------------------- */

static struct window *window_at(int x, int y)
{
    for (int z = s_nz - 1; z >= 0; z--) {
        struct window *w = &s_win[s_z[z]];
        if (w->visible && !(w->flags & GFX_WIN_NOINPUT) && x >= w->x && y >= w->y && x < w->x + w->w &&
            y < w->y + w->h)
            return w;
    }
    return NULL;
}

static int32_t screen_pos(const struct gfx_input *in)
{
    return (int32_t)(((uint32_t)(uint16_t)in->y << 16) | (uint16_t)in->x);
}

/* A key: a release or a repeat goes where the press went (the focus may have moved since, or
 * the manager's menu closed); a press goes to the manager when it is a Windows key or the
 * manager takes all keys, else to the focused window */
static void do_key(uint16_t code, int32_t value)
{
    int k = -1;
    for (int i = 0; i < s_nheld; i++)
        if (s_held[i].code == code)
            k = i;
    bool to_wm;
    struct window *w;
    if (value != 1 && k >= 0) {
        to_wm = s_held[k].wm;
        w = s_held[k].w;
    } else {
        to_wm = s_wm && (s_wm_keys || code == KEY_LEFTMETA || code == KEY_RIGHTMETA);
        w = to_wm ? NULL : s_focus;
    }
    if (k >= 0 && value != 2) /* released, or pressed again: forget the old press */
        s_held[k] = s_held[--s_nheld];
    if (value == 1 && s_nheld < KEYS_HELD) {
        s_held[s_nheld].code = code;
        s_held[s_nheld].wm = to_wm;
        s_held[s_nheld].w = w;
        s_nheld++;
    }
    if (to_wm)
        send_wm_key(code, value);
    else if (w)
        send_event(w, GFX_EV_KEY, 0, code, 0, 0, value);
}

static void do_input(const struct gfx_input *in)
{
    unsigned finger = in->code;
    uint32_t pflags = (uint32_t)in->value & GFX_PTR_MOUSE; /* pointer: from a mouse? */
    switch (in->kind) {
    case GFX_PTR_DOWN: {
        if (finger >= GFX_FINGERS)
            break;
        struct window *w = window_at(in->x, in->y);
        if (finger) {
            /* another finger: only for a window that takes them, as it is */
            s_grab[finger] = w && (w->flags & GFX_WIN_MULTITOUCH) ? w : NULL;
            if (s_grab[finger])
                send_ptr_event(w, GFX_EV_POINTER, GFX_PTR_DOWN, (uint16_t)finger, in->x - w->x, in->y - w->y,
                               screen_pos(in), pflags);
            break;
        }
        s_grab[0] = w;
        if (!w && s_wm) /* the desktop belongs to the manager (win 0) */
            send_ptr_event(NULL, GFX_EV_POINTER, GFX_PTR_DOWN, 0, in->x, in->y, screen_pos(in), pflags);
        if (w) {
            raise_window(w);
            /* any part of a framed window (title bar, grip) gives the focus to its client */
            struct window *owner = w->frame ? w->frame : w;
            struct window *f = owner->child ? owner->child : w;
            if (focusable(f))
                set_focus(f);
            send_ptr_event(w, GFX_EV_POINTER, GFX_PTR_DOWN, 0, in->x - w->x, in->y - w->y, screen_pos(in), pflags);
        }
        break;
    }
    case GFX_PTR_MOVE:
    case GFX_PTR_UP: {
        if (finger >= GFX_FINGERS)
            break;
        struct window *w = s_grab[finger];
        if (w)
            send_ptr_event(w, GFX_EV_POINTER, in->kind, (uint16_t)finger, in->x - w->x, in->y - w->y, screen_pos(in),
                           pflags);
        if (in->kind == GFX_PTR_UP)
            s_grab[finger] = NULL;
        break;
    }
    case GFX_WHEEL: {
        struct window *w = window_at(in->x, in->y);
        if (w && in->code <= GFX_WHEEL_HORIZONTAL && in->value)
            send_ptr_event(w, GFX_EV_WHEEL, 0, in->code, in->x - w->x, in->y - w->y, in->value, GFX_PTR_MOUSE);
        break;
    }
    case GFX_PTR_HOVER: {
        /* only windows that asked; any other window under the cursor ends the hover too */
        struct window *w = window_at(in->x, in->y);
        if (w && !(w->flags & GFX_WIN_HOVER))
            w = NULL;
        if (s_hover != w)
            hover_leave(s_hover);
        s_hover = w;
        if (w)
            send_ptr_event(w, GFX_EV_POINTER, GFX_PTR_HOVER, 0, in->x - w->x, in->y - w->y, screen_pos(in),
                           GFX_PTR_MOUSE);
        break;
    }
    case GFX_KEY:
        do_key(in->code, in->value);
        break;
    case GFX_STICK:
        if (in->code >= GFX_STICKS)
            break;
        s_stick[in->code][0] = in->x;
        s_stick[in->code][1] = in->y;
        if (s_focus)
            send_event(s_focus, GFX_EV_STICK, 0, in->code, in->x, in->y, 0);
        break;
    default:
        break;
    }
}

/* ---- clipboard ------------------------------------------------------------------------------------ */

static void do_clip_put(const struct crtos_msginfo *mi, const struct gfx_clip *q)
{
    struct gfx_status rep = { { GFX_CLIP_PUT, sizeof(rep) }, 0 };
    if (q->total > GFX_CLIP_MAX || q->len > GFX_CLIP_PIECE || q->offset > q->total || q->len > q->total - q->offset) {
        rep.status = -EINVAL;
    } else if (q->offset == 0) { /* a new text: it replaces an unfinished one of anybody */
        free(s_clip_new);
        s_clip_new = (char *)malloc(q->total ? q->total : 1u);
        s_clip_new_len = q->total;
        s_clip_new_have = 0;
        s_clip_new_pid = mi->pid;
        if (!s_clip_new)
            rep.status = -ENOMEM;
    } else if (!s_clip_new || s_clip_new_pid != mi->pid || q->offset != s_clip_new_have ||
               q->total != s_clip_new_len) {
        rep.status = -EINVAL;
    }
    if (!rep.status) {
        memcpy(s_clip_new + q->offset, q->data, q->len);
        s_clip_new_have += q->len;
        if (s_clip_new_have == s_clip_new_len) { /* complete: the clipboard now */
            free(s_clip);
            s_clip = s_clip_new;
            s_clip_len = s_clip_new_len;
            s_clip_new = NULL;
            s_clip_serial++;
            for (int i = 0; i < MAX_CLIENTS; i++) {
                struct client *c = &s_client[i];
                if (!c->used || !c->clip_watch || c->evport < 0)
                    continue;
                struct gfx_event ev = { { GFX_EV_CLIP, GFX_EVENT_SHORT }, 0, 0, 0, 0, 0, (int32_t)s_clip_serial,
                                        (uint32_t)(crtos_time_us() / 1000u), 0, 0, 0, 0, { 0 } };
                crtos_msg_send(c->evport, &ev, GFX_EVENT_SHORT, -1, 0);
            }
        }
    }
    crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
}

static void do_clip_get(const struct crtos_msginfo *mi, const struct gfx_clip *q)
{
    static struct gfx_clip rep;
    rep.h.type = GFX_CLIP_GET;
    rep.total = s_clip_len;
    rep.serial = s_clip_serial;
    rep.offset = q->offset < s_clip_len ? q->offset : s_clip_len;
    uint32_t left = s_clip_len - rep.offset;
    rep.len = (uint16_t)(left < GFX_CLIP_PIECE ? left : GFX_CLIP_PIECE);
    if (rep.len)
        memcpy(rep.data, s_clip + rep.offset, rep.len);
    size_t size = offsetof(struct gfx_clip, data) + rep.len;
    rep.h.size = (uint16_t)size;
    crtos_msg_reply(mi->token, &rep, size, -1);
}

/* ---- the screen copy (remote desktop) ---------------------------------------------------------------- */

static uint32_t caps_of(int pid)
{
    struct crtos_procinfo pi;
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++)
        if (pi.pid == pid)
            return pi.caps;
    return 0;
}

/* It sees every window: only a client with CAP_SYS (the remote desktop service), one at a time.
 * The whole screen goes into the copy with the next frame. */
static void do_screen_watch(const struct crtos_msginfo *mi, const struct gfx_screen_watch *q)
{
    struct gfx_status rep = { { GFX_SCREEN_WATCH, sizeof(rep) }, 0 };
    struct client *c = client_of(mi->pid);
    if (!c)
        rep.status = -ENOTCONN;
    else if (!(caps_of(mi->pid) & CAP_SYS))
        rep.status = -EPERM;
    else if (s_watch.c && s_watch.c != c)
        rep.status = -EBUSY;
    else if (mi->handle < 0 || q->stride < (uint32_t)s_info.width * 2u)
        rep.status = -EINVAL;
    if (rep.status) {
        if (mi->handle >= 0)
            close(mi->handle);
        crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
        return;
    }
    stop_watch();
    s_watch.c = c;
    s_watch.shm = mi->handle;
    s_watch.surf.handle = mi->handle;
    s_watch.surf.offset = 0;
    s_watch.surf.width = (uint16_t)s_info.width;
    s_watch.surf.height = (uint16_t)s_info.height;
    s_watch.surf.stride = q->stride;
    s_watch.surf.format = GPU2D_FMT_RGB565;
    struct rect all = { 0, 0, s_info.width, s_info.height };
    damage(&all);
    printf("gfxd: screen copy for pid %ld\n", (long)mi->pid);
    crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
}

static void do_screen_take(const struct crtos_msginfo *mi)
{
    struct gfx_screen_take rep;
    memset(&rep, 0, sizeof(rep));
    rep.h.type = GFX_SCREEN_TAKE;
    rep.h.size = sizeof(rep);
    struct client *c = client_of(mi->pid);
    if (c && s_watch.c == c) {
        for (int i = 0; i < s_watch.pending.n && rep.n < GFX_SCREEN_RECTS; i++) {
            const struct rect *r = &s_watch.pending.r[i];
            rep.r[rep.n].x = (int16_t)r->x;
            rep.r[rep.n].y = (int16_t)r->y;
            rep.r[rep.n].w = (uint16_t)r->w;
            rep.r[rep.n].h = (uint16_t)r->h;
            rep.n++;
        }
        s_watch.pending.n = 0;
        s_watch.told = false;
    }
    crtos_msg_reply(mi->token, &rep, sizeof(rep), -1);
}

static void handle(const struct crtos_msginfo *mi, const void *buf, int n)
{
    const struct gfx_hdr *h = (const struct gfx_hdr *)buf;
    switch (n >= (int)sizeof(*h) ? h->type : 0) {
    case GFX_HELLO: /* these answer and take the handle themselves */
        do_hello(mi);
        return;
    case GFX_WIN_CREATE:
        if (n >= (int)sizeof(struct gfx_win_create)) {
            do_create(mi, (const struct gfx_win_create *)buf);
            return;
        }
        break;
    case GFX_STATS:
        s_st.h.type = GFX_STATS;
        s_st.h.size = sizeof(s_st);
        crtos_msg_reply(mi->token, &s_st, sizeof(s_st), -1);
        return;
    case GFX_WM_REGISTER:
        do_wm_register(mi);
        return;
    case GFX_WIN_RESIZE:
        if (n >= (int)sizeof(struct gfx_win_resize)) {
            do_resize(mi, (const struct gfx_win_resize *)buf);
            return;
        }
        break;
    case GFX_WIN_DESTROY:
        if (n >= (int)sizeof(struct gfx_win_ref)) {
            struct window *w = window_of(((const struct gfx_win_ref *)buf)->id, mi->pid);
            if (w)
                destroy_window(w);
        }
        break;
    case GFX_WIN_DAMAGE:
        if (n >= (int)sizeof(struct gfx_win_damage))
            do_damage(mi, (const struct gfx_win_damage *)buf);
        break;
    case GFX_WIN_MOVE:
        if (n >= (int)sizeof(struct gfx_win_move))
            do_move(mi, (const struct gfx_win_move *)buf);
        break;
    case GFX_WIN_RAISE:
        if (n >= (int)sizeof(struct gfx_win_ref)) {
            struct window *w = window_of(((const struct gfx_win_ref *)buf)->id, mi->pid);
            if (w)
                raise_window(w);
        }
        break;
    case GFX_WIN_SHOW:
        if (n >= (int)sizeof(struct gfx_win_show))
            do_show(mi, (const struct gfx_win_show *)buf);
        break;
    case GFX_WIN_TITLE:
        if (n >= (int)sizeof(struct gfx_win_title))
            do_title(mi, (const struct gfx_win_title *)buf);
        break;
    case GFX_WM_ATTACH:
        if (n >= (int)sizeof(struct gfx_wm_attach))
            do_wm_attach(mi, (const struct gfx_wm_attach *)buf);
        break;
    case GFX_WM_CONFIGURE:
        if (n >= (int)sizeof(struct gfx_wm_configure))
            do_wm_configure(mi, (const struct gfx_wm_configure *)buf);
        break;
    case GFX_WM_CLOSE:
    case GFX_WM_FOCUS:
        if (n >= (int)sizeof(struct gfx_win_ref))
            do_wm_ref(mi, h->type, (const struct gfx_win_ref *)buf);
        break;
    case GFX_INPUT:
        if (n >= (int)sizeof(struct gfx_input))
            do_input((const struct gfx_input *)buf);
        break;
    case GFX_WM_KEYS:
        if (n >= (int)sizeof(struct gfx_wm_keys) && s_wm && s_wm->pid == mi->pid)
            s_wm_keys = ((const struct gfx_wm_keys *)buf)->on != 0;
        break;
    case GFX_CLIP_PUT:
        if (n >= (int)offsetof(struct gfx_clip, data) && mi->token &&
            n >= (int)offsetof(struct gfx_clip, data) + ((const struct gfx_clip *)buf)->len) {
            do_clip_put(mi, (const struct gfx_clip *)buf);
            return;
        }
        break;
    case GFX_CLIP_GET:
        if (n >= (int)offsetof(struct gfx_clip, data) && mi->token) {
            do_clip_get(mi, (const struct gfx_clip *)buf);
            return;
        }
        break;
    case GFX_CLIP_WATCH: {
        struct client *c = client_of(mi->pid);
        if (c)
            c->clip_watch = true;
        break;
    }
    case GFX_SCREEN_WATCH:
        if (n >= (int)sizeof(struct gfx_screen_watch) && mi->token) {
            do_screen_watch(mi, (const struct gfx_screen_watch *)buf);
            return;
        }
        break;
    case GFX_SCREEN_TAKE:
        if (mi->token) {
            do_screen_take(mi);
            return;
        }
        break;
    case GFX_SETTINGS:
        settings_changed();
        break;
    default:
        break;
    }
    if (mi->token) /* a call we do not answer otherwise */
        crtos_msg_reply(mi->token, NULL, 0, -1);
    if (mi->handle >= 0) /* nobody wanted it */
        close(mi->handle);
}

/* The appearance settings changed: the wallpaper anew, and every client is told */
static void settings_changed(void)
{
    static int32_t serial;
    draw_desktop();
    struct rect all = { 0, 0, s_info.width, s_info.height };
    damage(&all);
    serial++;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        struct client *c = &s_client[i];
        if (!c->used || c->evport < 0)
            continue;
        struct gfx_event ev = { { GFX_EV_SETTINGS, GFX_EVENT_SHORT }, 0, 0, 0, 0, 0, serial,
                                (uint32_t)(crtos_time_us() / 1000u), 0, 0, 0, 0, { 0 } };
        crtos_msg_send(c->evport, &ev, GFX_EVENT_SHORT, -1, 0);
    }
}

/* Windows the manager did not frame in time are shown as they are; returns the time until
 * the next such deadline (ms, -1: none) */
static int manage_timeouts(void)
{
    if (!s_wm)
        return -1;
    uint64_t now = crtos_time_us(), wait = GFX_WM_WAIT_MS * 1000ull;
    int next = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        struct window *w = &s_win[i];
        if (!w->used || w->managed || !w->mapped || !wants_manager(w))
            continue;
        if (now - w->born_us >= wait) {
            w->managed = true;
            update_visible(w);
            continue;
        }
        int ms = (int)((w->born_us + wait - now + 999u) / 1000u);
        if (next < 0 || ms < next)
            next = ms;
    }
    return next;
}

/* ---- setup ----------------------------------------------------------------------------------------- */

/* The wallpaper of the settings into the desktop's memory (made the first time) */
static void draw_desktop(void)
{
    int w = s_info.width, h = s_info.height;
    if (s_desk < 0)
        s_desk = crtos_shm_create((size_t)w * (size_t)h * 2u, 0);
    uint16_t *pix = s_desk >= 0 ? (uint16_t *)crtos_shm_map(s_desk) : NULL;
    if (!pix) {
        if (s_desk >= 0)
            close(s_desk);
        s_desk = -1;
        return;
    }
    struct gfx_surface s = { pix, w, h, w * 2, GPU2D_FMT_RGB565 };
    struct ui_settings set;
    ui_settings_load(&set);
    uint64_t t0 = crtos_time_us();
    if (gfx_wallpaper_draw(&s, set.wallpaper, set.fit))
        printf("gfxd: wallpaper %s: %s\n", set.wallpaper, strerror(errno));
    printf("gfxd: wallpaper %s in %lu ms\n", set.wallpaper, (unsigned long)((crtos_time_us() - t0) / 1000u));
    crtos_shm_unmap(pix);
    s_desk_surf.handle = s_desk;
    s_desk_surf.offset = 0;
    s_desk_surf.width = (uint16_t)w;
    s_desk_surf.height = (uint16_t)h;
    s_desk_surf.stride = (uint32_t)w * 2u;
    s_desk_surf.format = GPU2D_FMT_RGB565;
}

static int setup(void)
{
    s_fb = open("/dev/fb0", O_RDWR);
    s_gpu = open("/dev/gpu2d", O_RDWR);
    if (s_fb < 0 || s_gpu < 0) {
        printf("gfxd: no display (%s)\n", strerror(errno));
        return -1;
    }
    if (ioctl(s_fb, FB_IOC_GET_INFO, &s_info) || s_info.format != FB_FMT_RGB565 || s_info.nbuffers < 2) {
        printf("gfxd: unsupported display\n");
        return -1;
    }
    for (int i = 0; i < 2; i++) {
        struct fb_shm_req q = { (uint32_t)i, -1 };
        if (ioctl(s_fb, FB_IOC_GET_SHM, &q)) {
            printf("gfxd: frame buffer %d: %s\n", i, strerror(errno));
            return -1;
        }
        s_fbh[i] = q.handle;
    }
    uint32_t shown = 0;
    ioctl(s_fb, FB_IOC_GET_SHOWN, &shown);
    s_front = (int)shown;
    s_back = 1 - s_front;
    draw_desktop();
    s_port = crtos_port_create(GFX_PORT_NAME);
    if (s_port < 0) {
        printf("gfxd: port: %s\n", strerror(errno));
        return -1;
    }
    /* the first two frames draw everything into both buffers */
    struct rect all = { 0, 0, s_info.width, s_info.height };
    region_add(&s_damage, &all);
    region_add(&s_prev, &all);
    s_period_us = s_info.refresh_mhz ? (uint32_t)(1000000000ull / s_info.refresh_mhz) : 16667u;
    printf("gfxd: %ux%u, composing with /dev/gpu2d\n", s_info.width, s_info.height);
    return 0;
}

int main(void)
{
    if (setup())
        return 1;
    static char buf[MSG_MAX];
    struct pollfd pf[2 + MAX_CLIENTS];
    for (;;) {
        int n = 0;
        pf[n].fd = s_port;
        pf[n].events = POLLIN;
        n++;
        pf[n].fd = s_flip_pending ? s_fb : -1; /* frame starts matter only while flipping */
        pf[n].events = POLLIN;
        n++;
        int map[MAX_CLIENTS];
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!s_client[i].used)
                continue;
            pf[n].fd = s_client[i].evport;
            pf[n].events = 0; /* only hang-ups */
            map[n - 2] = i;
            n++;
        }
        int timeout = manage_timeouts();
        if (!s_flip_pending && s_damage.n) {
            uint64_t now = crtos_time_us(), at = compose_time(now);
            if (at <= now) {
                compose();
                continue;
            }
            int ms = (int)((at - now + 999u) / 1000u);
            if (timeout < 0 || ms < timeout)
                timeout = ms;
        }
        if (poll(pf, (nfds_t)n, timeout) < 0)
            continue;

        if (pf[1].revents & POLLIN) { /* a frame started */
            uint32_t frame = 0, shown = 0;
            ioctl(s_fb, FB_IOC_GET_FRAME, &frame);
            ioctl(s_fb, FB_IOC_GET_SHOWN, &shown);
            s_st.frames = frame;
            s_vsync_us = crtos_time_us();
            if (s_flip_pending && (int)shown == s_back) {
                s_flip_pending = false;
                s_st.flips++;
                if (frame - s_show_frame >= 2u)
                    s_st.missed++;
                s_front = s_back;
                s_back = 1 - s_front;
                for (int i = 0; i < MAX_WINDOWS; i++)
                    if (s_win[i].used && s_win[i].in_flip) {
                        s_win[i].in_flip = false;
                        send_event(&s_win[i], GFX_EV_FRAME, 0, 0, 0, 0, (int32_t)frame);
                    }
            }
        }
        for (int k = 2; k < n; k++)
            if (pf[k].revents & (POLLHUP | POLLNVAL))
                drop_client(&s_client[map[k - 2]]);
        for (;;) {
            struct crtos_msginfo mi;
            int len = crtos_msg_recv(s_port, buf, sizeof(buf), &mi, 0);
            if (len < 0)
                break;
            handle(&mi, buf, len);
        }
    }
}
