/*
 * vncd - remote desktop (layer 2): the board's screen, pointer, keyboard and clipboard over the
 * network for a VNC viewer - "crtos desktop" on the computer, or any other (TightVNC, TigerVNC,
 * RealVNC...).
 *
 * TCP port 5900, RFB protocol 3.3, 3.7 and 3.8 (RFC 6143) with VNC authentication: the
 * password is the first line of /sd/crtos/etc/vnc.passwd (as in every VNC viewer only its first
 * 8 characters count), read at each connection; without that file every viewer is turned away
 * ("crtos desktop" installs one). A wrong password costs a second. One viewer at a time: one
 * that logs in while another is connected takes over.
 *
 * The picture: gfxd copies what changes on the screen into shared memory of ours with the 2D
 * accelerator (GFX_SCREEN_WATCH, needs CAP_SYS) and tells where (GFX_SCREEN_TAKE). We keep our
 * own copy - what the viewer has or is about to get - compare the changed areas with it in
 * 16 x 16 tiles and send only the tiles that really changed, when the viewer asks
 * (FramebufferUpdateRequest), in the pixel format it wants and the first encoding of its list
 * we have: Zlib (one deflate stream, deflate.c: a desktop shrinks to 1-2 %, a game picture to
 * about 10 %; rectangles of up to ZMAX bytes), Hextile or Raw. The viewer that takes our own
 * format (RGB565, little endian: "crtos desktop") gets the pixels as they are, row by row.
 *
 * Input: the left button is a finger (GFX_PTR_*, marked GFX_PTR_MOUSE), a move without it is
 * a hover (GFX_PTR_HOVER: tooltips), buttons 4-7 are the wheels (GFX_WHEEL); keys (X11 keysyms) become key codes of a US keyboard (GFX_KEY), with
 * Shift pressed or let go around a character that needs it. The clipboard goes both ways
 * (ClientCutText / ServerCutText and gfxd's clipboard).
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <crtos.h>
#include <crtos/keys.h>
#include "deflate.h"
#include "des.h"
#include "gfx.h"
#include "gfx_ui.h"

#define PORT        5900
#define PASSWD_FILE "/sd/crtos/etc/vnc.passwd"
#define NAME        "CRTOS"
#define TILE        16
#define OUT_SIZE    32768       /* what goes to the viewer is gathered in pieces of this size */
#define ZMAX        65536       /* bytes of pixels in one Zlib rectangle at most (taller ones are cut) */
#define SERVE_PRIO  11          /* above the programs (10) */
#define SERVE_STACK 16384

/* RFB encodings */
#define ENC_RAW     0
#define ENC_HEXTILE 5
#define ENC_ZLIB    6
#define IN_SIZE     2048        /* its messages (a SetEncodings of 500 encodings fits) */
#define KEYS_DOWN   16

/* hextile subencodings */
#define HX_RAW          1u
#define HX_BG           2u
#define HX_FG           4u
#define HX_ANY          8u
#define HX_COLOURED     16u

struct pixfmt {
    uint8_t bpp, depth, big, truecolour;
    uint16_t rmax, gmax, bmax;
    uint8_t rshift, gshift, bshift;
};

/* the viewer */
static struct {
    int fd;
    int bytes;                      /* per pixel in its format */
    bool big;
    bool native;                    /* its format is ours: pixels go as they are */
    uint32_t lut_r[32], lut_g[64], lut_b[32];   /* RGB565 components -> its pixel */
    int enc;                        /* ENC_* */
    bool want;                      /* an update request waits */
    int rx, ry, rw, rh;             /* its area */
    uint8_t in[IN_SIZE];
    int have;
    uint32_t cut_left;              /* bytes of a ClientCutText still coming */
    char *cut;
    uint32_t cut_len;
    uint8_t out[OUT_SIZE];
    int olen;
    bool out_err;
    bool shift_l, shift_r;          /* the Shift keys it holds */
    struct {
        uint32_t sym;
        uint16_t code;
    } down[KEYS_DOWN];              /* keys held: their keysyms and the codes pressed for them */
    int ndown;
    uint8_t buttons;
    int px, py;
} V;

/* the screen */
static struct gfx *s_g;
static int W, H, TX, TY;
static int s_shm = -1;
static uint16_t *s_shadow;          /* gfxd's copy */
static uint16_t *s_fb;              /* ours */
static uint8_t *s_dirty;            /* per tile: the viewer does not have it yet */
static bool s_have_screen;          /* the copy was filled once */
static int s_clip_ignore;           /* GFX_EV_CLIP of texts the viewer put itself */
static uint8_t *s_row;              /* one row in the viewer's format */
static struct zdef s_z;             /* the viewer's zlib stream */
static uint8_t *s_work;             /* a Zlib rectangle's pixels (ZMAX) */
static uint8_t *s_zout;             /* ... compressed */

/* hextile state within one rectangle */
static uint32_t s_hbg, s_hfg;
static bool s_hbg_ok, s_hfg_ok;

/* ---- the connection ------------------------------------------------------------------------------ */

static int send_all(int fd, const void *data, size_t len)
{
    const char *p = (const char *)data;
    while (len) {
        int w = (int)send(fd, p, len, 0);
        if (w <= 0)
            return -1;
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

static int recv_all(int fd, void *data, size_t len)
{
    char *p = (char *)data;
    while (len) {
        int r = (int)recv(fd, p, len, 0);
        if (r <= 0)
            return -1;
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

static void flush_out(void)
{
    if (V.olen && !V.out_err && send_all(V.fd, V.out, (size_t)V.olen))
        V.out_err = true;
    V.olen = 0;
}

static void put(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len) {
        if (V.olen == OUT_SIZE)
            flush_out();
        size_t n = OUT_SIZE - (size_t)V.olen < len ? OUT_SIZE - (size_t)V.olen : len;
        memcpy(V.out + V.olen, p, n);
        V.olen += (int)n;
        p += n;
        len -= n;
    }
}

static void put8(uint8_t v)
{
    put(&v, 1);
}

static void put16(uint16_t v)
{
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    put(b, 2);
}

static void put32(uint32_t v)
{
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    put(b, 4);
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

/* ---- pixels in the viewer's format ------------------------------------------------------------------- */

static void set_format(const struct pixfmt *f)
{
    V.bytes = f->bpp / 8;
    V.big = f->big != 0;
    for (unsigned i = 0; i < 32; i++) {
        V.lut_r[i] = ((i * f->rmax + 15u) / 31u) << f->rshift;
        V.lut_b[i] = ((i * f->bmax + 15u) / 31u) << f->bshift;
    }
    for (unsigned i = 0; i < 64; i++)
        V.lut_g[i] = ((i * f->gmax + 31u) / 63u) << f->gshift;
    V.native = f->bpp == 16 && !f->big && f->truecolour && f->rmax == 31 && f->gmax == 63 && f->bmax == 31 &&
               f->rshift == 11 && f->gshift == 5 && f->bshift == 0;
}

/* @w pixels of our copy at (x, y) in the viewer's format into @dst */
static void row_pixels(int x, int y, int w, uint8_t *dst)
{
    const uint16_t *src = s_fb + (size_t)y * (size_t)W + (size_t)x;
    if (V.native) {
        memcpy(dst, src, (size_t)w * 2u);
        return;
    }
    for (int i = 0; i < w; i++) {
        uint32_t p = V.lut_r[src[i] >> 11] | V.lut_g[(src[i] >> 5) & 63u] | V.lut_b[src[i] & 31u];
        switch (V.bytes) {
        case 1:
            *dst++ = (uint8_t)p;
            break;
        case 2:
            dst[V.big ? 0 : 1] = (uint8_t)(p >> 8);
            dst[V.big ? 1 : 0] = (uint8_t)p;
            dst += 2;
            break;
        default:
            for (int b = 0; b < 4; b++)
                dst[V.big ? 3 - b : b] = (uint8_t)(p >> (8 * b));
            dst += 4;
            break;
        }
    }
}

static uint32_t pixel(uint16_t v)
{
    return V.lut_r[v >> 11] | V.lut_g[(v >> 5) & 63u] | V.lut_b[v & 31u];
}

static void put_pixel(uint32_t p)
{
    uint8_t b[4];
    switch (V.bytes) {
    case 1:
        b[0] = (uint8_t)p;
        break;
    case 2:
        b[V.big ? 0 : 1] = (uint8_t)(p >> 8);
        b[V.big ? 1 : 0] = (uint8_t)p;
        break;
    default:
        for (int i = 0; i < 4; i++)
            b[V.big ? 3 - i : i] = (uint8_t)(p >> (8 * i));
        break;
    }
    put(b, (size_t)V.bytes);
}

/* A viewer that wants a colour map gets a fixed one: 3 bits red, 3 green, 2 blue */
static void set_colour_map(void)
{
    struct pixfmt f = { 8, 8, 0, 1, 7, 7, 3, 0, 3, 6 };
    set_format(&f);
    put8(1);
    put8(0);
    put16(0);
    put16(256);
    for (unsigned i = 0; i < 256; i++) {
        put16((uint16_t)((i & 7u) * 65535u / 7u));
        put16((uint16_t)(((i >> 3) & 7u) * 65535u / 7u));
        put16((uint16_t)(((i >> 6) & 3u) * 65535u / 3u));
    }
    flush_out();
}

static void set_pixel_format(const uint8_t *p)
{
    struct pixfmt f = { p[0], p[1], p[2], p[3], be16(p + 4), be16(p + 6), be16(p + 8), p[10], p[11], p[12] };
    if (f.bpp != 8 && f.bpp != 16 && f.bpp != 32)
        f.bpp = 32;
    if (!f.truecolour) {
        set_colour_map();
        return;
    }
    set_format(&f);
}

/* ---- the screen ------------------------------------------------------------------------------------- */

/* A tile's row of 16 pixels (32 bytes) differs */
static inline bool row_differs(const uint16_t *a, const uint16_t *b)
{
    const uint32_t *p = (const uint32_t *)a, *q = (const uint32_t *)b;
    return ((p[0] ^ q[0]) | (p[1] ^ q[1]) | (p[2] ^ q[2]) | (p[3] ^ q[3]) | (p[4] ^ q[4]) | (p[5] ^ q[5]) |
            (p[6] ^ q[6]) | (p[7] ^ q[7])) != 0;
}

/* The areas gfxd says changed: into our copy, and the tiles that differ are due. Row by row
 * (both copies are read in order), with the lines a few tiles ahead asked for early: a tile's
 * row is one cache line of each copy, and waiting for each miss in turn (350 ns) was most of
 * the time (21 ms for a full window that changed nowhere). */
static void take_screen(void)
{
    struct gfx_screen_rect r[GFX_SCREEN_RECTS];
    int n = gfx_screen_take(s_g, r);
    for (int i = 0; i < n; i++) {
        int x0 = r[i].x < 0 ? 0 : r[i].x, y0 = r[i].y < 0 ? 0 : r[i].y;
        int x1 = r[i].x + r[i].w > W ? W : r[i].x + r[i].w, y1 = r[i].y + r[i].h > H ? H : r[i].y + r[i].h;
        if (x0 >= x1 || y0 >= y1)
            continue;
        int tx0 = x0 / TILE, tx1 = (x1 - 1) / TILE;
        for (int y = y0; y < y1; y++) {
            const uint16_t *a = s_shadow + (size_t)y * (size_t)W;
            uint16_t *b = s_fb + (size_t)y * (size_t)W;
            uint8_t *dirty = s_dirty + (size_t)(y / TILE) * (size_t)TX;
            for (int tx = tx0; tx <= tx1; tx++) {
                int ax = tx * TILE > x0 ? tx * TILE : x0, bx = (tx + 1) * TILE < x1 ? (tx + 1) * TILE : x1;
                __builtin_prefetch(a + ax + 4 * TILE);
                __builtin_prefetch(b + ax + 4 * TILE);
                bool differs = bx - ax == TILE && !((uintptr_t)(a + ax) & 3u)
                                   ? row_differs(a + ax, b + ax)
                                   : memcmp(b + ax, a + ax, (size_t)(bx - ax) * 2u) != 0;
                if (differs) {
                    memcpy(b + ax, a + ax, (size_t)(bx - ax) * 2u);
                    dirty[tx] = 1;
                }
            }
        }
    }
    s_have_screen = true;
}

static void mark_all(int x, int y, int w, int h)
{
    for (int ty = y / TILE; ty * TILE < y + h && ty < TY; ty++)
        for (int tx = x / TILE; tx * TILE < x + w && tx < TX; tx++)
            s_dirty[ty * TX + tx] = 1;
}

/* ---- encodings --------------------------------------------------------------------------------------- */

static void raw_rect(int x0, int y0, int w, int h)
{
    for (int y = y0; y < y0 + h; y++) {
        if (V.native) {
            put(s_fb + (size_t)y * (size_t)W + (size_t)x0, (size_t)w * 2u);
        } else {
            row_pixels(x0, y, w, s_row);
            put(s_row, (size_t)w * (size_t)V.bytes);
        }
    }
}

/* Zlib: the rectangle's pixels deflated as one piece of the viewer's stream, after its length */
static void zlib_rect(int x0, int y0, int w, int h)
{
    size_t row = (size_t)w * (size_t)V.bytes;
    const uint8_t *in = (const uint8_t *)(s_fb + (size_t)y0 * (size_t)W);
    if (!V.native || x0 || w != W) { /* (whole rows of ours are in one piece already) */
        for (int y = 0; y < h; y++)
            row_pixels(x0, y0 + y, w, s_work + (size_t)y * row);
        in = s_work;
    }
    size_t n = zdef_rect(&s_z, in, row * (size_t)h, (unsigned)V.bytes, row);
    put32((uint32_t)n);
    put(s_zout, n);
}

/* Rows of a Zlib rectangle at most (its pixels within ZMAX) */
static int zlib_rows(int w)
{
    int rows = ZMAX / (w * V.bytes);
    return rows < 1 ? 1 : rows;
}

static void raw_tile(const uint32_t *px, int n)
{
    put8(HX_RAW);
    for (int i = 0; i < n; i++)
        put_pixel(px[i]);
    s_hbg_ok = s_hfg_ok = false;
}

/* One tile of Hextile: one colour, two (a background and one-colour rectangles), or more
 * (coloured rectangles) - or raw pixels when those would be longer */
static void hextile_tile(int x0, int y0, int w, int h)
{
    static uint32_t px[TILE * TILE];
    static uint8_t data[TILE * TILE * 6], done[TILE * TILE];
    int n = w * h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = pixel(s_fb[(y0 + y) * W + x0 + x]);
    uint32_t col[16];
    int cnt[16], ncol = 0;
    for (int i = 0; i < n && ncol <= 16; i++) {
        int k = 0;
        while (k < ncol && col[k] != px[i])
            k++;
        if (k < ncol)
            cnt[k]++;
        else if (ncol < 16) {
            col[ncol] = px[i];
            cnt[ncol++] = 1;
        } else
            ncol = 17;
    }
    if (ncol > 16) { /* a picture: raw */
        raw_tile(px, n);
        return;
    }
    if (ncol == 1) {
        bool bg = !s_hbg_ok || s_hbg != col[0];
        put8(bg ? HX_BG : 0);
        if (bg)
            put_pixel(col[0]);
        s_hbg = col[0];
        s_hbg_ok = true;
        return;
    }
    int best = 0;
    for (int k = 1; k < ncol; k++)
        if (cnt[k] > cnt[best])
            best = k;
    uint32_t bg = col[best];
    bool mono = ncol == 2;
    uint32_t fg = mono ? col[1 - best] : 0;
    memset(done, 0, (size_t)n);
    int count = 0, size = 0, limit = n * V.bytes, add = mono ? 2 : 2 + V.bytes;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = y * w + x;
            if (done[i] || px[i] == bg)
                continue;
            uint32_t c = px[i];
            int rw = 1, rh = 1;
            while (x + rw < w && !done[i + rw] && px[i + rw] == c)
                rw++;
            for (bool ok = true; ok && y + rh < h;) {
                for (int k = 0; k < rw && ok; k++) {
                    int j = (y + rh) * w + x + k;
                    ok = !done[j] && px[j] == c;
                }
                if (ok)
                    rh++;
            }
            for (int yy = 0; yy < rh; yy++)
                memset(done + (y + yy) * w + x, 1, (size_t)rw);
            if (count == 255 || size + add > limit) {
                raw_tile(px, n);
                return;
            }
            if (!mono) {
                for (int b = 0; b < V.bytes; b++)
                    data[size + b] = 0;
                /* the colour in the viewer's byte order */
                uint8_t *q = data + size;
                for (int b = 0; b < V.bytes; b++)
                    q[V.big ? V.bytes - 1 - b : b] = (uint8_t)(c >> (8 * b));
                size += V.bytes;
            }
            data[size++] = (uint8_t)((x << 4) | y);
            data[size++] = (uint8_t)(((rw - 1) << 4) | (rh - 1));
            count++;
        }
    }
    uint8_t flags = HX_ANY | (mono ? HX_FG : HX_COLOURED);
    if (!s_hbg_ok || s_hbg != bg)
        flags |= HX_BG;
    if (mono && s_hfg_ok && s_hfg == fg)
        flags &= (uint8_t)~HX_FG;
    int total = 1 + ((flags & HX_BG) ? V.bytes : 0) + ((flags & HX_FG) ? V.bytes : 0) + 1 + size;
    if (total > 1 + limit) {
        raw_tile(px, n);
        return;
    }
    put8(flags);
    if (flags & HX_BG)
        put_pixel(bg);
    if (flags & HX_FG)
        put_pixel(fg);
    put8((uint8_t)count);
    put(data, (size_t)size);
    s_hbg = bg;
    s_hbg_ok = true;
    if (mono) {
        s_hfg = fg;
        s_hfg_ok = true;
    } else {
        s_hfg_ok = false;
    }
}

struct rect {
    int x, y, w, h;
};

/* The due tiles in the requested area as rectangles: runs of a tile row, joined with the run
 * of the same columns in the row below */
static int due_rects(struct rect *out, int max)
{
    int tx0 = V.rx / TILE, ty0 = V.ry / TILE;
    int tx1 = (V.rx + V.rw + TILE - 1) / TILE, ty1 = (V.ry + V.rh + TILE - 1) / TILE;
    tx1 = tx1 > TX ? TX : tx1;
    ty1 = ty1 > TY ? TY : ty1;
    int n = 0;
    for (int ty = ty0; ty < ty1; ty++) {
        for (int tx = tx0; tx < tx1;) {
            if (!s_dirty[ty * TX + tx]) {
                tx++;
                continue;
            }
            int run = 1;
            while (tx + run < tx1 && s_dirty[ty * TX + tx + run])
                run++;
            for (int k = 0; k < run; k++)
                s_dirty[ty * TX + tx + k] = 0;
            struct rect r = { tx * TILE, ty * TILE, run * TILE, TILE };
            if (r.x + r.w > W)
                r.w = W - r.x;
            if (r.y + r.h > H)
                r.h = H - r.y;
            bool joined = false;
            for (int i = n - 1; i >= 0 && !joined; i--) {
                if (out[i].x == r.x && out[i].w == r.w && out[i].y + out[i].h == r.y) {
                    out[i].h += r.h;
                    joined = true;
                }
            }
            if (!joined) {
                if (n == max) { /* no room: the rest in one */
                    out[n - 1].x = 0;
                    out[n - 1].w = W;
                    out[n - 1].h = H - out[n - 1].y;
                    return n;
                }
                out[n++] = r;
            }
            tx += run;
        }
    }
    return n;
}

static void send_update(void)
{
    static struct rect rects[512];
    int n = due_rects(rects, 512);
    if (!n)
        return;
    V.want = false;
    int count = n; /* Zlib rectangles are cut into pieces of at most ZMAX bytes */
    if (V.enc == ENC_ZLIB) {
        count = 0;
        for (int i = 0; i < n; i++)
            count += (rects[i].h + zlib_rows(rects[i].w) - 1) / zlib_rows(rects[i].w);
    }
    put8(0);
    put8(0);
    put16((uint16_t)count);
    for (int i = 0; i < n; i++) {
        const struct rect *r = &rects[i];
        if (V.enc == ENC_ZLIB) {
            int step = zlib_rows(r->w);
            for (int y = r->y; y < r->y + r->h; y += step) {
                int h = r->y + r->h - y < step ? r->y + r->h - y : step;
                put16((uint16_t)r->x);
                put16((uint16_t)y);
                put16((uint16_t)r->w);
                put16((uint16_t)h);
                put32(ENC_ZLIB);
                zlib_rect(r->x, y, r->w, h);
            }
            continue;
        }
        put16((uint16_t)r->x);
        put16((uint16_t)r->y);
        put16((uint16_t)r->w);
        put16((uint16_t)r->h);
        put32((uint32_t)V.enc);
        if (V.enc == ENC_RAW) {
            raw_rect(r->x, r->y, r->w, r->h);
            continue;
        }
        s_hbg_ok = s_hfg_ok = false;
        for (int y = r->y; y < r->y + r->h; y += TILE)
            for (int x = r->x; x < r->x + r->w; x += TILE)
                hextile_tile(x, y, r->x + r->w - x < TILE ? r->x + r->w - x : TILE,
                             r->y + r->h - y < TILE ? r->y + r->h - y : TILE);
    }
    flush_out();
}

/* ---- input --------------------------------------------------------------------------------------------- */

static const struct {
    uint32_t sym;
    uint16_t code;
} s_keysyms[] = {
    { 0xFF08, KEY_BACKSPACE }, { 0xFF09, KEY_TAB }, { 0xFF0D, KEY_ENTER }, { 0xFF1B, KEY_ESC },
    { 0xFFFF, KEY_DELETE }, { 0xFF50, KEY_HOME }, { 0xFF51, KEY_LEFT }, { 0xFF52, KEY_UP },
    { 0xFF53, KEY_RIGHT }, { 0xFF54, KEY_DOWN }, { 0xFF55, KEY_PAGEUP }, { 0xFF56, KEY_PAGEDOWN },
    { 0xFF57, KEY_END }, { 0xFF63, KEY_INSERT }, { 0xFF67, KEY_COMPOSE }, { 0xFF13, KEY_PAUSE },
    { 0xFF14, KEY_SCROLLLOCK }, { 0xFF61, KEY_SYSRQ }, { 0xFF7F, KEY_NUMLOCK }, { 0xFF8D, KEY_KPENTER },
    { 0xFFAA, KEY_KPASTERISK }, { 0xFFAB, KEY_KPPLUS }, { 0xFFAD, KEY_KPMINUS }, { 0xFFAE, KEY_KPDOT },
    { 0xFFAF, KEY_KPSLASH }, { 0xFFB0, KEY_KP0 }, { 0xFFB1, KEY_KP1 }, { 0xFFB2, KEY_KP2 },
    { 0xFFB3, KEY_KP3 }, { 0xFFB4, KEY_KP4 }, { 0xFFB5, KEY_KP5 }, { 0xFFB6, KEY_KP6 },
    { 0xFFB7, KEY_KP7 }, { 0xFFB8, KEY_KP8 }, { 0xFFB9, KEY_KP9 }, { 0xFF95, KEY_HOME },
    { 0xFF96, KEY_LEFT }, { 0xFF97, KEY_UP }, { 0xFF98, KEY_RIGHT }, { 0xFF99, KEY_DOWN },
    { 0xFF9A, KEY_PAGEUP }, { 0xFF9B, KEY_PAGEDOWN }, { 0xFF9C, KEY_END }, { 0xFF9E, KEY_INSERT },
    { 0xFF9F, KEY_DELETE }, { 0xFFBE, KEY_F1 }, { 0xFFBF, KEY_F2 }, { 0xFFC0, KEY_F3 },
    { 0xFFC1, KEY_F4 }, { 0xFFC2, KEY_F5 }, { 0xFFC3, KEY_F6 }, { 0xFFC4, KEY_F7 },
    { 0xFFC5, KEY_F8 }, { 0xFFC6, KEY_F9 }, { 0xFFC7, KEY_F10 }, { 0xFFC8, KEY_F11 },
    { 0xFFC9, KEY_F12 }, { 0xFFE1, KEY_LEFTSHIFT }, { 0xFFE2, KEY_RIGHTSHIFT }, { 0xFFE3, KEY_LEFTCTRL },
    { 0xFFE4, KEY_RIGHTCTRL }, { 0xFFE5, KEY_CAPSLOCK }, { 0xFFE7, KEY_LEFTMETA }, { 0xFFE8, KEY_RIGHTMETA },
    { 0xFFE9, KEY_LEFTALT }, { 0xFFEA, KEY_RIGHTALT }, { 0xFFEB, KEY_LEFTMETA }, { 0xFFEC, KEY_RIGHTMETA },
    { 0xFE03, KEY_RIGHTALT },
};

/* A keysym as a key code; *shift: 1 the character needs Shift, 0 it must be without, -1 as held */
static int keysym_key(uint32_t sym, uint16_t *code, int *shift)
{
    *shift = -1;
    if (sym >= 0x20 && sym <= 0x7E) {
        unsigned mods;
        if (ui_char_key((int)sym, code, &mods))
            return -1;
        *shift = (mods & UI_MOD_SHIFT) ? 1 : 0;
        return 0;
    }
    for (size_t i = 0; i < sizeof(s_keysyms) / sizeof(s_keysyms[0]); i++)
        if (s_keysyms[i].sym == sym) {
            *code = s_keysyms[i].code;
            return 0;
        }
    return -1;
}

static void key(uint16_t code, int32_t value)
{
    gfx_send_input(s_g, GFX_KEY, code, 0, 0, value);
}

static void key_event(bool down, uint32_t sym)
{
    if (sym == 0xFFE1)
        V.shift_l = down;
    if (sym == 0xFFE2)
        V.shift_r = down;
    int k = -1;
    for (int i = 0; i < V.ndown; i++)
        if (V.down[i].sym == sym)
            k = i;
    if (!down) {
        if (k >= 0) {
            key(V.down[k].code, 0);
            V.down[k] = V.down[--V.ndown];
        }
        return;
    }
    if (k >= 0) { /* held: the viewer repeats it */
        key(V.down[k].code, 2);
        return;
    }
    uint16_t code;
    int shift;
    if (keysym_key(sym, &code, &shift))
        return;
    bool held = V.shift_l || V.shift_r;
    if (shift == 1 && !held) {
        key(KEY_LEFTSHIFT, 1);
        key(code, 1);
        key(KEY_LEFTSHIFT, 0);
    } else if (shift == 0 && held) {
        if (V.shift_l)
            key(KEY_LEFTSHIFT, 0);
        if (V.shift_r)
            key(KEY_RIGHTSHIFT, 0);
        key(code, 1);
        if (V.shift_l)
            key(KEY_LEFTSHIFT, 1);
        if (V.shift_r)
            key(KEY_RIGHTSHIFT, 1);
    } else {
        key(code, 1);
    }
    if (V.ndown < KEYS_DOWN) {
        V.down[V.ndown].sym = sym;
        V.down[V.ndown].code = code;
        V.ndown++;
    }
}

static void pointer_event(uint8_t mask, int x, int y)
{
    x = x < 0 ? 0 : x >= W ? W - 1 : x;
    y = y < 0 ? 0 : y >= H ? H - 1 : y;
    bool left = mask & 1u, was = V.buttons & 1u;
    if (left && !was)
        gfx_send_input(s_g, GFX_PTR_DOWN, 0, x, y, GFX_PTR_MOUSE);
    else if (!left && was)
        gfx_send_input(s_g, GFX_PTR_UP, 0, x, y, GFX_PTR_MOUSE);
    else if (x != V.px || y != V.py)
        gfx_send_input(s_g, left ? GFX_PTR_MOVE : GFX_PTR_HOVER, 0, x, y, GFX_PTR_MOUSE);
    uint8_t pressed = (uint8_t)(mask & ~V.buttons);
    if (pressed & 0x08u) /* button 4: the wheel up */
        gfx_send_input(s_g, GFX_WHEEL, GFX_WHEEL_VERTICAL, x, y, 1);
    if (pressed & 0x10u)
        gfx_send_input(s_g, GFX_WHEEL, GFX_WHEEL_VERTICAL, x, y, -1);
    if (pressed & 0x20u) /* 6 and 7: the horizontal one */
        gfx_send_input(s_g, GFX_WHEEL, GFX_WHEEL_HORIZONTAL, x, y, -1);
    if (pressed & 0x40u)
        gfx_send_input(s_g, GFX_WHEEL, GFX_WHEEL_HORIZONTAL, x, y, 1);
    V.buttons = mask;
    V.px = x;
    V.py = y;
}

/* The viewer's clipboard text (Latin-1: the ASCII part is ours), CR LF made LF */
static void cut_done(void)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < V.cut_len; i++)
        if (!(V.cut[i] == '\r' && i + 1 < V.cut_len && V.cut[i + 1] == '\n'))
            V.cut[n++] = V.cut[i] == '\r' ? '\n' : V.cut[i];
    if (!gfx_clip_set(s_g, V.cut, n))
        s_clip_ignore++;
    V.cut_len = 0;
}

/* Our clipboard to the viewer */
static void send_clip(void)
{
    char *text = (char *)malloc(GFX_CLIP_MAX + 1u);
    if (!text)
        return;
    int n = gfx_clip_get(s_g, text, GFX_CLIP_MAX + 1u);
    if (n >= 0) {
        if (n > (int)GFX_CLIP_MAX)
            n = (int)GFX_CLIP_MAX;
        put8(3);
        put8(0);
        put16(0);
        put32((uint32_t)n);
        put(text, (size_t)n);
        flush_out();
    }
    free(text);
}

/* The viewer's messages in V.in; -1: one we do not know (the stream cannot go on) */
static int process(void)
{
    int used = 0;
    for (;;) {
        uint8_t *p = V.in + used;
        int have = V.have - used;
        if (V.cut_left) {
            uint32_t n = (uint32_t)have < V.cut_left ? (uint32_t)have : V.cut_left;
            uint32_t keep = V.cut_len + n <= GFX_CLIP_MAX ? n : GFX_CLIP_MAX - V.cut_len;
            memcpy(V.cut + V.cut_len, p, keep);
            V.cut_len += keep;
            V.cut_left -= n;
            used += (int)n;
            if (!V.cut_left)
                cut_done();
            if (!n)
                break;
            continue;
        }
        if (!have)
            break;
        int need;
        switch (p[0]) {
        case 0: need = 20; break;
        case 2: need = have >= 4 ? 4 + 4 * be16(p + 2) : 4; break;
        case 3: need = 10; break;
        case 4: need = 8; break;
        case 5: need = 6; break;
        case 6: need = 8; break;
        default: return -1;
        }
        if (need > IN_SIZE)
            return -1;
        if (have < need)
            break;
        switch (p[0]) {
        case 0:
            set_pixel_format(p + 4);
            mark_all(0, 0, W, H); /* all of it again in the new format */
            break;
        case 2: /* the first of its encodings that we have, else Raw */
            V.enc = ENC_RAW;
            for (int i = 0; i < be16(p + 2); i++) {
                int32_t e = (int32_t)be32(p + 4 + 4 * i);
                if (e == ENC_ZLIB || e == ENC_HEXTILE || e == ENC_RAW) {
                    V.enc = e;
                    break;
                }
            }
            break;
        case 3:
            V.want = true;
            V.rx = be16(p + 2);
            V.ry = be16(p + 4);
            V.rw = be16(p + 6);
            V.rh = be16(p + 8);
            if (!p[1]) /* not incremental: all of the area */
                mark_all(V.rx, V.ry, V.rw, V.rh);
            break;
        case 4:
            key_event(p[1] != 0, be32(p + 4));
            break;
        case 5:
            pointer_event(p[1], be16(p + 2), be16(p + 4));
            break;
        case 6:
            V.cut_left = be32(p + 4);
            V.cut_len = 0;
            if (!V.cut_left)
                cut_done();
            break;
        }
        used += need;
    }
    memmove(V.in, V.in + used, (size_t)(V.have - used));
    V.have -= used;
    return 0;
}

/* ---- logging in -------------------------------------------------------------------------------------- */

static bool read_password(char pw[9])
{
    FILE *f = fopen(PASSWD_FILE, "r");
    if (!f)
        return false;
    char line[64] = "";
    bool ok = fgets(line, sizeof(line), f) != NULL;
    fclose(f);
    line[strcspn(line, "\r\n")] = 0;
    snprintf(pw, 9, "%s", line);
    return ok && pw[0];
}

static void random_bytes(uint8_t *b, size_t n)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, b, n) != (int)n) {
        uint64_t t = crtos_time_us();
        for (size_t i = 0; i < n; i++) /* (no randomness: at least different each time) */
            b[i] = (uint8_t)(t >> (8 * (i % 8))) ^ (uint8_t)(i * 151u);
    }
    if (fd >= 0)
        close(fd);
}

static void send_reason(int fd, const char *why)
{
    uint32_t n = (uint32_t)strlen(why);
    uint8_t l[4] = { (uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n };
    send_all(fd, l, 4);
    send_all(fd, why, n);
}

/* SecurityResult "failed" (RFB 3.8 with a reason) */
static void auth_failed(int fd, int minor)
{
    uint8_t b[4] = { 0, 0, 0, 1 };
    send_all(fd, b, 4);
    if (minor >= 8)
        send_reason(fd, "wrong password");
}

/* Version, security, the password, ClientInit and ServerInit; 0: the viewer is in */
static int handshake(int fd, const char *peer)
{
    struct timeval tv = { 10, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int on = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    char ver[13] = { 0 };
    if (send_all(fd, "RFB 003.008\n", 12) || recv_all(fd, ver, 12) || memcmp(ver, "RFB 003.", 8))
        return -1;
    int minor = atoi(ver + 8);
    minor = minor >= 8 ? 8 : minor == 7 ? 7 : 3;
    char pw[9];
    bool have_pw = read_password(pw);
    const char *no_pw = "no password on the board (" PASSWD_FILE "): \"crtos desktop\" installs one";
    if (minor >= 7) {
        if (!have_pw) { /* no security types: the connection failed, and why */
            uint8_t zero = 0;
            send_all(fd, &zero, 1);
            send_reason(fd, no_pw);
            printf("vncd: %s turned away: no password (%s)\n", peer, PASSWD_FILE);
            return -1;
        }
        uint8_t types[2] = { 1, 2 }, chosen = 0;
        if (send_all(fd, types, 2) || recv_all(fd, &chosen, 1) || chosen != 2)
            return -1;
    } else {
        uint8_t t[4] = { 0, 0, 0, (uint8_t)(have_pw ? 2 : 0) }; /* 0: failed, a reason follows */
        send_all(fd, t, 4);
        if (!have_pw) {
            send_reason(fd, no_pw);
            printf("vncd: %s turned away: no password (%s)\n", peer, PASSWD_FILE);
            return -1;
        }
    }
    uint8_t challenge[16], response[16], expected[16];
    random_bytes(challenge, 16);
    if (send_all(fd, challenge, 16) || recv_all(fd, response, 16))
        return -1;
    vnc_auth_response(pw, challenge, expected);
    unsigned diff = 0; /* the same time for any wrong answer */
    for (int i = 0; i < 16; i++)
        diff |= (unsigned)(expected[i] ^ response[i]);
    if (diff) {
        printf("vncd: %s: wrong password\n", peer);
        crtos_sleep_ms(1000);
        auth_failed(fd, minor);
        return -1;
    }
    uint8_t ok[4] = { 0, 0, 0, 0 }, shared;
    if (send_all(fd, ok, 4) || recv_all(fd, &shared, 1))
        return -1;
    /* ServerInit: our size, our pixel format (RGB565, little endian), the name */
    uint8_t init[24 + sizeof(NAME) - 1] = { (uint8_t)(W >> 8), (uint8_t)W, (uint8_t)(H >> 8), (uint8_t)H,
                                            16, 16, 0, 1, 0, 31, 0, 63, 0, 31, 11, 5, 0, 0, 0, 0,
                                            0, 0, 0, (uint8_t)(sizeof(NAME) - 1) };
    memcpy(init + 24, NAME, sizeof(NAME) - 1);
    if (send_all(fd, init, sizeof(init)))
        return -1;
    printf("vncd: %s connected (RFB 3.%d)\n", peer, minor);
    return 0;
}

/* ---- the session --------------------------------------------------------------------------------------- */

static void viewer_start(int fd)
{
    free(V.cut);
    memset(&V, 0, sizeof(V));
    V.fd = fd;
    V.cut = (char *)malloc(GFX_CLIP_MAX);
    struct pixfmt native = { 16, 16, 0, 1, 31, 63, 31, 11, 5, 0 };
    set_format(&native);
    zdef_reset(&s_z); /* a new viewer, a new stream */
    struct timeval tv = { 15, 0 }; /* a viewer that stops reading is dropped after this */
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    memset(s_dirty, 1, (size_t)(TX * TY));
}

/* The screen from gfxd; false when that is not possible (no gfxd, no capability) */
static bool screen_start(void)
{
    s_g = gfx_open();
    if (!s_g)
        return false;
    W = gfx_screen_width(s_g);
    H = gfx_screen_height(s_g);
    TX = (W + TILE - 1) / TILE;
    TY = (H + TILE - 1) / TILE;
    /* gfxd's copy, cached: the 2D accelerator invalidates what it wrote, before and after (an
     * uncached copy cost 120 ms a full window to compare) */
    s_shm = crtos_shm_create((size_t)W * (size_t)H * 2u, 0);
    s_shadow = s_shm >= 0 ? (uint16_t *)crtos_shm_map(s_shm) : NULL;
    /* our copy as large as the screen, outside the heap (750 KB at 800x480); zeroed */
    int fb_shm = crtos_shm_create((size_t)W * (size_t)H * 2u, 0);
    s_fb = fb_shm >= 0 ? (uint16_t *)crtos_shm_map(fb_shm) : NULL;
    if (fb_shm >= 0)
        close(fb_shm); /* (the mapping keeps it) */
    s_dirty = (uint8_t *)malloc((size_t)(TX * TY));
    s_row = (uint8_t *)malloc((size_t)W * 4u);
    s_work = (uint8_t *)malloc(ZMAX);
    s_zout = (uint8_t *)malloc(zdef_bound(ZMAX));
    if (!s_shadow || !s_fb || !s_dirty || !s_row || !s_work || zdef_init(&s_z, s_zout, ZMAX) ||
        gfx_screen_watch(s_g, s_shm, (uint32_t)W * 2u)) {
        printf("vncd: no screen copy (%s)\n", strerror(errno));
        return false;
    }
    gfx_clip_watch(s_g);
    s_have_screen = false;
    s_clip_ignore = 0;
    return true;
}

static void screen_stop(void)
{
    if (s_g)
        gfx_close(s_g); /* gfxd stops the copy */
    s_g = NULL;
    if (s_shadow)
        crtos_shm_unmap(s_shadow);
    if (s_shm >= 0)
        close(s_shm);
    if (s_fb)
        crtos_shm_unmap(s_fb);
    free(s_dirty);
    free(s_row);
    free(s_work);
    free(s_zout);
    s_shadow = NULL;
    s_shm = -1;
    s_fb = NULL;
    s_dirty = NULL;
    s_row = s_work = s_zout = NULL;
}

static void peer_name(int fd, char *buf, size_t size)
{
    struct sockaddr_in a;
    socklen_t n = sizeof(a);
    if (getpeername(fd, (struct sockaddr *)&a, &n) || !inet_ntop(AF_INET, &a.sin_addr, buf, (socklen_t)size))
        snprintf(buf, size, "?");
}

/* Serve viewers until the last one leaves */
static void session(int fd, int listener)
{
    char peer[INET_ADDRSTRLEN];
    peer_name(fd, peer, sizeof(peer));
    if (!screen_start()) {
        screen_stop();
        close(fd);
        return;
    }
    if (handshake(fd, peer)) {
        screen_stop();
        close(fd);
        return;
    }
    viewer_start(fd);
    for (;;) {
        struct pollfd pf[3] = { { V.fd, POLLIN, 0 }, { gfx_event_handle(s_g), POLLIN, 0 }, { listener, POLLIN, 0 } };
        if (poll(pf, 3, 1000) < 0)
            continue;
        if (pf[2].revents & POLLIN) { /* another viewer: it takes over once logged in */
            struct sockaddr_in a;
            socklen_t alen = sizeof(a);
            int nfd = accept(listener, (struct sockaddr *)&a, &alen);
            if (nfd >= 0) {
                char np[INET_ADDRSTRLEN];
                peer_name(nfd, np, sizeof(np));
                if (handshake(nfd, np) == 0) {
                    printf("vncd: %s takes over from %s\n", np, peer);
                    close(V.fd);
                    viewer_start(nfd);
                    snprintf(peer, sizeof(peer), "%s", np);
                } else {
                    close(nfd);
                }
            }
        }
        if (pf[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            int r = (int)recv(V.fd, V.in + V.have, (size_t)(IN_SIZE - V.have), 0);
            if (r <= 0 || (V.have += r, process()) < 0)
                break;
        }
        struct gfx_event ev;
        while (gfx_next_event(s_g, &ev, 0) == 0) {
            if (ev.h.type == GFX_EV_SCREEN) {
                take_screen();
            } else if (ev.h.type == GFX_EV_CLIP) {
                if (s_clip_ignore) /* the viewer's own text coming back */
                    s_clip_ignore--;
                else
                    send_clip();
            }
        }
        if (V.want && s_have_screen)
            send_update();
        if (V.out_err)
            break;
    }
    printf("vncd: %s disconnected\n", peer);
    close(V.fd);
    free(V.cut);
    V.cut = NULL;
    screen_stop();
}

static void *serve(void *arg)
{
    int l = (int)(intptr_t)arg;
    for (;;) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int fd = accept(l, (struct sockaddr *)&peer, &plen);
        if (fd >= 0)
            session(fd, l);
    }
    return NULL;
}

int main(void)
{
    int l;
    while ((l = socket(AF_INET, SOCK_STREAM, 0)) < 0) /* the network stack may still be loading */
        crtos_sleep_ms(500);
    int on = 1;
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    if (bind(l, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(l, 2) < 0) {
        printf("vncd: port %d: %s\n", PORT, strerror(errno));
        return 1;
    }
    printf("vncd: listening on port %d\n", PORT);
    /* the viewers are served at a priority above the programs': a busy program (a game) would
     * otherwise take half the processor from the picture; we only work when the screen changed
     * and the viewer asked for it */
    crtos_thread_t *t = crtos_thread_start(serve, (void *)(intptr_t)l, SERVE_STACK, SERVE_PRIO);
    if (!t)
        serve((void *)(intptr_t)l);
    crtos_thread_join(t);
    return 0;
}
