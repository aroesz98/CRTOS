/*
 * nes_draw.h - drawing in the NES program's retro look on its RGB565 window: the 8 x 8 font
 * (nes_font.c) at whole-number scales, framed boxes, darkened backgrounds.
 */
#ifndef NES_DRAW_H
#define NES_DRAW_H

#include <stdint.h>
#include "gfx.h"

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xf8) << 8) | (((g) & 0xfc) << 3) | ((b) >> 3)))

/* colours of the NES's own palette */
#define C_BLACK   RGB565(0, 0, 0)
#define C_WHITE   RGB565(252, 252, 252)
#define C_GREY    RGB565(188, 188, 188)
#define C_DARK    RGB565(116, 116, 116)
#define C_NAVY    RGB565(0, 0, 168)
#define C_BLUE    RGB565(0, 88, 248)
#define C_SKY     RGB565(60, 188, 252)
#define C_RED     RGB565(248, 56, 0)
#define C_GOLD    RGB565(248, 184, 0)
#define C_GREEN   RGB565(88, 216, 84)
#define C_PINK    RGB565(248, 120, 248)

void draw_fill(const struct gfx_surface *s, int x, int y, int w, int h, uint16_t c);
/* One character (ASCII or GLYPH_*) with its top left corner at x, y, each font pixel
 * scale x scale; the background stays */
void draw_char(const struct gfx_surface *s, int x, int y, unsigned ch, uint16_t c, int scale);
/* Returns the x after the text */
int draw_text(const struct gfx_surface *s, int x, int y, const char *t, uint16_t c, int scale);
/* the text with a one pixel shadow down and right */
int draw_text_shadow(const struct gfx_surface *s, int x, int y, const char *t, uint16_t c, int scale);
int text_width(const char *t, int scale);
/* A box with a double line border (outer c, inner c) filled with fill */
void draw_frame(const struct gfx_surface *s, int x, int y, int w, int h, uint16_t c, uint16_t fill);
/* Halves the brightness of the area (behind a menu) */
void draw_dim(const struct gfx_surface *s, int x, int y, int w, int h);

#endif
