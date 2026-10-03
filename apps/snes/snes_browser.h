/*
 * snes_browser.h - the cartridge browser of the SNES program: folders and ROM files (.sfc,
 * .smc) of the SD card in a retro list, the header of the selected ROM, message boxes.
 */
#ifndef SNES_BROWSER_H
#define SNES_BROWSER_H

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

/* what a key or button means outside the game */
enum nav {
    NAV_NONE,
    NAV_UP,
    NAV_DOWN,
    NAV_LEFT,
    NAV_RIGHT,
    NAV_PAGE_UP,
    NAV_PAGE_DOWN,
    NAV_HOME,
    NAV_END,
    NAV_OK,
    NAV_BACK,
    NAV_MENU
};

/* what the browser wants after an input */
enum browser_result {
    BROWSER_IDLE,       /* nothing changed */
    BROWSER_REDRAW,     /* draw it again */
    BROWSER_OPEN        /* start the game at browser_path() */
};

/* Opens the folder of the last session (SNES_VAR_DIR/snes.cfg, snes_core.h), else @start,
 * else /sd/snes if there is one, else /sd */
void browser_init(const char *start);
void browser_draw(const struct gfx_surface *s, uint64_t now_us);
enum browser_result browser_nav(enum nav nav);
/* GFX_PTR_* at window coordinates */
enum browser_result browser_pointer(int kind, int x, int y);
/* the file chosen by BROWSER_OPEN */
const char *browser_path(void);
/* Stores the folder and the file for the next session */
void browser_remember(void);

/* A message box over the list; any key or touch closes it (unless @wait: "please wait",
 * closed by browser_popup_close()) */
void browser_popup(const char *title, const char *text, bool wait);
void browser_popup_close(void);

#endif
