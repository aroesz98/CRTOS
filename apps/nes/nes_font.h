/*
 * nes_font.h - the 8 x 8 retro font of the NES program and its drawing on RGB565 windows.
 */
#ifndef NES_FONT_H
#define NES_FONT_H

#include <stdint.h>

/* characters beyond ASCII, for use in strings as "\x80" etc. */
enum {
    GLYPH_FOLDER = 0x80,
    GLYPH_CART,
    GLYPH_CURSOR,
    GLYPH_UP,
    GLYPH_CROSS,
    GLYPH_CIRCLE,
    GLYPH_SQUARE,
    GLYPH_TRIANGLE,
    GLYPH_DPAD,
    GLYPH_BLOCK,
    GLYPH_COUNT
};

/* 8 rows, bit 0 = the leftmost pixel */
const uint8_t *font_glyph(unsigned c);

#endif
