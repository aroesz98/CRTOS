/*
 * appearance - the appearance settings from the command line (what Settings > Appearance and
 * Wallpaper change; gfx_ui.h, /sd/crtos/etc/ui.cfg).
 *
 *   appearance                      show them
 *   appearance KEY VALUE [...]      change them, write the file and tell gfxd: the wallpaper is
 *                                   drawn anew and every program follows
 *
 * Keys: scale (100 125 150 175 200), font (a family of /sd/crtos/share/fonts/families.txt),
 * accent (RRGGBB), transparency (on/off), opacity (30-100), rounded (on/off), wallpaper (a
 * built-in name, color:RRGGBB or a picture file), fit (fill fit stretch center tile).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "gfx_ui.h"

static const char *const s_fits[GFX_FITS] = { "fill", "fit", "stretch", "center", "tile" };

static void show(const struct ui_settings *s)
{
    printf("scale        %d %%\n", s->scale);
    printf("font         %s\n", s->font);
    printf("accent       %06lx\n", (unsigned long)(s->accent & 0xFFFFFFu));
    printf("transparency %s (opacity %d %%)\n", s->transparency ? "on" : "off", s->opacity);
    printf("rounded      %s\n", s->rounded ? "on" : "off");
    printf("wallpaper    %s (%s)\n", s->wallpaper, s_fits[s->fit >= 0 && s->fit < GFX_FITS ? s->fit : 0]);
}

int main(int argc, char **argv)
{
    struct ui_settings s;
    ui_settings_load(&s);
    if (argc == 1) {
        show(&s);
        return 0;
    }
    if ((argc - 1) % 2) {
        fprintf(stderr, "usage: appearance [KEY VALUE]...\n");
        return 2;
    }
    for (int i = 1; i + 1 < argc; i += 2)
        if (ui_settings_set(&s, argv[i], argv[i + 1])) {
            fprintf(stderr, "appearance: %s %s: no such setting\n", argv[i], argv[i + 1]);
            return 2;
        }
    if (ui_settings_save(&s)) {
        fprintf(stderr, "appearance: %s: %s\n", UI_CFG, strerror(errno));
        return 1;
    }
    struct gfx *g = gfx_open();
    if (!g) {
        fprintf(stderr, "appearance: saved; no graphics server to tell\n");
        return 0;
    }
    gfx_settings_changed(g);
    gfx_close(g);
    show(&s);
    return 0;
}
