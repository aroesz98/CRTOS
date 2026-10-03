/*
 * kernel/rtos/arch/context.cpp - Cortex-M7 context switching and the syscall trampoline.
 *
 * Saved context layout on a task stack (lowest address first):
 *     CONTROL, R4..R11, EXC_RETURN, [S16..S31 if the task used the FPU], hardware frame
 *
 * Syscalls (SVC #0 from unprivileged code, id in R12, args R0-R5) are not executed in the
 * SVC handler. The handler switches the task to privileged thread mode on its kernel
 * stack and "returns" into syscall_thread(), so system calls are preemptible and may block
 * like any kernel code. syscall_thread() ends with SVC #1, which restores the user frame
 * (with the result in R0/R1) and drops privileges again. Both stack guards of a user
 * thread stay on meanwhile (MPU regions 14 and 15), so no region changes. Only getpid,
 * gettid and time_us are answered in the handler itself (syscall_fast()).
 */
#define CRTOS_KERNEL 1
#include "../kernel.h"
#include <crtos/syscall.h>
#include "fsl_device_registers.h"

extern "C" {
void sched_switch(void);
void sched_context_overflow(task_t *t);
uint32_t syscall_enter(uint32_t *frame, uint32_t exc_return, uint32_t r4, uint32_t r5);
uint64_t syscall_exit(uint32_t *kframe);
void syscall_fast(uint32_t *frame);
void PendSV_Handler(void) __attribute__((naked));
void SVC_Handler(void) __attribute__((naked));
void sched_start_asm(void) __attribute__((naked, noreturn));
}

/* Context pushed by PendSV below the hardware frame: CONTROL, R4-R11, EXC_RETURN (+ S16-S31) */
#define CTX_SAVE_BYTES    40
#define CTX_SAVE_FP_BYTES 104

__attribute__((section(".ramfunc.$SRAM_ITC")))
void PendSV_Handler(void)
{
    __asm volatile(
        "   ldr     r3, =g_current          \n"
        "   ldr     r1, [r3]                \n"
        "   cbz     r1, 1f                  \n" /* first switch: nothing to save */
        "   ldr     r2, [r1, #4]            \n" /* flags */
        "   tst     r2, #1                  \n" /* TF_NOSAVE */
        "   bne     1f                      \n"
        "   mrs     r0, psp                 \n"
        "   ldr     r2, =task_stack_limit   \n"
        "   ldr     r2, [r2]                \n" /* limit of the active stack (set by mpu code) */
        "   tst     lr, #0x10               \n"
        "   ite     eq                      \n"
        "   subeq   r12, r0, %[save_fp]     \n" /* context incl. S16-S31 */
        "   subne   r12, r0, %[save]        \n"
        "   cmp     r12, r2                 \n"
        "   blo     2f                      \n"
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vstmdbeq r0!, {s16-s31}         \n"
        "   mrs     r2, control             \n"
        "   stmdb   r0!, {r2, r4-r11, lr}   \n"
        "   str     r0, [r1]                \n"
        "   b       1f                      \n"
        "2: mov     r0, r1                  \n" /* no room for the context: kill the task */
        "   bl      sched_context_overflow  \n"
        "1: mov     r0, %[basepri]          \n"
        "   msr     basepri, r0             \n"
        "   dsb                             \n"
        "   isb                             \n"
        "   bl      sched_switch            \n"
        "   mov     r0, #0                  \n"
        "   msr     basepri, r0             \n"
        "   ldr     r3, =g_current          \n"
        "   ldr     r1, [r3]                \n"
        "   ldr     r0, [r1]                \n"
        "   ldmia   r0!, {r2, r4-r11, lr}   \n"
        "   mrs     r3, control             \n"
        "   bic     r3, r3, #1              \n"
        "   and     r2, r2, #1              \n"
        "   orr     r3, r3, r2              \n"
        "   msr     control, r3             \n"
        "   isb                             \n"
        "   tst     lr, #0x10               \n"
        "   it      eq                      \n"
        "   vldmiaeq r0!, {s16-s31}         \n"
        "   msr     psp, r0                 \n"
        "   bx      lr                      \n"
        "   .ltorg                          \n"
        :
        : [basepri] "i"(KERNEL_BASEPRI), [save] "i"(CTX_SAVE_BYTES), [save_fp] "i"(CTX_SAVE_FP_BYTES));
}

/* The caller's privilege tells the two SVCs apart (no load of the instruction from the
 * program's memory): unprivileged code makes system calls, the privileged SVC #1 is the end
 * of one in syscall_thread(). A few calls are answered right here (syscall_fast()). */
KERNEL_FAST void SVC_Handler(void)
{
    __asm volatile(
        "   mrs     r0, control             \n"
        "   tst     r0, #1                  \n" /* nPRIV */
        "   beq     4f                      \n"
        "   mrs     r0, psp                 \n" /* programs always run on PSP */
        "   ldr     r1, [r0, #16]           \n" /* R12: the call number */
        "   sub     r1, r1, %[fast0]        \n"
        "   cmp     r1, %[nfast]            \n"
        "   bhs     3f                      \n"
        "   push    {r0, lr}                \n"
        "   bl      syscall_fast            \n" /* the result goes into the frame */
        "   pop     {r0, pc}                \n" /* EXC_RETURN: back to the program */
        "3: mov     r1, lr                  \n" /* syscall entry from user mode */
        "   mov     r2, r4                  \n"
        "   mov     r3, r5                  \n"
        "   bl      syscall_enter           \n" /* returns the kernel-stack PSP */
        "   msr     psp, r0                 \n"
        "   ldr     lr, =0xFFFFFFFD         \n"
        "   bx      lr                      \n"
        "4: mrs     r0, psp                 \n" /* syscall_thread's frame on the kernel stack */
        "   bl      syscall_exit            \n" /* returns r0 = PSP, r1 = EXC_RETURN */
        "   msr     psp, r0                 \n"
        "   mov     lr, r1                  \n"
        "   bx      lr                      \n"
        "   .ltorg                          \n"
        :
        : [fast0] "i"(SYS_GETPID), [nfast] "i"(SYS_TIME_US - SYS_GETPID + 1));
}

static_assert(SYS_GETTID == SYS_GETPID + 1 && SYS_TIME_US == SYS_GETPID + 2, "fast system calls");

/* System calls answered in the SVC handler itself, without the switch to the kernel stack
 * and privileged thread mode: they cannot block and take a few instructions */
extern "C" KERNEL_FAST void syscall_fast(uint32_t *frame)
{
    task_t *t = g_current;
    uint64_t r;
    if (frame[4] == SYS_GETPID)
        r = t->proc ? (uint32_t)t->proc->pid : 0;
    else if (frame[4] == SYS_GETTID)
        r = (uint32_t)t->id;
    else
        r = time_us();
    frame[0] = (uint32_t)r;
    frame[1] = (uint32_t)(r >> 32);
}

/* Called from main on MSP: reset MSP to its top, trigger the first PendSV and enable IRQs */
void sched_start_asm(void)
{
    __asm volatile(
        "   cpsid   i                       \n"
        "   ldr     r0, =_vStackTop         \n"
        "   msr     msp, r0                 \n"
        "   mov     r0, #0                  \n"
        "   msr     basepri, r0             \n"
        "   ldr     r0, =0xE000ED04         \n" /* ICSR */
        "   mov     r1, #0x10000000         \n" /* PENDSVSET */
        "   str     r1, [r0]                \n"
        "   dsb                             \n"
        "   isb                             \n"
        "   cpsie   i                       \n"
        "1: b       1b                      \n"
        "   .ltorg                          \n");
}

/* ---- syscall trampoline ------------------------------------------------------------ */

extern "C" KERNEL_FAST uint64_t syscall_thread_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    task_t *t = g_current;
#if CONFIG_OS
    if (t->flags & TF_PAGEIN) { /* a fault, not a system call: the program's R0/R1 stay */
        t->flags &= ~TF_PAGEIN;
        vmem_pagein_call(t->sc_a4);
        return ((uint64_t)a1 << 32) | a0;
    }
    return (uint64_t)syscall_dispatch(t->sc_id, a0, a1, a2, a3, t->sc_a4, t->sc_a5);
#else
    (void)t; (void)a0; (void)a1; (void)a2; (void)a3;
    return (uint64_t)(uint32_t)-ENOSYS; /* (no programs without the operating system part) */
#endif
}

/* Runs in privileged thread mode on the task's kernel stack. R4-R11 still hold the caller's
 * values: keep them across the C code, which never returns here normally. */
static KERNEL_FAST void __attribute__((naked)) syscall_thread(void)
{
    __asm volatile(
        "   push    {r4-r11}                \n"
        "   bl      syscall_thread_c        \n" /* result in r0:r1 */
        "   pop     {r4-r11}                \n"
        "   svc     #1                      \n"
        "   b       .                       \n");
}

static inline void set_npriv(uint32_t npriv)
{
    uint32_t c;
    __asm volatile("mrs %0, control" : "=r"(c));
    c = (c & ~1u) | npriv;
    __asm volatile("msr control, %0\n isb 0xF" ::"r"(c) : "memory");
}

KERNEL_FAST uint32_t syscall_enter(uint32_t *frame, uint32_t exc_return, uint32_t r4, uint32_t r5)
{
    task_t *t = g_current;
    if (!(t->flags & TF_USER) || (t->flags & TF_IN_SYSCALL))
        panic("SVC #0 from non-user context (task '%s')", t->name);

    t->usp = frame;
    t->uexc_return = exc_return;
    t->sc_id = frame[4];  /* R12 */
    t->sc_a4 = r4;
    t->sc_a5 = r5;
    t->flags |= TF_IN_SYSCALL;
    /* S16-S31 are not in the exception frame: keep the user's values while kernel code and
     * other tasks run (the lazily stacked S0-S15 stay in the user frame) */
    if (!(exc_return & 0x10u))
        __asm volatile("vstmia %0, {s16-s31}" ::"r"(t->fp_save) : "memory");

    uint32_t *k = (uint32_t *)((uintptr_t)(t->kstack + t->kstack_size) & ~7u);
    *--k = 0x01000000u;                               /* xPSR */
    *--k = (uint32_t)syscall_thread & ~1u;            /* PC */
    *--k = 0;                                         /* LR (never returns) */
    *--k = 0;                                         /* R12 */
    *--k = frame[3];
    *--k = frame[2];
    *--k = frame[1];
    *--k = frame[0];

    /* both stack guards are on already (MPU regions 14 and 15): only the limit PendSV checks
     * when it saves the context moves to the kernel stack */
    t->stack_limit = (uint32_t)(uintptr_t)t->kstack + CONFIG_STACK_GUARD;
    task_stack_limit = t->stack_limit;
    set_npriv(0);
    return (uint32_t)k;
}

/* A user thread's fault that needs kernel work which may block (a page of emulated memory,
 * vmem.cpp) continues like a system call: in privileged thread mode on its kernel stack, back
 * at the same instruction afterwards with all registers as they were. Called from the fault
 * handler; returns the kernel-stack PSP to go on with (EXC_RETURN 0xFFFFFFFD). */
KERNEL_FAST uint32_t syscall_enter_fault(uint32_t *frame, uint32_t exc_return, uint32_t id, uint32_t arg)
{
    uint32_t k = syscall_enter(frame, exc_return, arg, 0);
    task_t *t = g_current;
    t->sc_id = id;
    t->flags |= TF_PAGEIN;
    return k;
}

static void kill_in_kernel(void *code)
{
    task_exit((int)(intptr_t)code);
}

KERNEL_FAST uint64_t syscall_exit(uint32_t *kframe)
{
    task_t *t = g_current;
    if (!(t->flags & TF_IN_SYSCALL))
        panic("SVC from kernel code (task '%s', pc=%08lx)", t->name, (unsigned long)kframe[6]);
    /* FPU state of the kernel part is dead: drop a pending lazy save into the kernel stack
     * frame, the context returned to is a different one */
    FPU->FPCCR &= ~FPU_FPCCR_LSPACT_Msk;
    if (t->flags & TF_KILLED) {
        /* Killed during the syscall: finish in kernel mode instead of returning to user */
        uint32_t *k = (uint32_t *)((uintptr_t)(t->kstack + t->kstack_size) & ~7u);
        *--k = 0x01000000u;
        *--k = (uint32_t)kill_in_kernel & ~1u;
        *--k = 0;
        *--k = 0;
        *--k = 0;
        *--k = 0;
        *--k = 0;
        *--k = (uint32_t)-EINTR;
        return ((uint64_t)0xFFFFFFFDu << 32) | (uint32_t)k;
    }
    uint32_t *uf = t->usp;
    uf[0] = kframe[0];
    uf[1] = kframe[1];
    t->flags &= ~TF_IN_SYSCALL;
    if (!(t->uexc_return & 0x10u))
        __asm volatile("vldmia %0, {s16-s31}" ::"r"(t->fp_save) : "memory");
    t->stack_limit = (uint32_t)(uintptr_t)t->ustack + CONFIG_STACK_GUARD;
    task_stack_limit = t->stack_limit;
    set_npriv(1);
    return ((uint64_t)t->uexc_return << 32) | (uint32_t)uf;
}
