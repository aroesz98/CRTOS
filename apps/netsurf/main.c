/*
 * main.c - NetSurf on CRTOS: runs the framebuffer front end (its main() is built as
 * nsfb_main()) in a window as large as the screen leaves room for, next to the window
 * manager's task bar and frame. The window can be resized and maximised later.
 *
 *     netsurf [-v | -V logfile] [url]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "gfx.h"

int nsfb_main(int argc, char **argv);

#define FRAME_ROOM 58 /* task bar and window title */

/* libcrtosheap, the browser's malloc (a TLSF: newlib's first-fit list made scripts, with their
 * many small objects, spend most of their time in malloc and free). When the arena's heap is
 * full - a page with large scripts - it grows into one window of at most 8 MB, which the kernel
 * can usually place in a single MPU region: the other windows stay for the window's surface,
 * made anew when the window is resized, and the clipboard. No emulated memory: it would slow
 * scripts and layout to a crawl. */
const int crtos_heap_windows = 1;
const int crtos_heap_window_kb = 8192;
const int crtos_heap_swap = 0;

/* the display's size, for scripts (window.screen: the JavaScript bindings, Window.bnd) */
int crtos_screen_width, crtos_screen_height;

int main(int argc, char **argv)
{
    /* the cookies (the Choices file in /sd/crtos/share/netsurf names the place) */
    mkdir("/sd/crtos/var", 0755);
    mkdir("/sd/crtos/var/netsurf", 0755);

    int w = 480, h = 272 - FRAME_ROOM;
    struct gfx *g = gfx_open();
    if (!g)
    {
        fprintf(stderr, "netsurf: no graphics server\n");
        return 1;
    }
    crtos_screen_width = gfx_screen_width(g);
    crtos_screen_height = gfx_screen_height(g);
    w = crtos_screen_width;
    h = crtos_screen_height - FRAME_ROOM;
    gfx_close(g);

    char ws[12], hs[12];
    snprintf(ws, sizeof(ws), "%d", w);
    snprintf(hs, sizeof(hs), "%d", h);
    char **args = calloc((size_t)argc + 8, sizeof(char *));
    if (!args)
        return 1;
    int n = 0, i = 1;
    args[n++] = argv[0];
    /* the log options first: NetSurf looks for them only in argv[1] */
    if (i < argc && !strcmp(argv[i], "-v"))
    {
        args[n++] = argv[i++];
    }
    else if (i + 1 < argc && !strcmp(argv[i], "-V"))
    {
        args[n++] = argv[i++];
        args[n++] = argv[i++];
    }
    args[n++] = "-f"; /* our window: the first surface found might be the RAM one */
    args[n++] = "crtos";
    args[n++] = "-w";
    args[n++] = ws;
    args[n++] = "-h";
    args[n++] = hs;
    while (i < argc)
        args[n++] = argv[i++];
    args[n] = NULL;
    return nsfb_main(n, args);
}
