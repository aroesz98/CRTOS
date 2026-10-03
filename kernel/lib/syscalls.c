/*
 * lib/syscalls.c - the system hooks newlib-nano needs inside the kernel: a small heap for the
 * C library itself (between _pvHeapStart and _pvHeapLimit, see linker/crtos.ld) and no files.
 * _read and _write are the debug console's (fsl_debug_console.c). The kernel's own memory is
 * kmalloc; this only serves what newlib allocates internally.
 *
 * (MCUXpresso IDE builds link NXP's libcr_newlib_nohost, which has the same functions; these
 * take precedence, so both builds behave alike.)
 */
#include <errno.h>
#include <stddef.h>
#include <sys/stat.h>

extern char _pvHeapStart[], _pvHeapLimit[];

void *_sbrk(ptrdiff_t incr)
{
    static char *brk = _pvHeapStart;
    char *prev = brk;
    if (incr > _pvHeapLimit - brk || incr < _pvHeapStart - brk) {
        errno = ENOMEM;
        return (void *)-1;
    }
    brk += incr;
    return prev;
}

int _close(int fd)
{
    (void)fd;
    errno = EBADF;
    return -1;
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    st->st_mode = S_IFCHR; /* the console */
    return 0;
}

int _isatty(int fd)
{
    (void)fd;
    return 1;
}

int _lseek(int fd, int off, int whence)
{
    (void)fd;
    (void)off;
    (void)whence;
    errno = ESPIPE;
    return -1;
}

int _getpid(void)
{
    return 1;
}

int _kill(int pid, int sig)
{
    (void)pid;
    (void)sig;
    errno = EINVAL;
    return -1;
}

void _exit(int status)
{
    (void)status;
    for (;;) {
    }
}
