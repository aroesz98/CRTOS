/*
 * kernel/os/sys_proc.cpp - process, thread, memory and system information calls.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include <crtos/module.h>
#include <crtos/syscall.h>
#include <string.h>
#include "fsl_device_registers.h"

#define SPAWN_STR_MAX   4096u
#define SPAWN_ARGS_MAX  64u

static int clamp_prio(struct proc *p, int prio)
{
    int max = (p->caps & CAP_SYS) ? PRIO_HIGH - 1 : SYS_PRIO_MAX_USER;
    if (prio <= 0)
        prio = p->default_prio;
    if (prio < 1)
        prio = 1;
    return prio > max ? max : prio;
}

int64_t sys_sbrk(int32_t incr)
{
    struct proc *p = g_current->proc;
    uint32_t key = irq_lock();
    uint32_t old = p->brk;
    int64_t nb = (int64_t)old + incr;
    /* keep a guard's worth of room below the stacks carved from the top */
    if (nb < (int64_t)p->heap_start || nb + CONFIG_STACK_GUARD > (int64_t)p->stack_floor) {
        irq_unlock(key);
        return -ENOMEM;
    }
    p->brk = (uint32_t)nb;
    irq_unlock(key);
    return (int64_t)(uintptr_t)(p->arena + old);
}

/* ---- spawn -------------------------------------------------------------------------------------- */

/* Copy a NULL terminated user string vector into @buf; pointers into @buf go to @vec */
static int copy_strv(const char *const *uv, const char **vec, uint32_t max, char *buf, uint32_t *used)
{
    uint32_t n = 0;
    if (!uv) {
        vec[0] = nullptr;
        return 0;
    }
    for (;; n++) {
        const char *s;
        if (copy_from_user(&s, &uv[n], sizeof(s)))
            return -EFAULT;
        if (!s)
            break;
        if (n + 1 >= max)
            return -E2BIG;
        int len = strncpy_from_user(buf + *used, s, SPAWN_STR_MAX - *used);
        if (len < 0)
            return len == -ENAMETOOLONG ? -E2BIG : len;
        vec[n] = buf + *used;
        *used += (uint32_t)len + 1u;
    }
    vec[n] = nullptr;
    return (int)n;
}

struct spawn_buf {
    const char *argv[SPAWN_ARGS_MAX];
    const char *envp[SPAWN_ARGS_MAX];
    char path[VFS_PATH_MAX];
    char raw[VFS_RAW_PATH_MAX];     /* as given (may hold ".."), normalised into path */
    char str[SPAWN_STR_MAX];
};

int64_t sys_spawn(const struct crtos_spawn *us)
{
    struct proc *p = g_current->proc;
    if (!(p->caps & CAP_SPAWN))
        return -EPERM;
    struct crtos_spawn sp;
    if (copy_from_user(&sp, us, sizeof(sp)))
        return -EFAULT;
    struct spawn_buf *b = (struct spawn_buf *)kmalloc(sizeof(*b), KM_ANY);
    if (!b)
        return -ENOMEM;
    int r = strncpy_from_user(b->raw, sp.path, sizeof(b->raw));
    if (r == 0)
        r = -ENOENT;
    if (r > 0 && b->raw[0] != '/') { /* relative to the current directory */
        char *rel = b->str;
        strcpy(rel, b->raw);
        r = ksnprintf(b->raw, sizeof(b->raw), "%s/%s", p->cwd, rel) >= (int)sizeof(b->raw) ? -ENAMETOOLONG : 1;
    }
    if (r > 0)
        r = vfs_normalize(b->raw, b->path, sizeof(b->path));
    uint32_t used = 0;
    if (r >= 0)
        r = copy_strv(sp.argv, b->argv, SPAWN_ARGS_MAX, b->str, &used);
    if (r >= 0)
        r = copy_strv(sp.envp, b->envp, SPAWN_ARGS_MAX, b->str, &used);
    if (r < 0) {
        kfree(b);
        return r;
    }
    if (!b->argv[0]) { /* argv[0] defaults to the path */
        b->argv[0] = b->path;
        b->argv[1] = nullptr;
    }
    struct file *stdio[3] = { nullptr, nullptr, nullptr };
    for (int i = 0; i < 3; i++) {
        uint8_t type = H_FILE;
        stdio[i] = (struct file *)handle_ref(p, sp.stdio[i] < 0 ? i : sp.stdio[i], &type, nullptr);
    }
    int err = 0;
    struct proc *child = app_spawn(p, b->path, b->argv, b->envp, stdio, sp.caps & p->caps,
                                   clamp_prio(p, sp.prio), &err);
    for (int i = 0; i < 3; i++)
        if (stdio[i])
            vfs_close(stdio[i]);
    kfree(b);
    if (!child)
        return err ? err : -ENOMEM;
    int pid = child->pid;
    proc_put(child); /* the parent link keeps it until we wait for it */
    return pid;
}

int64_t sys_wait(int pid, int32_t *ustatus, uint32_t timeout)
{
    if (ustatus && !uaccess_ok(ustatus, sizeof(*ustatus), 1) && !vmem_contains(g_current->proc, ustatus, 4))
        return -EFAULT;
    int code = 0;
    int r = proc_wait_child(g_current->proc, pid, timeout, &code);
    if (r > 0 && ustatus) {
        int32_t c = code;
        copy_to_user(ustatus, &c, sizeof(c));
    }
    return r;
}

int64_t sys_kill(int pid, int code)
{
    struct proc *self = g_current->proc;
    struct proc *p = proc_get_by_pid(pid);
    if (!p)
        return -ESRCH;
    if (p == self) { /* ourselves (abort): proc_kill does not return, so the reference goes first */
        proc_put(p);
        proc_kill(self, code);
        return 0;
    }
    bool allowed = p->parent == self || (self->caps & CAP_KILL);
    if (allowed)
        proc_kill(p, code);
    proc_put(p);
    return allowed ? 0 : -EPERM;
}

int64_t sys_thread_create(uint32_t entry, uint32_t arg, uintptr_t stack, uint32_t size, int prio)
{
    struct proc *p = g_current->proc;
    if (!uaccess_ok((const void *)stack, size, 1) || !(entry & 1u))
        return -EINVAL;
    task_t *t = proc_thread_create_at(p, entry, arg, (uint8_t *)stack, size, clamp_prio(p, prio));
    return t ? t->id : -EINVAL;
}

/* ---- futexes ------------------------------------------------------------------------------------- */

#define FUTEX_BUCKETS 16
static struct wait_queue s_futex[FUTEX_BUCKETS];
static bool s_futex_ready;

static struct wait_queue *futex_queue(uintptr_t a)
{
    if (!s_futex_ready) {
        uint32_t key = irq_lock();
        if (!s_futex_ready) {
            for (int i = 0; i < FUTEX_BUCKETS; i++)
                wq_init(&s_futex[i]);
            s_futex_ready = true;
        }
        irq_unlock(key);
    }
    return &s_futex[(a >> 2) % FUTEX_BUCKETS];
}

int64_t sys_futex_wait(volatile uint32_t *addr, uint32_t expected, uint32_t timeout)
{
    uintptr_t a = (uintptr_t)addr;
    if ((a & 3u) || !uaccess_ok((const void *)a, 4, 0))
        return -EFAULT;
    struct wait_queue *q = futex_queue(a);
    task_t *t = g_current;
    uint32_t key = irq_lock();
    if (*addr != expected) {
        irq_unlock(key);
        return -EAGAIN;
    }
    t->futex_key = a;
    int r = sched_block(q, timeout, key);
    t->futex_key = 0;
    return r;
}

int64_t sys_futex_wake(volatile uint32_t *addr, uint32_t count)
{
    uintptr_t a = (uintptr_t)addr;
    if ((a & 3u) || !uaccess_ok((const void *)a, 4, 0))
        return -EFAULT;
    struct wait_queue *q = futex_queue(a);
    uint32_t n = 0;
    uint32_t key = irq_lock();
    struct list_head *pos, *tmp;
    list_for_each_safe(pos, tmp, &q->waiters) {
        if (n >= count)
            break;
        task_t *w = list_entry(pos, task_t, rq_node);
        if (w->futex_key == a) {
            sched_wake(w, 0);
            n++;
        }
    }
    irq_unlock(key);
    return n;
}

/* ---- information ---------------------------------------------------------------------------------- */

struct info_ctx {
    int want, index;
    bool found;
    union {
        struct crtos_procinfo pi;
        struct crtos_taskinfo ti;
    };
};

static void proc_info_one(struct proc *p, void *vctx)
{
    struct info_ctx *c = (struct info_ctx *)vctx;
    if (c->index++ != c->want)
        return;
    c->found = true;
    struct crtos_procinfo *pi = &c->pi;
    memset(pi, 0, sizeof(*pi));
    pi->pid = p->pid;
    pi->ppid = p->parent ? p->parent->pid : 0;
    strncpy(pi->name, p->name, sizeof(pi->name) - 1);
    pi->state = p->state;
    pi->nthreads = (uint8_t)p->nthreads;
    pi->nhandles = p->released ? 0 : (uint16_t)handle_count(p);
    pi->caps = p->caps;
    pi->arena_size = p->released ? 0 : p->arena_size;
    pi->heap_used = p->brk - p->heap_start;
    /* CPU time: its threads' and what the ended ones used. With interrupts masked: the
     * tick updates the 64-bit counts (a torn read would be off by 2^32 cycles). */
    uint32_t key = irq_lock();
    uint64_t cycles = p->cycles_dead;
    struct list_head *pos;
    list_for_each(pos, &p->threads)
        cycles += list_entry(pos, task_t, proc_node)->cycles;
    irq_unlock(key);
    pi->cycles = cycles;
}

int64_t sys_proc_info(int index, struct crtos_procinfo *upi)
{
    struct info_ctx *c = (struct info_ctx *)kzalloc(sizeof(*c), KM_FAST);
    if (!c)
        return -ENOMEM;
    c->want = index;
    proc_foreach(proc_info_one, c);
    int r = -ENOENT;
    if (c->found)
        r = copy_to_user(upi, &c->pi, sizeof(c->pi));
    kfree(c);
    return r;
}

static void task_info_one(task_t *t, void *vctx)
{
    struct info_ctx *c = (struct info_ctx *)vctx;
    if (c->index++ != c->want)
        return;
    c->found = true;
    struct crtos_taskinfo *ti = &c->ti;
    memset(ti, 0, sizeof(*ti));
    ti->tid = t->id;
    ti->pid = t->proc && t->state != TASK_DEAD ? t->proc->pid : 0;
    strncpy(ti->name, t->name, sizeof(ti->name) - 1);
    ti->prio = t->prio;
    ti->base_prio = t->base_prio;
    ti->state = t == g_current ? TASK_STATE_RUNNING : t->state;
    ti->kstack_size = t->kstack_size;
    ti->kstack_used = t->kstack ? t->kstack_size - stack_unused(t->kstack, t->kstack_size) : 0;
    ti->ustack_size = t->ustack_size;
    ti->ustack_used = t->ustack && t->state != TASK_DEAD ? t->ustack_size - stack_unused(t->ustack, t->ustack_size) : 0;
    uint32_t key = irq_lock(); /* (64 bits the tick updates) */
    ti->cycles = t->cycles;
    irq_unlock(key);
}

int64_t sys_task_info(int index, struct crtos_taskinfo *uti)
{
    struct info_ctx *c = (struct info_ctx *)kzalloc(sizeof(*c), KM_FAST);
    if (!c)
        return -ENOMEM;
    c->want = index;
    task_foreach(task_info_one, c);
    int r = c->found ? copy_to_user(uti, &c->ti, sizeof(c->ti)) : -ENOENT;
    kfree(c);
    return r;
}

static void count_task(task_t *, void *ctx)
{
    (*(uint32_t *)ctx)++;
}

int64_t sys_sys_info(struct crtos_sysinfo *usi)
{
    struct crtos_sysinfo si;
    memset(&si, 0, sizeof(si));
    si.uptime_us = time_us();
    task_t *idle = sched_idle_task();
    uint32_t key = irq_lock();
    si.idle_cycles = idle->cycles;
    irq_unlock(key);
    si.cpu_hz = SystemCoreClock;
    si.nprocs = (uint32_t)proc_count();
    task_foreach(count_task, &si.ntasks);
    for (int i = 0; i < mm_pool_count(); i++) {
        struct mm_pool_info pi;
        if (mm_pool_info(i, &pi))
            continue;
        if (!strcmp(pi.name, "sdram")) {
            si.mem_total += pi.size;
            si.mem_free += pi.free;
        } else if (!strcmp(pi.name, "ncache")) {
            si.dma_total += pi.size;
            si.dma_free += pi.free;
        } else {
            si.kmem_total += pi.size;
            si.kmem_free += pi.free;
        }
    }
    return copy_to_user(usi, &si, sizeof(si));
}

/* ---- privileged ------------------------------------------------------------------------------------- */

int64_t sys_module_load(const char *upath)
{
    struct proc *p = g_current->proc;
    if (!(p->caps & CAP_MODULE))
        return -EPERM;
    char *path = (char *)kmalloc(VFS_PATH_MAX, KM_FAST);
    if (!path)
        return -ENOMEM;
    int r = strncpy_from_user(path, upath, VFS_PATH_MAX);
    if (r > 0)
        r = module_load(path, nullptr);
    else if (r == 0)
        r = -ENOENT;
    kfree(path);
    return r;
}

int64_t sys_module_unload(const char *uname)
{
    struct proc *p = g_current->proc;
    if (!(p->caps & CAP_MODULE))
        return -EPERM;
    char name[32];
    int r = strncpy_from_user(name, uname, sizeof(name));
    if (r <= 0)
        return r ? r : -ENOENT;
    return module_unload(name);
}

int64_t sys_reboot(void)
{
    if (!(g_current->proc->caps & CAP_SYS))
        return -EPERM;
    printk("reboot requested by %s\n", g_current->proc->name);
    task_sleep_ms(50); /* let the console send the log */
    NVIC_SystemReset();
    return 0;
}
