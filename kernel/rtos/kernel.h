/*
 * kernel/rtos/kernel.h - kernel-internal definitions (tasks, processes, scheduler internals).
 * Not part of the module API.
 */
#ifndef KERNEL_KERNEL_H
#define KERNEL_KERNEL_H

#include <stdint.h>
#include <stddef.h>
#include <crtos/config.h>
#include <crtos/arch.h>
#include <crtos/errno.h>
#include <crtos/list.h>
#include <crtos/printk.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include <crtos/mm.h>
#include <crtos/poll.h>
#include <crtos/vfs.h>

#ifdef __cplusplus
extern "C" {
#endif

struct proc;
struct module;
struct port;
struct shm;
struct crtos_pollfd;

/* Hot paths executed from ITCM (copied there by the startup code) */
#define KERNEL_FAST __attribute__((section(".ramfunc.$SRAM_ITC")))

/* Limit of the running task's active stack (used by PendSV before saving context) */
extern volatile uint32_t task_stack_limit;

enum task_state : uint8_t {
    TASK_READY = 0,     /* in a ready queue (includes the running task) */
    TASK_BLOCKED,       /* on a wait queue and/or the sleep queue */
    TASK_SUSPENDED,
    TASK_DEAD,          /* exited, waiting to be reaped */
};

/* task->flags */
#define TF_NOSAVE     0x01u  /* PendSV must not save context (task is gone) */
#define TF_KTHREAD    0x02u  /* kernel thread: always privileged */
#define TF_USER       0x04u  /* user thread of a process */
#define TF_IN_SYSCALL 0x08u  /* user thread currently executing a syscall in kernel mode */
#define TF_KILLED     0x10u  /* termination requested */
#define TF_SLEEPQ     0x20u  /* linked on the sleep queue */
#define TF_REDIRECT   0x40u  /* context is unusable: restart in task_exit() at the next switch-out */
#define TF_PAGEIN     0x80u  /* in the kernel for a page of emulated memory (vmem.cpp), not a syscall */

struct task {
    /* --- offsets used by assembly: keep first --- */
    uint32_t *sp;               /* 0: saved PSP */
    uint32_t flags;             /* 4: TF_* */
    /* --------------------------------------------- */
    uint8_t prio;               /* effective priority (may be boosted by PI) */
    uint8_t base_prio;
    uint8_t state;
    uint8_t slice;
    int id;

    struct list_head rq_node;   /* ready queue or wait queue */
    struct list_head sl_node;   /* sleep (timeout) queue */
    struct list_head all_node;  /* all tasks */
    struct list_head proc_node; /* threads of the owning process */
    uint32_t wake_tick;
    struct wait_queue *waiting_on;
    struct mutex *blocked_on_mutex;
    int32_t wait_result;
    struct list_head mutexes_held;

    /* Stacks. Kernel threads only use kstack; user threads run user code on ustack
     * (inside the process arena) and syscalls on kstack. */
    uint8_t *kstack;
    uint32_t kstack_size;
    uint8_t *ustack;
    uint32_t ustack_size;
    uint32_t *usp;              /* user frame saved while in a syscall */
    uint32_t uexc_return;
    uint32_t sc_id, sc_a4, sc_a5;
    uint32_t fp_save[16];       /* user S16-S31 while in a syscall (FPU tasks only) */
    uint32_t stack_limit;       /* lowest usable address of the active stack */

    /* MPU guard regions: region 14 below the stack of the mode it starts in (user stack of
     * user threads), region 15 below the kernel stack of a user thread (off for kernel
     * threads) - both stay on while it runs, so system calls switch no MPU region */
    uint32_t guard_rbar, guard_rasr;
    uint32_t kguard_rbar, kguard_rasr;

    struct proc *proc;
    task_fn_t entry;
    void *arg;
    int32_t exit_code;

    uintptr_t futex_key;        /* address waited on in SYS_FUTEX_WAIT */
    uintptr_t excl_addr;        /* LDREX emulated on emulated memory (arch/emulate.cpp) */
    uint32_t excl_switch;       /* ... and nswitch then: a STREX after a switch fails */
    /* console of this thread (the kernel monitor on the debug probe); NULL: the UART */
    void (*con_write)(const char *s, size_t n);
    int (*con_getc)(uint32_t timeout);
    volatile uint8_t poll_armed;    /* sleeping in poll(): poll_notify() may wake it */
    volatile uint8_t poll_hit;      /* an object it polls may have changed */

    /* statistics */
    uint64_t cycles;
    uint64_t cycles_snap;       /* for CPU load reports */
    uint32_t switch_in;
    uint32_t nswitch;
    char name[16];
    int net_errno;              /* errno slot of the network stack's socket layer */
};

static_assert(offsetof(struct task, sp) == 0, "asm offset");
static_assert(offsetof(struct task, flags) == 4, "asm offset");

/* Handles: per-process references to kernel objects (file descriptors are handles) */
enum handle_type : uint8_t { H_FREE = 0, H_FILE, H_PORT, H_SHM };
#define HR_RECV 0x01u   /* port: the receiving end */

struct handle {
    uint8_t type;
    uint8_t rights;
    uint16_t reserved;
    void *obj;
};

/* Shared memory mapped into a process: MPU regions 9-11 while its threads run. A large object
 * takes several of them (one region each for the pieces of its cover). A process with a fast
 * code block (app.cpp) has it in one of them, shm NULL and the region enabled. */
#define SHM_WINDOWS 3
struct shm_window {
    struct shm *shm;
    uint32_t rbar, rasr;
};

/* Process: an isolated user program (arena + threads + handles) */
enum proc_state : uint8_t { PROC_ALIVE = 0, PROC_EXITING, PROC_DEAD };

struct proc {
    int pid;
    uint8_t state;
    uint8_t released;           /* arena and handles freed */
    uint32_t caps;
    char name[16];

    uint8_t *arena;             /* size-aligned MPU region (power of two, maybe with subregions) */
    uint32_t arena_size;
    uint32_t arena_rbar, arena_rasr;
    uint32_t brk;               /* current heap end (offset into arena) */
    uint32_t heap_start;        /* offset where the heap begins */
    uint32_t stack_floor;       /* lowest offset used by the stacks carved from the top */

    struct list_head threads;
    int32_t nthreads;
    uint64_t cycles_dead;       /* CPU time of its threads that ended (irq locked) */
    struct list_head all_node;
    int32_t exit_code;
    uint32_t refs;              /* own (until released), parent link, kernel users */
    struct wait_queue exit_wq;

    struct proc *parent;        /* NULL: started by the kernel, or the parent ended */
    struct list_head children;
    struct list_head sibling;
    struct wait_queue child_wq; /* a child ended */

    struct handle *htab;        /* CONFIG_MAX_HANDLES entries */
    struct shm_window win[SHM_WINDOWS];
    uint8_t *fastcode;          /* its ".fast" code in on-chip memory (app.cpp), or NULL */
    uint32_t fastcode_size;
    /* a program that runs in place (XIP, app.cpp): r9 of its threads (its GOT, 0: none),
     * where its code is, and its file, kept open so that the code stays there */
    uint32_t sb;
    uint32_t text, text_size;   /* in place: its constants there may be read by system calls */
    struct file *xip_file;
    struct vmem_region *vmem;   /* its emulated memory (vmem.cpp) */
    int default_prio;
    char cwd[VFS_PATH_MAX];
};

/* ---- scheduler internals (sched.cpp) ---- */
extern task_t *volatile g_current;
extern volatile uint32_t g_ticks;

void sched_init(void);
void sched_start(void) __attribute__((noreturn));
void sched_tick(void);
void sched_ready(task_t *t);                 /* irq locked: make runnable */
int sched_block(struct wait_queue *wq, uint32_t timeout, uint32_t key);
void sched_wake(task_t *t, int result);      /* irq locked */
void sched_pend_switch(void);
void sched_set_prio(task_t *t, int prio);    /* irq locked: change effective prio */
task_t *task_create_raw(const char *name, int prio, uint32_t flags);
void task_start_kernel(task_t *t, task_fn_t fn, void *arg);
/* Initial context (see PendSV): returns the saved stack pointer */
uint32_t *task_build_frame(uint8_t *stack_top, uint32_t pc, uint32_t r0, uint32_t control, uint32_t lr);
#define STACK_FILL 0xDEADBEEFu
void task_fill_stack(uint8_t *stack, uint32_t size);
uint32_t stack_unused(const uint8_t *base, uint32_t size); /* bytes never touched */
void task_redirect_to_exit(task_t *t, int code);  /* rebuild context to run task_exit */
void task_foreach(void (*fn)(task_t *t, void *ctx), void *ctx);
void sched_reap(void);
int sched_is_locked(void);
task_t *sched_idle_task(void);

/* Ticks the timer interrupt could not take in time (interrupts masked or a long handler for
 * more than a period): counted afterwards from the cycle counter */
struct tick_stats {
    uint32_t late;              /* ticks that came a period or more late */
    uint32_t lost;              /* periods counted afterwards */
    uint32_t max_gap_cycles;    /* the longest time without a tick */
    uint32_t max_gap_at;        /* ... ended at this tick */
    char max_gap_task[16];      /* ... in this task */
};
void sched_tick_stats(struct tick_stats *out);
task_t *task_find(int id);                    /* call with sched_lock() held */

/* wait queue helpers used by sync.cpp and sched.cpp */
void wq_insert(struct wait_queue *wq, task_t *t);
void mutex_waiter_changed(struct mutex *m);  /* re-evaluate priority inheritance */
void mutex_release_all(task_t *t);

/* ---- MPU (arch/mpu.cpp) ---- */
void mpu_init(void);
void mpu_switch(task_t *prev, task_t *next);
void mpu_set_guard(task_t *t, const void *stack_base);   /* one stack (kernel threads) */
void mpu_set_user_guards(task_t *t);                     /* user and kernel stack */
/* Encode a region for slot @region covering [base, base + size) (sizes below 32 count as 32):
 * a power of two aligned to its size, or a run of eighths (subregions) of a larger aligned
 * one. Returns 0, or -EINVAL if no MPU region can express it. */
int mpu_encode_region(int region, uintptr_t base, uint32_t size, uint32_t attr, uint32_t *rbar, uint32_t *rasr);
#define MPU_ATTR_USER_RWX  1u   /* normal cached memory, full access, executable */
#define MPU_ATTR_USER_RW   2u   /* normal cached memory, full access, XN */
#define MPU_ATTR_USER_RW_NC 3u  /* non-cacheable memory, full access, XN */
#define MPU_ATTR_USER_RX   4u   /* normal memory, user read and execute, kernel read/write */
void mpu_forget_proc(struct proc *p);         /* proc is being freed */
void mpu_proc_changed(struct proc *p);        /* its arena/window regions changed */
void mpu_empty_region(int region, uint32_t *rbar, uint32_t *rasr);
void mpu_dump(void);
#define MPU_REGION_ARENA   8
#define MPU_REGION_SHM0    9
#define MPU_REGION_NULL    12
#define MPU_REGION_MSP     13
#define MPU_REGION_GUARD   14
#define MPU_REGION_KGUARD  15   /* kernel stack of a user thread */

/* ---- processes (proc.cpp) ---- */
/* New process with an arena of at least @arena_size bytes (zeroed), child of @parent (may be
 * NULL). Returns with one reference for the caller besides the process' own. */
struct proc *proc_create(const char *name, uint32_t arena_size, struct proc *parent);
/* Thread with a stack carved from the top of the arena */
task_t *proc_thread_create(struct proc *p, uint32_t entry, uint32_t arg, uint32_t ustack_size, int prio);
/* Thread on a stack the program provides (inside its arena, guard-aligned) */
task_t *proc_thread_create_at(struct proc *p, uint32_t entry, uint32_t arg, uint8_t *stack, uint32_t size, int prio);
void proc_kill(struct proc *p, int code);
void proc_thread_exited(task_t *t, int code);
void proc_get(struct proc *p);
void proc_put(struct proc *p);
void proc_detach(struct proc *p);
int proc_wait(struct proc *p, uint32_t timeout, int *code);       /* 0, -ETIMEDOUT, -EINTR */
int proc_wait_child(struct proc *p, int pid, uint32_t timeout, int *code); /* reaps: pid or -errno */
struct proc *proc_find(int pid);                                  /* call with sched_lock() held */
struct proc *proc_get_by_pid(int pid);                            /* with a reference, or NULL */
void proc_foreach(void (*fn)(struct proc *p, void *ctx), void *ctx);
int proc_count(void);

/* User memory: the caller's arena and mapped shared memory (kernel threads: anything) */
int uaccess_ok(const void *ptr, size_t len, int write);
int copy_from_user(void *dst, const void *src, size_t len);       /* 0 or -EFAULT */
int copy_to_user(void *dst, const void *src, size_t len);
int strncpy_from_user(char *dst, const char *src, size_t size);   /* length, -EFAULT, -ENAMETOOLONG */
int capable(uint32_t caps);                                       /* crtos/uaccess.h */

/* ---- handles (handle.cpp) ---- */
/* Install @obj (the caller's reference moves to the table) at the lowest free slot >= @min */
int handle_install(struct proc *p, uint8_t type, uint8_t rights, void *obj, int min);
/* Install at slot @h, closing what was there (dup2) */
int handle_install_at(struct proc *p, int h, uint8_t type, uint8_t rights, void *obj);
/* Object of handle @h with a new reference (release with obj_put), NULL if not of @type
 * (H_FREE: any type; *type and *rights receive the actual ones) */
void *handle_ref(struct proc *p, int h, uint8_t *type, uint8_t *rights);
int handle_close(struct proc *p, int h);
void handle_close_all(struct proc *p);
int handle_count(struct proc *p);
void obj_get(uint8_t type, void *obj);
void obj_put(uint8_t type, void *obj);
int obj_poll(uint8_t type, uint8_t rights, void *obj, struct poll_entry *e);

/* ---- IPC ports (ipc.cpp) and shared memory (shm.cpp) ---- */
void port_get(struct port *pt);
void port_put(struct port *pt);
void port_close_recv(struct port *pt);        /* the receiving handle was closed */
int port_poll(struct port *pt, uint8_t rights, struct poll_entry *e);
void shm_get(struct shm *s);
void shm_put(struct shm *s);
struct shm *shm_alloc(uint32_t size, uint32_t flags);
struct shm *shm_wrap(void *base, uint32_t size, bool nocache);    /* driver memory */
void shm_revoke(struct shm *s);                                     /* the memory is gone */
bool shm_unshared(struct shm *s);                                   /* only its creator holds it */
struct shm *shm_lookup(int h, uint8_t **base, uint32_t *size, bool *cached);
size_t shm_window_span(struct proc *p, uintptr_t addr);  /* bytes of a mapped object from addr on */
void shm_unmap_all(struct proc *p);

/* ---- programs (app.cpp) ---- */
/* Load an .app and start it. @argv/@envp are kernel strings; @stdio[i] (may be NULL) become
 * handles 0-2. Returns the new process with a reference, or NULL (*err). */
struct proc *app_spawn(struct proc *parent, const char *path, const char *const *argv, const char *const *envp,
                       struct file *const stdio[3], uint32_t caps, int prio, int *err);

/* ---- poll (poll.cpp) and the console terminal (tty.cpp) ---- */
int64_t sys_poll(struct crtos_pollfd *ufds, uint32_t n, uint32_t timeout);
void tty_init(void);                          /* /dev/console, /dev/null, /dev/zero */
void uevent_init(void);                       /* /dev/uevent */
void gpu2d_init(void);                        /* /dev/gpu2d (CPU fallback until a driver comes) */
void uevent_emit(const char *action, const char *name);
void tty_rx_notify(void);                     /* console input arrived (ISR) */
struct file *tty_open_console(void);          /* for processes started by the kernel */
void console_set_focus(int who);              /* CON_KMON or CON_TTY */
int console_focus(void);
#define CON_KMON    0
#define CON_TTY     1
#define CON_BINARY  1u                        /* console_read_as flag: no escape keys */
int console_read_as(int who, void *buf, size_t max, uint32_t timeout, uint32_t flags);
int console_rx_pending(void);
void log_write_wait(const char *s, size_t len);   /* log_write() that waits for room */
void con_write(const char *s, size_t n);          /* the calling thread's console (cprintf) */
int con_getc(uint32_t timeout);
void swdcon_start(void);                          /* kmon over the debug probe */
void kmon_run(void *arg);                         /* a kernel monitor on the thread's console */
const char *kmon_console_path(void);              /* stdio for programs a monitor starts */

/* ---- syscalls ---- */
int64_t syscall_dispatch(uint32_t id, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);

/* ---- misc ---- */
void irq_init(void);
void console_init(void);
void console_kick(void);
void console_panic_write(const char *s, size_t len);
int console_panic_getc(void);                 /* polled, -1 if nothing */
int console_getc(uint32_t timeout);
int console_read(void *buf, size_t max, uint32_t timeout);  /* >= 1 byte or -ETIMEDOUT */
size_t log_console_read_locked(char *dst, size_t max); /* irq locked */
void log_panic_flush(void);
void log_panic_write(const char *s, size_t len); /* console + record kept over the reboot */
void log_panic_previous(void);               /* boot: print the record of the last panic */
size_t log_snapshot(char *dst, size_t max);   /* the end of the kernel log */
uint32_t log_lost_bytes(void);
void console_stats(uint32_t *rx_overrun, uint32_t *rx_dropped);
void timers_start(void);                      /* the thread of software timers (timer.cpp) */
void timer_tick(uint32_t now);                /* tick interrupt: wake it when one is due */
void mm_init(void);
void kmon_start(void);
void kworker_init(void);
void kworker_queue(void (*fn)(void *), void *arg);
void kernel_tests(const char *name);
void fault_init(void);

/* ---- emulated memory (vmem.cpp) and the load/store emulation (arch/emulate.cpp) ---- */
#define EMU_PAGE 4096u
enum { EMU_DONE = 0, EMU_MISS = 1, EMU_BAD = 2 };
struct emu {
    uint32_t *frame;            /* exception frame: R0-R3, R12, LR, PC, xPSR (+ S0-S15, FPSCR) */
    uint32_t *regs;             /* R4-R11 */
    bool fpframe;               /* the frame has the FP registers (EXC_RETURN bit 4 clear) */
    bool branched;              /* the instruction loaded the PC */
    uintptr_t miss;             /* EMU_MISS: an address of the page that is missing */
    /* pointer to the byte at @a of a page that is present; EMU_MISS, or EMU_BAD for memory
     * this is not */
    int (*page)(void *ctx, uintptr_t a, bool write, uint8_t **p);
    void *ctx;
};
int emulate_access(struct emu *e);

struct vmem_region;
bool vmem_fault(task_t *t, uint32_t *frame, uint32_t *regs, uint32_t exc_return, uintptr_t addr, uint64_t *ret);
void vmem_pagein_call(uintptr_t addr);
bool vmem_contains(struct proc *p, const void *addr, size_t len);
int vmem_copy_in(void *dst, const void *src, size_t len);
int vmem_copy_out(void *dst, const void *src, size_t len);
int vmem_strncpy_in(char *dst, const char *src, size_t size);
void vmem_proc_release(struct proc *p);
int64_t sys_vmem(uint32_t op, uint32_t arg);
void vmem_show(int (*out)(const char *fmt, ...));
/* context.cpp: a user thread's fault continues as the internal "system call" @id */
uint32_t syscall_enter_fault(uint32_t *frame, uint32_t exc_return, uint32_t id, uint32_t arg);
#define SC_PAGEIN 0x10000u

/* ---- M2: storage, device tree, driver model, modules ---- */
void vfs_init(void);
int fat_mount(const char *mountpoint);
const char *fat_type_name(void);
uint32_t fat_cluster_size(void);
void fat_cache_invalidate(void);
int fat_swap_create(const char *rel, uint32_t size, uint32_t *lba);
int fat_raw_io(uint32_t lba, void *buf, uint32_t count, bool write);
int device_populate(void);
void driver_unregister_owner(struct module *owner);
struct module *module_loading(void);
/* A reference on the module whose memory holds @addr (*out = NULL: the kernel's): it cannot
 * be unloaded until module_put(). -ENODEV: it is being unloaded. Open files hold one on the
 * module of their file_ops. */
int module_get_addr(const void *addr, struct module **out);
void module_put(struct module *m);
const void *ksym_kernel_lookup(const char *name);
unsigned ksym_kernel_count(void);
const char *module_addr_lookup(uintptr_t addr, uint32_t *offset);
void module_foreach(void (*fn)(const char *name, const char *desc, void *base, uint32_t size, uint32_t refs, void *ctx),
                    void *ctx);
void boot_start(void);                        /* storage, device tree and drivers (thread) */
int boot_setup_devices(const char *dtb, const char *driver_dir);
int ramfs_mount(const char *mountpoint);
#define CRTOS_ROOT        "/sd/crtos"
#define CRTOS_DTB_PATH    CRTOS_ROOT "/boot/board.dtb"
#define CRTOS_DRIVER_DIR  CRTOS_ROOT "/drivers"
#define CRTOS_INIT_PATH   CRTOS_ROOT "/sbin/init.app"
/* the environment of the programs the kernel starts (init, kmon run): programs on the flash
 * file system first (they run in place), temporary files on the RAM disk */
#define CRTOS_PROGRAM_ENV "PATH=/flash0/bin:" CRTOS_ROOT "/bin:" CRTOS_ROOT "/apps", "HOME=" CRTOS_ROOT, "TMPDIR=/ram"

extern uint32_t __stack_guard_base; /* MSP stack bottom */

#ifdef __cplusplus
}
#endif

#endif
