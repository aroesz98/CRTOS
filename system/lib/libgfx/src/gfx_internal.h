/*
 * gfx_internal.h - what the parts of libgfx share and programs do not see.
 */
#ifndef GFX_INTERNAL_H
#define GFX_INTERNAL_H

/* settings.c: the appearance settings into the theme - once (gfx_open), and again when they
 * changed (GFX_EV_SETTINGS) */
void ui_settings_init(void);
void ui_settings_reload(void);

/* image.c: the header of a netpbm file (PAM, PPM) in @buf (@len bytes): the bytes a pixel has
 * (1-4) and *w, *h, *pixels (where the pixels start in @buf), or 0 */
int gfx_netpbm_header(const char *buf, int len, int *w, int *h, int *pixels);

#endif
