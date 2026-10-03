/*
 * files - file browser (layer 3): walk the directories, open programs (.app) and look into
 * files (text, or hex for binary data). It only reads: nothing is changed on the card.
 * The window can be resized; the list and the viewer scroll with a finger or the mouse wheel.
 * Sizes follow the interface scale (ui_px; the window grows or shrinks with it, GFX_EV_SETTINGS).
 */
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include "gfx.h"
#include "gfx_ui.h"

static int W, H;
#define MAXE 256
#define VIEW_MAX (48 * 1024)
#define VIEW_COLS 51

struct entry
{
    char name[64];
    bool dir;
    long long size;
};

static struct gfx *g;
static struct gfx_win *w;
static char s_path[256] = "/sd/crtos";
static struct entry *s_ent;
static int s_n;
static struct ui_list s_list;
static char s_msg[80];
static int s_pressed = -1; /* 0 up/back */

/* viewer */
static bool s_viewing;
static char *s_text; /* lines, each VIEW_COLS + 1 */
static int s_lines;
static char s_view_name[64];

static struct ui_rect r_up;
static int s_scale; /* the one the window has its size for */

static int head_h(void)
{
    return ui_px(26);
}

static int row_h(void)
{
    int h = ui_theme->font->yAdvance + ui_px(6);
    return h > ui_px(20) ? h : ui_px(20);
}

/* The bar, the list and the line at the bottom for the window's size */
static void layout(void)
{
    r_up = (struct ui_rect){ui_px(4), ui_px(3), ui_px(44), head_h() - ui_px(6)};
    s_list.r.x = 0;
    s_list.r.y = head_h() + 1;
    s_list.r.w = W;
    s_list.r.h = H - s_list.r.y - (ui_theme->font->yAdvance + ui_px(4));
}

static int cmp(const void *a, const void *b)
{
    const struct entry *x = a, *y = b;
    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void load_dir(void)
{
    s_n = 0;
    s_msg[0] = 0;
    DIR *d = opendir(s_path);
    if (!d)
    {
        snprintf(s_msg, sizeof(s_msg), "%.40s: %s", s_path, strerror(errno));
    }
    else
    {
        struct dirent *de;
        while ((de = readdir(d)) && s_n < MAXE)
        {
            if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
                continue;
            struct entry *e = &s_ent[s_n++];
            snprintf(e->name, sizeof(e->name), "%.63s", de->d_name);
            e->dir = de->d_type == DT_DIR;
            e->size = de->d_size;
        }
        closedir(d);
        qsort(s_ent, (size_t)s_n, sizeof(s_ent[0]), cmp);
        snprintf(s_msg, sizeof(s_msg), "%d item%s", s_n, s_n == 1 ? "" : "s");
    }
    s_list.count = s_n;
    s_list.top = 0;
    s_list.sel = -1;
}

static void human(long long v, char *buf, size_t n)
{
    if (v < 1024)
        snprintf(buf, n, "%ld B", (long)v);
    else if (v < 1024 * 1024)
        snprintf(buf, n, "%ld KB", (long)((v + 512) / 1024));
    else
        snprintf(buf, n, "%ld MB", (long)((v + 512 * 1024) / (1024 * 1024)));
}

static void icon(const struct gfx_surface *s, int x, int y, const struct entry *e, bool sel)
{
    if (e->dir)
    {
        uint32_t c = sel ? GFX_RGB(255, 230, 150) : GFX_RGB(230, 190, 80);
        int r = ui_theme->radius ? ui_px(2) : 0;
        gfx_round_rect_aa(s, x, y + ui_px(1), ui_px(6), ui_px(3), r, c, 0, GFX_CORNERS_TOP);
        gfx_round_rect_aa(s, x, y + ui_px(3), ui_px(14), ui_px(9), r, c, 0, GFX_CORNERS_ALL);
    }
    else
    {
        const char *dot = strrchr(e->name, '.');
        uint32_t c = dot && !strcasecmp(dot, ".app") ? GFX_RGB(110, 200, 130) : GFX_RGB(170, 180, 200);
        int l = ui_px(1) > 1 ? ui_px(1) : 1;
        gfx_round_rect_aa(s, x + ui_px(2), y, ui_px(10), ui_px(13), ui_theme->radius ? ui_px(2) : 0, c, 0, GFX_CORNERS_ALL);
        gfx_fill(s, x + ui_px(4), y + ui_px(3), ui_px(6), l, GFX_RGB(60, 70, 90));
        gfx_fill(s, x + ui_px(4), y + ui_px(6), ui_px(6), l, GFX_RGB(60, 70, 90));
        gfx_fill(s, x + ui_px(4), y + ui_px(9), ui_px(4), l, GFX_RGB(60, 70, 90));
    }
}

static void row(const struct gfx_surface *s, const struct ui_rect *r, int i, bool sel, void *ctx)
{
    (void)ctx;
    const struct ui_theme *t = ui_theme;
    const struct entry *e = &s_ent[i];
    icon(s, r->x + ui_px(6), r->y + (r->h - ui_px(13)) / 2, e, sel);
    char size[16] = "";
    if (!e->dir)
        human(e->size, size, sizeof(size));
    int sw = gfx_text_width(t->font, size), base = r->y + (r->h + gfx_font_ascent(t->font)) / 2;
    ui_text_fit(s, t->font, r->x + ui_px(28), base, r->w - ui_px(40) - sw, e->name, sel ? t->accent_text : t->text);
    gfx_text(s, t->font, r->x + r->w - ui_px(8) - sw, base, size, sel ? t->accent_text : t->text_dim);
}

static void view_row(const struct gfx_surface *s, const struct ui_rect *r, int i, bool sel, void *ctx)
{
    (void)sel;
    (void)ctx;
    const GFXfont *f = ui_theme->mono;
    gfx_text(s, f, r->x + ui_px(4), r->y + gfx_font_ascent(f) + 1, s_text + (size_t)i * (VIEW_COLS + 1), ui_theme->text);
}

static void draw(void)
{
    const struct ui_theme *t = ui_theme;
    struct gfx_surface *s = &w->s;
    gfx_fill(s, 0, 0, W, H, t->bg);
    gfx_fill(s, 0, 0, W, head_h(), t->panel);
    ui_button(s, &r_up, s_viewing ? "Back" : "Up", s_pressed == 0);
    int x = r_up.x + r_up.w + ui_px(8);
    ui_text_fit(s, t->bold, x, (head_h() + gfx_font_ascent(t->bold)) / 2, W - x - ui_px(8), s_viewing ? s_view_name : s_path,
                t->text);
    ui_list_draw(&s_list, s);
    int bottom = s_list.r.y + s_list.r.h;
    ui_text_fit(s, t->font, ui_px(6), bottom + (H - bottom + gfx_font_ascent(t->font)) / 2, W - ui_px(12), s_msg,
                t->text_dim);
    gfx_present(w);
}

static void join(char *out, size_t n, const char *dir, const char *name)
{
    if (!strcmp(dir, "/"))
        snprintf(out, n, "/%s", name);
    else
        snprintf(out, n, "%s/%s", dir, name);
}

static void go_up(void)
{
    char *slash = strrchr(s_path, '/');
    if (!slash)
        return;
    if (slash == s_path)
        s_path[1] = 0; /* "/" */
    else
        *slash = 0;
    load_dir();
}

static void set_list_files(void)
{
    s_list.row_h = row_h();
    s_list.draw_row = row;
    s_list.count = s_n;
}

static void close_view(void)
{
    free(s_text);
    s_text = NULL;
    s_viewing = false;
    set_list_files();
    s_list.top = 0;
    s_list.sel = -1;
    snprintf(s_msg, sizeof(s_msg), "%d item%s", s_n, s_n == 1 ? "" : "s");
}

/* Lay the file out as text lines (wrapped), or as a hex dump when it is not text */
static void open_view(const char *path, const char *name)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        snprintf(s_msg, sizeof(s_msg), "%s: %s", name, strerror(errno));
        return;
    }
    unsigned char *data = malloc(VIEW_MAX);
    int n = data ? (int)read(fd, data, VIEW_MAX) : -1;
    close(fd);
    if (n < 0)
    {
        snprintf(s_msg, sizeof(s_msg), "%s: %s", name, strerror(errno));
        free(data);
        return;
    }
    int odd = 0;
    for (int i = 0; i < n; i++)
        if (data[i] < 9 || (data[i] > 13 && data[i] < 32 && data[i] != 27))
            odd++;
    bool text = odd * 50 <= n;
    int max_lines = text ? n + 1 : (n + 7) / 8 + 1;
    s_text = malloc((size_t)max_lines * (VIEW_COLS + 1));
    if (!s_text)
    {
        free(data);
        snprintf(s_msg, sizeof(s_msg), "%s: too big", name);
        return;
    }
    s_lines = 0;
    if (text)
    {
        int col = 0;
        char *line = s_text;
        for (int i = 0; i <= n; i++)
        {
            int c = i < n ? data[i] : '\n';
            if (c == '\r')
                continue;
            if (c == '\t')
                c = ' ';
            if (c == '\n' || col == VIEW_COLS)
            {
                line[col] = 0;
                s_lines++;
                line = s_text + (size_t)s_lines * (VIEW_COLS + 1);
                col = 0;
                if (c == '\n')
                    continue;
            }
            line[col++] = (char)(c >= 32 && c < 127 ? c : '.');
        }
    }
    else
    {
        for (int off = 0; off < n; off += 8)
        {
            char *line = s_text + (size_t)s_lines++ * (VIEW_COLS + 1);
            int k = snprintf(line, VIEW_COLS + 1, "%06x ", off);
            for (int j = 0; j < 8; j++)
                k += snprintf(line + k, (size_t)(VIEW_COLS + 1 - k), off + j < n ? "%02x " : "   ",
                              off + j < n ? data[off + j] : 0);
            for (int j = 0; j < 8 && off + j < n; j++)
                line[k++] = isprint(data[off + j]) ? (char)data[off + j] : '.';
            line[k] = 0;
        }
    }
    free(data);
    s_viewing = true;
    snprintf(s_view_name, sizeof(s_view_name), "%s", name);
    snprintf(s_msg, sizeof(s_msg), "%d bytes%s, %d lines%s", n, n == VIEW_MAX ? " (the beginning)" : "", s_lines,
             text ? "" : ", hex");
    s_list.row_h = ui_theme->mono->yAdvance + 1;
    s_list.draw_row = view_row;
    s_list.count = s_lines;
    s_list.top = 0;
    s_list.sel = -1;
}

static void activate(int i)
{
    const struct entry *e = &s_ent[i];
    char path[320];
    join(path, sizeof(path), s_path, e->name);
    if (e->dir)
    {
        if (strlen(path) < sizeof(s_path))
        {
            snprintf(s_path, sizeof(s_path), "%s", path);
            load_dir();
        }
        return;
    }
    const char *dot = strrchr(e->name, '.');
    if (dot && !strcasecmp(dot, ".app"))
    {
        const char *argv[] = {e->name, NULL};
        struct crtos_spawn sp;
        memset(&sp, 0, sizeof(sp));
        sp.path = path;
        sp.argv = argv;
        sp.stdio[0] = sp.stdio[1] = sp.stdio[2] = -1;
        int pid = crtos_spawn(&sp);
        if (pid < 0)
            snprintf(s_msg, sizeof(s_msg), "%.40s: %s", e->name, strerror(errno));
        else
            snprintf(s_msg, sizeof(s_msg), "Started %.40s (pid %d)", e->name, pid);
        return;
    }
    open_view(path, e->name);
}

int main(int argc, char **argv)
{
    if (argc > 1)
        snprintf(s_path, sizeof(s_path), "%s", argv[1]);
    g = gfx_open();
    if (!g)
    {
        printf("files: no graphics server\n");
        return 1;
    }
    s_ent = calloc(MAXE, sizeof(*s_ent));
    s_scale = ui_theme->scale;
    W = ui_px(320);
    H = ui_px(222);
    if (W > gfx_screen_width(g))
        W = gfx_screen_width(g);
    if (H > gfx_screen_height(g) - ui_px(50))
        H = gfx_screen_height(g) - ui_px(50);
    w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_RESIZABLE, "Files");
    if (!w || !s_ent)
        return 1;
    layout();
    set_list_files();
    load_dir();
    draw();
    for (;;)
    {
        struct gfx_event ev;
        if (gfx_next_event(g, &ev, 1000) != 0)
        {
            int status;
            while (crtos_wait(-1, &status, 0) > 0)
            { /* programs started from here */
            }
            continue;
        }
        if (ev.h.type == GFX_EV_CLOSE)
            break;
        if (ev.h.type == GFX_EV_CONFIGURE && gfx_win_resize(w, ev.w, ev.hgt) == 0)
        {
            W = w->s.w;
            H = w->s.h;
            layout();
            ui_list_show(&s_list, s_list.sel);
            draw();
            continue;
        }
        if (ev.h.type == GFX_EV_SETTINGS)
        { /* another scale or font: the same window that much bigger or smaller, rows to fit */
            if (ui_theme->scale != s_scale)
            {
                int nw = W * ui_theme->scale / s_scale, nh = H * ui_theme->scale / s_scale;
                s_scale = ui_theme->scale;
                if (nw > gfx_screen_width(g))
                    nw = gfx_screen_width(g);
                if (nh > gfx_screen_height(g) - ui_px(50))
                    nh = gfx_screen_height(g) - ui_px(50);
                if (gfx_win_resize(w, nw, nh) >= 0)
                {
                    W = w->s.w;
                    H = w->s.h;
                }
            }
            layout();
            s_list.row_h = s_viewing ? ui_theme->mono->yAdvance + 1 : row_h();
            ui_list_show(&s_list, s_list.sel);
            draw();
            continue;
        }
        if (ev.h.type == GFX_EV_WHEEL)
        {
            if (ev.code == GFX_WHEEL_VERTICAL && ui_list_wheel(&s_list, ev.value) != UI_NONE)
                draw();
            continue;
        }
        if (ev.h.type != GFX_EV_POINTER)
            continue;
        if (ev.kind == GFX_PTR_DOWN && ui_inside(&r_up, ev.x, ev.y))
        {
            s_pressed = 0;
            draw();
            continue;
        }
        if (s_pressed == 0)
        {
            if (ev.kind == GFX_PTR_UP)
            {
                s_pressed = -1;
                if (ui_inside(&r_up, ev.x, ev.y))
                {
                    if (s_viewing)
                        close_view();
                    else
                        go_up();
                }
                draw();
            }
            continue;
        }
        int r = ui_list_pointer(&s_list, ev.kind, ev.x, ev.y);
        if (r == UI_ACTIVATED && !s_viewing && s_list.sel >= 0)
            activate(s_list.sel);
        if (r != UI_NONE)
            draw();
    }
    gfx_close(g);
    return 0;
}
