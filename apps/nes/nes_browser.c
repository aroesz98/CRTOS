/*
 * nes_browser.c - the cartridge browser of the NES program (nes_browser.h): the folders and
 * .nes files under /sd in the look of an 8-bit menu, with the header of the selected ROM on
 * an information line. Keys, the gamepad (through enum nav) and touch move through it.
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "nes_browser.h"
#include "nes_core.h"
#include "nes_draw.h"
#include "nes_font.h"

#define ROOT "/sd"
#define CONFIG NES_VAR_DIR "/nes.cfg"
#define MAX_ENTRIES 1024
#define NAME_LEN 128

/* the layout (the window is at least 400 x 200) */
#define HEADER_H 27
#define LIST_Y 31
#define ROW_H 12
#define FOOTER_H 32

struct entry {
    char name[NAME_LEN];
    bool dir;
    long size;
};

static struct entry s_ent[MAX_ENTRIES];
static int s_count, s_sel, s_top;
static char s_dir[256];
static char s_path[400];        /* the file chosen by BROWSER_OPEN */
static char s_error[80];        /* why the folder could not be read */

/* the header of the selected file */
static int s_info_for = -1;
static struct nes_rom_info s_info;
static const char *s_problem;

static bool s_popup, s_popup_wait;
static char s_popup_title[40], s_popup_text[240];

/* touch */
static bool s_touch_down, s_touch_armed, s_touch_drag;
static int s_touch_y0, s_touch_top0;
static int s_ok_x0, s_ok_x1, s_back_x0, s_back_x1; /* the hints "OPEN" and "BACK" */

static int s_w = 480, s_h = 226; /* of the last drawing */

static int rows(void)
{
    int n = (s_h - LIST_Y - FOOTER_H) / ROW_H;
    return n < 1 ? 1 : n;
}

static bool is_parent(const struct entry *e)
{
    return e->dir && !strcmp(e->name, "..");
}

/* ---- the folder ----------------------------------------------------------------------------- */

static int compare(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static bool is_rom(const char *name)
{
    size_t n = strlen(name);
    return n > 4 && !strcasecmp(name + n - 4, ".nes");
}

static void ensure_visible(void)
{
    int n = rows();
    if (s_sel < s_top)
        s_top = s_sel;
    if (s_sel >= s_top + n)
        s_top = s_sel - n + 1;
    if (s_top > s_count - n)
        s_top = s_count - n;
    if (s_top < 0)
        s_top = 0;
}

/* Reads s_dir; @select: the name to put the cursor on */
static void load_dir(const char *select)
{
    s_count = 0;
    s_error[0] = 0;
    if (strcmp(s_dir, ROOT))
    {
        strcpy(s_ent[0].name, "..");
        s_ent[0].dir = true;
        s_ent[0].size = 0;
        s_count = 1;
    }
    int first = s_count;
    DIR *d = opendir(s_dir);
    if (!d)
    {
        snprintf(s_error, sizeof(s_error), "CANNOT READ THE FOLDER: %s", strerror(errno));
    }
    else
    {
        struct dirent *de;
        while ((de = readdir(d)) && s_count < MAX_ENTRIES)
        {
            bool dir = de->d_type == DT_DIR;
            if (de->d_name[0] == '.' || !strcmp(de->d_name, "System Volume Information"))
                continue;
            if (!dir && !is_rom(de->d_name))
                continue;
            if (strlen(de->d_name) >= NAME_LEN)
                continue;
            struct entry *e = &s_ent[s_count++];
            strcpy(e->name, de->d_name);
            e->dir = dir;
            e->size = (long)de->d_size;
        }
        closedir(d);
        qsort(s_ent + first, (size_t)(s_count - first), sizeof(s_ent[0]), compare);
    }
    s_sel = 0;
    s_top = 0;
    s_info_for = -1;
    if (select)
        for (int i = 0; i < s_count; i++)
            if (!strcmp(s_ent[i].name, select))
                s_sel = i;
    ensure_visible();
}

static void enter(int i)
{
    const struct entry *e = &s_ent[i];
    if (is_parent(e))
    {
        char child[NAME_LEN];
        char *slash = strrchr(s_dir, '/');
        snprintf(child, sizeof(child), "%s", slash ? slash + 1 : "");
        if (slash && slash != s_dir)
            *slash = 0;
        load_dir(child);
        return;
    }
    size_t n = strlen(s_dir);
    if (n + 1 + strlen(e->name) >= sizeof(s_dir))
    {
        browser_popup("SORRY", "THE PATH OF THIS FOLDER IS TOO LONG", false);
        return;
    }
    snprintf(s_dir + n, sizeof(s_dir) - n, "/%s", e->name);
    load_dir(NULL);
}

/* reads the header of the selected file (once) */
static void update_info(void)
{
    if (s_info_for == s_sel || !s_count || s_ent[s_sel].dir)
        return;
    char path[400];
    snprintf(path, sizeof(path), "%s/%s", s_dir, s_ent[s_sel].name);
    s_problem = nes_rom_info(path, &s_info);
    s_info_for = s_sel;
}

/* ---- configuration -------------------------------------------------------------------------- */

void browser_init(const char *start)
{
    char file[NAME_LEN] = "";
    snprintf(s_dir, sizeof(s_dir), "%s", start ? start : ROOT);
    FILE *f = start ? NULL : fopen(CONFIG, "r");
    if (f)
    {
        char line[300];
        while (fgets(line, sizeof(line), f))
        {
            line[strcspn(line, "\r\n")] = 0;
            if (!strncmp(line, "dir=", 4))
                snprintf(s_dir, sizeof(s_dir), "%.255s", line + 4);
            else if (!strncmp(line, "file=", 5))
                snprintf(file, sizeof(file), "%.127s", line + 5);
        }
        fclose(f);
    }
    size_t n = strlen(s_dir);
    while (n > 1 && s_dir[n - 1] == '/')
        s_dir[--n] = 0;
    /* only the card, and a folder that is still there */
    bool ok = !strncmp(s_dir, ROOT, 3) && (s_dir[3] == 0 || s_dir[3] == '/') && !strstr(s_dir, "/..");
    DIR *d = ok ? opendir(s_dir) : NULL;
    if (d)
    {
        closedir(d);
    }
    else
    {
        strcpy(s_dir, ROOT);
        file[0] = 0;
    }
    load_dir(file[0] ? file : NULL);
}

void browser_remember(void)
{
    mkdir("/sd/crtos/var", 0755);
    mkdir(NES_VAR_DIR, 0755);
    FILE *f = fopen(CONFIG, "w");
    if (!f)
        return;
    fprintf(f, "dir=%s\n", s_dir);
    if (s_count && !s_ent[s_sel].dir)
        fprintf(f, "file=%s\n", s_ent[s_sel].name);
    fclose(f);
}

const char *browser_path(void)
{
    return s_path;
}

void browser_popup(const char *title, const char *text, bool wait)
{
    snprintf(s_popup_title, sizeof(s_popup_title), "%s", title);
    snprintf(s_popup_text, sizeof(s_popup_text), "%s", text);
    s_popup = true;
    s_popup_wait = wait;
}

void browser_popup_close(void)
{
    s_popup = false;
}

/* ---- input ---------------------------------------------------------------------------------- */

static enum browser_result open_selected(void)
{
    if (!s_count)
        return BROWSER_IDLE;
    if (s_ent[s_sel].dir)
    {
        enter(s_sel);
        return BROWSER_REDRAW;
    }
    update_info();
    if (s_problem)
    {
        browser_popup("CANNOT PLAY THIS GAME", s_problem, false);
        return BROWSER_REDRAW;
    }
    snprintf(s_path, sizeof(s_path), "%s/%s", s_dir, s_ent[s_sel].name);
    return BROWSER_OPEN;
}

enum browser_result browser_nav(enum nav nav)
{
    if (s_popup)
    {
        if (s_popup_wait || nav == NAV_NONE)
            return BROWSER_IDLE;
        s_popup = false;
        return BROWSER_REDRAW;
    }
    int old_sel = s_sel, old_top = s_top, page = rows();
    switch (nav)
    {
    case NAV_UP:
        if (s_sel > 0)
            s_sel--;
        break;
    case NAV_DOWN:
        if (s_sel < s_count - 1)
            s_sel++;
        break;
    case NAV_LEFT:
    case NAV_PAGE_UP:
        s_sel = s_sel > page ? s_sel - page : 0;
        break;
    case NAV_RIGHT:
    case NAV_PAGE_DOWN:
        s_sel = s_sel + page < s_count ? s_sel + page : s_count - 1;
        break;
    case NAV_HOME:
        s_sel = 0;
        break;
    case NAV_END:
        s_sel = s_count - 1;
        break;
    case NAV_OK:
        return open_selected();
    case NAV_BACK:
        if (s_count && is_parent(&s_ent[0]))
        {
            enter(0);
            return BROWSER_REDRAW;
        }
        return BROWSER_IDLE;
    default:
        return BROWSER_IDLE;
    }
    if (s_sel < 0)
        s_sel = 0;
    ensure_visible();
    return s_sel != old_sel || s_top != old_top ? BROWSER_REDRAW : BROWSER_IDLE;
}

enum browser_result browser_pointer(int kind, int x, int y)
{
    if (s_popup)
    {
        if (kind != GFX_PTR_DOWN || s_popup_wait)
            return BROWSER_IDLE;
        s_popup = false;
        s_touch_down = false;
        return BROWSER_REDRAW;
    }
    if (kind == GFX_PTR_DOWN)
    {
        s_touch_down = true;
        s_touch_armed = false;
        s_touch_drag = false;
        s_touch_y0 = y;
        s_touch_top0 = s_top;
        if (y < HEADER_H)
            return browser_nav(NAV_BACK);
        if (y >= s_h - 18)
        {
            if (x >= s_ok_x0 - 6 && x < s_ok_x1 + 6)
                return browser_nav(NAV_OK);
            if (x >= s_back_x0 - 6 && x < s_back_x1 + 6)
                return browser_nav(NAV_BACK);
            return BROWSER_IDLE;
        }
        int r = (y - LIST_Y) / ROW_H;
        if (y < LIST_Y || r >= rows() || s_top + r >= s_count)
            return BROWSER_IDLE;
        if (s_top + r == s_sel)
        {
            s_touch_armed = true; /* a tap on the selected line opens it */
            return BROWSER_IDLE;
        }
        s_sel = s_top + r;
        return BROWSER_REDRAW;
    }
    if (!s_touch_down)
        return BROWSER_IDLE;
    if (kind == GFX_PTR_MOVE)
    {
        int dy = y - s_touch_y0;
        if (!s_touch_drag && (dy > 8 || dy < -8))
        {
            s_touch_drag = true;
            s_touch_armed = false;
        }
        if (!s_touch_drag)
            return BROWSER_IDLE;
        int n = rows(), top = s_touch_top0 - dy / ROW_H;
        if (top > s_count - n)
            top = s_count - n;
        if (top < 0)
            top = 0;
        if (top == s_top)
            return BROWSER_IDLE;
        s_top = top;
        if (s_sel < s_top)
            s_sel = s_top;
        if (s_sel >= s_top + n)
            s_sel = s_top + n - 1;
        return BROWSER_REDRAW;
    }
    if (kind == GFX_PTR_UP)
    {
        s_touch_down = false;
        if (s_touch_armed)
        {
            s_touch_armed = false;
            return browser_nav(NAV_OK);
        }
    }
    return BROWSER_IDLE;
}

/* ---- drawing -------------------------------------------------------------------------------- */

/* @text cut to @max characters ("..." at the end) */
static const char *fit(const char *text, int max, char *buf, size_t size)
{
    if ((int)strlen(text) <= max || max < 4)
        return text;
    snprintf(buf, size, "%.*s...", max - 3, text);
    return buf;
}

static void draw_info(const struct gfx_surface *s, int x, int y)
{
    char text[96], chr[16];
    uint16_t colour = C_GREEN;
    if (!s_count)
        return;
    const struct entry *e = &s_ent[s_sel];
    if (e->dir)
    {
        draw_text(s, x, y, is_parent(e) ? "UP ONE FOLDER" : "FOLDER", C_GOLD, 1);
        return;
    }
    update_info();
    bool header = s_info.prg_kb || s_info.chr_kb || s_info.mapper;
    if (s_info.chr_kb)
        snprintf(chr, sizeof(chr), "%dK", s_info.chr_kb);
    else
        snprintf(chr, sizeof(chr), "RAM");
    if (s_problem && header)
    {
        snprintf(text, sizeof(text), "MAPPER %d %s: %s", s_info.mapper, s_info.mapper_name, s_problem);
        colour = C_RED;
    }
    else if (s_problem)
    {
        snprintf(text, sizeof(text), "%s", s_problem);
        colour = C_RED;
    }
    else
    {
        snprintf(text, sizeof(text), "MAPPER %d %s  PRG %dK  CHR %s  %s%s", s_info.mapper, s_info.mapper_name,
                 s_info.prg_kb, chr, s_info.pal ? "PAL" : "NTSC", s_info.battery ? "  SAVES" : "");
    }
    char buf[96];
    draw_text(s, x, y, fit(text, (s->w - 2 * x) / 8, buf, sizeof(buf)), colour, 1);
}

static int draw_hint(const struct gfx_surface *s, int x, int y, unsigned glyph, uint16_t colour, const char *text)
{
    if (glyph)
    {
        draw_char(s, x, y, glyph, colour, 1);
        x += 10;
    }
    return draw_text(s, x, y, text, C_GREY, 1) + 14;
}

/* the lines of @text at most @width characters long, broken at spaces */
static int wrap(const char *text, char lines[][48], int max_lines, int width)
{
    int n = 0;
    while (*text && n < max_lines)
    {
        int len = (int)strlen(text);
        if (len > width)
        {
            len = width;
            while (len > 0 && text[len] != ' ')
                len--;
            if (!len)
                len = width;
        }
        snprintf(lines[n++], 48, "%.*s", len, text);
        text += len;
        while (*text == ' ')
            text++;
    }
    return n;
}

static void draw_popup(const struct gfx_surface *s)
{
    char lines[5][48];
    int n = wrap(s_popup_text, lines, 5, 40);
    int chars = (int)strlen(s_popup_title);
    for (int i = 0; i < n; i++)
        if ((int)strlen(lines[i]) > chars)
            chars = (int)strlen(lines[i]);
    int bw = chars * 8 + 40, bh = 34 + n * 11 + (s_popup_wait ? 0 : 16);
    int bx = (s->w - bw) / 2, by = (s->h - bh) / 2;
    draw_dim(s, 3, 3, s->w - 6, s->h - 6);
    draw_frame(s, bx, by, bw, bh, C_WHITE, C_NAVY);
    draw_text_shadow(s, bx + (bw - text_width(s_popup_title, 1)) / 2, by + 10, s_popup_title, C_GOLD, 1);
    for (int i = 0; i < n; i++)
        draw_text(s, bx + (bw - text_width(lines[i], 1)) / 2, by + 26 + i * 11, lines[i], C_WHITE, 1);
    if (!s_popup_wait)
    {
        int x = bx + (bw - 42) / 2;
        draw_char(s, x, by + bh - 17, GLYPH_CROSS, C_SKY, 1);
        draw_text(s, x + 10, by + bh - 17, " OK", C_GREY, 1);
    }
}

void browser_draw(const struct gfx_surface *s, uint64_t now_us)
{
    char buf[128];
    s_w = s->w;
    s_h = s->h;
    int W = s->w, H = s->h, n = rows();
    ensure_visible();
    draw_frame(s, 0, 0, W, H, C_BLUE, C_BLACK);

    /* title and folder */
    draw_char(s, 10, 7, GLYPH_CART, C_GOLD, 2);
    draw_text_shadow(s, 30, 7, "NES", C_RED, 2);
    int max = (W - 100) / 8;
    size_t len = strlen(s_dir);
    const char *dir = (int)len > max ? s_dir + len - (max - 3) : s_dir;
    snprintf(buf, sizeof(buf), "%s%.120s", (int)len > max ? "..." : "", dir);
    draw_text(s, 90, 11, buf, C_SKY, 1);
    draw_fill(s, 4, HEADER_H, W - 8, 1, C_BLUE);

    /* the list */
    bool blink = (now_us / 400000u) & 1u;
    if (s_error[0])
        draw_text(s, (W - text_width(s_error, 1)) / 2, LIST_Y + 2 * ROW_H, s_error, C_RED, 1);
    else if (s_count == 0 || (s_count == 1 && is_parent(&s_ent[0])))
        draw_text(s, (W - text_width("NO GAMES IN THIS FOLDER", 1)) / 2, LIST_Y + 2 * ROW_H + (s_count ? ROW_H : 0),
                  "NO GAMES IN THIS FOLDER", C_DARK, 1);
    for (int r = 0; r < n && s_top + r < s_count; r++)
    {
        int i = s_top + r, y = LIST_Y + r * ROW_H;
        const struct entry *e = &s_ent[i];
        bool sel = i == s_sel;
        if (sel)
        {
            draw_fill(s, 6, y, W - 22, ROW_H - 1, C_NAVY);
            if (!blink)
                draw_char(s, 9, y + 2, GLYPH_CURSOR, C_WHITE, 1);
        }
        unsigned icon = is_parent(e) ? GLYPH_UP : e->dir ? GLYPH_FOLDER : GLYPH_CART;
        draw_char(s, 21, y + 2, icon, e->dir ? C_GOLD : sel ? C_RED : C_DARK, 1);

        char name[NAME_LEN], size[16];
        snprintf(name, sizeof(name), "%s", e->name);
        if (!e->dir)
            name[strlen(name) - 4] = 0; /* no ".nes" */
        if (is_parent(e))
            size[0] = 0;
        else if (e->dir)
            snprintf(size, sizeof(size), "<DIR>");
        else
            snprintf(size, sizeof(size), "%ldK", (e->size + 1023) / 1024);
        int size_x = W - 20 - text_width(size, 1);
        draw_text(s, 33, y + 2, fit(name, (size_x - 33) / 8 - 1, buf, sizeof(buf)),
                  e->dir ? C_GOLD : sel ? C_WHITE : C_GREY, 1);
        draw_text(s, size_x, y + 2, size, sel ? C_GREY : C_DARK, 1);
    }
    if (s_count > n)
    {
        int track = n * ROW_H - 1, thumb = track * n / s_count;
        if (thumb < 6)
            thumb = 6;
        int ty = LIST_Y + (track - thumb) * s_top / (s_count - n);
        draw_fill(s, W - 12, LIST_Y, 2, track, C_NAVY);
        draw_fill(s, W - 12, ty, 2, thumb, C_GREY);
    }

    /* information and hints */
    draw_fill(s, 4, H - FOOTER_H, W - 8, 1, C_BLUE);
    draw_info(s, 10, H - FOOTER_H + 5);
    int x = 10, y = H - 14;
    s_ok_x0 = x;
    x = draw_hint(s, x, y, GLYPH_CROSS, C_SKY, "OPEN");
    s_ok_x1 = x - 14;
    s_back_x0 = x;
    x = draw_hint(s, x, y, GLYPH_CIRCLE, C_RED, "BACK");
    s_back_x1 = x - 14;
    x = draw_hint(s, x, y, GLYPH_DPAD, C_GREY, "MOVE");
    draw_hint(s, x, y, 0, 0, "L1/R1 PAGE");
    const char *menu = "PS: GAME MENU";
    draw_text(s, W - 10 - text_width(menu, 1), y, menu, C_DARK, 1);

    if (s_popup)
        draw_popup(s);
}
