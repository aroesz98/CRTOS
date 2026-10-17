/*
 * heap.c - libcrtosheap: a malloc for programs that need more memory than their arena and
 * would otherwise lose much of it to fragmentation (the compiler on the board: cc1, cc1plus).
 *
 * newlib's malloc takes memory only from the arena's heap (sbrk) and cannot use memory that
 * does not continue it. This one is a Two-Level Segregated Fit allocator (as the kernel's,
 * kernel/rtos/mm/tlsf.cpp) over several pools: the arena's heap, grown with sbrk as needed,
 * and, once that is full, shared memory objects the process creates for itself and maps into
 * its MPU windows (at most three, CRTOS_HEAP_WINDOWS; each the largest the system still has,
 * tried from 30 MB down - the kernel makes one of more than a region can hold out of several,
 * one window each, so the first is usually nearly all the free memory in one piece). When
 * that runs out too, the rest comes from emulated memory (crtos_vmem_map: the swap file on the
 * card, every access carried out by the kernel - slow, but compiling a large file then only
 * takes longer instead of failing), with free lists of its own, used only when memory has no
 * block. Neighbouring free blocks are merged at once and a request takes a block of the
 * smallest size class that fits, so GCC's pattern - the garbage collector's page groups of
 * 64 KB between vectors of a megabyte that grow by realloc - leaves far less memory in unusable
 * holes than newlib's allocator does.
 *
 * Linking with -lcrtosheap replaces the C library's malloc family: the reentrant entry points
 * newlib itself calls and the plain functions, grouped as newlib's objects define them, so
 * none of those is pulled from libc (mallinfo, malloc_stats and mallopt stay newlib's, on the
 * entry points here). The windows are the ones libgfx maps window surfaces into: a program
 * with windows sets CRTOS_HEAP_WINDOWS=0 (or less than 3), best as its own default - it
 * defines crtos_heap_windows (and crtos_heap_window_kb, crtos_heap_swap, crtos.h), which the
 * environment's settings still override. A window is at most CRTOS_HEAP_WINDOW_KB large and
 * leaves the system CRTOS_HEAP_RESERVE KB of its free SDRAM (default 1024: windows of the
 * desktop, network buffers, the RAM disk) and, until emulated memory is used, what its page
 * cache will take.
 * CRTOS_HEAP_SWAP limits emulated memory to that many MB (0: none). With CRTOS_HEAP_STATS set,
 * the program reports its heap (where its pools are, the emulated accesses) on stderr at exit
 * and when a request fails.
 */
#include <errno.h>
#include <malloc.h>
#include <reent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>

#define ALIGN_LOG2   3
#define ALIGN        8u
#define SL_LOG2      5                          /* 32 classes per power of two: ~3% slack */
#define SL_COUNT     (1u << SL_LOG2)
#define FL_SHIFT     (SL_LOG2 + ALIGN_LOG2)
#define SMALL_BLOCK  (1u << FL_SHIFT)
#define FL_COUNT     20                         /* blocks below 64 MB */
#define F_FREE       1u
#define SIZE_MASK    (~(ALIGN - 1u))
#define MAX_REQUEST  (32u << 20)                /* larger than any pool can be */
#define MAX_POOLS    8
#define GROW_STEP    (256u << 10)               /* the arena's heap grows at least this much */
#define WINDOWS      3                          /* MPU windows of a process (kernel: SHM_WINDOWS) */
#define WINDOW_MAX   (30u << 20)                /* more than the free SDRAM of a running system */
#define WINDOW_MIN   (256u << 10)
#define RESERVE      (1u << 20)                 /* SDRAM a window leaves the system */

void *_sbrk(ptrdiff_t incr);                    /* libcrtos (syscalls.c) */

struct blk {
    struct blk *prev_phys;
    uint32_t size;          /* payload bytes | F_FREE */
    struct blk *next_free;  /* the two list links live in the payload of free blocks */
    struct blk *prev_free;
};

#define HDR          8u                         /* prev_phys + size */
#define MIN_PAYLOAD  8u                         /* room for the list links */

enum pool_kind { POOL_ARENA, POOL_WINDOW, POOL_VMEM };

struct pool {
    uintptr_t start;        /* first block */
    uintptr_t end;          /* end of the sentinel block */
    uint8_t kind;
};

/* Free blocks by size class: one set for memory (the arena's heap, windows), one for emulated
 * memory, which a request uses only when memory has nothing */
struct lists {
    uint32_t fl_bitmap;
    uint32_t sl_bitmap[FL_COUNT];
    struct blk *heads[FL_COUNT][SL_COUNT];
};

static struct {
    struct lists fast, slow;
    struct pool pools[MAX_POOLS];
    int npools;
    uintptr_t brk_end;      /* end of the pool that ends at the program break, 0: none */
    int windows, windows_max;
    uint32_t window_max;    /* the largest window (CRTOS_HEAP_WINDOW_KB) */
    uint32_t reserve;
    uint32_t vmem_limit;    /* emulated memory to take at most (CRTOS_HEAP_SWAP), 0: none */
    uint32_t vmem_cache;    /* SDRAM the kernel's page cache takes when it is first used */
    bool ready, stats, no_windows, ram_full, vmem_tried;
    size_t total, window_bytes, vmem_bytes, free_bytes, used, peak;
    uint32_t nused, nfree;
} H;

static inline struct lists *lists_of(const void *b)
{
    return CRTOS_IN_VMEM(b) ? &H.slow : &H.fast;
}

static inline uint32_t bsize(const struct blk *b) { return b->size & SIZE_MASK; }
static inline bool is_free(const struct blk *b) { return (b->size & F_FREE) != 0; }
static inline struct blk *next_phys(const struct blk *b)
{
    return (struct blk *)((uint8_t *)b + HDR + bsize(b));
}
static inline void *payload(const struct blk *b) { return (uint8_t *)b + HDR; }
static inline struct blk *from_payload(const void *p) { return (struct blk *)((uint8_t *)p - HDR); }
static inline uint32_t fls_u32(uint32_t v) { return 31u - (uint32_t)__builtin_clz(v); }

/* ---- size classes and free lists ------------------------------------------------------------ */

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

/* the first class all of whose blocks are large enough */
static inline void mapping_search(uint32_t size, uint32_t *fl, uint32_t *sl)
{
    if (size >= SMALL_BLOCK)
        size += (1u << (fls_u32(size) - SL_LOG2)) - 1u;
    mapping_insert(size, fl, sl);
}

static void insert_free(struct blk *b)
{
    struct lists *L = lists_of(b);
    uint32_t fl, sl, sz = bsize(b);
    mapping_insert(sz, &fl, &sl);
    struct blk **h = &L->heads[fl][sl];
    b->size = sz | F_FREE;
    b->prev_free = NULL;
    b->next_free = *h;
    if (*h)
        (*h)->prev_free = b;
    *h = b;
    L->fl_bitmap |= 1u << fl;
    L->sl_bitmap[fl] |= 1u << sl;
    H.free_bytes += sz;
    H.nfree++;
}

static void remove_free(struct blk *b)
{
    struct lists *L = lists_of(b);
    uint32_t fl, sl, sz = bsize(b);
    mapping_insert(sz, &fl, &sl);
    struct blk **h = &L->heads[fl][sl];
    if (b->prev_free)
        b->prev_free->next_free = b->next_free;
    else
        *h = b->next_free;
    if (b->next_free)
        b->next_free->prev_free = b->prev_free;
    if (!*h) {
        L->sl_bitmap[fl] &= ~(1u << sl);
        if (!L->sl_bitmap[fl])
            L->fl_bitmap &= ~(1u << fl);
    }
    b->size = sz;
    H.free_bytes -= sz;
    H.nfree--;
}

/* A free block of at least @n bytes: from the first class that surely fits, else (a request
 * close to the largest free block) one of @n's own class that happens to be large enough */
static struct blk *find_free(struct lists *L, uint32_t n)
{
    uint32_t fl, sl;
    mapping_search(n, &fl, &sl);
    if (fl < FL_COUNT) {
        uint32_t sl_map = L->sl_bitmap[fl] & (~0u << sl);
        if (!sl_map) {
            uint32_t fl_map = L->fl_bitmap & (~0u << (fl + 1));
            if (fl_map) {
                fl = (uint32_t)__builtin_ctz(fl_map);
                sl_map = L->sl_bitmap[fl];
            }
        }
        if (sl_map)
            return L->heads[fl][__builtin_ctz(sl_map)];
    }
    mapping_insert(n, &fl, &sl);
    for (struct blk *b = L->heads[fl][sl]; b; b = b->next_free)
        if (bsize(b) >= n)
            return b;
    return NULL;
}

/* Block @b, not on a list, joins its free neighbours and goes on a list */
static void release(struct blk *b)
{
    struct blk *prev = b->prev_phys;
    if (prev && is_free(prev)) {
        remove_free(prev);
        prev->size = bsize(prev) + HDR + bsize(b);
        b = prev;
        next_phys(b)->prev_phys = b;
    }
    struct blk *nx = next_phys(b);
    if (is_free(nx)) {
        remove_free(nx);
        b->size = bsize(b) + HDR + bsize(nx);
        next_phys(b)->prev_phys = b;
    }
    insert_free(b);
}

/* Allocated block @b keeps @n bytes, the rest becomes free */
static void split(struct blk *b, uint32_t n)
{
    uint32_t sz = bsize(b);
    if (sz < n + HDR + MIN_PAYLOAD)
        return;
    struct blk *t = (struct blk *)((uint8_t *)b + HDR + n);
    t->size = sz - n - HDR;
    t->prev_phys = b;
    next_phys(t)->prev_phys = t;
    b->size = n;
    release(t);
}

/* ---- pools ---------------------------------------------------------------------------------- */

/* [mem, mem + bytes) becomes a pool: one free block and a sentinel that never merges */
static bool add_pool(void *mem, size_t bytes, enum pool_kind kind)
{
    uintptr_t s = ((uintptr_t)mem + ALIGN - 1u) & ~(uintptr_t)(ALIGN - 1u);
    uintptr_t e = ((uintptr_t)mem + bytes) & ~(uintptr_t)(ALIGN - 1u);
    if (H.npools == MAX_POOLS || e <= s || e - s < 2 * HDR + 256)
        return false;
    struct blk *first = (struct blk *)s;
    struct blk *sentinel = (struct blk *)(e - HDR);
    first->prev_phys = NULL;
    first->size = (uint32_t)(e - s - 2 * HDR);
    sentinel->prev_phys = first;
    sentinel->size = 0;
    H.pools[H.npools++] = (struct pool){ s, e, (uint8_t)kind };
    H.total += e - s;
    insert_free(first);
    return true;
}

static struct pool *pool_of(uintptr_t end)
{
    for (int i = 0; i < H.npools; i++)
        if (H.pools[i].end == end)
            return &H.pools[i];
    return NULL;
}

/* At least @inc more bytes from the arena's heap: they continue the pool at the break (its
 * sentinel becomes a free block), or form a new pool */
static bool grow_brk_by(uint32_t inc)
{
    uint32_t step = inc < GROW_STEP ? GROW_STEP : (inc + 4095u) & ~4095u;
    void *p = _sbrk((ptrdiff_t)step);
    if (p == (void *)-1) {
        step = (inc + ALIGN - 1u) & SIZE_MASK;
        p = _sbrk((ptrdiff_t)step);
        if (p == (void *)-1)
            return false;
    }
    struct pool *pl = H.brk_end ? pool_of(H.brk_end) : NULL;
    if (pl && (uintptr_t)p == H.brk_end) {
        struct blk *s = (struct blk *)(H.brk_end - HDR);
        struct blk *ns = (struct blk *)(H.brk_end + step - HDR);
        ns->prev_phys = s;
        ns->size = 0;
        s->size = step - HDR;
        pl->end += step;
        H.brk_end += step;
        H.total += step;
        release(s);
        return true;
    }
    if (!add_pool(p, step, POOL_ARENA))
        return false;
    H.brk_end = H.pools[H.npools - 1].end;
    return true;
}

/* Room for a block of @n bytes from the arena's heap */
static bool grow_brk(uint32_t n)
{
    if (!H.brk_end) {
        /* the break on an 8-byte boundary once: then every step keeps it there */
        uintptr_t cur = (uintptr_t)_sbrk(0);
        if (cur != (uintptr_t)-1 && (cur & (ALIGN - 1u)))
            _sbrk((ptrdiff_t)(ALIGN - (cur & (ALIGN - 1u))));
        return grow_brk_by(n + 2 * HDR);
    }
    /* a free last block grows by what continues it */
    struct blk *last = ((struct blk *)(H.brk_end - HDR))->prev_phys;
    uint32_t have = last && is_free(last) ? bsize(last) + HDR : 0;
    return grow_brk_by(n + HDR > have ? n + HDR - have : ALIGN);
}

/* A shared memory object of the process's own for a block of @n bytes: the largest the system
 * has. The sizes go down in the kernel's finest steps for them (1/64 of the power of two), so
 * none is rounded up past the free memory. */
static bool add_window(uint32_t n)
{
    if (H.windows >= H.windows_max || H.no_windows)
        return false;
    uint32_t need = n + 2 * HDR;
    uint32_t size = H.window_max;
    struct crtos_sysinfo si;
    if (crtos_sys_info(&si) == 0) {
        /* (and the page cache emulated memory will need, until it has it) */
        uint32_t keep = H.reserve + (H.vmem_tried ? 0 : H.vmem_cache);
        uint32_t avail = si.mem_free > keep ? si.mem_free - keep : 0;
        if (avail < size)
            size = avail;
    }
    while (size >= need && size >= WINDOW_MIN) {
        uint32_t step = (1u << (32 - __builtin_clz(size - 1))) / 64u;
        size -= size % step;
        int h = crtos_shm_create(size, 0);
        if (h < 0) {
            size -= step;
            continue;
        }
        void *m = crtos_shm_map(h);
        int err = errno;
        close(h); /* the mapping keeps the object */
        if (!m) {
            if (err != ENOSPC) {
                H.no_windows = true;
                return false;
            }
            /* its pieces need more windows than the process has free: a smaller one fewer */
            size = size / 2u > need ? size / 2u : size - step;
            continue;
        }
        if (!add_pool(m, size, POOL_WINDOW)) {
            crtos_shm_unmap(m);
            return false;
        }
        H.windows++;
        H.window_bytes += size;
        return true;
    }
    return false;
}

/* Emulated memory (crtos_vmem_map), once memory has run out: as much as the swap file has
 * free, up to CRTOS_HEAP_SWAP MB */
static bool add_vmem(uint32_t n)
{
    if (H.vmem_tried)
        return false;
    H.vmem_tried = true;
    struct crtos_vmeminfo vi;
    if (!H.vmem_limit || crtos_vmem_info(&vi) || !vi.size)
        return false;
    uint32_t size = vi.free < H.vmem_limit ? vi.free : H.vmem_limit;
    for (; size >= n + 2 * HDR && size >= WINDOW_MIN; size /= 2u) {
        void *m = crtos_vmem_map(size);
        if (!m)
            continue;
        if (!add_pool(m, size, POOL_VMEM)) {
            crtos_vmem_unmap(m);
            return false;
        }
        H.vmem_bytes = size;
        return true;
    }
    return false;
}

/* ---- the allocator ---------------------------------------------------------------------------- */

static uint32_t largest_free(const struct lists *L)
{
    uint32_t largest = 0;
    if (L->fl_bitmap) {
        uint32_t fl = fls_u32(L->fl_bitmap);
        for (struct blk *b = L->heads[fl][fls_u32(L->sl_bitmap[fl])]; b; b = b->next_free)
            if (bsize(b) > largest)
                largest = bsize(b);
    }
    return largest;
}

static void report(FILE *f, const char *why)
{
    static const char *const kind[] = { "arena", "window", "emulated" };
    fprintf(f, "heap%s: peak %u KB, now %u KB in %u blocks; taken %u KB (arena %u KB, %d windows %u KB, "
            "emulated %u KB), largest free %u KB (emulated %u KB)\n", why, (unsigned)(H.peak >> 10),
            (unsigned)(H.used >> 10), (unsigned)H.nused, (unsigned)(H.total >> 10),
            (unsigned)((H.total - H.window_bytes - H.vmem_bytes) >> 10), H.windows, (unsigned)(H.window_bytes >> 10),
            (unsigned)(H.vmem_bytes >> 10), (unsigned)(largest_free(&H.fast) >> 10),
            (unsigned)(largest_free(&H.slow) >> 10));
    for (int i = 0; i < H.npools; i++)
        fprintf(f, "  pool %08x..%08x %6u KB %s\n", (unsigned)H.pools[i].start, (unsigned)H.pools[i].end,
                (unsigned)((H.pools[i].end - H.pools[i].start) >> 10), kind[H.pools[i].kind]);
    struct crtos_vmeminfo vi;
    if (H.vmem_bytes && !crtos_vmem_info(&vi))
        fprintf(f, "  emulated memory: %llu accesses, %u pages read, %u written, %u zeroed; page cache %u KB\n",
                (unsigned long long)vi.emulated, (unsigned)vi.pageins, (unsigned)vi.pageouts, (unsigned)vi.zerofills,
                (unsigned)(vi.cache >> 10));
}

static void report_at_exit(void)
{
    report(stderr, "");
}

/* the program's own defaults (crtos.h), where it defines them */
#pragma weak crtos_heap_windows
#pragma weak crtos_heap_window_kb
#pragma weak crtos_heap_swap

static void heap_init(void)
{
    H.ready = true;
    int windows = &crtos_heap_windows && crtos_heap_windows >= 0 ? crtos_heap_windows : WINDOWS;
    const char *w = getenv("CRTOS_HEAP_WINDOWS");
    if (w && *w >= '0' && *w <= '9')
        windows = *w - '0';
    H.windows_max = windows < WINDOWS ? windows : WINDOWS;
    uint32_t window_kb = &crtos_heap_window_kb && crtos_heap_window_kb > 0 ? (uint32_t)crtos_heap_window_kb : 0;
    const char *k = getenv("CRTOS_HEAP_WINDOW_KB");
    if (k && *k >= '0' && *k <= '9')
        window_kb = (uint32_t)strtoul(k, NULL, 10);
    H.window_max = window_kb && window_kb < (WINDOW_MAX >> 10) ? window_kb << 10 : WINDOW_MAX;
    H.reserve = RESERVE;
    const char *r = getenv("CRTOS_HEAP_RESERVE");
    if (r && *r >= '0' && *r <= '9')
        H.reserve = (uint32_t)strtoul(r, NULL, 10) << 10;
    struct crtos_vmeminfo vi;
    if (!crtos_vmem_info(&vi) && vi.size) {
        H.vmem_limit = vi.size;
        H.vmem_cache = vi.cache;
    }
    long swap = &crtos_heap_swap && crtos_heap_swap >= 0 ? crtos_heap_swap : -1;
    const char *v = getenv("CRTOS_HEAP_SWAP");
    if (v && *v >= '0' && *v <= '9')
        swap = (long)strtoul(v, NULL, 10);
    if (swap >= 0) {
        uint32_t mb = (uint32_t)swap;
        if (!mb)
            H.vmem_cache = 0;
        if (mb < 4096 && (mb << 20) < H.vmem_limit)
            H.vmem_limit = mb << 20;
    }
    H.stats = getenv("CRTOS_HEAP_STATS") != NULL;
    if (H.stats)
        atexit(report_at_exit);
}

static inline uint32_t adjust(size_t size)
{
    if (size > MAX_REQUEST)
        return 0;
    uint32_t n = ((uint32_t)size + ALIGN - 1u) & SIZE_MASK;
    return n < MIN_PAYLOAD ? MIN_PAYLOAD : n;
}

/* A free block of at least @n bytes, off its list; more memory if there is none (the arena's
 * heap first, then a window; once neither gives any, emulated memory). Called locked. */
static struct blk *take(uint32_t n)
{
    struct blk *b;
    for (int tries = 0; !(b = find_free(&H.fast, n)) && !H.ram_full; tries++) {
        if (tries == 4)
            break;
        if (!grow_brk(n) && !add_window(n))
            H.ram_full = H.vmem_limit != 0; /* (from now on emulated memory, not a system call each time) */
    }
    if (!b && !(b = find_free(&H.slow, n)) && add_vmem(n))
        b = find_free(&H.slow, n);
    if (!b)
        return NULL;
    remove_free(b);
    return b;
}

static void *finish(struct blk *b, uint32_t n)
{
    split(b, n);
    H.used += bsize(b);
    H.nused++;
    if (H.used > H.peak)
        H.peak = H.used;
    return payload(b);
}

/* A block freed twice, a pointer the heap never gave out or a block overrun: the program stops.
 * abort() leaves no fault report, so the message says where free or realloc was called from and
 * the arena, for tools/appsym.py (build/apps/X/X.debug.app ARENA CALLER). */
static void corrupt(const void *p, const void *caller)
{
    fprintf(stderr, "heap: bad pointer %p freed or reallocated, called from %p (arena %p)\n", p,
            caller, __crtos_startup ? __crtos_startup->arena : NULL);
    abort();
}

static inline void check(const struct blk *b, const void *p, const void *caller)
{
    if (((uintptr_t)p & (ALIGN - 1u)) || is_free(b) || next_phys(b)->prev_phys != b)
        corrupt(p, caller);
}

void *_malloc_r(struct _reent *r, size_t size)
{
    if (!H.ready)
        heap_init();
    uint32_t n = adjust(size);
    __malloc_lock(r);
    struct blk *b = n ? take(n) : NULL;
    void *p = b ? finish(b, n) : NULL;
    if (!p && H.stats) {
        fprintf(stderr, "heap: no memory for %u bytes\n", (unsigned)size);
        report(stderr, " at the failure");
    }
    __malloc_unlock(r);
    if (!p)
        errno = ENOMEM;
    return p;
}

void _free_r(struct _reent *r, void *p)
{
    if (!p)
        return;
    struct blk *b = from_payload(p);
    __malloc_lock(r);
    check(b, p, __builtin_return_address(0));
    H.used -= bsize(b);
    H.nused--;
    release(b);
    __malloc_unlock(r);
}

void *_realloc_r(struct _reent *r, void *p, size_t size)
{
    if (!p)
        return _malloc_r(r, size);
    uint32_t n = adjust(size);
    if (!n) {
        errno = ENOMEM;
        return NULL;
    }
    struct blk *b = from_payload(p);
    __malloc_lock(r);
    check(b, p, __builtin_return_address(0));
    uint32_t sz = bsize(b);
    struct blk *nx = next_phys(b);
    /* the last block of the arena's heap grows in place (vectors that keep growing) */
    if (n > sz && !is_free(nx) && !bsize(nx) && (uintptr_t)nx == H.brk_end - HDR)
        grow_brk_by(n - sz);
    nx = next_phys(b);
    if (n > sz && is_free(nx) && sz + HDR + bsize(nx) >= n) {
        remove_free(nx);
        b->size = sz + HDR + bsize(nx);
        next_phys(b)->prev_phys = b;
    }
    if (bsize(b) >= n) {
        H.used -= sz;
        H.nused--;
        finish(b, n);
        __malloc_unlock(r);
        return p;
    }
    __malloc_unlock(r);
    void *q = _malloc_r(r, size);
    if (q) {
        memcpy(q, p, sz);
        _free_r(r, p);
    }
    return q;
}

void *_calloc_r(struct _reent *r, size_t count, size_t size)
{
    size_t bytes;
    if (__builtin_mul_overflow(count, size, &bytes)) {
        errno = ENOMEM;
        return NULL;
    }
    void *p = _malloc_r(r, bytes);
    if (p)
        memset(p, 0, bytes);
    return p;
}

void *_memalign_r(struct _reent *r, size_t align, size_t size)
{
    if (align <= ALIGN)
        return _malloc_r(r, size);
    uint32_t n = adjust(size);
    if ((align & (align - 1)) || align > MAX_REQUEST || !n || n > MAX_REQUEST - align - HDR - MIN_PAYLOAD) {
        errno = (align & (align - 1)) ? EINVAL : ENOMEM;
        return NULL;
    }
    if (!H.ready)
        heap_init();
    __malloc_lock(r);
    /* the aligned payload starts less than align + HDR + MIN_PAYLOAD into the block: the gap
     * in front of it becomes a free block of its own */
    struct blk *b = take(n + (uint32_t)align + HDR + MIN_PAYLOAD);
    void *p = NULL;
    if (b) {
        uintptr_t pay = (uintptr_t)payload(b);
        uintptr_t a = (pay + align - 1) & ~(uintptr_t)(align - 1);
        if (a != pay) {
            while (a - pay < HDR + MIN_PAYLOAD)
                a += align;
            struct blk *nb = (struct blk *)(a - HDR);
            nb->size = bsize(b) - (uint32_t)(a - pay);
            nb->prev_phys = b;
            next_phys(nb)->prev_phys = nb;
            b->size = (uint32_t)(a - pay) - HDR;
            insert_free(b); /* its predecessor is in use: b was a free block */
            b = nb;
        }
        p = finish(b, n);
    }
    __malloc_unlock(r);
    if (!p)
        errno = ENOMEM;
    return p;
}

void *_valloc_r(struct _reent *r, size_t size)
{
    return _memalign_r(r, 4096, size);
}

void *_pvalloc_r(struct _reent *r, size_t size)
{
    return _memalign_r(r, 4096, (size + 4095u) & ~(size_t)4095u);
}

size_t _malloc_usable_size_r(struct _reent *r, void *p)
{
    (void)r;
    return p ? bsize(from_payload(p)) : 0;
}

int _malloc_trim_r(struct _reent *r, size_t pad)
{
    (void)r;
    (void)pad;
    return 0; /* the memory stays with the program until it ends */
}

struct mallinfo _mallinfo_r(struct _reent *r)
{
    struct mallinfo mi;
    memset(&mi, 0, sizeof(mi));
    __malloc_lock(r);
    mi.arena = H.total;
    mi.ordblks = H.nfree;
    mi.hblks = (size_t)H.windows;
    mi.hblkhd = H.window_bytes;
    mi.usmblks = H.peak;
    mi.uordblks = H.used;
    mi.fordblks = H.free_bytes;
    __malloc_unlock(r);
    return mi;
}

void _malloc_stats_r(struct _reent *r)
{
    __malloc_lock(r);
    report(stderr, "");
    __malloc_unlock(r);
}

int _mallopt_r(struct _reent *r, int param, int value)
{
    (void)r;
    (void)param;
    (void)value;
    return 0;
}

/* Consistency of the whole heap (heaptest): 0, or which rule is broken */
int __crtos_heap_check(void)
{
    size_t freeb = 0, usedb = 0;
    uint32_t nused = 0, nfree = 0, listed = 0;
    int err = 0;
    __malloc_lock(_REENT);
    for (int i = 0; i < H.npools && !err; i++) {
        const struct blk *b = (const struct blk *)H.pools[i].start, *prev = NULL;
        bool prev_free = false;
        for (;;) {
            if ((uintptr_t)b < H.pools[i].start || (uintptr_t)b > H.pools[i].end - HDR) {
                err = -1;
                break;
            }
            if (b->prev_phys != prev) {
                err = -2;
                break;
            }
            if ((uintptr_t)b == H.pools[i].end - HDR) {
                if (b->size)
                    err = -3; /* the sentinel */
                break;
            }
            if (is_free(b)) {
                if (prev_free) {
                    err = -4; /* two free neighbours */
                    break;
                }
                freeb += bsize(b);
                nfree++;
            } else {
                usedb += bsize(b);
                nused++;
            }
            prev_free = is_free(b);
            prev = b;
            b = next_phys(b);
        }
    }
    for (int k = 0; k < 2 && !err; k++) {
        const struct lists *L = k ? &H.slow : &H.fast;
        for (uint32_t fl = 0; fl < FL_COUNT && !err; fl++) {
            for (uint32_t sl = 0; sl < SL_COUNT && !err; sl++) {
                const struct blk *h = L->heads[fl][sl];
                if (!h != !((L->sl_bitmap[fl] >> sl) & 1u))
                    err = -5;
                for (const struct blk *f = h; f && !err; f = f->next_free) {
                    uint32_t mfl, msl;
                    mapping_insert(bsize(f), &mfl, &msl);
                    if (!is_free(f) || mfl != fl || msl != sl || (f->next_free && f->next_free->prev_free != f) ||
                        lists_of(f) != L)
                        err = -6;
                    listed++;
                }
            }
            if (!err && !L->sl_bitmap[fl] != !((L->fl_bitmap >> fl) & 1u))
                err = -7;
        }
    }
    if (!err && (freeb != H.free_bytes || nfree != H.nfree || listed != nfree))
        err = -8;
    if (!err && (usedb != H.used || nused != H.nused))
        err = -9;
    __malloc_unlock(_REENT);
    return err;
}

/* ---- the plain functions, as newlib's objects group them (malloc.o: malloc and free) ------- */

void *malloc(size_t size)
{
    return _malloc_r(_REENT, size);
}

void free(void *p)
{
    _free_r(_REENT, p);
}

void *calloc(size_t count, size_t size)
{
    return _calloc_r(_REENT, count, size);
}

void *realloc(void *p, size_t size)
{
    return _realloc_r(_REENT, p, size);
}

void *memalign(size_t align, size_t size)
{
    return _memalign_r(_REENT, align, size);
}

void *valloc(size_t size)
{
    return _valloc_r(_REENT, size);
}

void *pvalloc(size_t size)
{
    return _pvalloc_r(_REENT, size);
}

size_t malloc_usable_size(void *p)
{
    return _malloc_usable_size_r(_REENT, p);
}

int malloc_trim(size_t pad)
{
    return _malloc_trim_r(_REENT, pad);
}

void cfree(void *p)
{
    _free_r(_REENT, p);
}
