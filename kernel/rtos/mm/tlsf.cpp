/*
 * kernel/rtos/mm/tlsf.cpp - Two-Level Segregated Fit allocator.
 *
 * Free blocks are kept in size classes: the first level splits sizes by powers of two, the
 * second level divides each power of two into SL_COUNT linear ranges. Bitmaps of non-empty
 * classes make both malloc and free O(1). Every block has an 8-byte header (pointer to the
 * physically previous block + size), payloads are 8-byte aligned, and neighbouring free
 * blocks are always merged.
 */
#include "tlsf.h"
#include <stddef.h>
#include <string.h>

#define ALIGN_LOG2   3
#define ALIGN        8u
#define SL_LOG2      4
#define SL_COUNT     (1u << SL_LOG2)
#define FL_SHIFT     (SL_LOG2 + ALIGN_LOG2)
#define SMALL_BLOCK  (1u << FL_SHIFT)
#define F_FREE       1u
#define SIZE_MASK    (~(ALIGN - 1u))
#define MAX_REQUEST  0x7FF00000u

struct blk {
    struct blk *prev_phys;
    uint32_t size;          /* payload bytes | F_FREE */
    struct blk *next_free;  /* the two list links live in the payload of free blocks */
    struct blk *prev_free;
};

#define HDR          ((uint32_t)offsetof(struct blk, next_free))  /* 8 on 32-bit targets */
#define MIN_PAYLOAD  ((uint32_t)(2 * sizeof(struct blk *)))

struct tlsf {
    uint32_t fl_bitmap;
    uint32_t fl_count;
    uintptr_t pool_start;   /* first block */
    uintptr_t pool_end;     /* end of the sentinel block */
    size_t free_bytes;
    size_t used_bytes;
    uint32_t nused;
    uint32_t *sl_bitmap;    /* [fl_count] */
    struct blk **heads;     /* [fl_count][SL_COUNT] */
};

static inline uint32_t bsize(const struct blk *b) { return b->size & SIZE_MASK; }
static inline bool is_free(const struct blk *b) { return (b->size & F_FREE) != 0; }
static inline struct blk *next_phys(const struct blk *b)
{
    return (struct blk *)((uint8_t *)b + HDR + bsize(b));
}
static inline void *payload(const struct blk *b) { return (uint8_t *)b + HDR; }
static inline struct blk *from_payload(const void *p) { return (struct blk *)((uint8_t *)p - HDR); }
static inline uint32_t fls_u32(uint32_t v) { return 31u - (uint32_t)__builtin_clz(v); }

static inline void mapping_insert(uint32_t size, uint32_t *fl, uint32_t *sl)
{
    if (size < SMALL_BLOCK) {
        *fl = 0;
        *sl = size / (SMALL_BLOCK / SL_COUNT);
    } else {
        uint32_t f = fls_u32(size);
        *sl = (size >> (f - SL_LOG2)) ^ SL_COUNT;
        *fl = f - (FL_SHIFT - 1);
    }
}

/* Round up to the next class boundary so any block of the class is large enough */
static inline void mapping_search(uint32_t size, uint32_t *fl, uint32_t *sl)
{
    if (size >= SMALL_BLOCK)
        size += (1u << (fls_u32(size) - SL_LOG2)) - 1u;
    mapping_insert(size, fl, sl);
}

static struct blk *find_suitable(struct tlsf *t, uint32_t fl, uint32_t sl)
{
    if (fl >= t->fl_count)
        return nullptr;
    uint32_t sl_map = t->sl_bitmap[fl] & (~0u << sl);
    if (!sl_map) {
        uint32_t fl_map = (fl + 1 < 32) ? (t->fl_bitmap & (~0u << (fl + 1))) : 0;
        if (!fl_map)
            return nullptr;
        fl = (uint32_t)__builtin_ctz(fl_map);
        sl_map = t->sl_bitmap[fl];
    }
    sl = (uint32_t)__builtin_ctz(sl_map);
    return t->heads[fl * SL_COUNT + sl];
}

static void insert_free(struct tlsf *t, struct blk *b)
{
    uint32_t fl, sl, sz = bsize(b);
    mapping_insert(sz, &fl, &sl);
    struct blk **h = &t->heads[fl * SL_COUNT + sl];
    b->size = sz | F_FREE;
    b->prev_free = nullptr;
    b->next_free = *h;
    if (*h)
        (*h)->prev_free = b;
    *h = b;
    t->fl_bitmap |= 1u << fl;
    t->sl_bitmap[fl] |= 1u << sl;
    t->free_bytes += sz;
}

static void remove_free(struct tlsf *t, struct blk *b)
{
    uint32_t fl, sl, sz = bsize(b);
    mapping_insert(sz, &fl, &sl);
    struct blk **h = &t->heads[fl * SL_COUNT + sl];
    if (b->prev_free)
        b->prev_free->next_free = b->next_free;
    else
        *h = b->next_free;
    if (b->next_free)
        b->next_free->prev_free = b->prev_free;
    if (!*h) {
        t->sl_bitmap[fl] &= ~(1u << sl);
        if (!t->sl_bitmap[fl])
            t->fl_bitmap &= ~(1u << fl);
    }
    b->size = sz;
    t->free_bytes -= sz;
}

/* @b is allocated (not on a list): give back everything beyond @n bytes */
static void trim_used(struct tlsf *t, struct blk *b, uint32_t n)
{
    uint32_t sz = bsize(b);
    if (sz >= n + HDR + MIN_PAYLOAD) {
        struct blk *r = (struct blk *)((uint8_t *)b + HDR + n);
        r->size = sz - n - HDR;
        r->prev_phys = b;
        next_phys(r)->prev_phys = r;
        b->size = n;
        insert_free(t, r); /* its successor is in use: free blocks are never adjacent */
    }
}

static inline uint32_t adjust(size_t size)
{
    if (size > MAX_REQUEST)
        return 0;
    uint32_t n = ((uint32_t)size + ALIGN - 1u) & SIZE_MASK;
    return n < MIN_PAYLOAD ? MIN_PAYLOAD : n;
}

struct tlsf *tlsf_create(void *mem, size_t bytes)
{
    uintptr_t s = ((uintptr_t)mem + ALIGN - 1u) & ~(uintptr_t)(ALIGN - 1u);
    uintptr_t e = ((uintptr_t)mem + bytes) & ~(uintptr_t)(ALIGN - 1u);
    if (e <= s || e - s < 1024)
        return nullptr;
    uint32_t span = (uint32_t)(e - s);
    uint32_t fl_count = fls_u32(span) - (FL_SHIFT - 1) + 1;
    size_t ctl = sizeof(struct tlsf) + fl_count * sizeof(uint32_t) + fl_count * SL_COUNT * sizeof(struct blk *);
    ctl = (ctl + ALIGN - 1u) & SIZE_MASK;
    if (span < ctl + 2 * HDR + MIN_PAYLOAD)
        return nullptr;

    struct tlsf *t = (struct tlsf *)s;
    memset(t, 0, ctl);
    t->fl_count = fl_count;
    t->sl_bitmap = (uint32_t *)(t + 1);
    t->heads = (struct blk **)(t->sl_bitmap + fl_count);

    uintptr_t p = s + ctl;
    struct blk *first = (struct blk *)p;
    struct blk *sentinel = (struct blk *)(e - HDR);
    first->prev_phys = nullptr;
    first->size = (uint32_t)((uintptr_t)sentinel - p - HDR);
    sentinel->prev_phys = first;
    sentinel->size = 0; /* permanently allocated, stops merging */
    t->pool_start = p;
    t->pool_end = e;
    insert_free(t, first);
    return t;
}

void *tlsf_malloc(struct tlsf *t, size_t size)
{
    uint32_t n = adjust(size);
    if (!n)
        return nullptr;
    uint32_t fl, sl;
    mapping_search(n, &fl, &sl);
    struct blk *b = find_suitable(t, fl, sl);
    if (!b)
        return nullptr;
    remove_free(t, b);
    trim_used(t, b, n);
    t->used_bytes += bsize(b);
    t->nused++;
    return payload(b);
}

/* Where in free block @b an @align-aligned run of @n payload bytes can start, 0 if nowhere:
 * the gap in front of it must be empty or able to hold a free block of its own, and with a
 * @boundary (a power of two >= align; 0: none) the run must not cross a multiple of it. */
static uintptr_t aligned_spot(const struct blk *b, uint32_t n, uint32_t align, uint32_t boundary)
{
    uintptr_t pay = (uintptr_t)payload(b);
    uintptr_t end = pay + bsize(b);
    uintptr_t a = (pay + align - 1) & ~(uintptr_t)(align - 1);
    for (;;) {
        if (a != pay)
            while (a - pay < HDR + MIN_PAYLOAD)
                a += align;
        if (a + n > end || a + n < a)
            return 0;
        if (!boundary || !((a ^ (a + n - 1)) & ~(uintptr_t)(boundary - 1)))
            return a;
        a = (a + boundary - 1) & ~(uintptr_t)(boundary - 1); /* start at the next boundary */
    }
}

/* The highest such start in free block @b, 0 if none */
static uintptr_t aligned_spot_high(const struct blk *b, uint32_t n, uint32_t align, uint32_t boundary)
{
    uintptr_t pay = (uintptr_t)payload(b);
    uintptr_t end = pay + bsize(b);
    if (end - pay < n)
        return 0;
    uintptr_t a = (end - n) & ~(uintptr_t)(align - 1);
    while (a >= pay) {
        if (boundary && ((a ^ (a + n - 1)) & ~(uintptr_t)(boundary - 1))) {
            uintptr_t edge = (a + n - 1) & ~(uintptr_t)(boundary - 1); /* end the run there */
            if (edge < pay + n)
                return 0;
            a = (edge - n) & ~(uintptr_t)(align - 1);
            continue;
        }
        if (a == pay || a - pay >= HDR + MIN_PAYLOAD)
            return a;
        if (a < pay + align)
            return 0;
        a -= align;
    }
    return 0;
}

/* A free block that holds such a run. The quick search asks for n + align, which fails for
 * large alignments (process arenas: megabytes on a region boundary) although such a run
 * exists; those are rare, so the free lists are walked from the class of n up. In the block
 * the run goes to its lowest or its highest place, whichever leaves the larger piece free:
 * an arena put on the first region boundary of a large free block would split it in two,
 * and each part holds less of the next large arena or object (MPU alignment). */
static struct blk *find_aligned(struct tlsf *t, uint32_t n, uint32_t align, uint32_t boundary, uintptr_t *spot)
{
    uint32_t fl, sl;
    mapping_insert(n, &fl, &sl);
    for (uint32_t i = fl * SL_COUNT + sl; i < t->fl_count * SL_COUNT; i++) {
        for (struct blk *b = t->heads[i]; b; b = b->next_free) {
            uintptr_t a = aligned_spot(b, n, align, boundary);
            if (a) {
                uintptr_t pay = (uintptr_t)payload(b), end = pay + bsize(b);
                uintptr_t h = aligned_spot_high(b, n, align, boundary);
                uintptr_t lo_rest = a - pay > end - (a + n) ? a - pay : end - (a + n);
                uintptr_t hi_rest = h ? (h - pay > end - (h + n) ? h - pay : end - (h + n)) : 0;
                *spot = hi_rest > lo_rest ? h : a;
                return b;
            }
        }
    }
    return nullptr;
}

void *tlsf_memalign(struct tlsf *t, size_t align, size_t size)
{
    return tlsf_memalign_bounded(t, align, size, 0);
}

void *tlsf_memalign_bounded(struct tlsf *t, size_t align, size_t size, size_t boundary)
{
    if (align < ALIGN)
        align = ALIGN;
    if ((align & (align - 1)) || align > 0x10000000u)
        return nullptr;
    if (boundary && ((boundary & (boundary - 1)) || boundary < align || boundary < size))
        return nullptr;
    if (align == ALIGN && !boundary)
        return tlsf_malloc(t, size);
    uint32_t n = adjust(size);
    if (!n || n > MAX_REQUEST - align)
        return nullptr;
    struct blk *b = nullptr;
    uintptr_t a = 0;
    if (!boundary) {
        /* Worst case the aligned payload starts align + 8 bytes into the block (the gap in
         * front must be able to hold a free block of its own) */
        uint32_t req = n + (uint32_t)align + HDR + MIN_PAYLOAD;
        uint32_t fl, sl;
        mapping_search(req, &fl, &sl);
        b = find_suitable(t, fl, sl);
        if (b && !(a = aligned_spot(b, n, (uint32_t)align, 0)))
            b = nullptr;
    }
    if (!b)
        b = find_aligned(t, n, (uint32_t)align, (uint32_t)boundary, &a);
    if (!b)
        return nullptr;
    remove_free(t, b);

    uintptr_t pay = (uintptr_t)payload(b);
    if (a != pay) {
        uint32_t gap = (uint32_t)(a - pay);
        struct blk *nb = (struct blk *)(a - HDR);
        nb->size = bsize(b) - gap;
        nb->prev_phys = b;
        next_phys(nb)->prev_phys = nb;
        b->size = gap - HDR;
        insert_free(t, b);
        b = nb;
    }
    trim_used(t, b, n);
    t->used_bytes += bsize(b);
    t->nused++;
    return payload(b);
}

int tlsf_free(struct tlsf *t, void *ptr)
{
    if (!ptr)
        return 0;
    if ((uintptr_t)ptr & (ALIGN - 1u))
        return -1;
    struct blk *b = from_payload(ptr);
    if ((uintptr_t)b < t->pool_start || (uintptr_t)b >= t->pool_end - HDR)
        return -2;
    if (is_free(b))
        return -3; /* double free */
    struct blk *nx = next_phys(b);
    if ((uintptr_t)nx > t->pool_end - HDR || nx->prev_phys != b)
        return -4; /* header overwritten */

    uint32_t sz = bsize(b);
    t->used_bytes -= sz;
    t->nused--;
    struct blk *prev = b->prev_phys;
    if (prev && is_free(prev)) {
        remove_free(t, prev);
        prev->size = bsize(prev) + HDR + sz;
        b = prev;
        next_phys(b)->prev_phys = b;
    }
    nx = next_phys(b);
    if (is_free(nx)) {
        remove_free(t, nx);
        b->size = bsize(b) + HDR + bsize(nx);
        next_phys(b)->prev_phys = b;
    }
    insert_free(t, b);
    return 0;
}

size_t tlsf_block_size(const void *ptr)
{
    return ptr ? bsize(from_payload(ptr)) : 0;
}

int tlsf_owns(const struct tlsf *t, const void *ptr)
{
    uintptr_t p = (uintptr_t)ptr;
    return p >= t->pool_start + HDR && p < t->pool_end;
}

void tlsf_get_stats(const struct tlsf *t, struct tlsf_stats *st)
{
    st->total = t->pool_end - t->pool_start;
    st->free = t->free_bytes;
    st->used = t->used_bytes;
    st->nused = t->nused;
    st->largest = 0;
    if (t->fl_bitmap) {
        uint32_t fl = fls_u32(t->fl_bitmap);
        uint32_t sl = fls_u32(t->sl_bitmap[fl]);
        for (const struct blk *b = t->heads[fl * SL_COUNT + sl]; b; b = b->next_free)
            if (bsize(b) > st->largest)
                st->largest = bsize(b);
    }
}

int tlsf_check(const struct tlsf *t)
{
    const struct blk *b = (const struct blk *)t->pool_start, *prev = nullptr;
    size_t freeb = 0, usedb = 0;
    uint32_t nused = 0, nfree = 0;
    bool prev_free = false;
    for (;;) {
        if ((uintptr_t)b < t->pool_start || (uintptr_t)b > t->pool_end - HDR)
            return -1;
        if (b->prev_phys != prev)
            return -2;
        if ((uintptr_t)b == t->pool_end - HDR)
            break;
        uint32_t sz = bsize(b);
        if (is_free(b)) {
            if (prev_free)
                return -3;
            freeb += sz;
            nfree++;
            prev_free = true;
        } else {
            usedb += sz;
            nused++;
            prev_free = false;
        }
        prev = b;
        b = next_phys(b);
    }
    if (freeb != t->free_bytes)
        return -4;
    if (usedb != t->used_bytes)
        return -5;
    if (nused != t->nused)
        return -6;
    uint32_t listed = 0;
    for (uint32_t fl = 0; fl < t->fl_count; fl++) {
        for (uint32_t sl = 0; sl < SL_COUNT; sl++) {
            const struct blk *h = t->heads[fl * SL_COUNT + sl];
            bool bit = (t->sl_bitmap[fl] >> sl) & 1u;
            if (!h != !bit)
                return -7;
            for (const struct blk *f = h; f; f = f->next_free) {
                uint32_t mfl, msl;
                mapping_insert(bsize(f), &mfl, &msl);
                if (!is_free(f) || mfl != fl || msl != sl)
                    return -8;
                listed++;
            }
        }
        if (!t->sl_bitmap[fl] != !((t->fl_bitmap >> fl) & 1u))
            return -9;
    }
    return listed == nfree ? 0 : -10;
}
