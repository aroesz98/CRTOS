/*
 * gfx.h - graphics for programs: windows on the graphics server (gfxd) and drawing.
 *
 *     struct gfx *g = gfx_open();
 *     struct gfx_win *w = gfx_win_create(g, -1, -1, 200, 120, 0, "demo");
 *     gfx_fill(&w->s, 0, 0, 200, 120, GFX_RGB(0, 64, 128));
 *     gfx_text(&w->s, &gfx_sans9, 10, 30, "hello", GFX_WHITE);
 *     gfx_present(w);                      // shown at the next frame
 *     struct gfx_event ev;
 *     while (gfx_next_event(g, &ev, CRTOS_FOREVER) == 0) { ... }
 *
 * Drawing works on any struct gfx_surface (RGB565 or ARGB8888); colours are 0xAARRGGBB.
 */
#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <crtos/gpu2d.h>
#include "gfx_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- surfaces and drawing ------------------------------------------------------------------ */

struct gfx_surface {
    void *pix;
    int w, h;
    int stride;                 /* bytes per line */
    uint32_t format;            /* GPU2D_FMT_RGB565 / GPU2D_FMT_ARGB8888 */
};

#define GFX_ARGB(a, r, g, b) (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#define GFX_RGB(r, g, b)     GFX_ARGB(255, r, g, b)
#define GFX_BLACK            GFX_RGB(0, 0, 0)
#define GFX_WHITE            GFX_RGB(255, 255, 255)
#define GFX_TRANSPARENT      0u

/* Adafruit GFX font format */
typedef struct {
    uint16_t bitmapOffset;
    uint8_t width, height;
    uint8_t xAdvance;
    int8_t xOffset, yOffset;
} GFXglyph;

typedef struct {
    uint8_t *bitmap;
    GFXglyph *glyph;
    uint16_t first, last;
    uint8_t yAdvance;
} GFXfont;

extern const GFXfont gfx_sans9, gfx_sans_bold9, gfx_sans12, gfx_sans_bold12, gfx_mono9, gfx_tiny;
/* small and crisp (hinted for the pixel grid): 11 px text, 10 px monospaced (6 x 13 cells) */
extern const GFXfont gfx_dejavu11, gfx_dejavu_bold11, gfx_dejavu_mono10;

void gfx_fill(const struct gfx_surface *s, int x, int y, int w, int h, uint32_t color);
void gfx_pixel(const struct gfx_surface *s, int x, int y, uint32_t color);
void gfx_hline(const struct gfx_surface *s, int x, int y, int w, uint32_t color);
void gfx_vline(const struct gfx_surface *s, int x, int y, int h, uint32_t color);
void gfx_rect(const struct gfx_surface *s, int x, int y, int w, int h, uint32_t color);
void gfx_line(const struct gfx_surface *s, int x0, int y0, int x1, int y1, uint32_t color);
void gfx_circle(const struct gfx_surface *s, int cx, int cy, int r, uint32_t color);
void gfx_fill_circle(const struct gfx_surface *s, int cx, int cy, int r, uint32_t color);
void gfx_round_rect(const struct gfx_surface *s, int x, int y, int w, int h, int r, uint32_t color, bool fill);
/* The same with smooth corners: filled (@line 0) or its outline @line pixels wide, only the
 * corners in @corners rounded (GFX_CORNER_*). The corner pixels are blended over what the
 * surface has (into an ARGB surface: alpha too, so a transparent surface gets soft corners). */
#define GFX_CORNER_TL   0x1u
#define GFX_CORNER_TR   0x2u
#define GFX_CORNER_BL   0x4u
#define GFX_CORNER_BR   0x8u
#define GFX_CORNERS_TOP (GFX_CORNER_TL | GFX_CORNER_TR)
#define GFX_CORNERS_ALL 0xFu
void gfx_round_rect_aa(const struct gfx_surface *s, int x, int y, int w, int h, int r, uint32_t color, int line,
                       unsigned corners);
/* @color over the pixel at (x, y), @cover of 255 of it (blended; clipped) */
void gfx_blend_pixel(const struct gfx_surface *s, int x, int y, uint32_t color, unsigned cover);
/* y is the baseline; returns the x after the text */
int gfx_text(const struct gfx_surface *s, const GFXfont *f, int x, int y, const char *text, uint32_t color);
int gfx_text_width(const GFXfont *f, const char *text);
int gfx_font_ascent(const GFXfont *f);     /* pixels above the baseline of 'A' */
/* A font file (tools/fontconv.py makes them: .fnt) in memory - one block, free() frees it -
 * or NULL (errno) */
GFXfont *gfx_font_load(const char *path);

/* ---- pictures and icons -------------------------------------------------------------------- */

/* A picture in memory: w x h pixels 0xAARRGGBB (alpha not premultiplied), row after row; one
 * block of memory, freed with free() */
struct gfx_image {
    int w, h;
    uint32_t *pix;
};

/* A netpbm file: PAM (P7; 8 bits with 1-4 channels: grey, grey + alpha, RGB, RGB_ALPHA) or
 * PPM (P6, 8 bits). NULL (errno) when it cannot be read. */
struct gfx_image *gfx_image_load(const char *path);
/* A copy at w x h, each pixel the average of the area it covers (smooth when shrinking) */
struct gfx_image *gfx_image_scale(const struct gfx_image *img, int w, int h);
/* @img with its top left corner at (x, y), blended by its alpha (clipped) */
void gfx_image_draw(const struct gfx_surface *s, int x, int y, const struct gfx_image *img);

/* Icons of programs: <GFX_ICON_DIR>/<name>.pam, name the program's file name without ".app"
 * (as ps shows it); "application" is the one for programs without their own */
#define GFX_ICON_DIR "/sd/crtos/share/icons"
#define GFX_ICON_DEFAULT "application"
/* The icon of @name at size x size (scaled), or NULL (errno ENOENT: it has none) */
struct gfx_image *gfx_icon_load(const char *name, int size);

/* ---- wallpapers ------------------------------------------------------------------------------- */

#define GFX_WALLPAPER_DIR "/sd/crtos/share/wallpapers"
/* how a picture covers a surface of another shape */
#define GFX_FIT_FILL    0       /* the whole surface, cut at two sides (proportions kept) */
#define GFX_FIT_FIT     1       /* all of the picture, bars at two sides */
#define GFX_FIT_STRETCH 2       /* the whole surface, proportions not kept */
#define GFX_FIT_CENTER  3       /* its own size, in the middle */
#define GFX_FIT_TILE    4       /* its own size, repeated */
#define GFX_FITS        5

/* Built-in wallpaper @i (from 0): its name, NULL after the last */
const char *gfx_wallpaper_builtin(int i);
/* Wallpaper @spec over all of @s: a built-in one's name, "color:RRGGBB", or a picture (PPM, PAM;
 * scaled smoothly as @fit says). 0, or -1 (errno) when it cannot be drawn - then the first
 * built-in one is. Pictures are read row by row: any size, little memory. */
int gfx_wallpaper_draw(const struct gfx_surface *s, const char *spec, int fit);

/* ---- the graphics server ------------------------------------------------------------------ */

struct gfx;

struct gfx_win {
    struct gfx *g;
    int id;
    int shm;                    /* handle of the pixels */
    uint32_t offset;            /* of the pixels in it */
    bool pooled;                /* carved out of the program's pool */
    struct gfx_surface s;
    int x, y;                   /* on the screen (as created) */
    uint32_t flags;
    bool frame_pending;         /* damage sent, frame not shown yet */
    /* gfx_win_resize(): the new pixels are drawn first and handed to gfxd with the next
     * damage; until then gfxd keeps showing these */
    bool resize_pending;
    bool old_pooled;
    int old_shm;
    uint32_t old_offset;
    void *old_pix;
    int old_w, old_h, old_stride;
};

struct gfx *gfx_open(void);                  /* NULL if gfxd does not answer (errno) */
void gfx_close(struct gfx *g);
/* When gfxd ends, the program ends too (exit code 1); @fn, if set, runs first */
void gfx_on_server_lost(void (*fn)(void));
int gfx_screen_width(const struct gfx *g);
int gfx_screen_height(const struct gfx *g);
int gfx_event_handle(const struct gfx *g);   /* for poll(): POLLIN when an event waits */

/* Shared memory for the pixels of the windows created from now on, as far as they fit
 * (a process can map only three shared memory objects at a time) */
int gfx_pool_reserve(struct gfx *g, size_t bytes);

/* x < 0: placed by gfxd or the window manager. flags: GFX_WIN_* (GFX_WIN_ALPHA for an
 * ARGB8888 surface). */
struct gfx_win *gfx_win_create(struct gfx *g, int x, int y, int w, int h, uint32_t flags, const char *title);
void gfx_win_destroy(struct gfx_win *w);
struct gfx_win *gfx_win_by_id(struct gfx *g, int id);   /* one of ours */
void gfx_win_set_title(struct gfx_win *w, const char *title);
/* New pixels of another size in w->s (contents undefined: draw them all); gfxd switches to
 * them with the next gfx_damage() / gfx_present(), so the new size shows up complete. Until
 * then the old pixels stay readable (old_pix, old_w, old_h, old_stride). The answer to
 * GFX_EV_CONFIGURE (for windows created with GFX_WIN_RESIZABLE).
 * Returns 0: new pixels, 1: the window has that size already (nothing to do), -1: error. */
int gfx_win_resize(struct gfx_win *w, int width, int height);
void gfx_damage(struct gfx_win *w, int x, int y, int width, int height);
void gfx_present(struct gfx_win *w);         /* damage the whole window */
void gfx_win_move(struct gfx_win *w, int x, int y);
void gfx_win_raise(struct gfx_win *w);
void gfx_win_show(struct gfx_win *w, bool visible);

/* Next event (queued ones first): 0, or -1 on timeout/error */
int gfx_next_event(struct gfx *g, struct gfx_event *ev, uint32_t timeout);
/* Wait until the last damage of @w is on the screen (other events stay queued) */
int gfx_wait_frame(struct gfx_win *w, uint32_t timeout);
int gfx_stats(struct gfx *g, struct gfx_stats *st);

/* Window manager side (see gfx_proto.h); ids are those of GFX_EV_WM_* events */
int gfx_wm_register(struct gfx *g);          /* 0, or -1 (errno EBUSY: another one runs) */
void gfx_wm_attach(struct gfx *g, int id, const struct gfx_win *frame, int dx, int dy);
void gfx_wm_close(struct gfx *g, int id);    /* sends the owner GFX_EV_CLOSE */
void gfx_wm_focus(struct gfx *g, int id);    /* raise it and give it the keyboard */
void gfx_wm_configure(struct gfx *g, int id, int width, int height);  /* ask its owner for a size */
void gfx_wm_keys(struct gfx *g, bool all);   /* every key to the manager (its menu is open) or not */

/* The appearance settings (gfx_ui.h, /sd/crtos/etc/ui.cfg) changed: gfxd draws the wallpaper
 * anew and every program gets GFX_EV_SETTINGS */
void gfx_settings_changed(struct gfx *g);

/* ---- clipboard: one text for all programs, kept by gfxd -------------------------------------- */

/* Make @text (@len bytes, at most GFX_CLIP_MAX) the clipboard: 0, or -1 (errno) */
int gfx_clip_set(struct gfx *g, const char *text, size_t len);
/* The clipboard into @buf (at most @size - 1 bytes and a 0): the text's whole length (more than
 * fits: cut), or -1 (errno) */
int gfx_clip_get(struct gfx *g, char *buf, size_t size);
void gfx_clip_watch(struct gfx *g);          /* GFX_EV_CLIP for every new text from now on */

/* ---- input and the screen for remote control ------------------------------------------------- */

/* Input as if from a device (struct gfx_input: kind, code, x, y, value) */
int gfx_send_input(struct gfx *g, uint16_t kind, uint16_t code, int x, int y, int32_t value);
/* A copy of the screen into @shm (the screen's size in RGB565, @stride bytes a line; needs
 * CAP_SYS): gfxd keeps it up to date and sends GFX_EV_SCREEN when it changed; 0 or -1 (errno) */
int gfx_screen_watch(struct gfx *g, int shm, uint32_t stride);
/* Where the copy changed since the last take: up to GFX_SCREEN_RECTS rectangles, their number */
int gfx_screen_take(struct gfx *g, struct gfx_screen_rect *r);

#ifdef __cplusplus
}
#endif

#endif
