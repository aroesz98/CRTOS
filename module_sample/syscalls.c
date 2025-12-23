#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "syscalls.h"

// Minimal semihosting-backed _write for newlib-nano.
// Writes to stdout (fd=1) and stderr (fd=2) when a debugger is attached.
// If no debugger is attached, it discards the data but reports success
// so higher-level stdio calls (printf) don't fail noisily.

static inline int debugger_attached(void)
{
    // CoreDebug->DHCSR.C_DEBUGEN bit (0) indicates a connected debugger
    volatile uint32_t const DHCSR = *(volatile uint32_t const *)0xE000EDF0u;
    return (DHCSR & 1u) != 0u;
}

static int semihost_write(const char *buf, int len)
{
    // Semihosting SYS_WRITE (0x05): r0=op, r1=&{fd, ptr, len};
    // Returns: r0 = number of bytes NOT written.
    struct {
        uint32_t fd;
        const char *ptr;
        uint32_t len;
    } args = { 1u, buf, (uint32_t)len };

    uint32_t op = 0x05u; // SYS_WRITE
    void *par = &args;
    uint32_t not_written;
    __asm volatile (
        "mov r0, %1\n"
        "mov r1, %2\n"
        "bkpt 0xAB\n"
        "mov %0, r0\n"
        : "=r"(not_written)
        : "r"(op), "r"(par)
        : "r0", "r1", "memory"
    );
    if (not_written == 0u) return len;
    if (not_written >= (uint32_t)len) return 0; // nothing written
    return (int)((uint32_t)len - not_written);
}

int _write(int fd, const void *buf, size_t count)
{
    if (buf == 0 || count == 0) return 0;

    // Only handle stdout/stderr; others are unsupported.
    if (fd == 1 || fd == 2) {
        // If a board-specific hook exists, use it.
        if (crt_putchar) {
            const char *p = (const char *)buf;
            for (size_t i = 0; i < count; ++i) {
                crt_putchar((unsigned char)p[i]);
            }
            return (int)count;
        }
        if (debugger_attached()) {
            // Best-effort write via semihosting; may be slow.
            return semihost_write((const char *)buf, (int)count);
        } else {
            // No debugger: drop output but claim success to keep printf happy.
            return (int)count;
        }
    }

    // Unsupported FD: report as unused, but don't hard-fail higher-level code.
    return (int)count;
}

int _close(int fd)
{
    (void)fd;
    return 0; // success, nothing to close
}

int _fstat(int fd, struct stat *st)
{
    (void)fd;
    if (st) {
        st->st_mode = S_IFCHR; // character device for stdout/stderr
    }
    return 0;
}

int _isatty(int fd)
{
    return (fd == 1 || fd == 2) ? 1 : 0;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    return (off_t)-1; // unsupported
}

int _read(int fd, void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return 0; // no input available
}
