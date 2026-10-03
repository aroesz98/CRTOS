/*
 * crtos.c - CRTOS system interface (processes, IPC, shared memory, information).
 */
#include <errno.h>
#include <string.h>
#include <crtos.h>

#define SYS(id, a0, a1, a2, a3) crtos_sys((id), (long)(a0), (long)(a1), (long)(a2), (long)(a3))

static inline int is_err(long r)
{
    return (unsigned long)r >= (unsigned long)-4095;
}

static long sys6(long id, long a0, long a1, long a2, long a3, long a4, long a5)
{
    long r = __crtos_syscall6(id, a0, a1, a2, a3, a4, a5);
    if (is_err(r)) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

/* ---- processes ---------------------------------------------------------------------------- */

int crtos_spawn(const struct crtos_spawn *sp)
{
    return (int)SYS(SYS_SPAWN, sp, 0, 0, 0);
}

int crtos_spawnv(const char *path, char *const argv[])
{
    struct crtos_spawn sp;
    memset(&sp, 0, sizeof(sp));
    sp.path = path;
    sp.argv = (const char *const *)argv;
    sp.stdio[0] = sp.stdio[1] = sp.stdio[2] = -1;
    return crtos_spawn(&sp);
}

int crtos_wait(int pid, int *status, uint32_t timeout)
{
    int32_t st = 0;
    int r = (int)SYS(SYS_WAIT, pid, &st, timeout, 0);
    if (r > 0 && status)
        *status = st;
    return r;
}

int crtos_kill(int pid, int code)
{
    return (int)SYS(SYS_KILL, pid, code, 0, 0);
}

int crtos_gettid(void)
{
    return (int)__crtos_syscall(SYS_GETTID, 0, 0, 0, 0);
}

uint64_t crtos_time_us(void)
{
    return (uint64_t)__crtos_syscall64(SYS_TIME_US, 0, 0, 0, 0);
}

void crtos_sleep_ms(uint32_t ms)
{
    __crtos_syscall(SYS_SLEEP_MS, (long)ms, 0, 0, 0);
}

void crtos_yield(void)
{
    __crtos_syscall(SYS_YIELD, 0, 0, 0, 0);
}

int crtos_futex_wait(volatile uint32_t *addr, uint32_t expected, uint32_t timeout)
{
    return SYS(SYS_FUTEX_WAIT, addr, expected, timeout, 0) < 0 ? -1 : 0;
}

int crtos_futex_wake(volatile uint32_t *addr, uint32_t count)
{
    return (int)SYS(SYS_FUTEX_WAKE, addr, count, 0, 0);
}

/* ---- IPC ---------------------------------------------------------------------------------- */

int crtos_port_create(const char *name)
{
    return (int)SYS(SYS_PORT_CREATE, name, 0, 0, 0);
}

int crtos_port_connect(const char *name, uint32_t timeout)
{
    return (int)SYS(SYS_PORT_CONNECT, name, timeout, 0, 0);
}

int crtos_msg_send(int port, const void *data, size_t len, int handle, uint32_t timeout)
{
    return (int)sys6(SYS_MSG_SEND, port, (long)data, (long)len, handle, (long)timeout, 0);
}

int crtos_msg_call2(int port, struct crtos_call *c, uint32_t timeout)
{
    return (int)SYS(SYS_MSG_CALL, port, c, timeout, 0);
}

int crtos_msg_call(int port, const void *req, size_t req_len, void *rep, size_t rep_max, uint32_t timeout)
{
    struct crtos_call c = { req, (uint32_t)req_len, -1, rep, (uint32_t)rep_max, -1 };
    return crtos_msg_call2(port, &c, timeout);
}

int crtos_msg_recv(int port, void *buf, size_t max, struct crtos_msginfo *info, uint32_t timeout)
{
    return (int)sys6(SYS_MSG_RECV, port, (long)buf, (long)max, (long)info, (long)timeout, 0);
}

int crtos_msg_reply(uint32_t token, const void *data, size_t len, int handle)
{
    return (int)SYS(SYS_MSG_REPLY, token, data, len, handle);
}

/* ---- shared memory ------------------------------------------------------------------------ */

int crtos_shm_create(size_t size, uint32_t flags)
{
    return (int)SYS(SYS_SHM_CREATE, size, flags, 0, 0);
}

void *crtos_shm_map(int handle)
{
    long r = __crtos_syscall(SYS_SHM_MAP, handle, 0, 0, 0);
    if (is_err(r)) {
        errno = (int)-r;
        return NULL;
    }
    return (void *)r;
}

int crtos_shm_unmap(void *addr)
{
    return (int)SYS(SYS_SHM_UNMAP, addr, 0, 0, 0);
}

/* ---- emulated memory ---------------------------------------------------------------------- */

void *crtos_vmem_map(size_t size)
{
    long r = __crtos_syscall(SYS_VMEM, VMEM_MAP, (long)size, 0, 0);
    if (is_err(r)) {
        errno = (int)-r;
        return NULL;
    }
    return (void *)r;
}

int crtos_vmem_unmap(void *addr)
{
    return (int)SYS(SYS_VMEM, VMEM_UNMAP, addr, 0, 0);
}

int crtos_vmem_info(struct crtos_vmeminfo *vi)
{
    return (int)SYS(SYS_VMEM, VMEM_INFO, vi, 0, 0);
}

/* ---- system -------------------------------------------------------------------------------- */

int crtos_proc_info(int index, struct crtos_procinfo *pi)
{
    return (int)SYS(SYS_PROC_INFO, index, pi, 0, 0);
}

int crtos_task_info(int index, struct crtos_taskinfo *ti)
{
    return (int)SYS(SYS_TASK_INFO, index, ti, 0, 0);
}

int crtos_statfs(const char *path, uint64_t *total, uint64_t *free)
{
    struct crtos_statfs sf;
    if (SYS(SYS_STATFS, path, &sf, 0, 0) < 0)
        return -1;
    if (total)
        *total = sf.total;
    if (free)
        *free = sf.free;
    return 0;
}

int crtos_sys_info(struct crtos_sysinfo *si)
{
    return (int)SYS(SYS_SYS_INFO, si, 0, 0, 0);
}

int crtos_cache_sync(const void *addr, size_t len)
{
    return (int)SYS(SYS_CACHE_SYNC, addr, len, 0, 0);
}

int crtos_module_load(const char *path)
{
    return (int)SYS(SYS_MODULE_LOAD, path, 0, 0, 0);
}

int crtos_module_unload(const char *name)
{
    return (int)SYS(SYS_MODULE_UNLOAD, name, 0, 0, 0);
}

int crtos_reboot(void)
{
    return (int)SYS(SYS_REBOOT, 0, 0, 0, 0);
}
