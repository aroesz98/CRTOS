/*
 * kernel/os/syscall.cpp - system call dispatch (ABI in crtos/syscall.h).
 * Runs in the calling thread, privileged, on its kernel stack; may block.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include <crtos/rtc.h>
#include <crtos/syscall.h>
#include "fsl_device_registers.h"

/* sys_fs.cpp */
int64_t sys_open(const char *path, uint32_t flags);
int64_t sys_close(int h);
int64_t sys_read(int fd, void *buf, uint32_t len);
int64_t sys_write(int fd, const void *buf, uint32_t len);
int64_t sys_lseek(int fd, uint32_t lo, uint32_t hi, int whence);
int64_t sys_ioctl(int fd, uint32_t cmd, uintptr_t arg);
int64_t sys_fstat(int fd, struct crtos_stat *st);
int64_t sys_stat(const char *path, struct crtos_stat *st);
int64_t sys_statfs(const char *path, struct crtos_statfs *sf);
int64_t sys_readdir(int fd, struct crtos_dirent *de);
int64_t sys_mkdir(const char *path);
int64_t sys_unlink(const char *path);
int64_t sys_rename(const char *from, const char *to);
int64_t sys_chdir(const char *path);
int64_t sys_getcwd(char *buf, uint32_t size);
int64_t sys_dup(int h);
int64_t sys_dup2(int h, int newh);
int64_t sys_fsync(int fd);
/* sys_proc.cpp */
int64_t sys_sbrk(int32_t incr);
int64_t sys_spawn(const struct crtos_spawn *sp);
int64_t sys_wait(int pid, int32_t *status, uint32_t timeout);
int64_t sys_kill(int pid, int code);
int64_t sys_thread_create(uint32_t entry, uint32_t arg, uintptr_t stack, uint32_t size, int prio);
int64_t sys_futex_wait(volatile uint32_t *addr, uint32_t expected, uint32_t timeout);
int64_t sys_futex_wake(volatile uint32_t *addr, uint32_t count);
int64_t sys_proc_info(int index, struct crtos_procinfo *pi);
int64_t sys_task_info(int index, struct crtos_taskinfo *ti);
int64_t sys_sys_info(struct crtos_sysinfo *si);
int64_t sys_module_load(const char *path);
int64_t sys_module_unload(const char *name);
int64_t sys_reboot(void);
/* ipc.cpp, shm.cpp */
int64_t sys_port_create(const char *name, uint32_t flags);
int64_t sys_port_connect(const char *name, uint32_t timeout);
int64_t sys_msg_send(int h, const void *data, uint32_t len, int xh, uint32_t timeout);
int64_t sys_msg_call(int h, struct crtos_call *c, uint32_t timeout);
int64_t sys_msg_recv(int h, void *buf, uint32_t max, struct crtos_msginfo *info, uint32_t timeout);
int64_t sys_msg_reply(uint32_t token, const void *data, uint32_t len, int xh);
int64_t sys_shm_create(uint32_t size, uint32_t flags);
int64_t sys_shm_map(int h);
int64_t sys_shm_unmap(uintptr_t addr);
int64_t sys_pipe(int32_t *fds, uint32_t flags);
/* subsys/net.cpp */
int64_t sys_socket(int domain, int type, int protocol);
int64_t sys_bind(int h, const void *uaddr, uint32_t len);
int64_t sys_connect(int h, const void *uaddr, uint32_t len);
int64_t sys_listen(int h, int backlog);
int64_t sys_accept(int h, void *uaddr, uint32_t *ulen);
int64_t sys_sendto(int h, const void *buf, uint32_t len, int flags, const void *uaddr, uint32_t alen);
int64_t sys_recvfrom(int h, void *buf, uint32_t len, int flags, void *uaddr, uint32_t *ulen);
int64_t sys_shutdown(int h, int how);
int64_t sys_setsockopt(int h, int level, int name, const void *uval, uint32_t len);
int64_t sys_getsockopt(int h, int level, int name, void *uval, uint32_t *ulen);
int64_t sys_getsockname(int h, void *uaddr, uint32_t *ulen);
int64_t sys_getpeername(int h, void *uaddr, uint32_t *ulen);

/* wall clock: boot time + offset set by whoever knows the date (settings, network) or taken
 * from a real-time clock driver (crtos/rtc.h) */
static int64_t s_rt_offset_us;
static const struct rtc_ops *s_rtc;
static void *s_rtc_ctx;
static struct mutex s_rtc_lock = MUTEX_INIT(s_rtc_lock);

#define RTC_VALID_US 1577836800000000LL /* 2020-01-01: anything earlier was never set */

int rtc_register(const struct rtc_ops *ops, void *ctx, const char *name)
{
    mutex_lock(&s_rtc_lock, WAIT_FOREVER);
    if (s_rtc) {
        mutex_unlock(&s_rtc_lock);
        return -EBUSY;
    }
    s_rtc = ops;
    s_rtc_ctx = ctx;
    int64_t t = 0;
    if (ops->read(ctx, &t) == 0 && t >= RTC_VALID_US) {
        s_rt_offset_us = t - (int64_t)time_us();
        uint32_t s = (uint32_t)(t / 1000000);
        printk("rtc: %s: the date comes from it (%lu days, %02lu:%02lu:%02lu UTC)\n", name,
               (unsigned long)(s / 86400u), (unsigned long)(s / 3600u % 24u), (unsigned long)(s / 60u % 60u),
               (unsigned long)(s % 60u));
    } else {
        printk("rtc: %s: not set yet\n", name);
    }
    mutex_unlock(&s_rtc_lock);
    return 0;
}

void rtc_unregister(void *ctx)
{
    mutex_lock(&s_rtc_lock, WAIT_FOREVER);
    if (s_rtc_ctx == ctx) {
        s_rtc = nullptr;
        s_rtc_ctx = nullptr;
    }
    mutex_unlock(&s_rtc_lock);
}

int64_t wall_time_us(void)
{
    return (int64_t)time_us() + s_rt_offset_us;
}

uint32_t fat_time_now(void)
{
    int64_t t = wall_time_us();
    if (t < RTC_VALID_US)
        return (41u << 25) | (1u << 21) | (1u << 16); /* 2021-01-01: FatFs's old fixed date */
    uint32_t secs = (uint32_t)(t / 1000000);
    uint32_t days = secs / 86400u, rem = secs % 86400u;
    /* days since 1970-01-01 -> civil date (H. Hinnant's algorithm) */
    uint32_t z = days + 719468u;
    uint32_t era = z / 146097u, doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t y = yoe + era * 400u, doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u, d = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t m = mp < 10u ? mp + 3u : mp - 9u;
    if (m <= 2u)
        y++;
    if (y < 1980u || y > 2107u)
        return (41u << 25) | (1u << 21) | (1u << 16);
    return ((y - 1980u) << 25) | (m << 21) | (d << 16) | ((rem / 3600u) << 11) | ((rem / 60u % 60u) << 5) |
           ((rem % 60u) / 2u);
}

static int64_t sys_time_set(uint32_t lo, uint32_t hi)
{
    if (!(g_current->proc->caps & CAP_SYS))
        return -EPERM;
    int64_t t = (int64_t)(((uint64_t)hi << 32) | lo);
    s_rt_offset_us = t - (int64_t)time_us();
    mutex_lock(&s_rtc_lock, WAIT_FOREVER);
    int r = s_rtc ? s_rtc->set(s_rtc_ctx, t) : 0;
    mutex_unlock(&s_rtc_lock);
    printk("clock set by %s%s\n", g_current->proc->name, r ? " (the real-time clock refused it)" : "");
    return 0;
}

/* End the calling thread; @done (optional) is set to 1 and woken once the thread has left
 * user mode, so a joining thread may free its stack */
static void sys_thread_exit(int code, uint32_t *done)
{
    if (done && !((uintptr_t)done & 3u) && uaccess_ok(done, 4, 1)) {
        *(volatile uint32_t *)done = 1;
        sys_futex_wake(done, 0x7FFFFFFFu);
    }
    task_exit(code);
}

/* A program wrote instructions (a JIT compiler): its data goes from the data cache to memory
 * and the instruction cache forgets the range, so the core runs the new code. Only the
 * program's own memory, at most 1 MB per call. */
static int64_t sys_cache_sync(const void *addr, uint32_t len)
{
    if (!len)
        return 0;
    if (len > 0x100000u || !uaccess_ok(addr, len, 0))
        return -EFAULT;
    uintptr_t a = (uintptr_t)addr & ~31u;
    int32_t size = (int32_t)ALIGN_UP((uintptr_t)addr + len - a, 32u);
    SCB_CleanDCache_by_Addr((void *)a, size);
    SCB_InvalidateICache_by_Addr((void *)a, size);
    return 0;
}

static int64_t sys_exit(int code)
{
    struct proc *p = g_current->proc;
    if (p)
        proc_kill(p, code); /* terminates the other threads, then the caller */
    task_exit(code);
}

KERNEL_FAST int64_t syscall_dispatch(uint32_t id, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
    switch (id) {
    case SYS_EXIT: return sys_exit((int)a0);
    case SYS_THREAD_EXIT: sys_thread_exit((int)a0, (uint32_t *)a1); return 0;
    case SYS_WRITE: return sys_write((int)a0, (const void *)a1, a2);
    case SYS_SLEEP_MS: return task_sleep_ms(a0);
    case SYS_YIELD: task_yield(); return 0;
    case SYS_GETPID: return g_current->proc ? g_current->proc->pid : 0;
    case SYS_GETTID: return g_current->id;
    case SYS_TIME_US: return (int64_t)time_us();
    case SYS_READ: return sys_read((int)a0, (void *)a1, a2);
    case SYS_OPEN: return sys_open((const char *)a0, a1);
    case SYS_CLOSE: return sys_close((int)a0);
    case SYS_LSEEK: return sys_lseek((int)a0, a1, a2, (int)a3);
    case SYS_IOCTL: return sys_ioctl((int)a0, a1, a2);
    case SYS_FSTAT: return sys_fstat((int)a0, (struct crtos_stat *)a1);
    case SYS_STAT: return sys_stat((const char *)a0, (struct crtos_stat *)a1);
    case SYS_READDIR: return sys_readdir((int)a0, (struct crtos_dirent *)a1);
    case SYS_MKDIR: return sys_mkdir((const char *)a0);
    case SYS_UNLINK: return sys_unlink((const char *)a0);
    case SYS_RENAME: return sys_rename((const char *)a0, (const char *)a1);
    case SYS_CHDIR: return sys_chdir((const char *)a0);
    case SYS_GETCWD: return sys_getcwd((char *)a0, a1);
    case SYS_DUP: return sys_dup((int)a0);
    case SYS_DUP2: return sys_dup2((int)a0, (int)a1);
    case SYS_FSYNC: return sys_fsync((int)a0);
    case SYS_SBRK: return sys_sbrk((int32_t)a0);
    case SYS_SPAWN: return sys_spawn((const struct crtos_spawn *)a0);
    case SYS_WAIT: return sys_wait((int)a0, (int32_t *)a1, a2);
    case SYS_KILL: return sys_kill((int)a0, (int)a1);
    case SYS_THREAD_CREATE: return sys_thread_create(a0, a1, a2, a3, (int)a4);
    case SYS_FUTEX_WAIT: return sys_futex_wait((volatile uint32_t *)a0, a1, a2);
    case SYS_FUTEX_WAKE: return sys_futex_wake((volatile uint32_t *)a0, a1);
    case SYS_POLL: return sys_poll((struct crtos_pollfd *)a0, a1, a2);
    case SYS_PORT_CREATE: return sys_port_create((const char *)a0, a1);
    case SYS_PORT_CONNECT: return sys_port_connect((const char *)a0, a1);
    case SYS_MSG_SEND: return sys_msg_send((int)a0, (const void *)a1, a2, (int)a3, a4);
    case SYS_MSG_CALL: return sys_msg_call((int)a0, (struct crtos_call *)a1, a2);
    case SYS_MSG_RECV: return sys_msg_recv((int)a0, (void *)a1, a2, (struct crtos_msginfo *)a3, a4);
    case SYS_MSG_REPLY: return sys_msg_reply(a0, (const void *)a1, a2, (int)a3);
    case SYS_SHM_CREATE: return sys_shm_create(a0, a1);
    case SYS_SHM_MAP: return sys_shm_map((int)a0);
    case SYS_SHM_UNMAP: return sys_shm_unmap(a0);
    case SYS_PROC_INFO: return sys_proc_info((int)a0, (struct crtos_procinfo *)a1);
    case SYS_TASK_INFO: return sys_task_info((int)a0, (struct crtos_taskinfo *)a1);
    case SYS_SYS_INFO: return sys_sys_info((struct crtos_sysinfo *)a0);
    case SYS_MODULE_LOAD: return sys_module_load((const char *)a0);
    case SYS_MODULE_UNLOAD: return sys_module_unload((const char *)a0);
    case SYS_REBOOT: return sys_reboot();
    case SYS_PIPE: return sys_pipe((int32_t *)a0, a1);
    case SYS_TIME_GET: return wall_time_us();
    case SYS_TIME_SET: return sys_time_set(a0, a1);
    case SYS_SOCKET: return sys_socket((int)a0, (int)a1, (int)a2);
    case SYS_BIND: return sys_bind((int)a0, (const void *)a1, a2);
    case SYS_CONNECT: return sys_connect((int)a0, (const void *)a1, a2);
    case SYS_LISTEN: return sys_listen((int)a0, (int)a1);
    case SYS_ACCEPT: return sys_accept((int)a0, (void *)a1, (uint32_t *)a2);
    case SYS_SENDTO: return sys_sendto((int)a0, (const void *)a1, a2, (int)a3, (const void *)a4, a5);
    case SYS_RECVFROM: return sys_recvfrom((int)a0, (void *)a1, a2, (int)a3, (void *)a4, (uint32_t *)a5);
    case SYS_SHUTDOWN: return sys_shutdown((int)a0, (int)a1);
    case SYS_SETSOCKOPT: return sys_setsockopt((int)a0, (int)a1, (int)a2, (const void *)a3, a4);
    case SYS_GETSOCKOPT: return sys_getsockopt((int)a0, (int)a1, (int)a2, (void *)a3, (uint32_t *)a4);
    case SYS_GETSOCKNAME: return sys_getsockname((int)a0, (void *)a1, (uint32_t *)a2);
    case SYS_GETPEERNAME: return sys_getpeername((int)a0, (void *)a1, (uint32_t *)a2);
    case SYS_CACHE_SYNC: return sys_cache_sync((const void *)a0, a1);
    case SYS_STATFS: return sys_statfs((const char *)a0, (struct crtos_statfs *)a1);
    case SYS_VMEM: return sys_vmem(a0, a1);
    default: return -ENOSYS;
    }
}
