/*
 * sh - the CRTOS shell (terminals: serial console, USB, the term window).
 *
 * A command line is a list of pipelines: "a | b | c", joined by ";", "&&" (next only if it
 * succeeded), "||" (next only if it failed) and "&" (in the background). A command may start
 * with assignments (VAR=value cmd: in the environment of that command only; alone they set the
 * shell's variable). Words: '...' literal, "..." with $ expansions and \" \\ \$, \x escapes
 * a character; $NAME, ${NAME}, $? (last exit code), $$ (pid), $0..$9, $#, $@ and $* (script
 * arguments). An unquoted expansion is split at spaces; an unquoted word with * ? or [...] is
 * replaced by the matching file names (in the last part of a path). Redirections: < file,
 * > file, >> file, 2> file, 2>> file, 2>&1, >&2. "#" starts a comment.
 *
 * Built-in commands handle files and processes; anything else runs a program: a path, or a
 * name looked up in $PATH as <dir>/<name>.app, then <dir>/<name>. A file that is not a
 * program (ELF) runs as a script of this shell. Scripts: "sh file [args]", ". file" (in this
 * shell), "set -e" (stop at the first failing command), "set -x" (show each command).
 *
 * All shell variables are environment variables (export only exists for scripts that use
 * it). Ctrl-C ends the program in the foreground or stops a built-in command (cat, cp, ls,
 * rm, sleep, wait...; see interrupted), and the rest of the command line - or of the script
 * - is dropped. Ctrl-] switches the terminal to the kernel monitor ("exit" there returns). At
 * the end of a script or a piped input the shell ends; on a terminal Ctrl-D and Ctrl-C only
 * give a new prompt.
 *
 * On a terminal the command line is edited here (see edit_line): arrows, Home/End, Delete,
 * Insert (overwrite), Ctrl-A/E/K/U/W/L, and Up/Down bring back earlier lines, kept in
 * $HISTFILE ($HOME/.sh_history) across shells.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <crtos.h>

#define LINE_MAX    1024
#define WORDS_MAX   63          /* arguments of one command (the kernel takes 63) */
#define CMDS_MAX    16          /* commands in one line */
#define REDIRS_MAX  4
#define ASSIGNS_MAX 8
#define POOL_SIZE   8192        /* the words of one line */
#define JOBS_MAX    16
#define PATH_LEN    256

extern char **environ;

static int s_status;                /* exit code of the last command */
static int s_jobs[JOBS_MAX];
static bool s_errexit, s_trace;     /* set -e, set -x */
static int s_argc;                  /* script arguments: $0 .. $9, $#, $@ */
static char **s_argv;
static bool s_edit;                 /* on a terminal, the command line edited here (edit_line) */
static bool s_watching;             /* the terminal is raw while a built-in runs (watch) */
static bool s_intr;                 /* Ctrl-C: the rest of the command line is dropped */
static unsigned char s_ahead[64];   /* keys typed while a built-in ran, for the next line */
static int s_nahead, s_ahead_pos;

enum { OP_END, OP_PIPE, OP_SEMI, OP_AND, OP_OR, OP_BG };
enum { R_IN, R_OUT, R_APPEND, R_DUP };

struct redir {
    int fd;                         /* 0, 1, 2 */
    int kind;
    int dupfd;                      /* R_DUP: the handle it becomes a copy of */
    char *target;
};

struct cmd {
    int argc;
    char *argv[WORDS_MAX + 1];
    int nassign;
    char *assign[ASSIGNS_MAX];
    int nredir;
    struct redir redir[REDIRS_MAX];
    int op;                         /* what follows this command */
};

struct line {
    int ncmd;
    struct cmd cmd[CMDS_MAX];
    char pool[POOL_SIZE];
    size_t used;
};

/* ---- helpers ------------------------------------------------------------------------------- */

static const char *errstr(void)
{
    return strerror(errno);
}

/* newlib-nano's printf has no %llu */
static void human(char *buf, size_t size, unsigned long long v)
{
    if (v >= 10ull * 1024 * 1024 * 1024)
        snprintf(buf, size, "%luG", (unsigned long)(v / (1024 * 1024 * 1024)));
    else if (v >= 10ull * 1024 * 1024)
        snprintf(buf, size, "%luM", (unsigned long)(v / (1024 * 1024)));
    else if (v >= 10ull * 1024)
        snprintf(buf, size, "%luK", (unsigned long)(v / 1024));
    else
        snprintf(buf, size, "%lu", (unsigned long)v);
}

/* ---- Ctrl-C while the shell itself works ----------------------------------------------------- */

/* The terminal ends the foreground program on Ctrl-C (TTY_IOC_SET_FG), but a built-in command
 * is no program: it runs in this shell. So while one runs on an edited terminal the terminal
 * is raw (watch): Ctrl-C then comes here as a byte - from the console, the USB line and the
 * term window alike - and the command stops at its next check (interrupted). Keys typed
 * meanwhile wait for the next command line (read_byte). A pipeline with programs keeps the
 * terminal canonical, and Ctrl-C ends its last program; the built-ins in it then see the end of
 * their pipe. */
static void watch(bool on)
{
    if (on == s_watching)
        return;
    s_watching = on;
    ioctl(0, TTY_IOC_SET_MODE, on ? 0 : TTY_MODE_CANON | TTY_MODE_ECHO);
    /* reads never wait meanwhile (the console may say "readable" for a line typed ahead) */
    int fl = fcntl(0, F_GETFL);
    if (fl >= 0)
        fcntl(0, F_SETFL, on ? fl | O_NONBLOCK : fl & ~O_NONBLOCK);
}

/* Has Ctrl-C been typed? The keys that came meanwhile are read now. */
static bool interrupted(void)
{
    while (s_watching && !s_intr) {
        struct pollfd p = { 0, POLLIN, 0 };
        unsigned char c;
        if (poll(&p, 1, 0) <= 0 || !(p.revents & POLLIN) || read(0, &c, 1) != 1)
            break;
        if (c == 3) {
            s_intr = true;
            fflush(stdout);
            write(1, "^C\n", 3); /* (a raw terminal echoes nothing) */
        } else if (s_nahead < (int)sizeof(s_ahead)) {
            s_ahead[s_nahead++] = c; /* (beyond that they are dropped, the terminal drained) */
        }
    }
    return s_intr;
}

/* Sleep @ms; false when Ctrl-C cut it short (looked for every 50 ms) */
static bool nap(uint64_t ms)
{
    uint64_t end = crtos_time_us() + ms * 1000u;
    for (;;) {
        if (interrupted())
            return false;
        uint64_t now = crtos_time_us();
        if (now >= end)
            return true;
        uint64_t left = (end - now + 999u) / 1000u;
        if (!s_watching) {
            crtos_sleep_ms((uint32_t)(left > 0x7FFFFFFFu ? 0x7FFFFFFFu : left));
            return true;
        }
        crtos_sleep_ms(left > 50u ? 50u : (uint32_t)left);
    }
}

/* 0, or -1 with errno (EINTR: Ctrl-C) */
static int copy_stream(FILE *in, FILE *out)
{
    static char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n)
            return -1;
        if (interrupted()) {
            errno = EINTR;
            return -1;
        }
    }
    return 0;
}

static int copy_file(const char *from, const char *to)
{
    FILE *in = fopen(from, "rb");
    if (!in)
        return -1;
    FILE *out = fopen(to, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    int r = copy_stream(in, out);
    fclose(in);
    if (fclose(out))
        r = -1;
    if (r && s_intr)
        unlink(to); /* no half copy that looks like the file */
    return r;
}

static bool is_dir(const char *p)
{
    struct stat st;
    return !stat(p, &st) && S_ISDIR(st.st_mode);
}

/* ---- built-in commands ------------------------------------------------------------------------ */

static int b_help(struct cmd *, FILE *out);
static int b_history(struct cmd *c, FILE *out);
static int b_clear(struct cmd *c, FILE *out);

static int b_cd(struct cmd *c, FILE *out)
{
    (void)out;
    const char *dir = c->argc > 1 ? c->argv[1] : getenv("HOME");
    if (chdir(dir ? dir : "/")) {
        printf("cd: %s: %s\n", dir, errstr());
        return 1;
    }
    char buf[PATH_LEN];
    if (getcwd(buf, sizeof(buf)))
        setenv("PWD", buf, 1);
    return 0;
}

static int b_pwd(struct cmd *c, FILE *out)
{
    (void)c;
    char buf[PATH_LEN];
    fprintf(out, "%s\n", getcwd(buf, sizeof(buf)) ? buf : "?");
    return 0;
}

static void ls_line(FILE *out, const char *name, const struct stat *st)
{
    char sz[16], when[20] = "";
    human(sz, sizeof(sz), (unsigned long long)st->st_size);
    if (st->st_mtime) {
        struct tm tm;
        localtime_r(&st->st_mtime, &tm);
        strftime(when, sizeof(when), "%Y-%m-%d %H:%M", &tm);
    }
    bool dir = S_ISDIR(st->st_mode);
    fprintf(out, "%c  %8s  %16s  %s%s\n", dir ? 'd' : S_ISCHR(st->st_mode) ? 'c' : '-', dir ? "" : sz, when,
            name, dir ? "/" : "");
}

static void ls_one(FILE *out, const char *dir, bool lng)
{
    DIR *d = opendir(dir);
    if (!d) {
        struct stat st;
        if (!stat(dir, &st) && !S_ISDIR(st.st_mode)) { /* a file */
            if (lng)
                ls_line(out, dir, &st);
            else
                fprintf(out, "%s\n", dir);
            return;
        }
        printf("ls: %s: %s\n", dir, errstr());
        return;
    }
    struct dirent *e;
    int col = 0;
    while (!interrupted() && (e = readdir(d))) {
        if (lng) {
            char path[PATH_LEN];
            struct stat st;
            if (snprintf(path, sizeof(path), "%s/%s", dir, e->d_name) >= (int)sizeof(path) || stat(path, &st)) {
                memset(&st, 0, sizeof(st));
                st.st_mode = e->d_type == DT_DIR ? S_IFDIR : S_IFREG;
                st.st_size = (off_t)e->d_size;
            }
            ls_line(out, e->d_name, &st);
        } else {
            int w = fprintf(out, "%s%s", e->d_name, e->d_type == DT_DIR ? "/" : "");
            col += w < 24 ? 24 : w + 2;
            if (col >= 72) {
                fputc('\n', out);
                col = 0;
            } else {
                for (int i = w; i < 24; i++)
                    fputc(' ', out);
            }
        }
    }
    if (col)
        fputc('\n', out);
    closedir(d);
}

static int b_ls(struct cmd *c, FILE *out)
{
    bool lng = false;
    int first = 1;
    if (c->argc > 1 && !strcmp(c->argv[1], "-l")) {
        lng = true;
        first = 2;
    }
    if (first >= c->argc)
        ls_one(out, ".", lng);
    for (int i = first; i < c->argc && !interrupted(); i++) {
        if (c->argc - first > 1)
            fprintf(out, "%s:\n", c->argv[i]);
        ls_one(out, c->argv[i], lng);
    }
    return 0;
}

static FILE *s_builtin_in;          /* standard input of a built-in command (in a pipeline) */

static int b_cat(struct cmd *c, FILE *out)
{
    if (c->argc < 2)
        return copy_stream(s_builtin_in ? s_builtin_in : stdin, out) ? 1 : 0;
    int r = 0;
    for (int i = 1; i < c->argc && !interrupted(); i++) {
        FILE *f = fopen(c->argv[i], "rb");
        if (!f) {
            printf("cat: %s: %s\n", c->argv[i], errstr());
            r = 1;
            continue;
        }
        copy_stream(f, out);
        fclose(f);
    }
    return r;
}

static int b_echo(struct cmd *c, FILE *out)
{
    int first = 1;
    bool newline = true;
    if (c->argc > 1 && !strcmp(c->argv[1], "-n")) {
        newline = false;
        first = 2;
    }
    for (int i = first; i < c->argc; i++)
        fprintf(out, "%s%s", c->argv[i], i + 1 < c->argc ? " " : "");
    if (newline)
        fputc('\n', out);
    return 0;
}

static int each_path(struct cmd *c, int (*fn)(const char *), const char *what)
{
    int r = 0;
    for (int i = 1; i < c->argc; i++)
        if (fn(c->argv[i])) {
            printf("%s: %s: %s\n", what, c->argv[i], errstr());
            r = 1;
        }
    return r;
}

/* mkdir -p: the missing parents too, no error when it exists */
static int mkdir_parents(const char *p)
{
    char buf[PATH_LEN];
    if (snprintf(buf, sizeof(buf), "%s", p) >= (int)sizeof(buf)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    for (char *s = buf + 1; *s; s++) {
        if (*s != '/')
            continue;
        *s = 0;
        if (!is_dir(buf) && mkdir(buf, 0755))
            return -1;
        *s = '/';
    }
    return is_dir(buf) || !mkdir(buf, 0755) ? 0 : -1;
}

static int b_mkdir(struct cmd *c, FILE *o)
{
    if (c->argc > 1 && !strcmp(c->argv[1], "-p")) {
        int r = 0;
        for (int i = 2; i < c->argc; i++)
            if (mkdir_parents(c->argv[i])) {
                printf("mkdir: %s: %s\n", c->argv[i], errstr());
                r = 1;
            }
        return r;
    }
    int r = 0;
    for (int i = 1; i < c->argc; i++)
        if (mkdir(c->argv[i], 0755)) {
            printf("mkdir: %s: %s\n", c->argv[i], errstr());
            r = 1;
        }
    return r;
}

/* a file, or a directory with everything in it */
static int remove_tree(const char *p)
{
    if (!is_dir(p))
        return unlink(p);
    DIR *d = opendir(p);
    if (!d)
        return -1;
    struct dirent *e;
    int r = 0;
    char path[PATH_LEN];
    /* (removing while reading could skip entries: read again until empty) */
    for (bool again = true; again && !r;) {
        again = false;
        rewinddir(d);
        while ((e = readdir(d)) && !r) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            if (interrupted()) {
                errno = EINTR;
                r = -1;
                break;
            }
            if (snprintf(path, sizeof(path), "%s/%s", p, e->d_name) >= (int)sizeof(path)) {
                errno = ENAMETOOLONG;
                r = -1;
                break;
            }
            r = remove_tree(path);
            again = true;
        }
    }
    closedir(d);
    return r ? r : unlink(p);
}

/* rm [-f] [-r] file...: -f no message for what is not there, -r directories with their files */
static int b_rm(struct cmd *c, FILE *o)
{
    bool force = false, recursive = false;
    int i = 1;
    for (; i < c->argc && c->argv[i][0] == '-' && c->argv[i][1]; i++) {
        for (const char *f = c->argv[i] + 1; *f; f++) {
            if (*f == 'f')
                force = true;
            else if (*f == 'r' || *f == 'R')
                recursive = true;
            else {
                printf("rm: -%c: unknown option (-f, -r)\n", *f);
                return 2;
            }
        }
    }
    int r = 0;
    for (; i < c->argc && !interrupted(); i++) {
        const char *p = c->argv[i];
        struct stat st;
        if (stat(p, &st)) {
            if (!force) {
                printf("rm: %s: %s\n", p, errstr());
                r = 1;
            }
            continue;
        }
        if ((recursive ? remove_tree(p) : unlink(p))) {
            if (!s_intr)
                printf("rm: %s: %s\n", p, errstr());
            r = 1;
        }
    }
    return r;
}

/* a new empty file, or the time of an existing one set to now */
static int do_touch(const char *p)
{
    struct stat st;
    if (!stat(p, &st))
        return utime(p, NULL);
    int fd = open(p, O_WRONLY | O_CREAT, 0644);
    return fd < 0 ? -1 : close(fd);
}

static int b_touch(struct cmd *c, FILE *o) { return each_path(c, do_touch, "touch"); }

static int b_mv(struct cmd *c, FILE *o)
{
    if (c->argc != 3) {
        printf("usage: mv <from> <to>\n");
        return 1;
    }
    if (rename(c->argv[1], c->argv[2])) {
        printf("mv: %s\n", errstr());
        return 1;
    }
    return 0;
}

static int b_cp(struct cmd *c, FILE *o)
{
    if (c->argc < 3) {
        printf("usage: cp <from...> <to>\n");
        return 1;
    }
    const char *to = c->argv[c->argc - 1];
    bool into = is_dir(to);
    if (c->argc > 3 && !into) {
        printf("cp: %s: not a directory\n", to);
        return 1;
    }
    int r = 0;
    for (int i = 1; i < c->argc - 1 && !interrupted(); i++) {
        char dest[PATH_LEN];
        if (into) {
            const char *base = strrchr(c->argv[i], '/');
            snprintf(dest, sizeof(dest), "%s/%s", to, base ? base + 1 : c->argv[i]);
        } else {
            snprintf(dest, sizeof(dest), "%s", to);
        }
        if (copy_file(c->argv[i], dest)) {
            if (!s_intr)
                printf("cp: %s: %s\n", c->argv[i], errstr());
            r = 1;
        }
    }
    return r;
}

/* CPU use since the previous "ps" (since boot the first time) */
#define PS_MAX 64
static struct {
    int tid;
    uint64_t cycles;
} s_prev[PS_MAX];
static uint64_t s_prev_total;

static int b_ps(struct cmd *c, FILE *out)
{
    (void)c;
    struct crtos_procinfo pi;
    fprintf(out, " PID PPID NAME             THR  ARENA   HEAP HND CAPS\n");
    for (int i = 0; crtos_proc_info(i, &pi) == 0; i++) {
        char a[16], h[16];
        human(a, sizeof(a), pi.arena_size);
        human(h, sizeof(h), pi.heap_used);
        fprintf(out, "%4d %4d %-16s %3d %6s %6s %3d  %02x%s\n", (int)pi.pid, (int)pi.ppid, pi.name, pi.nthreads, a, h,
                pi.nhandles, (unsigned)pi.caps, pi.state == 2 ? " (ended)" : pi.state == 1 ? " (ending)" : "");
    }
    struct crtos_sysinfo si;
    crtos_sys_info(&si);
    uint64_t total = (uint64_t)si.uptime_us * (si.cpu_hz / 1000000u);
    uint64_t span = total - s_prev_total;
    s_prev_total = total;
    fprintf(out, "\n TID  PID NAME             PRI STATE    CPU%%  STACK\n");
    static const char *const st[] = { "ready", "wait", "susp", "dead", "run" };
    struct crtos_taskinfo ti;
    for (int i = 0; crtos_task_info(i, &ti) == 0; i++) {
        uint64_t prev = 0;
        int slot = -1;
        for (int k = 0; k < PS_MAX; k++) {
            if (s_prev[k].tid == ti.tid) {
                prev = s_prev[k].cycles;
                slot = k;
                break;
            }
            if (slot < 0 && !s_prev[k].tid)
                slot = k;
        }
        uint64_t d = ti.cycles - prev;
        if (slot >= 0) {
            s_prev[slot].tid = ti.tid;
            s_prev[slot].cycles = ti.cycles;
        }
        unsigned pm = span ? (unsigned)(d * 1000u / span) : 0;
        uint32_t used = ti.ustack_size ? ti.ustack_used : ti.kstack_used;
        uint32_t size = ti.ustack_size ? ti.ustack_size : ti.kstack_size;
        fprintf(out, "%4d %4d %-16s %3d %-6s %3u.%u  %lu/%lu\n", (int)ti.tid, (int)ti.pid, ti.name, ti.prio,
                ti.state <= 4 ? st[ti.state] : "?", pm / 10, pm % 10, (unsigned long)used, (unsigned long)size);
    }
    return 0;
}

static int b_kill(struct cmd *c, FILE *o)
{
    if (c->argc < 2) {
        printf("usage: kill <pid> [code]\n");
        return 1;
    }
    if (crtos_kill(atoi(c->argv[1]), c->argc > 2 ? atoi(c->argv[2]) : -4)) {
        printf("kill: %s\n", errstr());
        return 1;
    }
    return 0;
}

static int b_free(struct cmd *c, FILE *out)
{
    (void)c;
    struct crtos_sysinfo si;
    if (crtos_sys_info(&si))
        return 1;
    fprintf(out, "            total       free\n");
    fprintf(out, "sdram  %10lu %10lu\n", (unsigned long)si.mem_total, (unsigned long)si.mem_free);
    fprintf(out, "kernel %10lu %10lu\n", (unsigned long)si.kmem_total, (unsigned long)si.kmem_free);
    fprintf(out, "dma    %10lu %10lu\n", (unsigned long)si.dma_total, (unsigned long)si.dma_free);
    return 0;
}

/* df [path...]: size and free space of the file systems (default: /sd, /flash0, /ram) */
static int b_df(struct cmd *c, FILE *out)
{
    static const char *const defaults[] = { "/sd", "/flash0", "/ram" };
    int n = c->argc > 1 ? c->argc - 1 : (int)(sizeof(defaults) / sizeof(defaults[0]));
    int r = 0;
    fprintf(out, "%-12s %8s %8s %8s  use\n", "file system", "size", "used", "free");
    for (int i = 0; i < n; i++) {
        const char *p = c->argc > 1 ? c->argv[i + 1] : defaults[i];
        uint64_t total, free;
        if (crtos_statfs(p, &total, &free)) {
            if (c->argc > 1) {
                printf("df: %s: %s\n", p, errstr());
                r = 1;
            }
            continue;
        }
        char t[16], u[16], f[16];
        human(t, sizeof(t), total);
        human(u, sizeof(u), total - free);
        human(f, sizeof(f), free);
        fprintf(out, "%-12s %8s %8s %8s %3u%%\n", p, t, u, f, total ? (unsigned)((total - free) * 100 / total) : 0u);
    }
    return r;
}

static int b_uptime(struct cmd *c, FILE *out)
{
    (void)c;
    struct crtos_sysinfo si;
    if (crtos_sys_info(&si))
        return 1;
    uint32_t s = (uint32_t)(si.uptime_us / 1000000u);
    uint64_t total = (uint64_t)si.uptime_us * (si.cpu_hz / 1000000u);
    unsigned idle = total ? (unsigned)(si.idle_cycles * 1000u / total) : 0;
    fprintf(out, "up %lu:%02lu:%02lu, %lu processes, %lu threads, idle %u.%u%%\n", (unsigned long)(s / 3600),
            (unsigned long)(s / 60 % 60), (unsigned long)(s % 60), (unsigned long)si.nprocs, (unsigned long)si.ntasks,
            idle / 10, idle % 10);
    return 0;
}

/* The time zone the system uses (settings writes it) */
static void load_tz(void)
{
    char tz[64] = "CET-1CEST,M3.5.0,M10.5.0/3";
    FILE *f = fopen("/sd/crtos/etc/timezone", "r");
    if (f) {
        if (fgets(tz, sizeof(tz), f))
            tz[strcspn(tz, "\r\n")] = 0;
        fclose(f);
    }
    setenv("TZ", tz, 1);
    tzset();
}

static int b_date(struct cmd *c, FILE *o)
{
    if (c->argc > 1) {
        int y, mo, d, h = 0, mi = 0, s = 0;
        if (sscanf(c->argv[1], "%d-%d-%d", &y, &mo, &d) != 3 ||
            (c->argc > 2 && sscanf(c->argv[2], "%d:%d:%d", &h, &mi, &s) < 2)) {
            fprintf(o, "usage: date [YYYY-MM-DD [HH:MM[:SS]]]\n");
            return 2;
        }
        struct tm tm;
        memset(&tm, 0, sizeof(tm));
        tm.tm_year = y - 1900;
        tm.tm_mon = mo - 1;
        tm.tm_mday = d;
        tm.tm_hour = h;
        tm.tm_min = mi;
        tm.tm_sec = s;
        tm.tm_isdst = -1;
        struct timeval tv = { mktime(&tm), 0 };
        if (settimeofday(&tv, NULL)) {
            fprintf(o, "date: %s\n", errstr());
            return 1;
        }
    }
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &tm);
    fprintf(o, "%s\n", buf);
    return 0;
}

static int b_sleep(struct cmd *c, FILE *o)
{
    return nap(c->argc > 1 ? (uint64_t)(atof(c->argv[1]) * 1000.0) : 1000u) ? 0 : 1;
}

static int b_insmod(struct cmd *c, FILE *o)
{
    if (c->argc < 2)
        return 1;
    char path[PATH_LEN];
    if (strchr(c->argv[1], '/'))
        snprintf(path, sizeof(path), "%s", c->argv[1]);
    else
        snprintf(path, sizeof(path), "/sd/crtos/drivers/%s.ko", c->argv[1]);
    if (crtos_module_load(path)) {
        printf("insmod: %s: %s\n", path, errstr());
        return 1;
    }
    return 0;
}

static int b_rmmod(struct cmd *c, FILE *o)
{
    if (c->argc < 2 || crtos_module_unload(c->argv[1])) {
        printf("rmmod: %s\n", errstr());
        return 1;
    }
    return 0;
}

static int b_reboot(struct cmd *c, FILE *o)
{
    (void)c;
    fflush(stdout);
    crtos_reboot();
    printf("reboot: %s\n", errstr());
    return 1;
}

static int b_exit(struct cmd *c, FILE *o)
{
    watch(false);
    fflush(stdout);
    exit(c->argc > 1 ? atoi(c->argv[1]) : s_status);
}

static int b_jobs(struct cmd *c, FILE *out)
{
    (void)c;
    for (int i = 0; i < JOBS_MAX; i++)
        if (s_jobs[i])
            fprintf(out, "[%d]\n", s_jobs[i]);
    return 0;
}

static int b_wait(struct cmd *c, FILE *o)
{
    int pid = c->argc > 1 ? atoi(c->argv[1]) : -1, code = 0, r;
    do /* (in slices while a key may come: Ctrl-C stops the waiting, not the programs) */
        r = crtos_wait(pid, &code, s_watching ? 100u : CRTOS_FOREVER);
    while (r < 0 && errno == ETIMEDOUT && !interrupted());
    for (int i = 0; i < JOBS_MAX; i++)
        if (s_jobs[i] == r)
            s_jobs[i] = 0;
    return r < 0 ? 1 : code;
}

static int run_line(const char *text);

static int b_time(struct cmd *c, FILE *o)
{
    char line[LINE_MAX] = "";
    for (int i = 1; i < c->argc; i++) {
        strncat(line, c->argv[i], sizeof(line) - strlen(line) - 2);
        strcat(line, " ");
    }
    uint64_t t0 = crtos_time_us();
    int r = run_line(line);
    uint64_t us = crtos_time_us() - t0;
    printf("%lu.%03lu s\n", (unsigned long)(us / 1000000u), (unsigned long)(us / 1000u % 1000u));
    return r;
}

/* ---- variables, scripts ---------------------------------------------------------------------- */

static bool valid_name(const char *s, size_t n)
{
    if (!n || !(s[0] == '_' || (s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')))
        return false;
    for (size_t i = 1; i < n; i++)
        if (!(s[i] == '_' || (s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z') ||
              (s[i] >= '0' && s[i] <= '9')))
            return false;
    return true;
}

/* "NAME=value" -> sets it; false when it is not an assignment */
static bool assign(const char *s)
{
    const char *eq = strchr(s, '=');
    if (!eq || !valid_name(s, (size_t)(eq - s)))
        return false;
    char name[64];
    size_t n = (size_t)(eq - s) < sizeof(name) - 1 ? (size_t)(eq - s) : sizeof(name) - 1;
    memcpy(name, s, n);
    name[n] = 0;
    setenv(name, eq + 1, 1);
    return true;
}

static int b_export(struct cmd *c, FILE *out)
{
    if (c->argc == 1) {
        for (char **e = environ; e && *e; e++)
            fprintf(out, "export %s\n", *e);
        return 0;
    }
    int r = 0;
    for (int i = 1; i < c->argc; i++) {
        if (strchr(c->argv[i], '=') ? !assign(c->argv[i]) : !valid_name(c->argv[i], strlen(c->argv[i]))) {
            printf("export: %s: not a valid name\n", c->argv[i]);
            r = 1;
        }
    }
    return r;
}

static int b_unset(struct cmd *c, FILE *out)
{
    (void)out;
    for (int i = 1; i < c->argc; i++)
        unsetenv(c->argv[i]);
    return 0;
}

static int b_env(struct cmd *c, FILE *out)
{
    (void)c;
    for (char **e = environ; e && *e; e++)
        fprintf(out, "%s\n", *e);
    return 0;
}

static int b_set(struct cmd *c, FILE *out)
{
    if (c->argc == 1)
        return b_env(c, out);
    for (int i = 1; i < c->argc; i++) {
        const char *a = c->argv[i];
        if ((a[0] != '-' && a[0] != '+') || !a[1]) {
            printf("set: %s: only -e, -x (+e, +x) here\n", a);
            return 2;
        }
        for (const char *p = a + 1; *p; p++) {
            if (*p == 'e')
                s_errexit = a[0] == '-';
            else if (*p == 'x')
                s_trace = a[0] == '-';
            else {
                printf("set: -%c: unknown option\n", *p);
                return 2;
            }
        }
    }
    return 0;
}

static int b_true(struct cmd *c, FILE *o) { return 0; }
static int b_false(struct cmd *c, FILE *o) { return 1; }

static bool find_program(const char *name, char *path, size_t size);
static bool is_builtin(const char *name);

static int b_which(struct cmd *c, FILE *out)
{
    int r = 0;
    char path[PATH_LEN];
    for (int i = 1; i < c->argc; i++) {
        if (is_builtin(c->argv[i]))
            fprintf(out, "%s: built into the shell\n", c->argv[i]);
        else if (find_program(c->argv[i], path, sizeof(path)))
            fprintf(out, "%s\n", path);
        else
            r = 1;
    }
    return r;
}

/* test / [ : -e -f -d -s -z -n, = != , -eq -ne -lt -le -gt -ge, ! */
static int test_expr(int argc, char **argv)
{
    if (argc > 0 && !strcmp(argv[0], "!"))
        return !test_expr(argc - 1, argv + 1);
    struct stat st;
    if (argc == 0)
        return 0;
    if (argc == 1)
        return argv[0][0] != 0;
    if (argc == 2) {
        const char *op = argv[0], *a = argv[1];
        if (!strcmp(op, "-e"))
            return !stat(a, &st);
        if (!strcmp(op, "-f"))
            return !stat(a, &st) && S_ISREG(st.st_mode);
        if (!strcmp(op, "-d"))
            return !stat(a, &st) && S_ISDIR(st.st_mode);
        if (!strcmp(op, "-s"))
            return !stat(a, &st) && st.st_size > 0;
        if (!strcmp(op, "-z"))
            return !*a;
        if (!strcmp(op, "-n"))
            return *a != 0;
        return -1;
    }
    if (argc == 3) {
        const char *a = argv[0], *op = argv[1], *b = argv[2];
        if (!strcmp(op, "="))
            return !strcmp(a, b);
        if (!strcmp(op, "!="))
            return strcmp(a, b) != 0;
        long x = strtol(a, NULL, 0), y = strtol(b, NULL, 0);
        if (!strcmp(op, "-eq"))
            return x == y;
        if (!strcmp(op, "-ne"))
            return x != y;
        if (!strcmp(op, "-lt"))
            return x < y;
        if (!strcmp(op, "-le"))
            return x <= y;
        if (!strcmp(op, "-gt"))
            return x > y;
        if (!strcmp(op, "-ge"))
            return x >= y;
    }
    return -1;
}

static int b_test(struct cmd *c, FILE *o)
{
    int argc = c->argc - 1;
    if (!strcmp(c->argv[0], "[")) {
        if (argc < 1 || strcmp(c->argv[c->argc - 1], "]")) {
            printf("[: missing ]\n");
            return 2;
        }
        argc--;
    }
    int r = test_expr(argc, c->argv + 1);
    if (r < 0) {
        printf("%s: expression not understood\n", c->argv[0]);
        return 2;
    }
    return r ? 0 : 1;
}

static int run_file(const char *path, int argc, char **argv);

static int b_source(struct cmd *c, FILE *o)
{
    if (c->argc < 2) {
        printf("usage: . <file> [args]\n");
        return 2;
    }
    return run_file(c->argv[1], c->argc - 1, c->argv + 1);
}

struct builtin {
    const char *name;
    int (*fn)(struct cmd *c, FILE *out);
    const char *help;
};

static const struct builtin s_builtins[] = {
    { "help", b_help, "this list" },
    { "cd", b_cd, "cd [dir]" },
    { "pwd", b_pwd, "current directory" },
    { "ls", b_ls, "ls [-l] [path...]" },
    { "cat", b_cat, "cat [file...] (no file: standard input)" },
    { "echo", b_echo, "echo [-n] <text>" },
    { "mkdir", b_mkdir, "mkdir [-p] <dir...>" },
    { "rm", b_rm, "rm [-f] [-r] <file or dir...>" },
    { "mv", b_mv, "mv <from> <to>" },
    { "cp", b_cp, "cp <from> <to>, cp <file...> <dir>" },
    { "touch", b_touch, "touch <file...> (new, or the time of now)" },
    { "ps", b_ps, "processes and threads (CPU since the last ps)" },
    { "kill", b_kill, "kill <pid> [code]" },
    { "free", b_free, "memory" },
    { "df", b_df, "df [path...] - size and free space of the file systems" },
    { "uptime", b_uptime, "time since boot, load" },
    { "sleep", b_sleep, "sleep <seconds>" },
    { "date", b_date, "date [YYYY-MM-DD [HH:MM[:SS]]] - show or set the clock" },
    { "time", b_time, "time <command> - run and measure" },
    { "jobs", b_jobs, "background programs" },
    { "wait", b_wait, "wait [pid] - for a background program" },
    { "export", b_export, "export [NAME[=value]...]" },
    { "unset", b_unset, "unset <NAME...>" },
    { "env", b_env, "the environment" },
    { "set", b_set, "set -e|+e (stop at an error), -x|+x (show commands)" },
    { "test", b_test, "test <expr>: -e -f -d -s -z -n FILE/TEXT, A = B, A != B, N -eq/-lt/... M, !" },
    { "[", b_test, "[ <expr> ] - the same as test" },
    { "true", b_true, "exit code 0" },
    { "false", b_false, "exit code 1" },
    { "which", b_which, "which <name...> - the program that runs (where: all of them)" },
    { "clear", b_clear, "clear the terminal and the lines kept above it" },
    { "history", b_history, "the command lines typed (-c: forget them)" },
    { ".", b_source, ". <file> [args] - run a script in this shell" },
    { "source", b_source, "the same as ." },
    { "insmod", b_insmod, "insmod <module|path> - load a driver" },
    { "rmmod", b_rmmod, "rmmod <module>" },
    { "reboot", b_reboot, "restart the board" },
    { "exit", b_exit, "exit [code] (init starts a new shell)" },
};

static const struct builtin *builtin(const char *name)
{
    for (size_t i = 0; i < sizeof(s_builtins) / sizeof(s_builtins[0]); i++)
        if (!strcmp(name, s_builtins[i].name))
            return &s_builtins[i];
    return NULL;
}

static bool is_builtin(const char *name)
{
    return builtin(name) != NULL;
}

static int b_help(struct cmd *c, FILE *out)
{
    (void)c;
    fprintf(out, "built-in commands:\n");
    for (size_t i = 0; i < sizeof(s_builtins) / sizeof(s_builtins[0]); i++)
        fprintf(out, "  %-8s %s\n", s_builtins[i].name, s_builtins[i].help);
    fprintf(out, "other names run <dir>/<name>.app from PATH (%s).\n"
                 "a | b  a ; b  a && b  a || b  a &   < > >> 2> 2>&1   $VAR ${VAR} $? '...' \"...\" * ?\n"
                 "keys: arrows, Home/End, Delete, Insert (overwrite), Up/Down: earlier lines,\n"
                 "Ctrl-A/E start/end, Ctrl-K/U cut to the end/start, Ctrl-W a word, Ctrl-L clear\n"
                 "Ctrl-C ends the program or built-in command (and the rest of the line),\n"
                 "Ctrl-] opens the kernel monitor\n",
            getenv("PATH") ? getenv("PATH") : "");
    return 0;
}

/* ---- words: expansion, file name patterns ---------------------------------------------------- */

struct lexer {
    struct line *l;
    const char *p;
    char *word;                     /* the word being built (in the pool) */
    bool have_word;                 /* even "" counts once quoted */
    bool glob;                      /* an unquoted * ? [ in it */
    const char *err;
};

static bool pool_put(struct lexer *x, char ch)
{
    if (x->l->used >= POOL_SIZE - 1) {
        x->err = "line too long";
        return false;
    }
    x->l->pool[x->l->used++] = ch;
    return true;
}

static void word_start(struct lexer *x)
{
    if (!x->word) {
        x->word = x->l->pool + x->l->used;
        x->glob = false;
        x->have_word = false;
    }
}

/* "*" "?" "[a-z]" against a name */
static bool match(const char *pat, const char *s)
{
    for (;; pat++, s++) {
        switch (*pat) {
        case 0:
            return !*s;
        case '*':
            while (pat[1] == '*')
                pat++;
            if (!pat[1])
                return true;
            for (; *s; s++)
                if (match(pat + 1, s))
                    return true;
            return false;
        case '?':
            if (!*s)
                return false;
            break;
        case '[': {
            if (!*s)
                return false;
            const char *q = pat + 1;
            bool neg = *q == '!' || *q == '^', ok = false;
            if (neg)
                q++;
            for (; *q && *q != ']'; q++) {
                if (q[1] == '-' && q[2] && q[2] != ']') {
                    if (*s >= q[0] && *s <= q[2])
                        ok = true;
                    q += 2;
                } else if (*q == *s) {
                    ok = true;
                }
            }
            if (!*q)
                return *s == '[' && match(pat + 1, s + 1); /* no closing ]: a plain [ */
            if (ok == neg)
                return false;
            pat = q;
            break;
        }
        default:
            if (*pat != *s)
                return false;
        }
    }
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* the file names matching @pat (a pattern in its last part only) as words of @c; false: none */
static bool expand_glob(struct lexer *x, struct cmd *c, const char *pat)
{
    const char *slash = strrchr(pat, '/');
    char dir[PATH_LEN];
    if (slash) {
        size_t n = (size_t)(slash - pat);
        if (n >= sizeof(dir))
            return false;
        memcpy(dir, pat, n);
        dir[n] = 0;
        if (!n)
            strcpy(dir, "/");
    }
    const char *name_pat = slash ? slash + 1 : pat;
    DIR *d = opendir(slash ? dir : ".");
    if (!d)
        return false;
    char *found[WORDS_MAX];
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) && n < WORDS_MAX) {
        if (e->d_name[0] == '.' && name_pat[0] != '.')
            continue;
        if (!match(name_pat, e->d_name))
            continue;
        size_t need = (slash ? (size_t)(slash - pat) + 1 : 0) + strlen(e->d_name) + 1;
        if (x->l->used + need > POOL_SIZE) {
            x->err = "line too long";
            break;
        }
        char *w = x->l->pool + x->l->used;
        if (slash)
            snprintf(w, need, "%.*s/%s", (int)(slash - pat), pat, e->d_name);
        else
            strcpy(w, e->d_name);
        x->l->used += need;
        found[n++] = w;
    }
    closedir(d);
    if (!n)
        return false;
    qsort(found, (size_t)n, sizeof(found[0]), cmp_str);
    for (int i = 0; i < n && c->argc < WORDS_MAX; i++)
        c->argv[c->argc++] = found[i];
    return true;
}

/* the word being built is complete: into @c as an argument (or its pattern's matches) */
static char *word_end(struct lexer *x)
{
    if (!x->word)
        return NULL;
    if (!pool_put(x, 0))
        return NULL;
    char *w = x->word;
    x->word = NULL;
    return w;
}

static void add_word(struct lexer *x, struct cmd *c, char *w, bool glob)
{
    if (!w)
        return;
    if (c->argc == 0 && c->nassign < ASSIGNS_MAX && strchr(w, '=') &&
        valid_name(w, (size_t)(strchr(w, '=') - w))) {
        c->assign[c->nassign++] = w;
        return;
    }
    if (glob && expand_glob(x, c, w))
        return;
    if (c->argc < WORDS_MAX)
        c->argv[c->argc++] = w;
    else
        x->err = "too many arguments";
}

/* $... at x->p (after the $): its value, or NULL when "$" is just a character */
static const char *dollar(struct lexer *x, char *tmp, size_t size)
{
    const char *p = x->p;
    if (*p == '?') {
        x->p++;
        snprintf(tmp, size, "%d", s_status);
        return tmp;
    }
    if (*p == '$') {
        x->p++;
        snprintf(tmp, size, "%d", (int)getpid());
        return tmp;
    }
    if (*p == '#') {
        x->p++;
        snprintf(tmp, size, "%d", s_argc > 0 ? s_argc - 1 : 0);
        return tmp;
    }
    if (*p >= '0' && *p <= '9') {
        x->p++;
        int i = *p - '0';
        return i < s_argc ? s_argv[i] : "";
    }
    if (*p == '@' || *p == '*') {
        x->p++;
        tmp[0] = 0;
        for (int i = 1; i < s_argc; i++) {
            strncat(tmp, s_argv[i], size - strlen(tmp) - 2);
            if (i + 1 < s_argc)
                strcat(tmp, " ");
        }
        return tmp;
    }
    const char *name = p;
    size_t n = 0;
    if (*p == '{') {
        const char *e = strchr(p, '}');
        if (!e) {
            x->err = "missing }";
            return "";
        }
        name = p + 1;
        n = (size_t)(e - name);
        x->p = e + 1;
    } else {
        while (valid_name(p, n + 1))
            n++;
        if (!n)
            return NULL;
        x->p = p + n;
    }
    if (n >= size) {
        x->err = "variable name too long";
        return "";
    }
    memcpy(tmp, name, n);
    tmp[n] = 0;
    const char *v = getenv(tmp);
    return v ? v : "";
}

/* ---- parsing ----------------------------------------------------------------------------------- */

static bool is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

/* one line -> commands; false with a message on a syntax error */
static bool parse(const char *text, struct line *l)
{
    memset(l, 0, offsetof(struct line, pool));
    l->used = 0;
    struct lexer x;
    memset(&x, 0, sizeof(x));
    x.l = l;
    x.p = text;
    struct cmd *c = &l->cmd[0];
    l->ncmd = 1;
    struct redir *pending = NULL;   /* a redirection waiting for its file name */
    char tmp[LINE_MAX];

    for (;;) {
        char ch = *x.p;
        bool end_word = !ch || is_space(ch) || ch == '|' || ch == ';' || ch == '&' || ch == '<' || ch == '>' ||
                        (ch == '#' && !x.word);
        if (end_word) {
            bool glob = x.glob;
            char *w = (x.word && (x.have_word || x.l->pool + x.l->used > x.word)) ? word_end(&x) : NULL;
            if (x.word) {
                x.word = NULL; /* empty and unquoted: nothing */
            }
            if (w) {
                if (pending) {
                    pending->target = w;
                    pending = NULL;
                } else {
                    add_word(&x, c, w, glob);
                }
            }
            if (x.err)
                break;
            if (!ch || ch == '#')
                break;
            if (is_space(ch)) {
                x.p++;
                continue;
            }
            if (pending) {
                x.err = "a redirection without a file name";
                break;
            }
            /* operators */
            int op = -1;
            if (ch == '|' && x.p[1] == '|') {
                op = OP_OR;
                x.p += 2;
            } else if (ch == '&' && x.p[1] == '&') {
                op = OP_AND;
                x.p += 2;
            } else if (ch == '|') {
                op = OP_PIPE;
                x.p++;
            } else if (ch == ';') {
                op = OP_SEMI;
                x.p++;
            } else if (ch == '&') {
                op = OP_BG;
                x.p++;
            }
            if (op >= 0) {
                if (!c->argc && !c->nassign) {
                    x.err = "a command is missing";
                    break;
                }
                c->op = op;
                if (l->ncmd == CMDS_MAX) {
                    x.err = "too many commands in one line";
                    break;
                }
                c = &l->cmd[l->ncmd++];
                continue;
            }
            /* < > >> n> n>> >&n n>&m */
            int fd = ch == '<' ? 0 : 1;
            /* a digit just before (as a whole word) was taken as a word: take it back */
            if (c->argc && x.p > text && x.p[-1] >= '0' && x.p[-1] <= '2' &&
                (x.p - 1 == text || is_space(x.p[-2])) && !strcmp(c->argv[c->argc - 1], (char[]){ x.p[-1], 0 })) {
                fd = x.p[-1] - '0';
                c->argc--;
            }
            if (c->nredir == REDIRS_MAX) {
                x.err = "too many redirections";
                break;
            }
            struct redir *r = &c->redir[c->nredir++];
            r->fd = fd;
            if (ch == '<') {
                r->kind = R_IN;
                x.p++;
            } else if (x.p[1] == '>') {
                r->kind = R_APPEND;
                x.p += 2;
            } else if (x.p[1] == '&' && x.p[2] >= '0' && x.p[2] <= '2') {
                r->kind = R_DUP;
                r->dupfd = x.p[2] - '0';
                x.p += 3;
                continue;
            } else {
                r->kind = R_OUT;
                x.p++;
            }
            pending = r;
            continue;
        }
        /* a character of a word */
        word_start(&x);
        if (ch == '\\' && x.p[1]) {
            x.have_word = true;
            if (!pool_put(&x, x.p[1]))
                break;
            x.p += 2;
        } else if (ch == '\'') {
            const char *e = strchr(x.p + 1, '\'');
            if (!e) {
                x.err = "missing '";
                break;
            }
            x.have_word = true;
            for (const char *q = x.p + 1; q < e; q++)
                if (!pool_put(&x, *q))
                    break;
            x.p = e + 1;
        } else if (ch == '"') {
            x.have_word = true;
            x.p++;
            while (*x.p && *x.p != '"') {
                if (*x.p == '\\' && (x.p[1] == '"' || x.p[1] == '\\' || x.p[1] == '$')) {
                    pool_put(&x, x.p[1]);
                    x.p += 2;
                } else if (*x.p == '$') {
                    x.p++;
                    const char *v = dollar(&x, tmp, sizeof(tmp));
                    if (!v)
                        pool_put(&x, '$');
                    else
                        for (; *v; v++)
                            pool_put(&x, *v);
                } else {
                    pool_put(&x, *x.p++);
                }
                if (x.err)
                    break;
            }
            if (*x.p != '"') {
                x.err = x.err ? x.err : "missing \"";
                break;
            }
            x.p++;
        } else if (ch == '$') {
            x.p++;
            const char *v = dollar(&x, tmp, sizeof(tmp));
            if (!v) {
                pool_put(&x, '$');
            } else {
                /* unquoted: split at spaces into words */
                for (; *v; v++) {
                    if (is_space(*v)) {
                        bool glob = x.glob;
                        char *w = x.l->pool + x.l->used > x.word || x.have_word ? word_end(&x) : NULL;
                        x.word = NULL;
                        if (w) {
                            if (pending) {
                                pending->target = w;
                                pending = NULL;
                            } else {
                                add_word(&x, c, w, glob);
                            }
                        }
                        while (v[1] && is_space(v[1]))
                            v++;
                        if (v[1])
                            word_start(&x);
                    } else {
                        word_start(&x);
                        if (*v == '*' || *v == '?' || *v == '[')
                            x.glob = true;
                        pool_put(&x, *v);
                    }
                }
            }
        } else {
            if (ch == '*' || ch == '?' || ch == '[')
                x.glob = true;
            pool_put(&x, ch);
            x.p++;
        }
        if (x.err)
            break;
    }
    if (!x.err && pending)
        x.err = "a redirection without a file name";
    if (!x.err && l->ncmd > 1 && !c->argc && !c->nassign) {
        if (l->cmd[l->ncmd - 2].op == OP_SEMI || l->cmd[l->ncmd - 2].op == OP_BG)
            l->ncmd--; /* "a ;" and "a &" end the line */
        else
            x.err = "a command is missing";
    }
    if (x.err) {
        fprintf(stderr, "sh: %s\n", x.err);
        return false;
    }
    for (int i = 0; i < l->ncmd; i++)
        l->cmd[i].argv[l->cmd[i].argc] = NULL;
    return true;
}

/* ---- running ----------------------------------------------------------------------------------- */

static bool is_file(const char *p)
{
    struct stat st;
    return !stat(p, &st) && !S_ISDIR(st.st_mode);
}

static bool find_program(const char *name, char *path, size_t size)
{
    if (strchr(name, '/')) {
        snprintf(path, size, "%s", name);
        return is_file(path);
    }
    const char *p = getenv("PATH");
    char dirs[PATH_LEN];
    snprintf(dirs, sizeof(dirs), "%s", p ? p : "/sd/crtos/bin");
    for (char *d = strtok(dirs, ":"); d; d = strtok(NULL, ":")) {
        snprintf(path, size, "%s/%s.app", d, name);
        if (is_file(path))
            return true;
        snprintf(path, size, "%s/%s", d, name);
        if (is_file(path))
            return true;
    }
    return false;
}

static bool is_elf(const char *path)
{
    unsigned char m[4] = { 0 };
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return false;
    bool elf = read(fd, m, 4) == 4 && m[0] == 0x7f && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
    close(fd);
    return elf;
}

static const char *shell_path(void)
{
    const char *s = getenv("SHELL");
    return s && *s ? s : "/sd/crtos/bin/sh.app";
}

/* the environment of a command with assignments in front of it */
static char **command_env(struct cmd *c, char **own)
{
    if (!c->nassign)
        return environ;
    char **env = own;
    int k = 0;
    for (char **e = environ; e && *e && k < 63 - c->nassign; e++) {
        bool replaced = false;
        for (int i = 0; i < c->nassign; i++) {
            size_t l = (size_t)(strchr(c->assign[i], '=') - c->assign[i]);
            if (!strncmp(*e, c->assign[i], l) && (*e)[l] == '=')
                replaced = true;
        }
        if (!replaced)
            env[k++] = *e;
    }
    for (int i = 0; i < c->nassign; i++)
        env[k++] = c->assign[i];
    env[k] = NULL;
    return env;
}

/* the handles a command's 0, 1, 2 become (-1: ours); files opened for it in opened[] */
static int open_redirs(struct cmd *c, int fds[3], int opened[REDIRS_MAX], int *nopened)
{
    *nopened = 0;
    for (int i = 0; i < c->nredir; i++) {
        struct redir *r = &c->redir[i];
        if (r->kind == R_DUP) {
            fds[r->fd] = fds[r->dupfd] >= 0 ? fds[r->dupfd] : r->dupfd;
            continue;
        }
        int flags = r->kind == R_IN ? O_RDONLY : O_WRONLY | O_CREAT | (r->kind == R_APPEND ? O_APPEND : O_TRUNC);
        int h = open(r->target, flags, 0644);
        if (h < 0) {
            printf("%s: %s\n", r->target, errstr());
            for (int k = 0; k < *nopened; k++)
                close(opened[k]);
            return -1;
        }
        opened[(*nopened)++] = h;
        fds[r->fd] = h;
    }
    return 0;
}

static void trace(struct cmd *c)
{
    fprintf(stderr, "+");
    for (int i = 0; i < c->nassign; i++)
        fprintf(stderr, " %s", c->assign[i]);
    for (int i = 0; i < c->argc; i++)
        fprintf(stderr, " %s", c->argv[i]);
    fprintf(stderr, "\n");
}

/* a program with the handles @fds (-1: ours); its pid, or <0 (the exit code to report) */
static int start_program(struct cmd *c, const int fds[3])
{
    char path[PATH_LEN];
    if (!find_program(c->argv[0], path, sizeof(path))) {
        fprintf(stderr, "%s: not found\n", c->argv[0]);
        return -127;
    }
    const char *argv_script[WORDS_MAX + 2];
    const char *const *argv = (const char *const *)c->argv;
    const char *run = path;
    if (!is_elf(path)) { /* a script: this shell runs it */
        argv_script[0] = "sh";
        argv_script[1] = path;
        int k = 2;
        for (int i = 1; i < c->argc && k < WORDS_MAX + 1; i++)
            argv_script[k++] = c->argv[i];
        argv_script[k] = NULL;
        argv = argv_script;
        run = shell_path();
    }
    char *own_env[64];
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = run;
    sp.argv = argv;
    sp.envp = (const char *const *)command_env(c, own_env);
    for (int i = 0; i < 3; i++)
        sp.stdio[i] = fds[i];
    sp.caps = CAP_ALL; /* limited to ours by the kernel */
    fflush(stdout);
    int pid = crtos_spawn(&sp);
    if (pid < 0) {
        printf("%s: %s\n", run, errstr());
        return -126;
    }
    return pid;
}

static int wait_for(int pid)
{
    int code = 0;
    if (crtos_wait(pid, &code, CRTOS_FOREVER) < 0) {
        printf("wait: %s\n", errstr());
        return 1;
    }
    if (code == -EINTR) { /* Ctrl-C (or kill): the rest of the line is dropped too */
        printf("[%d interrupted]\n", pid);
        s_intr = true;
    }
    else if (code == -EFAULT)
        printf("[%d ended by a memory fault]\n", pid);
    return code;
}

/* a built-in command with its output to @outfd (-1: ours), input from @infd; @watch_ok: no
 * program of its pipeline uses the terminal, so Ctrl-C may come to us (watch) */
static int run_builtin(const struct builtin *b, struct cmd *c, int infd, int outfd, bool watch_ok)
{
    /* assignments in front of it: only for its duration */
    char *saved[ASSIGNS_MAX];
    for (int i = 0; i < c->nassign; i++) {
        size_t l = (size_t)(strchr(c->assign[i], '=') - c->assign[i]);
        char name[64];
        snprintf(name, sizeof(name), "%.*s", (int)l, c->assign[i]);
        const char *old = getenv(name);
        saved[i] = old ? strdup(old) : NULL;
        setenv(name, c->assign[i] + l + 1, 1);
    }
    FILE *out = stdout, *in = NULL;
    int fds[3] = { infd, outfd, -1 }, opened[REDIRS_MAX], nopened = 0;
    int r = 1;
    if (open_redirs(c, fds, opened, &nopened) == 0) {
        if (fds[1] >= 0)
            out = fdopen(dup(fds[1]), "w");
        if (fds[0] >= 0)
            in = fdopen(dup(fds[0]), "r");
        if (!out) {
            printf("%s: %s\n", c->argv[0], errstr());
        } else {
            bool was = s_watching, reads_tty = fds[0] < 0 && b->fn == b_cat && c->argc < 2;
            watch(watch_ok && s_edit && !reads_tty);
            s_builtin_in = in;
            r = b->fn(c, out);
            s_builtin_in = NULL;
            fflush(out);
            if (s_intr)
                r = 130; /* (as a shell reports SIGINT) */
            watch(was);
        }
        if (out && out != stdout)
            fclose(out);
        if (in)
            fclose(in);
        for (int k = 0; k < nopened; k++)
            close(opened[k]);
    }
    for (int i = 0; i < c->nassign; i++) {
        size_t l = (size_t)(strchr(c->assign[i], '=') - c->assign[i]);
        char name[64];
        snprintf(name, sizeof(name), "%.*s", (int)l, c->assign[i]);
        if (saved[i]) {
            setenv(name, saved[i], 1);
            free(saved[i]);
        } else {
            unsetenv(name);
        }
    }
    return r;
}

/* commands first..last joined by pipes; the exit code of the last */
static int run_pipeline(struct cmd *cmds, int n, bool background)
{
    /* a line of assignments only */
    if (n == 1 && !cmds[0].argc) {
        for (int i = 0; i < cmds[0].nassign; i++)
            assign(cmds[0].assign[i]);
        return 0;
    }
    if (s_trace)
        for (int i = 0; i < n; i++)
            trace(&cmds[i]);
    int pipes[CMDS_MAX][2];
    for (int i = 0; i < n - 1; i++) {
        if (pipe(pipes[i])) {
            printf("pipe: %s\n", errstr());
            for (int k = 0; k < i; k++) {
                close(pipes[k][0]);
                close(pipes[k][1]);
            }
            return 1;
        }
    }
    int pids[CMDS_MAX] = { 0 }, status = 0, fg = 0;
    const struct builtin *bi[CMDS_MAX];
    bool programs = false, was = s_watching;
    for (int i = 0; i < n; i++)
        programs |= !builtin(cmds[i].argv[0]);
    if (programs)
        watch(false); /* (they may read the terminal: lines, and Ctrl-C ends them) */
    /* the programs first (a built-in runs here, so its readers must exist) */
    for (int i = 0; i < n; i++) {
        pids[i] = 0;
        bi[i] = builtin(cmds[i].argv[0]);
        if (bi[i])
            continue;
        int fds[3] = { i > 0 ? pipes[i - 1][0] : -1, i < n - 1 ? pipes[i][1] : -1, -1 };
        int opened[REDIRS_MAX], nopened;
        if (open_redirs(&cmds[i], fds, opened, &nopened)) {
            pids[i] = -1;
            continue;
        }
        pids[i] = start_program(&cmds[i], fds);
        if (pids[i] > 0)
            fg = pids[i];
        for (int k = 0; k < nopened; k++)
            close(opened[k]);
    }
    /* Ctrl-C ends the last program - also while the built-ins of the pipeline run; afterwards
     * the terminal goes back to whom it served (a script's shell: Ctrl-C ends the script) */
    int32_t prev_fg = 0;
    if (fg && !background) {
        ioctl(0, TTY_IOC_GET_FG, &prev_fg);
        ioctl(0, TTY_IOC_SET_FG, fg);
    }
    /* our copies of the pipe ends the programs use (built-ins keep theirs until done) */
    for (int i = 0; i < n - 1; i++) {
        if (!bi[i]) {
            close(pipes[i][1]);
            pipes[i][1] = -1;
        }
        if (!bi[i + 1]) {
            close(pipes[i][0]);
            pipes[i][0] = -1;
        }
    }
    for (int i = 0; i < n; i++) {
        if (!bi[i])
            continue;
        int infd = i > 0 ? pipes[i - 1][0] : -1, outfd = i < n - 1 ? pipes[i][1] : -1;
        int r = run_builtin(bi[i], &cmds[i], infd, outfd, !programs);
        if (i == n - 1)
            status = r;
        if (outfd >= 0) {
            close(outfd);
            pipes[i][1] = -1;
        }
        if (infd >= 0) {
            close(infd);
            pipes[i - 1][0] = -1;
        }
    }
    if (background) {
        for (int i = 0; i < n; i++) {
            if (pids[i] <= 0)
                continue;
            printf("[%d]\n", pids[i]);
            for (int k = 0; k < JOBS_MAX; k++)
                if (!s_jobs[k]) {
                    s_jobs[k] = pids[i];
                    break;
                }
        }
        watch(was);
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (pids[i] > 0) {
            int code = wait_for(pids[i]);
            if (i == n - 1)
                status = code;
        } else if (pids[i] < 0 && i == n - 1) {
            status = pids[i] == -1 ? 1 : -pids[i];
        }
    }
    if (fg)
        ioctl(0, TTY_IOC_SET_FG, prev_fg);
    watch(was);
    return status;
}

/* The end of the and-or element at @p: the first ; & && || outside quotes, or a comment, or
 * the end of the line; *op tells which. A & of a redirection (2>&1, >&2) is not one. */
static const char *split(const char *p, const char *line, int *op)
{
    char q = 0;
    for (; *p; p++) {
        if (q) {
            if (*p == '\\' && q == '"' && p[1])
                p++;
            else if (*p == q)
                q = 0;
            continue;
        }
        if (*p == '\\' && p[1]) {
            p++;
            continue;
        }
        if (*p == '\'' || *p == '"') {
            q = *p;
            continue;
        }
        if (*p == '#' && (p == line || is_space(p[-1]))) {
            *op = OP_END;
            return p;
        }
        if (*p == ';') {
            *op = OP_SEMI;
            return p;
        }
        if ((*p == '&' && p[1] == '&') || (*p == '|' && p[1] == '|')) {
            *op = *p == '&' ? OP_AND : OP_OR;
            return p;
        }
        if (*p == '&' && (p == line || (p[-1] != '>' && p[-1] != '<'))) {
            *op = OP_BG;
            return p;
        }
    }
    *op = OP_END;
    return p;
}

/* A command line. Each pipeline is parsed - its $ expansions and file names taken - only when
 * its turn comes, so "false; echo $?" shows 1. After && a pipeline runs only if the last one
 * that ran succeeded, after || only if it failed; a skipped one keeps the status (so
 * "false && a || b" runs b). */
static int run_line(const char *text)
{
    static struct line lines[4];    /* nested: time, . file */
    static int depth;
    static char segs[4][LINE_MAX];
    if (depth == (int)(sizeof(lines) / sizeof(lines[0]))) {
        fprintf(stderr, "sh: nested too deep\n");
        return 2;
    }
    int status = s_status, prev = OP_SEMI;
    const char *p = text;
    for (;;) {
        int op;
        const char *e = split(p, text, &op);
        size_t n = (size_t)(e - p);
        char *seg = segs[depth];
        if (n >= LINE_MAX) {
            fprintf(stderr, "sh: line too long\n");
            return 2;
        }
        memcpy(seg, p, n);
        seg[n] = 0;
        bool blank = true;
        for (const char *c = seg; *c; c++)
            if (!is_space(*c))
                blank = false;
        bool run = prev == OP_AND ? status == 0 : prev == OP_OR ? status != 0 : true;
        if (run && !blank && !s_intr) {
            struct line *l = &lines[depth++];
            if (parse(seg, l))
                status = run_pipeline(l->cmd, l->ncmd, op == OP_BG);
            else
                status = 2;
            depth--;
            s_status = status;
        }
        if (op == OP_END || s_intr) /* (Ctrl-C: the rest of the line is dropped) */
            break;
        p = e + (op == OP_AND || op == OP_OR ? 2 : 1);
        prev = op;
    }
    return status;
}

/* ---- scripts and the terminal -------------------------------------------------------------------- */

/* one logical line (a \ at the end continues it) into buf; false at the end */
static bool read_line(FILE *f, char *buf, size_t size)
{
    size_t n = 0;
    for (;;) {
        if (!fgets(buf + n, (int)(size - n), f))
            return n > 0;
        n = strlen(buf);
        if (n >= 2 && buf[n - 2] == '\\' && buf[n - 1] == '\n') {
            n -= 2;
            buf[n] = 0;
            continue;
        }
        return true;
    }
}

static int run_file(const char *path, int argc, char **argv)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        printf("sh: %s: %s\n", path, errstr());
        return 127;
    }
    int old_argc = s_argc;
    char **old_argv = s_argv;
    s_argc = argc;
    s_argv = argv;
    static char line[LINE_MAX];
    int status = 0;
    while (!s_intr && read_line(f, line, sizeof(line))) {
        status = run_line(line);
        if (s_errexit && status != 0)
            break;
    }
    fclose(f);
    s_argc = old_argc;
    s_argv = old_argv;
    return status;
}

/* ---- the command line on a terminal: editing and history ------------------------------------------ */

/* While a command line is typed the terminal is raw (no echo, every key at once) and the line
 * is edited here - the same on the serial console, the USB serial line and the term window:
 * the cursor moves along it, Insert switches between inserting and overwriting (a block
 * cursor: ESC [ 2 q), Up and Down bring back earlier lines. The terminal is canonical again
 * while a program runs, so programs read lines and Ctrl-C ends them as before (while a
 * built-in runs it stays raw: see watch). A line longer than the terminal is wide scrolls
 * sideways. */

#define HIST_MAX    200         /* lines kept in memory; the file is cut back to them */

enum {
    K_NONE = 0x100, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_HOME, K_END, K_DELETE, K_INSERT, K_PGUP, K_PGDN,
    K_WORD_LEFT, K_WORD_RIGHT,
};

static char *s_history[HIST_MAX];
static int s_nhistory;
static char s_histfile[PATH_LEN];
static bool s_overwrite;            /* Insert: typing replaces the character under the cursor */

struct editor {
    char buf[LINE_MAX];
    int len, pos;
    const char *prompt;
    int plen;
    int cols;                       /* of the terminal */
    int hidx;                       /* the history line shown (s_nhistory: the new line) */
    char *typed;                    /* the new line, while an older one is shown */
};

static int term_out(const char *s, size_t n)
{
    while (n) {
        int w = (int)write(1, s, n);
        if (w <= 0)
            return -1;
        s += w;
        n -= (size_t)w;
    }
    return 0;
}

static void hist_push(const char *line)
{
    if (s_nhistory == HIST_MAX) {
        free(s_history[0]);
        memmove(s_history, s_history + 1, (HIST_MAX - 1) * sizeof(s_history[0]));
        s_nhistory--;
    }
    if ((s_history[s_nhistory] = strdup(line)))
        s_nhistory++;
}

/* the history file again, with only the lines in memory */
static void hist_rewrite(void)
{
    FILE *f = *s_histfile ? fopen(s_histfile, "w") : NULL;
    if (!f)
        return;
    for (int i = 0; i < s_nhistory; i++)
        fprintf(f, "%s\n", s_history[i]);
    fclose(f);
}

/* $HISTFILE, else $HOME/.sh_history */
static void hist_load(void)
{
    const char *file = getenv("HISTFILE"), *home = getenv("HOME");
    if (file && *file)
        snprintf(s_histfile, sizeof(s_histfile), "%s", file);
    else if (home && *home)
        snprintf(s_histfile, sizeof(s_histfile), "%s/.sh_history", home);
    FILE *f = *s_histfile ? fopen(s_histfile, "r") : NULL;
    if (!f)
        return;
    static char line[LINE_MAX];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (*line) {
            hist_push(line);
            n++;
        }
    }
    fclose(f);
    if (n > 2 * HIST_MAX)
        hist_rewrite();
}

/* A line typed: kept unless blank or the same as the one before; appended to the file */
static void hist_add(const char *line)
{
    const char *p = line;
    while (is_space(*p))
        p++;
    if (!*p || (s_nhistory && !strcmp(s_history[s_nhistory - 1], line)))
        return;
    hist_push(line);
    FILE *f = *s_histfile ? fopen(s_histfile, "a") : NULL;
    if (f) {
        fprintf(f, "%s\n", line);
        fclose(f);
    }
}

static int b_history(struct cmd *c, FILE *out)
{
    if (c->argc > 1 && !strcmp(c->argv[1], "-c")) {
        for (int i = 0; i < s_nhistory; i++)
            free(s_history[i]);
        s_nhistory = 0;
        hist_rewrite();
        return 0;
    }
    for (int i = 0; i < s_nhistory; i++)
        fprintf(out, "%5d  %s\n", i + 1, s_history[i]);
    return 0;
}

static int b_clear(struct cmd *c, FILE *out)
{
    (void)c;
    fputs("\033[H\033[2J\033[3J", out); /* home, clear the screen and the lines kept above it */
    return 0;
}

static int read_byte(int timeout_ms)
{
    if (s_ahead_pos < s_nahead) { /* typed while a built-in command ran */
        int c = s_ahead[s_ahead_pos++];
        if (s_ahead_pos == s_nahead)
            s_nahead = s_ahead_pos = 0;
        return c;
    }
    if (timeout_ms >= 0) {
        struct pollfd p = { 0, POLLIN, 0 };
        if (poll(&p, 1, timeout_ms) <= 0)
            return -2; /* nothing came */
    }
    unsigned char c;
    return read(0, &c, 1) == 1 ? c : -1;
}

/* A key: a character, a control character or K_* from an escape sequence (VT100 and xterm:
 * ESC [ ... or ESC O ...); -1 at the end of the input */
static int read_key(void)
{
    int c = read_byte(-1);
    if (c != 27)
        return c;
    int c1 = read_byte(50); /* a lone Esc: nothing follows at once */
    if (c1 == -1)
        return -1;
    if (c1 != '[' && c1 != 'O')
        return K_NONE;
    int par[4] = { 0 }, npar = 0, ch;
    for (;;) {
        ch = read_byte(100);
        if (ch < 0)
            return ch == -1 ? -1 : K_NONE;
        if (ch >= '0' && ch <= '9') {
            if (!npar)
                npar = 1;
            par[npar - 1] = par[npar - 1] * 10 + (ch - '0');
        } else if (ch == ';') {
            if (npar < 4)
                npar++;
        } else {
            break;
        }
    }
    bool ctrl = npar >= 2 && par[1] == 5; /* xterm: 5 = Ctrl held */
    switch (ch) {
    case 'A':
        return K_UP;
    case 'B':
        return K_DOWN;
    case 'C':
        return ctrl ? K_WORD_RIGHT : K_RIGHT;
    case 'D':
        return ctrl ? K_WORD_LEFT : K_LEFT;
    case 'H':
        return K_HOME;
    case 'F':
        return K_END;
    case '~':
        switch (par[0]) {
        case 1:
        case 7:
            return K_HOME;
        case 4:
        case 8:
            return K_END;
        case 2:
            return K_INSERT;
        case 3:
            return K_DELETE;
        case 5:
            return K_PGUP;
        case 6:
            return K_PGDN;
        default:
            return K_NONE;
        }
    default:
        return K_NONE;
    }
}

/* The prompt and the part of the line around the cursor, the rest of the terminal line
 * erased, the cursor put where it is in the line */
static int ed_refresh(const struct editor *e)
{
    static char out[LINE_MAX + PATH_LEN + 32];
    int avail = e->cols - 1 - e->plen;
    if (avail < 8)
        avail = 8;
    int start = e->pos > avail - 1 ? e->pos - (avail - 1) : 0;
    int n = e->len - start < avail ? e->len - start : avail;
    int k = snprintf(out, sizeof(out), "\r%s%.*s\033[K\r", e->prompt, n, e->buf + start);
    int col = e->plen + e->pos - start;
    if (col > 0 && k < (int)sizeof(out) - 16)
        k += snprintf(out + k, sizeof(out) - (size_t)k, "\033[%dC", col);
    return term_out(out, (size_t)k);
}

static void ed_set(struct editor *e, const char *text)
{
    snprintf(e->buf, sizeof(e->buf), "%s", text);
    e->len = e->pos = (int)strlen(e->buf);
}

static void ed_history(struct editor *e, int to)
{
    if (to < 0 || to > s_nhistory || to == e->hidx)
        return;
    if (e->hidx == s_nhistory) { /* leaving the new line: keep it */
        free(e->typed);
        e->typed = strdup(e->buf);
    }
    e->hidx = to;
    ed_set(e, to == s_nhistory ? (e->typed ? e->typed : "") : s_history[to]);
}

static void ed_delete(struct editor *e, int from, int to)
{
    if (from < 0)
        from = 0;
    if (to > e->len)
        to = e->len;
    if (from >= to)
        return;
    memmove(e->buf + from, e->buf + to, (size_t)(e->len - to));
    e->len -= to - from;
    e->buf[e->len] = 0;
    if (e->pos > to)
        e->pos -= to - from;
    else if (e->pos > from)
        e->pos = from;
}

static int ed_word_start(const struct editor *e, int pos)
{
    while (pos > 0 && e->buf[pos - 1] == ' ')
        pos--;
    while (pos > 0 && e->buf[pos - 1] != ' ')
        pos--;
    return pos;
}

static int ed_word_end(const struct editor *e, int pos)
{
    while (pos < e->len && e->buf[pos] == ' ')
        pos++;
    while (pos < e->len && e->buf[pos] != ' ')
        pos++;
    return pos;
}

/* A command line typed on the terminal into @line: 1 a line, 0 the end of the input (the
 * terminal is gone), -1 none (Ctrl-C, Ctrl-D on an empty line) */
static int edit_line(const char *prompt, char *line, size_t size)
{
    static struct editor e;
    memset(&e, 0, sizeof(e));
    e.prompt = prompt;
    e.plen = (int)strlen(prompt);
    e.hidx = s_nhistory;
    struct tty_size sz = { 80, 24 };
    e.cols = ioctl(0, TTY_IOC_GET_SIZE, &sz) == 0 && sz.cols >= 20 ? sz.cols : 80;
    ioctl(0, TTY_IOC_SET_MODE, 0);
    if (s_overwrite)
        term_out("\033[2 q", 5);
    int result = 1;
    if (ed_refresh(&e))
        result = 0;
    while (result == 1) {
        int k = read_key();
        if (k == -1) {
            result = 0;
            break;
        }
        if (k == '\r' || k == '\n')
            break;
        switch (k) {
        case 3: /* Ctrl-C */
            e.pos = e.len;
            ed_refresh(&e);
            term_out("^C", 2);
            result = -1;
            break;
        case 4: /* Ctrl-D: nothing typed - a new prompt; else delete */
            if (!e.len)
                result = -1;
            else
                ed_delete(&e, e.pos, e.pos + 1);
            break;
        case 8:
        case 127:
            ed_delete(&e, e.pos - 1, e.pos);
            break;
        case K_DELETE:
            ed_delete(&e, e.pos, e.pos + 1);
            break;
        case K_LEFT:
        case 2: /* Ctrl-B */
            if (e.pos > 0)
                e.pos--;
            break;
        case K_RIGHT:
        case 6: /* Ctrl-F */
            if (e.pos < e.len)
                e.pos++;
            break;
        case K_HOME:
        case 1: /* Ctrl-A */
            e.pos = 0;
            break;
        case K_END:
        case 5: /* Ctrl-E */
            e.pos = e.len;
            break;
        case K_WORD_LEFT:
            e.pos = ed_word_start(&e, e.pos);
            break;
        case K_WORD_RIGHT:
            e.pos = ed_word_end(&e, e.pos);
            break;
        case K_UP:
        case 16: /* Ctrl-P */
            ed_history(&e, e.hidx - 1);
            break;
        case K_DOWN:
        case 14: /* Ctrl-N */
            ed_history(&e, e.hidx + 1);
            break;
        case K_PGUP:
            ed_history(&e, 0);
            break;
        case K_PGDN:
            ed_history(&e, s_nhistory);
            break;
        case K_INSERT:
            s_overwrite = !s_overwrite;
            term_out(s_overwrite ? "\033[2 q" : "\033[0 q", 5);
            break;
        case 11: /* Ctrl-K: to the end */
            ed_delete(&e, e.pos, e.len);
            break;
        case 21: /* Ctrl-U: to the start */
            ed_delete(&e, 0, e.pos);
            break;
        case 23: /* Ctrl-W: the word before */
            ed_delete(&e, ed_word_start(&e, e.pos), e.pos);
            break;
        case 12: /* Ctrl-L: a clean screen */
            term_out("\033[H\033[2J", 7);
            break;
        default:
            if (k >= 32 && k < 127) {
                if (s_overwrite && e.pos < e.len) {
                    e.buf[e.pos++] = (char)k;
                } else if (e.len < (int)sizeof(e.buf) - 1) {
                    memmove(e.buf + e.pos + 1, e.buf + e.pos, (size_t)(e.len - e.pos));
                    e.buf[e.pos++] = (char)k;
                    e.buf[++e.len] = 0;
                }
            }
            break;
        }
        if (result == 1 && ed_refresh(&e))
            result = 0;
    }
    if (result >= 0 && e.len) { /* the whole line stays on the screen as typed */
        e.pos = e.len;
        ed_refresh(&e);
    }
    term_out("\n", 1);
    if (s_overwrite)
        term_out("\033[0 q", 5); /* the programs get the usual cursor */
    ioctl(0, TTY_IOC_SET_MODE, TTY_MODE_CANON | TTY_MODE_ECHO);
    free(e.typed);
    snprintf(line, size, "%s", result == 1 ? e.buf : "");
    return result;
}

static void reap_jobs(void)
{
    int code, pid;
    while ((pid = crtos_wait(-1, &code, 0)) > 0) {
        for (int i = 0; i < JOBS_MAX; i++)
            if (s_jobs[i] == pid)
                s_jobs[i] = 0;
        printf("[%d done, %d]\n", pid, code);
    }
}

/* what every shell should have, also when it was started without an environment */
static void default_env(void)
{
    setenv("PATH", "/flash0/bin:/sd/crtos/bin:/sd/crtos/apps", 0);
    setenv("HOME", "/sd/crtos", 0);
    setenv("TMPDIR", "/ram", 0);
    setenv("SHELL", "/sd/crtos/bin/sh.app", 0);
    load_tz();
}

int main(int argc, char **argv)
{
    static char line[LINE_MAX];
    default_env();
    s_argc = argc;
    s_argv = argv;
    if (argc > 2 && !strcmp(argv[1], "-c")) { /* sh -c command line [name args...] (words joined) */
        size_t n = 0;
        for (int i = 2; i < argc && n < sizeof(line) - 2; i++)
            n += (size_t)snprintf(line + n, sizeof(line) - 1 - n, "%s%s", i > 2 ? " " : "", argv[i]);
        return run_line(line);
    }
    if (argc > 1 && argv[1][0] != '-') /* sh file [args] */
        return run_file(argv[1], argc - 1, argv + 1);
    bool tty = isatty(0);
    s_edit = tty && isatty(1);
    if (tty)
        printf("\nCRTOS shell - 'help' lists the commands\n");
    if (s_edit)
        hist_load();
    for (;;) {
        reap_jobs();
        if (s_edit) {
            char cwd[96], prompt[128];
            snprintf(prompt, sizeof(prompt), "crtos:%s$ ", getcwd(cwd, sizeof(cwd)) ? cwd : "?");
            fflush(stdout);
            int r = edit_line(prompt, line, sizeof(line));
            if (r == 0)
                return s_status; /* nobody types to us any more (a terminal window closed) */
            if (r < 0)
                continue;        /* Ctrl-C, Ctrl-D: a new prompt */
            hist_add(line);
        } else {
            if (tty) {
                char cwd[96];
                printf("crtos:%s$ ", getcwd(cwd, sizeof(cwd)) ? cwd : "?");
                fflush(stdout);
                if (ferror(stdout)) /* nobody reads us any more (a terminal window closed) */
                    return 0;
            }
            if (!read_line(stdin, line, sizeof(line))) {
                if (!tty)
                    return s_status; /* the end of a script or of piped input */
                clearerr(stdin);     /* Ctrl-C (EINTR) or Ctrl-D */
                printf("\n");
                continue;
            }
        }
        s_intr = false;
        s_status = run_line(line);
        if (!tty && s_errexit && s_status != 0)
            return s_status;
    }
}
