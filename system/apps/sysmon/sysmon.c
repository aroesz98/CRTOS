/*
 * sysmon - system monitor (layer 3): processor load over the last minute, memory, the network
 * and the processes with their share of the processor and memory. Tap a process and "End" to end
 * it (needs the kill capability for processes it did not start). The window can be resized; the
 * process list scrolls with a finger or the mouse wheel. Sizes follow the interface scale (ui_px;
 * the window grows or shrinks with it, GFX_EV_SETTINGS).
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <crtos.h>
#include "gfx.h"
#include "gfx_ui.h"

static int W, H;
#define HIST 120 /* samples of the load graph (every 500 ms) */
#define PERIOD 500
#define MAXP 32

struct proc_row
{
    int pid;
    char name[16];
    uint32_t arena_kb, heap_kb;
    int threads;
    uint64_t cycles;
    uint32_t load10; /* per mille */
};

static struct gfx *g;
static struct gfx_win *w;
static uint8_t s_hist[HIST]; /* load in % */
static int s_nhist;
static struct crtos_sysinfo s_si, s_prev_si;
static struct proc_row s_rows[MAXP], s_prev_rows[MAXP];
static int s_nrows, s_nprev;
static int s_sel_pid = -1;
static struct ui_list s_list;
static bool s_end_pressed;
static char s_msg[48];

static struct ui_rect r_graph, r_end;
static int s_scale; /* the one the window has its size for */

/* small labels: the tiny font at 100 %, the theme's above */
static const GFXfont *small(void)
{
    return ui_theme->scale > 100 ? ui_theme->font : &gfx_tiny;
}

static int row_h(void)
{
    int h = ui_theme->font->yAdvance + ui_px(2);
    return h > ui_px(16) ? h : ui_px(16);
}

/* the first network interface */
static int s_net_fd = -1;
static struct net_ifinfo s_if, s_prev_if;
static bool s_have_if;
static uint32_t s_rx_rate, s_tx_rate; /* bytes/s */

/* Everything placed for the window's size */
static void layout(void)
{
    int m = ui_px(8), head = ui_theme->bold->yAdvance + ui_px(6), sl = small()->yAdvance;
    r_graph = (struct ui_rect){m, head, W - 2 * m, H >= ui_px(200) ? ui_px(52) + (H - ui_px(222)) / 3 : ui_px(40)};
    r_end = (struct ui_rect){W - ui_px(70), H - ui_px(24), ui_px(62), ui_px(20)};
    /* memory bars, network, column titles */
    int top = r_graph.y + r_graph.h + ui_px(4) + (sl + ui_px(8)) + (sl + ui_px(6)) + (sl + ui_px(6));
    s_list.r.x = m;
    s_list.r.y = top;
    s_list.r.w = W - 2 * m;
    s_list.row_h = row_h();
    s_list.r.h = H - top - ui_px(28) > s_list.row_h ? H - top - ui_px(28) : s_list.row_h;
}

/* Column positions in the process list, for its width (designed for 304) */
static int col(int x304)
{
    return x304 * (W - 16) / 304;
}

static void sample_net(uint64_t dt_us)
{
    if (s_net_fd < 0)
        s_net_fd = socket(AF_INET, SOCK_DGRAM, 0); /* none until the stack is loaded */
    s_prev_if = s_if;
    bool had = s_have_if;
    memset(&s_if, 0, sizeof(s_if));
    s_have_if = s_net_fd >= 0 && ioctl(s_net_fd, NET_IOC_IFINFO, &s_if) == 0;
    if (had && s_have_if && dt_us)
    {
        s_rx_rate = (uint32_t)((uint64_t)(s_if.rx_bytes - s_prev_if.rx_bytes) * 1000000u / dt_us);
        s_tx_rate = (uint32_t)((uint64_t)(s_if.tx_bytes - s_prev_if.tx_bytes) * 1000000u / dt_us);
    }
}

static void sample(void)
{
    s_prev_si = s_si;
    crtos_sys_info(&s_si);
    sample_net(s_prev_si.uptime_us ? s_si.uptime_us - s_prev_si.uptime_us : 0);
    memcpy(s_prev_rows, s_rows, sizeof(s_rows));
    s_nprev = s_nrows;
    s_nrows = 0;
    struct crtos_procinfo pi;
    for (int i = 0; s_nrows < MAXP && crtos_proc_info(i, &pi) == 0; i++)
    {
        if (pi.state == 2)
            continue;
        struct proc_row *r = &s_rows[s_nrows++];
        r->pid = pi.pid;
        memcpy(r->name, pi.name, sizeof(r->name));
        r->name[sizeof(r->name) - 1] = 0;
        r->arena_kb = pi.arena_size / 1024u;
        r->heap_kb = pi.heap_used / 1024u;
        r->threads = pi.nthreads;
        r->cycles = pi.cycles;
        r->load10 = 0;
    }
    uint64_t dt = s_si.uptime_us - s_prev_si.uptime_us;
    if (!s_prev_si.uptime_us || !dt)
        return;
    uint64_t avail = dt * (s_si.cpu_hz / 1000000u);
    uint64_t idle = s_si.idle_cycles - s_prev_si.idle_cycles;
    int load = avail ? 100 - (int)(idle * 100u / avail) : 0;
    load = load < 0 ? 0 : load > 100 ? 100
                                     : load;
    if (s_nhist == HIST)
        memmove(s_hist, s_hist + 1, HIST - 1);
    else
        s_nhist++;
    s_hist[s_nhist - 1] = (uint8_t)load;
    for (int i = 0; i < s_nrows; i++)
        for (int k = 0; k < s_nprev; k++)
            if (s_prev_rows[k].pid == s_rows[i].pid && avail)
            {
                uint64_t d = s_rows[i].cycles - s_prev_rows[k].cycles;
                s_rows[i].load10 = (uint32_t)(d * 1000u / avail);
            }
    s_list.count = s_nrows;
    s_list.sel = -1;
    for (int i = 0; i < s_nrows; i++)
        if (s_rows[i].pid == s_sel_pid)
            s_list.sel = i;
}

static void row(const struct gfx_surface *s, const struct ui_rect *r, int i, bool sel, void *ctx)
{
    (void)ctx;
    const struct ui_theme *t = ui_theme;
    const struct proc_row *p = &s_rows[i];
    uint32_t c = sel ? t->accent_text : t->text, dim = sel ? t->accent_text : t->text_dim;
    int y = r->y + (r->h + gfx_font_ascent(t->font)) / 2;
    char buf[24];
    snprintf(buf, sizeof(buf), "%d", p->pid);
    gfx_text(s, t->font, r->x + col(30) - gfx_text_width(t->font, buf), y, buf, dim);
    ui_text_fit(s, t->font, r->x + col(38), y, col(150) - col(38), p->name, c);
    snprintf(buf, sizeof(buf), "%lu.%lu%%", (unsigned long)(p->load10 / 10u), (unsigned long)(p->load10 % 10u));
    gfx_text(s, t->font, r->x + col(196) - gfx_text_width(t->font, buf), y, buf, c);
    snprintf(buf, sizeof(buf), "%lu K", (unsigned long)p->arena_kb);
    gfx_text(s, t->font, r->x + col(252) - gfx_text_width(t->font, buf), y, buf, dim);
    snprintf(buf, sizeof(buf), "%d", p->threads);
    gfx_text(s, t->font, r->x + col(290) - gfx_text_width(t->font, buf), y, buf, dim);
}

static void bar(struct gfx_surface *s, int x, int y, int bw, const char *label, uint32_t used, uint32_t total,
                const char *unit, uint32_t div)
{
    const struct ui_theme *t = ui_theme;
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %lu / %lu %s", label, (unsigned long)(used / div), (unsigned long)(total / div), unit);
    const GFXfont *f = small();
    ui_text_fit(s, f, x, y + gfx_font_ascent(f), bw, buf, t->text_dim);
    int by = y + f->yAdvance + ui_px(1), bh = ui_px(5), rad = t->radius ? bh / 2 : 0;
    gfx_round_rect_aa(s, x, by, bw, bh, rad, GFX_RGB(50, 58, 74), 0, GFX_CORNERS_ALL);
    int fw = total ? (int)((uint64_t)used * (uint64_t)bw / total) : 0;
    if (fw > 0)
        gfx_round_rect_aa(s, x, by, fw, bh, rad, used * 10u > total * 8u ? GFX_RGB(230, 120, 60) : GFX_RGB(80, 180, 120), 0,
                          GFX_CORNERS_ALL);
}

static void draw(void)
{
    const struct ui_theme *t = ui_theme;
    struct gfx_surface *s = &w->s;
    gfx_fill(s, 0, 0, W, H, t->bg);
    /* load */
    int load = s_nhist ? s_hist[s_nhist - 1] : 0;
    char buf[64];
    uint32_t up = (uint32_t)(s_si.uptime_us / 1000000u);
    int m = ui_px(8), base = ui_px(4) + gfx_font_ascent(t->bold);
    snprintf(buf, sizeof(buf), "CPU %d%%", load);
    int cx = gfx_text(s, t->bold, m, base, buf, t->text) + ui_px(12);
    snprintf(buf, sizeof(buf), "%lu processes, %lu threads, up %lu:%02lu:%02lu", (unsigned long)s_si.nprocs,
             (unsigned long)s_si.ntasks, (unsigned long)(up / 3600u), (unsigned long)(up / 60u % 60u),
             (unsigned long)(up % 60u));
    int tw = gfx_text_width(t->font, buf);
    ui_text_fit(s, t->font, W - m - tw > cx ? W - m - tw : cx, base, W - m - cx, buf, t->text_dim);
    gfx_fill(s, r_graph.x, r_graph.y, r_graph.w, r_graph.h, GFX_RGB(18, 22, 30));
    for (int k = 1; k < 4; k++)
        gfx_hline(s, r_graph.x, r_graph.y + r_graph.h * k / 4, r_graph.w, GFX_RGB(36, 42, 54));
    int n = s_nhist, bw = r_graph.w / HIST;
    if (bw < 1)
        bw = 1;
    for (int i = 0; i < n; i++)
    {
        int x = r_graph.x + r_graph.w - (n - i) * bw;
        int hh = s_hist[i] * (r_graph.h - 2) / 100;
        if (hh)
            gfx_fill(s, x, r_graph.y + r_graph.h - 1 - hh, bw, hh, s_hist[i] > 80 ? GFX_RGB(230, 110, 70) : GFX_RGB(70, 150, 230));
    }
    gfx_rect(s, r_graph.x, r_graph.y, r_graph.w, r_graph.h, t->border);
    /* memory */
    int my = r_graph.y + r_graph.h + ui_px(4), mw = (W - 4 * m) / 3;
    bar(s, m, my, mw, "RAM", s_si.mem_total - s_si.mem_free, s_si.mem_total, "MB", 1024u * 1024u);
    bar(s, 2 * m + mw, my, mw, "kernel", s_si.kmem_total - s_si.kmem_free, s_si.kmem_total, "KB", 1024u);
    bar(s, 3 * m + 2 * mw, my, mw, "DMA", s_si.dma_total - s_si.dma_free, s_si.dma_total, "KB", 1024u);
    /* network */
    const GFXfont *sf = small();
    int ny = my + sf->yAdvance + ui_px(8) + gfx_font_ascent(sf);
    if (!s_have_if)
    {
        snprintf(buf, sizeof(buf), "network: none");
    }
    else if (!(s_if.flags & NET_IF_LINK))
    {
        snprintf(buf, sizeof(buf), "%s: no link", s_if.name);
    }
    else
    {
        char a[INET_ADDRSTRLEN] = "no address";
        if (s_if.addr)
            inet_ntop(AF_INET, &s_if.addr, a, sizeof(a));
        snprintf(buf, sizeof(buf), "%s %s  %lu Mbit/s  in %lu.%lu KB/s  out %lu.%lu KB/s", s_if.name, a,
                 (unsigned long)s_if.speed, (unsigned long)(s_rx_rate / 1024u), (unsigned long)(s_rx_rate % 1024u * 10u / 1024u),
                 (unsigned long)(s_tx_rate / 1024u), (unsigned long)(s_tx_rate % 1024u * 10u / 1024u));
    }
    ui_text_fit(s, sf, m, ny, W - 2 * m, buf, t->text_dim);
    /* processes */
    int hh = sf->yAdvance + ui_px(4), hy = s_list.r.y - hh, hb = hy + (hh + gfx_font_ascent(sf)) / 2;
    gfx_fill(s, m, hy, W - 2 * m, hh, t->panel);
    gfx_text(s, sf, m + col(10), hb, "PID", t->text_dim);
    gfx_text(s, sf, m + col(38), hb, "NAME", t->text_dim);
    gfx_text(s, sf, m + col(170), hb, "CPU", t->text_dim);
    gfx_text(s, sf, m + col(224), hb, "MEM", t->text_dim);
    gfx_text(s, sf, m + col(270), hb, "THR", t->text_dim);
    ui_list_draw(&s_list, s);
    ui_button(s, &r_end, "End", s_end_pressed);
    if (s_msg[0])
        ui_text_fit(s, t->font, m, r_end.y + (r_end.h + gfx_font_ascent(t->font)) / 2, r_end.x - 2 * m, s_msg,
                    t->text_dim);
    gfx_present(w);
}

static void end_selected(void)
{
    if (s_list.sel < 0 || s_list.sel >= s_nrows)
    {
        snprintf(s_msg, sizeof(s_msg), "Tap a process first");
        return;
    }
    const struct proc_row *p = &s_rows[s_list.sel];
    if (p->pid == 1)
    {
        snprintf(s_msg, sizeof(s_msg), "init cannot be ended");
        return;
    }
    if (crtos_kill(p->pid, -1) == 0)
        snprintf(s_msg, sizeof(s_msg), "Ended %s (%d)", p->name, p->pid);
    else
        snprintf(s_msg, sizeof(s_msg), "%s: %s", p->name, strerror(errno));
}

int main(void)
{
    g = gfx_open();
    if (!g)
    {
        printf("sysmon: no graphics server\n");
        return 1;
    }
    s_scale = ui_theme->scale;
    W = ui_px(320);
    H = ui_px(222);
    if (W > gfx_screen_width(g))
        W = gfx_screen_width(g);
    if (H > gfx_screen_height(g) - ui_px(50))
        H = gfx_screen_height(g) - ui_px(50);
    w = gfx_win_create(g, -1, -1, W, H, GFX_WIN_RESIZABLE, "System monitor");
    if (!w)
        return 1;
    layout();
    s_list.sel = -1;
    s_list.draw_row = row;
    sample();
    draw();
    uint64_t next = crtos_time_us() + PERIOD * 1000u;
    for (;;)
    {
        uint64_t now = crtos_time_us();
        uint32_t wait = next > now ? (uint32_t)((next - now) / 1000u) : 0;
        struct gfx_event ev;
        if (gfx_next_event(g, &ev, wait) == 0)
        {
            if (ev.h.type == GFX_EV_CLOSE)
                break;
            if (ev.h.type == GFX_EV_CONFIGURE &&
                gfx_win_resize(w, ev.w < ui_px(240) ? ui_px(240) : ev.w, ev.hgt < ui_px(170) ? ui_px(170) : ev.hgt) == 0)
            {
                W = w->s.w;
                H = w->s.h;
                layout();
                draw();
                continue;
            }
            if (ev.h.type == GFX_EV_SETTINGS)
            { /* another scale: the same window, that much bigger or smaller */
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
            int r = ui_list_pointer(&s_list, ev.kind, ev.x, ev.y);
            if (r != UI_NONE)
            {
                s_sel_pid = s_list.sel >= 0 ? s_rows[s_list.sel].pid : -1;
                s_msg[0] = 0;
                draw();
            }
            bool in = ui_inside(&r_end, ev.x, ev.y);
            if (ev.kind == GFX_PTR_DOWN && in)
            {
                s_end_pressed = true;
                draw();
            }
            else if (ev.kind == GFX_PTR_UP && s_end_pressed)
            {
                s_end_pressed = false;
                if (in)
                    end_selected();
                draw();
            }
            continue;
        }
        if (crtos_time_us() >= next)
        {
            next += PERIOD * 1000u;
            if (next < crtos_time_us())
                next = crtos_time_us() + PERIOD * 1000u;
            sample();
            draw();
        }
    }
    gfx_close(g);
    return 0;
}
