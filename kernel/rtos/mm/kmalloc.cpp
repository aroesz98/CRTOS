/*
 * kernel/rtos/mm/kmalloc.cpp - kernel heaps.
 *
 * Every RAM that is not used by the kernel image becomes a TLSF pool. Allocation flags
 * pick an ordered list of pools (fastest suitable first). All pool operations are O(1)
 * and done with kernel interrupts masked, so kmalloc/kfree are usable from interrupts.
 *
 * The end of ITCM from an 8 KB boundary is no pool but the fast code area: one program's
 * hottest code (app.cpp), a block at the top of ITCM that one MPU region describes - a power
 * of two aligned to its size, of which the block takes the top eighths (subregions); TLSF
 * could not align such a block within the little ITCM there is. The top is the ITCM the chip
 * has (platform_itcm_top: on the STM32H7S7 the option bytes set it), at most the linker's.
 */
#include "../kernel.h"
#include "platform.h"
#include "tlsf.h"
#include <string.h>

extern "C" {
extern uint8_t __end_noinit_SRAM_ITC[], __top_SRAM_ITC[];
extern uint8_t _pvHeapLimit[], _vStackBase[];
extern uint8_t __end_noinit_SRAM_OC[], __top_SRAM_OC[];
extern uint8_t __end_noinit_BOARD_SDRAM[], __top_BOARD_SDRAM[];
extern uint8_t __end_noinit_NCACHE_REGION[], __top_NCACHE_REGION[];
}

enum { P_DTCM, P_ITCM, P_OCRAM, P_SDRAM, P_NCACHE, P_COUNT };
#define P_END 0xFF

struct pool {
    const char *name;
    uintptr_t base, end;
    unsigned flags;
    struct tlsf *t;
};

static struct pool s_pool[P_COUNT];

#define FASTCODE_ALIGN 8192u
#define ITCM_HEAP_MIN 2048u             /* the kernel's ITCM heap before the fast code area */
static uintptr_t s_fc_base, s_fc_end;   /* the fast code area */
static uintptr_t s_fc_block;            /* the block handed out, 0: none */

static const uint8_t ord_any[] = { P_OCRAM, P_SDRAM, P_DTCM, P_END };
static const uint8_t ord_fast[] = { P_DTCM, P_ITCM, P_OCRAM, P_SDRAM, P_END };
static const uint8_t ord_fast_exec[] = { P_ITCM, P_OCRAM, P_SDRAM, P_END };
static const uint8_t ord_onchip_exec[] = { P_ITCM, P_OCRAM, P_END };
static const uint8_t ord_exec[] = { P_OCRAM, P_SDRAM, P_END };
static const uint8_t ord_large[] = { P_SDRAM, P_OCRAM, P_END };
static const uint8_t ord_nocache[] = { P_NCACHE, P_END };

static void pool_add(int id, const char *name, uintptr_t base, uintptr_t end, unsigned flags)
{
    struct pool *p = &s_pool[id];
    base = ALIGN_UP(base, 32u);
    end = ALIGN_DOWN(end, 32u);
    p->name = name;
    p->base = base;
    p->end = end;
    p->flags = flags;
    p->t = end > base ? tlsf_create((void *)base, end - base) : nullptr;
}

void mm_init(void)
{
    pool_add(P_DTCM, "dtcm", (uintptr_t)_pvHeapLimit, (uintptr_t)_vStackBase, KM_FAST);
    s_fc_base = ALIGN_UP((uintptr_t)__end_noinit_SRAM_ITC + ITCM_HEAP_MIN, FASTCODE_ALIGN);
    s_fc_end = (uintptr_t)__top_SRAM_ITC;
    if (platform_itcm_top() < s_fc_end)
        s_fc_end = platform_itcm_top();
    if (s_fc_base >= s_fc_end)
        s_fc_base = s_fc_end;
    /* a block can start only on an eighth of its region (kmalloc_fastcode): below the lowest
     * such start the ITCM stays the kernel's heap */
    uintptr_t lowest = s_fc_end;
    for (uintptr_t region = 2048u; region <= (1u << 20); region <<= 1) {
        uintptr_t sub = region / 8u, lo = ALIGN_UP(s_fc_base, sub);
        if (lo < ALIGN_DOWN(s_fc_end - 1u, region))
            lo = ALIGN_DOWN(s_fc_end - 1u, region);
        if (s_fc_end % sub == 0 && lo < lowest)
            lowest = lo;
    }
    s_fc_base = lowest;
    pool_add(P_ITCM, "itcm", (uintptr_t)__end_noinit_SRAM_ITC, s_fc_base, KM_FAST | KM_EXEC);
    pool_add(P_OCRAM, PLATFORM_POOL_ONCHIP, (uintptr_t)__end_noinit_SRAM_OC, (uintptr_t)__top_SRAM_OC, KM_EXEC | KM_DMA);
    pool_add(P_SDRAM, PLATFORM_POOL_EXT, (uintptr_t)__end_noinit_BOARD_SDRAM, (uintptr_t)__top_BOARD_SDRAM,
             KM_EXEC | KM_DMA | KM_LARGE);
    pool_add(P_NCACHE, "ncache", (uintptr_t)__end_noinit_NCACHE_REGION, (uintptr_t)__top_NCACHE_REGION,
             KM_NOCACHE | KM_DMA);
}

static const uint8_t *pool_order(unsigned flags)
{
    if (flags & KM_NOCACHE)
        return ord_nocache;
    if (flags & KM_FAST)
        return (flags & KM_EXEC) ? ((flags & KM_ONCHIP) ? ord_onchip_exec : ord_fast_exec) : ord_fast;
    if (flags & KM_LARGE)
        return ord_large;
    if (flags & (KM_EXEC | KM_DMA))
        return ord_exec;
    return ord_any;
}

void *kmalloc_bounded(size_t size, size_t align, size_t boundary, unsigned flags)
{
    void *p = nullptr;
    for (const uint8_t *o = pool_order(flags); *o != P_END && !p; o++) {
        struct pool *pl = &s_pool[*o];
        if (!pl->t)
            continue;
        uint32_t key = irq_lock();
        p = align > 8 || boundary ? tlsf_memalign_bounded(pl->t, align, size, boundary) : tlsf_malloc(pl->t, size);
        irq_unlock(key);
    }
    if (p && (flags & KM_ZERO))
        memset(p, 0, size);
    return p;
}

void *kmalloc_aligned(size_t size, size_t align, unsigned flags)
{
    return kmalloc_bounded(size, align, 0, flags);
}

void *kmalloc(size_t size, unsigned flags)
{
    return kmalloc_aligned(size, 8, flags);
}

void *kzalloc(size_t size, unsigned flags)
{
    return kmalloc_aligned(size, 8, flags | KM_ZERO);
}

void *kmalloc_fastcode(size_t size, size_t *got)
{
    /* the smallest region (from 2 KB: subregions of 256 bytes) whose top eighths hold @size
     * and end where the area ends */
    uintptr_t start = 0;
    for (uintptr_t region = 2048u; size && region <= (1u << 20); region <<= 1) {
        uintptr_t base = ALIGN_DOWN(s_fc_end - 1u, region), sub = region / 8u;
        uintptr_t need = ALIGN_UP(size, sub);
        if (s_fc_end % sub == 0 && need <= s_fc_end - base && need <= s_fc_end - s_fc_base) {
            start = s_fc_end - need;
            break;
        }
    }
    uint32_t key = irq_lock();
    void *p = nullptr;
    if (!s_fc_block && start) {
        s_fc_block = start;
        p = (void *)start;
        *got = s_fc_end - start;
    }
    irq_unlock(key);
    return p;
}

void kfree(void *ptr)
{
    if (!ptr)
        return;
    if ((uintptr_t)ptr >= s_fc_base && (uintptr_t)ptr < s_fc_end) {
        s_fc_block = 0;
        return;
    }
    for (int i = 0; i < P_COUNT; i++) {
        struct pool *pl = &s_pool[i];
        if (pl->t && tlsf_owns(pl->t, ptr)) {
            uint32_t key = irq_lock();
            int r = tlsf_free(pl->t, ptr);
            irq_unlock(key);
            if (r)
                panic("kfree(%p): invalid pointer or heap corruption (%d) in pool %s", ptr, r, pl->name);
            return;
        }
    }
    panic("kfree(%p): not a kernel heap pointer", ptr);
}

size_t ksize(const void *ptr)
{
    return tlsf_block_size(ptr);
}

int mm_pool_count(void)
{
    return P_COUNT + 1; /* and the fast code area */
}

int mm_pool_info(int index, struct mm_pool_info *info)
{
    if (index == P_COUNT) {
        info->name = "fastcode";
        info->base = s_fc_base;
        info->size = s_fc_end - s_fc_base;
        info->flags = KM_FAST | KM_EXEC;
        info->free = info->largest_free = s_fc_block ? s_fc_block - s_fc_base : info->size;
        return 0;
    }
    if (index < 0 || index >= P_COUNT)
        return -EINVAL;
    struct pool *pl = &s_pool[index];
    info->name = pl->name;
    info->base = pl->base;
    info->size = pl->end - pl->base;
    info->flags = pl->flags;
    info->free = 0;
    info->largest_free = 0;
    if (pl->t) {
        struct tlsf_stats st;
        uint32_t key = irq_lock();
        tlsf_get_stats(pl->t, &st);
        irq_unlock(key);
        info->free = st.free;
        info->largest_free = st.largest;
    }
    return 0;
}

/* Verify all heaps (slow: walks every block with interrupts masked per pool) */
int mm_check(void)
{
    for (int i = 0; i < P_COUNT; i++) {
        if (!s_pool[i].t)
            continue;
        uint32_t key = irq_lock();
        int r = tlsf_check(s_pool[i].t);
        irq_unlock(key);
        if (r) {
            printk("E: heap %s corrupted (%d)\n", s_pool[i].name, r);
            return r;
        }
    }
    return 0;
}
