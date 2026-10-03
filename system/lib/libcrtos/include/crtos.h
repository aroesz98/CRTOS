/*
 * crtos.h - CRTOS system interface for programs (libcrtos).
 *
 * Programs use the C library (newlib) for files, memory and time; this header adds what
 * is specific to CRTOS: processes, threads, futex-based locks, IPC ports, shared memory and
 * system information. Functions return -1 and set errno on failure (like POSIX) unless
 * noted otherwise. Timeouts are in milliseconds, CRTOS_FOREVER waits without limit.
 */
#ifndef CRTOS_H
#define CRTOS_H

#include <stddef.h>
#include <stdint.h>
#include <crtos/syscall.h>
#include <crtos/poll.h>
#include <crtos/ioctl.h>
#include <crtos/tty.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CRTOS_FOREVER 0xFFFFFFFFu

/* startup information of this program (arena, arguments) */
extern struct crtos_startup *__crtos_startup;

/* The program header: the main thread's stack and the heap in bytes, which the kernel reads
 * before it starts the program (it makes the arena that large). Write it once, in one source
 * file, outside any function:
 *     CRTOS_APP(32 * 1024, 1024 * 1024);
 * Without it a program gets 16 KB of stack and 64 KB of heap (libcrtos' default). */
#define CRTOS_APP(stack, heap)                                                                   \
    CRTOS_APP_LINKAGE const struct crtos_app_info __crtos_app_info;                             \
    __attribute__((used, section(".crtos_app"))) const struct crtos_app_info __crtos_app_info = { \
        CRTOS_APP_MAGIC, CRTOS_APP_ABI, (uint32_t)(stack), (uint32_t)(heap), 0u                   \
    }
#ifdef __cplusplus
#define CRTOS_APP_LINKAGE extern "C"
#else
#define CRTOS_APP_LINKAGE extern
#endif

/* libcrtosheap (-lcrtosheap): a program's own defaults for what the environment may set
 * (CRTOS_HEAP_WINDOWS, CRTOS_HEAP_WINDOW_KB, CRTOS_HEAP_SWAP, which still decide where they
 * are set). Define only those the program wants, once, outside any function:
 *     const int crtos_heap_windows = 0;      MPU windows the heap may take, 0-3 (default 3)
 *     const int crtos_heap_window_kb = 8192; the largest window, KB (default: all it can get)
 *     const int crtos_heap_swap = 0;         MB of emulated memory at most (default: all of it)
 * A program with windows leaves most MPU windows to their surfaces; a window the kernel can
 * place in one MPU region (it tries that first) takes one of them. */
extern const int crtos_heap_windows;
extern const int crtos_heap_window_kb;
extern const int crtos_heap_swap;

/* ---- processes ---------------------------------------------------------------------------- */

/* Start a program. stdio[] of -1 passes the caller's 0/1/2; caps are limited to ours. */
int crtos_spawn(const struct crtos_spawn *sp);
/* Simple form: same stdin/out/err, no capabilities, default priority */
int crtos_spawnv(const char *path, char *const argv[]);
/* Wait for a child (pid, or -1 for any): its pid, the exit code in *status */
int crtos_wait(int pid, int *status, uint32_t timeout);
int crtos_kill(int pid, int code);
int crtos_gettid(void);
uint64_t crtos_time_us(void);               /* since boot */
int64_t crtos_wall_us(void);                /* since 1970 (wall clock) */
int crtos_pipe(int fds[2], uint32_t flags); /* PIPE_TTY: behaves as a terminal */
void crtos_sleep_ms(uint32_t ms);
void crtos_yield(void);

/* ---- threads and locks -------------------------------------------------------------------- */

typedef struct crtos_thread crtos_thread_t;
/* stack_size 0: 8 KB; prio 0: the process default */
crtos_thread_t *crtos_thread_start(void *(*fn)(void *), void *arg, size_t stack_size, int prio);
void *crtos_thread_join(crtos_thread_t *t);
int crtos_thread_id(const crtos_thread_t *t);
int crtos_thread_done(const crtos_thread_t *t);     /* 1 once it has ended (join does not wait) */

int crtos_futex_wait(volatile uint32_t *addr, uint32_t expected, uint32_t timeout);   /* 0 or -1 */
int crtos_futex_wake(volatile uint32_t *addr, uint32_t count);                         /* woken */

/* Recursive mutex; a zeroed one is unlocked */
typedef struct {
    volatile uint32_t state;    /* 0 free, 1 locked, 2 locked with waiters */
    volatile int owner;
    int count;
} crtos_mutex_t;
#define CRTOS_MUTEX_INIT { 0, 0, 0 }
void crtos_mutex_lock(crtos_mutex_t *m);
int crtos_mutex_trylock(crtos_mutex_t *m);                  /* 1 if locked */
void crtos_mutex_unlock(crtos_mutex_t *m);

/* ---- IPC ---------------------------------------------------------------------------------- */

int crtos_port_create(const char *name);                    /* NULL: anonymous (pass it on) */
int crtos_port_connect(const char *name, uint32_t timeout);
int crtos_msg_send(int port, const void *data, size_t len, int handle, uint32_t timeout);
/* Request/reply: length of the reply */
int crtos_msg_call(int port, const void *req, size_t req_len, void *rep, size_t rep_max, uint32_t timeout);
int crtos_msg_call2(int port, struct crtos_call *c, uint32_t timeout);
int crtos_msg_recv(int port, void *buf, size_t max, struct crtos_msginfo *info, uint32_t timeout);
int crtos_msg_reply(uint32_t token, const void *data, size_t len, int handle);

/* ---- shared memory ------------------------------------------------------------------------ */

int crtos_shm_create(size_t size, uint32_t flags);          /* SHM_NOCACHE */
void *crtos_shm_map(int handle);                             /* NULL on failure */
int crtos_shm_unmap(void *addr);

/* ---- emulated memory ------------------------------------------------------------------------ */
/* Memory backed by the swap file on the SD card (crtos/syscall.h: SYS_VMEM): every access is
 * carried out by the kernel (about a microsecond; a card read when its page is not cached).
 * The kernel copies data there for system calls that take a path or a buffer (open, read,
 * write, stat...), not for IPC, poll, ioctl, futexes or thread stacks. Freed at exit. */
void *crtos_vmem_map(size_t size);                           /* NULL on failure */
int crtos_vmem_unmap(void *addr);
int crtos_vmem_info(struct crtos_vmeminfo *vi);
#define CRTOS_IN_VMEM(p) ((uintptr_t)(p) - CRTOS_VMEM_BASE < CRTOS_VMEM_SPAN)

/* ---- system -------------------------------------------------------------------------------- */

int crtos_proc_info(int index, struct crtos_procinfo *pi);   /* -1/ENOENT after the last */
int crtos_task_info(int index, struct crtos_taskinfo *ti);
int crtos_sys_info(struct crtos_sysinfo *si);
/* size and free space of the file system of @path, in bytes (statvfs() in sys/statvfs.h) */
int crtos_statfs(const char *path, uint64_t *total, uint64_t *free);
/* Code the program wrote into its memory becomes runnable (cache maintenance for a JIT) */
int crtos_cache_sync(const void *addr, size_t len);
int crtos_module_load(const char *path);
int crtos_module_unload(const char *name);
int crtos_reboot(void);

/* raw system call with errno handling: the result or -1 */
long crtos_sys(long id, long a0, long a1, long a2, long a3);

/* device control (crtos/ioctl.h encoding) */
int ioctl(int fd, unsigned long cmd, ...);

#ifdef __cplusplus
}
#endif

#endif
