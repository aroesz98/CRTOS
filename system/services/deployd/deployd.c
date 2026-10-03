/*
 * deployd - installs files sent over the network (layer 2 service, for development):
 * "crtos deploy" sends the changed files of the build, much faster than the
 * debug probe or the serial line.
 *
 * TCP port 5555, one command per line, each answered "OK ..." or "ERR <reason>":
 *   AUTH <token>                   the token in /sd/crtos/etc/deploy.token (read each time)
 *   PUT <path> <size> <crc32>      then <size> bytes; written to <path>.part, checked, then
 *                                  put in place of <path>. On /flash0 the old <path> goes
 *                                  first: a file there is one run of blocks that cannot
 *                                  move, so a long one could not be written while its old
 *                                  version still takes its place (a failed PUT leaves none);
 *                                  and the size goes to the file system first, so the file
 *                                  takes the shortest free run that holds it
 *   CRC <path>                     -> OK <size> <crc32>
 *   GET <path>                     -> OK <size>, then <size> bytes, then "OK <crc32>" (or
 *                                     "ERR ..." when the file could not be read to its end)
 *   STAT <path>                    -> OK <d|f> <size> <mtime>
 *   LIST <dir>                     -> OK <n>, then n lines "<d|f> <size> <mtime> <name>"
 *   MKDIR <path>                   the directory (and the ones on the way to it)
 *   FLASH <path>                   the kernel image at <path> into the boot flash (/dev/mtd0,
 *                                  "OK writing ...", then the board restarts)
 *   REBOOT
 *   QUIT
 * Paths (and the names LIST gives) are encoded as in URLs: %XX for a space, '%' and anything
 * below '!' (crtos scp sends names with spaces so). Only paths under /sd/crtos/ (the system's
 * part of the card), /flash0/ (the flash file system) and /ram/ without ".." are written; read
 * may be all of /sd, /flash0 and /ram; nothing before AUTH, and without a token file every
 * AUTH fails. UDP port 5555 answers "CRTOS?" with "CRTOS <mac>" (the sender learns the
 * address), so the tools find the board.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <crtos.h>
#include <crtos/flashfs.h>
#include <crtos/mtd.h>

#define PORT        5555
#define TOKEN_FILE  "/sd/crtos/etc/deploy.token"
/* where files may be written (nothing else on the card: the user's own files are there) */
static const char *const s_prefixes[] = { "/sd/crtos/", "/flash0/", "/ram/" };
/* what may be read (and listed) */
static const char *const s_read_roots[] = { "/sd", "/flash0", "/ram" };
#define CHUNK       65536
#define MAX_KERNEL  (2u * 1024u * 1024u)

static uint32_t s_crc_table[256];
static char *s_buf;

static void crc_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        s_crc_table[i] = c;
    }
}

static uint32_t crc_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    crc = ~crc;
    while (len--)
        crc = s_crc_table[(crc ^ *p++) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

/* ---- the connection ---------------------------------------------------------------------------- */

struct conn {
    int fd;
    char line[512];
    size_t have;                    /* bytes in line[] not yet used */
};

static int send_str(int fd, const char *s)
{
    size_t len = strlen(s);
    while (len) {
        int w = (int)send(fd, s, len, 0);
        if (w <= 0)
            return -1;
        s += w;
        len -= (size_t)w;
    }
    return 0;
}

static void reply(struct conn *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void reply(struct conn *c, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    send_str(c->fd, buf);
}

/* The next line (without the newline) or NULL when the peer is gone */
static char *read_line(struct conn *c)
{
    for (;;) {
        char *nl = memchr(c->line, '\n', c->have);
        if (nl) {
            *nl = 0;
            if (nl > c->line && nl[-1] == '\r')
                nl[-1] = 0;
            static char out[512];
            strcpy(out, c->line);
            size_t used = (size_t)(nl + 1 - c->line);
            memmove(c->line, nl + 1, c->have - used);
            c->have -= used;
            return out;
        }
        if (c->have >= sizeof(c->line) - 1)
            return NULL; /* too long */
        int n = (int)recv(c->fd, c->line + c->have, sizeof(c->line) - 1 - c->have, 0);
        if (n <= 0)
            return NULL;
        c->have += (size_t)n;
    }
}

/* Up to @max bytes of data: what is left in the line buffer first */
static int read_data(struct conn *c, char *buf, size_t max)
{
    if (c->have) {
        size_t n = c->have < max ? c->have : max;
        memcpy(buf, c->line, n);
        memmove(c->line, c->line + n, c->have - n);
        c->have -= n;
        return (int)n;
    }
    return (int)recv(c->fd, buf, max, 0);
}

/* the length of the allowed prefix @p starts with, 0: none */
static size_t prefix_len(const char *p)
{
    for (size_t i = 0; i < sizeof(s_prefixes) / sizeof(s_prefixes[0]); i++)
        if (!strncmp(p, s_prefixes[i], strlen(s_prefixes[i])))
            return strlen(s_prefixes[i]);
    return 0;
}

static bool path_ok(const char *p)
{
    return prefix_len(p) && !strstr(p, "/..") && !strstr(p, "//") && !strchr(p, '\\') && strlen(p) < 200 &&
           p[strlen(p) - 1] != '/';
}

/* May be read: one of the roots or below it, without ".." */
static bool read_ok(const char *p)
{
    if (strstr(p, "/..") || strstr(p, "//") || strchr(p, '\\') || strlen(p) >= 200)
        return false;
    for (size_t i = 0; i < sizeof(s_read_roots) / sizeof(s_read_roots[0]); i++) {
        size_t n = strlen(s_read_roots[i]);
        if (!strncmp(p, s_read_roots[i], n) && (!p[n] || p[n] == '/'))
            return true;
    }
    return false;
}

/* %XX -> the byte (in place); false for a broken escape or a NUL */
static bool url_decode(char *s)
{
    char *o = s;
    for (const char *i = s; *i; i++) {
        if (*i != '%') {
            *o++ = *i;
            continue;
        }
        unsigned v = 0;
        for (int k = 1; k <= 2; k++) {
            char c = i[k];
            unsigned d = c >= '0' && c <= '9'   ? (unsigned)(c - '0')
                         : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10)
                         : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10)
                                                : 16u;
            if (d > 15u)
                return false;
            v = v * 16u + d;
        }
        if (!v)
            return false;
        *o++ = (char)v;
        i += 2;
    }
    *o = 0;
    return true;
}

/* A name for a reply line: %XX for what would split it */
static void url_encode(const char *s, char *out, size_t size)
{
    size_t k = 0;
    for (; *s && k + 4 < size; s++) {
        unsigned char c = (unsigned char)*s;
        if (c <= ' ' || c == '%' || c == 127)
            k += (size_t)snprintf(out + k, size - k, "%%%02X", c);
        else
            out[k++] = (char)c;
    }
    out[k] = 0;
}

/* Create the directories on the way to @path (below its allowed prefix) */
static void make_dirs(const char *path)
{
    char dir[256];
    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = 0;
    for (char *s = dir + prefix_len(dir); (s = strchr(s, '/')); s++) {
        *s = 0;
        mkdir(dir, 0755);
        *s = '/';
    }
}

static bool check_token(const char *given)
{
    FILE *f = fopen(TOKEN_FILE, "r");
    if (!f)
        return false;
    char tok[128] = "";
    bool ok = fgets(tok, sizeof(tok), f) != NULL;
    fclose(f);
    tok[strcspn(tok, "\r\n \t")] = 0;
    if (!ok || strlen(tok) < 16 || strlen(given) != strlen(tok))
        return false;
    unsigned diff = 0; /* the same time for every wrong token */
    for (size_t i = 0; tok[i]; i++)
        diff |= (unsigned)(tok[i] ^ given[i]);
    return !diff;
}

static void cmd_put(struct conn *c, const char *path, unsigned long size, uint32_t crc)
{
    bool allowed = path_ok(path);
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s.part", path);
    int f = -1, err = 0;
    uint64_t t0 = crtos_time_us(), t_recv = 0, t_write = 0;
    if (allowed) {
        make_dirs(path);
        bool flash = !strncmp(path, "/flash0/", 8);
        if (flash)
            unlink(path); /* (a running program keeps its code until it ends) */
        f = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        err = f < 0 ? errno : 0;
        uint32_t size32 = (uint32_t)size;
        if (flash && !err && ioctl(f, FLASHFS_IOC_RESERVE, &size32) && errno == ENOSPC)
            err = ENOSPC;
    }
    /* the data is always taken in, so that the next command is read correctly */
    uint32_t got = 0;
    unsigned long left = size;
    uint64_t t_open = crtos_time_us() - t0;
    while (left) {
        /* whole chunks: the card is written in long multi-block commands, never across a
         * sector boundary */
        uint64_t t = crtos_time_us();
        int n = 0, want = (int)(left < CHUNK ? left : CHUNK);
        while (n < want) {
            int r = read_data(c, s_buf + n, (size_t)(want - n));
            if (r <= 0) {
                if (f >= 0) {
                    close(f);
                    unlink(tmp);
                }
                return; /* connection lost */
            }
            n += r;
        }
        t_recv += crtos_time_us() - t;
        got = crc_update(got, s_buf, (size_t)n);
        t = crtos_time_us();
        if (f >= 0 && !err && write(f, s_buf, (size_t)n) != n)
            err = errno ? errno : ENOSPC;
        t_write += crtos_time_us() - t;
        left -= (unsigned long)n;
    }
    if (!allowed) {
        reply(c, "ERR path not allowed");
        return;
    }
    uint64_t t = crtos_time_us();
    if (f >= 0 && close(f) && !err)
        err = errno;
    uint64_t t_close = crtos_time_us() - t;
    if (err) {
        unlink(tmp);
        reply(c, "ERR %s", strerror(err));
        return;
    }
    if (got != crc) {
        unlink(tmp);
        reply(c, "ERR crc %08lx, expected %08lx", (unsigned long)got, (unsigned long)crc);
        return;
    }
    t = crtos_time_us();
    unlink(path); /* FAT renames only to a free name */
    if (rename(tmp, path)) {
        reply(c, "ERR rename: %s", strerror(errno));
        return;
    }
    uint64_t t_rename = crtos_time_us() - t;
    reply(c, "OK %lu bytes in %lu ms: open %lu, receive %lu, write %lu, close %lu, rename %lu", size,
          (unsigned long)((crtos_time_us() - t0) / 1000u), (unsigned long)(t_open / 1000u),
          (unsigned long)(t_recv / 1000u), (unsigned long)(t_write / 1000u), (unsigned long)(t_close / 1000u),
          (unsigned long)(t_rename / 1000u));
}

static void cmd_crc(struct conn *c, const char *path)
{
    if (!path_ok(path)) {
        reply(c, "ERR path not allowed");
        return;
    }
    int f = open(path, O_RDONLY);
    if (f < 0) {
        reply(c, "ERR %s", strerror(errno));
        return;
    }
    uint32_t crc = 0;
    unsigned long size = 0;
    int n;
    while ((n = (int)read(f, s_buf, CHUNK)) > 0) {
        crc = crc_update(crc, s_buf, (size_t)n);
        size += (unsigned long)n;
    }
    close(f);
    reply(c, "OK %lu %08lx", size, (unsigned long)crc);
}

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

/* A file to the peer: its size first, the data, then its CRC (a read error sends zeros for
 * the rest - the peer counts the bytes - and an error instead of the CRC); -1: the peer left */
static int cmd_get(struct conn *c, const char *path)
{
    if (!read_ok(path)) {
        reply(c, "ERR path not allowed");
        return 0;
    }
    int f = open(path, O_RDONLY);
    struct stat st;
    if (f < 0 || fstat(f, &st) || S_ISDIR(st.st_mode)) {
        reply(c, "ERR %s", f < 0 ? strerror(errno) : "a directory");
        if (f >= 0)
            close(f);
        return 0;
    }
    unsigned long size = (unsigned long)st.st_size, left = size;
    reply(c, "OK %lu", size);
    uint32_t crc = 0;
    int err = 0;
    while (left) {
        int want = (int)(left < CHUNK ? left : CHUNK), n = err ? 0 : (int)read(f, s_buf, (size_t)want);
        if (n <= 0) {
            if (!err)
                err = n < 0 ? errno : EIO;
            memset(s_buf, 0, (size_t)want);
            n = want;
        }
        crc = crc_update(crc, s_buf, (size_t)n);
        if (send_all(c->fd, s_buf, (size_t)n)) {
            close(f);
            return -1;
        }
        left -= (unsigned long)n;
    }
    close(f);
    if (err)
        reply(c, "ERR %s", strerror(err));
    else
        reply(c, "OK %08lx", (unsigned long)crc);
    return 0;
}

static void cmd_stat(struct conn *c, const char *path)
{
    struct stat st;
    if (!read_ok(path)) {
        reply(c, "ERR path not allowed");
        return;
    }
    if (stat(path, &st)) {
        reply(c, "ERR %s", strerror(errno));
        return;
    }
    reply(c, "OK %c %lu %lu", S_ISDIR(st.st_mode) ? 'd' : 'f', (unsigned long)st.st_size, (unsigned long)st.st_mtime);
}

static void cmd_list(struct conn *c, const char *path)
{
    if (!read_ok(path)) {
        reply(c, "ERR path not allowed");
        return;
    }
    DIR *d = opendir(path);
    if (!d) {
        reply(c, "ERR %s", strerror(errno));
        return;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)))
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, ".."))
            n++;
    rewinddir(d);
    reply(c, "OK %d", n);
    int sent = 0;
    while (sent < n && (e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        char full[512], name[400];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", path, e->d_name);
        bool ok = !stat(full, &st);
        url_encode(e->d_name, name, sizeof(name));
        reply(c, "%c %lu %lu %s", ok && S_ISDIR(st.st_mode) ? 'd' : 'f', ok ? (unsigned long)st.st_size : 0ul,
              ok ? (unsigned long)st.st_mtime : 0ul, name);
        sent++;
    }
    for (; sent < n; sent++) /* (gone meanwhile) */
        reply(c, "f 0 0 %%3F");
    closedir(d);
}

static void cmd_mkdir(struct conn *c, const char *path)
{
    if (!path_ok(path)) {
        reply(c, "ERR path not allowed");
        return;
    }
    make_dirs(path);
    if (mkdir(path, 0755) && errno != EEXIST) {
        reply(c, "ERR %s", strerror(errno));
        return;
    }
    reply(c, "OK");
}

/* A kernel image sent before (PUT) into the boot flash: the board restarts on success, so
 * "OK writing" is the last line the peer reads; an error follows it only when refused */
static void cmd_flash(struct conn *c, const char *path)
{
    if (!path_ok(path)) {
        reply(c, "ERR path not allowed");
        return;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        reply(c, "ERR %s", strerror(errno));
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *img = size > 0 && size <= MAX_KERNEL ? malloc((size_t)size) : NULL;
    bool ok = img && fread(img, 1, (size_t)size, f) == (size_t)size;
    fclose(f);
    int mtd = ok ? open("/dev/mtd0", O_RDONLY) : -1;
    if (mtd < 0) {
        reply(c, "ERR %s", !ok ? "cannot read the image" : strerror(errno));
        free(img);
        return;
    }
    struct mtd_kernel_update ku = { (uintptr_t)img, (uint32_t)size, crc_update(0, img, (size_t)size) };
    reply(c, "OK writing %ld bytes", size);
    printf("deployd: writing the kernel %s (%ld bytes)\n", path, size);
    crtos_sleep_ms(200);
    ioctl(mtd, MTD_IOC_KERNEL_UPDATE, &ku); /* (returns only when refused) */
    reply(c, "ERR %s", strerror(errno));
    close(mtd);
    free(img);
}

static void session(int fd, const struct sockaddr_in *peer)
{
    struct conn *c = (struct conn *)calloc(1, sizeof(*c));
    if (!c)
        return;
    c->fd = fd;
    struct timeval tv = { 30, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    char pa[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer->sin_addr, pa, sizeof(pa));
    reply(c, "CRTOS deployd 2");
    bool authed = false;
    unsigned files = 0;
    unsigned long bytes = 0;
    char *line;
    while ((line = read_line(c))) {
        char cmd[16] = "", path[256] = "";
        unsigned long size = 0, crc = 0;
        int n = sscanf(line, "%15s %255s %lu %lx", cmd, path, &size, &crc);
        if (n < 1)
            continue;
        if (n >= 2 && !url_decode(path))
            path[0] = 0; /* (allowed nowhere) */
        if (!strcmp(cmd, "QUIT")) {
            reply(c, "OK bye");
            break;
        }
        if (!strcmp(cmd, "AUTH") && n == 2) {
            authed = check_token(path);
            if (!authed) {
                printf("deployd: %s: wrong token\n", pa);
                crtos_sleep_ms(1000); /* slow down guessing */
                reply(c, "ERR wrong token");
                break;
            }
            reply(c, "OK");
        } else if (!authed) {
            reply(c, "ERR AUTH first");
            break;
        } else if (!strcmp(cmd, "PUT")) {
            if (n != 4) { /* how much data follows is unknown: the stream is lost */
                reply(c, "ERR bad PUT");
                break;
            }
            cmd_put(c, path, size, (uint32_t)crc);
            files++;
            bytes += size;
        } else if (!strcmp(cmd, "CRC") && n == 2) {
            cmd_crc(c, path);
        } else if (!strcmp(cmd, "GET") && n == 2) {
            if (cmd_get(c, path))
                break;
            files++;
        } else if (!strcmp(cmd, "STAT") && n == 2) {
            cmd_stat(c, path);
        } else if (!strcmp(cmd, "LIST") && n == 2) {
            cmd_list(c, path);
        } else if (!strcmp(cmd, "MKDIR") && n == 2) {
            cmd_mkdir(c, path);
        } else if (!strcmp(cmd, "FLASH") && n == 2) {
            cmd_flash(c, path);
        } else if (!strcmp(cmd, "REBOOT")) {
            reply(c, "OK rebooting");
            printf("deployd: reboot asked by %s\n", pa);
            crtos_sleep_ms(200);
            crtos_reboot();
            reply(c, "ERR %s", strerror(errno));
        } else {
            reply(c, "ERR unknown command");
        }
    }
    if (files)
        printf("deployd: %u file(s), %lu bytes from %s\n", files, bytes, pa);
    free(c);
}

/* ---- discovery ---------------------------------------------------------------------------------- */

static void answer_discovery(int u)
{
    char msg[64];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    int n = (int)recvfrom(u, msg, sizeof(msg) - 1, 0, (struct sockaddr *)&from, &flen);
    if (n < 6 || memcmp(msg, "CRTOS?", 6))
        return;
    char out[64] = "CRTOS";
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        struct net_ifinfo in;
        memset(&in, 0, sizeof(in));
        in.index = 0;
        if (!ioctl(fd, NET_IOC_IFINFO, &in))
            snprintf(out, sizeof(out), "CRTOS %02x:%02x:%02x:%02x:%02x:%02x", in.mac[0], in.mac[1], in.mac[2], in.mac[3],
                     in.mac[4], in.mac[5]);
        close(fd);
    }
    sendto(u, out, strlen(out), 0, (struct sockaddr *)&from, flen);
}

static int open_socket(int type)
{
    int s = socket(AF_INET, type, 0);
    if (s < 0)
        return -1;
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons(PORT);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 || (type == SOCK_STREAM && listen(s, 2) < 0)) {
        printf("deployd: port %d: %s\n", PORT, strerror(errno));
        close(s);
        return -1;
    }
    return s;
}

int main(void)
{
    crc_init();
    s_buf = (char *)malloc(CHUNK);
    if (!s_buf)
        return 1;
    int l;
    while ((l = socket(AF_INET, SOCK_STREAM, 0)) < 0) /* the stack may still be loading */
        crtos_sleep_ms(500);
    close(l);
    l = open_socket(SOCK_STREAM);
    int u = open_socket(SOCK_DGRAM);
    if (l < 0)
        return 1;
    printf("deployd: listening on port %d\n", PORT);
    for (;;) {
        struct pollfd p[2] = { { l, POLLIN, 0 }, { u, POLLIN, 0 } };
        if (poll(p, u >= 0 ? 2 : 1, -1) < 0)
            continue;
        if (u >= 0 && p[1].revents)
            answer_discovery(u);
        if (p[0].revents) {
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            int c = accept(l, (struct sockaddr *)&peer, &plen);
            if (c >= 0) {
                session(c, &peer);
                close(c);
            }
        }
    }
}
