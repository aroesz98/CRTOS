/*
 * httpd - a small web server (layer 2 service): the files under /sd/crtos/www and the state
 * of the system - /status (a page) and /api/status (JSON).
 *
 *   httpd [-p port] [-r root] [-t threads]
 *
 * GET and HEAD, one request per connection (HTTP/1.0 style, "Connection: close"). Worker
 * threads take the connections from the listening socket one by one.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <crtos.h>

#define MAX_REQUEST     2048
#define MAX_THREADS     8

static const char *s_root = "/sd/crtos/www";

/* ---- a growing text buffer --------------------------------------------------------------------- */

struct text {
    char *p;
    size_t n, cap;
    bool failed;
};

static void tprintf(struct text *t, const char *fmt, ...)
{
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        size_t room = t->cap - t->n;
        int w = vsnprintf(t->p ? t->p + t->n : NULL, t->p ? room : 0, fmt, ap);
        va_end(ap);
        if (w < 0)
            return;
        if ((size_t)w < room) {
            t->n += (size_t)w;
            return;
        }
        size_t cap = t->cap ? t->cap * 2 : 4096;
        while (cap - t->n <= (size_t)w)
            cap *= 2;
        char *np = (char *)realloc(t->p, cap);
        if (!np) {
            t->failed = true;
            return;
        }
        t->p = np;
        t->cap = cap;
    }
}

/* text with <, >, & and " escaped */
static void tescape(struct text *t, const char *s)
{
    for (; *s; s++) {
        switch (*s) {
        case '<': tprintf(t, "&lt;"); break;
        case '>': tprintf(t, "&gt;"); break;
        case '&': tprintf(t, "&amp;"); break;
        case '"': tprintf(t, "&quot;"); break;
        default: tprintf(t, "%c", *s);
        }
    }
}

/* ---- sending ------------------------------------------------------------------------------------- */

static int send_all(int fd, const void *data, size_t len)
{
    const char *p = (const char *)data;
    while (len) {
        int w = (int)send(fd, p, len, 0);
        if (w <= 0)
            return -1;
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

static void header(int fd, int code, const char *reason, const char *type, long long length)
{
    char h[256];
    int n = snprintf(h, sizeof(h), "HTTP/1.1 %d %s\r\nServer: CRTOS httpd\r\nConnection: close\r\n", code, reason);
    if (type)
        n += snprintf(h + n, sizeof(h) - (size_t)n, "Content-Type: %s\r\n", type);
    if (length >= 0)
        n += snprintf(h + n, sizeof(h) - (size_t)n, "Content-Length: %lu\r\n", (unsigned long)length);
    n += snprintf(h + n, sizeof(h) - (size_t)n, "\r\n");
    send_all(fd, h, (size_t)n);
}

static void reply_text(int fd, bool head, int code, const char *reason, const char *type, const struct text *t)
{
    if (t->failed) {
        code = 500;
        reason = "Out of memory";
    }
    header(fd, code, reason, type, t->failed ? 0 : (long long)t->n);
    if (!head && !t->failed)
        send_all(fd, t->p, t->n);
}

static void reply_error(int fd, bool head, int code, const char *reason)
{
    struct text t = { 0 };
    tprintf(&t, "<!DOCTYPE html><html><head><title>%d %s</title></head><body><h1>%d %s</h1></body></html>\n", code,
            reason, code, reason);
    reply_text(fd, head, code, reason, "text/html; charset=utf-8", &t);
    free(t.p);
}

/* ---- the system's state ---------------------------------------------------------------------------- */

struct state {
    struct crtos_sysinfo si;
    int load;                       /* % over 200 ms */
    struct crtos_procinfo procs[48];
    int nprocs;
    struct net_ifinfo ifs[4];
    int nifs;
    struct timeval now;
};

static void get_state(struct state *s)
{
    memset(s, 0, sizeof(*s));
    struct crtos_sysinfo a;
    crtos_sys_info(&a);
    crtos_sleep_ms(200);
    crtos_sys_info(&s->si);
    uint64_t avail = (s->si.uptime_us - a.uptime_us) * (s->si.cpu_hz / 1000000u);
    uint64_t idle = s->si.idle_cycles - a.idle_cycles;
    s->load = avail ? 100 - (int)(idle * 100u / avail) : 0;
    s->load = s->load < 0 ? 0 : s->load;
    while (s->nprocs < (int)(sizeof(s->procs) / sizeof(s->procs[0])) && !crtos_proc_info(s->nprocs, &s->procs[s->nprocs]))
        s->nprocs++;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        uint32_t n = 0;
        ioctl(fd, NET_IOC_IFCOUNT, &n);
        for (uint32_t i = 0; i < n && s->nifs < 4; i++) {
            struct net_ifinfo *in = &s->ifs[s->nifs];
            memset(in, 0, sizeof(*in));
            in->index = (int32_t)i;
            if (!ioctl(fd, NET_IOC_IFINFO, in))
                s->nifs++;
        }
        close(fd);
    }
    gettimeofday(&s->now, NULL);
}

static const char *ip(uint32_t a, char *buf)
{
    inet_ntop(AF_INET, &a, buf, INET_ADDRSTRLEN);
    return buf;
}

static void status_json(int fd, bool head)
{
    struct state *s = (struct state *)malloc(sizeof(*s));
    if (!s) {
        reply_error(fd, head, 500, "Out of memory");
        return;
    }
    get_state(s);
    struct text t = { 0 };
    tprintf(&t, "{\"uptime_ms\":%lu,\"time\":%ld,\"cpu_hz\":%lu,\"load\":%d,", (unsigned long)(s->si.uptime_us / 1000u),
            (long)s->now.tv_sec, (unsigned long)s->si.cpu_hz, s->load);
    tprintf(&t, "\"mem\":{\"total\":%lu,\"free\":%lu},\"kmem\":{\"total\":%lu,\"free\":%lu},\"dma\":{\"total\":%lu,\"free\":%lu},",
            (unsigned long)s->si.mem_total, (unsigned long)s->si.mem_free, (unsigned long)s->si.kmem_total,
            (unsigned long)s->si.kmem_free, (unsigned long)s->si.dma_total, (unsigned long)s->si.dma_free);
    tprintf(&t, "\"procs\":[");
    for (int i = 0; i < s->nprocs; i++) {
        const struct crtos_procinfo *p = &s->procs[i];
        tprintf(&t, "%s{\"pid\":%ld,\"name\":\"%s\",\"threads\":%u,\"handles\":%u,\"arena\":%lu,\"heap\":%lu,\"cpu_ms\":%lu}",
                i ? "," : "", (long)p->pid, p->name, p->nthreads, p->nhandles, (unsigned long)p->arena_size,
                (unsigned long)p->heap_used, (unsigned long)(p->cycles / (s->si.cpu_hz / 1000u)));
    }
    tprintf(&t, "],\"net\":[");
    for (int i = 0; i < s->nifs; i++) {
        const struct net_ifinfo *in = &s->ifs[i];
        char a[INET_ADDRSTRLEN], g[INET_ADDRSTRLEN];
        tprintf(&t, "%s{\"name\":\"%s\",\"link\":%s,\"speed\":%lu,\"addr\":\"%s\",\"prefix\":%d,\"gw\":\"%s\","
                    "\"rx_packets\":%lu,\"rx_bytes\":%lu,\"tx_packets\":%lu,\"tx_bytes\":%lu}",
                i ? "," : "", in->name, in->flags & NET_IF_LINK ? "true" : "false", (unsigned long)in->speed,
                ip(in->addr, a), __builtin_popcount(in->netmask), ip(in->gw, g), (unsigned long)in->rx_packets,
                (unsigned long)in->rx_bytes, (unsigned long)in->tx_packets, (unsigned long)in->tx_bytes);
    }
    tprintf(&t, "]}\n");
    reply_text(fd, head, 200, "OK", "application/json", &t);
    free(t.p);
    free(s);
}

static void kb(struct text *t, uint32_t bytes)
{
    tprintf(t, "%lu KB", (unsigned long)(bytes / 1024u));
}

static void status_page(int fd, bool head)
{
    struct state *s = (struct state *)malloc(sizeof(*s));
    if (!s) {
        reply_error(fd, head, 500, "Out of memory");
        return;
    }
    get_state(s);
    struct text t = { 0 };
    tprintf(&t, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" "
                "content=\"width=device-width\"><meta http-equiv=\"refresh\" content=\"5\"><title>CRTOS</title><style>"
                "body{font-family:sans-serif;margin:16px;background:#f6f6f6;color:#222}"
                "table{border-collapse:collapse;margin-bottom:16px}td,th{padding:3px 10px;text-align:left;"
                "border-bottom:1px solid #ddd}th{background:#e8e8e8}td.n{text-align:right}"
                "@media(prefers-color-scheme:dark){body{background:#1d1f21;color:#ddd}th{background:#333}"
                "td,th{border-color:#444}}</style></head><body>");
    uint32_t up = (uint32_t)(s->si.uptime_us / 1000000u);
    time_t now = s->now.tv_sec;
    struct tm tm;
    gmtime_r(&now, &tm);
    tprintf(&t, "<h2>CRTOS on i.MX RT1052</h2><table>"
                "<tr><th>Time</th><td>%04d-%02d-%02d %02d:%02d:%02d UTC</td></tr>"
                "<tr><th>Up</th><td>%lu d %02lu:%02lu:%02lu</td></tr>"
                "<tr><th>CPU</th><td>%lu MHz, %d%% busy</td></tr>",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, (unsigned long)(up / 86400u),
            (unsigned long)(up / 3600u % 24u), (unsigned long)(up / 60u % 60u), (unsigned long)(up % 60u),
            (unsigned long)(s->si.cpu_hz / 1000000u), s->load);
    tprintf(&t, "<tr><th>SDRAM</th><td>");
    kb(&t, s->si.mem_free);
    tprintf(&t, " free of ");
    kb(&t, s->si.mem_total);
    tprintf(&t, "</td></tr><tr><th>Kernel heaps</th><td>");
    kb(&t, s->si.kmem_free);
    tprintf(&t, " free of ");
    kb(&t, s->si.kmem_total);
    tprintf(&t, "</td></tr><tr><th>DMA memory</th><td>");
    kb(&t, s->si.dma_free);
    tprintf(&t, " free of ");
    kb(&t, s->si.dma_total);
    tprintf(&t, "</td></tr></table>");

    tprintf(&t, "<h3>Network</h3><table><tr><th>Interface</th><th>Address</th><th>Link</th><th>Received</th>"
                "<th>Sent</th></tr>");
    for (int i = 0; i < s->nifs; i++) {
        const struct net_ifinfo *in = &s->ifs[i];
        char a[INET_ADDRSTRLEN];
        tprintf(&t, "<tr><td>%s</td><td>%s/%d</td><td>", in->name, ip(in->addr, a), __builtin_popcount(in->netmask));
        if (in->flags & NET_IF_LINK)
            tprintf(&t, "%lu Mbit/s", (unsigned long)in->speed);
        else
            tprintf(&t, "down");
        tprintf(&t, "</td><td class=n>%lu packets, %lu KB</td><td class=n>%lu packets, %lu KB</td></tr>",
                (unsigned long)in->rx_packets, (unsigned long)(in->rx_bytes / 1024u), (unsigned long)in->tx_packets,
                (unsigned long)(in->tx_bytes / 1024u));
    }
    tprintf(&t, "</table>");

    tprintf(&t, "<h3>Processes</h3><table><tr><th>PID</th><th>Name</th><th>Threads</th><th>Handles</th>"
                "<th>Memory</th><th>CPU time</th></tr>");
    for (int i = 0; i < s->nprocs; i++) {
        const struct crtos_procinfo *p = &s->procs[i];
        uint64_t ms = p->cycles / (s->si.cpu_hz / 1000u);
        tprintf(&t, "<tr><td class=n>%ld</td><td>", (long)p->pid);
        tescape(&t, p->name);
        tprintf(&t, "</td><td class=n>%u</td><td class=n>%u</td><td class=n>", p->nthreads, p->nhandles);
        kb(&t, p->arena_size);
        tprintf(&t, "</td><td class=n>%lu.%03lu s</td></tr>", (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u));
    }
    tprintf(&t, "</table><p><a href=\"/api/status\">JSON</a></p></body></html>\n");
    reply_text(fd, head, 200, "OK", "text/html; charset=utf-8", &t);
    free(t.p);
    free(s);
}

/* ---- files ------------------------------------------------------------------------------------------ */

static const char *mime(const char *path)
{
    static const char *const map[][2] = {
        { ".html", "text/html; charset=utf-8" }, { ".htm", "text/html; charset=utf-8" },
        { ".css", "text/css" }, { ".js", "text/javascript" }, { ".json", "application/json" },
        { ".txt", "text/plain; charset=utf-8" }, { ".cfg", "text/plain; charset=utf-8" },
        { ".png", "image/png" }, { ".jpg", "image/jpeg" }, { ".jpeg", "image/jpeg" }, { ".gif", "image/gif" },
        { ".svg", "image/svg+xml" }, { ".ico", "image/x-icon" }, { ".bmp", "image/bmp" },
    };
    const char *dot = strrchr(path, '.');
    if (dot)
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
            if (!strcasecmp(dot, map[i][0]))
                return map[i][1];
    return "application/octet-stream";
}

static void listing(int fd, bool head, const char *url, const char *path)
{
    DIR *d = opendir(path);
    if (!d) {
        reply_error(fd, head, 404, "Not Found");
        return;
    }
    struct text t = { 0 };
    tprintf(&t, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>");
    tescape(&t, url);
    tprintf(&t, "</title></head><body><h2>");
    tescape(&t, url);
    tprintf(&t, "</h2><ul>");
    const char *slash = url[strlen(url) - 1] == '/' ? "" : "/";
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        tprintf(&t, "<li><a href=\"");
        tescape(&t, url);
        tprintf(&t, "%s", slash);
        tescape(&t, e->d_name);
        tprintf(&t, "%s\">", e->d_type == DT_DIR ? "/" : "");
        tescape(&t, e->d_name);
        if (e->d_type == DT_DIR)
            tprintf(&t, "/</a></li>");
        else
            tprintf(&t, "</a> (%lu bytes)</li>", (unsigned long)e->d_size);
    }
    closedir(d);
    tprintf(&t, "</ul></body></html>\n");
    reply_text(fd, head, 200, "OK", "text/html; charset=utf-8", &t);
    free(t.p);
}

static void send_file(int fd, bool head, const char *url, const char *path)
{
    struct stat st;
    if (stat(path, &st)) {
        reply_error(fd, head, 404, "Not Found");
        return;
    }
    if (S_ISDIR(st.st_mode)) {
        char index[300];
        snprintf(index, sizeof(index), "%s/index.html", path);
        if (!stat(index, &st) && !S_ISDIR(st.st_mode))
            path = index;
        else {
            listing(fd, head, url, path);
            return;
        }
    }
    int f = open(path, O_RDONLY);
    if (f < 0) {
        reply_error(fd, head, 403, "Forbidden");
        return;
    }
    header(fd, 200, "OK", mime(path), (long long)st.st_size);
    if (!head) {
        char *buf = (char *)malloc(8192);
        if (buf) {
            int n;
            while ((n = (int)read(f, buf, 8192)) > 0)
                if (send_all(fd, buf, (size_t)n))
                    break;
            free(buf);
        }
    }
    close(f);
}

/* %xx decoding in place; false if the path is not acceptable */
static bool decode_path(char *p)
{
    char *w = p;
    for (char *r = p; *r; r++) {
        if (*r == '%' && r[1] && r[2]) {
            char hex[3] = { r[1], r[2], 0 };
            char *end;
            long v = strtol(hex, &end, 16);
            if (*end || v <= 0)
                return false;
            *w++ = (char)v;
            r += 2;
        } else if (*r == '?') {
            break;
        } else {
            *w++ = *r;
        }
    }
    *w = 0;
    return p[0] == '/' && !strstr(p, "/..") && !strchr(p, '\\');
}

static void serve(int fd)
{
    struct timeval tv = { 10, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char *req = (char *)malloc(MAX_REQUEST + 1);
    if (!req)
        return;
    size_t n = 0;
    while (n < MAX_REQUEST) {
        int r = (int)recv(fd, req + n, MAX_REQUEST - n, 0);
        if (r <= 0)
            break;
        n += (size_t)r;
        req[n] = 0;
        if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n"))
            break;
    }
    req[n] = 0;
    char method[8], url[256];
    if (!n || sscanf(req, "%7s %255s", method, url) != 2) {
        free(req);
        return;
    }
    free(req);
    bool head = !strcmp(method, "HEAD");
    if (!head && strcmp(method, "GET")) {
        reply_error(fd, false, 501, "Not Implemented");
        return;
    }
    if (!decode_path(url)) {
        reply_error(fd, head, 400, "Bad Request");
        return;
    }
    if (!strcmp(url, "/status")) {
        status_page(fd, head);
        return;
    }
    if (!strcmp(url, "/api/status")) {
        status_json(fd, head);
        return;
    }
    char path[300];
    snprintf(path, sizeof(path), "%s%s", s_root, url);
    size_t len = strlen(path);
    if (len > 1 && path[len - 1] == '/')
        path[len - 1] = 0;
    struct stat st;
    if (!strcmp(url, "/") && stat(path, &st)) { /* no web root: the status page */
        status_page(fd, head);
        return;
    }
    send_file(fd, head, url, path);
}

static int s_listen;

static void *worker(void *arg)
{
    (void)arg;
    for (;;) {
        int c = accept(s_listen, NULL, NULL);
        if (c < 0) {
            crtos_sleep_ms(100);
            continue;
        }
        serve(c);
        close(c);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    int port = 80, threads = 3, opt;
    while ((opt = getopt(argc, argv, "p:r:t:")) != -1) {
        switch (opt) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'r':
            s_root = optarg;
            break;
        case 't':
            threads = atoi(optarg);
            break;
        default:
            fprintf(stderr, "usage: httpd [-p port] [-r root] [-t threads]\n");
            return 2;
        }
    }
    threads = threads < 1 ? 1 : threads > MAX_THREADS ? MAX_THREADS : threads;
    while ((s_listen = socket(AF_INET, SOCK_STREAM, 0)) < 0) /* the stack may still be loading */
        crtos_sleep_ms(500);
    int on = 1;
    setsockopt(s_listen, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (bind(s_listen, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(s_listen, 8) < 0) {
        printf("httpd: port %d: %s\n", port, strerror(errno));
        return 1;
    }
    printf("httpd: serving %s on port %d\n", s_root, port);
    for (int i = 1; i < threads; i++)
        if (!crtos_thread_start(worker, NULL, 6144, 0))
            printf("httpd: no thread %d: %s\n", i, strerror(errno));
    worker(NULL);
    return 0;
}
