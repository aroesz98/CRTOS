/*
 * wallpaper.c - the desktop's wallpaper (gfxd draws it; Settings shows small ones): built-in
 * pictures computed for any size, a solid colour, or a picture file.
 *
 * Built-in: gradients with soft glows, worked out per pixel (no files, any screen). A glow
 * is (1 - q)^2 inside an ellipse (q its normalised square distance): smooth, cheap, and no
 * edge. Colours are computed with 8 bits and dithered to RGB565 (an ordered 4 x 4 Bayer
 * pattern): gradients without bands.
 *
 * Files: PPM or PAM (8 bits, grey, grey + alpha, RGB, RGBA; alpha over black), read row by row
 * and shrunk by averaging the source pixels each target pixel covers (enlarged: the nearest
 * one) - any size in a few KB of memory. Tiles are read whole (small pictures only).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gfx.h"
#include "gfx_internal.h"

#define SOURCE_MAX_W 4096       /* pixels in a row of a picture file */
#define TILE_MAX_PIXELS (256 * 256)
#define BAR_COLOR 0x101216u     /* around a picture that does not cover the surface */

static const uint8_t s_bayer[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };

/* One pixel of the target, 8 bits a channel (dithered into RGB565) */
static inline void put(const struct gfx_surface *s, int x, int y, unsigned r, unsigned g, unsigned b)
{
    uint8_t *row = (uint8_t *)s->pix + (size_t)y * (size_t)s->stride;
    if (s->format == GPU2D_FMT_RGB565) {
        unsigned t = s_bayer[(y & 3) * 4 + (x & 3)] * 255u;
        unsigned r5 = (r * 31u * 16u + t) / (255u * 16u), g6 = (g * 63u * 16u + t) / (255u * 16u);
        unsigned b5 = (b * 31u * 16u + t) / (255u * 16u);
        ((uint16_t *)row)[x] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
    } else {
        ((uint32_t *)row)[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

/* ---- built-in pictures ---- */

struct rgb {
    float r, g, b;
};

static inline struct rgb mix(struct rgb a, struct rgb b, float t)
{
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    struct rgb o = { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t };
    return o;
}

/* how much of a glow about (cx, cy), radii rx, ry, reaches (u, v): 1 at its centre, 0 at its rim */
static inline float glow(float u, float v, float cx, float cy, float rx, float ry)
{
    float dx = (u - cx) / rx, dy = (v - cy) / ry, q = dx * dx + dy * dy;
    return q < 1.0f ? (1.0f - q) * (1.0f - q) : 0.0f;
}

static inline void add(struct rgb *o, struct rgb c, float k)
{
    o->r += c.r * k;
    o->g += c.g * k;
    o->b += c.b * k;
}

#define RGB(r, g, b) ((struct rgb){ (float)(r), (float)(g), (float)(b) })

/* u across (0 to the aspect ratio a), v down (0 to 1) */
static struct rgb aurora(float u, float v, float a)
{
    struct rgb o = mix(RGB(14, 18, 48), RGB(5, 8, 18), v);
    add(&o, RGB(20, 190, 175), 0.55f * glow(u, v, 0.28f * a, 0.30f, 0.55f * a, 0.45f));
    add(&o, RGB(120, 60, 255), 0.50f * glow(u, v, 0.72f * a, 0.20f, 0.50f * a, 0.50f));
    add(&o, RGB(40, 100, 255), 0.40f * glow(u, v, 0.52f * a, 0.98f, 0.80f * a, 0.40f));
    add(&o, RGB(255, 80, 170), 0.22f * glow(u, v, 0.92f * a, 0.78f, 0.35f * a, 0.40f));
    return o;
}

static float hills(float u)
{
    /* sin from a parabola: smooth enough, no libm call per pixel */
    float s1 = u * 5.0f + 1.0f, s2 = u * 13.0f;
    s1 -= 6.2831853f * (float)(int)(s1 / 6.2831853f);
    s2 -= 6.2831853f * (float)(int)(s2 / 6.2831853f);
    s1 = s1 > 3.1415927f ? s1 - 6.2831853f : s1;
    s2 = s2 > 3.1415927f ? s2 - 6.2831853f : s2;
    float y1 = 1.2732395f * s1 - 0.4052847f * s1 * (s1 < 0 ? -s1 : s1);
    float y2 = 1.2732395f * s2 - 0.4052847f * s2 * (s2 < 0 ? -s2 : s2);
    return 0.80f + 0.05f * y1 + 0.025f * y2;
}

static struct rgb dusk(float u, float v, float a)
{
    struct rgb o = v < 0.55f ? mix(RGB(22, 20, 66), RGB(118, 44, 108), v / 0.55f)
                             : mix(RGB(118, 44, 108), RGB(250, 122, 72), (v - 0.55f) / 0.45f);
    add(&o, RGB(255, 210, 140), 0.75f * glow(u, v, 0.68f * a, 0.80f, 0.22f * a, 0.30f));
    float hy = hills(u / a);
    if (v > hy) { /* hills in front of the sun */
        float d = (v - hy) * 12.0f;
        o = mix(o, RGB(34, 18, 44), 0.85f + (d > 1.0f ? 0.15f : 0.15f * d));
    }
    return o;
}

static struct rgb ocean(float u, float v, float a)
{
    struct rgb o = mix(RGB(3, 24, 54), RGB(10, 100, 135), 0.6f * u / a + 0.4f * v);
    for (int k = 0; k < 4; k++) {
        float w = u / a * 4.0f + (float)k * 1.7f;
        w -= 6.2831853f * (float)(int)(w / 6.2831853f);
        w = w > 3.1415927f ? w - 6.2831853f : w;
        float sw = 1.2732395f * w - 0.4052847f * w * (w < 0 ? -w : w);
        float b = v - (0.48f + 0.12f * (float)k + 0.035f * sw);
        b = b < 0 ? -b : b;
        if (b < 0.035f)
            add(&o, RGB(80, 200, 230), 0.13f * (1.0f - b / 0.035f));
    }
    add(&o, RGB(120, 220, 255), 0.18f * glow(u, v, 0.2f * a, 0.1f, 0.6f * a, 0.5f));
    return o;
}

static struct rgb forest(float u, float v, float a)
{
    struct rgb o = mix(RGB(8, 30, 24), RGB(20, 70, 50), v);
    add(&o, RGB(130, 215, 140), 0.35f * glow(u, v, 0.15f * a, 0.10f, 0.60f * a, 0.60f));
    add(&o, RGB(20, 140, 125), 0.30f * glow(u, v, 0.85f * a, 0.70f, 0.50f * a, 0.50f));
    return o;
}

static struct rgb graphite(float u, float v, float a)
{
    float q = glow(u, v, 0.5f * a, 0.42f, 0.95f * a, 0.95f);
    struct rgb o = mix(RGB(14, 16, 20), RGB(60, 66, 78), q);
    add(&o, RGB(52, 120, 220), 0.10f * glow(u, v, 0.5f * a, 1.0f, 0.7f * a, 0.4f));
    return o;
}

static struct rgb classic(float u, float v, float a)
{
    (void)u;
    (void)a;
    float t = v * 255.0f;
    return RGB(10.0f + t / 8.0f, 24.0f + t / 6.0f, 48.0f + t / 3.0f);
}

static const struct {
    const char *name;
    struct rgb (*shade)(float u, float v, float a);
} s_builtin[] = {
    { "Aurora", aurora }, { "Dusk", dusk }, { "Ocean", ocean }, { "Forest", forest },
    { "Graphite", graphite }, { "CRTOS", classic },
};
#define NBUILTIN ((int)(sizeof(s_builtin) / sizeof(s_builtin[0])))

const char *gfx_wallpaper_builtin(int i)
{
    return i >= 0 && i < NBUILTIN ? s_builtin[i].name : NULL;
}

static unsigned clamp8(float f)
{
    return f <= 0.0f ? 0u : f >= 255.0f ? 255u : (unsigned)(f + 0.5f);
}

static void draw_builtin(const struct gfx_surface *s, int k)
{
    float a = (float)s->w / (float)s->h, fu = a / (float)s->w, fv = 1.0f / (float)s->h;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            struct rgb c = s_builtin[k].shade(((float)x + 0.5f) * fu, ((float)y + 0.5f) * fv, a);
            put(s, x, y, clamp8(c.r), clamp8(c.g), clamp8(c.b));
        }
    if (s_builtin[k].shade == classic && s->w > 200) { /* its name, as gfxd always had it */
        int tw = gfx_text_width(&gfx_sans_bold12, "CRTOS");
        gfx_text(s, &gfx_sans_bold12, s->w - tw - 12, s->h - 12, "CRTOS", GFX_RGB(90, 120, 170));
    }
}

static void fill_rgb(const struct gfx_surface *s, int x, int y, int w, int h, uint32_t rgb)
{
    for (int j = y; j < y + h; j++)
        for (int i = x; i < x + w; i++)
            put(s, i, j, (rgb >> 16) & 255u, (rgb >> 8) & 255u, rgb & 255u);
}

/* ---- picture files ---- */

struct reader {
    int fd;
    char buf[1024];
    int have, pos;
};

static bool read_bytes(struct reader *r, uint8_t *out, size_t n)
{
    while (n) {
        if (r->pos == r->have) {
            int k = (int)read(r->fd, r->buf, sizeof(r->buf));
            if (k <= 0)
                return false;
            r->have = k;
            r->pos = 0;
        }
        size_t take = (size_t)(r->have - r->pos) < n ? (size_t)(r->have - r->pos) : n;
        memcpy(out, r->buf + r->pos, take);
        r->pos += (int)take;
        out += take;
        n -= take;
    }
    return true;
}

/* source pixel i of a row of @depth bytes as r, g, b (alpha over black) */
static inline void source_rgb(const uint8_t *row, int i, int depth, unsigned *r, unsigned *g, unsigned *b)
{
    const uint8_t *p = row + (size_t)i * (size_t)depth;
    switch (depth) {
    case 1:
        *r = *g = *b = p[0];
        break;
    case 2:
        *r = *g = *b = p[0] * p[1] / 255u;
        break;
    case 3:
        *r = p[0];
        *g = p[1];
        *b = p[2];
        break;
    default:
        *r = p[0] * p[3] / 255u;
        *g = p[1] * p[3] / 255u;
        *b = p[2] * p[3] / 255u;
        break;
    }
}

/* Source rectangle (sx, sy, sw, sh) of the picture onto (dx, dy, dw, dh) of @s, averaging */
static int scale_rows(const struct gfx_surface *s, struct reader *rd, int w0, int depth, int sx, int sy, int sw,
                      int sh, int dx, int dy, int dw, int dh)
{
    size_t rowbytes = (size_t)w0 * (size_t)depth;
    uint8_t *row = (uint8_t *)malloc(rowbytes);
    uint32_t *acc = (uint32_t *)malloc((size_t)dw * 3u * sizeof(uint32_t));
    uint16_t *col = (uint16_t *)malloc(((size_t)dw + 1u) * sizeof(uint16_t));
    int ret = -1;
    if (!row || !acc || !col) {
        errno = ENOMEM;
        goto out;
    }
    for (int c = 0; c <= dw; c++) /* target column c takes source columns col[c] .. col[c + 1] - 1 */
        col[c] = (uint16_t)(sx + (int)((long)c * sw / dw));
    int loaded = -1; /* the source row in @row */
    for (int j = 0; j < dh; j++) {
        int y0 = sy + (int)((long)j * sh / dh), y1 = sy + (int)((long)(j + 1) * sh / dh);
        if (y1 <= y0)
            y1 = y0 + 1; /* (enlarged: the nearest row) */
        memset(acc, 0, (size_t)dw * 3u * sizeof(uint32_t));
        for (int y = y0; y < y1; y++) {
            while (loaded < y) {
                if (!read_bytes(rd, row, rowbytes)) {
                    errno = EINVAL;
                    goto out;
                }
                loaded++;
            }
            for (int c = 0; c < dw; c++) {
                int x0 = col[c], x1 = col[c + 1] > x0 ? col[c + 1] : x0 + 1;
                uint32_t *a = acc + c * 3;
                for (int x = x0; x < x1; x++) {
                    unsigned r, g, b;
                    source_rgb(row, x, depth, &r, &g, &b);
                    a[0] += r;
                    a[1] += g;
                    a[2] += b;
                }
            }
        }
        for (int c = 0; c < dw; c++) {
            uint32_t n = (uint32_t)(y1 - y0) * (uint32_t)(col[c + 1] > col[c] ? col[c + 1] - col[c] : 1);
            uint32_t *a = acc + c * 3;
            put(s, dx + c, dy + j, a[0] / n, a[1] / n, a[2] / n);
        }
    }
    ret = 0;
out:
    free(row);
    free(acc);
    free(col);
    return ret;
}

static int draw_tiles(const struct gfx_surface *s, const char *path)
{
    struct gfx_image *img = gfx_image_load(path);
    if (!img)
        return -1;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint32_t p = img->pix[(y % img->h) * img->w + x % img->w], a = p >> 24;
            put(s, x, y, ((p >> 16) & 255u) * a / 255u, ((p >> 8) & 255u) * a / 255u, (p & 255u) * a / 255u);
        }
    free(img);
    return 0;
}

static int draw_file(const struct gfx_surface *s, const char *path, int fit)
{
    struct reader rd;
    memset(&rd, 0, sizeof(rd));
    rd.fd = open(path, O_RDONLY);
    if (rd.fd < 0)
        return -1;
    rd.have = (int)read(rd.fd, rd.buf, sizeof(rd.buf));
    int w0, h0, at, depth = gfx_netpbm_header(rd.buf, rd.have, &w0, &h0, &at);
    int ret = -1;
    if (!depth || w0 > SOURCE_MAX_W) {
        errno = EINVAL;
        goto out;
    }
    rd.pos = at;
    int W = s->w, H = s->h;
    if (fit == GFX_FIT_TILE) {
        if ((long)w0 * h0 > TILE_MAX_PIXELS) {
            fit = GFX_FIT_FILL; /* (too big to keep: a picture, not a tile) */
        } else {
            close(rd.fd);
            return draw_tiles(s, path);
        }
    }
    int sx = 0, sy = 0, sw = w0, sh = h0, dx = 0, dy = 0, dw = W, dh = H;
    bool wider = (long)w0 * H > (long)W * h0; /* the picture is wider than the surface */
    switch (fit) {
    case GFX_FIT_FIT:
        if (wider) {
            dh = (int)((long)h0 * W / w0);
            dy = (H - dh) / 2;
        } else {
            dw = (int)((long)w0 * H / h0);
            dx = (W - dw) / 2;
        }
        break;
    case GFX_FIT_STRETCH:
        break;
    case GFX_FIT_CENTER:
        dw = w0 < W ? w0 : W;
        dh = h0 < H ? h0 : H;
        dx = (W - dw) / 2;
        dy = (H - dh) / 2;
        sx = (w0 - dw) / 2;
        sy = (h0 - dh) / 2;
        sw = dw;
        sh = dh;
        break;
    default: /* fill: cut what sticks out */
        if (wider) {
            sw = (int)((long)W * h0 / H);
            sx = (w0 - sw) / 2;
        } else {
            sh = (int)((long)H * w0 / W);
            sy = (h0 - sh) / 2;
        }
        break;
    }
    if (dw < 1 || dh < 1 || sw < 1 || sh < 1) {
        errno = EINVAL;
        goto out;
    }
    if (dx > 0 || dy > 0 || dw < W || dh < H) { /* bars around it */
        fill_rgb(s, 0, 0, W, dy, BAR_COLOR);
        fill_rgb(s, 0, dy + dh, W, H - dy - dh, BAR_COLOR);
        fill_rgb(s, 0, dy, dx, dh, BAR_COLOR);
        fill_rgb(s, dx + dw, dy, W - dx - dw, dh, BAR_COLOR);
    }
    ret = scale_rows(s, &rd, w0, depth, sx, sy, sw, sh, dx, dy, dw, dh);
out:
    close(rd.fd);
    return ret;
}

int gfx_wallpaper_draw(const struct gfx_surface *s, const char *spec, int fit)
{
    if (!spec)
        spec = "";
    for (int k = 0; k < NBUILTIN; k++)
        if (!strcmp(spec, s_builtin[k].name)) {
            draw_builtin(s, k);
            return 0;
        }
    if (!strncmp(spec, "color:", 6)) {
        fill_rgb(s, 0, 0, s->w, s->h, (uint32_t)strtoul(spec + 6, NULL, 16) & 0xFFFFFFu);
        return 0;
    }
    if (spec[0] && draw_file(s, spec, fit) == 0) /* anything else is a file */
        return 0;
    int e = spec[0] ? errno : ENOENT;
    draw_builtin(s, 0);
    errno = e;
    return -1;
}
