/*
 * settings - system settings (layer 3).
 *
 * Appearance: the interface scale, the font family, the accent colour, transparency (and how
 * much the translucent surfaces hide) and rounded corners. Wallpaper: the built-in pictures,
 * pictures in /sd/crtos/share/wallpapers (PPM, PAM: "crtos wallpaper" on the computer makes
 * them), solid colours, and how a picture covers the screen. Every change is written to
 * /sd/crtos/etc/ui.cfg at once and gfxd is told (gfx_settings_changed): it draws the wallpaper
 * anew and every program follows (GFX_EV_SETTINGS) - this one too, which lays itself out again.
 *
 * Clock: date, time and time zone. The clock is set with settimeofday() (needs the sys
 * capability); the zone goes to /sd/crtos/etc/timezone as a POSIX TZ string, where the
 * window manager and other programs read it.
 * System: what the system is, restart the window manager (it comes back through init and
 * takes over the windows) and reboot.
 *
 * Layout: sizes meant for 100 % are taken at the scale (ui_px); a page scrolls when the window
 * is too small for it (drag it or turn the wheel). Each drawing of a page notes where its
 * controls are, and a touch is looked up there.
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>
#include <crtos.h>
#include "gfx.h"
#include "gfx_ui.h"

#define BASE_W 460 /* the window at 100 % */
#define BASE_H 340
#define TZ_CFG "/sd/crtos/etc/timezone"
#define MAXHITS 96
#define MAXWALL 24
#define MAXFAM 8
#define DRAG_PX 8 /* a finger moved this far scrolls the page instead of pressing */

static const struct
{
    const char *name, *tz;
} s_zones[] = {
    {"UTC", "UTC0"},
    {"Warsaw, Berlin, Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"London, Lisbon", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Kyiv, Helsinki, Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Moscow, Istanbul", "MSK-3"},
    {"New York", "EST5EDT,M3.2.0,M11.1.0"},
    {"Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"Los Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Tokyo", "JST-9"},
    {"Beijing, Singapore", "CST-8"},
    {"India", "IST-5:30"},
    {"Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};
#define NZONES ((int)(sizeof(s_zones) / sizeof(s_zones[0])))

enum
{
    F_YEAR,
    F_MON,
    F_DAY,
    F_HOUR,
    F_MIN,
    F_ZONE,
    NFIELDS
};
static const char *const s_field_names[NFIELDS] = {"Year", "Month", "Day", "Hour", "Minute", "Zone"};

enum
{
    TAB_LOOK,
    TAB_WALL,
    TAB_CLOCK,
    TAB_SYS,
    NTABS
};
static const char *const s_tab_names[NTABS] = {"Appearance", "Wallpaper", "Clock", "System"};

/* what a touch can land on */
enum
{
    H_TAB,
    H_SCALE,
    H_FONT,
    H_ACCENT,
    H_TRANSP,
    H_OPACITY,
    H_ROUNDED,
    H_WALL,
    H_FIT,
    H_MINUS,
    H_PLUS,
    H_SET_CLOCK,
    H_WM,
    H_REBOOT
};

struct hit
{
    struct ui_rect r; /* in the window */
    int id, arg;
};

static const uint32_t s_accents[] = {0x3478DC, 0x0F9D8F, 0x2E9D4E, 0xE07B24, 0xD9473F, 0xD0418B, 0x7B4FD8, 0x5A6478};
#define NACCENTS ((int)(sizeof(s_accents) / sizeof(s_accents[0])))
static const char *const s_colors[] = {"color:1B2838", "color:202020", "color:23352B", "color:3A2433"};
#define NCOLORS ((int)(sizeof(s_colors) / sizeof(s_colors[0])))
static const char *const s_fit_names[GFX_FITS] = {"Fill", "Fit", "Stretch", "Center", "Tile"};

struct wall
{
    char spec[96];
    char name[32];
    uint16_t *thumb; /* RGB565, s_thumb_w x s_thumb_h; NULL: not made yet */
    bool file;
    bool failed; /* the picture could not be read */
};

static struct gfx *g;
static struct gfx_win *w;
static int s_tab;
static struct ui_settings s_set;  /* what the pages show (the settings saved last) */
static int s_laid_scale;          /* the scale the window has its size for */
static struct hit s_hits[MAXHITS];
static int s_nhits;
static struct ui_rect s_content; /* the page's area of the window */
static int s_scroll, s_page_h;
static int s_press_id = -1, s_press_arg; /* control under the finger */
static bool s_dragging, s_sliding;
static int s_down_y, s_down_scroll, s_slide;
static struct ui_rect s_slider; /* (in the window) */
static struct wall s_walls[MAXWALL];
static int s_nwalls, s_thumb_w, s_thumb_h;
static char s_fam_ids[MAXFAM][24], s_fam_names[MAXFAM][32];
static int s_nfam;
static struct tm s_edit; /* date and time being edited (local) */
static bool s_edited;    /* stop following the clock */
static int s_zone;
static char s_msg[80];

/* ---- clock ------------------------------------------------------------------------------------------ */

static void load_zone(void)
{
    char tz[64] = "";
    FILE *f = fopen(TZ_CFG, "r");
    if (f)
    {
        if (fgets(tz, sizeof(tz), f))
            tz[strcspn(tz, "\r\n")] = 0;
        fclose(f);
    }
    s_zone = 1; /* the window manager's default */
    for (int i = 0; i < NZONES; i++)
        if (!strcmp(tz, s_zones[i].tz))
            s_zone = i;
    setenv("TZ", s_zones[s_zone].tz, 1);
    tzset();
}

static void save_zone(void)
{
    FILE *f = fopen(TZ_CFG, "w");
    if (!f)
    {
        snprintf(s_msg, sizeof(s_msg), "zone: %s", strerror(errno));
        return;
    }
    fprintf(f, "%s\n", s_zones[s_zone].tz);
    fclose(f);
    setenv("TZ", s_zones[s_zone].tz, 1);
    tzset();
}

static void follow_clock(void)
{
    time_t now = (time_t)(crtos_wall_us() / 1000000);
    localtime_r(&now, &s_edit);
}

static int days_in(int mon, int year)
{
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return mon == 1 && leap ? 29 : d[mon];
}

static void change(int field, int delta)
{
    struct tm *t = &s_edit;
    s_edited = true;
    switch (field)
    {
    case F_YEAR:
        t->tm_year += delta;
        if (t->tm_year < 100)
            t->tm_year = 100;
        break;
    case F_MON:
        t->tm_mon = (t->tm_mon + delta + 12) % 12;
        break;
    case F_DAY:
        t->tm_mday = (t->tm_mday - 1 + delta + 31) % 31 + 1;
        break;
    case F_HOUR:
        t->tm_hour = (t->tm_hour + delta + 24) % 24;
        break;
    case F_MIN:
        t->tm_min = (t->tm_min + delta + 60) % 60;
        break;
    case F_ZONE:
        s_zone = (s_zone + delta + NZONES) % NZONES;
        save_zone();
        if (!s_edited)
            follow_clock();
        return;
    default:
        break;
    }
    int dim = days_in(t->tm_mon, t->tm_year + 1900);
    if (t->tm_mday > dim)
        t->tm_mday = dim;
    t->tm_sec = 0;
}

static void set_clock(void)
{
    struct tm t = s_edit;
    t.tm_isdst = -1;
    time_t v = mktime(&t);
    struct timeval tv = {v, 0};
    if (v == (time_t)-1 || settimeofday(&tv, NULL))
    {
        snprintf(s_msg, sizeof(s_msg), "Cannot set the clock: %s", strerror(errno));
        return;
    }
    s_edited = false;
    snprintf(s_msg, sizeof(s_msg), "Clock set");
}

static void field_value(int f, char *buf, size_t n)
{
    switch (f)
    {
    case F_YEAR:
        snprintf(buf, n, "%d", s_edit.tm_year + 1900);
        break;
    case F_MON:
        snprintf(buf, n, "%02d", s_edit.tm_mon + 1);
        break;
    case F_DAY:
        snprintf(buf, n, "%02d", s_edit.tm_mday);
        break;
    case F_HOUR:
        snprintf(buf, n, "%02d", s_edit.tm_hour);
        break;
    case F_MIN:
        snprintf(buf, n, "%02d", s_edit.tm_min);
        break;
    default:
        snprintf(buf, n, "%s", s_zones[s_zone].name);
        break;
    }
}

static void restart_wm(void)
{
    struct crtos_procinfo pi;
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++)
        if (!strcmp(pi.name, "wm") && pi.state == 0)
        {
            if (crtos_kill(pi.pid, 0) == 0)
                snprintf(s_msg, sizeof(s_msg), "Window manager restarting...");
            else
                snprintf(s_msg, sizeof(s_msg), "wm: %s", strerror(errno));
            return;
        }
    snprintf(s_msg, sizeof(s_msg), "No window manager runs");
}

/* ---- wallpapers --------------------------------------------------------------------------------- */

static void walls_free(void)
{
    for (int i = 0; i < s_nwalls; i++)
        free(s_walls[i].thumb);
    s_nwalls = 0;
}

static void wall_add(const char *spec, const char *name, bool file)
{
    if (s_nwalls == MAXWALL)
        return;
    struct wall *v = &s_walls[s_nwalls++];
    memset(v, 0, sizeof(*v));
    snprintf(v->spec, sizeof(v->spec), "%s", spec);
    snprintf(v->name, sizeof(v->name), "%s", name);
    v->file = file;
}

static int by_name(const void *a, const void *b)
{
    return strcmp(((const struct wall *)a)->name, ((const struct wall *)b)->name);
}

/* The wallpapers to choose from: built-in, the pictures in the folder, colours (and the one
 * set now, if it is none of them) */
static void walls_scan(void)
{
    walls_free();
    for (int i = 0; gfx_wallpaper_builtin(i); i++)
        wall_add(gfx_wallpaper_builtin(i), gfx_wallpaper_builtin(i), false);
    int first_file = s_nwalls;
    DIR *d = opendir(GFX_WALLPAPER_DIR);
    if (d)
    {
        struct dirent *e;
        while ((e = readdir(d)) && s_nwalls < MAXWALL - NCOLORS - 1)
        {
            size_t n = strlen(e->d_name);
            if (n < 5 || (strcasecmp(e->d_name + n - 4, ".ppm") && strcasecmp(e->d_name + n - 4, ".pam")))
                continue;
            char spec[96], name[32];
            snprintf(spec, sizeof(spec), "%s/%s", GFX_WALLPAPER_DIR, e->d_name);
            snprintf(name, sizeof(name), "%.*s", (int)(n - 4 < 31 ? n - 4 : 31), e->d_name);
            wall_add(spec, name, true);
        }
        closedir(d);
    }
    qsort(s_walls + first_file, (size_t)(s_nwalls - first_file), sizeof(s_walls[0]), by_name);
    for (int i = 0; i < NCOLORS; i++)
        wall_add(s_colors[i], "Colour", false);
    bool known = false;
    for (int i = 0; i < s_nwalls; i++)
        known |= !strcmp(s_walls[i].spec, s_set.wallpaper);
    if (!known)
    {
        const char *b = strrchr(s_set.wallpaper, '/');
        wall_add(s_set.wallpaper, b ? b + 1 : s_set.wallpaper, true);
    }
}

/* The picture of wallpaper @v for its tile (the files take a moment: one at a time) */
static void thumb_make(struct wall *v)
{
    v->thumb = (uint16_t *)malloc((size_t)s_thumb_w * (size_t)s_thumb_h * 2u);
    if (!v->thumb)
    {
        v->failed = true;
        return;
    }
    struct gfx_surface t = {v->thumb, s_thumb_w, s_thumb_h, s_thumb_w * 2, GPU2D_FMT_RGB565};
    if (gfx_wallpaper_draw(&t, v->spec, v->file ? GFX_FIT_FILL : s_set.fit))
    { /* (it holds the built-in picture drawn instead: the tile says so rather than show that) */
        free(v->thumb);
        v->thumb = NULL;
        v->failed = true;
    }
}

/* One tile still without a picture made; true if there was one */
static bool thumbs_pending(void)
{
    for (int i = 0; i < s_nwalls; i++)
        if (!s_walls[i].thumb && !s_walls[i].failed)
        {
            thumb_make(&s_walls[i]);
            return true;
        }
    return false;
}

static void thumbs_drop(void)
{
    for (int i = 0; i < s_nwalls; i++)
    {
        free(s_walls[i].thumb);
        s_walls[i].thumb = NULL;
        s_walls[i].failed = false;
    }
}

/* ---- drawing helpers ---------------------------------------------------------------------------- */

static void hit_add(const struct gfx_surface *cs, int x, int y, int ww, int hh, int id, int arg)
{
    if (s_nhits == MAXHITS || y + hh <= 0 || y >= cs->h) /* (scrolled away) */
        return;
    struct hit *h = &s_hits[s_nhits++];
    h->r.x = s_content.x + x;
    h->r.y = s_content.y + y;
    h->r.w = ww;
    h->r.h = hh;
    h->id = id;
    h->arg = arg;
}

static bool pressed(int id, int arg)
{
    return s_press_id == id && s_press_arg == arg && !s_dragging;
}

static int line_h(const GFXfont *f)
{
    return f->yAdvance;
}

/* A page title; returns the y below it */
static int heading(const struct gfx_surface *s, int x, int y, const char *text)
{
    const struct ui_theme *t = ui_theme;
    gfx_text(s, t->heading, x, y + gfx_font_ascent(t->heading) + ui_px(6), text, t->text);
    return y + line_h(t->heading) + ui_px(12);
}

/* A setting's name and what it does at (x, y); its height */
static int label(const struct gfx_surface *s, int x, int y, int width, const char *name, const char *desc)
{
    const struct ui_theme *t = ui_theme;
    ui_text_fit(s, t->font, x, y + gfx_font_ascent(t->font), width, name, t->text);
    int h = line_h(t->font);
    if (desc)
    {
        ui_text_fit(s, t->font, x, y + h + gfx_font_ascent(t->font), width, desc, t->text_dim);
        h += line_h(t->font);
    }
    return h;
}

/* The corners of a picture drawn at (x, y): rounded, @bg over what lies outside the arc (smooth) */
static void round_corners(const struct gfx_surface *s, int x, int y, int ww, int hh, int r, uint32_t bg)
{
    for (int c = 0; c < 4; c++)
    {
        int x0 = c & 1 ? x + ww - r : x, y0 = c & 2 ? y + hh - r : y;
        int cx = c & 1 ? x + ww - r : x + r, cy = c & 2 ? y + hh - r : y + r;
        for (int j = 0; j < r; j++)
            for (int i = 0; i < r; i++)
            {
                float dx = (float)(x0 + i - cx) + 0.5f, dy = (float)(y0 + j - cy) + 0.5f;
                float out = sqrtf(dx * dx + dy * dy) - (float)r + 0.5f;
                if (out > 0.0f)
                    gfx_blend_pixel(s, x0 + i, y0 + j, bg, out >= 1.0f ? 255u : (unsigned)(out * 255.0f));
            }
    }
}

static void card(const struct gfx_surface *s, int x, int y, int cw, int h)
{
    struct ui_rect r = {x, y, cw, h};
    ui_panel(s, &r, ui_theme->panel);
}

/* A card with a name, a description and a switch at its right */
static int switch_row(const struct gfx_surface *s, int x, int y, int cw, const char *name, const char *desc, bool on,
                      bool enabled, int id)
{
    int pad = ui_px(12), sw = ui_switch_width();
    int h = 2 * pad + 2 * line_h(ui_theme->font);
    card(s, x, y, cw, h);
    label(s, x + pad, y + pad, cw - 3 * pad - sw, name, desc);
    struct ui_rect r = {x + cw - pad - sw, y, sw, h};
    ui_switch(s, &r, on, enabled);
    if (enabled)
        hit_add(s, x, y, cw, h, id, 0);
    return h;
}

/* ---- pages ---------------------------------------------------------------------------------------- */

static int page_look(const struct gfx_surface *s, int x, int y, int cw)
{
    const struct ui_theme *t = ui_theme;
    int pad = ui_px(12), gap = ui_px(8), bh = ui_px(30);
    y = heading(s, x, y, "Appearance");

    /* scale */
    int h = 2 * pad + 2 * line_h(t->font) + gap + bh;
    card(s, x, y, cw, h);
    label(s, x + pad, y + pad, cw - 2 * pad, "Scale", "Size of text, title bars and controls");
    int by = y + pad + 2 * line_h(t->font) + gap, bw = (cw - 2 * pad - (UI_SCALES - 1) * gap) / UI_SCALES;
    for (int i = 0; i < UI_SCALES; i++)
    {
        char txt[8];
        snprintf(txt, sizeof(txt), "%d%%", ui_scales[i]);
        struct ui_rect r = {x + pad + i * (bw + gap), by, bw, bh};
        ui_choice(s, &r, txt, s_set.scale == ui_scales[i], pressed(H_SCALE, i));
        hit_add(s, r.x, r.y, r.w, r.h, H_SCALE, i);
    }
    y += h + gap;

    /* font family: each name in its own font */
    int cols = cw > ui_px(380) ? 2 : 1, rows = (s_nfam + cols - 1) / cols, fh = ui_px(34);
    h = 2 * pad + 2 * line_h(t->font) + gap + rows * fh + (rows - 1) * gap;
    card(s, x, y, cw, h);
    label(s, x + pad, y + pad, cw - 2 * pad, "Font", "The typeface of the whole interface");
    by = y + pad + 2 * line_h(t->font) + gap;
    int fw = (cw - 2 * pad - (cols - 1) * gap) / cols;
    for (int i = 0; i < s_nfam; i++)
    {
        struct ui_rect r = {x + pad + (i % cols) * (fw + gap), by + (i / cols) * (fh + gap), fw, fh};
        bool chosen = !strcmp(s_set.font, s_fam_ids[i]);
        ui_choice(s, &r, "", chosen, pressed(H_FONT, i));
        ui_text_center(s, &r, ui_family_font(s_fam_ids[i]), s_fam_names[i], chosen ? t->accent_text : t->button_text);
        hit_add(s, r.x, r.y, r.w, r.h, H_FONT, i);
    }
    y += h + gap;

    /* accent colour */
    int sz = ui_px(28);
    h = 2 * pad + line_h(t->font) + gap + sz;
    card(s, x, y, cw, h);
    label(s, x + pad, y + pad, cw - 2 * pad, "Accent colour", NULL);
    by = y + pad + line_h(t->font) + gap;
    int step = (cw - 2 * pad - sz) / (NACCENTS - 1);
    if (step > sz + ui_px(14))
        step = sz + ui_px(14);
    for (int i = 0; i < NACCENTS; i++)
    {
        int sx = x + pad + i * step, rad = t->radius ? sz / 2 : ui_px(3);
        uint32_t c = 0xFF000000u | s_accents[i];
        gfx_round_rect_aa(s, sx, by, sz, sz, rad, c, 0, GFX_CORNERS_ALL);
        if ((s_set.accent & 0xFFFFFFu) == s_accents[i])
        { /* chosen: a ring inside */
            int k = ui_px(4);
            gfx_round_rect_aa(s, sx + k, by + k, sz - 2 * k, sz - 2 * k, t->radius ? (sz - 2 * k) / 2 : ui_px(2),
                              GFX_WHITE, ui_px(2), GFX_CORNERS_ALL);
        }
        hit_add(s, sx - ui_px(3), by - ui_px(3), sz + ui_px(6), sz + ui_px(6), H_ACCENT, i);
    }
    y += h + gap;

    /* transparency and how opaque */
    y += switch_row(s, x, y, cw, "Transparency effects", "Task bar, menus and tooltips show what is behind them",
                    s_set.transparency, true, H_TRANSP) +
         gap;
    h = 2 * pad + line_h(t->font) + gap + ui_px(24);
    card(s, x, y, cw, h);
    int opacity = s_sliding ? 30 + s_slide * 70 / 1000 : s_set.opacity;
    char txt[24];
    snprintf(txt, sizeof(txt), "Opacity: %d %%", opacity);
    gfx_text(s, t->font, x + pad, y + pad + gfx_font_ascent(t->font), txt, s_set.transparency ? t->text : t->text_dim);
    struct ui_rect sr = {x + pad, y + pad + line_h(t->font) + gap, cw - 2 * pad, ui_px(24)};
    ui_slider(s, &sr, (opacity - 30) * 1000 / 70, s_set.transparency);
    s_slider = (struct ui_rect){s_content.x + sr.x, s_content.y + sr.y, sr.w, sr.h};
    if (s_set.transparency)
        hit_add(s, sr.x, sr.y - ui_px(6), sr.w, sr.h + ui_px(12), H_OPACITY, 0);
    y += h + gap;

    y += switch_row(s, x, y, cw, "Rounded corners", "Buttons, menus and title bars", s_set.rounded, true, H_ROUNDED);
    return y + pad;
}

static int page_wall(const struct gfx_surface *s, int x, int y, int cw)
{
    const struct ui_theme *t = ui_theme;
    int pad = ui_px(12), gap = ui_px(8), tw = s_thumb_w, th = s_thumb_h;
    y = heading(s, x, y, "Wallpaper");
    int cell_w = tw + gap, per = (cw - 2 * pad + gap) / cell_w;
    if (per < 1)
        per = 1;
    int cell_h = th + ui_px(4) + line_h(t->font) + gap, rows = (s_nwalls + per - 1) / per;
    int h = 2 * pad + rows * cell_h - gap;
    card(s, x, y, cw, h);
    int x0 = x + pad + (cw - 2 * pad - (per * cell_w - gap)) / 2;
    bool file_chosen = false;
    for (int i = 0; i < s_nwalls; i++)
    {
        struct wall *v = &s_walls[i];
        int tx = x0 + (i % per) * cell_w, ty = y + pad + (i / per) * cell_h;
        bool chosen = !strcmp(v->spec, s_set.wallpaper);
        file_chosen |= chosen && v->file;
        if (chosen)
        { /* a frame in the accent colour around it */
            int k = ui_px(3);
            gfx_round_rect_aa(s, tx - k, ty - k, tw + 2 * k, th + 2 * k, t->radius ? t->radius + k : 0, t->accent, 0,
                              GFX_CORNERS_ALL);
        }
        if (v->thumb)
        {
            for (int j = 0; j < th; j++)
            {
                int yy = ty + j;
                if (yy < 0 || yy >= s->h)
                    continue;
                uint16_t *dst = (uint16_t *)((uint8_t *)s->pix + (size_t)yy * (size_t)s->stride) + tx;
                int n = tx + tw > s->w ? s->w - tx : tw;
                if (tx >= 0 && n > 0)
                    memcpy(dst, v->thumb + (size_t)j * (size_t)tw, (size_t)n * 2u);
            }
            if (t->radius)
                round_corners(s, tx, ty, tw, th, t->radius, chosen ? t->accent : t->panel);
        }
        else
        {
            struct ui_rect r = {tx, ty, tw, th};
            ui_panel(s, &r, t->button);
            ui_text_center(s, &r, t->font, v->failed ? "cannot read" : "...", t->text_dim);
        }
        struct ui_rect nr = {tx, ty + th + ui_px(4), tw, line_h(t->font)};
        int nw = gfx_text_width(t->font, v->name);
        ui_text_fit(s, t->font, nw < tw ? tx + (tw - nw) / 2 : tx, nr.y + gfx_font_ascent(t->font), tw, v->name,
                    chosen ? t->text : t->text_dim);
        hit_add(s, tx, ty, tw, th + ui_px(4) + line_h(t->font), H_WALL, i);
    }
    y += h + gap;

    /* how a picture covers the screen */
    int bh = ui_px(30);
    h = 2 * pad + 2 * line_h(t->font) + gap + bh;
    card(s, x, y, cw, h);
    label(s, x + pad, y + pad, cw - 2 * pad, "Picture position",
          file_chosen ? "How the picture covers the screen" : "For pictures from the wallpapers folder");
    int by = y + pad + 2 * line_h(t->font) + gap, bw = (cw - 2 * pad - (GFX_FITS - 1) * gap) / GFX_FITS;
    for (int i = 0; i < GFX_FITS; i++)
    {
        struct ui_rect r = {x + pad + i * (bw + gap), by, bw, bh};
        ui_choice(s, &r, s_fit_names[i], file_chosen && s_set.fit == i, pressed(H_FIT, i));
        if (file_chosen)
            hit_add(s, r.x, r.y, r.w, r.h, H_FIT, i);
    }
    y += h + gap;
    ui_text_fit(s, t->font, x + ui_px(4), y + gfx_font_ascent(t->font), cw - ui_px(8),
                "Your pictures: PPM/PAM files in " GFX_WALLPAPER_DIR, t->text_dim);
    y += line_h(t->font);
    ui_text_fit(s, t->font, x + ui_px(4), y + gfx_font_ascent(t->font), cw - ui_px(8),
                "(\"crtos wallpaper PICTURE\" on the computer puts one there)", t->text_dim);
    return y + line_h(t->font) + pad;
}

static int page_clock(const struct gfx_surface *s, int x, int y, int cw)
{
    const struct ui_theme *t = ui_theme;
    int pad = ui_px(12), gap = ui_px(6), rh = ui_px(30), bw = ui_px(40);
    y = heading(s, x, y, "Date and time");
    int h = 2 * pad + NFIELDS * rh + (NFIELDS - 1) * gap;
    card(s, x, y, cw, h);
    int lw = ui_px(80);
    for (int f = 0; f < NFIELDS; f++)
    {
        int ry = y + pad + f * (rh + gap);
        gfx_text(s, t->font, x + pad, ry + (rh + gfx_font_ascent(t->font)) / 2, s_field_names[f], t->text_dim);
        struct ui_rect minus = {x + pad + lw, ry, bw, rh}, plus = {x + cw - pad - bw, ry, bw, rh};
        struct ui_rect vr = {minus.x + bw, ry, plus.x - minus.x - bw, rh};
        char v[40];
        field_value(f, v, sizeof(v));
        ui_text_center(s, &vr, f == F_ZONE ? t->font : t->bold, v, t->text);
        ui_button(s, &minus, f == F_ZONE ? "<" : "-", pressed(H_MINUS, f));
        ui_button(s, &plus, f == F_ZONE ? ">" : "+", pressed(H_PLUS, f));
        hit_add(s, minus.x, minus.y, minus.w, minus.h, H_MINUS, f);
        hit_add(s, plus.x, plus.y, plus.w, plus.h, H_PLUS, f);
    }
    y += h + ui_px(8);
    struct ui_rect sr = {x + cw - ui_px(130), y, ui_px(130), ui_px(32)};
    ui_button(s, &sr, "Set clock", pressed(H_SET_CLOCK, 0));
    hit_add(s, sr.x, sr.y, sr.w, sr.h, H_SET_CLOCK, 0);
    if (s_msg[0])
        ui_text_fit(s, t->font, x + ui_px(4), sr.y + (sr.h + gfx_font_ascent(t->font)) / 2, cw - sr.w - ui_px(12), s_msg,
                    t->accent);
    return sr.y + sr.h + pad;
}

static int page_system(const struct gfx_surface *s, int x, int y, int cw)
{
    const struct ui_theme *t = ui_theme;
    int pad = ui_px(12), gap = ui_px(8), lh = line_h(t->font);
    y = heading(s, x, y, "System");
    struct crtos_sysinfo si;
    crtos_sys_info(&si);
    uint32_t up = (uint32_t)(si.uptime_us / 1000000u);
    char lines[6][96];
    snprintf(lines[0], sizeof(lines[0]), "CRTOS - layered operating system for i.MX RT");
    snprintf(lines[1], sizeof(lines[1]), "Processor: Cortex-M7 at %lu MHz", (unsigned long)(si.cpu_hz / 1000000u));
    snprintf(lines[2], sizeof(lines[2]), "Memory: %lu of %lu MB free", (unsigned long)(si.mem_free >> 20),
             (unsigned long)(si.mem_total >> 20));
    snprintf(lines[3], sizeof(lines[3]), "Kernel heaps: %lu of %lu KB free", (unsigned long)(si.kmem_free >> 10),
             (unsigned long)(si.kmem_total >> 10));
    snprintf(lines[4], sizeof(lines[4]), "Display: %d x %d, processes: %lu, threads: %lu", gfx_screen_width(g),
             gfx_screen_height(g), (unsigned long)si.nprocs, (unsigned long)si.ntasks);
    snprintf(lines[5], sizeof(lines[5]), "Running for %lu:%02lu:%02lu", (unsigned long)(up / 3600u),
             (unsigned long)(up / 60u % 60u), (unsigned long)(up % 60u));
    int h = 2 * pad + 6 * lh;
    card(s, x, y, cw, h);
    for (int i = 0; i < 6; i++)
        ui_text_fit(s, i ? t->font : t->bold, x + pad, y + pad + i * lh + gfx_font_ascent(t->font), cw - 2 * pad,
                    lines[i], t->text);
    y += h + gap;
    int bh = ui_px(32);
    struct ui_rect wr = {x, y, cw, bh}, rr = {x, y + bh + gap, cw, bh};
    ui_button(s, &wr, "Restart the window manager", pressed(H_WM, 0));
    ui_button(s, &rr, "Reboot", pressed(H_REBOOT, 0));
    hit_add(s, wr.x, wr.y, wr.w, wr.h, H_WM, 0);
    hit_add(s, rr.x, rr.y, rr.w, rr.h, H_REBOOT, 0);
    y = rr.y + bh + gap;
    if (s_msg[0])
        ui_text_fit(s, t->font, x + ui_px(4), y + gfx_font_ascent(t->font), cw - ui_px(8), s_msg, t->accent);
    return y + lh + pad;
}

/* ---- the window ----------------------------------------------------------------------------------- */

static int tabs_h(void)
{
    return ui_px(40);
}

static void draw(void)
{
    const struct ui_theme *t = ui_theme;
    struct gfx_surface *s = &w->s;
    s_nhits = 0;
    gfx_fill(s, 0, 0, s->w, s->h, t->bg);
    /* tabs */
    int pad = ui_px(8), gap = ui_px(6), th = tabs_h();
    gfx_fill(s, 0, 0, s->w, th, t->panel);
    int tw = (s->w - 2 * pad - (NTABS - 1) * gap) / NTABS;
    for (int i = 0; i < NTABS; i++)
    {
        struct ui_rect r = {pad + i * (tw + gap), pad - ui_px(2), tw, th - 2 * pad + ui_px(4)};
        ui_choice(s, &r, s_tab_names[i], s_tab == i, pressed(H_TAB, i));
        if (s_nhits < MAXHITS)
            s_hits[s_nhits++] = (struct hit){r, H_TAB, i};
    }
    /* the page, scrolled, in a surface of its own (drawing there stays inside it) */
    s_content = (struct ui_rect){0, th, s->w, s->h - th};
    struct gfx_surface cs = *s;
    cs.pix = (uint8_t *)s->pix + (size_t)th * (size_t)s->stride;
    cs.h = s->h - th;
    int x = ui_px(14), cw = s->w - 2 * x, y = ui_px(6) - s_scroll;
    int end;
    switch (s_tab)
    {
    case TAB_LOOK:
        end = page_look(&cs, x, y, cw);
        break;
    case TAB_WALL:
        end = page_wall(&cs, x, y, cw);
        break;
    case TAB_CLOCK:
        end = page_clock(&cs, x, y, cw);
        break;
    default:
        end = page_system(&cs, x, y, cw);
        break;
    }
    s_page_h = end + s_scroll;
    int max = s_page_h - cs.h;
    if (max > 0)
    { /* where we are */
        int bh = cs.h * cs.h / s_page_h, by = (cs.h - bh) * s_scroll / max;
        gfx_round_rect_aa(&cs, cs.w - ui_px(4), by, ui_px(3), bh, t->radius ? ui_px(1) : 0, t->text_dim, 0,
                          GFX_CORNERS_ALL);
    }
    gfx_present(w);
}

static void clamp_scroll(void)
{
    int max = s_page_h - s_content.h;
    if (s_scroll > max)
        s_scroll = max;
    if (s_scroll < 0)
        s_scroll = 0;
}

/* Write the settings and tell gfxd (we follow on GFX_EV_SETTINGS, like everyone else) */
static void apply(void)
{
    if (ui_settings_save(&s_set))
    {
        snprintf(s_msg, sizeof(s_msg), "Cannot save the settings: %s", strerror(errno));
        return;
    }
    gfx_settings_changed(g);
}

static void activate(int id, int arg)
{
    s_msg[0] = 0;
    switch (id)
    {
    case H_TAB:
        if (s_tab != arg)
        {
            s_tab = arg;
            s_scroll = 0;
            if (s_tab == TAB_WALL)
                walls_scan();
        }
        break;
    case H_SCALE:
        s_set.scale = ui_scales[arg];
        apply();
        break;
    case H_FONT:
        snprintf(s_set.font, sizeof(s_set.font), "%s", s_fam_ids[arg]);
        apply();
        break;
    case H_ACCENT:
        s_set.accent = s_accents[arg];
        apply();
        break;
    case H_TRANSP:
        s_set.transparency = !s_set.transparency;
        apply();
        break;
    case H_ROUNDED:
        s_set.rounded = !s_set.rounded;
        apply();
        break;
    case H_WALL:
        snprintf(s_set.wallpaper, sizeof(s_set.wallpaper), "%s", s_walls[arg].spec);
        apply();
        break;
    case H_FIT:
        s_set.fit = arg;
        apply();
        break;
    case H_MINUS:
    case H_PLUS:
        change(arg, id == H_PLUS ? 1 : -1);
        break;
    case H_SET_CLOCK:
        set_clock();
        break;
    case H_WM:
        restart_wm();
        break;
    case H_REBOOT:
        snprintf(s_msg, sizeof(s_msg), "Rebooting...");
        draw();
        crtos_sleep_ms(300);
        if (crtos_reboot())
            snprintf(s_msg, sizeof(s_msg), "Reboot: %s", strerror(errno));
        break;
    default:
        break;
    }
}

static const struct hit *hit_at(int x, int y)
{
    for (int i = s_nhits - 1; i >= 0; i--)
        if (ui_inside(&s_hits[i].r, x, y))
            return &s_hits[i];
    return NULL;
}

static void pointer(const struct gfx_event *ev)
{
    const struct hit *h = hit_at(ev->x, ev->y);
    switch (ev->kind)
    {
    case GFX_PTR_DOWN:
        s_dragging = false;
        s_down_y = ev->y;
        s_down_scroll = s_scroll;
        s_press_id = h ? h->id : -1;
        s_press_arg = h ? h->arg : 0;
        if (h && h->id == H_OPACITY)
        {
            s_sliding = true;
            s_slide = ui_slider_value(&s_slider, ev->x);
        }
        draw();
        break;
    case GFX_PTR_MOVE:
        if (s_sliding)
        {
            s_slide = ui_slider_value(&s_slider, ev->x);
            draw();
        }
        else if (s_dragging || ev->y - s_down_y > ui_px(DRAG_PX) || s_down_y - ev->y > ui_px(DRAG_PX))
        {
            if (!s_dragging && ev->y < s_content.y)
                break; /* (on the tabs) */
            s_dragging = true;
            s_scroll = s_down_scroll - (ev->y - s_down_y);
            clamp_scroll();
            draw();
        }
        break;
    case GFX_PTR_UP:
        if (s_sliding)
        {
            s_sliding = false;
            s_set.opacity = 30 + ui_slider_value(&s_slider, ev->x) * 70 / 1000;
            apply();
        }
        else if (!s_dragging && s_press_id >= 0 && h && h->id == s_press_id && h->arg == s_press_arg)
        {
            activate(h->id, h->arg);
        }
        s_press_id = -1;
        s_dragging = false;
        draw();
        break;
    default:
        break;
    }
}

/* The window's size for the scale (on a small screen at most the screen above the task bar) */
static void default_size(int *ww, int *hh)
{
    int sw = gfx_screen_width(g), sh = gfx_screen_height(g);
    *ww = ui_px(BASE_W) < sw - ui_px(8) ? ui_px(BASE_W) : sw - ui_px(8);
    int maxh = sh - ui_px(26) - ui_px(20) - ui_px(8); /* (the task bar, a title bar) */
    *hh = ui_px(BASE_H) < maxh ? ui_px(BASE_H) : maxh;
}

static void thumb_size(void)
{
    int tw = ui_px(104), th = tw * gfx_screen_height(g) / gfx_screen_width(g);
    if (tw != s_thumb_w || th != s_thumb_h)
    {
        s_thumb_w = tw;
        s_thumb_h = th;
        thumbs_drop();
    }
}

/* The settings changed (libgfx follows them already): the window at the new scale */
static void restyle(void)
{
    s_set = ui_settings;
    thumb_size();
    if (s_set.scale != s_laid_scale)
    {
        s_laid_scale = s_set.scale;
        int ww, hh;
        default_size(&ww, &hh);
        gfx_win_resize(w, ww, hh);
        s_scroll = 0;
    }
    draw();
}

int main(void)
{
    g = gfx_open();
    if (!g)
    {
        printf("settings: no graphics server\n");
        return 1;
    }
    s_set = ui_settings;
    s_laid_scale = s_set.scale;
    s_nfam = ui_font_families(s_fam_ids, s_fam_names, MAXFAM);
    int ww, hh;
    default_size(&ww, &hh);
    w = gfx_win_create(g, -1, -1, ww, hh, GFX_WIN_RESIZABLE, "Settings");
    if (!w)
        return 1;
    thumb_size();
    load_zone();
    follow_clock();
    draw();
    for (;;)
    {
        bool busy = s_tab == TAB_WALL && thumbs_pending(); /* one tile's picture, then look again */
        if (busy)
            draw();
        struct gfx_event ev;
        if (gfx_next_event(g, &ev, busy ? 0 : 1000) != 0)
        {
            if (!busy && (s_tab == TAB_CLOCK || s_tab == TAB_SYS))
            { /* once a second: the clock moves on */
                if (!s_edited)
                    follow_clock();
                draw();
            }
            continue;
        }
        switch (ev.h.type)
        {
        case GFX_EV_CLOSE:
            gfx_close(g);
            return 0;
        case GFX_EV_POINTER:
            pointer(&ev);
            break;
        case GFX_EV_WHEEL:
            if (ev.code == GFX_WHEEL_VERTICAL)
            {
                s_scroll -= ev.value * ui_px(40);
                clamp_scroll();
                draw();
            }
            break;
        case GFX_EV_CONFIGURE:
            if (gfx_win_resize(w, ev.w, ev.hgt) >= 0)
            {
                clamp_scroll();
                draw();
            }
            break;
        case GFX_EV_SETTINGS:
            restyle();
            break;
        default:
            break;
        }
    }
}
