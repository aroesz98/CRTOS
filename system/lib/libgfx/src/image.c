/*
 * image.c - pictures for programs: loading netpbm files (PAM with alpha, PPM), smooth
 * shrinking and blended drawing on any surface; the icons of programs.
 *
 * The icons lie in GFX_ICON_DIR as <program>.pam (RGB_ALPHA, usually 64 x 64: "crtos build"
 * makes them from the PNG of crtos_app(... ICON)); a program shows one at the size it needs.
 * netpbm keeps the reader small - a header of text and the pixels as they are - where a PNG
 * would need a decompressor.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gfx.h"
#include "gfx_internal.h"

#define HEADER_MAX 512
#define IMAGE_MAX_PIXELS (1024 * 1024)

static struct gfx_image *image_new(int w, int h)
{
    if (w <= 0 || h <= 0 || (long)w * h > IMAGE_MAX_PIXELS) {
        errno = EINVAL;
        return NULL;
    }
    struct gfx_image *img = (struct gfx_image *)malloc(sizeof(*img) + (size_t)w * (size_t)h * 4u);
    if (!img) {
        errno = ENOMEM;
        return NULL;
    }
    img->w = w;
    img->h = h;
    img->pix = (uint32_t *)(img + 1);
    return img;
}

/* ---- the header ---- */

struct header {
    const char *p, *end;
};

static void skip_space(struct header *hd)
{
    while (hd->p < hd->end) {
        if (*hd->p == '#') { /* a comment to the end of the line */
            while (hd->p < hd->end && *hd->p != '\n')
                hd->p++;
        } else if (*hd->p == ' ' || *hd->p == '\t' || *hd->p == '\r' || *hd->p == '\n') {
            hd->p++;
        } else {
            break;
        }
    }
}

/* The next word (at most @size - 1 characters); false at the end */
static bool word(struct header *hd, char *out, size_t size)
{
    skip_space(hd);
    size_t n = 0;
    while (hd->p < hd->end && *hd->p > ' ') {
        if (n + 1 < size)
            out[n++] = *hd->p;
        hd->p++;
    }
    out[n] = 0;
    return n > 0;
}

static bool number(struct header *hd, int *v)
{
    char w[12];
    if (!word(hd, w, sizeof(w)))
        return false;
    char *e;
    long n = strtol(w, &e, 10);
    if (*e || n <= 0 || n > 65535)
        return false;
    *v = (int)n;
    return true;
}

/* PAM: "P7", then WIDTH, HEIGHT, DEPTH, MAXVAL and TUPLTYPE lines and ENDHDR. PPM: "P6 w h
 * maxval" and one white space character. The number of bytes a pixel has (1-4), or 0. */
static int parse_header(struct header *hd, int *w, int *h)
{
    char tok[20];
    if (!word(hd, tok, sizeof(tok)))
        return 0;
    int depth = 0, maxval = 0;
    *w = *h = 0;
    if (!strcmp(tok, "P6")) {
        if (!number(hd, w) || !number(hd, h) || !number(hd, &maxval) || hd->p >= hd->end)
            return 0;
        hd->p++; /* the single white space before the pixels */
        depth = 3;
    } else if (!strcmp(tok, "P7")) {
        for (;;) {
            if (!word(hd, tok, sizeof(tok)))
                return 0;
            if (!strcmp(tok, "ENDHDR")) {
                while (hd->p < hd->end && *hd->p != '\n') /* the rest of its line */
                    hd->p++;
                if (hd->p >= hd->end)
                    return 0;
                hd->p++;
                break;
            }
            if (!strcmp(tok, "TUPLTYPE")) { /* the depth says enough */
                word(hd, tok, sizeof(tok));
                continue;
            }
            int *v = !strcmp(tok, "WIDTH") ? w : !strcmp(tok, "HEIGHT") ? h : !strcmp(tok, "DEPTH") ? &depth
                   : !strcmp(tok, "MAXVAL") ? &maxval : NULL;
            if (!v || !number(hd, v))
                return 0;
        }
    } else {
        return 0;
    }
    if (maxval != 255 || depth < 1 || depth > 4 || *w <= 0 || *h <= 0)
        return 0;
    return depth;
}

int gfx_netpbm_header(const char *buf, int len, int *w, int *h, int *pixels)
{
    struct header hd = { buf, buf + (len > 0 ? len : 0) };
    int depth = parse_header(&hd, w, h);
    *pixels = (int)(hd.p - buf);
    return depth;
}

/* The pixels of @depth bytes (grey, grey + alpha, RGB, RGBA) at the end of the image's
 * memory become 0xAARRGGBB from its start: each result goes where the bytes before it were
 * read already */
static void expand(struct gfx_image *img, int depth)
{
    size_t n = (size_t)img->w * (size_t)img->h;
    const uint8_t *src = (const uint8_t *)img->pix + n * (4u - (size_t)depth);
    uint32_t *dst = img->pix;
    for (size_t i = 0; i < n; i++, src += depth) {
        uint32_t r, g, b, a = 255;
        switch (depth) {
        case 1:
            r = g = b = src[0];
            break;
        case 2:
            r = g = b = src[0];
            a = src[1];
            break;
        case 3:
            r = src[0];
            g = src[1];
            b = src[2];
            break;
        default:
            r = src[0];
            g = src[1];
            b = src[2];
            a = src[3];
            break;
        }
        dst[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

struct gfx_image *gfx_image_load(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    char head[HEADER_MAX];
    int n = (int)read(fd, head, sizeof(head));
    struct header hd = { head, head + (n > 0 ? n : 0) };
    int w, h, depth = parse_header(&hd, &w, &h);
    struct gfx_image *img = depth ? image_new(w, h) : NULL;
    if (!img) {
        if (!depth)
            errno = EINVAL;
        close(fd);
        return NULL;
    }
    size_t need = (size_t)w * (size_t)h * (size_t)depth;
    uint8_t *raw = (uint8_t *)img->pix + (size_t)w * (size_t)h * (4u - (size_t)depth);
    size_t have = (size_t)(hd.end - hd.p); /* pixels read with the header */
    if (have > need)
        have = need;
    memcpy(raw, hd.p, have);
    while (have < need) {
        int r = (int)read(fd, raw + have, need - have);
        if (r <= 0)
            break;
        have += (size_t)r;
    }
    close(fd);
    if (have < need) { /* cut short */
        free(img);
        errno = EINVAL;
        return NULL;
    }
    expand(img, depth);
    return img;
}

/* ---- scaling ---- */

struct gfx_image *gfx_image_scale(const struct gfx_image *img, int w, int h)
{
    struct gfx_image *out = image_new(w, h);
    if (!out)
        return NULL;
    /* each result pixel: the average of the source area under it, weighted by how much of
     * each source pixel it covers, the colours by their alpha (no dark fringes) */
    float fx = (float)img->w / (float)w, fy = (float)img->h / (float)h;
    for (int oy = 0; oy < h; oy++) {
        float y0 = (float)oy * fy, y1 = y0 + fy;
        int sy1 = (int)y1 < img->h ? (int)y1 : img->h - 1;
        if ((float)sy1 == y1 && sy1 > (int)y0)
            sy1--;
        for (int ox = 0; ox < w; ox++) {
            float x0 = (float)ox * fx, x1 = x0 + fx;
            int sx1 = (int)x1 < img->w ? (int)x1 : img->w - 1;
            if ((float)sx1 == x1 && sx1 > (int)x0)
                sx1--;
            float a = 0, r = 0, g = 0, b = 0, area = 0;
            for (int sy = (int)y0; sy <= sy1; sy++) {
                float top = (float)sy > y0 ? (float)sy : y0;
                float bottom = (float)(sy + 1) < y1 ? (float)(sy + 1) : y1;
                const uint32_t *line = img->pix + (size_t)sy * (size_t)img->w;
                for (int sx = (int)x0; sx <= sx1; sx++) {
                    float left = (float)sx > x0 ? (float)sx : x0;
                    float right = (float)(sx + 1) < x1 ? (float)(sx + 1) : x1;
                    float k = (bottom - top) * (right - left);
                    uint32_t p = line[sx];
                    float pa = (float)(p >> 24) * k;
                    a += pa;
                    r += (float)((p >> 16) & 255u) * pa;
                    g += (float)((p >> 8) & 255u) * pa;
                    b += (float)(p & 255u) * pa;
                    area += k;
                }
            }
            uint32_t v = 0;
            if (a > 0.0f && area > 0.0f) {
                uint32_t oa = (uint32_t)(a / area + 0.5f);
                uint32_t orr = (uint32_t)(r / a + 0.5f), og = (uint32_t)(g / a + 0.5f), ob = (uint32_t)(b / a + 0.5f);
                v = ((oa > 255u ? 255u : oa) << 24) | ((orr > 255u ? 255u : orr) << 16) |
                    ((og > 255u ? 255u : og) << 8) | (ob > 255u ? 255u : ob);
            }
            out->pix[(size_t)oy * (size_t)w + (size_t)ox] = v;
        }
    }
    return out;
}

/* ---- drawing ---- */

static inline uint32_t mix(uint32_t s, uint32_t d, uint32_t a)
{
    return (s * a + d * (255u - a) + 127u) / 255u;
}

void gfx_image_draw(const struct gfx_surface *s, int x, int y, const struct gfx_image *img)
{
    if (!img)
        return;
    int x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0;
    int x1 = x + img->w > s->w ? s->w - x : img->w, y1 = y + img->h > s->h ? s->h - y : img->h;
    for (int j = y0; j < y1; j++) {
        const uint32_t *src = img->pix + (size_t)j * (size_t)img->w;
        uint8_t *line = (uint8_t *)s->pix + (size_t)(y + j) * (size_t)s->stride;
        for (int i = x0; i < x1; i++) {
            uint32_t p = src[i], a = p >> 24;
            if (!a)
                continue;
            uint32_t r = (p >> 16) & 255u, g = (p >> 8) & 255u, b = p & 255u;
            if (s->format == GPU2D_FMT_RGB565) {
                uint16_t *d = (uint16_t *)line + x + i;
                if (a < 255u) {
                    uint32_t dv = *d;
                    uint32_t dr = ((dv >> 11) & 31u) * 255u / 31u, dg = ((dv >> 5) & 63u) * 255u / 63u,
                             db = (dv & 31u) * 255u / 31u;
                    r = mix(r, dr, a);
                    g = mix(g, dg, a);
                    b = mix(b, db, a);
                }
                *d = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            } else {
                uint32_t *d = (uint32_t *)line + x + i;
                uint32_t oa = 255u;
                if (a < 255u) {
                    uint32_t dv = *d;
                    uint32_t da = s->format == GPU2D_FMT_ARGB8888 ? dv >> 24 : 255u;
                    /* this over what is there: the result's alpha, the colours in its share */
                    oa = a + (da * (255u - a) + 127u) / 255u;
                    uint32_t dk = oa ? (da * (255u - a) + 127u) / 255u : 0u;
                    if (oa) {
                        r = (r * a + ((dv >> 16) & 255u) * dk + oa / 2u) / oa;
                        g = (g * a + ((dv >> 8) & 255u) * dk + oa / 2u) / oa;
                        b = (b * a + (dv & 255u) * dk + oa / 2u) / oa;
                    }
                }
                *d = (oa << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

/* ---- icons ---- */

struct gfx_image *gfx_icon_load(const char *name, int size)
{
    size_t n = name ? strlen(name) : 0;
    if (!n || n > 32 || strchr(name, '/') || name[0] == '.' || size <= 0) {
        errno = EINVAL;
        return NULL;
    }
    char path[sizeof(GFX_ICON_DIR) + 40];
    memcpy(path, GFX_ICON_DIR "/", sizeof(GFX_ICON_DIR));
    memcpy(path + sizeof(GFX_ICON_DIR), name, n);
    memcpy(path + sizeof(GFX_ICON_DIR) + n, ".pam", 5);
    struct gfx_image *img = gfx_image_load(path);
    if (!img || (img->w == size && img->h == size))
        return img;
    struct gfx_image *out = gfx_image_scale(img, size, size);
    free(img);
    return out;
}
