/*
 * kernel/rtos/arch/fault.cpp - processor fault handling.
 *
 * A fault raised by a thread (kernel thread or user process) terminates only that thread -
 * for a user process the whole process - and the system keeps running. The thread's broken
 * context is discarded and it is restarted in task_exit() on its kernel stack, so all its
 * resources are released normally. Faults in interrupt handlers, inside kernel critical
 * sections or in the idle task are fatal (panic).
 *
 * A program's access to its emulated memory (vmem.cpp) is no error: the MemManage fault it
 * raises is carried out here (arch/emulate.cpp) or continues in the kernel to fetch the page.
 * So the entry keeps R4-R11 where the emulation can change them and restores them on the way
 * out.
 */
#include "../kernel.h"
#include "fsl_device_registers.h"

extern "C" {
uint64_t fault_handle(uint32_t *frame, uint32_t exc_return, uint32_t vect, uint32_t *regs);
void HardFault_Handler(void) __attribute__((naked));
void MemManage_Handler(void) __attribute__((naked));
void BusFault_Handler(void) __attribute__((naked));
void UsageFault_Handler(void) __attribute__((naked));
void fault_common(void) __attribute__((naked));
}

/* CFSR bits */
#define MMFSR_IACCVIOL  (1u << 0)
#define MMFSR_DACCVIOL  (1u << 1)
#define MMFSR_MUNSTKERR (1u << 3)
#define MMFSR_MSTKERR   (1u << 4)
#define MMFSR_MLSPERR   (1u << 5)
#define MMFSR_MMARVALID (1u << 7)
#define BFSR_IBUSERR    (1u << 8)
#define BFSR_PRECISERR  (1u << 9)
#define BFSR_IMPRECISERR (1u << 10)
#define BFSR_UNSTKERR   (1u << 11)
#define BFSR_STKERR     (1u << 12)
#define BFSR_LSPERR     (1u << 13)
#define BFSR_BFARVALID  (1u << 15)
#define UFSR_UNDEFINSTR (1u << 16)
#define UFSR_INVSTATE   (1u << 17)
#define UFSR_INVPC      (1u << 18)
#define UFSR_NOCP       (1u << 19)
#define UFSR_UNALIGNED  (1u << 24)
#define UFSR_DIVBYZERO  (1u << 25)

#define STACKING_ERRORS (MMFSR_MUNSTKERR | MMFSR_MSTKERR | MMFSR_MLSPERR | BFSR_UNSTKERR | BFSR_STKERR | BFSR_LSPERR)

void HardFault_Handler(void)  { __asm volatile("mov r2, #3\n b fault_common"); }
void MemManage_Handler(void)  { __asm volatile("mov r2, #4\n b fault_common"); }
void BusFault_Handler(void)   { __asm volatile("mov r2, #5\n b fault_common"); }
void UsageFault_Handler(void) { __asm volatile("mov r2, #6\n b fault_common"); }

void fault_common(void)
{
    __asm volatile(
        "   tst     lr, #4                  \n"
        "   ite     eq                      \n"
        "   mrseq   r0, msp                 \n"
        "   mrsne   r0, psp                 \n"
        "   mov     r1, lr                  \n"
        "   push    {r4-r11}                \n" /* for the dump and the emulation of an access */
        "   mov     r3, sp                  \n"
        "   bl      fault_handle            \n" /* r0 = PSP to resume with, r1 = EXC_RETURN */
        "   pop     {r4-r11}                \n" /* (as the emulation may have left them) */
        "   msr     psp, r0                 \n"
        "   bx      r1                      \n");
}

static const char *fault_reason(uint32_t cfsr, uint32_t hfsr)
{
    if (cfsr & MMFSR_MSTKERR)   return "stack overflow while entering an exception";
    if (cfsr & MMFSR_MUNSTKERR) return "bad stack on exception return";
    if (cfsr & MMFSR_MLSPERR)   return "stack overflow during lazy FPU save";
    if (cfsr & MMFSR_IACCVIOL)  return "instruction fetch from a forbidden address";
    if (cfsr & MMFSR_DACCVIOL)  return "memory access violation";
    if (cfsr & BFSR_STKERR)     return "bus error while stacking";
    if (cfsr & BFSR_UNSTKERR)   return "bus error while unstacking";
    if (cfsr & BFSR_LSPERR)     return "bus error during lazy FPU save";
    if (cfsr & BFSR_IBUSERR)    return "instruction bus error";
    if (cfsr & BFSR_PRECISERR)  return "precise data bus error";
    if (cfsr & BFSR_IMPRECISERR) return "imprecise data bus error";
    if (cfsr & UFSR_UNDEFINSTR) return "undefined instruction";
    if (cfsr & UFSR_INVSTATE)   return "invalid state (ARM mode / bad function pointer)";
    if (cfsr & UFSR_INVPC)      return "invalid EXC_RETURN";
    if (cfsr & UFSR_NOCP)       return "coprocessor access";
    if (cfsr & UFSR_UNALIGNED)  return "unaligned access";
    if (cfsr & UFSR_DIVBYZERO)  return "division by zero";
    if (hfsr & SCB_HFSR_VECTTBL_Msk) return "vector table read error";
    return "unknown";
}

static const char *const s_vect_name[] = { "", "", "", "HardFault", "MemManage", "BusFault", "UsageFault" };

typedef int (*out_fn)(const char *fmt, ...);

static void report(out_fn out, task_t *t, uint32_t vect, uint32_t cfsr, uint32_t hfsr, uint32_t addr, bool addr_valid,
                   const uint32_t *frame, bool frame_ok, const uint32_t *regs, uint32_t exc_return)
{
    out("*** %s: %s", s_vect_name[vect], fault_reason(cfsr, hfsr));
    if (addr_valid)
        out(" at %08lx", (unsigned long)addr);
    if (t) {
        out("\n    task %d '%s'", t->id, t->name);
        if (t->proc && t->proc->sb) /* runs in place: where its text and GOT are (appsym.py --xip) */
            out(" (process %d '%s', text %08lx, got %08lx%s)", (int)t->proc->pid, t->proc->name,
                (unsigned long)t->proc->text, (unsigned long)t->proc->sb, (t->flags & TF_IN_SYSCALL) ? ", in syscall" : "");
        else if (t->proc && t->proc->fastcode) /* ... and its fast code (tools/appsym.py --fast) */
            out(" (process %d '%s', arena %08lx, fast %08lx%s)", (int)t->proc->pid, t->proc->name,
                (unsigned long)(uintptr_t)t->proc->arena, (unsigned long)(uintptr_t)t->proc->fastcode,
                (t->flags & TF_IN_SYSCALL) ? ", in syscall" : "");
        else if (t->proc) /* the arena is where the program's image starts (tools/appsym.py BASE) */
            out(" (process %d '%s', arena %08lx%s)", (int)t->proc->pid, t->proc->name,
                (unsigned long)(uintptr_t)t->proc->arena, (t->flags & TF_IN_SYSCALL) ? ", in syscall" : "");
    }
    out("\n    cfsr=%08lx hfsr=%08lx exc_return=%08lx\n", (unsigned long)cfsr, (unsigned long)hfsr,
        (unsigned long)exc_return);
    if (frame_ok) {
        out("    pc=%08lx lr=%08lx sp=%08lx psr=%08lx\n", (unsigned long)frame[6], (unsigned long)frame[5],
            (unsigned long)(uintptr_t)frame, (unsigned long)frame[7]);
        out("    r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx\n", (unsigned long)frame[0], (unsigned long)frame[1],
            (unsigned long)frame[2], (unsigned long)frame[3], (unsigned long)frame[4]);
    } else {
        out("    sp=%08lx (exception frame not readable)\n", (unsigned long)(uintptr_t)frame);
    }
    out("    r4=%08lx r5=%08lx r6=%08lx r7=%08lx r8=%08lx r9=%08lx r10=%08lx r11=%08lx\n", (unsigned long)regs[0],
        (unsigned long)regs[1], (unsigned long)regs[2], (unsigned long)regs[3], (unsigned long)regs[4],
        (unsigned long)regs[5], (unsigned long)regs[6], (unsigned long)regs[7]);
}

/* printf-compatible sink that writes synchronously (used before panicking) */
static int panic_out(const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 1)
        n = sizeof(buf) - 1;
    log_panic_write(buf, (size_t)n);
    return n;
}

uint64_t fault_handle(uint32_t *frame, uint32_t exc_return, uint32_t vect, uint32_t *regs)
{
    uint32_t cfsr = SCB->CFSR;
    uint32_t hfsr = SCB->HFSR;
    uint32_t mmfar = SCB->MMFAR, bfar = SCB->BFAR;
    SCB->CFSR = cfsr; /* write-one-to-clear */
    SCB->HFSR = hfsr;

    uint32_t addr = 0;
    bool addr_valid = false;
    if (cfsr & MMFSR_MMARVALID) {
        addr = mmfar;
        addr_valid = true;
    } else if (cfsr & BFSR_BFARVALID) {
        addr = bfar;
        addr_valid = true;
    }

    task_t *t = g_current;
    bool thread = (exc_return & 0x8u) && (exc_return & 0x4u); /* thread mode on PSP */
    uint32_t psp = __get_PSP();

#if CONFIG_OS
    /* A program's access to its emulated memory: done here, or its page is fetched first */
    if (vect == 4 && thread && t && t->proc && t->proc->vmem && (cfsr & MMFSR_DACCVIOL) && addr_valid &&
        !(cfsr & STACKING_ERRORS) && (t->flags & (TF_USER | TF_IN_SYSCALL)) == TF_USER && !__get_BASEPRI() &&
        !__get_PRIMASK()) {
        uint64_t ret;
        if (vmem_fault(t, frame, regs, exc_return, addr, &ret))
            return ret;
    }
#endif

    /* Lazy FPU state preservation failed: the space reserved in the interrupted thread's
     * frame is not writable. Drop the lazy state and let the handler continue; the thread
     * is terminated when it is switched out (or faults unstacking). */
    if ((cfsr & (MMFSR_MLSPERR | BFSR_LSPERR)) && !thread && t) {
        FPU->FPCCR &= ~FPU_FPCCR_LSPACT_Msk;
        t->flags |= TF_KILLED | TF_REDIRECT;
        printk("\n*** %s: stack overflow during lazy FPU save - task %d '%s' will be killed\n",
               s_vect_name[vect], t->id, t->name);
        sched_pend_switch();
        return ((uint64_t)exc_return << 32) | psp;
    }

    /* The exception frame can only be read when it was stacked completely and does not
     * lie in a guard region */
    bool frame_ok = !(cfsr & STACKING_ERRORS) && !(thread && t && (uintptr_t)frame < t->stack_limit);

    const char *fatal = nullptr;
    uint32_t basepri = __get_BASEPRI(), primask = __get_PRIMASK();
    if (!(exc_return & 0x8u))
        fatal = "fault in interrupt/exception context";
    else if (!thread || !t)
        fatal = "fault before the scheduler started";
    else if (t == sched_idle_task())
        fatal = "fault in the idle task";
    else if (basepri || primask)
        fatal = "fault inside a kernel critical section";
    else if (sched_is_locked())
        fatal = "fault with the scheduler locked";
    else if (hfsr & SCB_HFSR_VECTTBL_Msk)
        fatal = "vector table fault";

    if (fatal) {
        __disable_irq();
        log_panic_flush();
        report(panic_out, t, vect, cfsr, hfsr, addr, addr_valid, frame, frame_ok, regs, exc_return);
        panic("%s", fatal);
    }

    report(printk, t, vect, cfsr, hfsr, addr, addr_valid, frame, frame_ok, regs, exc_return);
    uint32_t guard = t->stack_limit - CONFIG_STACK_GUARD;
    if ((cfsr & (MMFSR_MSTKERR | BFSR_STKERR)) || (addr_valid && addr >= guard && addr < t->stack_limit))
        printk("    -> stack overflow (%s stack)\n", (t->flags & (TF_USER | TF_IN_SYSCALL)) == TF_USER ? "user" : "kernel");
    printk("    -> %s terminated\n", t->proc ? "process" : "task");

#if CONFIG_OS
    if (t->proc)
        proc_kill(t->proc, -EFAULT); /* other threads of the process */
#endif

    /* Discard any lazily preserved FPU state of the broken context and restart the task in
     * task_exit() on top of its kernel stack, privileged. */
    FPU->FPCCR &= ~FPU_FPCCR_LSPACT_Msk;
    task_redirect_to_exit(t, -EFAULT);
    mpu_switch(t, t);
    __set_CONTROL(__get_CONTROL() & ~1u);
    __ISB();
    /* t->sp points at the software-saved part (CONTROL, R4-R11, EXC_RETURN); the hardware
     * frame follows it */
    return ((uint64_t)0xFFFFFFFDu << 32) | (uint32_t)(uintptr_t)(t->sp + 10);
}

void fault_init(void)
{
    NVIC_SetPriority(MemoryManagement_IRQn, 0);
    NVIC_SetPriority(BusFault_IRQn, 0);
    NVIC_SetPriority(UsageFault_IRQn, 0);
    SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk | SCB_SHCSR_BUSFAULTENA_Msk | SCB_SHCSR_USGFAULTENA_Msk;
    SCB->CCR |= SCB_CCR_DIV_0_TRP_Msk;
    __DSB();
    __ISB();
}
