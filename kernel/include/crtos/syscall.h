/*
 * crtos/syscall.h - system call ABI (shared by the kernel and user space).
 *
 * Calling convention: "svc #0" with the call number in R12 and up to six arguments in
 * R0-R5. The result comes back in R0 (R0:R1 for 64-bit results); values -4095..-1 are
 * error codes (-Exxx, crtos/errno.h). All other registers are preserved.
 *
 * Handles: every process has a table of handles to kernel objects (open files, IPC ports,
 * shared memory). File descriptors are handles; 0, 1 and 2 are stdin, stdout and stderr.
 * SYS_CLOSE closes any handle; the rest are closed when the process ends.
 *
 * Pointers passed to system calls must lie in the caller's memory (its arena or a mapped
 * shared memory window), otherwise the call fails with -EFAULT. Emulated memory (SYS_VMEM) is
 * accepted where the kernel copies the data (paths, argument vectors, results, read, write);
 * calls that keep or hand on the pointer itself (IPC, poll, ioctl, futexes, thread stacks)
 * refuse it with -EFAULT.
 */
#ifndef CRTOS_SYSCALL_H
#define CRTOS_SYSCALL_H

#include <stdint.h>

/* processes and threads */
#define SYS_EXIT            0   /* (int code) end the process */
#define SYS_THREAD_EXIT     1   /* (int code, uint32_t *done) end the calling thread; *done = 1 + woken */
#define SYS_WRITE           2   /* (int fd, const void *buf, uint32_t len) -> bytes */
#define SYS_SLEEP_MS        3   /* (uint32_t ms) */
#define SYS_YIELD           4
#define SYS_GETPID          5
#define SYS_GETTID          6
#define SYS_TIME_US         7   /* -> uint64_t microseconds since boot */
/* files */
#define SYS_READ            8   /* (fd, buf, len) -> bytes (0: end of file) */
#define SYS_OPEN            9   /* (path, flags) -> fd; flags as newlib's O_* */
#define SYS_CLOSE          10   /* (handle) */
#define SYS_LSEEK          11   /* (fd, off_lo, off_hi, whence) -> int64 new offset */
#define SYS_IOCTL          12   /* (fd, cmd, arg) */
#define SYS_FSTAT          13   /* (fd, struct crtos_stat *) */
#define SYS_STAT           14   /* (path, struct crtos_stat *) */
#define SYS_READDIR        15   /* (fd of a directory, struct crtos_dirent *) -> 1 entry, 0 end */
#define SYS_MKDIR          16   /* (path) */
#define SYS_UNLINK         17   /* (path) files and empty directories */
#define SYS_RENAME         18   /* (from, to) */
#define SYS_CHDIR          19   /* (path) */
#define SYS_GETCWD         20   /* (buf, size) -> length */
#define SYS_DUP            21   /* (handle) -> lowest free handle */
#define SYS_DUP2           22   /* (handle, new) -> new */
#define SYS_FSYNC          23   /* (fd) */
/* memory */
#define SYS_SBRK           24   /* (int32_t increment) -> previous end of the heap */
/* processes */
#define SYS_SPAWN          25   /* (const struct crtos_spawn *) -> pid; CAP_SPAWN */
#define SYS_WAIT           26   /* (pid or -1 for any child, int *status, timeout_ms) -> pid */
#define SYS_KILL           27   /* (pid, exit code); own children, others need CAP_KILL */
#define SYS_THREAD_CREATE  28   /* (entry, arg, stack, stack_size, prio) -> tid */
#define SYS_FUTEX_WAIT     29   /* (uint32_t *addr, expected, timeout_ms) -> 0, -EAGAIN, -ETIMEDOUT */
#define SYS_FUTEX_WAKE     30   /* (uint32_t *addr, count) -> tasks woken */
#define SYS_POLL           31   /* (struct crtos_pollfd *, n, timeout_ms) -> ready count */
/* IPC */
#define SYS_PORT_CREATE    32   /* (name or NULL, flags) -> handle for receiving */
#define SYS_PORT_CONNECT   33   /* (name, timeout_ms) -> handle for sending */
#define SYS_MSG_SEND       34   /* (port, data, len, handle to pass or -1, timeout_ms) */
#define SYS_MSG_CALL       35   /* (port, struct crtos_call *, timeout_ms) -> reply length */
#define SYS_MSG_RECV       36   /* (port, buf, max, struct crtos_msginfo *, timeout_ms) -> length */
#define SYS_MSG_REPLY      37   /* (token, data, len, handle to pass or -1) */
#define SYS_SHM_CREATE     38   /* (size, flags) -> handle */
#define SYS_SHM_MAP        39   /* (handle) -> address */
#define SYS_SHM_UNMAP      40   /* (address) */
/* system */
#define SYS_PROC_INFO      41   /* (index, struct crtos_procinfo *) -> 0, -ENOENT after the last */
#define SYS_TASK_INFO      42   /* (index, struct crtos_taskinfo *) -> 0, -ENOENT after the last */
#define SYS_SYS_INFO       43   /* (struct crtos_sysinfo *) */
#define SYS_MODULE_LOAD    44   /* (path) CAP_MODULE */
#define SYS_MODULE_UNLOAD  45   /* (name) CAP_MODULE */
#define SYS_REBOOT         46   /* () CAP_SYS */
#define SYS_PIPE           47   /* (int32_t fds[2], flags) fds[0] reads what fds[1] writes */
#define SYS_TIME_GET       48   /* -> int64_t microseconds since 1970 (wall clock) */
#define SYS_TIME_SET       49   /* (lo, hi) microseconds since 1970; CAP_SYS */
/* sockets (crtos/socket.h); a socket is a file handle */
#define SYS_SOCKET         50   /* (domain, type, protocol) -> handle */
#define SYS_BIND           51   /* (h, addr, len) */
#define SYS_CONNECT        52   /* (h, addr, len) */
#define SYS_LISTEN         53   /* (h, backlog) */
#define SYS_ACCEPT         54   /* (h, addr, *len) -> handle */
#define SYS_SENDTO         55   /* (h, buf, len, flags, addr, alen) -> bytes */
#define SYS_RECVFROM       56   /* (h, buf, len, flags, addr, *alen) -> bytes */
#define SYS_SHUTDOWN       57   /* (h, how) */
#define SYS_SETSOCKOPT     58   /* (h, level, name, val, len) */
#define SYS_GETSOCKOPT     59   /* (h, level, name, val, *len) */
#define SYS_GETSOCKNAME    60   /* (h, addr, *len) */
#define SYS_GETPEERNAME    61   /* (h, addr, *len) */
#define SYS_CACHE_SYNC     62   /* (addr, len): code written by the program runs (a JIT) */
#define SYS_STATFS         63   /* (path, struct crtos_statfs *): size and free space of its file system */
#define SYS_VMEM           64   /* (op, arg): emulated memory backed by the swap file, VMEM_* */
#define SYS_NR             65

/* SYS_VMEM operations. Emulated memory lies at CRTOS_VMEM_BASE and above, where no memory is:
 * every access of a program to it traps and the kernel carries it out on a copy of the page
 * in its page cache, which it fills from and writes back to the swap file on the SD card.
 * Much slower than memory (about a microsecond an access, a card read when the page is not
 * in the cache), but as large as the swap file. */
#define VMEM_MAP        0   /* (size) -> address of a new region, -ENOMEM; freed at exit */
#define VMEM_UNMAP      1   /* (address of a region) */
#define VMEM_INFO       2   /* (struct crtos_vmeminfo *) */
#define CRTOS_VMEM_BASE 0x90000000u
#define CRTOS_VMEM_SPAN 0x10000000u     /* addresses that may be emulated memory */

#define PIPE_TTY    0x1u    /* SYS_PIPE: answers the terminal ioctls (isatty, mode, foreground) */

/* capabilities of a process (inherited at most) */
#define CAP_SPAWN   0x01u   /* start processes */
#define CAP_KILL    0x02u   /* end processes it did not start */
#define CAP_MODULE  0x04u   /* load and unload kernel modules */
#define CAP_SYS     0x08u   /* reboot, console focus, priorities above SYS_PRIO_MAX_USER */
#define CAP_DEV     0x10u   /* open devices under /dev (other than console, null, zero, random) */
#define CAP_ALL     0x1Fu

#define SYS_PRIO_DEFAULT    10
#define SYS_PRIO_MAX_USER   19

#define CRTOS_S_IFMT    0170000u
#define CRTOS_S_IFDIR   0040000u
#define CRTOS_S_IFCHR   0020000u
#define CRTOS_S_IFREG   0100000u

struct crtos_stat {
    uint32_t mode;      /* CRTOS_S_IF* */
    uint32_t mtime;     /* FAT date << 16 | time, 0 if unknown */
    int64_t size;
};

struct crtos_statfs {
    uint64_t total, free;   /* bytes */
};

struct crtos_dirent {
    uint32_t mode;
    uint32_t reserved;
    int64_t size;
    char name[256];
};

struct crtos_spawn {
    const char *path;
    const char *const *argv;    /* NULL terminated, argv[0] is the program name */
    const char *const *envp;    /* NULL terminated, or NULL */
    int32_t stdio[3];           /* caller's handles that become 0, 1, 2; -1: the caller's 0, 1, 2 */
    uint32_t caps;              /* at most the caller's */
    int32_t prio;               /* 0: SYS_PRIO_DEFAULT */
    uint32_t flags;
};

/* passed to the program entry point _start() in R0; lives at the top of the arena */
#define CRTOS_STARTUP_MAGIC 0x53525443u
struct crtos_startup {
    uint32_t magic;
    int32_t argc;
    char **argv;
    char **envp;
    void (**init_array)(void);  /* constructors (.preinit_array then .init_array) */
    uint32_t init_count;
    void (**fini_array)(void);
    uint32_t fini_count;
    void *arena;
    uint32_t arena_size;
    int32_t pid;
};

/* program description in section ".crtos_app" (generated by the build) */
#define CRTOS_APP_MAGIC 0x50415243u
#define CRTOS_APP_ABI   1u
struct crtos_app_info {
    uint32_t magic;
    uint32_t abi;
    uint32_t stack_size;        /* main thread */
    uint32_t heap_size;         /* room for malloc() and extra thread stacks */
    uint32_t flags;
};

struct crtos_pollfd {
    int32_t fd;
    int16_t events;             /* POLL* from crtos/poll.h */
    int16_t revents;
};

#define PORT_NAME_MAX   24
#define MSG_MAX         512     /* bytes per message */

struct crtos_msginfo {
    int32_t pid;                /* sender */
    uint32_t len;               /* message length */
    uint32_t token;             /* != 0: the sender waits for SYS_MSG_REPLY(token) */
    int32_t handle;             /* handle that came with the message, or -1 */
};

struct crtos_call {
    const void *req;
    uint32_t req_len;
    int32_t req_handle;         /* passed to the server, or -1 */
    void *rep;
    uint32_t rep_max;
    int32_t rep_handle;         /* out: handle passed back with the reply, or -1 */
};

#define SHM_NOCACHE 0x1u        /* non-cacheable memory (DMA, display buffers) */

struct crtos_procinfo {
    int32_t pid, ppid;
    char name[16];
    uint8_t state;              /* 0 running, 1 ending, 2 ended (not waited for yet) */
    uint8_t nthreads;
    uint16_t nhandles;
    uint32_t caps;
    uint32_t arena_size;
    uint32_t heap_used;
    uint64_t cycles;            /* CPU time of its threads */
};

#define TASK_STATE_READY    0
#define TASK_STATE_BLOCKED  1
#define TASK_STATE_SUSP     2
#define TASK_STATE_DEAD     3
#define TASK_STATE_RUNNING  4

struct crtos_taskinfo {
    int32_t tid, pid;           /* pid 0: kernel thread */
    char name[16];
    uint8_t prio, base_prio, state, reserved;
    uint32_t kstack_used, kstack_size;
    uint32_t ustack_used, ustack_size;
    uint64_t cycles;
};

struct crtos_vmeminfo {
    uint32_t size;              /* emulated memory the system has (the swap file), 0: none */
    uint32_t free;              /* of it not given to a process */
    uint32_t mine;              /* the calling process' */
    uint32_t cache;             /* page cache (taken from SDRAM at the first VMEM_MAP) */
    uint64_t emulated;          /* accesses carried out (all processes, since boot) */
    uint32_t pageins, pageouts; /* pages read from and written to the card */
    uint32_t zerofills;         /* pages given out zeroed (never written before) */
    uint32_t reserved;
};

struct crtos_sysinfo {
    uint64_t uptime_us;
    uint64_t idle_cycles;       /* CPU time of the idle task */
    uint32_t cpu_hz;
    uint32_t nprocs, ntasks;
    uint32_t mem_total, mem_free;       /* SDRAM: processes and large buffers */
    uint32_t kmem_total, kmem_free;     /* kernel heaps in on-chip RAM */
    uint32_t dma_total, dma_free;       /* non-cacheable memory */
};

#if !defined(__ASSEMBLER__) && !defined(CRTOS_KERNEL) && !defined(CRTOS_MODULE)

static inline long __crtos_syscall6(long id, long a0, long a1, long a2, long a3, long a4, long a5)
{
    register long r0 __asm("r0") = a0;
    register long r1 __asm("r1") = a1;
    register long r2 __asm("r2") = a2;
    register long r3 __asm("r3") = a3;
    register long r4 __asm("r4") = a4;
    register long r5 __asm("r5") = a5;
    register long r12 __asm("r12") = id;
    __asm volatile("svc #0" : "+r"(r0), "+r"(r1) : "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r12) : "memory");
    return r0;
}

static inline long __crtos_syscall(long id, long a0, long a1, long a2, long a3)
{
    register long r0 __asm("r0") = a0;
    register long r1 __asm("r1") = a1;
    register long r2 __asm("r2") = a2;
    register long r3 __asm("r3") = a3;
    register long r12 __asm("r12") = id;
    __asm volatile("svc #0" : "+r"(r0), "+r"(r1) : "r"(r2), "r"(r3), "r"(r12) : "memory");
    return r0;
}

static inline int64_t __crtos_syscall64(long id, long a0, long a1, long a2, long a3)
{
    register long r0 __asm("r0") = a0;
    register long r1 __asm("r1") = a1;
    register long r2 __asm("r2") = a2;
    register long r3 __asm("r3") = a3;
    register long r12 __asm("r12") = id;
    __asm volatile("svc #0" : "+r"(r0), "+r"(r1) : "r"(r2), "r"(r3), "r"(r12) : "memory");
    return (int64_t)(((uint64_t)(uint32_t)r1 << 32) | (uint32_t)r0);
}

#endif

#endif
