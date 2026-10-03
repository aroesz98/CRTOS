/*
 * paint - finger painting in a window: pick a colour on the left, draw on the canvas,
 * "clear" wipes it. Only the changed rectangles are sent to the graphics server. The window
 * can be resized; the picture stays.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crtos.h>
#include "gfx.h"

static int W = 300, H = 190;
#define BAR 34

static const uint32_t s_colors[] = {GFX_RGB(255, 255, 255), GFX_RGB(255, 208, 0), GFX_RGB(64, 255, 64),
                                    GFX_RGB(255, 64, 128), GFX_RGB(64, 192, 255), GFX_RGB(0, 0, 0)};
#define NCOL ((int)(sizeof(s_colors) / sizeof(s_colors[0])))

static void draw_bar(struct gfx_surface *s, int sel)
{
    gfx_fill(s, 0, 0, BAR, H, GFX_RGB(40, 48, 64));
    for (int i = 0; i < NCOL; i++)
    {
        int y = 4 + i * 26;
        gfx_fill(s, 5, y, BAR - 10, 22, s_colors[i]);
        gfx_rect(s, 4, y - 1, BAR - 8, 24, i == sel ? GFX_WHITE : GFX_RGB(40, 48, 64));
    }
    gfx_fill(s, 3, H - 22, BAR - 6, 18, GFX_RGB(160, 40, 40));
    gfx_text(s, &gfx_tiny, 7, H - 10, "clear", GFX_WHITE);
}

static void clear_canvas(struct gfx_surface *s)
{
    gfx_fill(s, BAR, 0, W - BAR, H, GFX_RGB(16, 32, 64));
    gfx_rect(s, BAR, 0, W - BAR, H, GFX_RGB(90, 110, 150));
    gfx_text(s, &gfx_sans9, BAR + 8, 16, "paint", GFX_RGB(120, 140, 180));
}

int main(void)
{
    struct gfx *g = gfx_open();
    if (!g)
    {
        printf("paint: no graphics server\n");
        return 1;
    }
    struct gfx_win *w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_RESIZABLE, "paint");
    if (!w)
        return 1;
    struct gfx_surface *s = &w->s;
    int sel = 1, lx = -1, ly = -1;
    draw_bar(s, sel);
    clear_canvas(s);
    gfx_present(w);
    for (;;)
    {
        struct gfx_event ev;
        if (gfx_next_event(g, &ev, CRTOS_FOREVER))
            continue;
        if (ev.h.type == GFX_EV_CLOSE)
            break;
        if (ev.h.type == GFX_EV_CONFIGURE &&
            gfx_win_resize(w, ev.w < BAR + 60 ? BAR + 60 : ev.w, ev.hgt < 4 + NCOL * 26 + 24 ? 4 + NCOL * 26 + 24 : ev.hgt) ==
                0)
        { /* smaller than the colours need: as small as they allow */
            /* keep the picture: copy what fits inside the old border */
            int ow = w->old_w, oh = w->old_h, ostride = w->old_stride;
            const uint8_t *old = (const uint8_t *)w->old_pix;
            W = w->s.w;
            H = w->s.h;
            clear_canvas(s);
            int x1 = (ow < W ? ow : W) - 1, y1 = (oh < H ? oh : H) - 1;
            for (int y = 1; y < y1; y++)
                memcpy((uint8_t *)s->pix + y * s->stride + (BAR + 1) * 2, old + y * ostride + (BAR + 1) * 2,
                       (size_t)(x1 - BAR - 1) * 2u);
            draw_bar(s, sel);
            gfx_present(w);
            lx = -1;
            continue;
        }
        if (ev.h.type != GFX_EV_POINTER)
            continue;
        int x = ev.x, y = ev.y;
        if (ev.kind == GFX_PTR_DOWN && x < BAR)
        {
            if (y > H - 24)
            {
                clear_canvas(s);
                gfx_present(w);
            }
            else if ((y - 4) / 26 < NCOL)
            {
                sel = (y - 4) / 26;
                draw_bar(s, sel);
                gfx_damage(w, 0, 0, BAR, H);
            }
            lx = -1;
            continue;
        }
        if (ev.kind == GFX_PTR_UP)
        {
            lx = -1;
            continue;
        }
        if (x < BAR + 3)
            continue;
        if (lx < 0)
        {
            lx = x;
            ly = y;
        }
        /* a thick line: filled circles along it */
        int dx = x - lx, dy = y - ly, steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
        for (int i = 0; i <= steps; i++)
        {
            int px = lx + (steps ? dx * i / steps : 0), py = ly + (steps ? dy * i / steps : 0);
            gfx_fill_circle(s, px, py, 3, s_colors[sel]);
        }
        int x0 = (lx < x ? lx : x) - 4, y0 = (ly < y ? ly : y) - 4;
        gfx_damage(w, x0, y0, abs(dx) + 9, abs(dy) + 9);
        lx = x;
        ly = y;
    }
    gfx_close(g);
    return 0;
}
