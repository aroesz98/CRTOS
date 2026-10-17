/*
 * syscalls.c - the C library's system interface (newlib's _read, _write, ... hooks) and the
 * POSIX functions newlib leaves to the platform (chdir, getcwd, dup, sleep, ...).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <poll.h>
#include <crtos.h>
#include <crtos/ioctl.h>

static inline int is_err(long r)
{
    return (unsigned long)r >= (unsigned long)-4095;
}

long crtos_sys(long id, long a0, long a1, long a2, long a3)
{
    long r = __crtos_syscall(id, a0, a1, a2, a3);
    if (is_err(r)) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

#define SYS(id, a0, a1, a2, a3) crtos_sys((id), (long)(a0), (long)(a1), (long)(a2), (long)(a3))

int _read(int fd, char *buf, int len)
{
    return (int)SYS(SYS_READ, fd, buf, len, 0);
}

int _write(int fd, const char *buf, int len)
{
    return (int)SYS(SYS_WRITE, fd, buf, len, 0);
}

int _open(const char *path, int flags, int mode)
{
    (void)mode;
    return (int)SYS(SYS_OPEN, path, flags, 0, 0);
}

int _close(int fd)
{
    return (int)SYS(SYS_CLOSE, fd, 0, 0, 0);
}

off_t _lseek(int fd, off_t off, int whence)
{
    int64_t r = __crtos_syscall64(SYS_LSEEK, fd, (long)off, off < 0 ? -1 : 0, whence);
    if (r < 0 && r >= -4095) {
        errno = (int)-r;
        return -1;
    }
    if (r > 0x7FFFFFFF) {
        errno = EOVERFLOW;
        return -1;
    }
    return (off_t)r;
}

time_t timegm(struct tm *tm); /* posix.c */

/* FAT timestamp (date << 16 | time, UTC: the kernel's wall clock) to seconds since 1970 */
static time_t fat_time(uint32_t t)
{
    if (!t)
        return 0;
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = (int)((t >> 25) & 0x7Fu) + 80;
    tm.tm_mon = (int)((t >> 21) & 0xFu) - 1;
    tm.tm_mday = (int)((t >> 16) & 0x1Fu);
    tm.tm_hour = (int)((t >> 11) & 0x1Fu);
    tm.tm_min = (int)((t >> 5) & 0x3Fu);
    tm.tm_sec = (int)(t & 0x1Fu) * 2;
    return timegm(&tm);
}

static void to_stat(struct stat *st, const struct crtos_stat *cs)
{
    memset(st, 0, sizeof(*st));
    st->st_mode = cs->mode | ((cs->mode & CRTOS_S_IFMT) == CRTOS_S_IFDIR ? 0755 : 0644);
    st->st_size = (off_t)cs->size;
    st->st_nlink = 1;
    st->st_blksize = 512;
    st->st_blocks = (blkcnt_t)((cs->size + 511) / 512);
    st->st_mtime = st->st_atime = st->st_ctime = fat_time(cs->mtime);
}

int _fstat(int fd, struct stat *st)
{
    struct crtos_stat cs;
    if (SYS(SYS_FSTAT, fd, &cs, 0, 0) < 0)
        return -1;
    to_stat(st, &cs);
    return 0;
}

int _stat(const char *path, struct stat *st)
{
    struct crtos_stat cs;
    if (SYS(SYS_STAT, path, &cs, 0, 0) < 0)
        return -1;
    to_stat(st, &cs);
    return 0;
}

int _isatty(int fd)
{
    uint32_t mode;
    long r = __crtos_syscall(SYS_IOCTL, fd, (long)TTY_IOC_GET_MODE, (long)&mode, 0);
    if (is_err(r)) {
        errno = ENOTTY;
        return 0;
    }
    return 1;
}

void *_sbrk(ptrdiff_t incr)
{
    long r = __crtos_syscall(SYS_SBRK, (long)incr, 0, 0, 0);
    if (is_err(r)) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)r;
}

void _exit(int code)
{
    __crtos_syscall(SYS_EXIT, code, 0, 0, 0);
    for (;;) {
    }
}

int _kill(int pid, int sig)
{
    return (int)SYS(SYS_KILL, pid, 128 + sig, 0, 0);
}

int _getpid(void)
{
    return (int)__crtos_syscall(SYS_GETPID, 0, 0, 0, 0);
}

/* Wall clock of the system (microseconds since 1970); starts at 1970 until something sets it */
int64_t crtos_wall_us(void)
{
    return __crtos_syscall64(SYS_TIME_GET, 0, 0, 0, 0);
}

int _gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (tv) {
        uint64_t us = (uint64_t)crtos_wall_us();
        tv->tv_sec = (time_t)(us / 1000000u);
        tv->tv_usec = (suseconds_t)(us % 1000000u);
    }
    return 0;
}

int settimeofday(const struct timeval *tv, const struct timezone *tz)
{
    (void)tz;
    uint64_t us = (uint64_t)tv->tv_sec * 1000000u + (uint64_t)tv->tv_usec;
    return (int)SYS(SYS_TIME_SET, (uint32_t)us, (uint32_t)(us >> 32), 0, 0);
}

clock_t _times(struct tms *t)
{
    clock_t c = (clock_t)(crtos_time_us() / (1000000u / CLOCKS_PER_SEC));
    if (t) {
        t->tms_utime = c;
        t->tms_stime = 0;
        t->tms_cutime = 0;
        t->tms_cstime = 0;
    }
    return c;
}

int clock_gettime(clockid_t id, struct timespec *ts)
{
    uint64_t us = id == CLOCK_REALTIME ? (uint64_t)crtos_wall_us() : crtos_time_us();
    ts->tv_sec = (time_t)(us / 1000000u);
    ts->tv_nsec = (long)(us % 1000000u) * 1000;
    return 0;
}

int _unlink(const char *path)
{
    return (int)SYS(SYS_UNLINK, path, 0, 0, 0);
}

int rmdir(const char *path)
{
    return (int)SYS(SYS_UNLINK, path, 0, 0, 0);
}

int _link(const char *from, const char *to)
{
    (void)from;
    (void)to;
    errno = EMLINK;
    return -1;
}

int rename(const char *from, const char *to)
{
    return (int)SYS(SYS_RENAME, from, to, 0, 0);
}

int _mkdir(const char *path, mode_t mode)
{
    (void)mode;
    return (int)SYS(SYS_MKDIR, path, 0, 0, 0);
}

int mkdir(const char *path, mode_t mode)
{
    return _mkdir(path, mode);
}

int _fork(void)
{
    errno = ENOSYS;
    return -1;
}

int _execve(const char *path, char *const argv[], char *const envp[])
{
    (void)path;
    (void)argv;
    (void)envp;
    errno = ENOSYS;
    return -1;
}

int chdir(const char *path)
{
    return (int)SYS(SYS_CHDIR, path, 0, 0, 0);
}

/* buf NULL: a buffer of @size bytes (256 if 0) from malloc, as in glibc */
char *getcwd(char *buf, size_t size)
{
    char *own = NULL;
    if (!buf) {
        size = size ? size : 256;
        buf = own = malloc(size);
        if (!buf) {
            errno = ENOMEM;
            return NULL;
        }
    }
    if (SYS(SYS_GETCWD, buf, size, 0, 0) < 0) {
        free(own);
        return NULL;
    }
    return buf;
}

int pipe(int fds[2])
{
    return (int)SYS(SYS_PIPE, fds, 0, 0, 0);
}

int crtos_pipe(int fds[2], uint32_t flags)
{
    return (int)SYS(SYS_PIPE, fds, flags, 0, 0);
}

int dup(int fd)
{
    return (int)SYS(SYS_DUP, fd, 0, 0, 0);
}

int dup2(int fd, int newfd)
{
    return (int)SYS(SYS_DUP2, fd, newfd, 0, 0);
}

int fsync(int fd)
{
    return (int)SYS(SYS_FSYNC, fd, 0, 0, 0);
}

unsigned sleep(unsigned seconds)
{
    crtos_sleep_ms(seconds * 1000u);
    return 0;
}

int usleep(useconds_t us)
{
    crtos_sleep_ms((uint32_t)((us + 999u) / 1000u));
    return 0;
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    crtos_sleep_ms((uint32_t)(req->tv_sec * 1000 + (req->tv_nsec + 999999) / 1000000));
    if (rem) {
        rem->tv_sec = 0;
        rem->tv_nsec = 0;
    }
    return 0;
}

int sched_yield(void)
{
    crtos_yield();
    return 0;
}

int ioctl(int fd, unsigned long cmd, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, cmd);
    long arg = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    return (int)SYS(SYS_IOCTL, fd, cmd, arg, 0);
}

/* Open flags only (F_GETFL / F_SETFL: O_NONBLOCK, O_APPEND); descriptor flags are ignored */
int fcntl(int fd, int cmd, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, cmd);
    long arg = __builtin_va_arg(ap, long);
    __builtin_va_end(ap);
    switch (cmd) {
    case F_GETFL: {
        uint32_t fl = 0;
        if (SYS(SYS_IOCTL, fd, FIO_GETFL, &fl, 0) < 0)
            return -1;
        return (int)fl;
    }
    case F_SETFL:
        return (int)SYS(SYS_IOCTL, fd, FIO_SETFL, arg, 0);
    case F_GETFD:
    case F_SETFD:
        return 0;
    case F_DUPFD:
        return dup(fd);
    default:
        errno = EINVAL;
        return -1;
    }
}

int poll(struct pollfd *fds, nfds_t n, int timeout)
{
    return (int)SYS(SYS_POLL, fds, n, timeout < 0 ? CRTOS_FOREVER : (uint32_t)timeout, 0);
}
