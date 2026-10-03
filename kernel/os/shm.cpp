/*
 * kernel/os/shm.cpp - shared memory.
 *
 * A shared memory object is a run of MPU subregions (eighths of a power-of-two region, the
 * others disabled) inside a block aligned to that region - placed like a process arena, so a
 * large object fits into the free memory between others - that processes map into one of
 * their windows (MPU regions 9-11). An object that cannot be one such run (larger than 16 MB,
 * or no place for it) is a contiguous block covered by up to three runs of different regions,
 * and a mapping takes a window for each: a program can have one object of nearly all the free
 * memory instead of several smaller ones on region boundaries. Without an MMU the block has
 * the same address in every process, so pointers into it can be exchanged directly. Handles and mappings each hold a reference;
 * the memory is freed with the last one. Drivers can also wrap their own memory (frame
 * buffers) with shm_wrap(); such an object may not be mappable (MPU alignment), then it is
 * only usable through its handle (2D accelerator), and the driver revokes it when the
 * memory goes away.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include "fsl_device_registers.h"
#include <crtos/syscall.h>
#include <string.h>

struct shm {
    uint32_t refs;
    uint8_t *base;                              /* NULL once revoked */
    uint32_t size;
    uint32_t attr;                              /* MPU_ATTR_USER_RW or MPU_ATTR_USER_RW_NC */
    bool owned;                                 /* allocated here: kfree with the last reference */
    bool mappable;
};

#define SHM_MAX (32u * 1024u * 1024u)

static uint32_t pow2_ceil(uint32_t v)
{
    return v <= 1 ? 1 : 1u << (32 - __builtin_clz(v - 1));
}

/* The MPU regions covering [base, base + size): runs of subregions, each piece reaching as far
 * as a region can from where the last one ended (the largest region whose subregions @p is a
 * multiple of). The pieces into @piece (at most @max); the number needed, max + 1 if more. */
struct piece {
    uintptr_t base;
    uint32_t size;
};

static int shm_cover(uintptr_t base, uint32_t size, struct piece *piece, int max)
{
    uintptr_t p = base, end = base + size;
    int n = 0;
    while (p < end) {
        uintptr_t best = p;
        for (uint32_t l2 = 8; l2 < 32; l2++) {
            uintptr_t full = (uintptr_t)1 << l2, sub = full >> 3;
            if (p & (sub - 1))
                break; /* (larger regions have larger subregions) */
            uintptr_t block_end = (p & ~(full - 1)) + full;
            uintptr_t e = (end < block_end ? end : block_end) & ~(sub - 1);
            if (e > best)
                best = e;
        }
        if (best == p || n == max)
            return max + 1;
        piece[n].base = p;
        piece[n].size = (uint32_t)(best - p);
        n++;
        p = best;
    }
    return n;
}

void shm_get(struct shm *s)
{
    uint32_t key = irq_lock();
    s->refs++;
    irq_unlock(key);
}

void shm_put(struct shm *s)
{
    uint32_t key = irq_lock();
    bool last = --s->refs == 0;
    irq_unlock(key);
    if (!last)
        return;
    if (s->owned)
        kfree(s->base);
    kfree(s);
}

/* A block for an object of @want bytes from the pool @km, its size rounded up in @size. As
 * arena_alloc() (proc.cpp): the smallest region that can hold it first, then one twice as
 * large - subregions twice as coarse, but twice as many places for the run. Else a block of
 * several regions, not crossing a multiple of twice its power of two: in steps of 1/64 of that
 * power if three pieces cover the place found (they may not), else in steps of 1/32, which
 * three pieces always cover (one up to the next 1/4, one of 1/4 steps up to the last, the
 * rest). */
static uint8_t *alloc_block(uint32_t want, unsigned km, uint32_t *size)
{
    uint8_t *b = nullptr;
    uint32_t full = pow2_ceil(want);
    for (int k = 0; k < 2 && !b && full <= SHM_MAX; k++, full <<= 1) {
        uint32_t sub = full / 8u;
        *size = ALIGN_UP(want, sub);
        b = (uint8_t *)kmalloc_bounded(*size, sub, full, km);
    }
    for (uint32_t div = 64; !b && want > 4096 && div >= 32; div /= 2) {
        uint32_t step = pow2_ceil(want) / div;
        struct piece piece[SHM_WINDOWS];
        *size = ALIGN_UP(want, step);
        b = (uint8_t *)kmalloc_bounded(*size, step, 2u * pow2_ceil(want), km);
        if (b && shm_cover((uintptr_t)b, *size, piece, SHM_WINDOWS) > SHM_WINDOWS) {
            kfree(b);
            b = nullptr;
        }
    }
    return b;
}

/* New object of at least @size bytes, zeroed. A non-cacheable one that the 2 MB of
 * non-cacheable memory cannot hold besides the frame buffers (the screen copy of a remote
 * desktop at 800x480) comes from the cached memory: the windows that map it are non-cacheable
 * all the same, and its lines leave the cache after the zeroing - the kernel does not touch it
 * through its cached view again, and the 2D accelerator cleans its targets there anyway. */
struct shm *shm_alloc(uint32_t size, uint32_t flags)
{
    if (!size || size > SHM_MAX)
        return nullptr;
    if (size < 256)
        size = 256;
    struct shm *s = (struct shm *)kzalloc(sizeof(*s), KM_ANY);
    if (!s)
        return nullptr;
    uint32_t want = size;
    bool cached = !(flags & SHM_NOCACHE);
    s->base = alloc_block(want, cached ? KM_LARGE : KM_NOCACHE, &size);
    if (!s->base && !cached) {
        s->base = alloc_block(want, KM_LARGE, &size);
        cached = s->base != nullptr;
    }
    if (!s->base) {
        kfree(s);
        return nullptr;
    }
    memset(s->base, 0, size);
    if (cached && (flags & SHM_NOCACHE))
        SCB_CleanInvalidateDCache_by_Addr(s->base, (int32_t)size);
    s->size = size;
    s->attr = (flags & SHM_NOCACHE) ? MPU_ATTR_USER_RW_NC : MPU_ATTR_USER_RW;
    s->refs = 1;
    s->owned = true;
    s->mappable = true;
    return s;
}

/* Share memory owned by a driver. It can be mapped only if it satisfies the MPU size and
 * alignment rules; the driver calls shm_revoke() before the memory goes away. */
struct shm *shm_wrap(void *base, uint32_t size, bool nocache)
{
    uint32_t rbar, rasr;
    struct shm *s = (struct shm *)kzalloc(sizeof(*s), KM_ANY);
    if (!s)
        return nullptr;
    s->base = (uint8_t *)base;
    s->size = size;
    s->attr = nocache ? MPU_ATTR_USER_RW_NC : MPU_ATTR_USER_RW;
    s->mappable = !mpu_encode_region(MPU_REGION_SHM0, (uintptr_t)base, size, s->attr, &rbar, &rasr); /* (one window) */
    s->refs = 1;
    return s;
}

void shm_revoke(struct shm *s)
{
    uint32_t key = irq_lock();
    s->base = nullptr;
    s->size = 0;
    irq_unlock(key);
}

/* No handle or mapping refers to @s any more: only the reference of whoever made it is left
 * (a frame buffer's wrapper whose users have all gone) */
bool shm_unshared(struct shm *s)
{
    uint32_t key = irq_lock();
    bool only = s->refs == 1;
    irq_unlock(key);
    return only;
}

/* Memory of the shared memory handle @h of the calling process, with a reference (shm_put) */
struct shm *shm_lookup(int h, uint8_t **base, uint32_t *size, bool *cached)
{
    uint8_t type = H_SHM;
    struct shm *s = (struct shm *)handle_ref(g_current->proc, h, &type, nullptr);
    if (!s)
        return nullptr;
    uint32_t key = irq_lock();
    *base = s->base;
    *size = s->size;
    irq_unlock(key);
    if (cached)
        *cached = s->attr == MPU_ATTR_USER_RW;
    if (!*base) {
        shm_put(s);
        return nullptr;
    }
    return s;
}

/* Bytes from @addr to the end of the object mapped in a window of @p that holds it, 0 if none
 * (checks of user pointers: the object's own bounds, whatever regions cover it) */
size_t shm_window_span(struct proc *p, uintptr_t addr)
{
    size_t span = 0;
    uint32_t key = irq_lock();
    for (int i = 0; i < SHM_WINDOWS; i++) {
        struct shm *s = p->win[i].shm;
        if (!s || !s->base)
            continue;
        uintptr_t b = (uintptr_t)s->base;
        if (addr >= b && addr - b < s->size) {
            span = s->size - (addr - b);
            break;
        }
    }
    irq_unlock(key);
    return span;
}

/* A window for each piece of the object's cover (one for an ordinary object) */
static int shm_map(struct proc *p, struct shm *s, uintptr_t *addr)
{
    struct piece piece[SHM_WINDOWS];
    uint32_t rbar[SHM_WINDOWS], rasr[SHM_WINDOWS];
    uint32_t key = irq_lock();
    int free_slots = 0;
    for (int i = 0; i < SHM_WINDOWS; i++) {
        if (p->win[i].shm == s) { /* already mapped */
            irq_unlock(key);
            *addr = (uintptr_t)s->base;
            return 0;
        }
        if (!p->win[i].shm && !(p->win[i].rasr & 1u)) /* (not the fast code block) */
            free_slots++;
    }
    if (!s->mappable || !s->base) {
        irq_unlock(key);
        return -EINVAL;
    }
    int n = shm_cover((uintptr_t)s->base, s->size, piece, SHM_WINDOWS);
    if (n > SHM_WINDOWS) {
        irq_unlock(key);
        return -EINVAL;
    }
    if (n > free_slots) {
        irq_unlock(key);
        return -ENOSPC;
    }
    for (int k = 0, i = 0; k < n; k++, i++) {
        while (p->win[i].shm || (p->win[i].rasr & 1u))
            i++;
        if (mpu_encode_region(MPU_REGION_SHM0 + i, piece[k].base, piece[k].size, s->attr, &rbar[k], &rasr[k])) {
            irq_unlock(key);
            return -EINVAL;
        }
    }
    s->refs++; /* one reference for the mapping, however many windows */
    for (int k = 0, i = 0; k < n; k++, i++) {
        while (p->win[i].shm || (p->win[i].rasr & 1u))
            i++;
        p->win[i].shm = s;
        p->win[i].rbar = rbar[k];
        p->win[i].rasr = rasr[k];
    }
    irq_unlock(key);
    mpu_proc_changed(p);
    *addr = (uintptr_t)s->base;
    return 0;
}

static int shm_unmap(struct proc *p, uintptr_t addr)
{
    struct shm *found = nullptr;
    uint32_t key = irq_lock();
    for (int i = 0; i < SHM_WINDOWS; i++) {
        struct shm *s = p->win[i].shm;
        if (s && (s == found || (!found && (uintptr_t)s->base == addr))) {
            found = s;
            p->win[i].shm = nullptr;
            mpu_empty_region(MPU_REGION_SHM0 + i, &p->win[i].rbar, &p->win[i].rasr);
        }
    }
    irq_unlock(key);
    if (!found)
        return -EINVAL;
    mpu_proc_changed(p);
    shm_put(found);
    return 0;
}

void shm_unmap_all(struct proc *p)
{
    for (int i = 0; i < SHM_WINDOWS; i++)
        if (p->win[i].shm)
            shm_unmap(p, (uintptr_t)p->win[i].shm->base);
}

/* ---- system calls ---------------------------------------------------------------------------- */

int64_t sys_shm_create(uint32_t size, uint32_t flags)
{
    struct shm *s = shm_alloc(size, flags);
    if (!s)
        return -ENOMEM;
    int h = handle_install(g_current->proc, H_SHM, 0, s, 0);
    if (h < 0)
        shm_put(s);
    return h;
}

int64_t sys_shm_map(int h)
{
    struct proc *p = g_current->proc;
    uint8_t type = H_SHM;
    struct shm *s = (struct shm *)handle_ref(p, h, &type, nullptr);
    if (!s)
        return -EBADF;
    uintptr_t addr = 0;
    int r = shm_map(p, s, &addr);
    shm_put(s);
    return r ? r : (int64_t)addr;
}

int64_t sys_shm_unmap(uintptr_t addr)
{
    return shm_unmap(g_current->proc, addr);
}
