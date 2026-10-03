/*
 * kernel/rtos/kmon.h - the kernel monitor's commands (internal).
 *
 * The core's commands (tasks, memory, interrupts, log, tests) are in rtos/kmon.cpp; with the
 * operating system part (CONFIG_OS) os/kmon_os.cpp adds those of processes, files, modules,
 * the device tree and devices.
 */
#ifndef KERNEL_KMON_H
#define KERNEL_KMON_H
#include <stddef.h>

#define KMON_ARGS_MAX 16

struct kmon_cmd {
    const char *name;
    void (*fn)(int argc, char **argv);
    const char *help;
};

#if CONFIG_OS
extern const struct kmon_cmd kmon_os_cmds[];
extern const size_t kmon_os_ncmds;
#endif

#endif
