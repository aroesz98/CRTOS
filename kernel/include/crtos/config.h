/*
 * crtos/config.h - kernel build-time configuration
 */
#ifndef CRTOS_CONFIG_H
#define CRTOS_CONFIG_H

/* The operating system part (kernel/os, kernel/subsys): processes and system calls, files,
 * driver modules and the device tree, started from the SD card. 0 builds the RTOS alone
 * (kernel/rtos): tasks and one application in the firmware ("crtos build --rtos"). */
#ifndef CONFIG_OS
#define CONFIG_OS                   1
#endif
/* the RTOS build's application task (app_main) */
#define CONFIG_APP_PRIO             10
#define CONFIG_APP_STACK            8192u

/* the thread running the software timers' functions (crtos/timer.h): above the drivers'
 * threads (17-20), below the monitor (22) and kworker (24) */
#define CONFIG_TIMER_PRIO           21
#define CONFIG_TIMER_STACK          2048u

#define CONFIG_CORE_CLOCK_HZ        600000000u
#define CONFIG_TICK_HZ              1000u
#define CONFIG_NUM_PRIO             32          /* task priorities 0 (idle) .. 31 (highest) */
#define CONFIG_TIMESLICE_TICKS      5u          /* round-robin quantum among equal priorities */

/* NVIC: 4 priority bits. IRQs at numeric priority >= CONFIG_IRQ_KERNEL_PRIO may use the
 * kernel API; 0..CONFIG_IRQ_KERNEL_PRIO-1 are "zero latency" and must not touch it. */
#define CONFIG_NVIC_PRIO_BITS       4
#define CONFIG_IRQ_KERNEL_PRIO      2
#define CONFIG_IRQ_DEFAULT_PRIO     8
#define CONFIG_NUM_IRQS             152         /* i.MX RT1052 NVIC lines */
#define CONFIG_NUM_VIRQS            192         /* GPIO interrupt lines (5 ports x 32 + spare) */

#define CONFIG_KTHREAD_STACK        2048u       /* default kernel thread stack */
#define CONFIG_KSTACK_SIZE          3072u       /* kernel (syscall) stack of every user thread */
/* No-access MPU region below every stack (power of two). Stack frames larger than this could
 * jump over it, so kernel code keeps frames smaller (checked from -fstack-usage output). The
 * guard is allocated on top of the requested stack size. */
#define CONFIG_STACK_GUARD          256u
#define CONFIG_STACK_GUARD_LOG2     8

#define CONFIG_LOG_BUF_SIZE         16384u      /* printk ring (also the console TX buffer) */
#define CONFIG_CONSOLE_RX_BUF       4096u

#define CONFIG_MAX_HANDLES          128         /* per process */

/* Emulated memory (kernel/os/vmem.cpp): the swap file on the card, created in one piece at
 * the first use, and the page cache in SDRAM taken for as long as some process has a region */
#define CONFIG_VMEM_FILE            "/crtos/var/swap"   /* on the card (FatFs path) */
#define CONFIG_VMEM_SIZE            (64u << 20)
#define CONFIG_VMEM_CACHE           (1u << 20)

#endif
