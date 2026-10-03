/*
 * snes_draw.c - the retro drawing of the SNES program (snes_draw.h).
 */
#include <string.h>

#include "snes_draw.h"
#include "snes_font.h"

static inline uint16_t *pixel_at(const struct gfx_surface *s, int x, int y)
{
    return (uint16_t *)((uint8_t *)s->pix + y * s->stride) + x;
}

/* clips the rectangle to the surface; false if nothing is left */
static int clip(const struct gfx_surface *s, int *x, int *y, int *w, int *h)
{
    if (*x < 0)
    {
        *w += *x;
        *x = 0;
    }
    if (*y < 0)
    {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > s->w)
        *w = s->w - *x;
    if (*y + *h > s->h)
        *h = s->h - *y;
    return *w > 0 && *h > 0;
}

void draw_fill(const struct gfx_surface *s, int x, int y, int w, int h, uint16_t c)
{
    if (!clip(s, &x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++)
    {
        uint16_t *p = pixel_at(s, x, y + j);
        for (int i = 0; i < w; i++)
            p[i] = c;
    }
}

void draw_char(const struct gfx_surface *s, int x, int y, unsigned ch, uint16_t c, int scale)
{
    const uint8_t *g = font_glyph(ch);
    for (int row = 0; row < 8; row++)
    {
        uint8_t bits = g[row];
        for (int col = 0; bits; col++, bits >>= 1)
            if (bits & 1)
            {
                if (scale == 1)
                {
                    int px = x + col, py = y + row;
                    if (px >= 0 && py >= 0 && px < s->w && py < s->h)
                        *pixel_at(s, px, py) = c;
                }
                else
                {
                    draw_fill(s, x + col * scale, y + row * scale, scale, scale, c);
                }
            }
    }
}

int draw_text(const struct gfx_surface *s, int x, int y, const char *t, uint16_t c, int scale)
{
    for (; *t; t++, x += 8 * scale)
        draw_char(s, x, y, (uint8_t)*t, c, scale);
    return x;
}

int draw_text_shadow(const struct gfx_surface *s, int x, int y, const char *t, uint16_t c, int scale)
{
    draw_text(s, x + scale, y + scale, t, C_BLACK, scale);
    return draw_text(s, x, y, t, c, scale);
}

int text_width(const char *t, int scale)
{
    return (int)strlen(t) * 8 * scale;
}

void draw_frame(const struct gfx_surface *s, int x, int y, int w, int h, uint16_t c, uint16_t fill)
{
    draw_fill(s, x, y, w, h, fill);
    /* outer line, a gap, inner line: the frames of the 8-bit menus */
    draw_fill(s, x, y, w, 1, c);
    draw_fill(s, x, y + h - 1, w, 1, c);
    draw_fill(s, x, y, 1, h, c);
    draw_fill(s, x + w - 1, y, 1, h, c);
    draw_fill(s, x + 2, y + 2, w - 4, 1, c);
    draw_fill(s, x + 2, y + h - 3, w - 4, 1, c);
    draw_fill(s, x + 2, y + 2, 1, h - 4, c);
    draw_fill(s, x + w - 3, y + 2, 1, h - 4, c);
}

void draw_dim(const struct gfx_surface *s, int x, int y, int w, int h)
{
    if (!clip(s, &x, &y, &w, &h))
        return;
    for (int j = 0; j < h; j++)
    {
        uint16_t *p = pixel_at(s, x, y + j);
        for (int i = 0; i < w; i++)
            p[i] = (uint16_t)((p[i] >> 1) & 0x7BEF);
    }
}
