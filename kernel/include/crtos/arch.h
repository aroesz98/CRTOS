/*
 * crtos/arch.h - Cortex-M7 primitives shared by the kernel and modules
 */
#ifndef CRTOS_ARCH_H
#define CRTOS_ARCH_H

#include <stdint.h>
#include <crtos/config.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BASEPRI value that masks every interrupt allowed to use the kernel API */
#define KERNEL_BASEPRI (CONFIG_IRQ_KERNEL_PRIO << (8 - CONFIG_NVIC_PRIO_BITS))

#define barrier()   __asm volatile("" ::: "memory")
#define dsb()       __asm volatile("dsb 0xF" ::: "memory")
#define isb()       __asm volatile("isb 0xF" ::: "memory")
#define dmb()       __asm volatile("dmb 0xF" ::: "memory")

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

/* Kernel critical section: masks kernel-aware IRQs (and PendSV/SysTick), nests */
static inline uint32_t irq_lock(void)
{
    uint32_t old;
    __asm volatile("mrs %0, basepri" : "=r"(old));
    __asm volatile("msr basepri_max, %0\n isb 0xF" ::"r"(KERNEL_BASEPRI) : "memory");
    return old;
}

static inline void irq_unlock(uint32_t key)
{
    __asm volatile("msr basepri, %0\n isb 0xF" ::"r"(key) : "memory");
}

/* True in handler mode (ISR/exception) */
static inline int in_interrupt(void)
{
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0;
}

/* The processor cycle counter (DWT CYCCNT). A debugger that disconnects switches the trace
 * block off (pyOCD writes DEMCR = 0): the counter then stops and reads as 0, which made
 * CPU time and short waits jump. It is switched on again here (the value was kept). */
static inline uint32_t cpu_cycles(void)
{
    volatile uint32_t *const cyccnt = (volatile uint32_t *)0xE0001004u; /* DWT->CYCCNT */
    volatile uint32_t *const demcr = (volatile uint32_t *)0xE000EDFCu;  /* CoreDebug->DEMCR */
    uint32_t c = *cyccnt;
    if (__builtin_expect(c == 0 || !(*demcr & (1u << 24)), 0)) { /* TRCENA off (or just now) */
        *demcr |= 1u << 24;
        c = *cyccnt;
    }
    return c;
}

static inline void cpu_wfi(void)
{
    __asm volatile("dsb 0xF\n wfi" ::: "memory");
}

#ifdef __cplusplus
}
#endif

#endif
