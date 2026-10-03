/*
 * edit - a full-screen text editor for terminals (the term window, the serial console, USB).
 *
 *     edit FILE [+LINE]
 *
 * Keys: arrows, Home/End (also Ctrl-A/Ctrl-E), PgUp/PgDn, Del (Ctrl-D), Backspace, Enter, Tab;
 * Ctrl-S save, Ctrl-Q quit (twice when there are unsaved changes), Ctrl-F find (Enter: this
 * one, arrows: next/previous, Esc: back), Ctrl-G go to a line, Ctrl-K cut the line (again:
 * more lines), Ctrl-U paste, Ctrl-L draw again.
 *
 * The terminal is switched to raw mode (TTY_IOC_SET_MODE) and driven with VT100 sequences
 * (cursor position, clear to the end of the line, colours) that every terminal here knows.
 * The whole file is in memory as an array of lines. Saving writes FILE.tmp, then replaces
 * FILE with it (FAT renames only onto a free name), so a failed write leaves the file as it
 * was. Line ends stay as they were (\n, or \r\n when the file had them).
 */
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <crtos.h>

#define TAB_WIDTH   4
#define ESC_WAIT_MS 60          /* the rest of an escape sequence comes this soon */

enum {
    K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_PGUP, K_PGDN, K_DEL, K_ESC,
};

#define CTRL(c) ((c) & 0x1f)

struct row {
    char *s;
    int len;
};

static struct {
    struct row *rows;
    int nrows, cap;
    int cx, cy;                 /* cursor: byte in the row, row */
    int rowoff, coloff;         /* the first row and render column shown */
    int screen_rows, screen_cols;
    const char *path;
    bool dirty, crlf;
    char msg[96];
    struct row *clip;           /* Ctrl-K / Ctrl-U */
    int nclip;
    bool last_was_cut;
    int quit_times;
} E;

/* ---- terminal ------------------------------------------------------------------------------- */

static void out(const char *s, size_t n)
{
    while (n) {
        ssize_t w = write(1, s, n);
        if (w <= 0)
            return;
        s += w;
        n -= (size_t)w;
    }
}

static void term_raw(bool raw)
{
    ioctl(0, TTY_IOC_SET_MODE, raw ? 0 : TTY_MODE_CANON | TTY_MODE_ECHO);
}

static void term_size(void)
{
    struct tty_size sz;
    E.screen_cols = 80;
    E.screen_rows = 24;
    if (ioctl(1, TTY_IOC_GET_SIZE, &sz) == 0 || ioctl(0, TTY_IOC_GET_SIZE, &sz) == 0) {
        if (sz.cols >= 20)
            E.screen_cols = sz.cols;
        if (sz.rows >= 5)
            E.screen_rows = sz.rows;
    }
    const char *c = getenv("COLUMNS"), *l = getenv("LINES");
    if (c && atoi(c) >= 20)
        E.screen_cols = atoi(c);
    if (l && atoi(l) >= 5)
        E.screen_rows = atoi(l);
    E.screen_rows -= 2; /* status and message lines */
}

static int read_byte(int timeout_ms)
{
    if (timeout_ms >= 0) {
        struct pollfd p = { 0, POLLIN, 0 };
        if (poll(&p, 1, timeout_ms) <= 0)
            return -1;
    }
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}

static int read_key(void)
{
    int c;
    while ((c = read_byte(-1)) < 0) {
        if (errno != EINTR && errno != EAGAIN)
            return CTRL('q'); /* the terminal is gone */
    }
    if (c != 27)
        return c;
    int a = read_byte(ESC_WAIT_MS);
    if (a < 0)
        return K_ESC;
    int b = read_byte(ESC_WAIT_MS);
    if (b < 0)
        return K_ESC;
    if (a == 'O') {
        return b == 'H' ? K_HOME : b == 'F' ? K_END : K_ESC;
    }
    if (a != '[')
        return K_ESC;
    if (b >= '0' && b <= '9') {
        int t = read_byte(ESC_WAIT_MS);
        if (t != '~')
            return K_ESC;
        switch (b) {
        case '1':
        case '7':
            return K_HOME;
        case '3':
            return K_DEL;
        case '4':
        case '8':
            return K_END;
        case '5':
            return K_PGUP;
        case '6':
            return K_PGDN;
        default:
            return K_ESC;
        }
    }
    switch (b) {
    case 'A':
        return K_UP;
    case 'B':
        return K_DOWN;
    case 'C':
        return K_RIGHT;
    case 'D':
        return K_LEFT;
    case 'H':
        return K_HOME;
    case 'F':
        return K_END;
    default:
        return K_ESC;
    }
}

static void message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.msg, sizeof(E.msg), fmt, ap);
    va_end(ap);
}

/* ---- the text ------------------------------------------------------------------------------- */

static void die_oom(void)
{
    term_raw(false);
    out("\033[2J\033[H", 7);
    fprintf(stderr, "edit: out of memory\n");
    exit(1);
}

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        die_oom();
    return q;
}

static void insert_row(int at, const char *s, int len)
{
    if (E.nrows == E.cap) {
        E.cap = E.cap ? E.cap * 2 : 64;
        E.rows = xrealloc(E.rows, (size_t)E.cap * sizeof(struct row));
    }
    memmove(&E.rows[at + 1], &E.rows[at], (size_t)(E.nrows - at) * sizeof(struct row));
    E.rows[at].s = xrealloc(NULL, (size_t)len + 1);
    memcpy(E.rows[at].s, s, (size_t)len);
    E.rows[at].s[len] = 0;
    E.rows[at].len = len;
    E.nrows++;
}

static void delete_row(int at)
{
    free(E.rows[at].s);
    memmove(&E.rows[at], &E.rows[at + 1], (size_t)(E.nrows - at - 1) * sizeof(struct row));
    E.nrows--;
}

static void row_insert(struct row *r, int at, const char *s, int n)
{
    r->s = xrealloc(r->s, (size_t)r->len + (size_t)n + 1);
    memmove(r->s + at + n, r->s + at, (size_t)(r->len - at) + 1);
    memcpy(r->s + at, s, (size_t)n);
    r->len += n;
}

static void row_delete(struct row *r, int at, int n)
{
    memmove(r->s + at, r->s + at + n, (size_t)(r->len - at - n) + 1);
    r->len -= n;
}

/* the screen column of byte @cx of a row */
static int render_col(const struct row *r, int cx)
{
    int col = 0;
    for (int i = 0; i < cx && i < r->len; i++)
        col += r->s[i] == '\t' ? TAB_WIDTH - col % TAB_WIDTH : 1;
    return col;
}

static void load(const char *path)
{
    E.path = path;
    FILE *f = fopen(path, "rb");
    if (!f) {
        insert_row(0, "", 0);
        message(errno == ENOENT ? "new file" : "%s: %s", path, strerror(errno));
        return;
    }
    char *buf = NULL;
    size_t cap = 0, len = 0;
    int c;
    bool any = false;
    while ((c = fgetc(f)) != EOF) {
        any = true;
        if (c == '\n') {
            if (len && buf[len - 1] == '\r') {
                len--;
                E.crlf = true;
            }
            insert_row(E.nrows, buf ? buf : "", (int)len);
            len = 0;
            continue;
        }
        if (len + 1 >= cap) {
            cap = cap ? cap * 2 : 128;
            buf = xrealloc(buf, cap);
        }
        buf[len++] = (char)c;
    }
    if (len || !any)
        insert_row(E.nrows, buf ? buf : "", (int)len);
    free(buf);
    fclose(f);
    message("%d lines%s", E.nrows, E.crlf ? " (CR LF)" : "");
}

static bool save(void)
{
    char tmp[260];
    snprintf(tmp, sizeof(tmp), "%s.tmp", E.path);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        message("cannot write %s: %s", tmp, strerror(errno));
        return false;
    }
    long bytes = 0;
    bool ok = true;
    /* every line ends with a line end; an empty text is an empty file */
    bool empty = E.nrows == 1 && !E.rows[0].len;
    for (int i = 0; i < E.nrows && ok && !empty; i++) {
        if (fwrite(E.rows[i].s, 1, (size_t)E.rows[i].len, f) != (size_t)E.rows[i].len ||
            (E.crlf && fputc('\r', f) == EOF) || fputc('\n', f) == EOF)
            ok = false;
        bytes += E.rows[i].len + (E.crlf ? 2 : 1);
    }
    if (fclose(f))
        ok = false;
    if (!ok) {
        remove(tmp);
        message("writing failed: %s (the file is unchanged)", strerror(errno));
        return false;
    }
    remove(E.path);
    if (rename(tmp, E.path)) {
        message("saved as %s only: %s", tmp, strerror(errno));
        return false;
    }
    E.dirty = false;
    message("%s: %d lines, %ld bytes written", E.path, E.nrows, bytes);
    return true;
}

/* ---- editing -------------------------------------------------------------------------------- */

static void insert_char(int c)
{
    char ch = (char)c;
    row_insert(&E.rows[E.cy], E.cx, &ch, 1);
    E.cx++;
    E.dirty = true;
}

static void insert_newline(void)
{
    struct row *r = &E.rows[E.cy];
    /* keep the indentation of the line */
    int indent = 0;
    while (indent < r->len && indent < E.cx && (r->s[indent] == ' ' || r->s[indent] == '\t'))
        indent++;
    insert_row(E.cy + 1, r->s + E.cx, r->len - E.cx);
    r = &E.rows[E.cy];
    r->len = E.cx;
    r->s[r->len] = 0;
    E.cy++;
    row_insert(&E.rows[E.cy], 0, E.rows[E.cy - 1].s, indent);
    E.cx = indent;
    E.dirty = true;
}

static void delete_back(void)
{
    if (E.cx > 0) {
        row_delete(&E.rows[E.cy], E.cx - 1, 1);
        E.cx--;
    } else if (E.cy > 0) {
        struct row *prev = &E.rows[E.cy - 1];
        E.cx = prev->len;
        row_insert(prev, prev->len, E.rows[E.cy].s, E.rows[E.cy].len);
        delete_row(E.cy);
        E.cy--;
    } else {
        return;
    }
    E.dirty = true;
}

static void delete_forward(void)
{
    struct row *r = &E.rows[E.cy];
    if (E.cx < r->len) {
        row_delete(r, E.cx, 1);
    } else if (E.cy + 1 < E.nrows) {
        row_insert(r, r->len, E.rows[E.cy + 1].s, E.rows[E.cy + 1].len);
        delete_row(E.cy + 1);
    } else {
        return;
    }
    E.dirty = true;
}

static void clear_clip(void)
{
    for (int i = 0; i < E.nclip; i++)
        free(E.clip[i].s);
    E.nclip = 0;
}

static void cut_line(bool append)
{
    if (!append)
        clear_clip();
    E.clip = xrealloc(E.clip, (size_t)(E.nclip + 1) * sizeof(struct row));
    struct row *r = &E.rows[E.cy];
    E.clip[E.nclip].s = xrealloc(NULL, (size_t)r->len + 1);
    memcpy(E.clip[E.nclip].s, r->s, (size_t)r->len + 1);
    E.clip[E.nclip].len = r->len;
    E.nclip++;
    if (E.nrows > 1) {
        delete_row(E.cy);
        if (E.cy >= E.nrows)
            E.cy = E.nrows - 1;
    } else {
        r->len = 0;
        r->s[0] = 0;
    }
    E.cx = 0;
    E.dirty = true;
    message("%d line(s) cut: Ctrl-U pastes", E.nclip);
}

static void paste(void)
{
    for (int i = 0; i < E.nclip; i++)
        insert_row(E.cy + i, E.clip[i].s, E.clip[i].len);
    if (E.nclip) {
        E.cy += E.nclip;
        E.cx = 0;
        E.dirty = true;
    }
}

static void move(int key)
{
    struct row *r = &E.rows[E.cy];
    switch (key) {
    case K_LEFT:
        if (E.cx > 0)
            E.cx--;
        else if (E.cy > 0)
            E.cx = E.rows[--E.cy].len;
        break;
    case K_RIGHT:
        if (E.cx < r->len)
            E.cx++;
        else if (E.cy + 1 < E.nrows) {
            E.cy++;
            E.cx = 0;
        }
        break;
    case K_UP:
        if (E.cy > 0)
            E.cy--;
        break;
    case K_DOWN:
        if (E.cy + 1 < E.nrows)
            E.cy++;
        break;
    case K_HOME:
        E.cx = 0;
        break;
    case K_END:
        E.cx = r->len;
        break;
    case K_PGUP:
        E.cy = E.cy > E.screen_rows ? E.cy - E.screen_rows : 0;
        break;
    case K_PGDN:
        E.cy = E.cy + E.screen_rows < E.nrows ? E.cy + E.screen_rows : E.nrows - 1;
        break;
    }
    if (E.cx > E.rows[E.cy].len)
        E.cx = E.rows[E.cy].len;
}

/* ---- the screen ----------------------------------------------------------------------------- */

struct abuf {
    char *b;
    size_t len, cap;
};

static void ab_add(struct abuf *a, const char *s, size_t n)
{
    if (a->len + n > a->cap) {
        a->cap = (a->len + n) * 2;
        a->b = xrealloc(a->b, a->cap);
    }
    memcpy(a->b + a->len, s, n);
    a->len += n;
}

static void ab_printf(struct abuf *a, const char *fmt, ...)
{
    char tmp[160];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0)
        ab_add(a, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

static void scroll(void)
{
    if (E.cy < E.rowoff)
        E.rowoff = E.cy;
    if (E.cy >= E.rowoff + E.screen_rows)
        E.rowoff = E.cy - E.screen_rows + 1;
    int rx = render_col(&E.rows[E.cy], E.cx);
    int width = E.screen_cols - 1;
    if (rx < E.coloff)
        E.coloff = rx;
    if (rx >= E.coloff + width)
        E.coloff = rx - width + 1;
}

static void draw(void)
{
    scroll();
    struct abuf a = { NULL, 0, 0 };
    int width = E.screen_cols - 1; /* the last column: some terminals wrap there */
    for (int y = 0; y < E.screen_rows; y++) {
        ab_printf(&a, "\033[%d;1H", y + 1);
        int fr = y + E.rowoff;
        if (fr < E.nrows) {
            const struct row *r = &E.rows[fr];
            int col = 0;
            for (int i = 0; i < r->len && col < E.coloff + width; i++) {
                char ch = r->s[i];
                int w = ch == '\t' ? TAB_WIDTH - col % TAB_WIDTH : 1;
                for (int k = 0; k < w; k++, col++) {
                    if (col < E.coloff || col >= E.coloff + width)
                        continue;
                    char o = ch == '\t' ? ' ' : (unsigned char)ch < 32 || ch == 127 ? '?' : ch;
                    ab_add(&a, &o, 1);
                }
            }
        } else {
            ab_add(&a, "\033[34m~\033[0m", 10);
        }
        ab_add(&a, "\033[K", 3);
    }
    /* status line (inverse) and the message or help line */
    char left[128], right[48];
    snprintf(left, sizeof(left), " %.80s%s", E.path, E.dirty ? " [changed]" : "");
    snprintf(right, sizeof(right), "line %d/%d, col %d ", E.cy + 1, E.nrows, render_col(&E.rows[E.cy], E.cx) + 1);
    int ll = (int)strlen(left), rl = (int)strlen(right);
    ab_printf(&a, "\033[%d;1H\033[30;47m", E.screen_rows + 1);
    for (int i = 0; i < width; i++) {
        char ch = i < ll ? left[i] : (i >= width - rl ? right[i - (width - rl)] : ' ');
        ab_add(&a, &ch, 1);
    }
    ab_add(&a, "\033[0m\033[K", 7);
    ab_printf(&a, "\033[%d;1H", E.screen_rows + 2);
    const char *m = E.msg[0] ? E.msg : "^S save  ^Q quit  ^F find  ^G line  ^K cut  ^U paste";
    ab_printf(&a, "%.*s\033[K", width, m);
    int rx = render_col(&E.rows[E.cy], E.cx) - E.coloff;
    ab_printf(&a, "\033[%d;%dH", E.cy - E.rowoff + 1, rx + 1);
    out(a.b, a.len);
    free(a.b);
}

/* a line typed on the message line; NULL when Esc cancels it. @live is called after every key */
static char *prompt(const char *what, void (*live)(const char *text, int key))
{
    static char buf[80];
    size_t len = 0;
    buf[0] = 0;
    for (;;) {
        message("%s%s", what, buf);
        draw();
        int c = read_key();
        if (c == '\r' || c == '\n') {
            if (live)
                live(buf, c);
            message("");
            return buf;
        }
        if (c == K_ESC || c == CTRL('q') || c == CTRL('c')) {
            if (live)
                live(buf, K_ESC);
            message("");
            return NULL;
        }
        if ((c == 127 || c == CTRL('h')) && len) {
            buf[--len] = 0;
        } else if (c >= 32 && c < 127 && len + 1 < sizeof(buf)) {
            buf[len++] = (char)c;
            buf[len] = 0;
        }
        if (live)
            live(buf, c);
    }
}

static int s_find_row, s_find_col, s_saved_cx, s_saved_cy;

static void find_step(const char *text, int key)
{
    if (key == K_ESC) {
        E.cx = s_saved_cx;
        E.cy = s_saved_cy;
        return;
    }
    if (key == '\r' || key == '\n' || !*text)
        return;
    int dir = key == K_UP || key == K_LEFT ? -1 : 1;
    bool next = key == K_UP || key == K_DOWN || key == K_LEFT || key == K_RIGHT;
    int row = s_find_row, col = next ? s_find_col + dir : s_find_col;
    for (int n = 0; n <= E.nrows; n++) {
        if (row < 0 || row >= E.nrows) {
            row = row < 0 ? E.nrows - 1 : 0;
            col = dir > 0 ? 0 : E.rows[row].len;
        }
        const struct row *r = &E.rows[row];
        if (dir > 0) {
            char *hit = col <= r->len ? strstr(r->s + (col < 0 ? 0 : col), text) : NULL;
            if (hit) {
                s_find_row = E.cy = row;
                s_find_col = E.cx = (int)(hit - r->s);
                return;
            }
            row++;
            col = 0;
        } else {
            for (int c = (col > r->len ? r->len : col); c >= 0; c--) {
                if (!strncmp(r->s + c, text, strlen(text))) {
                    s_find_row = E.cy = row;
                    s_find_col = E.cx = c;
                    return;
                }
            }
            row--;
            col = row >= 0 ? E.rows[row].len : 0;
        }
    }
    message("not found: %s", text);
}

static void find(void)
{
    s_saved_cx = E.cx;
    s_saved_cy = E.cy;
    s_find_row = E.cy;
    s_find_col = E.cx;
    prompt("Find (arrows: next/previous, Esc: back): ", find_step);
}

static void go_to_line(void)
{
    char *t = prompt("Go to line: ", NULL);
    if (!t || !*t)
        return;
    int n = atoi(t);
    E.cy = n < 1 ? 0 : n > E.nrows ? E.nrows - 1 : n - 1;
    E.cx = 0;
}

/* ---- main ----------------------------------------------------------------------------------- */

static void quit(int code)
{
    term_raw(false);
    out("\033[2J\033[H", 7);
    exit(code);
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        fprintf(stderr, "usage: edit FILE [+LINE]\n");
        return 2;
    }
    if (!isatty(0)) {
        fprintf(stderr, "edit: needs a terminal\n");
        return 2;
    }
    term_size();
    load(argv[1]);
    if (argc == 3 && argv[2][0] == '+') {
        int n = atoi(argv[2] + 1);
        E.cy = n < 1 ? 0 : n > E.nrows ? E.nrows - 1 : n - 1;
    }
    term_raw(true);
    out("\033[2J", 4);
    E.quit_times = 2;
    for (;;) {
        draw();
        int c = read_key();
        bool cut = false;
        if (c != CTRL('q') && c != CTRL('x'))
            E.quit_times = 2;
        switch (c) {
        case CTRL('q'):
        case CTRL('x'):
            if (E.dirty && --E.quit_times > 0) {
                message("unsaved changes: Ctrl-Q again quits without saving, Ctrl-S saves");
                break;
            }
            quit(0);
            break;
        case CTRL('s'):
            save();
            break;
        case CTRL('f'):
            find();
            break;
        case CTRL('g'):
            go_to_line();
            break;
        case CTRL('k'):
            cut_line(E.last_was_cut);
            cut = true;
            break;
        case CTRL('u'):
            paste();
            break;
        case CTRL('l'):
            term_size();
            out("\033[2J", 4);
            break;
        case CTRL('a'):
            move(K_HOME);
            break;
        case CTRL('e'):
            move(K_END);
            break;
        case CTRL('d'):
        case K_DEL:
            delete_forward();
            break;
        case 127:
        case CTRL('h'):
            delete_back();
            break;
        case '\r':
        case '\n':
            insert_newline();
            break;
        case CTRL('c'):
            message("Ctrl-Q quits, Ctrl-S saves");
            break;
        case K_ESC:
            break;
        case K_UP:
        case K_DOWN:
        case K_LEFT:
        case K_RIGHT:
        case K_HOME:
        case K_END:
        case K_PGUP:
        case K_PGDN:
            move(c);
            break;
        default:
            if (c == '\t' || (c >= 32 && c < 256 && c != 127)) {
                insert_char(c);
                E.msg[0] = 0;
            }
            break;
        }
        E.last_was_cut = cut;
    }
}
