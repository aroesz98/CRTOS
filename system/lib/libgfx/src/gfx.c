/*
 * gfx.c - client side of the graphics server protocol (gfx_proto.h).
 *
 * A window's pixels are shared memory. A process can map only a few shared memory objects
 * at a time (they are MPU regions), so a program with many windows reserves a pool first
 * (gfx_pool_reserve): the windows that fit are carved out of it and passed to gfxd as
 * offsets in the same object.
 *
 * The appearance settings (gfx_ui.h) are read when the program opens gfxd, and again when
 * gfxd says they changed (GFX_EV_SETTINGS), before the program gets that event.
 */
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include "gfx.h"
#include "gfx_internal.h"

/* ---- losing the server ------------------------------------------------------------------------ */

/* When gfxd ends, init starts it again - without our windows, and our connection is dead.
 * A client cannot go on: it ends (after its own handler, if it set one), the way X clients
 * end with their server. Services such as the window manager are then restarted by init. */
static void (*s_lost_handler)(void);

void gfx_on_server_lost(void (*fn)(void))
{
    s_lost_handler = fn;
}

static void server_lost(void)
{
    if (s_lost_handler)
        s_lost_handler();
    fprintf(stderr, "gfx: the graphics server is gone\n");
    exit(1);
}

static int msg_send(int port, const void *data, size_t len, int handle, uint32_t timeout)
{
    int r = crtos_msg_send(port, data, len, handle, timeout);
    if (r < 0 && errno == EPIPE)
        server_lost();
    return r;
}

static int msg_call(int port, const void *req, size_t req_len, void *rep, size_t rep_max, uint32_t timeout)
{
    int r = crtos_msg_call(port, req, req_len, rep, rep_max, timeout);
    if (r < 0 && errno == EPIPE)
        server_lost();
    return r;
}

static int msg_call2(int port, struct crtos_call *c, uint32_t timeout)
{
    int r = crtos_msg_call2(port, c, timeout);
    if (r < 0 && errno == EPIPE)
        server_lost();
    return r;
}

#define QMAX    32
#define WMAX    32
#define PMAX    WMAX
#define PALIGN  64u

struct pool {
    int shm;
    uint8_t *base;
    uint32_t size;
    int n;
    struct {
        uint32_t off, size;
    } blk[PMAX];                /* in use, by offset */
};

struct gfx {
    int port;                   /* gfxd */
    int evport;                 /* ours */
    int width, height;
    uint32_t format;
    struct gfx_event q[QMAX];   /* events set aside while waiting for a frame */
    int qhead, qtail;
    struct gfx_win *wins[WMAX];
    struct pool *pool;
};

struct gfx *gfx_open(void)
{
    struct gfx *g = (struct gfx *)calloc(1, sizeof(*g));
    if (!g)
        return NULL;
    g->port = crtos_port_connect(GFX_PORT_NAME, 5000);
    g->evport = g->port >= 0 ? crtos_port_create(NULL) : -1;
    if (g->evport < 0) {
        if (g->port >= 0)
            close(g->port);
        free(g);
        return NULL;
    }
    struct gfx_hdr req = { GFX_HELLO, sizeof(req) };
    struct gfx_hello_rep rep;
    struct crtos_call c = { &req, sizeof(req), g->evport, &rep, sizeof(rep), -1 };
    if (crtos_msg_call2(g->port, &c, 3000) != (int)sizeof(rep)) {
        gfx_close(g);
        errno = EPROTO;
        return NULL;
    }
    g->width = rep.width;
    g->height = rep.height;
    g->format = rep.format;
    ui_settings_init();
    return g;
}

void gfx_close(struct gfx *g)
{
    for (int i = 0; i < WMAX; i++)
        if (g->wins[i])
            gfx_win_destroy(g->wins[i]);
    if (g->pool) {
        crtos_shm_unmap(g->pool->base);
        close(g->pool->shm);
        free(g->pool);
    }
    close(g->evport);
    close(g->port);
    free(g);
}

int gfx_screen_width(const struct gfx *g)
{
    return g->width;
}

int gfx_screen_height(const struct gfx *g)
{
    return g->height;
}

int gfx_event_handle(const struct gfx *g)
{
    return g->evport;
}

/* ---- pool --------------------------------------------------------------------------------------- */

int gfx_pool_reserve(struct gfx *g, size_t bytes)
{
    if (g->pool) {
        errno = EEXIST;
        return -1;
    }
    struct pool *p = (struct pool *)calloc(1, sizeof(*p));
    if (!p)
        return -1;
    p->size = (uint32_t)((bytes + PALIGN - 1) & ~(size_t)(PALIGN - 1));
    p->shm = crtos_shm_create(p->size, 0);
    p->base = p->shm >= 0 ? (uint8_t *)crtos_shm_map(p->shm) : NULL;
    if (!p->base) {
        if (p->shm >= 0)
            close(p->shm);
        free(p);
        return -1;
    }
    g->pool = p;
    return 0;
}

/* First fit; returns the offset or -1 */
static int64_t pool_alloc(struct pool *p, uint32_t size)
{
    size = (size + PALIGN - 1) & ~(PALIGN - 1);
    if (p->n == PMAX)
        return -1;
    uint32_t start = 0;
    int at = p->n;
    for (int i = 0; i < p->n; i++) {
        if (p->blk[i].off - start >= size) {
            at = i;
            break;
        }
        start = p->blk[i].off + p->blk[i].size;
    }
    if (at == p->n && p->size - start < size)
        return -1;
    memmove(&p->blk[at + 1], &p->blk[at], (size_t)(p->n - at) * sizeof(p->blk[0]));
    p->blk[at].off = start;
    p->blk[at].size = size;
    p->n++;
    return start;
}

static void pool_free(struct pool *p, uint32_t off)
{
    for (int i = 0; i < p->n; i++)
        if (p->blk[i].off == off) {
            memmove(&p->blk[i], &p->blk[i + 1], (size_t)(p->n - i - 1) * sizeof(p->blk[0]));
            p->n--;
            return;
        }
}

/* ---- windows ------------------------------------------------------------------------------------ */

struct gfx_win *gfx_win_create(struct gfx *g, int x, int y, int w, int h, uint32_t flags, const char *title)
{
    int slot = -1;
    for (int i = 0; i < WMAX && slot < 0; i++)
        if (!g->wins[i])
            slot = i;
    if (slot < 0 || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        errno = EINVAL;
        return NULL;
    }
    struct gfx_win *win = (struct gfx_win *)calloc(1, sizeof(*win));
    if (!win)
        return NULL;
    bool alpha = flags & GFX_WIN_ALPHA, xrgb = !alpha && (flags & GFX_WIN_XRGB);
    int stride = ((w * (alpha || xrgb ? 4 : 2)) + 3) & ~3;
    uint32_t bytes = (uint32_t)stride * (uint32_t)h;
    win->g = g;
    win->flags = flags;
    win->s.w = w;
    win->s.h = h;
    win->s.stride = stride;
    win->s.format = alpha ? GPU2D_FMT_ARGB8888 : xrgb ? GPU2D_FMT_XRGB8888 : GPU2D_FMT_RGB565;
    int64_t off = g->pool ? pool_alloc(g->pool, bytes) : -1;
    if (off >= 0) {
        win->pooled = true;
        win->offset = (uint32_t)off;
        win->shm = g->pool->shm;
        win->s.pix = g->pool->base + off;
    } else {
        win->shm = crtos_shm_create(bytes, 0);
        win->s.pix = win->shm >= 0 ? crtos_shm_map(win->shm) : NULL;
        if (!win->s.pix) {
            if (win->shm >= 0)
                close(win->shm);
            free(win);
            return NULL;
        }
    }
    struct gfx_win_create req;
    memset(&req, 0, sizeof(req));
    req.h.type = GFX_WIN_CREATE;
    req.h.size = sizeof(req);
    req.x = (int16_t)x;
    req.y = (int16_t)y;
    req.width = (uint16_t)w;
    req.height = (uint16_t)h;
    req.stride = (uint32_t)stride;
    req.format = win->s.format;
    req.offset = win->offset;
    req.flags = flags;
    if (title)
        strncpy(req.title, title, sizeof(req.title) - 1);
    struct gfx_win_rep rep;
    struct crtos_call c = { &req, sizeof(req), win->shm, &rep, sizeof(rep), -1 };
    if (msg_call2(g->port, &c, 3000) != (int)sizeof(rep) || rep.id < 0) {
        if (win->pooled) {
            pool_free(g->pool, win->offset);
        } else {
            crtos_shm_unmap(win->s.pix);
            close(win->shm);
        }
        free(win);
        errno = EPROTO;
        return NULL;
    }
    win->id = rep.id;
    win->x = rep.x;
    win->y = rep.y;
    g->wins[slot] = win;
    return win;
}

static void free_pixels(struct gfx *g, bool pooled, int shm, uint32_t offset, void *pix)
{
    if (pooled) {
        pool_free(g->pool, offset);
    } else {
        crtos_shm_unmap(pix);
        close(shm);
    }
}

void gfx_win_destroy(struct gfx_win *w)
{
    struct gfx *g = w->g;
    struct gfx_win_ref m = { { GFX_WIN_DESTROY, sizeof(m) }, w->id };
    msg_send(g->port, &m, sizeof(m), -1, 1000);
    free_pixels(g, w->pooled, w->shm, w->offset, w->s.pix);
    if (w->resize_pending)
        free_pixels(g, w->old_pooled, w->old_shm, w->old_offset, w->old_pix);
    for (int i = 0; i < WMAX; i++)
        if (g->wins[i] == w)
            g->wins[i] = NULL;
    free(w);
}

int gfx_win_resize(struct gfx_win *w, int width, int height)
{
    struct gfx *g = w->g;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        errno = EINVAL;
        return -1;
    }
    if (width == w->s.w && height == w->s.h)
        return 1; /* that size already: nothing new to draw */
    int bpp = w->s.format == GPU2D_FMT_RGB565 ? 2 : 4;
    int stride = (width * bpp + 3) & ~3;
    uint32_t bytes = (uint32_t)stride * (uint32_t)height;
    int64_t off = w->pooled && g->pool ? pool_alloc(g->pool, bytes) : -1;
    int shm;
    void *pix;
    if (off >= 0) {
        shm = g->pool->shm;
        pix = g->pool->base + off;
    } else {
        shm = crtos_shm_create(bytes, 0);
        pix = shm >= 0 ? crtos_shm_map(shm) : NULL;
        if (!pix) {
            if (shm >= 0)
                close(shm);
            return -1;
        }
    }
    if (w->resize_pending) { /* resized again before it was shown: that buffer goes */
        free_pixels(g, w->pooled, w->shm, w->offset, w->s.pix);
    } else {
        w->old_pooled = w->pooled;
        w->old_shm = w->shm;
        w->old_offset = w->offset;
        w->old_pix = w->s.pix;
        w->old_w = w->s.w;
        w->old_h = w->s.h;
        w->old_stride = w->s.stride;
        w->resize_pending = true;
    }
    w->pooled = off >= 0;
    w->shm = shm;
    w->offset = off >= 0 ? (uint32_t)off : 0u;
    w->s.pix = pix;
    w->s.w = width;
    w->s.h = height;
    w->s.stride = stride;
    return 0;
}

/* Hand the new pixels of a resized window to gfxd */
static void commit_resize(struct gfx_win *w)
{
    struct gfx *g = w->g;
    struct gfx_win_resize req = { { GFX_WIN_RESIZE, sizeof(req) }, w->id, (uint16_t)w->s.w, (uint16_t)w->s.h,
                                  (uint32_t)w->s.stride, w->s.format, w->offset };
    struct gfx_status rep;
    struct crtos_call c = { &req, sizeof(req), w->shm, &rep, sizeof(rep), -1 };
    if (msg_call2(g->port, &c, 3000) == (int)sizeof(rep) && rep.status == 0) {
        free_pixels(g, w->old_pooled, w->old_shm, w->old_offset, w->old_pix);
        w->resize_pending = false;
        return;
    }
    /* refused: back to the old pixels */
    free_pixels(g, w->pooled, w->shm, w->offset, w->s.pix);
    w->pooled = w->old_pooled;
    w->shm = w->old_shm;
    w->offset = w->old_offset;
    w->s.pix = w->old_pix;
    w->s.w = w->old_w;
    w->s.h = w->old_h;
    w->s.stride = w->old_stride;
    w->resize_pending = false;
}

void gfx_damage(struct gfx_win *w, int x, int y, int width, int height)
{
    if (w->resize_pending)
        commit_resize(w);
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x + width > w->s.w)
        width = w->s.w - x;
    if (y + height > w->s.h)
        height = w->s.h - y;
    if (width <= 0 || height <= 0)
        return;
    struct gfx_win_damage m = { { GFX_WIN_DAMAGE, sizeof(m) }, w->id, (int16_t)x, (int16_t)y, (uint16_t)width,
                                (uint16_t)height };
    if (msg_send(w->g->port, &m, sizeof(m), -1, 1000) == 0)
        w->frame_pending = true;
}

void gfx_present(struct gfx_win *w)
{
    gfx_damage(w, 0, 0, w->s.w, w->s.h);
}

void gfx_win_move(struct gfx_win *w, int x, int y)
{
    struct gfx_win_move m = { { GFX_WIN_MOVE, sizeof(m) }, w->id, (int16_t)x, (int16_t)y };
    if (msg_send(w->g->port, &m, sizeof(m), -1, 1000) == 0) {
        w->x = x;
        w->y = y;
    }
}

void gfx_win_raise(struct gfx_win *w)
{
    struct gfx_win_ref m = { { GFX_WIN_RAISE, sizeof(m) }, w->id };
    msg_send(w->g->port, &m, sizeof(m), -1, 1000);
}

void gfx_win_show(struct gfx_win *w, bool visible)
{
    struct gfx_win_show m = { { GFX_WIN_SHOW, sizeof(m) }, w->id, visible ? 1 : 0 };
    msg_send(w->g->port, &m, sizeof(m), -1, 1000);
}

void gfx_win_set_title(struct gfx_win *w, const char *title)
{
    struct gfx_win_title m;
    memset(&m, 0, sizeof(m));
    m.h.type = GFX_WIN_TITLE;
    m.h.size = sizeof(m);
    m.id = w->id;
    strncpy(m.title, title ? title : "", sizeof(m.title) - 1);
    msg_send(w->g->port, &m, sizeof(m), -1, 1000);
}

/* ---- window manager ------------------------------------------------------------------------------ */

int gfx_wm_register(struct gfx *g)
{
    struct gfx_hdr req = { GFX_WM_REGISTER, sizeof(req) };
    struct gfx_wm_rep rep;
    if (msg_call(g->port, &req, sizeof(req), &rep, sizeof(rep), 3000) != (int)sizeof(rep)) {
        errno = EPROTO;
        return -1;
    }
    if (rep.status < 0) {
        errno = -rep.status;
        return -1;
    }
    return 0;
}

void gfx_wm_attach(struct gfx *g, int id, const struct gfx_win *frame, int dx, int dy)
{
    struct gfx_wm_attach m = { { GFX_WM_ATTACH, sizeof(m) }, id, frame ? frame->id : 0, (int16_t)dx, (int16_t)dy };
    msg_send(g->port, &m, sizeof(m), -1, 1000);
}

static void wm_ref(struct gfx *g, uint16_t type, int id)
{
    struct gfx_win_ref m = { { type, sizeof(m) }, id };
    msg_send(g->port, &m, sizeof(m), -1, 1000);
}

void gfx_wm_close(struct gfx *g, int id)
{
    wm_ref(g, GFX_WM_CLOSE, id);
}

void gfx_wm_focus(struct gfx *g, int id)
{
    wm_ref(g, GFX_WM_FOCUS, id);
}

void gfx_wm_configure(struct gfx *g, int id, int width, int height)
{
    struct gfx_wm_configure m = { { GFX_WM_CONFIGURE, sizeof(m) }, id, (uint16_t)width, (uint16_t)height };
    msg_send(g->port, &m, sizeof(m), -1, 1000);
}

void gfx_wm_keys(struct gfx *g, bool all)
{
    struct gfx_wm_keys m = { { GFX_WM_KEYS, sizeof(m) }, all ? 1 : 0 };
    msg_send(g->port, &m, sizeof(m), -1, 1000);
}

void gfx_settings_changed(struct gfx *g)
{
    struct gfx_hdr m = { GFX_SETTINGS, sizeof(m) };
    msg_send(g->port, &m, sizeof(m), -1, 1000);
}

/* ---- clipboard --------------------------------------------------------------------------------- */

/* A message holds at most 512 bytes: the text goes in pieces, each answered */
int gfx_clip_set(struct gfx *g, const char *text, size_t len)
{
    if (len > GFX_CLIP_MAX) {
        errno = EFBIG;
        return -1;
    }
    static struct gfx_clip m;
    uint32_t off = 0;
    do {
        uint32_t n = len - off < GFX_CLIP_PIECE ? (uint32_t)len - off : GFX_CLIP_PIECE;
        m.h.type = GFX_CLIP_PUT;
        m.h.size = (uint16_t)(offsetof(struct gfx_clip, data) + n);
        m.total = (uint32_t)len;
        m.offset = off;
        m.len = (uint16_t)n;
        memcpy(m.data, text + off, n);
        struct gfx_status rep;
        if (msg_call(g->port, &m, m.h.size, &rep, sizeof(rep), 3000) != (int)sizeof(rep)) {
            errno = EPROTO;
            return -1;
        }
        if (rep.status < 0) {
            errno = -rep.status;
            return -1;
        }
        off += n;
    } while (off < len);
    return 0;
}

int gfx_clip_get(struct gfx *g, char *buf, size_t size)
{
    static struct gfx_clip rep;
    if (!size) {
        errno = EINVAL;
        return -1;
    }
    for (int attempt = 0; attempt < 4; attempt++) { /* a new text while reading: again */
        uint32_t off = 0, total = 0, serial = 0;
        bool changed = false;
        do {
            struct gfx_clip q;
            memset(&q, 0, offsetof(struct gfx_clip, data));
            q.h.type = GFX_CLIP_GET;
            q.h.size = (uint16_t)offsetof(struct gfx_clip, data);
            q.offset = off;
            int n = msg_call(g->port, &q, q.h.size, &rep, sizeof(rep), 3000);
            if (n < (int)offsetof(struct gfx_clip, data) || n < (int)offsetof(struct gfx_clip, data) + rep.len) {
                errno = EPROTO;
                return -1;
            }
            if (off && (rep.serial != serial || rep.total != total)) {
                changed = true;
                break;
            }
            serial = rep.serial;
            total = rep.total;
            if (off < size - 1u) {
                size_t k = size - 1u - off < rep.len ? size - 1u - off : rep.len;
                memcpy(buf + off, rep.data, k);
            }
            off += rep.len;
            if (!rep.len)
                break;
        } while (off < total && off < size - 1u);
        if (changed)
            continue;
        buf[off < size - 1u ? off : size - 1u] = 0;
        return (int)total;
    }
    errno = EAGAIN;
    return -1;
}

void gfx_clip_watch(struct gfx *g)
{
    struct gfx_hdr m = { GFX_CLIP_WATCH, sizeof(m) };
    msg_send(g->port, &m, sizeof(m), -1, 1000);
}

/* ---- remote control ---------------------------------------------------------------------------- */

int gfx_send_input(struct gfx *g, uint16_t kind, uint16_t code, int x, int y, int32_t value)
{
    struct gfx_input in = { { GFX_INPUT, sizeof(in) }, kind, code, (int16_t)x, (int16_t)y, value,
                            (uint32_t)(crtos_time_us() / 1000u) };
    return msg_send(g->port, &in, sizeof(in), -1, 1000) < 0 ? -1 : 0;
}

int gfx_screen_watch(struct gfx *g, int shm, uint32_t stride)
{
    struct gfx_screen_watch q = { { GFX_SCREEN_WATCH, sizeof(q) }, stride };
    struct gfx_status rep;
    struct crtos_call c = { &q, sizeof(q), shm, &rep, sizeof(rep), -1 };
    if (msg_call2(g->port, &c, 3000) != (int)sizeof(rep)) {
        errno = EPROTO;
        return -1;
    }
    if (rep.status < 0) {
        errno = -rep.status;
        return -1;
    }
    return 0;
}

int gfx_screen_take(struct gfx *g, struct gfx_screen_rect *r)
{
    struct gfx_hdr q = { GFX_SCREEN_TAKE, sizeof(q) };
    struct gfx_screen_take rep;
    if (msg_call(g->port, &q, sizeof(q), &rep, sizeof(rep), 3000) != (int)sizeof(rep))
        return -1;
    int n = rep.n < GFX_SCREEN_RECTS ? rep.n : GFX_SCREEN_RECTS;
    memcpy(r, rep.r, (size_t)n * sizeof(r[0]));
    return n;
}

/* ---- events ------------------------------------------------------------------------------------ */

/* Receive one event from gfxd; FRAME events update the window state. A long wait looks at
 * the connection to gfxd once a second: its port reports a hang-up when gfxd has ended (a
 * client that sends notices at once, from the send). */
static int receive(struct gfx *g, struct gfx_event *ev, uint32_t timeout)
{
    uint64_t end = timeout == CRTOS_FOREVER ? 0 : crtos_time_us() + (uint64_t)timeout * 1000u;
    for (;;) {
        uint32_t slice = 1000;
        if (end) {
            uint64_t now = crtos_time_us();
            uint64_t left = now < end ? (end - now + 999u) / 1000u : 0;
            slice = left < slice ? (uint32_t)left : slice;
        }
        int n = crtos_msg_recv(g->evport, ev, sizeof(*ev), NULL, slice);
        if (n < 0) {
            if (errno != ETIMEDOUT)
                return -1;
            struct pollfd p = { g->port, 0, 0 };
            if (slice && poll(&p, 1, 0) > 0 && (p.revents & (POLLHUP | POLLERR))) /* (0: a quick look) */
                server_lost();
            if (end && crtos_time_us() >= end)
                return -1;
            continue;
        }
        if (n < GFX_EVENT_SHORT)
            continue;
        if (n < (int)sizeof(*ev)) /* the short kind: no window manager fields */
            memset((char *)ev + n, 0, sizeof(*ev) - (size_t)n);
        if (ev->h.type == GFX_EV_FRAME)
            for (int i = 0; i < WMAX; i++)
                if (g->wins[i] && g->wins[i]->id == ev->win)
                    g->wins[i]->frame_pending = false;
        if (ev->h.type == GFX_EV_SETTINGS)
            ui_settings_reload(); /* the theme first: the program lays out with it */
        return 0;
    }
}

int gfx_next_event(struct gfx *g, struct gfx_event *ev, uint32_t timeout)
{
    if (g->qhead != g->qtail) {
        *ev = g->q[g->qtail++ % QMAX];
        return 0;
    }
    return receive(g, ev, timeout);
}

int gfx_wait_frame(struct gfx_win *w, uint32_t timeout)
{
    struct gfx *g = w->g;
    uint64_t end = crtos_time_us() + (uint64_t)timeout * 1000u;
    while (w->frame_pending) {
        uint32_t left = CRTOS_FOREVER;
        if (timeout != CRTOS_FOREVER) {
            uint64_t now = crtos_time_us();
            if (now >= end)
                return -1;
            left = (uint32_t)((end - now) / 1000u) + 1u;
        }
        struct gfx_event ev;
        if (receive(g, &ev, left))
            return -1;
        if (ev.h.type != GFX_EV_FRAME && g->qhead - g->qtail < QMAX)
            g->q[g->qhead++ % QMAX] = ev;
    }
    return 0;
}

int gfx_stats(struct gfx *g, struct gfx_stats *st)
{
    struct gfx_hdr req = { GFX_STATS, sizeof(req) };
    return msg_call(g->port, &req, sizeof(req), st, sizeof(*st), 1000) == (int)sizeof(*st) ? 0 : -1;
}

struct gfx_win *gfx_win_by_id(struct gfx *g, int id)
{
    for (int i = 0; i < WMAX; i++)
        if (g->wins[i] && g->wins[i]->id == id)
            return g->wins[i];
    return NULL;
}
