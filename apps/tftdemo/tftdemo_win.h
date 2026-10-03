/*
 * tftdemo_win.h - window of tftdemo (tftdemo_win.c).
 */
#ifndef TFTDEMO_WIN_H
#define TFTDEMO_WIN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TDW_CONFIGURE 100        /* the window manager asks for width x height */
#define TDW_FOCUS 101            /* value 1: the window has the focus now, 0: lost it */

struct tdw_event
{
    int kind;                   /* GFX_PTR_* (1 down, 2 move, 3 up), TDW_CONFIGURE, TDW_FOCUS, 0: other */
    int x, y;
    int width, height;
    int value;
    uint32_t time_ms;           /* when gfxd sent it */
};

uint32_t *tdw_open(int width, int height, const char *title);  /* XRGB8888 pixels, stride = width */
int tdw_present(void);                                          /* show it, wait for the frame; -1: not shown in 100 ms */
uint32_t *tdw_resize(int width, int height);                    /* new pixels (draw them all), NULL: no */
int tdw_event(struct tdw_event *e, uint32_t timeout);           /* 1 event, 0 none, -1 close */
void tdw_close(void);

#ifdef __cplusplus
}
#endif

#endif
