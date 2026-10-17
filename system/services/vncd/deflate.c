/*
 * deflate.c - a small, fast DEFLATE (RFC 1951) compressor for vncd's zlib encoding.
 *
 * The data are pixels of a screen rectangle, row after row. Matches (LZ77) start at whole
 * pixels and are whole pixels long; three places are tried: the pixel before (runs of one
 * colour), the pixel above (the row before: what a window or text repeats downwards) and the
 * last earlier place with the same 4 bytes (a hash) - the longest wins, up to 258 bytes and
 * 32 KB back, within the rectangle. One pass, no lazy matching, the fixed Huffman codes: a
 * screen of flat colours and repeated shapes shrinks to a few percent at a few cycles a byte;
 * a picture (a game) mostly stays literals at 9 bits a byte or less.
 *
 * The hash table keeps positions from earlier rectangles (16 bits: rectangles of up to 64 KB
 * are meant); a match from one is still checked byte by byte against the data before the
 * current position, so it is right whatever its origin. A candidate is first compared 4 bytes
 * at once, so most cost one load; matches of the pixel before pay off from 2 pixels on, the
 * others (longer distance codes) from 3. On the board (Cortex-M7) the cost is the data cache:
 * the table is small enough to stay in it. See deflate.h for the stream.
 */
#include <string.h>
#include "deflate.h"

#define MIN_MATCH 4         /* the pixel before: a 7-bit length and a 5-bit distance */
#define MIN_FAR 6           /* farther: a longer distance code, 3 pixels before it pays */
#define MAX_MATCH 258
#define MAX_DIST 32768u
#define HASH_DIST 8192u     /* hash candidates this near only: still in the data cache (a miss each
                             * took more than the rest of the pixel's work) */

static uint16_t s_lit_code[256];    /* literal byte -> its code, bit-reversed */
static uint8_t s_lit_bits[256];
static uint32_t s_len_code[MAX_MATCH + 1]; /* length -> code and extra bits, bit-reversed */
static uint8_t s_len_bits[MAX_MATCH + 1];
static uint8_t s_dist_rev[30];      /* distance code -> its 5 bits reversed */
static uint8_t s_dist_code[512];    /* distance - 1 -> code (0-255 direct, then by 128s) */
static const uint16_t s_dist_base[30] = { 1,    2,    3,    4,    5,    7,     9,     13,    17,  25,
                                          33,   49,   65,   97,   129,  193,   257,   385,   513, 769,
                                          1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577 };
static const uint8_t s_dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                          6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
static int s_ready;

static uint32_t reverse(uint32_t code, unsigned n)
{
    uint32_t r = 0;
    for (unsigned i = 0; i < n; i++)
        r |= ((code >> i) & 1u) << (n - 1 - i);
    return r;
}

/* The fixed Huffman code of a literal/length symbol (RFC 1951, 3.2.6), reversed */
static uint32_t fixed(unsigned sym, unsigned *n)
{
    if (sym < 144) {
        *n = 8;
        return reverse(0x30u + sym, 8);
    }
    if (sym < 256) {
        *n = 9;
        return reverse(0x190u + sym - 144u, 9);
    }
    if (sym < 280) {
        *n = 7;
        return reverse(sym - 256u, 7);
    }
    *n = 8;
    return reverse(0xC0u + sym - 280u, 8);
}

static void tables(void)
{
    static const uint16_t base[29] = { 3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
    static const uint8_t extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
    unsigned n;
    for (unsigned b = 0; b < 256; b++) {
        s_lit_code[b] = (uint16_t)fixed(b, &n);
        s_lit_bits[b] = (uint8_t)n;
    }
    for (unsigned k = 0; k < 29; k++) {
        uint32_t code = fixed(257u + k, &n);
        for (unsigned e = 0; e < (1u << extra[k]); e++) {
            unsigned len = base[k] + e;
            if (len > MAX_MATCH || (k == 27 && len == MAX_MATCH)) /* (258 has a code of its own) */
                break;
            s_len_code[len] = code | (e << n);
            s_len_bits[len] = (uint8_t)(n + extra[k]);
        }
    }
    for (unsigned c = 0; c < 30; c++) {
        s_dist_rev[c] = (uint8_t)reverse(c, 5);
        for (unsigned d = s_dist_base[c]; d < s_dist_base[c] + (1u << s_dist_extra[c]) && d <= MAX_DIST; d++) {
            unsigned i = d - 1u < 256u ? d - 1u : 256u + ((d - 1u) >> 7);
            s_dist_code[i] = (uint8_t)c;
        }
    }
    s_ready = 1;
}

static inline void put_bits(struct zdef *z, uint32_t v, unsigned n)
{
    z->acc |= (uint64_t)v << z->nbits;
    z->nbits += n;
    if (z->nbits >= 32) {
        uint32_t w = (uint32_t)z->acc;
        memcpy(z->out + z->len, &w, 4); /* (little endian: the first bits first) */
        z->len += 4;
        z->acc >>= 32;
        z->nbits -= 32;
    }
}

/* Bytes in common at @a and @b, up to @max */
static inline size_t common(const uint8_t *a, const uint8_t *b, size_t max)
{
    size_t n = 0;
    while (n + 4 <= max) {
        uint32_t x, y;
        memcpy(&x, a + n, 4);
        memcpy(&y, b + n, 4);
        if (x != y)
            return n + ((unsigned)__builtin_ctz(x ^ y) >> 3);
        n += 4;
    }
    while (n < max && a[n] == b[n])
        n++;
    return n;
}

size_t zdef_bound(size_t max_in)
{
    return max_in + max_in / 8u + 64u;
}

int zdef_init(struct zdef *z, uint8_t *out, size_t max_in)
{
    if (!s_ready)
        tables();
    memset(z->head, 0, sizeof(z->head));
    z->out = out;
    z->cap = zdef_bound(max_in);
    z->started = 0;
    return out ? 0 : -1;
}

void zdef_reset(struct zdef *z)
{
    z->started = 0;
}

size_t zdef_rect(struct zdef *z, const uint8_t *in, size_t len, unsigned unit, size_t row)
{
    z->len = 0;
    z->acc = 0;
    z->nbits = 0;
    if (!z->started) { /* zlib header: deflate, 32 KB window, fastest */
        z->out[z->len++] = 0x78;
        z->out[z->len++] = 0x01;
        z->started = 1;
    }
    put_bits(z, 2u, 3); /* not the last block, fixed codes */
    size_t i = 0, mask = ~(size_t)(unit - 1u); /* (unit: 1, 2 or 4) */
    while (i < len) {
        size_t left = len - i, best = 0, dist = 0;
        if (left >= MIN_MATCH) {
            size_t max = (left < MAX_MATCH ? left : MAX_MATCH) & mask;
            uint32_t v, w;
            memcpy(&v, in + i, 4);
            uint32_t h = (v * 2654435761u) >> (32 - ZDEF_HASH_BITS);
            size_t c = z->head[h];
            z->head[h] = (uint16_t)i;
            if (i >= unit && (memcpy(&w, in + i - unit, 4), w == v)) {
                best = common(in + i - unit, in + i, max) & mask;
                dist = unit;
            }
            if (best < max && row != unit && i >= row && row <= MAX_DIST && (memcpy(&w, in + i - row, 4), w == v)) {
                size_t l = common(in + i - row, in + i, max) & mask;
                if (l > best && l >= MIN_FAR) {
                    best = l;
                    dist = row;
                }
            }
            if (best < max && c < i && i - c <= HASH_DIST && !((i - c) & (unit - 1u)) && (memcpy(&w, in + c, 4), w == v)) {
                size_t l = common(in + c, in + i, max) & mask;
                if (l > best && l >= MIN_FAR) {
                    best = l;
                    dist = i - c;
                }
            }
        }
        if (best >= MIN_MATCH) {
            put_bits(z, s_len_code[best], s_len_bits[best]);
            unsigned dc = s_dist_code[dist - 1u < 256u ? dist - 1u : 256u + ((dist - 1u) >> 7)];
            put_bits(z, s_dist_rev[dc] | ((uint32_t)(dist - s_dist_base[dc]) << 5), 5u + s_dist_extra[dc]);
            i += best;
        } else if (unit == 2 && left >= 2) { /* a pixel: both bytes at once */
            uint8_t a = in[i], b = in[i + 1];
            put_bits(z, s_lit_code[a] | ((uint32_t)s_lit_code[b] << s_lit_bits[a]), s_lit_bits[a] + s_lit_bits[b]);
            i += 2;
        } else {
            for (unsigned k = 0; k < unit && i < len; k++, i++)
                put_bits(z, s_lit_code[in[i]], s_lit_bits[in[i]]);
        }
    }
    put_bits(z, 0, 7); /* end of block */
    put_bits(z, 0, 3); /* sync flush: an empty stored block, not the last */
    while (z->nbits) {
        z->out[z->len++] = (uint8_t)z->acc;
        z->acc >>= 8;
        z->nbits = z->nbits > 8 ? z->nbits - 8 : 0;
    }
    z->out[z->len++] = 0x00;
    z->out[z->len++] = 0x00;
    z->out[z->len++] = 0xFF;
    z->out[z->len++] = 0xFF;
    return z->len;
}
