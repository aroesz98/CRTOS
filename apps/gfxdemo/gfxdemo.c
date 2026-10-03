/*
 * gfxdemo - animated window for the graphics server: a bouncing ball, frames per second and
 * the compositor's timing. Draws the next frame when the previous one is on the screen
 * (GFX_EV_FRAME), so it runs at the display rate without tearing. A tap changes the colour.
 * The window can be resized.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crtos.h>
#include "gfx.h"

static int W = 220, H = 140;

int main(int argc, char **argv)
{
    struct gfx *g = gfx_open();
    if (!g)
    {
        printf("gfxdemo: no graphics server\n");
        return 1;
    }

    int x0 = argc > 2 ? atoi(argv[1]) : -1, y0 = argc > 2 ? atoi(argv[2]) : -1;
    struct gfx_win *w = gfx_win_create(g, x0, y0, W, H, GFX_WIN_RESIZABLE, "gfxdemo");
    if (!w)
    {
        printf("gfxdemo: no window\n");
        return 1;
    }

    static const uint32_t colors[] = {GFX_RGB(255, 200, 0), GFX_RGB(80, 220, 120), GFX_RGB(255, 90, 140),
                                      GFX_RGB(90, 170, 255)};
    int ci = 0;
    int bx = 40, by = 50, vx = 3, vy = 2, r = 12;
    uint32_t frames = 0, fps = 0;
    uint64_t t_fps = crtos_time_us();
    struct gfx_stats st;
    memset(&st, 0, sizeof(st));
    for (;;)
    {
        struct gfx_surface *s = &w->s;
        gfx_fill(s, 0, 0, W, H, GFX_RGB(20, 28, 44));
        gfx_rect(s, 0, 0, W, H, GFX_RGB(90, 110, 150));
        gfx_fill(s, 1, 1, W - 2, 20, GFX_RGB(40, 60, 100));
        gfx_text(s, &gfx_sans_bold9, 6, 15, "gfxdemo", GFX_WHITE);
        char line[48];
        snprintf(line, sizeof(line), "%lu fps", (unsigned long)fps);
        gfx_text(s, &gfx_sans9, W - 6 - gfx_text_width(&gfx_sans9, line), 15, line, GFX_RGB(200, 220, 255));
        snprintf(line, sizeof(line), "compose %lu us (max %lu)", (unsigned long)st.avg_us, (unsigned long)st.max_us);
        gfx_text(s, &gfx_tiny, 6, H - 6, line, GFX_RGB(150, 170, 200));
        bx += vx;
        by += vy;
        if (bx < r + 1 || bx > W - r - 2)
            vx = -vx;
        if (by < 22 + r || by > H - r - 12)
            vy = -vy;
        gfx_fill_circle(s, bx, by, r, colors[ci]);
        gfx_circle(s, bx, by, r, GFX_WHITE);
        gfx_present(w);
        gfx_wait_frame(w, 200);

        struct gfx_event ev;
        while (gfx_next_event(g, &ev, 0) == 0)
        {
            if (ev.h.type == GFX_EV_POINTER && ev.kind == GFX_PTR_DOWN)
                ci = (ci + 1) % 4;
            if (ev.h.type == GFX_EV_CLOSE)
            {
                gfx_close(g);
                return 0;
            }
            if (ev.h.type == GFX_EV_CONFIGURE && gfx_win_resize(w, ev.w, ev.hgt) == 0)
            {
                W = w->s.w; /* the next frame is drawn at the new size */
                H = w->s.h;
                if (bx > W - r - 2)
                    bx = W - r - 2;
                if (by > H - r - 12)
                    by = H - r - 12;
            }
        }
        frames++;
        uint64_t now = crtos_time_us();
        if (now - t_fps >= 1000000u)
        {
            fps = (uint32_t)(frames * 1000000ull / (now - t_fps));
            frames = 0;
            t_fps = now;
            gfx_stats(g, &st);
        }
    }
}
