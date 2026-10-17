/*
 * hello_world - Hello world: a window with a button (made by "crtos new"; change it as you like).
 *
 * The graphics server (gfxd) shows the window. The program draws into the window's pixels
 * (w->s) and then tells the server what changed (gfx_present: the whole window). Events come
 * from gfx_next_event(): touches (GFX_EV_POINTER), keys (GFX_EV_KEY), "please close"
 * (GFX_EV_CLOSE) and a new size chosen in the window manager (GFX_EV_CONFIGURE).
 * Drawing functions: system/lib/libgfx/include/gfx.h; buttons, lists, keyboard: gfx_ui.h.
 *
 * The look follows Settings > Appearance: sizes meant for 100 % go through ui_px() (the
 * interface scale), fonts and colours come from ui_theme, and GFX_EV_SETTINGS says that the
 * user changed them (the window then takes the size for the new scale).
 */
#include <stdbool.h>
#include <stdio.h>
#include <crtos.h>
#include "gfx.h"
#include "gfx_ui.h"

static int W, H;                /* window size: ui_px(240) x ui_px(150) to start */
static int count;               /* how many times the button was pressed */

static struct ui_rect button_rect(void)
{
    struct ui_rect r = { W / 2 - ui_px(60), H - ui_px(52), ui_px(120), ui_px(36) };
    return r;
}

static void draw(struct gfx_win *w, bool pressed)
{
    struct gfx_surface *s = &w->s;
    char text[48];
    int x = ui_px(12), y = ui_px(10) + gfx_font_ascent(ui_theme->bold);
    gfx_fill(s, 0, 0, W, H, ui_theme->bg);
    gfx_text(s, ui_theme->bold, x, y, "Hello world", ui_theme->text);
    snprintf(text, sizeof(text), "The button was pressed %d times", count);
    gfx_text(s, ui_theme->font, x, y + ui_theme->bold->yAdvance + ui_px(10), text, ui_theme->text_dim);
    struct ui_rect b = button_rect();
    ui_button(s, &b, "Press me", pressed);
    gfx_present(w);
}

int main(void)
{
    struct gfx *g = gfx_open();
    if (!g) {
        printf("hello_world: no graphics server\n");
        return 1;
    }
    W = ui_px(240);
    H = ui_px(150);
    /* x, y = -1: the window manager places it; GFX_WIN_RESIZABLE: it may change its size */
    struct gfx_win *w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_RESIZABLE, "Hello world");
    if (!w) {
        printf("hello_world: no window\n");
        return 1;
    }
    bool pressed = false;
    draw(w, pressed);
    for (;;) {
        struct gfx_event ev;
        if (gfx_next_event(g, &ev, CRTOS_FOREVER))
            continue;
        if (ev.h.type == GFX_EV_CLOSE)
            break;
        if (ev.h.type == GFX_EV_CONFIGURE && gfx_win_resize(w, ev.w, ev.hgt) == 0) {
            W = w->s.w;
            H = w->s.h;
            draw(w, pressed);
        } else if (ev.h.type == GFX_EV_SETTINGS) {  /* another scale, font or colour */
            if (gfx_win_resize(w, ui_px(240), ui_px(150)) >= 0) {
                W = w->s.w;
                H = w->s.h;
            }
            draw(w, pressed);
        } else if (ev.h.type == GFX_EV_POINTER) {
            struct ui_rect b = button_rect();
            bool inside = ui_inside(&b, ev.x, ev.y);
            bool now = inside && ev.kind != GFX_PTR_UP;
            if (ev.kind == GFX_PTR_UP && inside && pressed)
                count++;
            if (now != pressed || ev.kind == GFX_PTR_UP) {
                pressed = now;
                draw(w, pressed);
            }
        }
    }
    gfx_close(g);
    return 0;
}
