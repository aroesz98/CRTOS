/*
 * gfxinfo - state of the graphics stack: devices known to devmgr, clients, windows and
 * composition times of gfxd. "gfxinfo -w" also opens a test window for a second;
 * "gfxinfo -b [s]" measures the frame rate of an animation, alone and while another window
 * is damaged at random moments (as by a finger in paint).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include "devmgr_proto.h"
#include "gfx.h"

/* Animate one window frame by frame for @us; with @draw, damage little squares of another
 * window every 4-12 ms meanwhile. Returns the frames the animation got. */
static uint32_t bench_run(struct gfx *g, struct gfx_win *a, struct gfx_win *b, uint64_t us, bool draw,
                          uint32_t *damages)
{
    uint32_t frames = 0;
    uint64_t now = crtos_time_us(), end = now + us, next = now;
    unsigned seed = 12345u;
    while ((now = crtos_time_us()) < end) {
        if (!a->frame_pending) {
            char line[24];
            snprintf(line, sizeof(line), "%lu", (unsigned long)frames);
            gfx_fill(&a->s, 0, 0, a->s.w, a->s.h, GFX_RGB(40, 40, 48));
            gfx_text(&a->s, &gfx_sans_bold12, 8, 32, line, GFX_WHITE);
            gfx_present(a);
            frames++;
        }
        if (draw && now >= next) {
            seed = seed * 1103515245u + 12345u;
            int x = (int)((seed >> 16) % (unsigned)(b->s.w - 8)), y = (int)((seed >> 8) % (unsigned)(b->s.h - 8));
            gfx_fill(&b->s, x, y, 8, 8, GFX_RGB(seed & 255, (seed >> 8) & 255, 160));
            gfx_damage(b, x, y, 8, 8);
            (*damages)++;
            next = now + 4000u + (seed >> 20) % 8000u;
        }
        struct gfx_event ev;
        uint32_t wait = 20;
        if (draw) {
            now = crtos_time_us();
            wait = next > now ? (uint32_t)((next - now) / 1000u) : 0;
        }
        gfx_next_event(g, &ev, wait); /* a frame event clears a->frame_pending */
    }
    gfx_wait_frame(a, 100);
    return frames;
}

static void bench(struct gfx *g, int seconds)
{
    struct gfx_win *a = gfx_win_create(g, 330, 180, 120, 48, 0, "bench-anim");
    struct gfx_win *b = gfx_win_create(g, 20, 150, 140, 90, 0, "bench-draw");
    if (!a || !b) {
        printf("bench: cannot create windows\n");
        return;
    }
    gfx_fill(&b->s, 0, 0, b->s.w, b->s.h, GFX_RGB(16, 32, 64));
    gfx_present(b);
    gfx_wait_frame(b, 200);
    for (int pass = 0; pass < 2; pass++) {
        struct gfx_stats s0, s1;
        uint32_t damages = 0;
        gfx_stats(g, &s0);
        uint64_t t0 = crtos_time_us();
        uint32_t frames = bench_run(g, a, b, (uint64_t)seconds * 1000000u, pass == 1, &damages);
        uint64_t t = crtos_time_us() - t0;
        gfx_stats(g, &s1);
        uint32_t fps10 = (uint32_t)((uint64_t)frames * 10000000u / t);
        /* composition time of these frames: gfxd's average is over all it ever composed */
        uint32_t composed = s1.composed - s0.composed;
        uint64_t sum = (uint64_t)s1.avg_us * s1.composed - (uint64_t)s0.avg_us * s0.composed;
        printf("bench %s: %lu.%lu fps (%lu frames), display %lu frames, gfxd composed %lu (%lu us each), %lu damages\n",
               pass ? "with drawing" : "alone       ", (unsigned long)(fps10 / 10u), (unsigned long)(fps10 % 10u),
               (unsigned long)frames, (unsigned long)(s1.frames - s0.frames), (unsigned long)composed,
               (unsigned long)(composed ? sum / composed : 0), (unsigned long)damages);
    }
    gfx_win_destroy(a);
    gfx_win_destroy(b);
}

int main(int argc, char **argv)
{
    int dm = crtos_port_connect(DEVMGR_PORT_NAME, 1000);
    if (dm >= 0) {
        struct devmgr_subscribe q = { DEVMGR_LIST, sizeof(q), "" };
        char list[MSG_MAX + 1];
        int n = crtos_msg_call(dm, &q, sizeof(q), list, MSG_MAX, 1000);
        if (n >= 0) {
            list[n] = 0;
            for (char *p = list; *p; p++)
                if (*p == '\n')
                    *p = ' ';
            printf("devices: %s\n", list);
        }
        close(dm);
    } else {
        printf("devmgr: not running\n");
    }
    struct gfx *g = gfx_open();
    if (!g) {
        printf("gfxd: not running\n");
        return 1;
    }
    struct gfx_stats st;
    if (gfx_stats(g, &st) == 0)
        printf("gfxd: screen %dx%d, %lu clients, %lu windows, %lu frames shown, %lu composed,\n"
               "      composition %lu us last, %lu us average, %lu us max, starts %lu us after a frame,\n"
               "      %lu flips, %lu late\n",
               gfx_screen_width(g), gfx_screen_height(g), (unsigned long)st.clients, (unsigned long)st.windows,
               (unsigned long)st.frames, (unsigned long)st.composed, (unsigned long)st.last_us,
               (unsigned long)st.avg_us, (unsigned long)st.max_us, (unsigned long)st.lag_us,
               (unsigned long)st.flips, (unsigned long)st.missed);
    if (argc > 1 && !strcmp(argv[1], "-w")) {
        struct gfx_win *w = gfx_win_create(g, 150, 60, 180, 100, GFX_WIN_ALPHA, "gfxinfo");
        if (w) {
            gfx_fill(&w->s, 0, 0, 180, 100, GFX_ARGB(160, 255, 255, 255));
            gfx_round_rect(&w->s, 4, 4, 172, 92, 10, GFX_ARGB(230, 30, 60, 120), true);
            gfx_text(&w->s, &gfx_sans_bold9, 16, 55, "translucent", GFX_WHITE);
            uint64_t t0 = crtos_time_us();
            gfx_present(w);
            int r = gfx_wait_frame(w, 1000);
            printf("test window: %s after %lu us\n", r ? "no frame" : "on screen",
                   (unsigned long)(crtos_time_us() - t0));
            crtos_sleep_ms(1000);
        }
    }
    if (argc > 1 && !strcmp(argv[1], "-b"))
        bench(g, argc > 2 ? atoi(argv[2]) : 3);
    gfx_close(g);
    return 0;
}
