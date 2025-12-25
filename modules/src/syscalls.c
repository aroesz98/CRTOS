/**
 * @file syscalls.c
 * @brief Simplified syscalls for loadable module with custom printf
 * 
 * Instead of fighting with newlib's complex FILE/reentrant infrastructure,
 * we provide a simple custom printf implementation that works reliably.
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <kernel.h>

/* =============================================================================
 * Simple Printf Implementation
 * ===========================================================================*/

// Helper: convert unsigned to string
static int uint_to_str(uint32_t num, char* buf, int base)
{
    const char digits[] = "0123456789ABCDEF";
    char temp[32];
    int i = 0;
    
    if (num == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    
    while (num > 0) {
        temp[i++] = digits[num % base];
        num /= base;
    }
    
    // Reverse
    for (int j = 0; j < i; j++) {
        buf[j] = temp[i - 1 - j];
    }
    buf[i] = '\0';
    return i;
}

// Simple printf that supports %d, %u, %x, %X, %s, %c, %%, field width, and alignment
int printf(const char* format, ...)
{
    char buffer[256];
    char* ptr = buffer;
    const char* fmt = format;
    va_list args;
    int count = 0;
    
    va_start(args, format);
    
    while (*fmt && (ptr - buffer) < 250) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            
            // Parse flags (-, +, space, 0, #)
            int left_align = 0;
            if (*fmt == '-') {
                left_align = 1;
                fmt++;
            }
            
            // Parse field width
            int width = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
            
            // Skip length modifiers (l, ll, h, hh, z, t) - we treat all ints the same on 32-bit
            while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 't') {
                fmt++;
            }
            
            char temp[32];
            int len = 0;
            
            switch (*fmt) {
                case 'd':
                case 'i': {
                    int val = va_arg(args, int);
                    if (val < 0) {
                        temp[len++] = '-';
                        val = -val;
                    }
                    len += uint_to_str((uint32_t)val, temp + len, 10);
                    break;
                }
                case 'u': {
                    uint32_t val = va_arg(args, uint32_t);
                    len = uint_to_str(val, temp, 10);
                    break;
                }
                case 'x':
                case 'X': {
                    uint32_t val = va_arg(args, uint32_t);
                    len = uint_to_str(val, temp, 16);
                    break;
                }
                case 'p': {
                    uint32_t val = va_arg(args, uint32_t);
                    temp[0] = '0';
                    temp[1] = 'x';
                    len = 2 + uint_to_str(val, temp + 2, 16);
                    break;
                }
                case 's': {
                    const char* str = va_arg(args, const char*);
                    if (!str) str = "(null)";
                    
                    // Calculate string length
                    const char* s = str;
                    while (*s) { s++; len++; }
                    
                    // Apply width and alignment
                    if (width > len) {
                        int padding = width - len;
                        if (left_align) {
                            // Left align: print string then padding
                            while (*str && (ptr - buffer) < 250) {
                                *ptr++ = *str++;
                                count++;
                            }
                            for (int i = 0; i < padding && (ptr - buffer) < 250; i++) {
                                *ptr++ = ' ';
                                count++;
                            }
                        } else {
                            // Right align: print padding then string
                            for (int i = 0; i < padding && (ptr - buffer) < 250; i++) {
                                *ptr++ = ' ';
                                count++;
                            }
                            while (*str && (ptr - buffer) < 250) {
                                *ptr++ = *str++;
                                count++;
                            }
                        }
                    } else {
                        // No padding needed
                        while (*str && (ptr - buffer) < 250) {
                            *ptr++ = *str++;
                            count++;
                        }
                    }
                    fmt++;
                    continue;
                }
                case 'c': {
                    temp[0] = (char)va_arg(args, int);
                    len = 1;
                    break;
                }
                case '%': {
                    temp[0] = '%';
                    len = 1;
                    break;
                }
                default:
                    *ptr++ = '%';
                    *ptr++ = *fmt;
                    count += 2;
                    fmt++;
                    continue;
            }
            
            // Apply width and alignment for numeric types
            if (width > len) {
                int padding = width - len;
                if (left_align) {
                    // Left align: print number then padding
                    for (int i = 0; i < len && (ptr - buffer) < 250; i++) {
                        *ptr++ = temp[i];
                        count++;
                    }
                    for (int i = 0; i < padding && (ptr - buffer) < 250; i++) {
                        *ptr++ = ' ';
                        count++;
                    }
                } else {
                    // Right align: print padding then number
                    for (int i = 0; i < padding && (ptr - buffer) < 250; i++) {
                        *ptr++ = ' ';
                        count++;
                    }
                    for (int i = 0; i < len && (ptr - buffer) < 250; i++) {
                        *ptr++ = temp[i];
                        count++;
                    }
                }
            } else {
                // No padding needed
                for (int i = 0; i < len && (ptr - buffer) < 250; i++) {
                    *ptr++ = temp[i];
                    count++;
                }
            }
        } else {
            *ptr++ = *fmt;
            count++;
        }
        fmt++;
    }
    
    *ptr = '\0';
    va_end(args);
    
    module_log(buffer);
    return count;
}

// Add strlen support for string length calculation
size_t strlen(const char* str)
{
    const char* s = str;
    while (*s) {
        s++;
    }
    return (size_t)(s - str);
}

/* =============================================================================
 * Heap Management
 * ===========================================================================*/

// Heap management for malloc/free
extern char _end;  // Linker-provided symbol for end of BSS
static char *heap_ptr = NULL;

void *_sbrk(ptrdiff_t incr)
{
    if (heap_ptr == NULL) {
        heap_ptr = &_end;
    }
    
    char *prev_heap = heap_ptr;
    heap_ptr += incr;
    
    return (void *)prev_heap;
}

/* =============================================================================
 * File I/O Syscalls (kept for compatibility, not used by our printf)
 * ===========================================================================*/

int _write(int fd, const void *buf, size_t count)
{
    if (buf == NULL || count == 0) {
        return 0;
    }

    // Only handle stdout/stderr
    if (fd == 1 || fd == 2) {
        // Use module logging API (safe for modules)
        // Create null-terminated string from buffer
        char log_buffer[256];
        size_t to_copy = (count < sizeof(log_buffer) - 1) ? count : sizeof(log_buffer) - 1;
        
        for (size_t i = 0; i < to_copy; i++) {
            log_buffer[i] = ((const char*)buf)[i];
        }
        log_buffer[to_copy] = '\0';
        
        module_log(log_buffer);
        return (int)count;
    }

    // Unsupported FD
    errno = EBADF;
    return -1;
}

int _close(int fd)
{
    (void)fd;
    return 0;
}

int _fstat(int fd, struct stat *st)
{
    if (st == NULL) {
        errno = EFAULT;
        return -1;
    }
    
    if (fd >= 0 && fd <= 2) {
        st->st_mode = S_IFCHR;
        st->st_blksize = 0;
        st->st_size = 0;
        return 0;
    }
    
    errno = EBADF;
    return -1;
}

int _isatty(int fd)
{
    return (fd == 1 || fd == 2) ? 1 : 0;
}

off_t _lseek(int fd, off_t offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    errno = ESPIPE;
    return (off_t)-1;
}

int _read(int fd, void *buf, size_t count)
{
    (void)fd; (void)buf; (void)count;
    return 0;
}

void _exit(int status)
{
    (void)status;
    // Module can't exit - just loop forever
    while (1);
}

int _kill(int pid, int sig)
{
    (void)pid; (void)sig;
    errno = EINVAL;
    return -1;
}

int _getpid(void)
{
    return 1;
}
