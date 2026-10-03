/*
 * ui.c - widgets for touch programs (gfx_ui.h). Their sizes come from the rectangles the
 * program gives (laid out with ui_px()), their corners and colours from the theme.
 */
#include <string.h>
#include "gfx_ui.h"

const struct ui_theme ui_theme_dark = {
    .bg = GFX_RGB(28, 32, 40),
    .panel = GFX_RGB(38, 44, 56),
    .text = GFX_RGB(230, 232, 238),
    .text_dim = GFX_RGB(140, 150, 165),
    .accent = GFX_RGB(52, 120, 220),
    .accent_text = GFX_RGB(255, 255, 255),
    .button = GFX_RGB(58, 66, 82),
    .button_pressed = GFX_RGB(52, 120, 220),
    .button_text = GFX_RGB(240, 242, 246),
    .border = GFX_RGB(74, 84, 104),
    .font = &gfx_dejavu11,
    .bold = &gfx_dejavu_bold11,
    .mono = &gfx_dejavu_mono10,
    .heading = &gfx_dejavu_bold11,
    .scale = 100,
    .radius = 4,
    .alpha = 255,
};

const struct ui_theme *ui_theme = &ui_theme_dark;

bool ui_inside(const struct ui_rect *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

/* The part of @s under @r, as a surface of its own: drawing into it is clipped to @r */
static struct gfx_surface sub(const struct gfx_surface *s, const struct ui_rect *r)
{
    struct gfx_surface o = *s;
    int x = r->x < 0 ? 0 : r->x, y = r->y < 0 ? 0 : r->y;
    int x1 = r->x + r->w > s->w ? s->w : r->x + r->w, y1 = r->y + r->h > s->h ? s->h : r->y + r->h;
    int bpp = s->format == GPU2D_FMT_RGB565 ? 2 : 4;
    o.pix = (uint8_t *)s->pix + (size_t)y * (size_t)s->stride + (size_t)x * (size_t)bpp;
    o.w = x1 > x ? x1 - x : 0;
    o.h = y1 > y ? y1 - y : 0;
    return o;
}

void ui_text_center(const struct gfx_surface *s, const struct ui_rect *r, const GFXfont *f, const char *text,
                    uint32_t color)
{
    int tw = gfx_text_width(f, text), asc = gfx_font_ascent(f);
    gfx_text(s, f, r->x + (r->w - tw) / 2, r->y + (r->h + asc) / 2, text, color);
}

int ui_text_fit(const struct gfx_surface *s, const GFXfont *f, int x, int y, int width, const char *text,
                uint32_t color)
{
    if (gfx_text_width(f, text) <= width)
        return gfx_text(s, f, x, y, text, color);
    char buf[128];
    int dots = gfx_text_width(f, "...");
    size_t n = strlen(text);
    if (n > sizeof(buf) - 4)
        n = sizeof(buf) - 4;
    memcpy(buf, text, n);
    for (; n > 0; n--) {
        buf[n] = 0;
        if (gfx_text_width(f, buf) + dots <= width)
            break;
    }
    memcpy(buf + n, "...", 4);
    return gfx_text(s, f, x, y, buf, color);
}

void ui_button(const struct gfx_surface *s, const struct ui_rect *r, const char *text, bool pressed)
{
    const struct ui_theme *t = ui_theme;
    gfx_round_rect_aa(s, r->x, r->y, r->w, r->h, t->radius, pressed ? t->button_pressed : t->button, 0, GFX_CORNERS_ALL);
    gfx_round_rect_aa(s, r->x, r->y, r->w, r->h, t->radius, pressed ? t->accent : t->border, 1, GFX_CORNERS_ALL);
    ui_text_center(s, r, t->font, text, t->button_text);
}

void ui_choice(const struct gfx_surface *s, const struct ui_rect *r, const char *text, bool chosen, bool pressed)
{
    const struct ui_theme *t = ui_theme;
    uint32_t bg = chosen ? t->accent : pressed ? t->border : t->button;
    gfx_round_rect_aa(s, r->x, r->y, r->w, r->h, t->radius, bg, 0, GFX_CORNERS_ALL);
    if (!chosen)
        gfx_round_rect_aa(s, r->x, r->y, r->w, r->h, t->radius, t->border, 1, GFX_CORNERS_ALL);
    ui_text_center(s, r, chosen ? t->bold : t->font, text, chosen ? t->accent_text : t->button_text);
}

void ui_panel(const struct gfx_surface *s, const struct ui_rect *r, uint32_t color)
{
    gfx_round_rect_aa(s, r->x, r->y, r->w, r->h, ui_theme->radius, color, 0, GFX_CORNERS_ALL);
}

int ui_switch_width(void)
{
    return ui_px(40);
}

void ui_switch(const struct gfx_surface *s, const struct ui_rect *r, bool on, bool enabled)
{
    const struct ui_theme *t = ui_theme;
    int w = ui_switch_width(), h = ui_px(22), x = r->x + r->w - w, y = r->y + (r->h - h) / 2;
    int rad = t->radius ? h / 2 : 0, k = h - ui_px(8), kx = on ? x + w - h + ui_px(4) : x + ui_px(4);
    uint32_t track = !enabled ? t->panel : on ? t->accent : t->button;
    gfx_round_rect_aa(s, x, y, w, h, rad, track, 0, GFX_CORNERS_ALL);
    if (!on || !enabled)
        gfx_round_rect_aa(s, x, y, w, h, rad, t->border, 1, GFX_CORNERS_ALL);
    uint32_t knob = !enabled ? t->text_dim : on ? t->accent_text : t->text;
    gfx_round_rect_aa(s, kx, y + ui_px(4), k, k, t->radius ? k / 2 : 0, knob, 0, GFX_CORNERS_ALL);
}

void ui_slider(const struct gfx_surface *s, const struct ui_rect *r, int value, bool enabled)
{
    const struct ui_theme *t = ui_theme;
    int knob = ui_px(18), th = ui_px(4) | 1, x0 = r->x + knob / 2, len = r->w - knob;
    int cy = r->y + r->h / 2, pos = x0 + (int)((long)len * (value < 0 ? 0 : value > 1000 ? 1000 : value) / 1000);
    int rad = t->radius ? th / 2 : 0;
    gfx_round_rect_aa(s, x0, cy - th / 2, len, th, rad, t->button, 0, GFX_CORNERS_ALL);
    gfx_round_rect_aa(s, x0, cy - th / 2, pos - x0 + 1, th, rad, enabled ? t->accent : t->border, 0, GFX_CORNERS_ALL);
    int kr = t->radius ? knob / 2 : ui_px(2);
    gfx_round_rect_aa(s, pos - knob / 2, cy - knob / 2, knob, knob, kr, enabled ? t->text : t->text_dim, 0,
                      GFX_CORNERS_ALL);
    if (enabled)
        gfx_round_rect_aa(s, pos - knob / 2, cy - knob / 2, knob, knob, kr, t->accent, ui_px(2), GFX_CORNERS_ALL);
}

int ui_slider_value(const struct ui_rect *r, int x)
{
    int knob = ui_px(18), len = r->w - knob;
    if (len <= 0)
        return 0;
    int v = (int)((long)(x - r->x - knob / 2) * 1000 / len);
    return v < 0 ? 0 : v > 1000 ? 1000 : v;
}

/* ---- list ------------------------------------------------------------------------------------- */

static int list_max_top(const struct ui_list *l)
{
    int m = l->count * l->row_h - l->r.h;
    return m > 0 ? m : 0;
}

void ui_list_draw(struct ui_list *l, const struct gfx_surface *s)
{
    const struct ui_theme *t = ui_theme;
    struct gfx_surface v = sub(s, &l->r);
    if (!l->no_bg)
        gfx_fill(&v, 0, 0, v.w, v.h, t->panel);
    if (l->top > list_max_top(l))
        l->top = list_max_top(l);
    if (l->top < 0)
        l->top = 0;
    int first = l->top / l->row_h;
    for (int i = first; i < l->count; i++) {
        struct ui_rect rr = { 0, i * l->row_h - l->top, v.w, l->row_h };
        if (rr.y >= v.h)
            break;
        if (i == l->sel && t->radius) /* a rounded mark, a little inside the row */
            gfx_round_rect_aa(&v, rr.x + ui_px(3), rr.y + 1, rr.w - ui_px(6), rr.h - 2, t->radius, t->accent, 0,
                              GFX_CORNERS_ALL);
        else if (i == l->sel)
            gfx_fill(&v, rr.x, rr.y, rr.w, rr.h, t->accent);
        if (l->draw_row)
            l->draw_row(&v, &rr, i, i == l->sel, l->ctx);
    }
    int total = l->count * l->row_h;
    if (total > v.h) { /* where we are */
        int bh = v.h * v.h / total, by = (v.h - bh) * l->top / list_max_top(l);
        if (bh < 8)
            bh = 8;
        gfx_fill(&v, v.w - 3, by, 3, bh, t->text_dim);
    }
}

int ui_list_wheel(struct ui_list *l, int notches)
{
    int old = l->top;
    l->top -= notches * 3 * l->row_h;
    if (l->top > list_max_top(l))
        l->top = list_max_top(l);
    if (l->top < 0)
        l->top = 0;
    return l->top != old ? UI_CHANGED : UI_NONE;
}

void ui_list_show(struct ui_list *l, int row)
{
    if (row < 0 || row >= l->count)
        return;
    if (row * l->row_h < l->top)
        l->top = row * l->row_h;
    else if ((row + 1) * l->row_h > l->top + l->r.h)
        l->top = (row + 1) * l->row_h - l->r.h;
}

int ui_list_pointer(struct ui_list *l, int kind, int x, int y)
{
    switch (kind) {
    case GFX_PTR_DOWN:
        l->dragging = false;
        l->down_y = y;
        l->down_top = l->top;
        l->down_row = ui_inside(&l->r, x, y) ? (y - l->r.y + l->top) / l->row_h : -1;
        if (l->down_row >= l->count)
            l->down_row = -1;
        return UI_NONE;
    case GFX_PTR_MOVE: {
        if (l->down_row < 0 && !ui_inside(&l->r, x, l->down_y))
            return UI_NONE;
        int dy = y - l->down_y;
        if (!l->dragging && (dy > 6 || dy < -6))
            l->dragging = true;
        if (!l->dragging)
            return UI_NONE;
        int top = l->down_top - dy;
        if (top > list_max_top(l))
            top = list_max_top(l);
        if (top < 0)
            top = 0;
        if (top == l->top)
            return UI_NONE;
        l->top = top;
        return UI_CHANGED;
    }
    case GFX_PTR_UP:
        if (l->dragging || l->down_row < 0 || !ui_inside(&l->r, x, y))
            return UI_NONE;
        l->sel = l->down_row;
        return UI_ACTIVATED;
    case GFX_PTR_HOVER: { /* the mouse over a row selects it (a window with GFX_WIN_HOVER) */
        int row = ui_inside(&l->r, x, y) ? (y - l->r.y + l->top) / l->row_h : -1;
        if (row < 0 || row >= l->count || row == l->sel)
            return UI_NONE;
        l->sel = row;
        return UI_CHANGED;
    }
    default:
        return UI_NONE;
    }
}

/* ---- on-screen keyboard ---------------------------------------------------------------------------- */

#define K_SHIFT (-10)
#define K_SYM   (-11)
#define K_CTRL  (-12)

struct key {
    const char *label;
    int code;                   /* 0: the label's character */
    int w;                      /* in half units, 24 per row */
};

#define ROWKEYS 14

/* rows of 24 half units; a NULL label ends a row */
static const struct key s_letters[UI_KEYBOARD_ROWS][ROWKEYS] = {
    { { "1", 0, 2 }, { "2", 0, 2 }, { "3", 0, 2 }, { "4", 0, 2 }, { "5", 0, 2 }, { "6", 0, 2 }, { "7", 0, 2 },
      { "8", 0, 2 }, { "9", 0, 2 }, { "0", 0, 2 }, { "del", '\b', 4 }, { NULL, 0, 0 } },
    { { "q", 0, 2 }, { "w", 0, 2 }, { "e", 0, 2 }, { "r", 0, 2 }, { "t", 0, 2 }, { "y", 0, 2 }, { "u", 0, 2 },
      { "i", 0, 2 }, { "o", 0, 2 }, { "p", 0, 2 }, { "-", 0, 2 }, { "=", 0, 2 }, { NULL, 0, 0 } },
    { { "tab", '\t', 3 }, { "a", 0, 2 }, { "s", 0, 2 }, { "d", 0, 2 }, { "f", 0, 2 }, { "g", 0, 2 }, { "h", 0, 2 },
      { "j", 0, 2 }, { "k", 0, 2 }, { "l", 0, 2 }, { "enter", '\n', 3 }, { NULL, 0, 0 } },
    { { "shift", K_SHIFT, 4 }, { "z", 0, 2 }, { "x", 0, 2 }, { "c", 0, 2 }, { "v", 0, 2 }, { "b", 0, 2 },
      { "n", 0, 2 }, { "m", 0, 2 }, { ",", 0, 2 }, { ".", 0, 2 }, { "/", 0, 2 }, { NULL, 0, 0 } },
    { { "ctrl", K_CTRL, 3 }, { "?123", K_SYM, 3 }, { "esc", 27, 2 }, { " ", ' ', 8 }, { "<", UI_KEY_LEFT, 2 },
      { "^", UI_KEY_UP, 2 }, { "v", UI_KEY_DOWN, 2 }, { ">", UI_KEY_RIGHT, 2 }, { NULL, 0, 0 } },
};

static const struct key s_symbols[UI_KEYBOARD_ROWS][ROWKEYS] = {
    { { "!", 0, 2 }, { "@", 0, 2 }, { "#", 0, 2 }, { "$", 0, 2 }, { "%", 0, 2 }, { "^", 0, 2 }, { "&", 0, 2 },
      { "*", 0, 2 }, { "(", 0, 2 }, { ")", 0, 2 }, { "del", '\b', 4 }, { NULL, 0, 0 } },
    { { "~", 0, 2 }, { "`", 0, 2 }, { "|", 0, 2 }, { "\\", 0, 2 }, { "{", 0, 2 }, { "}", 0, 2 }, { "[", 0, 2 },
      { "]", 0, 2 }, { "<", 0, 2 }, { ">", 0, 2 }, { "_", 0, 2 }, { "+", 0, 2 }, { NULL, 0, 0 } },
    { { "tab", '\t', 3 }, { ":", 0, 2 }, { ";", 0, 2 }, { "\"", 0, 2 }, { "'", 0, 2 }, { "?", 0, 2 }, { "=", 0, 2 },
      { "-", 0, 2 }, { "*", 0, 2 }, { "/", 0, 2 }, { "enter", '\n', 3 }, { NULL, 0, 0 } },
    { { "abc", K_SYM, 4 }, { "1", 0, 2 }, { "2", 0, 2 }, { "3", 0, 2 }, { "4", 0, 2 }, { "5", 0, 2 }, { "6", 0, 2 },
      { "7", 0, 2 }, { "8", 0, 2 }, { "9", 0, 2 }, { "0", 0, 2 }, { NULL, 0, 0 } },
    { { "ctrl", K_CTRL, 3 }, { "abc", K_SYM, 3 }, { "esc", 27, 2 }, { " ", ' ', 8 }, { "<", UI_KEY_LEFT, 2 },
      { "^", UI_KEY_UP, 2 }, { "v", UI_KEY_DOWN, 2 }, { ">", UI_KEY_RIGHT, 2 }, { NULL, 0, 0 } },
};

static const char s_shift_from[] = "1234567890-=,./";
static const char s_shift_to[] = "!@#$%^&*()_+<>?";

static const struct key (*layout(const struct ui_keyboard *k))[ROWKEYS]
{
    return k->mode == 2 ? s_symbols : s_letters;
}

/* Where key @i (row * ROWKEYS + column) is */
static bool key_rect(const struct ui_keyboard *k, int i, struct ui_rect *out)
{
    const struct key *row = layout(k)[i / ROWKEYS];
    int col = i % ROWKEYS, x = 0;
    for (int c = 0; c < col; c++) {
        if (!row[c].label)
            return false;
        x += row[c].w;
    }
    if (!row[col].label)
        return false;
    int rh = k->r.h / UI_KEYBOARD_ROWS;
    int rw = i / ROWKEYS == UI_KEYBOARD_ROWS - 1 ? k->r.w - k->corner : k->r.w;
    out->x = k->r.x + x * rw / 24;
    out->w = k->r.x + (x + row[col].w) * rw / 24 - out->x;
    out->y = k->r.y + (i / ROWKEYS) * rh;
    out->h = rh;
    return true;
}

static int key_at(const struct ui_keyboard *k, int x, int y)
{
    if (!ui_inside(&k->r, x, y))
        return -1;
    for (int i = 0; i < UI_KEYBOARD_ROWS * ROWKEYS; i++) {
        struct ui_rect r;
        if (key_rect(k, i, &r) && ui_inside(&r, x, y))
            return i;
    }
    return -1;
}

/* The label as shown in the current mode */
static const char *key_label(const struct ui_keyboard *k, const struct key *key, char *buf)
{
    if (k->mode != 1 || key->code || !key->label[0] || key->label[1])
        return key->label;
    char c = key->label[0];
    const char *p = strchr(s_shift_from, c);
    buf[0] = c >= 'a' && c <= 'z' ? (char)(c - 32) : p ? s_shift_to[p - s_shift_from] : c;
    buf[1] = 0;
    return buf;
}

int ui_keyboard_height(int width)
{
    int h = width / 4, lo = ui_px(100), hi = ui_px(160);
    return h < lo ? lo : h > hi ? hi : h;
}

void ui_keyboard_draw(struct ui_keyboard *k, const struct gfx_surface *s)
{
    const struct ui_theme *t = ui_theme;
    gfx_fill(s, k->r.x, k->r.y, k->r.w, k->r.h, GFX_RGB(20, 22, 28));
    for (int i = 0; i < UI_KEYBOARD_ROWS * ROWKEYS; i++) {
        struct ui_rect r;
        if (!key_rect(k, i, &r))
            continue;
        const struct key *key = &layout(k)[i / ROWKEYS][i % ROWKEYS];
        bool latched = (key->code == K_SHIFT && k->mode == 1) || (key->code == K_CTRL && k->ctrl);
        bool special = key->code < 0 || key->code == '\b' || key->code == '\n' || key->code == '\t' || key->code == 27;
        uint32_t bg = i == k->pressed ? t->button_pressed : latched ? t->accent : special ? GFX_RGB(44, 50, 62) : t->button;
        gfx_round_rect_aa(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, t->radius ? (t->radius + 1) / 2 : 0, bg, 0,
                          GFX_CORNERS_ALL);
        char buf[2];
        const char *label = key_label(k, key, buf);
        ui_text_center(s, &r, label[1] ? t->font : t->bold, label, t->button_text);
    }
}

int ui_keyboard_pointer(struct ui_keyboard *k, int kind, int x, int y, bool *redraw)
{
    *redraw = false;
    int i = key_at(k, x, y);
    if (kind == GFX_PTR_DOWN || kind == GFX_PTR_MOVE) {
        if (kind == GFX_PTR_MOVE && k->pressed < 0)
            return -1;
        if (i != k->pressed) {
            k->pressed = i;
            *redraw = true;
        }
        return -1;
    }
    if (kind != GFX_PTR_UP)
        return -1;
    int was = k->pressed;
    k->pressed = -1;
    *redraw = was >= 0;
    if (was < 0 || i != was)
        return -1;
    const struct key *key = &layout(k)[i / ROWKEYS][i % ROWKEYS];
    switch (key->code) {
    case K_SHIFT:
        k->mode = k->mode == 1 ? 0 : 1;
        return -1;
    case K_SYM:
        k->mode = k->mode == 2 ? 0 : 2;
        return -1;
    case K_CTRL:
        k->ctrl = !k->ctrl;
        return -1;
    default:
        break;
    }
    int c = key->code;
    if (!c) {
        char buf[2];
        c = (unsigned char)key_label(k, key, buf)[0];
        if (k->mode == 1) /* shift is for one key */
            k->mode = 0;
    }
    if (k->ctrl && c < 0x100) {
        k->ctrl = false;
        if (c >= 'a' && c <= 'z')
            c -= 'a' - 1;
        else if (c >= '@' && c <= '_')
            c -= '@';
    }
    return c;
}
