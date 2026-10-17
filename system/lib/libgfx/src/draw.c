/*
 * draw.c - software drawing on RGB565 / ARGB8888 surfaces (clipped), GFXfont text.
 *
 * Drawing writes the colour as it is (into an ARGB surface with its alpha, which gfxd blends);
 * only gfx_blend_pixel() and the smooth corners of gfx_round_rect_aa() mix with what is there.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"

static inline uint16_t to565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

static inline void *row(const struct gfx_surface *s, int y)
{
    return (uint8_t *)s->pix + (size_t)y * (size_t)s->stride;
}

void gfx_fill(const struct gfx_surface *s, int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x;
    if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0)
        return;
    if (s->format == GPU2D_FMT_RGB565) {
        uint16_t v = to565(c);
        uint32_t pair = (uint32_t)v | ((uint32_t)v << 16);
        for (int j = 0; j < h; j++) {
            uint16_t *p = (uint16_t *)row(s, y + j) + x;
            int n = w;
            if (((uintptr_t)p & 2u) && n) {
                *p++ = v;
                n--;
            }
            uint32_t *q = (uint32_t *)p;
            for (; n >= 2; n -= 2)
                *q++ = pair;
            if (n)
                *(uint16_t *)q = v;
        }
    } else {
        for (int j = 0; j < h; j++) {
            uint32_t *p = (uint32_t *)row(s, y + j) + x;
            for (int i = 0; i < w; i++)
                p[i] = c;
        }
    }
}

void gfx_pixel(const struct gfx_surface *s, int x, int y, uint32_t c)
{
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h)
        return;
    if (s->format == GPU2D_FMT_RGB565)
        ((uint16_t *)row(s, y))[x] = to565(c);
    else
        ((uint32_t *)row(s, y))[x] = c;
}

void gfx_hline(const struct gfx_surface *s, int x, int y, int w, uint32_t c)
{
    gfx_fill(s, x, y, w, 1, c);
}

void gfx_vline(const struct gfx_surface *s, int x, int y, int h, uint32_t c)
{
    gfx_fill(s, x, y, 1, h, c);
}

void gfx_rect(const struct gfx_surface *s, int x, int y, int w, int h, uint32_t c)
{
    if (w <= 0 || h <= 0)
        return;
    gfx_hline(s, x, y, w, c);
    gfx_hline(s, x, y + h - 1, w, c);
    gfx_vline(s, x, y, h, c);
    gfx_vline(s, x + w - 1, y, h, c);
}

void gfx_line(const struct gfx_surface *s, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        gfx_pixel(s, x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_circle(const struct gfx_surface *s, int cx, int cy, int r, uint32_t c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_pixel(s, cx + x, cy + y, c);
        gfx_pixel(s, cx - x, cy + y, c);
        gfx_pixel(s, cx + x, cy - y, c);
        gfx_pixel(s, cx - x, cy - y, c);
        gfx_pixel(s, cx + y, cy + x, c);
        gfx_pixel(s, cx - y, cy + x, c);
        gfx_pixel(s, cx + y, cy - x, c);
        gfx_pixel(s, cx - y, cy - x, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_fill_circle(const struct gfx_surface *s, int cx, int cy, int r, uint32_t c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        gfx_hline(s, cx - x, cy + y, 2 * x + 1, c);
        gfx_hline(s, cx - x, cy - y, 2 * x + 1, c);
        gfx_hline(s, cx - y, cy + x, 2 * y + 1, c);
        gfx_hline(s, cx - y, cy - x, 2 * y + 1, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_round_rect(const struct gfx_surface *s, int x, int y, int w, int h, int r, uint32_t c, bool fill)
{
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    /* corners: quarter circles by the midpoint method */
    int px = r, py = 0, err = 1 - r;
    while (px >= py) {
        int ox[2] = { px, py }, oy[2] = { py, px };
        for (int k = 0; k < 2; k++) {
            int l = x + r - ox[k], rr = x + w - 1 - r + ox[k];
            int t = y + r - oy[k], b = y + h - 1 - r + oy[k];
            if (fill) {
                gfx_hline(s, l, t, rr - l + 1, c);
                gfx_hline(s, l, b, rr - l + 1, c);
            } else {
                gfx_pixel(s, l, t, c);
                gfx_pixel(s, rr, t, c);
                gfx_pixel(s, l, b, c);
                gfx_pixel(s, rr, b, c);
            }
        }
        py++;
        if (err < 0) {
            err += 2 * py + 1;
        } else {
            px--;
            err += 2 * (py - px) + 1;
        }
    }
    if (fill) {
        gfx_fill(s, x, y + r, w, h - 2 * r, c);
    } else {
        gfx_hline(s, x + r, y, w - 2 * r, c);
        gfx_hline(s, x + r, y + h - 1, w - 2 * r, c);
        gfx_vline(s, x, y + r, h - 2 * r, c);
        gfx_vline(s, x + w - 1, y + r, h - 2 * r, c);
    }
}

/* ---- blending and smooth corners ------------------------------------------------------------- */

void gfx_blend_pixel(const struct gfx_surface *s, int x, int y, uint32_t c, unsigned cover)
{
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h || !cover)
        return;
    unsigned a = ((c >> 24) * (cover > 255u ? 255u : cover) + 127u) / 255u; /* (its own alpha too) */
    if (s->format == GPU2D_FMT_RGB565) {
        uint16_t *p = (uint16_t *)row(s, y) + x;
        if (a >= 255u) {
            *p = to565(c);
            return;
        }
        unsigned d = *p, na = 255u - a;
        unsigned r = (((c >> 19) & 31u) * a + ((d >> 11) & 31u) * na + 127u) / 255u;
        unsigned g = (((c >> 10) & 63u) * a + ((d >> 5) & 63u) * na + 127u) / 255u;
        unsigned b = (((c >> 3) & 31u) * a + (d & 31u) * na + 127u) / 255u;
        *p = (uint16_t)((r << 11) | (g << 5) | b);
        return;
    }
    /* ARGB, not premultiplied: the colour over what is there ("over") */
    uint32_t *p = (uint32_t *)row(s, y) + x;
    uint32_t d = *p;
    unsigned da = (d >> 24) * (255u - a) / 255u, oa = a + da;
    if (!oa) {
        *p = 0;
        return;
    }
    uint32_t o = (uint32_t)oa << 24;
    for (int sh = 0; sh < 24; sh += 8)
        o |= ((((c >> sh) & 255u) * a + ((d >> sh) & 255u) * da) / oa) << sh;
    *p = o;
}

/* The r x r corner square at (x0, y0) of a circle about (cx, cy): each pixel by how much of it
 * lies inside (filled) or within @line of the edge */
static void corner(const struct gfx_surface *s, int x0, int y0, int cx, int cy, int r, uint32_t c, int line)
{
    for (int j = 0; j < r; j++)
        for (int i = 0; i < r; i++) {
            float dx = (float)(x0 + i - cx) + 0.5f, dy = (float)(y0 + j - cy) + 0.5f;
            float d = sqrtf(dx * dx + dy * dy);
            float in = (float)r - d + 0.5f;
            float cov = in <= 0.0f ? 0.0f : in >= 1.0f ? 1.0f : in;
            if (line) {
                float inner = (float)(r - line) - d + 0.5f;
                cov -= inner <= 0.0f ? 0.0f : inner >= 1.0f ? 1.0f : inner;
            }
            if (cov > 0.0f)
                gfx_blend_pixel(s, x0 + i, y0 + j, c, (unsigned)(cov * 255.0f + 0.5f));
        }
}

void gfx_round_rect_aa(const struct gfx_surface *s, int x, int y, int w, int h, int r, uint32_t c, int line,
                       unsigned corners)
{
    if (w <= 0 || h <= 0)
        return;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    if (r < 0)
        r = 0;
    int tl = corners & GFX_CORNER_TL ? r : 0, tr = corners & GFX_CORNER_TR ? r : 0;
    int bl = corners & GFX_CORNER_BL ? r : 0, br = corners & GFX_CORNER_BR ? r : 0;
    if (line) { /* the straight edges, then the arcs */
        gfx_fill(s, x + tl, y, w - tl - tr, line, c);
        gfx_fill(s, x + bl, y + h - line, w - bl - br, line, c);
        gfx_fill(s, x, y + tl, line, h - tl - bl, c);
        gfx_fill(s, x + w - line, y + tr, line, h - tr - br, c);
    } else { /* all but the round corners' squares */
        gfx_fill(s, x + tl, y, w - tl - tr, r, c);
        gfx_fill(s, x, y + r, w, h - 2 * r, c);
        gfx_fill(s, x + bl, y + h - r, w - bl - br, r, c);
        if (!tl)
            gfx_fill(s, x, y, r, r, c);
        if (!tr)
            gfx_fill(s, x + w - r, y, r, r, c);
        if (!bl)
            gfx_fill(s, x, y + h - r, r, r, c);
        if (!br)
            gfx_fill(s, x + w - r, y + h - r, r, r, c);
    }
    if (tl)
        corner(s, x, y, x + r, y + r, r, c, line);
    if (tr)
        corner(s, x + w - r, y, x + w - r, y + r, r, c, line);
    if (bl)
        corner(s, x, y + h - r, x + r, y + h - r, r, c, line);
    if (br)
        corner(s, x + w - r, y + h - r, x + w - r, y + h - r, r, c, line);
}

/* ---- text ------------------------------------------------------------------------------------ */

static const GFXglyph *glyph(const GFXfont *f, unsigned ch)
{
    if (ch < f->first || ch > f->last)
        ch = '?';
    return &f->glyph[ch - f->first];
}

int gfx_text(const struct gfx_surface *s, const GFXfont *f, int x, int y, const char *text, uint32_t c)
{
    for (; *text; text++) {
        unsigned ch = (unsigned char)*text;
        if (ch == '\n') {
            y += f->yAdvance;
            continue;
        }
        const GFXglyph *g = glyph(f, ch);
        const uint8_t *bits = f->bitmap + g->bitmapOffset;
        int gx = x + g->xOffset, gy = y + g->yOffset;
        unsigned bit = 0;
        uint8_t byte = 0;
        for (int j = 0; j < g->height; j++) {
            int run = -1; /* draw horizontal runs: fewer calls */
            for (int i = 0; i < g->width; i++) {
                if (!(bit++ & 7u))
                    byte = *bits++;
                bool on = byte & 0x80u;
                byte <<= 1;
                if (on && run < 0)
                    run = i;
                if (!on && run >= 0) {
                    gfx_hline(s, gx + run, gy + j, i - run, c);
                    run = -1;
                }
            }
            if (run >= 0)
                gfx_hline(s, gx + run, gy + j, g->width - run, c);
        }
        x += g->xAdvance;
    }
    return x;
}

int gfx_text_width(const GFXfont *f, const char *text)
{
    int w = 0;
    for (; *text; text++)
        w += glyph(f, (unsigned char)*text)->xAdvance;
    return w;
}

int gfx_font_ascent(const GFXfont *f)
{
    return -glyph(f, 'A')->yOffset;
}
