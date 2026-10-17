/*
 * settings.c - the appearance settings (gfx_ui.h): UI_CFG, and the theme that follows them.
 *
 * The file has a "key = value" a line ('#' starts a comment): scale (percent), font (a family
 * id), accent (RRGGBB), transparency (on/off), opacity (percent), rounded (on/off), wallpaper
 * (the rest of the line), fit (fill, fit, stretch, center, tile). Missing keys keep the
 * defaults, unknown ones are ignored.
 *
 * Fonts: each scale has its sizes (the tables below; tools/fonts.py makes the files at the
 * same sizes): <family>-regular-<px>.fnt and <family>-bold-<px>.fnt in UI_FONT_DIR, and
 * mono-<px>.fnt for every family. DejaVu at 100 % is built in. A font is loaded once and kept
 * for the life of the program (a window may still be drawn with one it took before a change);
 * one that cannot be read falls back to DejaVu at that size, then to the built-in one.
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx_ui.h"
#include "gfx_internal.h"

const int ui_scales[UI_SCALES] = { 100, 125, 150, 175, 200 };
static const uint8_t s_text_px[UI_SCALES] = { 11, 14, 16, 19, 22 };
static const uint8_t s_heading_px[UI_SCALES] = { 15, 19, 22, 26, 30 };
static const uint8_t s_mono_px[UI_SCALES] = { 10, 12, 15, 17, 20 };

static const char *const s_fit_names[GFX_FITS] = { "fill", "fit", "stretch", "center", "tile" };

struct ui_settings ui_settings;
static struct ui_theme s_theme;
static bool s_ready;

void ui_settings_default(struct ui_settings *s)
{
    memset(s, 0, sizeof(*s));
    s->scale = 100;
    snprintf(s->font, sizeof(s->font), "dejavu");
    s->accent = 0x3478DC;
    s->transparency = true;
    s->opacity = 80;
    s->rounded = true;
    snprintf(s->wallpaper, sizeof(s->wallpaper), "%s", gfx_wallpaper_builtin(0));
    s->fit = GFX_FIT_FILL;
}

static int scale_index(int scale)
{
    int best = 0;
    for (int i = 1; i < UI_SCALES; i++)
        if (abs(ui_scales[i] - scale) < abs(ui_scales[best] - scale))
            best = i;
    return best;
}

static bool flag(const char *v)
{
    return !strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "yes") || !strcmp(v, "true");
}

int ui_settings_load(struct ui_settings *s)
{
    ui_settings_default(s);
    FILE *f = fopen(UI_CFG, "r");
    if (!f)
        return -1;
    char line[160];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (isspace((unsigned char)*p))
            p++;
        if (*p == '#' || !*p)
            continue;
        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        char *k = p, *v = eq + 1;
        char *ke = eq;
        while (ke > k && isspace((unsigned char)ke[-1]))
            ke--;
        *ke = 0;
        while (isspace((unsigned char)*v))
            v++;
        size_t n = strcspn(v, "\r\n");
        while (n && isspace((unsigned char)v[n - 1]))
            n--;
        v[n] = 0;
        ui_settings_set(s, k, v);
    }
    fclose(f);
    return 0;
}

int ui_settings_set(struct ui_settings *s, const char *k, const char *v)
{
    if (!strcmp(k, "scale")) {
        s->scale = ui_scales[scale_index(atoi(v))];
    } else if (!strcmp(k, "font") && *v) {
        snprintf(s->font, sizeof(s->font), "%s", v);
    } else if (!strcmp(k, "accent") && *v) {
        s->accent = (uint32_t)strtoul(v[0] == '#' ? v + 1 : v, NULL, 16) & 0xFFFFFFu;
    } else if (!strcmp(k, "transparency")) {
        s->transparency = flag(v);
    } else if (!strcmp(k, "opacity")) {
        int o = atoi(v);
        s->opacity = o < 30 ? 30 : o > 100 ? 100 : o;
    } else if (!strcmp(k, "rounded")) {
        s->rounded = flag(v);
    } else if (!strcmp(k, "wallpaper") && *v) {
        snprintf(s->wallpaper, sizeof(s->wallpaper), "%s", v);
    } else if (!strcmp(k, "fit")) {
        for (int i = 0; i < GFX_FITS; i++)
            if (!strcmp(v, s_fit_names[i])) {
                s->fit = i;
                return 0;
            }
        return -1;
    } else {
        return -1;
    }
    return 0;
}

int ui_settings_save(const struct ui_settings *s)
{
    FILE *f = fopen(UI_CFG, "w");
    if (!f)
        return -1;
    fprintf(f, "# appearance of the interface (Settings > Appearance; libgfx gfx_ui.h)\n");
    fprintf(f, "scale = %d\n", s->scale);
    fprintf(f, "font = %s\n", s->font);
    fprintf(f, "accent = %06lX\n", (unsigned long)(s->accent & 0xFFFFFFu));
    fprintf(f, "transparency = %s\n", s->transparency ? "on" : "off");
    fprintf(f, "opacity = %d\n", s->opacity);
    fprintf(f, "rounded = %s\n", s->rounded ? "on" : "off");
    fprintf(f, "wallpaper = %s\n", s->wallpaper);
    fprintf(f, "fit = %s\n", s_fit_names[s->fit >= 0 && s->fit < GFX_FITS ? s->fit : 0]);
    int bad = ferror(f);
    if (fclose(f) || bad)
        return -1;
    return 0;
}

/* ---- fonts ---- */

#define FONTS_KEPT 24

static struct {
    char family[24];
    char style[8];
    int px;
    const GFXfont *font;        /* NULL: it could not be loaded */
} s_fonts[FONTS_KEPT];
static int s_nfonts;

static const GFXfont *builtin(const char *style, int px)
{
    if (!strcmp(style, "regular") && px == 11)
        return &gfx_dejavu11;
    if (!strcmp(style, "bold") && px == 11)
        return &gfx_dejavu_bold11;
    if (!strcmp(style, "mono") && px == 10)
        return &gfx_dejavu_mono10;
    return NULL;
}

static const GFXfont *load(const char *family, const char *style, int px)
{
    if (!strcmp(family, "dejavu") || !strcmp(style, "mono")) {
        const GFXfont *b = builtin(style, px);
        if (b)
            return b;
    }
    for (int i = 0; i < s_nfonts; i++)
        if (s_fonts[i].px == px && !strcmp(s_fonts[i].style, style) && !strcmp(s_fonts[i].family, family))
            return s_fonts[i].font;
    char path[96];
    if (!strcmp(style, "mono"))
        snprintf(path, sizeof(path), "%s/mono-%d.fnt", UI_FONT_DIR, px);
    else
        snprintf(path, sizeof(path), "%s/%s-%s-%d.fnt", UI_FONT_DIR, family, style, px);
    const GFXfont *f = gfx_font_load(path);
    if (s_nfonts < FONTS_KEPT) {
        snprintf(s_fonts[s_nfonts].family, sizeof(s_fonts[0].family), "%s", family);
        snprintf(s_fonts[s_nfonts].style, sizeof(s_fonts[0].style), "%s", style);
        s_fonts[s_nfonts].px = px;
        s_fonts[s_nfonts].font = f;
        s_nfonts++;
    }
    return f;
}

/* @family's @style at @px; DejaVu at that size when it has none, the built-in one last */
static const GFXfont *font_for(const char *family, const char *style, int px)
{
    const GFXfont *f = load(family, style, px);
    if (!f && strcmp(family, "dejavu"))
        f = load("dejavu", style, px);
    if (!f)
        f = !strcmp(style, "mono") ? &gfx_dejavu_mono10 : !strcmp(style, "bold") ? &gfx_dejavu_bold11 : &gfx_dejavu11;
    return f;
}

int ui_font_families(char (*ids)[24], char (*names)[32], int max)
{
    FILE *f = fopen(UI_FONT_DIR "/families.txt", "r");
    int n = 0;
    if (f) {
        char line[96];
        while (n < max && fgets(line, sizeof(line), f)) {
            if (line[0] == '#' || isspace((unsigned char)line[0]))
                continue;
            line[strcspn(line, "\r\n")] = 0;
            char *sp = strchr(line, ' ');
            if (!sp)
                continue;
            *sp = 0;
            snprintf(ids[n], sizeof(ids[0]), "%.23s", line);
            snprintf(names[n], sizeof(names[0]), "%.31s", sp + 1);
            n++;
        }
        fclose(f);
    }
    if (!n && max > 0) { /* (no files: the built-in one) */
        snprintf(ids[0], sizeof(ids[0]), "dejavu");
        snprintf(names[0], sizeof(names[0]), "DejaVu Sans");
        n = 1;
    }
    return n;
}

const GFXfont *ui_family_font(const char *id)
{
    int k = scale_index(s_ready ? s_theme.scale : 100);
    const GFXfont *f = load(id, "regular", s_text_px[k]);
    return f ? f : ui_theme->font;
}

/* ---- the theme ---- */

int ui_px(int px)
{
    int scale = s_ready ? s_theme.scale : 100;
    int v = (px * scale + (px >= 0 ? 50 : -50)) / 100;
    return px > 0 && v < 1 ? 1 : v;
}

void ui_settings_apply(const struct ui_settings *s)
{
    if (s != &ui_settings)
        ui_settings = *s;
    int k = scale_index(s->scale);
    s_theme = ui_theme_dark;
    s_theme.scale = ui_scales[k];
    s_theme.font = font_for(s->font, "regular", s_text_px[k]);
    s_theme.bold = font_for(s->font, "bold", s_text_px[k]);
    s_theme.heading = font_for(s->font, "bold", s_heading_px[k]);
    s_theme.mono = font_for(s->font, "mono", s_mono_px[k]);
    uint32_t accent = 0xFF000000u | (s->accent & 0xFFFFFFu);
    s_theme.accent = accent;
    s_theme.button_pressed = accent;
    s_ready = true;
    s_theme.radius = s->rounded ? ui_px(6) : 0;
    s_theme.alpha = (uint8_t)(s->transparency ? s->opacity * 255 / 100 : 255);
    ui_theme = &s_theme;
}

void ui_settings_init(void)
{
    if (s_ready)
        return;
    struct ui_settings s;
    ui_settings_load(&s);
    ui_settings_apply(&s);
}

void ui_settings_reload(void)
{
    struct ui_settings s;
    ui_settings_load(&s);
    ui_settings_apply(&s);
}
