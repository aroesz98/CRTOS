/*
 * crtos/rtos.h - the programming interface of an RTOS application.
 *
 * The RTOS build ("crtos build --rtos DIR") is the kernel's core alone - no processes, files,
 * driver modules or SD card - with one application linked into the firmware. The application
 * defines app_main(): it runs in a task of its own (priority CONFIG_APP_PRIO, stack
 * CONFIG_APP_STACK) once the scheduler runs, privileged like all kernel code, and may return
 * (its task ends) after it started the tasks it needs.
 *
 *   tasks       crtos/sched.h   kthread_create, task_sleep_ms, task_yield, priorities, time
 *   locks       crtos/sync.h    mutex (priority inheritance), semaphore, event flags
 *   messages    crtos/queue.h   message queues (from interrupts too, without waiting)
 *   timers      crtos/timer.h   one-shot and periodic software timers
 *   interrupts  crtos/irq.h     irq_request, irq_enable/irq_disable; irq_lock (crtos/arch.h)
 *   memory      crtos/mm.h      kmalloc, kzalloc, kfree
 *   output      crtos/printk.h  printk: the console (LPUART1) and kmon "dmesg"
 *
 * The hardware: the NXP SDK drivers of the board (fsl_gpio.h, fsl_lpi2c.h, fsl_lpspi.h, ...).
 * The kernel monitor stays on the console and the debug probe ("crtos kmon": ps, mem, irq,
 * dmesg, test).
 */
#ifndef CRTOS_RTOS_H
#define CRTOS_RTOS_H

#include <crtos/config.h>
#include <crtos/errno.h>
#include <crtos/arch.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <crtos/queue.h>
#include <crtos/timer.h>
#include <crtos/irq.h>
#include <crtos/mm.h>
#include <crtos/printk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The application: the RTOS build calls it once, in the task "app" */
void app_main(void);

#ifdef __cplusplus
}
#endif

#endif
