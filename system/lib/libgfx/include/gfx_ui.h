/*
 * gfx_ui.h - a few widgets for touch programs: a theme, buttons, switches, sliders, a scrolling
 * list and an on-screen keyboard; the appearance settings the theme follows.
 *
 * Widgets draw into a surface and take pointer events in the same coordinates; the program
 * owns the layout, calls the draw functions and damages what they report as changed:
 *
 *     struct ui_list l = { .r = { 0, 0, 200, 150 }, .row_h = 20, .count = n, .draw_row = row };
 *     ui_list_draw(&l, s);
 *     ...
 *     int r = ui_list_pointer(&l, ev.kind, ev.x, ev.y);
 *     if (r != UI_NONE) { ui_list_draw(&l, s); gfx_damage(...); }
 *     if (r == UI_ACTIVATED) open(l.sel);
 *
 * The theme follows the appearance settings (Settings > Appearance, /sd/crtos/etc/ui.cfg):
 * the interface scale, the font family, the accent colour, transparency and rounded corners.
 * libgfx reads them when the program opens gfxd and again on GFX_EV_SETTINGS (the user changed
 * them): ui_theme then has the fonts for the scale, and ui_px() turns sizes meant for 100 %
 * into the scale's. A program that lays out with ui_px() and the theme's fonts, and does it
 * again on GFX_EV_SETTINGS, follows a change at once.
 */
#ifndef GFX_UI_H
#define GFX_UI_H

#include "gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ui_theme {
    uint32_t bg;                /* window background */
    uint32_t panel;             /* bars, list background */
    uint32_t text, text_dim;
    uint32_t accent, accent_text;   /* selection, active title */
    uint32_t button, button_pressed, button_text;
    uint32_t border;
    const GFXfont *font, *bold, *mono;
    const GFXfont *heading;     /* titles of pages and sections (bold, larger) */
    int scale;                  /* interface scale, percent (ui_px) */
    int radius;                 /* corners of buttons, panels and menus; 0: square */
    uint8_t alpha;              /* opacity of translucent surfaces (task bar, menus): 255 opaque */
};

extern const struct ui_theme ui_theme_dark;    /* the colours; 100 %, the built-in fonts */
extern const struct ui_theme *ui_theme;        /* the one the widgets use (follows the settings) */

/* ---- appearance settings ------------------------------------------------------------------------ */

#define UI_CFG          "/sd/crtos/etc/ui.cfg"
#define UI_FONT_DIR     "/sd/crtos/share/fonts"
#define UI_SCALES       5
extern const int ui_scales[UI_SCALES];         /* 100, 125, 150, 175, 200 */

struct ui_settings {
    int scale;                  /* interface scale, percent (one of ui_scales) */
    char font[24];              /* font family: an id of UI_FONT_DIR/families.txt */
    uint32_t accent;            /* accent colour 0xRRGGBB */
    bool transparency;          /* the task bar, menus and tooltips let what is below show through */
    int opacity;                /* how much of it they hide, percent (30 - 100) */
    bool rounded;               /* rounded corners (buttons, menus, title bars) */
    char wallpaper[96];         /* gfx_wallpaper_draw() spec */
    int fit;                    /* GFX_FIT_*: how a picture covers the screen */
};

extern struct ui_settings ui_settings;         /* what ui_theme follows now */
void ui_settings_default(struct ui_settings *s);
/* UI_CFG into @s (missing keys and a missing file: the defaults); 0, or -1 without the file */
int ui_settings_load(struct ui_settings *s);
int ui_settings_save(const struct ui_settings *s);  /* 0, or -1 (errno) */
/* One setting as the file has it (key, value: "scale", "150"); 0, or -1: no such key/value */
int ui_settings_set(struct ui_settings *s, const char *key, const char *value);
/* ui_theme follows @s (fonts for its scale and family, accent, corners, opacity) */
void ui_settings_apply(const struct ui_settings *s);
/* @px meant for 100 % at the current scale (rounded; at least 1 for px > 0) */
int ui_px(int px);
/* The font families (families.txt): up to @max ids and names; their number */
int ui_font_families(char (*ids)[24], char (*names)[32], int max);
/* Family @id's text font at the current scale (to show it), the theme's when it has none */
const GFXfont *ui_family_font(const char *id);

struct ui_rect {
    int x, y, w, h;
};

bool ui_inside(const struct ui_rect *r, int x, int y);

/* Text centred in @r (vertically on the font's capital height) */
void ui_text_center(const struct gfx_surface *s, const struct ui_rect *r, const GFXfont *f, const char *text,
                    uint32_t color);
/* Text cut with "..." to fit @width; returns the end x */
int ui_text_fit(const struct gfx_surface *s, const GFXfont *f, int x, int y, int width, const char *text,
                uint32_t color);

void ui_button(const struct gfx_surface *s, const struct ui_rect *r, const char *text, bool pressed);
/* A button that stays chosen (one of a group: a tab, an option) */
void ui_choice(const struct gfx_surface *s, const struct ui_rect *r, const char *text, bool chosen, bool pressed);
/* A panel (a card, a menu's body): filled, with the theme's corners */
void ui_panel(const struct gfx_surface *s, const struct ui_rect *r, uint32_t color);
/* An on/off switch: a track with a knob, at the right of @r and centred in it */
void ui_switch(const struct gfx_surface *s, const struct ui_rect *r, bool on, bool enabled);
int ui_switch_width(void);                      /* (its height: ui_px(22)) */
/* A slider along @r: @value from 0 to 1000 */
void ui_slider(const struct gfx_surface *s, const struct ui_rect *r, int value, bool enabled);
int ui_slider_value(const struct ui_rect *r, int x);    /* 0 - 1000 at x */

/* Results of the pointer functions */
#define UI_NONE         0       /* nothing to redraw */
#define UI_CHANGED      1       /* redraw the widget */
#define UI_ACTIVATED    2       /* redraw; the selected item was tapped */

/* ---- scrolling list: rows of a fixed height, drag to scroll, tap to pick --------------------- */

struct ui_list {
    struct ui_rect r;
    int row_h;
    int count;
    int top;                    /* scroll position in pixels */
    int sel;                    /* selected row, -1: none */
    void (*draw_row)(const struct gfx_surface *s, const struct ui_rect *r, int row, bool sel, void *ctx);
    void *ctx;
    bool no_bg;                 /* the program draws the background (a translucent menu) */
    /* touch state */
    int down_y, down_top, down_row;
    bool dragging;
};

void ui_list_draw(struct ui_list *l, const struct gfx_surface *s);
/* GFX_PTR_* of a pointer event; GFX_PTR_HOVER (the mouse over a row) selects that row */
int ui_list_pointer(struct ui_list *l, int kind, int x, int y);
void ui_list_show(struct ui_list *l, int row);     /* scroll so that @row is visible */
/* A mouse wheel turned over the list (GFX_EV_WHEEL value: notches, up positive): three rows a
 * notch; UI_CHANGED when it moved */
int ui_list_wheel(struct ui_list *l, int notches);

/* ---- on-screen keyboard ------------------------------------------------------------------------ */

/* Keys besides ASCII (the on-screen keyboard has the arrows; a real keyboard all of them) */
#define UI_KEY_UP       0x100
#define UI_KEY_DOWN     0x101
#define UI_KEY_LEFT     0x102
#define UI_KEY_RIGHT    0x103
#define UI_KEY_HOME     0x104
#define UI_KEY_END      0x105
#define UI_KEY_PGUP     0x106
#define UI_KEY_PGDN     0x107
#define UI_KEY_DELETE   0x108
#define UI_KEY_INSERT   0x109

struct ui_keyboard {
    struct ui_rect r;
    int corner;                 /* pixels kept free at the right end of the last row (a grip) */
    int mode;                   /* 0 letters, 1 shifted, 2 symbols */
    bool ctrl;                  /* the next key is a control key */
    int pressed;                /* key under the finger, -1: none */
};

#define UI_KEYBOARD_ROWS 5
int ui_keyboard_height(int width);                  /* a good height for a width */
void ui_keyboard_draw(struct ui_keyboard *k, const struct gfx_surface *s);
/* Pointer event -> key: a character (Ctrl applied), '\b', '\n', '\t', 27, UI_KEY_*, or -1
 * with *redraw set when only the keyboard's look changed */
int ui_keyboard_pointer(struct ui_keyboard *k, int kind, int x, int y, bool *redraw);

/* ---- keys and characters (US layout) --------------------------------------------------------- */

/* Key events (GFX_EV_KEY: a real keyboard or the system's on-screen keyboard) as text, in
 * the form ui_keyboard_pointer() returns: feed it every key event; a key press gives its
 * character (Shift, Caps Lock and Ctrl applied), '\b', '\n', '\t', 27 or UI_KEY_*; releases,
 * modifier keys and keys without a character give -1.
 *
 *     static struct ui_keys keys;
 *     int c = ui_key_char(&keys, &ev);
 *     if (c >= 0) type(c);
 */
struct ui_keys {
    bool shift, ctrl, caps;
};
int ui_key_char(struct ui_keys *ks, const struct gfx_event *ev);

/* The other way, for on-screen keyboards: the key code (crtos/keys.h) and the modifiers that
 * type @c (a ui_keyboard_pointer() result); 0, or -1 if no key types it */
#define UI_MOD_SHIFT 0x1u
#define UI_MOD_CTRL  0x2u
int ui_char_key(int c, uint16_t *code, unsigned *mods);

#ifdef __cplusplus
}
#endif

#endif
