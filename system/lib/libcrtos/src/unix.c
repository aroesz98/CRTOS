/*
 * unix.c - more of the POSIX interface that ported tools (compilers, make, binutils) expect:
 * realpath, sysconf, pathconf, getpagesize, getrusage, temporary files in $TMPDIR, sigaction (on
 * newlib's signal()), setting a file's time to now (utime, utimes, utimensat), and the file
 * mode, owner and link functions of a file system that has neither (FAT, ramfs): they accept
 * and change nothing, or report what there is.
 *
 * Temporary files: newlib's tmpfile()/tmpnam() would use /tmp, which does not exist. Here they
 * use $TMPDIR (default /ram, the RAM disk). A file of tmpfile() cannot be removed while it is
 * open (FAT and ramfs refuse), so it is removed when the program ends.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <crtos.h>

#define PAGE_SIZE       4096
#define PATH_LEN        256     /* the kernel's normalised paths are shorter (VFS_PATH_MAX) */
#define DEFAULT_TMPDIR  "/ram"

/* ---- paths -------------------------------------------------------------------------------- */

/* "/a/./b/../c" -> "/a/c" in place (an absolute path) */
static void normalize(char *p)
{
    char *out = p, *s = p;
    while (*s) {
        while (*s == '/')
            s++;
        if (!*s)
            break;
        char *e = s;
        while (*e && *e != '/')
            e++;
        size_t n = (size_t)(e - s);
        if (n == 1 && s[0] == '.') {
            /* nothing */
        } else if (n == 2 && s[0] == '.' && s[1] == '.') {
            while (out > p && *--out != '/') {
            }
        } else {
            *out++ = '/';
            memmove(out, s, n);
            out += n;
        }
        s = e;
    }
    if (out == p)
        *out++ = '/';
    *out = 0;
}

char *realpath(const char *path, char *resolved)
{
    if (!path || !*path) {
        errno = path ? ENOENT : EINVAL;
        return NULL;
    }
    char buf[2 * PATH_LEN];
    if (path[0] == '/') {
        if (strlen(path) >= sizeof(buf)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        strcpy(buf, path);
    } else {
        if (!getcwd(buf, PATH_LEN))
            return NULL;
        size_t n = strlen(buf);
        if (n + 1 + strlen(path) >= sizeof(buf)) {
            errno = ENAMETOOLONG;
            return NULL;
        }
        buf[n] = '/';
        strcpy(buf + n + 1, path);
    }
    normalize(buf);
    if (strlen(buf) >= PATH_LEN) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    struct stat st;
    if (stat(buf, &st))
        return NULL;
    char *out = resolved ? resolved : malloc(PATH_LEN);
    if (!out) {
        errno = ENOMEM;
        return NULL;
    }
    return strcpy(out, buf);
}

/* no symbolic links anywhere */
ssize_t readlink(const char *path, char *buf, size_t size)
{
    (void)buf;
    (void)size;
    struct stat st;
    if (stat(path, &st) == 0)
        errno = EINVAL;
    return -1;
}

int lstat(const char *path, struct stat *st)
{
    return stat(path, st);
}

int symlink(const char *target, const char *path)
{
    (void)target;
    (void)path;
    errno = EPERM;
    return -1;
}

/* ---- modes and owners: FAT and ramfs have none -------------------------------------------- */

static mode_t s_umask = 022;

mode_t umask(mode_t mask)
{
    mode_t old = s_umask;
    s_umask = mask & 0777;
    return old;
}

int chmod(const char *path, mode_t mode)
{
    (void)mode;
    struct stat st;
    return stat(path, &st);
}

int fchmod(int fd, mode_t mode)
{
    (void)mode;
    struct stat st;
    return fstat(fd, &st);
}

int chown(const char *path, uid_t owner, gid_t group)
{
    (void)owner;
    (void)group;
    struct stat st;
    return stat(path, &st);
}

/* The file systems keep the time of the last write only, set by a write: "now" is set by
 * writing the last byte again (an empty file: by truncating it, which counts as a write). A
 * given time cannot be set (ENOSYS). */
static int touch_now(const char *path)
{
    struct stat st;
    if (stat(path, &st))
        return -1;
    if (S_ISDIR(st.st_mode))
        return 0; /* directories: no time to change */
    if (st.st_size == 0) {
        int fd = open(path, O_WRONLY | O_TRUNC);
        return fd < 0 ? -1 : close(fd);
    }
    int fd = open(path, O_RDWR);
    if (fd < 0)
        return -1;
    char ch;
    int r = -1;
    if (lseek(fd, st.st_size - 1, SEEK_SET) >= 0 && read(fd, &ch, 1) == 1 && lseek(fd, st.st_size - 1, SEEK_SET) >= 0 &&
        write(fd, &ch, 1) == 1)
        r = 0;
    close(fd);
    return r;
}

int utime(const char *path, const struct utimbuf *times)
{
    if (times) {
        errno = ENOSYS;
        return -1;
    }
    return touch_now(path);
}

int utimes(const char *path, const struct timeval times[2])
{
    if (times) {
        errno = ENOSYS;
        return -1;
    }
    return touch_now(path);
}

int utimensat(int dirfd, const char *path, const struct timespec times[2], int flags)
{
    (void)flags;
    if (dirfd != AT_FDCWD && path[0] != '/') {
        errno = ENOSYS;
        return -1;
    }
    if (times && !(times[0].tv_nsec == UTIME_NOW && times[1].tv_nsec == UTIME_NOW)) {
        errno = ENOSYS;
        return -1;
    }
    return touch_now(path);
}

/* ---- signals: only within a process (Ctrl-C and faults end it without a signal) ------------ */

int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
    if (sig <= 0 || sig >= NSIG) {
        errno = EINVAL;
        return -1;
    }
    _sig_func_ptr old;
    if (act) {
        old = signal(sig, act->sa_handler);
    } else {
        old = signal(sig, SIG_IGN); /* (the only way to read it) */
        signal(sig, old);
    }
    if (old == SIG_ERR)
        return -1;
    if (oact) {
        memset(oact, 0, sizeof(*oact));
        oact->sa_handler = old;
    }
    return 0;
}

uid_t getuid(void)
{
    return 0;
}

uid_t geteuid(void)
{
    return 0;
}

gid_t getgid(void)
{
    return 0;
}

gid_t getegid(void)
{
    return 0;
}

/* ---- this process ------------------------------------------------------------------------- */

static int self_info(struct crtos_procinfo *pi)
{
    int me = getpid();
    for (int i = 0; crtos_proc_info(i, pi) == 0; i++)
        if (pi->pid == me)
            return 0;
    errno = ESRCH;
    return -1;
}

pid_t getppid(void)
{
    struct crtos_procinfo pi;
    return self_info(&pi) ? 0 : pi.ppid;
}

int getrusage(int who, struct rusage *ru)
{
    memset(ru, 0, sizeof(*ru));
    if (who != RUSAGE_SELF)
        return 0; /* the children's times are not kept */
    struct crtos_procinfo pi;
    struct crtos_sysinfo si;
    if (self_info(&pi) || crtos_sys_info(&si))
        return -1;
    uint64_t us = si.cpu_hz ? pi.cycles / (si.cpu_hz / 1000000u) : 0;
    ru->ru_utime.tv_sec = (time_t)(us / 1000000u);
    ru->ru_utime.tv_usec = (suseconds_t)(us % 1000000u);
    return 0;
}

int getpagesize(void)
{
    return PAGE_SIZE;
}

long sysconf(int name)
{
    struct crtos_sysinfo si;
    switch (name) {
    case _SC_PAGESIZE:
        return PAGE_SIZE;
    case _SC_CLK_TCK:
        return CLOCKS_PER_SEC;
    case _SC_NPROCESSORS_CONF:
    case _SC_NPROCESSORS_ONLN:
        return 1;
    case _SC_OPEN_MAX:
        return 128;
    case _SC_ARG_MAX:
        return 4096;
    case _SC_CHILD_MAX:
        return 32;
    case _SC_PHYS_PAGES:
    case _SC_AVPHYS_PAGES:
        if (crtos_sys_info(&si))
            return -1;
        return (long)((name == _SC_PHYS_PAGES ? si.mem_total : si.mem_free) / PAGE_SIZE);
    default:
        errno = EINVAL;
        return -1;
    }
}

/* the limits of every CRTOS file system (FAT, ramfs, flashfs) are the kernel's */
static long path_limit(int name)
{
    switch (name) {
    case _PC_PATH_MAX:
        return PATH_LEN;
    case _PC_NAME_MAX:
        return 255;
    case _PC_LINK_MAX:
        return 1;
    case _PC_PIPE_BUF:
        return 512;
    case _PC_NO_TRUNC:
        return 1;
    case _PC_CHOWN_RESTRICTED:
        return 1;
    default:
        errno = EINVAL;
        return -1;
    }
}

long pathconf(const char *path, int name)
{
    struct stat st;
    if (stat(path, &st))
        return -1;
    return path_limit(name);
}

long fpathconf(int fd, int name)
{
    struct stat st;
    if (fstat(fd, &st))
        return -1;
    return path_limit(name);
}

/* ---- file systems ------------------------------------------------------------------------- */

int statvfs(const char *path, struct statvfs *b)
{
    uint64_t total, free;
    if (crtos_statfs(path, &total, &free))
        return -1;
    memset(b, 0, sizeof(*b));
    b->f_bsize = b->f_frsize = 4096;
    b->f_blocks = (fsblkcnt_t)(total / 4096u);
    b->f_bfree = b->f_bavail = (fsblkcnt_t)(free / 4096u);
    b->f_namemax = 255;
    return 0;
}

int fstatvfs(int fd, struct statvfs *b)
{
    (void)fd;
    (void)b;
    errno = ENOSYS;
    return -1;
}

/* ---- temporary files ---------------------------------------------------------------------- */

static const char *tmpdir(void)
{
    const char *d = getenv("TMPDIR");
    return d && *d ? d : DEFAULT_TMPDIR;
}

char *tmpnam(char *s)
{
    static char buf[L_tmpnam > PATH_LEN ? L_tmpnam : PATH_LEN];
    static unsigned counter;
    char *out = s ? s : buf;
    for (int tries = 0; tries < 1000; tries++) {
        snprintf(out, s ? L_tmpnam : sizeof(buf), "%s/t%d_%u", tmpdir(), getpid(), counter++);
        struct stat st;
        if (stat(out, &st) && errno == ENOENT)
            return out;
    }
    return NULL;
}

struct tmp {
    struct tmp *next;
    int fd;
    char path[];
};
static struct tmp *s_tmps;

/* at exit (before stdio closes its streams): the handle first, as an open file cannot be
 * removed; what is still in the stream's buffer does not matter any more */
static void remove_tmps(void)
{
    for (struct tmp *t = s_tmps; t; t = t->next) {
        close(t->fd);
        remove(t->path);
    }
}

FILE *tmpfile(void)
{
    char path[PATH_LEN];
    if (snprintf(path, sizeof(path), "%s/tmpXXXXXX", tmpdir()) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return NULL;
    }
    int fd = mkstemp(path);
    if (fd < 0)
        return NULL;
    FILE *f = fdopen(fd, "w+");
    struct tmp *t = f ? malloc(sizeof(*t) + strlen(path) + 1) : NULL;
    if (!t) {
        if (f)
            fclose(f);
        else
            close(fd);
        remove(path);
        errno = ENOMEM;
        return NULL;
    }
    strcpy(t->path, path);
    t->fd = fd;
    if (!s_tmps)
        atexit(remove_tmps);
    t->next = s_tmps;
    s_tmps = t;
    return f;
}
