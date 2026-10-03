/*
 * lib/kformat.cpp - freestanding printf-style formatter (no floating point, no allocation).
 * Supports flags "-+ #0", width/precision (incl. *), length hh/h/l/ll/j/z/t and
 * conversions d i u x X o b c s p %.
 */
#include "kformat.h"
#include <stdint.h>
#include <string.h>

static int utoa_rev(char *buf, uint64_t v, unsigned base, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if ((v >> 32) == 0) { /* 32-bit fast path (hardware divide) */
        uint32_t w = (uint32_t)v;
        do {
            buf[n++] = digits[w % base];
            w /= base;
        } while (w);
    } else {
        do {
            buf[n++] = digits[v % base];
            v /= base;
        } while (v);
    }
    return n; /* digits are reversed */
}

int kformat(kformat_emit_fn emit, void *ctx, const char *fmt, va_list ap)
{
    int count = 0;
#define OUT(ch)            \
    do {                   \
        emit((ch), ctx);   \
        count++;           \
    } while (0)

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            OUT(*fmt);
            continue;
        }
        fmt++;
        bool left = false, plus = false, space = false, alt = false, zero = false;
        for (;; fmt++) {
            if (*fmt == '-') left = true;
            else if (*fmt == '+') plus = true;
            else if (*fmt == ' ') space = true;
            else if (*fmt == '#') alt = true;
            else if (*fmt == '0') zero = true;
            else break;
        }
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = true;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9')
                width = width * 10 + (*fmt++ - '0');
        }
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                if (prec < 0)
                    prec = -1;
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9')
                    prec = prec * 10 + (*fmt++ - '0');
            }
        }
        int len = 0; /* 0 int, 1 long, 2 long long, -1 short, -2 char */
        for (;;) {
            if (*fmt == 'l') { len = (len == 1) ? 2 : 1; fmt++; }
            else if (*fmt == 'h') { len = (len == -1) ? -2 : -1; fmt++; }
            else if (*fmt == 'j' || *fmt == 'q') { len = 2; fmt++; }
            else if (*fmt == 'z' || *fmt == 't') { len = 0; fmt++; } /* 32-bit */
            else break;
        }
        char conv = *fmt;
        if (!conv)
            break;

        char buf[24];
        const char *str = buf;
        int n = 0;
        bool is_num = false, reversed = false;
        const char *prefix = "";
        uint64_t v = 0;

        switch (conv) {
        case 'c':
            buf[0] = (char)va_arg(ap, int);
            n = 1;
            break;
        case 's':
            str = va_arg(ap, const char *);
            if (!str)
                str = "(null)";
            n = (int)strnlen(str, prec >= 0 ? (size_t)prec : 0x7FFFFFFFu);
            break;
        case '%':
            OUT('%');
            continue;
        case 'd':
        case 'i': {
            int64_t sv;
            if (len == 2) sv = va_arg(ap, long long);
            else if (len == 1) sv = va_arg(ap, long);
            else sv = va_arg(ap, int);
            if (len == -1) sv = (short)sv;
            else if (len == -2) sv = (signed char)sv;
            if (sv < 0) {
                v = (uint64_t)(-(sv + 1)) + 1u;
                prefix = "-";
            } else {
                v = (uint64_t)sv;
                prefix = plus ? "+" : space ? " " : "";
            }
            n = utoa_rev(buf, v, 10, false);
            is_num = reversed = true;
            break;
        }
        case 'p':
            v = (uintptr_t)va_arg(ap, void *);
            n = utoa_rev(buf, v, 16, false);
            prefix = "0x";
            if (prec < 0)
                prec = 8;
            is_num = reversed = true;
            break;
        case 'u':
        case 'x':
        case 'X':
        case 'o':
        case 'b': {
            if (len == 2) v = va_arg(ap, unsigned long long);
            else if (len == 1) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            if (len == -1) v = (unsigned short)v;
            else if (len == -2) v = (unsigned char)v;
            unsigned base = conv == 'o' ? 8 : conv == 'b' ? 2 : conv == 'u' ? 10 : 16;
            n = utoa_rev(buf, v, base, conv == 'X');
            if (alt && v)
                prefix = conv == 'o' ? "0" : conv == 'X' ? "0X" : conv == 'x' ? "0x" : "";
            is_num = reversed = true;
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
            (void)va_arg(ap, long long); /* consume the double without touching the FPU */
            OUT('%');
            OUT(conv);
            continue;
        default:
            OUT('%');
            OUT(conv);
            continue;
        }

        int zeros = 0;
        if (is_num && prec >= 0) {
            zero = false;
            if (prec == 0 && v == 0)
                n = 0;
            if (prec > n)
                zeros = prec - n;
        }
        int plen = (int)strlen(prefix);
        int pad = width - (plen + zeros + n);
        if (pad < 0)
            pad = 0;
        if (!left && !(zero && is_num))
            for (; pad > 0; pad--)
                OUT(' ');
        for (const char *p = prefix; *p; p++)
            OUT(*p);
        if (!left && zero && is_num)
            for (; pad > 0; pad--)
                OUT('0');
        for (; zeros > 0; zeros--)
            OUT('0');
        if (reversed)
            for (int i = n - 1; i >= 0; i--)
                OUT(str[i]);
        else
            for (int i = 0; i < n; i++)
                OUT(str[i]);
        for (; pad > 0; pad--)
            OUT(' ');
    }
#undef OUT
    return count;
}

struct buf_ctx {
    char *buf;
    size_t size;
    size_t pos;
};

static void buf_emit(char c, void *vctx)
{
    struct buf_ctx *b = (struct buf_ctx *)vctx;
    if (b->pos + 1 < b->size)
        b->buf[b->pos] = c;
    b->pos++;
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct buf_ctx b = { buf, size, 0 };
    int n = kformat(buf_emit, &b, fmt, ap);
    if (size)
        buf[b.pos < size ? b.pos : size - 1] = '\0';
    return n;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

