/*
 * kernel/rtos/ktest.h - the kernel self tests' common parts (internal): rtos/tests.cpp has the
 * core's tests and the runner, os/tests_os.cpp (CONFIG_OS) those of processes.
 */
#ifndef KERNEL_KTEST_H
#define KERNEL_KTEST_H
#include <stddef.h>
#include <stdint.h>

struct ktest {
    const char *name;
    void (*fn)(void);
    const char *what;
};

extern int ktest_failures;

#define CHECK(cond, ...)                     \
    do {                                     \
        if (!(cond)) {                       \
            cprintf("  FAIL: " __VA_ARGS__); \
            cprintf("\n");                   \
            ktest_failures++;                \
        }                                    \
    } while (0)

/* Wait until a task has exited and been reaped */
bool ktest_wait_task_gone(int id, uint32_t timeout_ms);
/* A kernel thread that computes with the FPU for a while (arg: its seed) */
void ktest_fpu_thread(void *arg);

#if CONFIG_OS
extern const struct ktest kernel_os_tests[];
extern const size_t kernel_os_ntests;
#endif

#endif
