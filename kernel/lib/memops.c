/*
 * memops.c - memcpy, memmove and memset for the Cortex-M7, used by the kernel and (through
 * libcrtos) by every program: newlib-nano's own copy and fill byte by byte (built for size).
 *
 * The destination is aligned to a word first; then 32 bytes go per round with word loads
 * and stores (the compiler makes LDM/STM or LDRD/STRD of them), the source may stay
 * unaligned: the M7 reads unaligned words from normal memory at little extra cost (never
 * use these on device memory with an unaligned source). The compiler must not turn the
 * loops into calls of the functions themselves (NO_LIBCALL, as glibc does), whichever build
 * compiles the file.
 */
#include <stddef.h>
#include <stdint.h>

#define NO_LIBCALL __attribute__((optimize("no-tree-loop-distribute-patterns")))

typedef uint32_t __attribute__((may_alias)) word_t;
typedef uint32_t __attribute__((aligned(1), may_alias)) uword_t; /* unaligned */

/* forward copy; also right for overlapping areas with dst below src (memmove) */
static inline NO_LIBCALL void copy_fwd(uint8_t *d, const uint8_t *s, size_t n)
{
    if (n >= 8) {
        while ((uintptr_t)d & 3) {
            *d++ = *s++;
            n--;
        }
        word_t *dw = (word_t *)d;
        if (!((uintptr_t)s & 3)) {
            const word_t *sw = (const word_t *)s;
            for (; n >= 32; n -= 32, sw += 8, dw += 8) {
                __builtin_prefetch(sw + 32); /* 128 bytes ahead: SDRAM lines come in meanwhile */
                uint32_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
                uint32_t f = sw[4], g = sw[5], h = sw[6], i = sw[7];
                dw[0] = a;
                dw[1] = b;
                dw[2] = c;
                dw[3] = e;
                dw[4] = f;
                dw[5] = g;
                dw[6] = h;
                dw[7] = i;
            }
            for (; n >= 4; n -= 4)
                *dw++ = *sw++;
            s = (const uint8_t *)sw;
        } else {
            const uword_t *sw = (const uword_t *)s;
            for (; n >= 16; n -= 16, sw += 4, dw += 4) {
                __builtin_prefetch(sw + 32);
                uint32_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
                dw[0] = a;
                dw[1] = b;
                dw[2] = c;
                dw[3] = e;
            }
            for (; n >= 4; n -= 4)
                *dw++ = *sw++;
            s = (const uint8_t *)sw;
        }
        d = (uint8_t *)dw;
    }
    while (n--)
        *d++ = *s++;
}

NO_LIBCALL void *memcpy(void *dst, const void *src, size_t n)
{
    copy_fwd(dst, src, n);
    return dst;
}

NO_LIBCALL void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d <= s || d >= s + n) {
        copy_fwd(d, s, n);
        return dst;
    }
    /* overlapping, dst above src: from the end down */
    d += n;
    s += n;
    if (n >= 8) {
        while ((uintptr_t)d & 3) {
            *--d = *--s;
            n--;
        }
        word_t *dw = (word_t *)d;
        const uword_t *sw = (const uword_t *)s;
        for (; n >= 16; n -= 16) {
            sw -= 4;
            dw -= 4;
            uint32_t a = sw[3], b = sw[2], c = sw[1], e = sw[0];
            dw[3] = a;
            dw[2] = b;
            dw[1] = c;
            dw[0] = e;
        }
        for (; n >= 4; n -= 4)
            *--dw = *--sw;
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }
    while (n--)
        *--d = *--s;
    return dst;
}

NO_LIBCALL void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = dst;
    if (n >= 8) {
        uint32_t v = (uint8_t)c * 0x01010101u;
        while ((uintptr_t)d & 3) {
            *d++ = (uint8_t)c;
            n--;
        }
        word_t *dw = (word_t *)d;
        for (; n >= 32; n -= 32, dw += 8) {
            dw[0] = v;
            dw[1] = v;
            dw[2] = v;
            dw[3] = v;
            dw[4] = v;
            dw[5] = v;
            dw[6] = v;
            dw[7] = v;
        }
        for (; n >= 4; n -= 4)
            *dw++ = v;
        d = (uint8_t *)dw;
    }
    while (n--)
        *d++ = (uint8_t)c;
    return dst;
}
