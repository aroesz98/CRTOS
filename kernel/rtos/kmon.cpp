/*
 * kernel/rtos/kmon.cpp - kernel monitor on the console.
 *
 * A small command interpreter that works without any file system or loadable module:
 * inspection (tasks, memory, MPU, interrupts), tests and recovery. With the operating system
 * part (CONFIG_OS) the commands of os/kmon_os.cpp come after these: processes, files, modules,
 * the device tree, devices.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include "kmon.h"
#include <crtos/irq.h>
#include "fsl_device_registers.h"
#include <stdlib.h>
#include <string.h>

#define KMON_PRIO  (PRIO_HIGH + 2)
#define LINE_MAX   512
#define ARGS_MAX   KMON_ARGS_MAX

static const struct kmon_cmd *find_cmd(const char *name);

/* ---- ps ---------------------------------------------------------------------------------- */

struct ps_ctx {
    uint64_t total;
    int pass;
};

static const char *state_name(const task_t *t)
{
    if (t == g_current)
        return "RUN";
    switch (t->state) {
    case TASK_READY: return "READY";
    case TASK_BLOCKED: return (t->flags & TF_SLEEPQ) && !t->waiting_on ? "SLEEP" : "WAIT";
    case TASK_SUSPENDED: return "SUSP";
    case TASK_DEAD: return "DEAD";
    default: return "?";
    }
}

static void ps_one(task_t *t, void *vctx)
{
    struct ps_ctx *c = (struct ps_ctx *)vctx;
    uint32_t key = irq_lock(); /* the tick updates the 64-bit count */
    uint64_t cycles = t->cycles;
    irq_unlock(key);
    uint64_t delta = cycles - t->cycles_snap;
    if (c->pass == 0) {
        c->total += delta;
        return;
    }
    t->cycles_snap = cycles;
    uint32_t pm = c->total ? (uint32_t)(delta * 1000u / c->total) : 0;
    uint32_t kfree_b = t->kstack ? stack_unused(t->kstack, t->kstack_size) : 0;
    char pid[8] = "-";
#if CONFIG_OS
    if (t->proc && t->state != TASK_DEAD)
        ksnprintf(pid, sizeof(pid), "%d", (int)t->proc->pid);
#endif
    cprintf("%4d %-15s %3d/%-3d %-5s %3lu.%lu %5lu/%-5lu", t->id, t->name, t->prio, t->base_prio, state_name(t),
            (unsigned long)(pm / 10), (unsigned long)(pm % 10), (unsigned long)(t->kstack_size - kfree_b),
            (unsigned long)t->kstack_size);
    if (t->ustack) {
        uint32_t ufree = stack_unused(t->ustack, t->ustack_size);
        cprintf(" %5lu/%-5lu", (unsigned long)(t->ustack_size - ufree), (unsigned long)t->ustack_size);
    } else {
        cprintf("            ");
    }
    cprintf(" %8lu %4s\n", (unsigned long)t->nswitch, pid);
}

static void cmd_ps(int, char **)
{
    struct ps_ctx c = { 0, 0 };
    cprintf("  ID NAME            PRI/BAS STATE  CPU%%  KSTACK used  USTACK used  SWITCHES  PID\n");
    task_foreach(ps_one, &c);
    c.pass = 1;
    task_foreach(ps_one, &c);
}

/* ---- memory ------------------------------------------------------------------------------ */

static void cmd_mem(int, char **)
{
    cprintf("pool     base       size       free       largest\n");
    for (int i = 0; i < mm_pool_count(); i++) {
        struct mm_pool_info pi;
        if (mm_pool_info(i, &pi))
            continue;
        cprintf("%-8s %08lx %10lu %10lu %10lu\n", pi.name, (unsigned long)pi.base, (unsigned long)pi.size,
                (unsigned long)pi.free, (unsigned long)pi.largest_free);
    }
    int r = mm_check();
    cprintf("heap check: %s\n", r ? "CORRUPTED" : "ok");
}

static void cmd_mpu(int, char **)
{
    mpu_dump();
}

static void cmd_irq(int, char **)
{
    cprintf(" IRQ  PRIO EN      COUNT  NAME\n");
    for (int i = 0; i < CONFIG_NUM_IRQS + CONFIG_NUM_VIRQS; i++) {
        struct irq_info ii;
        if (irq_info(i, &ii) == 0)
            cprintf("%4d  %4d %2s %10lu  %s\n", i, ii.prio, ii.enabled ? "y" : "n", (unsigned long)ii.count,
                    ii.name);
    }
}

static void cmd_uptime(int, char **)
{
    uint64_t us = time_us();
    task_t *idle = sched_idle_task();
    uint64_t cyc = 0;
    uint32_t key = irq_lock();
    cyc = idle->cycles;
    irq_unlock(key);
    uint32_t sec = (uint32_t)(us / 1000000u);
    uint64_t total = us * (SystemCoreClock / 1000000u);
    uint32_t idle_pm = total ? (uint32_t)(cyc * 1000u / total) : 0;
    cprintf("up %lu.%03lu s, idle %lu.%lu%% since boot\n", (unsigned long)sec,
            (unsigned long)((us / 1000u) % 1000u), (unsigned long)(idle_pm / 10u), (unsigned long)(idle_pm % 10u));
    struct tick_stats ts;
    sched_tick_stats(&ts);
    if (ts.late) {
        uint32_t gap_us = ts.max_gap_cycles / (SystemCoreClock / 1000000u);
        cprintf("ticks: %lu came late (%lu ms counted afterwards), longest gap %lu.%03lu ms at %lu.%03lu s in '%s'\n",
                (unsigned long)ts.late, (unsigned long)ts.lost, (unsigned long)(gap_us / 1000u),
                (unsigned long)(gap_us % 1000u), (unsigned long)(ts.max_gap_at / 1000u),
                (unsigned long)(ts.max_gap_at % 1000u), ts.max_gap_task);
    } else {
        cprintf("ticks: none came late\n");
    }
    uint32_t ovr, drop;
    console_stats(&ovr, &drop);
    cprintf("console: rx overruns %lu, rx dropped %lu, log bytes lost %lu\n", (unsigned long)ovr,
            (unsigned long)drop, (unsigned long)log_lost_bytes());
}

/* ---- control ----------------------------------------------------------------------------- */

static void cmd_kill(int argc, char **argv)
{
    if (argc < 2) {
        cprintf("usage: kill <task id>%s\n", CONFIG_OS ? " | kill -p <pid>" : "");
        return;
    }
#if CONFIG_OS
    if (!strcmp(argv[1], "-p") && argc > 2) {
        int pid = atoi(argv[2]);
        sched_lock();
        struct proc *p = proc_find(pid);
        if (p)
            proc_get(p);
        sched_unlock();
        if (!p) {
            cprintf("no process %d\n", pid);
            return;
        }
        proc_kill(p, -EINTR);
        proc_put(p);
        return;
    }
#endif
    int id = atoi(argv[1]);
    sched_lock();
    task_t *t = task_find(id);
#if CONFIG_OS
    struct proc *p = (t && t->proc) ? t->proc : nullptr; /* a program's thread: the whole program */
    if (p)
        proc_get(p);
#endif
    sched_unlock();
    if (!t) {
        cprintf("no task %d\n", id);
        return;
    }
#if CONFIG_OS
    if (p) {
        proc_kill(p, -EINTR);
        proc_put(p);
        return;
    }
#endif
    if (task_kill(t))
        cprintf("task %d cannot be killed\n", id);
}

/* dmesg [bytes]: the end of the kernel log */
static void cmd_dmesg(int argc, char **argv)
{
    size_t max = argc > 1 ? strtoul(argv[1], nullptr, 0) : 4096;
    if (max > CONFIG_LOG_BUF_SIZE)
        max = CONFIG_LOG_BUF_SIZE;
    char *buf = (char *)kmalloc(max, KM_LARGE);
    if (!buf)
        return;
    size_t n = log_snapshot(buf, max);
    size_t start = 0;
    if (n == max) /* begin at a line start */
        while (start < n && buf[start++] != '\n')
            ;
    con_write(buf + start, n - start);
    kfree(buf);
}

/* panic [text]: stop the system the way a fatal error does (tests the panic report) */
static void cmd_panic(int argc, char **argv)
{
    panic("requested from kmon: %s", argc > 1 ? argv[1] : "-");
}

static void cmd_reboot(int, char **)
{
    cprintf("rebooting...\n");
    task_sleep_ms(20); /* let the console drain */
    NVIC_SystemReset();
}

static void cmd_test(int argc, char **argv)
{
    kernel_tests(argc > 1 ? argv[1] : "help");
}

static void cmd_help(int, char **);

static const struct kmon_cmd s_cmds[] = {
    { "help", cmd_help, "list commands" },
    { "ps", cmd_ps, "tasks: priority, state, CPU load, stack use" },
    { "mem", cmd_mem, "kernel heaps (and integrity check)" },
    { "mpu", cmd_mpu, "MPU regions" },
    { "irq", cmd_irq, "registered interrupts" },
    { "uptime", cmd_uptime, "time since boot, idle time, console stats" },
    { "kill", cmd_kill, CONFIG_OS ? "kill <task id> | kill -p <pid>" : "kill <task id>" },
    { "dmesg", cmd_dmesg, "dmesg [bytes] - the end of the kernel log" },
    { "test", cmd_test, "test <name|all> - kernel self tests" },
    { "reboot", cmd_reboot, "reset the board" },
    { "panic", cmd_panic, "panic [text] - stop the system as on a fatal error (reboots in 10 s)" },
};

/* The core's commands, then (CONFIG_OS) those of the operating system part */
static const struct kmon_cmd *cmd_at(size_t i)
{
    if (i < ARRAY_SIZE(s_cmds))
        return &s_cmds[i];
#if CONFIG_OS
    if (i - ARRAY_SIZE(s_cmds) < kmon_os_ncmds)
        return &kmon_os_cmds[i - ARRAY_SIZE(s_cmds)];
#endif
    return nullptr;
}

static void cmd_help(int, char **)
{
    const struct kmon_cmd *c;
    for (size_t i = 0; (c = cmd_at(i)); i++)
        cprintf("  %-8s %s\n", c->name, c->help);
}

static const struct kmon_cmd *find_cmd(const char *name)
{
    const struct kmon_cmd *c;
    for (size_t i = 0; (c = cmd_at(i)); i++)
        if (!strcmp(c->name, name))
            return c;
    return nullptr;
}

/* ---- line input -------------------------------------------------------------------------- */

static int readline(char *buf, int max)
{
    int n = 0;
    int prev = 0;
    for (;;) {
        int c = con_getc(WAIT_FOREVER);
        if (c < 0)
            continue;
        if (c == '\n' && prev == '\r') { /* CR LF: already handled */
            prev = c;
            continue;
        }
        prev = c;
        if (c == '\r' || c == '\n') {
            con_write("\n", 1);
            buf[n] = 0;
            return n;
        }
        if (c == 8 || c == 127) {
            if (n) {
                n--;
                con_write("\b \b", 3);
            }
            continue;
        }
        if (c == 3) { /* Ctrl-C */
            con_write("^C\n", 3);
            buf[0] = 0;
            return 0;
        }
        if (c < 32 || c > 126)
            continue;
        if (n < max - 1) {
            buf[n++] = (char)c;
            char e = (char)c;
            con_write(&e, 1);
        }
    }
}

static int split_args(char *line, char **argv, int max)
{
    int argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = 0;
    }
    return argc;
}

/* One monitor instance; the thread's console hooks (con_write/con_getc) choose the channel */
void kmon_run(void *)
{
    char *line = (char *)kmalloc(LINE_MAX, KM_ANY);
    if (!line)
        return;
    char *argv[ARGS_MAX];
    cprintf("\nkmon ready - type 'help'\n");
    for (;;) {
        cprintf("kmon> ");
        if (readline(line, LINE_MAX) == 0)
            continue;
        int argc = split_args(line, argv, ARGS_MAX);
        if (!argc)
            continue;
        const struct kmon_cmd *c = find_cmd(argv[0]);
        if (!c) {
            cprintf("unknown command '%s' (try 'help')\n", argv[0]);
            continue;
        }
        uint64_t t0 = time_us();
        c->fn(argc, argv);
        uint64_t dt = time_us() - t0;
        if (dt > 100000u)
            cprintf("(%lu ms)\n", (unsigned long)(dt / 1000u));
    }
}

void kmon_start(void)
{
    if (!kthread_create("kmon", kmon_run, nullptr, KMON_PRIO, 3072))
        panic("cannot start kmon");
    swdcon_start();
}
