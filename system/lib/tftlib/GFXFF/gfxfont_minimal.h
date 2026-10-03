/*
 * Minimal font header - only includes fonts actually used by the display module
 * This reduces the binary size by ~700KB compared to including all fonts
 */
#ifndef _GFXFONT_MINIMAL_H_
#define _GFXFONT_MINIMAL_H_

typedef struct __attribute__((packed)) { // Data stored PER GLYPH
	uint16_t bitmapOffset;     // Offset into GFXfont->bitmap (NOT uint32_t!)
	uint8_t  width, height;    // Bitmap dimensions in pixels
	uint8_t  xAdvance;         // Distance to advance cursor (x axis)
	int8_t   xOffset, yOffset; // Dist from cursor pos to UL corner
} GFXglyph;

typedef struct { // Data stored for FONT AS A WHOLE:
	uint8_t  *bitmap;      // Glyph bitmaps, concatenated
	GFXglyph *glyph;       // Glyph array
	uint16_t  first, last; // ASCII extents
	uint8_t   yAdvance;    // Newline distance (y axis)
} GFXfont;

/* Only include the fonts we actually use */
#include <GFXFF/TomThumb.h>           // Tiny 5x6 font
#include <GFXFF/FreeMono9pt7b.h>      // Monospace 9pt
#include <GFXFF/FreeMono12pt7b.h>     // Monospace 12pt
#include <GFXFF/FreeSans9pt7b.h>      // Sans-serif 9pt
#include <GFXFF/FreeSans12pt7b.h>     // Sans-serif 12pt
#include <GFXFF/FreeSansBold9pt7b.h>  // Sans-serif bold 9pt
#include <GFXFF/FreeSansBold12pt7b.h> // Sans-serif bold 12pt

#endif // _GFXFONT_MINIMAL_H_
