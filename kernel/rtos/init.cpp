/*
 * kernel/rtos/init.cpp - kernel start-up.
 *
 * The core (memory, interrupts, console, scheduler, the monitor) starts first. Then the
 * operating system build (CONFIG_OS) starts the boot thread - SD card, device tree, driver
 * modules, init - and the RTOS build the application's app_main() in a task of its own.
 */
#include "kernel.h"
#include <crtos/rtos.h>
#include "fsl_device_registers.h"
#include "fsl_clock.h"

static void print_reset_cause(void)
{
    static const char *const names[] = { "power-on/pin", "lockup/sysreset", "CSU", "user", "watchdog", "JTAG",
                                         "JTAG sw", "watchdog3", "temperature" };
    uint32_t srsr = SRC->SRSR;
    char buf[96];
    int n = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(names); i++)
        if (srsr & (1u << i))
            n += ksnprintf(buf + n, sizeof(buf) - (size_t)n, "%s%s", n ? ", " : "", names[i]);
    if (!n)
        ksnprintf(buf, sizeof(buf), "unknown");
    SRC->SRSR = srsr; /* write one to clear */
    printk("reset cause: %s (SRSR=%03lx)\n", buf, (unsigned long)srsr);
}

#if !CONFIG_OS
/* The RTOS build's application (crtos/rtos.h): its own app_main(), or this one */
extern "C" __attribute__((weak)) void app_main(void)
{
    printk("no application in this firmware (crtos build --rtos DIR)\n");
}

static void app_thread(void *)
{
    app_main();
}
#endif

extern "C" void kernel_main(void) __attribute__((noreturn));

void kernel_main(void)
{
    /* The idle task sleeps with WFI. CCM_CLPCR resets to WAIT mode, where WFI gates the core
     * clock: SysTick would stop and the debugger could no longer halt the core. Stay in RUN. */
    CLOCK_SetMode(kCLOCK_ModeRun);
    fault_init();
    mm_init();
    irq_init();
    console_init();

    printk("\n\nCRTOS kernel, built " __DATE__ " " __TIME__ "\n");
    printk("cpu: i.MX RT1052 Cortex-M7 @ %lu MHz\n", (unsigned long)(SystemCoreClock / 1000000u));
    print_reset_cause();
    log_panic_previous();
    for (int i = 0; i < mm_pool_count(); i++) {
        struct mm_pool_info pi;
        if (mm_pool_info(i, &pi) == 0)
            printk("mem: %-6s %08lx-%08lx %6lu KB free\n", pi.name, (unsigned long)pi.base,
                   (unsigned long)(pi.base + pi.size), (unsigned long)(pi.free / 1024u));
    }

    sched_init();
    kworker_init();
    timers_start();
    kmon_start();
#if CONFIG_OS
    boot_start();
#else
    if (!kthread_create("app", app_thread, nullptr, CONFIG_APP_PRIO, CONFIG_APP_STACK))
        panic("cannot start the application");
#endif
    printk("starting scheduler\n");
    sched_start();
}
