/*
 * kernel/os/tests_os.cpp - the kernel self tests of processes (with rtos/tests.cpp: kmon
 * "test <name>" / "test all"): an unprivileged program runs and exits, its access to kernel
 * memory and its stack overflow end only the process, its registers survive system calls and
 * switches, and the cost of a system call.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include "ktest.h"
#include <crtos/syscall.h>
#include "fsl_device_registers.h"
#include <string.h>

#define XSTR(x) STR(x)
#define STR(x) #x

/* ---- user space ---------------------------------------------------------------------------
 * Test programs are position independent Thumb code copied into a process arena. */

#define UPROG(name)                                                        \
    extern "C" void name(void) __attribute__((naked, used, noinline, aligned(8)));    \
    extern "C" const uint8_t name##_end[]

#define UPROG_END(name) "   .balign 4\n   .global " #name "_end\n" #name "_end:\n"

UPROG(uprog_hello);
void uprog_hello(void)
{
    __asm volatile(
        "   adr     r1, 2f                  \n"
        "   mov     r2, r1                  \n"
        "1: ldrb    r3, [r2], #1            \n"
        "   cmp     r3, #0                  \n"
        "   bne     1b                      \n"
        "   sub     r2, r2, r1              \n"
        "   sub     r2, r2, #1              \n"
        "   movs    r0, #1                  \n"
        "   mov     r12, #" XSTR(SYS_WRITE) "\n"
        "   svc     #0                      \n"
        "   movs    r0, #42                 \n"
        "   mov     r12, #" XSTR(SYS_EXIT) "\n"
        "   svc     #0                      \n"
        "   b       .                       \n"
        "   .balign 4                       \n"
        "2: .asciz  \"  hello from user space (unprivileged, pid via MPU arena)\\n\"\n"
        UPROG_END(uprog_hello));
}

UPROG(uprog_badmem);
void uprog_badmem(void)
{
    __asm volatile(
        "   movw    r0, #0x0000             \n"
        "   movt    r0, #0x2000             \n" /* DTCM: kernel data */
        "   movs    r1, #1                  \n"
        "   str     r1, [r0]                \n"
        "   b       .                       \n"
        UPROG_END(uprog_badmem));
}

UPROG(uprog_stack);
void uprog_stack(void)
{
    __asm volatile(
        "1: push    {r4-r7, lr}             \n"
        "   sub     sp, sp, #64             \n"
        "   bl      1b                      \n"
        "   b       .                       \n"
        UPROG_END(uprog_stack));
}

UPROG(uprog_syscalls);
void uprog_syscalls(void)
{
    __asm volatile(
        "   mov     r4, r0                  \n"
        "1: mov     r12, #" XSTR(SYS_GETPID) "\n"
        "   svc     #0                      \n"
        "   subs    r4, r4, #1              \n"
        "   bne     1b                      \n"
        "   movs    r0, #0                  \n"
        "   mov     r12, #" XSTR(SYS_EXIT) "\n"
        "   svc     #0                      \n"
        "   b       .                       \n"
        UPROG_END(uprog_syscalls));
}

/* Keeps values in callee-saved core and FPU registers across sleeps/yields (other tasks
 * run in between) and exits with 0 if they survived, 1 otherwise */
UPROG(uprog_regs);
void uprog_regs(void)
{
    __asm volatile(
        "   mov     r5, r0                  \n"
        "   add     r6, r5, #1              \n"
        "   add     r7, r5, #2              \n"
        "   add     r8, r5, #3              \n"
        "   add     r11, r5, #4             \n"
        "   vmov    s16, r5                 \n"
        "   vmov    s0, r6                  \n"
        "   vmov    s31, r7                 \n"
        "   movs    r4, #40                 \n"
        "1: movs    r0, #1                  \n"
        "   mov     r12, #" XSTR(SYS_SLEEP_MS) "\n"
        "   svc     #0                      \n"
        "   vmov    r1, s16                 \n"
        "   cmp     r1, r5                  \n"
        "   bne     9f                      \n"
        "   vmov    r1, s0                  \n"
        "   cmp     r1, r6                  \n"
        "   bne     9f                      \n"
        "   vmov    r1, s31                 \n"
        "   cmp     r1, r7                  \n"
        "   bne     9f                      \n"
        "   add     r1, r5, #3              \n"
        "   cmp     r1, r8                  \n"
        "   bne     9f                      \n"
        "   add     r1, r5, #4              \n"
        "   cmp     r1, r11                 \n"
        "   bne     9f                      \n"
        "   mov     r12, #" XSTR(SYS_YIELD) "\n"
        "   svc     #0                      \n"
        "   subs    r4, r4, #1              \n"
        "   bne     1b                      \n"
        "   movs    r0, #0                  \n"
        "   b       8f                      \n"
        "9: movs    r0, #1                  \n"
        "8: mov     r12, #" XSTR(SYS_EXIT) "\n"
        "   svc     #0                      \n"
        "   b       .                       \n"
        UPROG_END(uprog_regs));
}

struct uproc_run {
    struct proc *p;
    int code;
};

static struct proc *uproc_start(const char *name, void (*prog)(void), const uint8_t *end, uint32_t arg, int prio)
{
    struct proc *p = proc_create(name, 64 * 1024, nullptr); /* with our reference for proc_wait() */
    if (!p)
        return nullptr;
    struct file *con = tty_open_console(); /* stdout/stderr */
    if (con) {
        vfs_file_get(con);
        handle_install_at(p, 1, H_FILE, 0, con);
        handle_install_at(p, 2, H_FILE, 0, con);
    }
    uintptr_t start = (uintptr_t)prog & ~1u;
    uint32_t len = (uint32_t)((uintptr_t)end - start);
    memcpy(p->arena, (const void *)start, len);
    p->brk = p->heap_start = ALIGN_UP(len, 32u);
    SCB_CleanDCache_by_Addr(p->arena, (int32_t)ALIGN_UP(len, 32u));
    SCB_InvalidateICache();
    if (!proc_thread_create(p, (uint32_t)(uintptr_t)p->arena | 1u, arg, 2048, prio)) {
        proc_kill(p, -ENOMEM);
        proc_put(p);
        return nullptr;
    }
    return p;
}

static int uproc_finish(struct proc *p, uint32_t timeout_ms, int *code)
{
    int r = proc_wait(p, timeout_ms, code);
    if (r == -ETIMEDOUT) {
        proc_kill(p, -ETIMEDOUT);
        proc_wait(p, 1000, code);
    }
    proc_put(p);
    return r;
}

static void test_user(void)
{
    int code = 0;
    struct proc *p = uproc_start("hello", uprog_hello, uprog_hello_end, 0, PRIO_NORMAL);
    CHECK(p, "cannot start process");
    if (!p)
        return;
    int r = uproc_finish(p, 1000, &code);
    CHECK(r == 0 && code == 42, "user program: wait %d, exit code %d (expected 42)", r, code);
}

static void test_usermem(void)
{
    int code = 0;
    struct proc *p = uproc_start("badmem", uprog_badmem, uprog_badmem_end, 0, PRIO_NORMAL);
    CHECK(p, "cannot start process");
    if (!p)
        return;
    int r = uproc_finish(p, 1000, &code);
    CHECK(r == 0 && code == -EFAULT, "access to kernel memory: wait %d, exit code %d (expected %d)", r, code, -EFAULT);
}

static void test_userstack(void)
{
    int code = 0;
    struct proc *p = uproc_start("stackbomb", uprog_stack, uprog_stack_end, 0, PRIO_NORMAL);
    CHECK(p, "cannot start process");
    if (!p)
        return;
    int r = uproc_finish(p, 1000, &code);
    CHECK(r == 0 && code == -EFAULT, "user stack overflow: wait %d, exit code %d (expected %d)", r, code, -EFAULT);
}

static void test_userregs(void)
{
    struct proc *p[3];
    for (int i = 0; i < 3; i++)
        p[i] = uproc_start("regs", uprog_regs, uprog_regs_end, 0x1000u * (uint32_t)(i + 1), PRIO_NORMAL);
    /* a kernel FPU user competes as well */
    task_t *k = kthread_create("fpu0", ktest_fpu_thread, (void *)2, PRIO_NORMAL, 1024);
    int kid = k ? task_id(k) : -1;
    for (int i = 0; i < 3; i++) {
        CHECK(p[i], "cannot start process %d", i);
        if (!p[i])
            continue;
        int code = -1;
        int r = uproc_finish(p[i], 3000, &code);
        CHECK(r == 0 && code == 0, "process %d: registers not preserved (wait %d, code %d)", i, r, code);
    }
    CHECK(ktest_wait_task_gone(kid, 3000), "kernel FPU thread did not finish");
}

static void test_syscall(void)
{
    const uint32_t n = 20000;
    uint64_t t0 = time_us();
    struct proc *p = uproc_start("sysperf", uprog_syscalls, uprog_syscalls_end, n, PRIO_HIGH);
    CHECK(p, "cannot start process");
    if (!p)
        return;
    int code = -1;
    int r = uproc_finish(p, 3000, &code);
    uint64_t dt = time_us() - t0;
    CHECK(r == 0 && code == 0, "syscall loop failed (wait %d, code %d)", r, code);
    cprintf("  %lu getpid() calls from user mode: %lu ns per call (incl. process setup)\n", (unsigned long)n,
            (unsigned long)(dt * 1000u / n));
}

const struct ktest kernel_os_tests[] = {
    { "user", test_user, "unprivileged process runs and exits" },
    { "usermem", test_usermem, "user access to kernel memory kills only the process" },
    { "userstack", test_userstack, "user stack overflow kills only the process" },
    { "userregs", test_userregs, "user core/FPU registers survive syscalls and switches" },
    { "syscall", test_syscall, "system call cost" },
};
const size_t kernel_os_ntests = ARRAY_SIZE(kernel_os_tests);
