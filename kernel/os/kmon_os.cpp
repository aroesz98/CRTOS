/*
 * kernel/os/kmon_os.cpp - the kernel monitor's commands of the operating system part (after
 * those of rtos/kmon.cpp): processes, files (os/kmon_cmds.cpp), the device tree and modules,
 * devices (os/kmon_dev.cpp), emulated memory, the network.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include "kmon.h"
#include <crtos/syscall.h>
#include <string.h>

#define ARGS_MAX KMON_ARGS_MAX

void net_dump(void); /* subsys/net.cpp */

static void cmd_net(int, char **)
{
    net_dump();
}

/* ---- processes ---------------------------------------------------------------------------- */

static void procs_one(struct proc *p, void *)
{
    static const char *const st[] = { "run", "ending", "ended" };
    cprintf("%4d %4d %-15s %-6s %3d %7lu %7lu %3d  %02lx\n", p->pid, p->parent ? p->parent->pid : 0, p->name,
            p->state <= PROC_DEAD ? st[p->state] : "?", (int)p->nthreads,
            (unsigned long)(p->released ? 0 : p->arena_size / 1024u), (unsigned long)((p->brk - p->heap_start) / 1024u),
            p->released ? 0 : handle_count(p), (unsigned long)p->caps);
}

static void cmd_procs(int, char **)
{
    cprintf(" PID PPID NAME            STATE  THR ARENA/K  HEAP/K HND CAPS\n");
    proc_foreach(procs_one, nullptr);
}

/* run [-w] <program> [args...]: start a program with the console as stdin/out/err and all
 * capabilities; -w waits for it and prints the exit code (Ctrl-C ends the program) */
static void cmd_run(int argc, char **argv)
{
    bool wait = argc > 1 && !strcmp(argv[1], "-w");
    int first = wait ? 2 : 1;
    if (argc <= first) {
        cprintf("usage: run [-w] <program> [args...]\n");
        return;
    }
    char *path = (char *)kmalloc(VFS_PATH_MAX, KM_ANY);
    if (!path)
        return;
    const char *prog = argv[first];
    struct vfs_stat st;
    if (prog[0] == '/')
        ksnprintf(path, VFS_PATH_MAX, "%s", prog);
    else if (strchr(prog, '.'))
        ksnprintf(path, VFS_PATH_MAX, "%s/%s", CRTOS_ROOT, prog);
    else {
        /* a name: where PATH finds it, the flash file system first */
        ksnprintf(path, VFS_PATH_MAX, "/flash0/bin/%s.app", prog);
        if (vfs_stat(path, &st))
            ksnprintf(path, VFS_PATH_MAX, "%s/bin/%s.app", CRTOS_ROOT, prog);
    }
    const char *av[ARGS_MAX + 1];
    int n = 0;
    for (int i = first; i < argc && n < ARGS_MAX; i++)
        av[n++] = argv[i];
    av[n] = nullptr;
    static const char *const envp[] = { CRTOS_PROGRAM_ENV, nullptr };
    struct file *con = nullptr;
    vfs_open(kmon_console_path(), VFS_O_RDWR, &con);
    struct file *stdio[3] = { con, con, con };
    int err = 0;
    struct proc *p = app_spawn(nullptr, path, av, envp, stdio, CAP_ALL, PRIO_NORMAL, &err);
    if (con)
        vfs_close(con);
    if (!p) {
        cprintf("run: %s: %d\n", path, err);
        kfree(path);
        return;
    }
    cprintf("started %s as pid %d\n", path, p->pid);
    kfree(path);
    if (wait) {
        int code = 0, r;
        while ((r = proc_wait(p, 100, &code)) == -ETIMEDOUT) {
            if (con_getc(0) == 3) { /* Ctrl-C */
                cprintf("^C\n");
                proc_kill(p, -EINTR);
                r = proc_wait(p, 2000, &code);
                break;
            }
        }
        cprintf("pid %d exited with %d%s\n", p->pid, code, r ? " (wait interrupted)" : "");
    }
    proc_put(p);
}

/* exit: give the console input back to user space (Ctrl-] returns here) */
static void cmd_exit(int, char **)
{
    cprintf("console to user space - Ctrl-] returns to kmon\n");
    console_set_focus(CON_TTY);
}

static void cmd_vmem(int, char **)
{
    vmem_show(cprintf);
}

/* kmon_cmds.cpp */
void kcmd_ls(int, char **);
void kcmd_cat(int, char **);
void kcmd_hexdump(int, char **);
void kcmd_rm(int, char **);
void kcmd_mkdir(int, char **);
void kcmd_mv(int, char **);
void kcmd_df(int, char **);
void kcmd_sd(int, char **);
void kcmd_put(int, char **);
void kcmd_dt(int, char **);
void kcmd_dtload(int, char **);
void kcmd_devices(int, char **);
void kcmd_drivers(int, char **);
void kcmd_lsmod(int, char **);
void kcmd_insmod(int, char **);
void kcmd_rmmod(int, char **);
void kcmd_crc32(int, char **);
void kcmd_sdbench(int, char **);
void kcmd_sdstress(int, char **);
void kcmd_stage(int, char **);
void kcmd_savestage(int, char **);
/* kmon_dev.cpp */
void kcmd_i2cdetect(int, char **);
void kcmd_evtest(int, char **);
void kcmd_fb(int, char **);
void kcmd_fbtest(int, char **);
void kcmd_gpu2dtest(int, char **);

const struct kmon_cmd kmon_os_cmds[] = {
    { "procs", cmd_procs, "user processes" },
    { "run", cmd_run, "run [-w] <program> [args] - start a program (name: /flash0/bin or /sd/crtos/bin/<name>.app)" },
    { "exit", cmd_exit, "give the console to user space (Ctrl-] comes back)" },
    { "vmem", cmd_vmem, "emulated memory: swap file, page cache, statistics" },
    { "net", cmd_net, "network interfaces and sockets" },
    { "ls", kcmd_ls, "ls [dir]" },
    { "cat", kcmd_cat, "cat <file>" },
    { "hexdump", kcmd_hexdump, "hexdump <file> [offset] [length]" },
    { "rm", kcmd_rm, "rm <path>..." },
    { "mkdir", kcmd_mkdir, "mkdir <dir>..." },
    { "mv", kcmd_mv, "mv <from> <to>" },
    { "df", kcmd_df, "free space on /sd" },
    { "sd", kcmd_sd, "sd [trace|reinit|mode ds|hs|sdr50|sdr104|clock <kHz>|pads <speed> <dse> <fast>] - SD card status" },
    { "put", kcmd_put, "put <path> <size> <crc32> - upload (use 'crtos put')" },
    { "dt", kcmd_dt, "dt [node] [depth] - show the device tree" },
    { "dtload", kcmd_dtload, "dtload <dtb> [driver dir] - late device tree setup" },
    { "devices", kcmd_devices, "devices and their drivers" },
    { "drivers", kcmd_drivers, "registered drivers" },
    { "lsmod", kcmd_lsmod, "loaded modules" },
    { "insmod", kcmd_insmod, "insmod <name|path> - load a module" },
    { "rmmod", kcmd_rmmod, "rmmod <name> - unload a module" },
    { "crc32", kcmd_crc32, "crc32 <file> - checksum of a file" },
    { "sdbench", kcmd_sdbench, "sdbench [MB] [chunk KB] - SD card speed test (uses /sd/crtos/tmp)" },
    { "sdstress", kcmd_sdstress, "sdstress [reads] [blocks] [gap-us] - random reads over the whole card" },
    { "stage", kcmd_stage, "stage <size> - buffer for an upload through the debugger" },
    { "savestage", kcmd_savestage, "savestage <path> <size> <crc32> - save the staged upload" },
    { "i2cdetect", kcmd_i2cdetect, "i2cdetect [bus] - scan an I2C bus" },
    { "evtest", kcmd_evtest, "evtest [eventN] [seconds] - show input events" },
    { "fb", kcmd_fb, "fb [WxH] - the display mode; change to another mode of the panel (no program may use it)" },
    { "fbtest", kcmd_fbtest, "fbtest [fb] - test pattern, refresh rate, accelerated fill" },
    { "gpu2dtest", kcmd_gpu2dtest, "2D accelerator self test (RAM buffers)" },
};
const size_t kmon_os_ncmds = ARRAY_SIZE(kmon_os_cmds);
