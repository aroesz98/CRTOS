/*
 * kernel/os/vmem.cpp - emulated memory: more memory for a program than the SDRAM has, on the card.
 *
 * Without an MMU a program's addresses are the memory's: a page cannot be moved out and back
 * to another place. Emulated memory therefore lies where there is no memory at all
 * (CRTOS_VMEM_BASE, behind the MPU's background region): every access of the program to it
 * raises a MemManage fault, and the fault handler carries the load or store out
 * (arch/emulate.cpp) on the page's copy in the page cache here. A page that is not in the
 * cache is fetched first: the fault continues the thread in the kernel, like a system call
 * (SC_PAGEIN), which reads the page from the swap file - blocking, preemptible - and returns
 * to the same instruction, which then finds it. The cache frees a frame by the clock
 * algorithm, writing it back to the swap file first when it was changed; a page never
 * written comes back zeroed.
 *
 * The swap file (CONFIG_VMEM_FILE, CONFIG_VMEM_SIZE) is created in one piece at the first use
 * (fat_swap_create), so its pages are read and written as card blocks (fat_raw_io) at the
 * page's own offset: region addresses and file pages correspond one to one. The page cache
 * (CONFIG_VMEM_CACHE, less if the SDRAM has not that much) is taken from the SDRAM while some
 * process has a region, in up to CACHE_CHUNKS pieces: frames need not be next to each other,
 * and by the time a program runs out of memory the SDRAM left is in fragments.
 *
 * The page cache and the swap file are guarded by a mutex (thread context). The fault handler
 * uses neither: it only reads a region's page map and marks a frame used or changed, and the
 * thread code publishes each change of the map in one store under irq_lock, so the handler of
 * another thread never sees half of one. System calls get the program's data in emulated
 * memory through vmem_copy_in/out (copy_from_user & co, read and write).
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include <crtos/syscall.h>
#include <string.h>

#define PAGE          EMU_PAGE
#define PAGE_SHIFT    12
#define PAGE_BLOCKS   (PAGE / 512u)
#define NONE          0xFFFFu
#define CACHE_MIN     (64u << 10)   /* the least page cache worth having */
#define CHUNK_MIN     (16u << 10)   /* the smallest piece of it taken */
#define CACHE_CHUNKS  16

struct vmem_region {
    struct vmem_region *next;   /* of the same process */
    struct proc *proc;
    uintptr_t base;
    uint32_t size;
    uint32_t first;             /* its first page in the swap file (base = CRTOS_VMEM_BASE + first pages) */
    uint16_t *map;              /* frame of each page, NONE */
    uint32_t *stored;           /* bit per page: its data is in the swap file */
};

struct frame {
    struct vmem_region *r;      /* NULL: free */
    uint32_t page;
    uint8_t *data;              /* its PAGE bytes in one of the cache's pieces */
    volatile uint8_t dirty;     /* set by the fault handler */
    volatile uint8_t ref;
};

static struct mutex s_lock = MUTEX_INIT(s_lock);

static struct {
    uint8_t *chunk[CACHE_CHUNKS];   /* the page cache's pieces */
    uint32_t nchunks;
    struct frame *frames;       /* NULL: no page cache (no region anywhere) */
    uint32_t nframes, hand;
    uint32_t lba;               /* first card block of the swap file */
    uint32_t pages;             /* pages of the swap file */
    uint32_t *used;             /* bit per swap page: given to a region */
    uint32_t used_pages, nregions;
    uint64_t emulated;
    uint32_t misses, pageins, pageouts, zerofills;
} V;

static inline bool bit(const uint32_t *b, uint32_t i)
{
    return (b[i >> 5] >> (i & 31u)) & 1u;
}

static inline void bit_put(uint32_t *b, uint32_t i, bool v)
{
    if (v)
        b[i >> 5] |= 1u << (i & 31u);
    else
        b[i >> 5] &= ~(1u << (i & 31u));
}

static inline uint8_t *frame_data(uint32_t f)
{
    return V.frames[f].data;
}

static inline uint32_t page_lba(const struct vmem_region *r, uint32_t pg)
{
    return V.lba + (r->first + pg) * PAGE_BLOCKS;
}

/* The region of @p that holds all of [a, a + len) */
static struct vmem_region *region_of(struct proc *p, uintptr_t a, size_t len)
{
    for (struct vmem_region *r = p ? p->vmem : nullptr; r; r = r->next)
        if (a - r->base < r->size && len <= r->size - (a - r->base))
            return r;
    return nullptr;
}

bool vmem_contains(struct proc *p, const void *addr, size_t len)
{
    return p && p->vmem && len && region_of(p, (uintptr_t)addr, len);
}

/* ---- the fault handler's side ----------------------------------------------------------------- */

static int page_fast(void *ctx, uintptr_t a, bool write, uint8_t **p)
{
    struct vmem_region *r = region_of((struct proc *)ctx, a, 1);
    if (!r)
        return EMU_BAD;
    uint32_t off = (uint32_t)(a - r->base);
    uint16_t f = r->map[off >> PAGE_SHIFT];
    if (f == NONE)
        return EMU_MISS;
    struct frame *fr = &V.frames[f];
    fr->ref = 1;
    if (write)
        fr->dirty = 1;
    *p = frame_data(f) + (off & (PAGE - 1u));
    return EMU_DONE;
}

/* A MemManage fault of a user thread (fault.cpp). Handled: *ret = PSP and EXC_RETURN to go on
 * with - after the instruction, or in the kernel to fetch the page. Not handled (not emulated
 * memory of the process, or an instruction that is not emulated): a real fault. */
bool vmem_fault(task_t *t, uint32_t *frame, uint32_t *regs, uint32_t exc_return, uintptr_t addr, uint64_t *ret)
{
    struct proc *p = t->proc;
    if (!region_of(p, addr, 1))
        return false;
    struct emu e;
    memset(&e, 0, sizeof(e));
    e.frame = frame;
    e.regs = regs;
    e.fpframe = !(exc_return & 0x10u);
    e.page = page_fast;
    e.ctx = p;
    int r = emulate_access(&e);
    if (r == EMU_DONE) {
        V.emulated++;
        *ret = ((uint64_t)exc_return << 32) | (uint32_t)(uintptr_t)frame;
        return true;
    }
    if (r == EMU_MISS) {
        V.misses++;
        uint32_t k = syscall_enter_fault(frame, exc_return, SC_PAGEIN, (uint32_t)e.miss);
        *ret = ((uint64_t)0xFFFFFFFDu << 32) | k;
        return true;
    }
    return false;
}

/* ---- the page cache (thread context, s_lock held) ----------------------------------------------- */

/* A free frame: a free one, else the one the clock hand finds unused longest (written back to
 * the swap file first if it was changed) */
static int frame_get(uint32_t *out)
{
    for (uint32_t n = 0; n <= 2 * V.nframes; n++) {
        uint32_t f = V.hand;
        V.hand = V.hand + 1 == V.nframes ? 0 : V.hand + 1;
        struct frame *fr = &V.frames[f];
        if (!fr->r) {
            *out = f;
            return 0;
        }
        if (fr->ref) {
            fr->ref = 0;
            continue;
        }
        struct vmem_region *r = fr->r;
        uint32_t key = irq_lock();
        r->map[fr->page] = NONE; /* from now on the program's accesses fetch it again */
        irq_unlock(key);
        if (fr->dirty) {
            int e = fat_raw_io(page_lba(r, fr->page), frame_data(f), PAGE_BLOCKS, true);
            if (e) {
                key = irq_lock();
                r->map[fr->page] = (uint16_t)f;
                irq_unlock(key);
                return e;
            }
            bit_put(r->stored, fr->page, true);
            V.pageouts++;
        }
        fr->r = nullptr;
        fr->dirty = 0;
        *out = f;
        return 0;
    }
    return -ENOMEM;
}

/* Page @pg of @r into the cache */
static int page_in(struct vmem_region *r, uint32_t pg)
{
    if (r->map[pg] != NONE)
        return 0;
    uint32_t f;
    int e = frame_get(&f);
    if (e)
        return e;
    if (bit(r->stored, pg)) {
        e = fat_raw_io(page_lba(r, pg), frame_data(f), PAGE_BLOCKS, false);
        if (e)
            return e;
        V.pageins++;
    } else {
        memset(frame_data(f), 0, PAGE);
        V.zerofills++;
    }
    struct frame *fr = &V.frames[f];
    fr->r = r;
    fr->page = pg;
    fr->dirty = 0;
    fr->ref = 1;
    uint32_t key = irq_lock();
    r->map[pg] = (uint16_t)f;
    irq_unlock(key);
    return 0;
}

/* SC_PAGEIN: the faulting thread fetches its page in the kernel, then its instruction runs again */
void vmem_pagein_call(uintptr_t addr)
{
    task_t *t = g_current;
    struct proc *p = t->proc;
    if (mutex_lock(&s_lock, WAIT_FOREVER))
        return; /* killed */
    struct vmem_region *r = region_of(p, addr, 1);
    int e = r ? page_in(r, (uint32_t)((addr - r->base) >> PAGE_SHIFT)) : 0;
    mutex_unlock(&s_lock);
    if (e) { /* its data cannot be had: the program cannot go on */
        printk("*** vmem: page %08lx of process %d '%s' not read from the swap file (error %d)\n",
               (unsigned long)addr, p ? (int)p->pid : -1, p ? p->name : "?", e);
        if (p)
            proc_kill(p, -EFAULT);
        t->flags |= TF_KILLED;
    }
}

/* Copy between the kernel and a program's emulated memory, page by page through the cache */
static int copy(void *kbuf, uintptr_t ua, size_t len, bool out)
{
    struct proc *p = g_current->proc;
    uint8_t *k = (uint8_t *)kbuf;
    if (mutex_lock(&s_lock, WAIT_FOREVER))
        return -EINTR;
    struct vmem_region *r = region_of(p, ua, len);
    int e = r ? 0 : -EFAULT;
    while (!e && len) {
        uint32_t off = (uint32_t)(ua - r->base);
        uint32_t pg = off >> PAGE_SHIFT, in = off & (PAGE - 1u);
        uint32_t n = len < PAGE - in ? (uint32_t)len : PAGE - in;
        e = page_in(r, pg);
        if (e)
            break;
        uint16_t f = r->map[pg];
        if (out) {
            memcpy(frame_data(f) + in, k, n);
            V.frames[f].dirty = 1;
        } else {
            memcpy(k, frame_data(f) + in, n);
        }
        V.frames[f].ref = 1;
        k += n;
        ua += n;
        len -= n;
    }
    mutex_unlock(&s_lock);
    return e;
}

int vmem_copy_in(void *dst, const void *src, size_t len)
{
    return copy(dst, (uintptr_t)src, len, false);
}

int vmem_copy_out(void *dst, const void *src, size_t len)
{
    return copy((void *)src, (uintptr_t)dst, len, true);
}

/* As strncpy_from_user(): the length, -ENAMETOOLONG, -EFAULT. Regions are whole pages, so a
 * piece up to the end of a page is either all in the program's region or not at all. */
int vmem_strncpy_in(char *dst, const char *src, size_t size)
{
    uintptr_t a = (uintptr_t)src;
    size_t i = 0;
    while (i < size) {
        size_t n = PAGE - ((a + i) & (PAGE - 1u));
        if (n > size - i)
            n = size - i;
        if (!vmem_contains(g_current->proc, (const void *)(a + i), n))
            break;
        int e = copy(dst + i, a + i, n, false);
        if (e)
            return e;
        for (size_t k = 0; k < n; k++)
            if (!dst[i + k])
                return (int)(i + k);
        i += n;
    }
    if (size)
        dst[size - 1] = 0;
    return i == size ? -ENAMETOOLONG : -EFAULT;
}

/* ---- regions ---------------------------------------------------------------------------------- */

/* The swap file and the page cache, at the first region */
static int setup(void)
{
    if (V.frames)
        return 0;
    uint32_t lba;
    int e = fat_swap_create(CONFIG_VMEM_FILE, CONFIG_VMEM_SIZE, &lba);
    if (e) {
        printk("E: vmem: no swap file %s of %u MB on the card (error %d)\n", CONFIG_VMEM_FILE,
               (unsigned)(CONFIG_VMEM_SIZE >> 20), e);
        return e;
    }
    V.lba = lba;
    V.pages = CONFIG_VMEM_SIZE / PAGE;
    V.used = (uint32_t *)kzalloc(V.pages / 8u, KM_LARGE);
    /* The largest pieces there are, until the cache has its size: a piece of what is missing,
     * halved while the SDRAM has no such block */
    uint32_t size = 0, piece = CONFIG_VMEM_CACHE, sizes[CACHE_CHUNKS];
    V.nchunks = 0;
    while (size < CONFIG_VMEM_CACHE && V.nchunks < CACHE_CHUNKS && piece >= CHUNK_MIN) {
        if (piece > CONFIG_VMEM_CACHE - size)
            piece = CONFIG_VMEM_CACHE - size;
        uint8_t *c = (uint8_t *)kmalloc_aligned(piece, 32, KM_LARGE);
        if (!c) {
            piece /= 2u;
            continue;
        }
        sizes[V.nchunks] = piece;
        V.chunk[V.nchunks++] = c;
        size += piece;
    }
    V.nframes = size >= CACHE_MIN ? size / PAGE : 0;
    V.frames = V.nframes ? (struct frame *)kzalloc(V.nframes * sizeof(struct frame), KM_LARGE) : nullptr;
    if (!V.used || !V.frames) {
        kfree(V.used);
        kfree(V.frames);
        for (uint32_t i = 0; i < V.nchunks; i++)
            kfree(V.chunk[i]);
        V.nchunks = 0;
        V.nframes = 0;
        V.used = nullptr;
        V.frames = nullptr;
        return -ENOMEM;
    }
    for (uint32_t i = 0, f = 0; i < V.nchunks; i++)
        for (uint32_t o = 0; o < sizes[i]; o += PAGE)
            V.frames[f++].data = V.chunk[i] + o;
    V.hand = 0;
    V.used_pages = 0;
    printk("vmem: swap file %s (%u MB at block %lu), page cache %u KB in %u piece(s)\n", CONFIG_VMEM_FILE,
           (unsigned)(CONFIG_VMEM_SIZE >> 20), (unsigned long)lba, (unsigned)(size >> 10), (unsigned)V.nchunks);
    return 0;
}

static void teardown(void)
{
    for (uint32_t i = 0; i < V.nchunks; i++)
        kfree(V.chunk[i]);
    kfree(V.frames);
    kfree(V.used);
    V.nchunks = 0;
    V.frames = nullptr;
    V.used = nullptr;
    V.nframes = 0;
}

static int64_t region_map(struct proc *p, uint32_t size)
{
    if (!size || size > CONFIG_VMEM_SIZE)
        return -ENOMEM;
    size = (size + PAGE - 1u) & ~(PAGE - 1u);
    int e = setup();
    if (e)
        return e;
    uint32_t n = size / PAGE, first = 0, run = 0;
    for (uint32_t i = 0; i < V.pages && run < n; i++) {
        if (bit(V.used, i)) {
            run = 0;
            first = i + 1;
        } else {
            run++;
        }
    }
    struct vmem_region *r = nullptr;
    if (run == n)
        r = (struct vmem_region *)kzalloc(sizeof(*r), KM_ANY);
    if (r) {
        r->map = (uint16_t *)kmalloc(n * sizeof(uint16_t), KM_LARGE);
        r->stored = (uint32_t *)kzalloc((n + 31u) / 32u * 4u, KM_LARGE);
    }
    if (!r || !r->map || !r->stored) {
        if (r) {
            kfree(r->map);
            kfree(r->stored);
            kfree(r);
        }
        if (!V.nregions)
            teardown();
        return -ENOMEM;
    }
    memset(r->map, 0xFF, n * sizeof(uint16_t));
    r->proc = p;
    r->first = first;
    r->size = size;
    r->base = CRTOS_VMEM_BASE + first * PAGE;
    for (uint32_t i = 0; i < n; i++)
        bit_put(V.used, first + i, true);
    V.used_pages += n;
    V.nregions++;
    uint32_t key = irq_lock();
    r->next = p->vmem;
    p->vmem = r;
    irq_unlock(key);
    return (int64_t)r->base;
}

/* Region @r goes, with its pages (not written back); the cache with the last region */
static void region_free(struct vmem_region *r)
{
    struct proc *p = r->proc;
    uint32_t key = irq_lock();
    for (struct vmem_region **pp = &p->vmem; *pp; pp = &(*pp)->next) {
        if (*pp == r) {
            *pp = r->next;
            break;
        }
    }
    irq_unlock(key);
    for (uint32_t f = 0; f < V.nframes; f++) {
        if (V.frames[f].r == r) {
            V.frames[f].r = nullptr;
            V.frames[f].dirty = 0;
        }
    }
    uint32_t n = r->size / PAGE;
    for (uint32_t i = 0; i < n; i++)
        bit_put(V.used, r->first + i, false);
    V.used_pages -= n;
    kfree(r->map);
    kfree(r->stored);
    kfree(r);
    if (!--V.nregions)
        teardown();
}

void vmem_proc_release(struct proc *p)
{
    if (!p->vmem)
        return;
    mutex_lock(&s_lock, WAIT_FOREVER);
    while (p->vmem)
        region_free(p->vmem);
    mutex_unlock(&s_lock);
}

int64_t sys_vmem(uint32_t op, uint32_t arg)
{
    struct proc *p = g_current->proc;
    if (!p)
        return -EINVAL;
    int64_t r;
    if (mutex_lock(&s_lock, WAIT_FOREVER))
        return -EINTR;
    switch (op) {
    case VMEM_MAP:
        r = region_map(p, arg);
        break;
    case VMEM_UNMAP: {
        struct vmem_region *reg = p->vmem;
        while (reg && reg->base != arg)
            reg = reg->next;
        if (reg)
            region_free(reg);
        r = reg ? 0 : -EINVAL;
        break;
    }
    case VMEM_INFO: {
        struct crtos_vmeminfo vi;
        memset(&vi, 0, sizeof(vi));
        vi.size = CONFIG_VMEM_SIZE;
        vi.free = CONFIG_VMEM_SIZE - V.used_pages * PAGE;
        for (struct vmem_region *reg = p->vmem; reg; reg = reg->next)
            vi.mine += reg->size;
        vi.cache = V.frames ? V.nframes * PAGE : CONFIG_VMEM_CACHE;
        vi.emulated = V.emulated;
        vi.pageins = V.pageins;
        vi.pageouts = V.pageouts;
        vi.zerofills = V.zerofills;
        mutex_unlock(&s_lock);
        return copy_to_user((void *)arg, &vi, sizeof(vi));
    }
    default:
        r = -EINVAL;
        break;
    }
    mutex_unlock(&s_lock);
    return r;
}

void vmem_show(int (*out)(const char *fmt, ...))
{
    out("swap file %s: %u MB, %u KB given to %u region(s)\n", CONFIG_VMEM_FILE, (unsigned)(CONFIG_VMEM_SIZE >> 20),
        (unsigned)(V.used_pages * (PAGE / 1024u)), (unsigned)V.nregions);
    if (V.frames) {
        uint32_t used = 0, dirty = 0;
        for (uint32_t f = 0; f < V.nframes; f++) {
            used += V.frames[f].r != nullptr;
            dirty += V.frames[f].r && V.frames[f].dirty;
        }
        out("page cache: %u KB in %u piece(s), %u of %u pages used, %u changed; swap file at block %lu\n",
            (unsigned)(V.nframes * (PAGE / 1024u)), (unsigned)V.nchunks, (unsigned)used, (unsigned)V.nframes,
            (unsigned)dirty, (unsigned long)V.lba);
    } else {
        out("page cache: none (no region)\n");
    }
    out("accesses emulated %llu, pages missing %lu, read %lu, written %lu, zeroed %lu\n",
        (unsigned long long)V.emulated, (unsigned long)V.misses, (unsigned long)V.pageins,
        (unsigned long)V.pageouts, (unsigned long)V.zerofills);
}
