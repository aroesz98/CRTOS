/*
 * fonts.c - bitmap fonts (Adafruit GFX format, generated from GNU FreeFont; TomThumb; DejaVu
 * by tools/fontconv.py), and font files loaded at run time (the interface's fonts at the sizes
 * of the scales, tools/fonts.py: see tools/fontconv.py for the format).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "gfx.h"

#include "../fonts/FreeSans9pt7b.h"
#include "../fonts/FreeSansBold9pt7b.h"
#include "../fonts/FreeSans12pt7b.h"
#include "../fonts/FreeSansBold12pt7b.h"
#include "../fonts/FreeMono9pt7b.h"
#include "../fonts/TomThumb.h"
#include "../fonts/DejaVuSans11.h"
#include "../fonts/DejaVuSansBold11.h"
#include "../fonts/DejaVuSansMono10.h"

const GFXfont gfx_sans9 = { (uint8_t *)FreeSans9pt7bBitmaps, (GFXglyph *)FreeSans9pt7bGlyphs, 0x20, 0x7E, 22 };
const GFXfont gfx_sans_bold9 = { (uint8_t *)FreeSansBold9pt7bBitmaps, (GFXglyph *)FreeSansBold9pt7bGlyphs, 0x20, 0x7E,
                                 22 };
const GFXfont gfx_sans12 = { (uint8_t *)FreeSans12pt7bBitmaps, (GFXglyph *)FreeSans12pt7bGlyphs, 0x20, 0x7E, 29 };
const GFXfont gfx_sans_bold12 = { (uint8_t *)FreeSansBold12pt7bBitmaps, (GFXglyph *)FreeSansBold12pt7bGlyphs, 0x20,
                                  0x7E, 29 };
const GFXfont gfx_mono9 = { (uint8_t *)FreeMono9pt7bBitmaps, (GFXglyph *)FreeMono9pt7bGlyphs, 0x20, 0x7E, 18 };
const GFXfont gfx_tiny = { (uint8_t *)TomThumbBitmaps, (GFXglyph *)TomThumbGlyphs, 0x20, 0x7E, 6 };

/* ---- font files ------------------------------------------------------------------------------- */

#define FNT_HEAD 16
#define FNT_GLYPH 8

static uint32_t le(const uint8_t *p, int n)
{
    uint32_t v = 0;
    for (int i = n - 1; i >= 0; i--)
        v = (v << 8) | p[i];
    return v;
}

static bool read_all(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        int r = (int)read(fd, p, len);
        if (r <= 0)
            return false;
        p += r;
        len -= (size_t)r;
    }
    return true;
}

GFXfont *gfx_font_load(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return NULL;
    uint8_t h[FNT_HEAD];
    GFXfont *f = NULL;
    if (!read_all(fd, h, sizeof(h)) || memcmp(h, "CRF1", 4))
        goto bad;
    unsigned first = h[4], last = h[5], line = h[6], n = le(h + 8, 2);
    uint32_t bits = le(h + 12, 4);
    if (last < first || n != last - first + 1 || bits > 0xFFFFu)
        goto bad;
    /* one block: the font, its glyphs, its bitmaps; the glyph records are read behind them first */
    size_t head = sizeof(GFXfont) + n * sizeof(GFXglyph);
    f = (GFXfont *)malloc(head + bits + n * FNT_GLYPH);
    if (!f) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }
    GFXglyph *g = (GFXglyph *)(f + 1);
    uint8_t *bitmap = (uint8_t *)f + head, *rec = bitmap + bits;
    if (!read_all(fd, rec, n * FNT_GLYPH) || !read_all(fd, bitmap, bits))
        goto bad;
    for (unsigned i = 0; i < n; i++, rec += FNT_GLYPH) {
        g[i].bitmapOffset = (uint16_t)le(rec, 2);
        g[i].width = rec[2];
        g[i].height = rec[3];
        g[i].xAdvance = rec[4];
        g[i].xOffset = (int8_t)rec[5];
        g[i].yOffset = (int8_t)rec[6];
        if ((uint32_t)g[i].bitmapOffset + ((uint32_t)g[i].width * g[i].height + 7u) / 8u > bits)
            goto bad; /* (a glyph outside the bitmaps) */
    }
    f->bitmap = bitmap;
    f->glyph = g;
    f->first = (uint16_t)first;
    f->last = (uint16_t)last;
    f->yAdvance = (uint8_t)line;
    close(fd);
    return f;
bad:
    free(f);
    close(fd);
    errno = EINVAL;
    return NULL;
}
