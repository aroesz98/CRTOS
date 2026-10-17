/*
 * tftdemo_win.c - the window side of tftdemo, in C: TFTLIB and libgfx both name their font
 * types GFXfont / GFXglyph (with different layouts), so they do not meet in one file.
 */
#include <stddef.h>
#include "gfx.h"
#include "tftdemo_win.h"

static struct gfx *s_g;
static struct gfx_win *s_w;

uint32_t *tdw_open(int width, int height, const char *title)
{
    s_g = gfx_open();
    if (!s_g)
        return NULL;
    s_w = gfx_win_create(s_g, -1, -1, width, height, GFX_WIN_XRGB | GFX_WIN_RESIZABLE, title);
    return s_w ? (uint32_t *)s_w->s.pix : NULL;
}

int tdw_present(void)
{
    gfx_present(s_w);
    return gfx_wait_frame(s_w, 100);
}

int tdw_event(struct tdw_event *e, uint32_t timeout)
{
    struct gfx_event ev;
    if (gfx_next_event(s_g, &ev, timeout))
        return 0;
    if (ev.h.type == GFX_EV_CLOSE)
        return -1;
    e->kind = ev.h.type == GFX_EV_POINTER     ? ev.kind
              : ev.h.type == GFX_EV_CONFIGURE ? TDW_CONFIGURE
              : ev.h.type == GFX_EV_FOCUS     ? TDW_FOCUS
                                              : 0;
    e->x = ev.x;
    e->y = ev.y;
    e->width = ev.w;
    e->height = ev.hgt;
    e->value = ev.value;
    e->time_ms = ev.time_ms;
    return 1;
}

uint32_t *tdw_resize(int width, int height)
{
    return gfx_win_resize(s_w, width, height) == 0 ? (uint32_t *)s_w->s.pix : NULL;
}

void tdw_close(void)
{
    gfx_close(s_g);
}
