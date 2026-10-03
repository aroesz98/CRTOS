/*
 * kernel/rtos/arch/mpu.cpp - memory protection.
 *
 * Region map (a higher number wins where regions overlap):
 *   0      4 GB background, no access (nothing outside the regions below is reachable)
 *   1..7   ITCM, DTCM, OCRAM, SDRAM, non-cacheable SDRAM, XIP flash, peripherals:
 *          privileged only - user code sees none of the kernel's memory - except the flash,
 *          which user code may read and execute: programs run in place from /flash0 there
 *   8      arena of the process owning the running thread (user RWX)
 *   9..11  shared memory windows of that process (user RW, XN), one of them maybe its fast
 *          code block in ITCM or OCRAM (user RX, app.cpp)
 *   12     NULL guard 0x0-0xFF (no access even for the kernel)
 *   13     guard at the bottom of the MSP (interrupt) stack
 *   14     guard at the bottom of the running thread's active stack
 *   15     spare
 * PRIVDEFENA is off, so a stray kernel pointer into unmapped space faults as well.
 */
#include "../kernel.h"
#include "fsl_device_registers.h"

extern "C" uint32_t _vStackBase[];

/* RASR fields */
#define RASR_ENABLE      1u
#define RASR_SIZE(log2)  ((uint32_t)((log2) - 1) << 1)
#define RASR_SRD(m)      ((uint32_t)(m) << 8)
#define RASR_XN          (1u << 28)
#define RASR_AP(ap)      ((uint32_t)(ap) << 24)
#define AP_NONE          0u
#define AP_PRIV_RW       1u
#define AP_PRIV_RW_USER_RO 2u
#define AP_FULL          3u
#define AP_PRIV_RO       5u
#define AP_RO            6u                 /* read-only for both */
/* TEX/S/C/B memory types */
#define MT_STRONG        0u
#define MT_DEVICE        (2u << 19)                          /* TEX=2: device, non-shareable */
#define MT_NORMAL_WB     ((1u << 17) | (1u << 16))           /* write-back, no write-allocate */
#define MT_NORMAL_WBWA   ((1u << 19) | (1u << 17) | (1u << 16)) /* write-back, read/write-allocate */
#define MT_NORMAL_NC     (1u << 19)                          /* TEX=1 C=0 B=0: non-cacheable */

#define RBAR(region, base) (((uint32_t)(base) & ~31u) | MPU_RBAR_VALID_Msk | (uint32_t)(region))

volatile uint32_t task_stack_limit;
static struct proc *s_mpu_proc; /* process whose arena is programmed into region 8 */

static inline uint32_t log2_ceil(uint32_t v)
{
    return v <= 1 ? 0 : 32u - (uint32_t)__builtin_clz(v - 1u);
}

static void region_set(int n, uint32_t base, uint32_t log2size, uint32_t attr)
{
    MPU->RBAR = RBAR(n, base);
    MPU->RASR = attr | RASR_SIZE(log2size) | RASR_ENABLE;
}

int mpu_encode_region(int region, uintptr_t base, uint32_t size, uint32_t attr, uint32_t *rbar, uint32_t *rasr)
{
    if (size < 32)
        size = 32;
    /* The smallest region that works: a power of two aligned to its size, or a run of its
     * eighths (subregions, in regions of 256 bytes or more) with the others disabled */
    uint32_t l2, srd = 0;
    uintptr_t block = 0;
    for (l2 = log2_ceil(size); l2 < 32; l2++) {
        uint32_t full = 1u << l2;
        block = base & ~(uintptr_t)(full - 1u);
        if (full == size && block == base)
            break;
        if (l2 < 8)
            continue;
        uint32_t sub = full >> 3;
        uint32_t first = (uint32_t)(base - block) / sub, n = size / sub;
        if ((base - block) % sub || size % sub || first + n > 8u)
            continue;
        srd = ~(((1u << n) - 1u) << first) & 0xFFu;
        break;
    }
    if (l2 >= 32)
        return -EINVAL;
    uint32_t a;
    switch (attr) {
    case MPU_ATTR_USER_RWX:   a = RASR_AP(AP_FULL) | MT_NORMAL_WBWA; break;
    case MPU_ATTR_USER_RW:    a = RASR_AP(AP_FULL) | MT_NORMAL_WBWA | RASR_XN; break;
    case MPU_ATTR_USER_RW_NC: a = RASR_AP(AP_FULL) | MT_NORMAL_NC | RASR_XN; break;
    case MPU_ATTR_USER_RX:    a = RASR_AP(AP_PRIV_RW_USER_RO) | MT_NORMAL_WBWA; break;
    default: return -EINVAL;
    }
    *rbar = RBAR(region, block);
    *rasr = a | RASR_SRD(srd) | RASR_SIZE(l2) | RASR_ENABLE;
    return 0;
}

/* A disabled region slot (RBAR still selects the slot, so it can be written blindly) */
void mpu_empty_region(int region, uint32_t *rbar, uint32_t *rasr)
{
    *rbar = RBAR(region, 0);
    *rasr = 0;
}

#define GUARD_RASR (RASR_XN | RASR_AP(AP_NONE) | RASR_SIZE(CONFIG_STACK_GUARD_LOG2) | RASR_ENABLE)

void mpu_set_guard(task_t *t, const void *stack_base)
{
    uintptr_t b = (uintptr_t)stack_base;
    BUG_ON(b & (CONFIG_STACK_GUARD - 1));
    t->guard_rbar = RBAR(MPU_REGION_GUARD, b);
    t->guard_rasr = GUARD_RASR;
    t->kguard_rbar = RBAR(MPU_REGION_KGUARD, 0);
    t->kguard_rasr = 0;
    t->stack_limit = b + CONFIG_STACK_GUARD;
}

void mpu_set_user_guards(task_t *t)
{
    uintptr_t u = (uintptr_t)t->ustack, k = (uintptr_t)t->kstack;
    BUG_ON((u | k) & (CONFIG_STACK_GUARD - 1));
    t->guard_rbar = RBAR(MPU_REGION_GUARD, u);
    t->guard_rasr = GUARD_RASR;
    t->kguard_rbar = RBAR(MPU_REGION_KGUARD, k);
    t->kguard_rasr = GUARD_RASR;
    t->stack_limit = u + CONFIG_STACK_GUARD;
}

/* Load a user region, disabling it first: between the two writes the slot has the new base
 * with the old size and rights, which may cover memory in use - a window moved to address 0
 * made ITCM, where this code runs, non-executable for a few cycles. */
__attribute__((always_inline)) static inline void region_load(uint32_t rbar, uint32_t rasr)
{
    MPU->RNR = rbar & MPU_RBAR_REGION_Msk;
    MPU->RASR = 0;
    if (rasr) {
        MPU->RBAR = rbar;
        MPU->RASR = rasr;
    }
}

/* Program the regions that follow the running thread (PendSV, syscall entry/exit, faults) */
KERNEL_FAST void mpu_switch(task_t *prev, task_t *next)
{
    (void)prev;
    /* every guard has the same size and rights: no harmful state in between */
    MPU->RBAR = next->guard_rbar;
    MPU->RASR = next->guard_rasr;
    MPU->RBAR = next->kguard_rbar;
    MPU->RASR = next->kguard_rasr;
    task_stack_limit = next->stack_limit;

    /* Kernel threads are privileged and do not need regions 8-11, so they are only
     * reloaded when a thread of a different process gets the CPU. */
    struct proc *p = next->proc;
    if (p && p != s_mpu_proc) {
        s_mpu_proc = p;
        region_load(p->arena_rbar, p->arena_rasr);
        for (int i = 0; i < SHM_WINDOWS; i++)
            region_load(p->win[i].rbar, p->win[i].rasr);
    }
    __DSB();
    __ISB();
}

/* The regions of @p changed (shared memory mapped/unmapped): reload them if they are active */
void mpu_proc_changed(struct proc *p)
{
    uint32_t key = irq_lock();
    if (s_mpu_proc == p) {
        s_mpu_proc = nullptr;
        task_t *cur = g_current;
        if (cur && cur->proc == p)
            mpu_switch(cur, cur);
    }
    irq_unlock(key);
}

void mpu_forget_proc(struct proc *p)
{
    uint32_t key = irq_lock();
    if (s_mpu_proc == p) {
        s_mpu_proc = nullptr;
        for (int r = MPU_REGION_ARENA; r < MPU_REGION_ARENA + 1 + SHM_WINDOWS; r++) {
            MPU->RNR = (uint32_t)r;
            MPU->RASR = 0;
        }
        __DSB();
        __ISB();
    }
    irq_unlock(key);
}

/* Called from main() before the clocks are raised: replaces the SDK's BOARD_ConfigMPU() */
extern "C" void mpu_init(void)
{
    if (SCB->CCR & SCB_CCR_IC_Msk)
        SCB_DisableICache();
    if (SCB->CCR & SCB_CCR_DC_Msk)
        SCB_DisableDCache();

    __DMB();
    MPU->CTRL = 0;
    for (int i = 0; i < 16; i++) {
        MPU->RNR = (uint32_t)i;
        MPU->RASR = 0;
        MPU->RBAR = 0;
    }

    region_set(0, 0x00000000u, 32, RASR_XN | RASR_AP(AP_NONE) | MT_STRONG);
    region_set(1, 0x00000000u, 17, RASR_AP(AP_PRIV_RW) | MT_NORMAL_WB);               /* ITCM 128K */
    region_set(2, 0x20000000u, 17, RASR_XN | RASR_AP(AP_PRIV_RW) | MT_NORMAL_WB);     /* DTCM 128K */
    region_set(3, 0x20200000u, 18, RASR_AP(AP_PRIV_RW) | MT_NORMAL_WBWA);             /* OCRAM 256K */
    region_set(4, 0x80000000u, 25, RASR_AP(AP_PRIV_RW) | MT_NORMAL_WBWA);             /* SDRAM 32M */
    region_set(5, 0x81E00000u, 21, RASR_XN | RASR_AP(AP_PRIV_RW) | MT_NORMAL_NC);     /* NCACHE 2M */
    /* XIP flash 64M: the kernel runs from it, programs from the file system /flash0 on it
     * (their code in place; the kernel's image is readable but holds nothing secret) */
    region_set(6, 0x60000000u, 26, RASR_AP(AP_RO) | MT_NORMAL_WB);
    region_set(7, 0x40000000u, 22, RASR_XN | RASR_AP(AP_PRIV_RW) | MT_DEVICE);        /* peripherals 4M */
    region_set(MPU_REGION_NULL, 0x00000000u, 8, RASR_XN | RASR_AP(AP_NONE) | MT_STRONG);
    region_set(MPU_REGION_MSP, (uint32_t)_vStackBase, CONFIG_STACK_GUARD_LOG2, RASR_XN | RASR_AP(AP_NONE) | MT_STRONG);
    task_stack_limit = 0;

    MPU->CTRL = MPU_CTRL_ENABLE_Msk; /* no PRIVDEFENA, no HFNMIENA */
    __DSB();
    __ISB();

    SCB_EnableDCache();
    SCB_EnableICache();
}

static const char *ap_name(uint32_t ap)
{
    switch (ap) {
    case 0: return "--/--";
    case 1: return "rw/--";
    case 2: return "rw/r-";
    case 3: return "rw/rw";
    case 5: return "r-/--";
    case 6: case 7: return "r-/r-";
    default: return "?";
    }
}

void mpu_dump(void)
{
    cprintf("MPU ctrl=%lx  (priv/user access)\n", (unsigned long)MPU->CTRL);
    for (int i = 0; i < 16; i++) {
        uint32_t key = irq_lock();
        MPU->RNR = (uint32_t)i;
        uint32_t rbar = MPU->RBAR, rasr = MPU->RASR;
        irq_unlock(key);
        if (!(rasr & RASR_ENABLE)) {
            cprintf("  %2d  -\n", i);
            continue;
        }
        uint32_t l2 = ((rasr >> 1) & 0x1F) + 1;
        uint32_t base = rbar & ~31u;
        uint64_t size = 1ull << l2;
        cprintf("  %2d  %08lx-%08lx %s %s tex%lu%s%s srd=%02lx\n", i, (unsigned long)base,
                (unsigned long)(base + size - 1), ap_name((rasr >> 24) & 7), (rasr & RASR_XN) ? "xn" : "x ",
                (unsigned long)((rasr >> 19) & 7), (rasr & (1u << 17)) ? " C" : "", (rasr & (1u << 16)) ? " B" : "",
                (unsigned long)((rasr >> 8) & 0xFF));
    }
}
