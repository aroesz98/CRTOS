/*
 * term - graphical terminal (layer 3): runs the shell over two terminal pipes (PIPE_TTY)
 * and shows its output with a VT100 subset (cursor movement, erasing, colours, the cursor's
 * shape), typed on an on-screen keyboard or a real one.
 *
 * The pipes answer the terminal ioctls, so the shell and its programs see a terminal: in
 * canonical mode (the default) this program edits the line (echo, backspace) and sends it
 * whole on Enter; in raw mode every key goes straight through (the shell edits its command
 * line so, with history and cursor keys). Ctrl-C ends the foreground process the shell
 * registered (TTY_IOC_SET_FG).
 *
 * Copy and paste go through the system clipboard (gfxd): drag with the mouse to select text
 * (a double click selects a word), Ctrl-C copies it when something is selected (else it
 * interrupts as always), Ctrl-V pastes; Ctrl-Shift-C / Ctrl-Shift-V and Ctrl-Insert /
 * Shift-Insert do the same. The mouse wheel scrolls back (200 lines); a finger drags them, and
 * a tap hides or shows the keyboard. The window can be resized (the rows and columns follow;
 * too low a window hides the keyboard). The characters are the theme's monospaced font at the
 * interface scale: a cell is one of its characters (GFX_EV_SETTINGS: the cells, rows and
 * columns anew).
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include <crtos/tty.h>
#include "gfx.h"
#include "gfx_ui.h"

#define COLS_MAX 80
#define HISTORY 200 /* lines kept, the screen included */
#define CELL_W s_cell_w
#define CELL_H s_cell_h
#define KBD_H s_kbd_h
#define PAD 2
#define WHEEL_LINES 3
#define DOUBLE_CLICK_MS 400u

static const GFXfont *s_font;            /* the theme's monospaced font */
static int s_cell_w, s_cell_h, s_glyph_y; /* a character's cell, its baseline in it */
static int s_kbd_h;

/* The cells for the theme's monospaced font */
static void cells(void)
{
    s_font = ui_theme->mono;
    s_cell_w = s_font->glyph['M' - s_font->first].xAdvance;
    s_cell_h = s_font->yAdvance;
    int below = 0; /* how far the lowest glyph goes under the baseline */
    for (unsigned c = s_font->first; c <= s_font->last; c++)
    {
        const GFXglyph *gl = &s_font->glyph[c - s_font->first];
        if (gl->height && gl->yOffset + gl->height > below)
            below = gl->yOffset + gl->height;
    }
    s_glyph_y = s_cell_h - below;
    s_kbd_h = ui_px(100);
}

struct cell
{
    char ch;
    uint8_t attr; /* bits 0-3 foreground colour, 4-6 background, 7 unused */
};

static const uint32_t s_palette[16] = {
    GFX_RGB(0, 0, 0),
    GFX_RGB(205, 49, 49),
    GFX_RGB(13, 188, 121),
    GFX_RGB(229, 229, 16),
    GFX_RGB(36, 114, 200),
    GFX_RGB(188, 63, 188),
    GFX_RGB(17, 168, 205),
    GFX_RGB(204, 204, 204),
    GFX_RGB(102, 102, 102),
    GFX_RGB(241, 76, 76),
    GFX_RGB(35, 209, 139),
    GFX_RGB(245, 245, 67),
    GFX_RGB(59, 142, 234),
    GFX_RGB(214, 112, 214),
    GFX_RGB(41, 184, 219),
    GFX_RGB(240, 240, 240),
};
#define ATTR_DEFAULT 0x07u
#define BG_DEFAULT GFX_RGB(12, 14, 20)
#define SEL_BG GFX_RGB(52, 120, 220)
#define SEL_FG GFX_RGB(255, 255, 255)
#define CURSOR_COLOR GFX_RGB(230, 230, 120)

static struct gfx *g;
static struct gfx_win *w;
static int W, H;
static int s_cols, s_rows;
static bool s_kbd_shown = true;
static struct ui_keyboard s_kbd;
static struct ui_keys s_keys; /* key events from a keyboard or the system's on-screen one */

static struct cell s_lines[HISTORY][COLS_MAX];
static int s_base;     /* history index of screen row 0 */
static int s_hist;     /* lines above the screen */
static long s_abs0;    /* number of screen row 0 counted from the first line ever (selection) */
static int s_cx, s_cy; /* cursor */
static uint8_t s_attr = ATTR_DEFAULT;
static bool s_block;     /* block cursor (ESC [ 2 q), else a line under the character */
static int s_view;       /* lines scrolled back (0: live) */
static uint32_t s_dirty; /* screen rows to redraw (bit per row, row 31+ with it) */
static bool s_all_dirty = true;

/* escape sequences */
static int s_esc; /* 0 none, 1 ESC seen, 2 CSI */
static int s_par[8], s_npar;
static bool s_priv;
static char s_inter; /* intermediate byte of a CSI sequence (' ' in ESC [ n SP q) */

/* the shell */
static int s_in = -1, s_out = -1; /* our ends: its input, its output */
static int s_pid = -1;
static char s_edit[256]; /* line being typed (canonical mode) */
static int s_elen;
static bool s_ended;

/* touch on the text */
static int s_down_y = -1, s_down_view;
static bool s_dragged;

/* selection with the mouse: cells from the anchor to the end (lines counted as s_abs0) */
struct pos
{
    long line;
    int col;
};
static struct pos s_sa, s_se;
static bool s_sel;       /* something is selected */
static bool s_selecting; /* the mouse button is down on the text */
static bool s_sel_had;   /* a selection was there when it went down (a click only clears it) */
static uint32_t s_click_ms;
static struct pos s_click;

/* ---- screen memory ------------------------------------------------------------------------- */

static struct cell *line_at(int row) /* screen row, live view */
{
    return s_lines[(s_base + row) % HISTORY];
}

/* a line by its number, NULL when it is no longer kept (or not there yet) */
static struct cell *line_abs(long line)
{
    long d = line - s_abs0;
    if (d < -s_hist || d >= s_rows)
        return NULL;
    return s_lines[((s_base + d) % HISTORY + HISTORY) % HISTORY];
}

static void mark(int row)
{
    if (row >= 31)
        s_all_dirty = true;
    else
        s_dirty |= 1u << row;
}

static void clear_cells(struct cell *c, int from, int to)
{
    for (int i = from; i < to && i < COLS_MAX; i++)
    {
        c[i].ch = ' ';
        c[i].attr = s_attr & 0x70u ? s_attr : ATTR_DEFAULT;
    }
}

static void scroll_up(void)
{
    s_base = (s_base + 1) % HISTORY;
    s_abs0++;
    if (s_hist < HISTORY - s_rows)
        s_hist++;
    clear_cells(line_at(s_rows - 1), 0, COLS_MAX);
    s_all_dirty = true;
    if (s_view) /* keep looking at the same lines */
        s_view = s_view + 1 > s_hist ? s_hist : s_view + 1;
}

static void newline(void)
{
    if (s_cy == s_rows - 1)
        scroll_up();
    else
        s_cy++;
}

static void put_char(char ch)
{
    if (s_cx >= s_cols)
    { /* wrap */
        s_cx = 0;
        newline();
    }
    struct cell *c = &line_at(s_cy)[s_cx];
    c->ch = ch;
    c->attr = s_attr;
    mark(s_cy);
    s_cx++;
}

/* ---- escape sequences ------------------------------------------------------------------------ */

static int par(int i, int def)
{
    return i < s_npar && s_par[i] > 0 ? s_par[i] : def;
}

static void sgr(void)
{
    if (!s_npar)
    {
        s_attr = ATTR_DEFAULT;
        return;
    }
    for (int i = 0; i < s_npar; i++)
    {
        int p = s_par[i];
        if (p == 0)
            s_attr = ATTR_DEFAULT;
        else if (p == 1)
            s_attr |= 0x08u; /* bold: the bright colour */
        else if (p == 22)
            s_attr &= ~0x08u;
        else if (p == 7 || p == 27) /* reverse on / off: swap the colours */
            s_attr = (uint8_t)((s_attr >> 4) | (s_attr << 4));
        else if (p >= 30 && p <= 37)
            s_attr = (uint8_t)((s_attr & 0xF8u) | (p - 30));
        else if (p == 39)
            s_attr = (uint8_t)((s_attr & 0xF0u) | 0x07u);
        else if (p >= 40 && p <= 47)
            s_attr = (uint8_t)((s_attr & 0x0Fu) | ((p - 40) << 4));
        else if (p == 49)
            s_attr &= 0x0Fu;
        else if (p >= 90 && p <= 97)
            s_attr = (uint8_t)((s_attr & 0xF0u) | (p - 90 + 8));
    }
}

static void csi(char f)
{
    if (s_inter)
    { /* ESC [ n SP q: the cursor's shape (0-2 a block, 3-6 a line: ours is under the character) */
        if (s_inter == ' ' && f == 'q')
        {
            s_block = s_npar && s_par[0] >= 1 && s_par[0] <= 2;
            mark(s_cy);
        }
        return;
    }
    switch (f)
    {
    case 'A':
        s_cy -= par(0, 1);
        break;
    case 'B':
        s_cy += par(0, 1);
        break;
    case 'C':
        s_cx += par(0, 1);
        break;
    case 'D':
        s_cx -= par(0, 1);
        break;
    case 'G':
        s_cx = par(0, 1) - 1;
        break;
    case 'H':
    case 'f':
        s_cy = par(0, 1) - 1;
        s_cx = par(1, 1) - 1;
        break;
    case 'J':
    {
        int m = s_npar ? s_par[0] : 0;
        if (m == 3)
        { /* the lines kept above the screen */
            s_hist = 0;
            s_view = 0;
            s_sel = false;
            s_all_dirty = true;
            break;
        }
        int from = m == 0 ? s_cy + 1 : 0, to = m == 1 ? s_cy : s_rows;
        if (m == 0)
            clear_cells(line_at(s_cy), s_cx, COLS_MAX);
        if (m == 1)
            clear_cells(line_at(s_cy), 0, s_cx + 1);
        for (int r = from; r < to; r++)
            clear_cells(line_at(r), 0, COLS_MAX);
        s_all_dirty = true;
        break;
    }
    case 'K':
    {
        int m = s_npar ? s_par[0] : 0;
        clear_cells(line_at(s_cy), m == 0 ? s_cx : 0, m == 1 ? s_cx + 1 : COLS_MAX);
        mark(s_cy);
        break;
    }
    case 'm':
        sgr();
        break;
    default:
        break; /* others are ignored */
    }
    if (s_cx < 0)
        s_cx = 0;
    if (s_cx > s_cols - 1)
        s_cx = s_cols - 1;
    if (s_cy < 0)
        s_cy = 0;
    if (s_cy > s_rows - 1)
        s_cy = s_rows - 1;
}

static void term_write(const char *p, int n)
{
    int old_cy = s_cy;
    for (int i = 0; i < n; i++)
    {
        char c = p[i];
        if (s_esc == 1)
        {
            s_esc = c == '[' ? 2 : 0;
            s_npar = 0;
            s_priv = false;
            s_inter = 0;
            memset(s_par, 0, sizeof(s_par));
            continue;
        }
        if (s_esc == 2)
        {
            if (c >= '0' && c <= '9')
            {
                if (!s_npar)
                    s_npar = 1;
                s_par[s_npar - 1] = s_par[s_npar - 1] * 10 + (c - '0');
            }
            else if (c == ';')
            {
                if (!s_npar)
                    s_npar = 1;
                if (s_npar < 8)
                    s_npar++;
            }
            else if (c == '?')
            {
                s_priv = true;
            }
            else if (c >= 0x20 && c <= 0x2F)
            {
                s_inter = c;
            }
            else if (c >= 0x40 && c <= 0x7E)
            {
                if (!s_priv)
                    csi(c);
                s_esc = 0;
            }
            continue;
        }
        switch (c)
        {
        case 27:
            s_esc = 1;
            break;
        case '\n':
            s_cx = 0;
            newline();
            break; /* no output processing: LF is CR LF */
        case '\r':
            s_cx = 0;
            break;
        case '\b':
            if (s_cx > 0)
                s_cx--;
            break;
        case '\t':
            do
            {
                put_char(' ');
            } while (s_cx % 8 && s_cx < s_cols);
            break;
        case 7:
            break;
        default:
            if ((unsigned char)c >= 32)
                put_char(c);
            break;
        }
    }
    mark(old_cy);
    mark(s_cy);
    s_view = s_view > s_hist ? s_hist : s_view;
}

/* ---- selection ------------------------------------------------------------------------------------ */

static bool pos_before(struct pos a, struct pos b)
{
    return a.line < b.line || (a.line == b.line && a.col < b.col);
}

/* the selection in reading order */
static void sel_range(struct pos *a, struct pos *b)
{
    *a = pos_before(s_se, s_sa) ? s_se : s_sa;
    *b = pos_before(s_se, s_sa) ? s_sa : s_se;
}

static bool selected(long line, int col)
{
    if (!s_sel)
        return false;
    struct pos a, b;
    sel_range(&a, &b);
    if (line < a.line || line > b.line)
        return false;
    if (line == a.line && col < a.col)
        return false;
    if (line == b.line && col > b.col)
        return false;
    return true;
}

static void sel_clear(void)
{
    if (s_sel)
        s_all_dirty = true;
    s_sel = false;
}

/* The selected text: each line without its trailing spaces, lines joined by '\n' */
static void copy_selection(void)
{
    if (!s_sel)
        return;
    struct pos a, b;
    sel_range(&a, &b);
    if (a.line < s_abs0 - s_hist)
    { /* its start scrolled out of the history */
        a.line = s_abs0 - s_hist;
        a.col = 0;
    }
    size_t cap = (size_t)(b.line - a.line + 1) * (COLS_MAX + 1) + 1, n = 0;
    char *text = cap <= GFX_CLIP_MAX + COLS_MAX ? (char *)malloc(cap) : NULL;
    if (!text)
        return;
    for (long l = a.line; l <= b.line; l++)
    {
        const struct cell *c = line_abs(l);
        int from = l == a.line ? a.col : 0, to = l == b.line ? b.col : s_cols - 1;
        size_t start = n;
        for (int x = from; c && x <= to && x < COLS_MAX; x++)
            text[n++] = c[x].ch ? c[x].ch : ' ';
        while (n > start && text[n - 1] == ' ')
            n--;
        if (l != b.line)
            text[n++] = '\n';
    }
    if (n > GFX_CLIP_MAX)
        n = GFX_CLIP_MAX;
    gfx_clip_set(g, text, n);
    free(text);
    sel_clear();
}

/* ---- drawing ----------------------------------------------------------------------------------- */

static int text_h(void)
{
    return H - (s_kbd_shown ? KBD_H : 0);
}

static void draw_row(int row)
{
    struct gfx_surface *s = &w->s;
    int y = PAD + row * CELL_H;
    gfx_fill(s, 0, y, W, CELL_H, BG_DEFAULT);
    int idx = (s_base - s_view + row + HISTORY * 2) % HISTORY;
    if (row - s_view < -s_hist) /* nothing that far back */
        return;
    long line = s_abs0 - s_view + row;
    const struct cell *c = s_lines[idx];
    char run[COLS_MAX + 1];
    for (int x = 0; x < s_cols;)
    {
        uint8_t a = c[x].attr;
        bool sel = selected(line, x);
        int n = 0;
        while (x + n < s_cols && c[x + n].attr == a && selected(line, x + n) == sel)
        {
            run[n] = c[x + n].ch ? c[x + n].ch : ' ';
            n++;
        }
        run[n] = 0;
        if (sel || (a & 0x70u))
            gfx_fill(s, PAD + x * CELL_W, y, n * CELL_W, CELL_H, sel ? SEL_BG : s_palette[(a >> 4) & 7u]);
        gfx_text(s, s_font, PAD + x * CELL_W, y + s_glyph_y, run, sel ? SEL_FG : s_palette[a & 15u]);
        x += n;
    }
    if (!s_view && row == s_cy && !s_ended)
    { /* cursor */
        if (s_block)
        {
            char ch[2] = {c[s_cx].ch ? c[s_cx].ch : ' ', 0};
            gfx_fill(s, PAD + s_cx * CELL_W, y, CELL_W, CELL_H, CURSOR_COLOR);
            gfx_text(s, s_font, PAD + s_cx * CELL_W, y + s_glyph_y, ch, BG_DEFAULT);
        }
        else
        {
            gfx_fill(s, PAD + s_cx * CELL_W, y + CELL_H - 2, CELL_W, 2, CURSOR_COLOR);
        }
    }
}

static void redraw(void)
{
    struct gfx_surface *s = &w->s;
    int top = -1, bottom = -1;
    for (int r = 0; r < s_rows; r++)
    {
        if (!s_all_dirty && !(r < 31 && (s_dirty & (1u << r))))
            continue;
        draw_row(r);
        if (top < 0)
            top = r;
        bottom = r;
    }
    if (s_all_dirty)
    { /* the strip below the last row */
        int y = PAD + s_rows * CELL_H;
        gfx_fill(s, 0, 0, W, PAD, BG_DEFAULT);
        gfx_fill(s, 0, y, W, text_h() - y, BG_DEFAULT);
        if (s_view)
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "-%d", s_view);
            gfx_text(s, &gfx_tiny, W - 30, 8, buf, GFX_RGB(230, 200, 90));
        }
        gfx_damage(w, 0, 0, W, text_h());
    }
    else if (top >= 0)
    {
        gfx_damage(w, 0, PAD + top * CELL_H, W, (bottom - top + 1) * CELL_H);
    }
    s_dirty = 0;
    s_all_dirty = false;
}

static void draw_keyboard(void)
{
    if (!s_kbd_shown)
        return;
    ui_keyboard_draw(&s_kbd, &w->s);
    gfx_damage(w, s_kbd.r.x, s_kbd.r.y, s_kbd.r.w, s_kbd.r.h);
}

static void layout(void)
{
    if (H < KBD_H + 3 * CELL_H) /* no room for the keyboard */
        s_kbd_shown = false;
    int rows = (text_h() - PAD) / CELL_H;
    if (rows < 1)
        rows = 1;
    if (s_rows && rows != s_rows)
    { /* keep the bottom line where it is */
        int d = rows - s_rows;
        if (d > 0)
        {
            int back = d < s_hist ? d : s_hist;
            s_base = (s_base - back + HISTORY) % HISTORY;
            s_abs0 -= back;
            s_hist -= back;
            s_cy += back;
            for (int r = s_rows + back; r < rows; r++)
                clear_cells(s_lines[(s_base + r) % HISTORY], 0, COLS_MAX);
        }
        else
        {
            int up = s_cy - (rows - 1) > 0 ? s_cy - (rows - 1) : 0;
            s_base = (s_base + up) % HISTORY;
            s_abs0 += up;
            s_hist = s_hist + up > HISTORY - rows ? HISTORY - rows : s_hist + up;
            s_cy -= up;
        }
    }
    s_rows = rows;
    s_cols = (W - 2 * PAD) / CELL_W;
    if (s_cols > COLS_MAX)
        s_cols = COLS_MAX;
    s_kbd.r.x = 0;
    s_kbd.r.y = H - KBD_H;
    s_kbd.r.w = W;
    s_kbd.r.h = KBD_H;
    s_kbd.corner = ui_px(26); /* the window manager's resize grip sits there */
    s_kbd.pressed = -1;
    if (s_in >= 0)
    {
        struct tty_size sz = {(uint16_t)s_cols, (uint16_t)s_rows};
        ioctl(s_in, TTY_IOC_SET_SIZE, &sz);
    }
    s_all_dirty = true;
}

/* ---- the shell -------------------------------------------------------------------------------- */

static int start_shell(void)
{
    int in[2], out[2];
    if (crtos_pipe(in, PIPE_TTY) || crtos_pipe(out, PIPE_TTY))
    {
        printf("term: pipe: %s\n", strerror(errno));
        return -1;
    }
    const char *argv[] = {"sh", NULL};
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = "/sd/crtos/bin/sh.app";
    sp.argv = argv;
    sp.stdio[0] = in[0];
    sp.stdio[1] = out[1];
    sp.stdio[2] = out[1];
    struct crtos_procinfo pi;
    sp.caps = 0;
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++)
        if (pi.pid == getpid())
            sp.caps = pi.caps;
    s_pid = crtos_spawn(&sp);
    close(in[0]); /* the shell has its own copies */
    close(out[1]);
    if (s_pid < 0)
    {
        printf("term: cannot start the shell: %s\n", strerror(errno));
        return -1;
    }
    s_in = in[1];
    s_out = out[0];
    fcntl(s_out, F_SETFL, O_NONBLOCK);
    return 0;
}

static uint32_t tty_mode(void)
{
    uint32_t m = TTY_MODE_CANON | TTY_MODE_ECHO;
    ioctl(s_in, TTY_IOC_GET_MODE, &m);
    return m;
}

static void send(const char *p, int n)
{
    if (s_in >= 0 && n > 0 && write(s_in, p, (size_t)n) < 0 && errno == EPIPE)
        s_ended = true;
}

static void interrupt(void)
{
    int32_t fg = 0;
    ioctl(s_in, TTY_IOC_GET_FG, &fg);
    term_write("^C\n", 3);
    s_elen = 0;
    if (fg > 0)
        crtos_kill(fg, -4); /* -EINTR: "interrupted" */
    else
        send("\n", 1); /* a fresh prompt */
}

/* A key in the terminal mode @mode */
static void key_mode(int k, uint32_t mode)
{
    if (s_ended)
        return;
    s_view = 0;
    if (k == 3 && (mode & TTY_MODE_CANON))
    { /* (in raw mode the program gets Ctrl-C itself, as from the kernel's terminal) */
        interrupt();
        return;
    }
    if (!(mode & TTY_MODE_CANON))
    { /* raw: straight through, other keys as VT100/xterm sequences */
        static const char *const seqs[] = {"\033[A", "\033[B", "\033[D", "\033[C", "\033[H",
                                           "\033[F", "\033[5~", "\033[6~", "\033[3~"};
        const char *seq = NULL;
        if (k == UI_KEY_LEFT && s_keys.ctrl)
            seq = "\033[1;5D"; /* a word back */
        else if (k == UI_KEY_RIGHT && s_keys.ctrl)
            seq = "\033[1;5C";
        else if (k >= UI_KEY_UP && k <= UI_KEY_DELETE)
            seq = seqs[k - UI_KEY_UP];
        else if (k == UI_KEY_INSERT)
            seq = "\033[2~";
        if (seq)
        {
            send(seq, (int)strlen(seq));
        }
        else if (k < 0x100)
        {
            char c = k == '\n' ? '\r' : k == '\b' ? 127
                                                  : (char)k;
            send(&c, 1);
            if (mode & TTY_MODE_ECHO)
                term_write(&c, 1);
        }
        return;
    }
    switch (k)
    {
    case '\n':
        s_edit[s_elen++] = '\n';
        term_write("\n", 1);
        send(s_edit, s_elen);
        s_elen = 0;
        break;
    case '\b':
        if (s_elen)
        {
            s_elen--;
            term_write("\b \b", 3);
        }
        break;
    case 21: /* Ctrl-U */
        while (s_elen)
        {
            s_elen--;
            term_write("\b \b", 3);
        }
        break;
    default:
        if (k >= 32 && k < 127 && s_elen < (int)sizeof(s_edit) - 2)
        {
            char c = (char)k;
            s_edit[s_elen++] = c;
            if (mode & TTY_MODE_ECHO)
                term_write(&c, 1);
        }
        break;
    }
}

static void key(int k)
{
    sel_clear();
    key_mode(k, tty_mode());
}

/* The clipboard as if typed: in raw mode at once (line ends as Enter: CR), in canonical mode
 * through the line editing (each line sent on its end) */
static void paste(void)
{
    char *text = (char *)malloc(GFX_CLIP_MAX + 1u);
    if (!text)
        return;
    int n = gfx_clip_get(g, text, GFX_CLIP_MAX + 1u);
    if (n > (int)GFX_CLIP_MAX)
        n = (int)GFX_CLIP_MAX;
    if (n > 0 && !s_ended)
    {
        sel_clear();
        uint32_t mode = tty_mode();
        int m = 0;
        for (int i = 0; i < n; i++)
        {
            char c = text[i];
            if (c == '\r' && i + 1 < n && text[i + 1] == '\n')
                continue; /* CR LF: one line end */
            if (!(mode & TTY_MODE_CANON))
                text[m++] = c == '\n' ? '\r' : c;
            else
                key_mode(c == '\r' ? '\n' : (unsigned char)c, mode);
        }
        if (!(mode & TTY_MODE_CANON))
        {
            s_view = 0;
            send(text, m);
            if (mode & TTY_MODE_ECHO)
                term_write(text, m);
        }
    }
    free(text);
}

static void read_output(void)
{
    char buf[1024];
    for (;;)
    {
        int n = (int)read(s_out, buf, sizeof(buf));
        if (n > 0)
        {
            term_write(buf, n);
            continue;
        }
        if (n == 0)
        { /* every writer closed: the shell ended */
            if (!s_ended)
            {
                s_ended = true;
                s_attr = ATTR_DEFAULT;
                term_write("\n[the shell ended - close the window]\n", 38);
            }
        }
        return;
    }
}

/* ---- input --------------------------------------------------------------------------------------- */

static void scroll_view(int lines)
{
    int v = s_view + lines;
    v = v < 0 ? 0 : v > s_hist ? s_hist
                               : v;
    if (v != s_view)
    {
        s_view = v;
        s_all_dirty = true;
    }
}

/* the cell under a point of the text (as a line number and a column) */
static struct pos cell_at(int x, int y)
{
    int col = (x - PAD) / CELL_W, row = (y - PAD) / CELL_H;
    col = col < 0 ? 0 : col >= s_cols ? s_cols - 1
                                      : col;
    row = y < PAD ? 0 : row >= s_rows ? s_rows - 1
                                      : row;
    struct pos p = {s_abs0 - s_view + row, col};
    return p;
}

static bool word_char(char c)
{
    return c && c != ' ' && c != '\t' && c != '"' && c != '\'' && c != '(' && c != ')' && c != '<' && c != '>' &&
           c != '|' && c != ';';
}

/* Mouse on the text: press, drag and release select; a double click selects a word; a click
 * without a selection is a tap (the keyboard on / off) */
static void mouse_pointer(const struct gfx_event *ev)
{
    struct pos p = cell_at(ev->x, ev->y);
    switch (ev->kind)
    {
    case GFX_PTR_DOWN:
    {
        bool twice = ev->time_ms - s_click_ms < DOUBLE_CLICK_MS && p.line == s_click.line && p.col == s_click.col;
        s_click_ms = ev->time_ms;
        s_click = p;
        s_sel_had = s_sel;
        sel_clear();
        const struct cell *c = line_abs(p.line);
        if (twice && c && word_char(c[p.col].ch))
        {
            int a = p.col, b = p.col;
            while (a > 0 && word_char(c[a - 1].ch))
                a--;
            while (b < s_cols - 1 && word_char(c[b + 1].ch))
                b++;
            s_sa.line = s_se.line = p.line;
            s_sa.col = a;
            s_se.col = b;
            s_sel = true;
            s_all_dirty = true;
            s_selecting = false;
            s_click_ms = 0;
            return;
        }
        s_sa = s_se = p;
        s_selecting = true;
        break;
    }
    case GFX_PTR_MOVE:
        if (!s_selecting)
            break;
        if (ev->y < PAD)
            scroll_view(1); /* above the text: further back */
        else if (ev->y >= text_h())
            scroll_view(-1);
        p = cell_at(ev->x, ev->y);
        if (p.line != s_se.line || p.col != s_se.col || !s_sel)
        {
            s_se = p;
            s_sel = true;
            s_all_dirty = true;
        }
        break;
    case GFX_PTR_UP:
        if (!s_selecting)
            break;
        s_selecting = false;
        if (s_sel)
            break;
        if (!s_sel_had)
        { /* a click: as a tap */
            s_kbd_shown = !s_kbd_shown;
            layout();
            draw_keyboard();
        }
        break;
    default:
        break;
    }
}

static void pointer(const struct gfx_event *ev)
{
    if (s_kbd_shown && (ui_inside(&s_kbd.r, ev->x, ev->y) || s_kbd.pressed >= 0) && s_down_y < 0 && !s_selecting)
    {
        bool redraw_kbd;
        int k = ui_keyboard_pointer(&s_kbd, ev->kind, ev->x, ev->y, &redraw_kbd);
        if (redraw_kbd)
            draw_keyboard();
        if (k == 3 && s_sel)
            copy_selection(); /* Ctrl-C with a selection: copy */
        else if (k == 22)
            paste();
        else if (k >= 0)
            key(k);
        return;
    }
    if ((ev->flags & GFX_PTR_MOUSE) && s_down_y < 0)
    {
        mouse_pointer(ev);
        return;
    }
    switch (ev->kind)
    {
    case GFX_PTR_DOWN:
        s_down_y = ev->y;
        s_down_view = s_view;
        s_dragged = false;
        break;
    case GFX_PTR_MOVE:
        if (s_down_y >= 0)
        {
            int d = (ev->y - s_down_y) / CELL_H;
            if (d || s_dragged)
            {
                s_dragged = true;
                scroll_view(s_down_view + d - s_view);
            }
        }
        break;
    case GFX_PTR_UP:
        if (s_down_y >= 0 && !s_dragged)
        { /* a tap: keyboard on / off */
            sel_clear();
            s_kbd_shown = !s_kbd_shown;
            layout();
            draw_keyboard();
        }
        s_down_y = -1;
        break;
    default:
        break;
    }
}

/* A key from a keyboard (or the system's on-screen one): copy and paste first */
static void key_event(const struct gfx_event *ev)
{
    int k = ui_key_char(&s_keys, ev);
    if (k < 0)
        return;
    bool copy = (k == 3 && (s_sel || s_keys.shift)) || (k == UI_KEY_INSERT && s_keys.ctrl);
    bool put = k == 22 || (k == UI_KEY_INSERT && s_keys.shift);
    if (copy)
        copy_selection();
    else if (put)
        paste();
    else
        key(k);
}

int main(void)
{
    g = gfx_open();
    if (!g)
    {
        printf("term: no graphics server\n");
        return 1;
    }
    cells();
    W = gfx_screen_width(g);
    H = gfx_screen_height(g) - ui_px(26) - ui_px(20); /* room for the task bar and a title bar */
    if (H > 16 * CELL_H + KBD_H + 2 * PAD)
        H = 16 * CELL_H + KBD_H + 2 * PAD;
    w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_RESIZABLE, "Terminal");
    if (!w)
        return 1;
    for (int i = 0; i < HISTORY; i++)
        clear_cells(s_lines[i], 0, COLS_MAX);
    chdir("/sd/crtos");
    layout();
    if (start_shell())
    {
        term_write("cannot start the shell\n", 23);
        s_ended = true;
    }
    layout();
    draw_keyboard();
    redraw();
    for (;;)
    {
        struct pollfd pf[2] = {{gfx_event_handle(g), POLLIN, 0}, {s_out, POLLIN, 0}};
        poll(pf, s_out >= 0 && !s_ended ? 2 : 1, -1);
        if (s_out >= 0 && (pf[1].revents & (POLLIN | POLLHUP)))
            read_output();
        struct gfx_event ev;
        while (gfx_next_event(g, &ev, 0) == 0)
        {
            if (ev.h.type == GFX_EV_CLOSE)
            {
                if (s_pid > 0)
                    crtos_kill(s_pid, 0);
                gfx_close(g);
                return 0;
            }
            if (ev.h.type == GFX_EV_POINTER)
                pointer(&ev);
            if (ev.h.type == GFX_EV_WHEEL && ev.code == GFX_WHEEL_VERTICAL)
                scroll_view(ev.value * WHEEL_LINES);
            if (ev.h.type == GFX_EV_KEY)
                key_event(&ev);
            if (ev.h.type == GFX_EV_CONFIGURE && gfx_win_resize(w, ev.w, ev.hgt) == 0)
            {
                W = w->s.w;
                H = w->s.h;
                s_kbd_shown = H >= KBD_H + 3 * CELL_H;
                layout(); /* all rows dirty: the next redraw() paints and shows everything */
                if (s_kbd_shown)
                    ui_keyboard_draw(&s_kbd, &w->s);
            }
            if (ev.h.type == GFX_EV_SETTINGS)
            { /* another font or scale: other cells in the same window */
                cells();
                s_kbd_shown = H >= KBD_H + 3 * CELL_H;
                layout();
                if (s_kbd_shown)
                    ui_keyboard_draw(&s_kbd, &w->s);
            }
        }
        redraw();
        int status;
        while (crtos_wait(-1, &status, 0) > 0)
        {
        }
    }
}
