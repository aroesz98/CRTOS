/*
 * kernel/os/proc.cpp - user processes.
 *
 * A process owns an arena: one block that MPU region 8 opens to unprivileged code while
 * one of its threads runs (a run of eighths of a power-of-two region, the other eighths
 * disabled as MPU subregions). Everything the program touches lives there (code, data, heap, thread
 * stacks) or in shared memory it mapped; the rest of memory is out of reach. Each thread
 * also has a kernel stack (outside the arena) used while it executes system calls.
 *
 * Lifetime: when the last thread ends the process is dead; the kernel worker then releases
 * its resources (handles, shared memory, arena). The struct itself lives on as long as it
 * is referenced: by itself until released, by its parent until the parent waited for it
 * (or ended), and by kernel users (proc_get/proc_put).
 */
#include "kernel.h"
#include <string.h>

static struct list_head s_procs = LIST_HEAD_INIT(s_procs);
static int s_next_pid = 1;
static int s_nprocs;

#define USTACK_DEFAULT 4096u

static uint32_t pow2_ceil(uint32_t v)
{
    return v <= 1 ? 1 : 1u << (32 - __builtin_clz(v - 1));
}

/* An arena of at least @want bytes: a run of MPU subregions (eighths of a power-of-two
 * region) inside one region-aligned block. The smallest region that can hold it is tried
 * first, then one twice as large: its subregions are twice as coarse, but there are twice
 * as many places for the run (a big program next to others' arenas). */
static uint8_t *arena_alloc(uint32_t want, uint32_t *size)
{
    if (want < 4096)
        want = 4096;
    uint32_t full = pow2_ceil(want);
    for (int k = 0; k < 2 && full; k++, full <<= 1) {
        uint32_t sub = full / 8u;
        uint32_t sz = ALIGN_UP(want, sub);
        void *a = kmalloc_bounded(sz, sub, full, KM_LARGE);
        if (a) {
            *size = sz;
            return (uint8_t *)a;
        }
    }
    return nullptr;
}

struct proc *proc_create(const char *name, uint32_t arena_size, struct proc *parent)
{
    uint32_t size = 0;

    struct proc *p = (struct proc *)kzalloc(sizeof(*p), KM_ANY);
    if (!p)
        return nullptr;
    p->htab = (struct handle *)kzalloc(CONFIG_MAX_HANDLES * sizeof(struct handle), KM_ANY);
    p->arena = arena_alloc(arena_size, &size);
    if (!p->htab || !p->arena ||
        mpu_encode_region(MPU_REGION_ARENA, (uintptr_t)p->arena, size, MPU_ATTR_USER_RWX, &p->arena_rbar,
                          &p->arena_rasr)) {
        kfree(p->arena);
        kfree(p->htab);
        kfree(p);
        return nullptr;
    }
    memset(p->arena, 0, size); /* never leak old kernel data to user space */
    for (int i = 0; i < SHM_WINDOWS; i++)
        mpu_empty_region(MPU_REGION_SHM0 + i, &p->win[i].rbar, &p->win[i].rasr);
    p->arena_size = size;
    p->stack_floor = size;     /* stacks are carved from the top */
    p->brk = p->heap_start = 0;
    p->state = PROC_ALIVE;
    p->refs = 2;               /* its own + the caller's */
    p->default_prio = PRIO_NORMAL;
    strncpy(p->name, name ? name : "proc", sizeof(p->name) - 1);
    list_init(&p->threads);
    list_init(&p->children);
    list_init(&p->sibling);
    wq_init(&p->exit_wq);
    wq_init(&p->child_wq);
    if (parent) {
        strncpy(p->cwd, parent->cwd, sizeof(p->cwd) - 1);
        p->default_prio = parent->default_prio;
    } else {
        strcpy(p->cwd, "/");
    }

    uint32_t key = irq_lock();
    p->pid = s_next_pid++;
    list_add_tail(&p->all_node, &s_procs);
    s_nprocs++;
    if (parent && parent->state == PROC_ALIVE) {
        p->parent = parent;
        p->refs++; /* the parent's link, dropped when it waits for us or ends */
        list_add_tail(&p->sibling, &parent->children);
    }
    irq_unlock(key);
    return p;
}

static task_t *thread_new(struct proc *p, uint32_t entry, uint32_t arg, uint8_t *ustack, uint32_t size, int prio)
{
    task_t *t = task_create_raw(p->name, prio, TF_USER);
    if (!t)
        return nullptr;
    t->kstack = (uint8_t *)kmalloc_aligned(CONFIG_KSTACK_SIZE + CONFIG_STACK_GUARD, CONFIG_STACK_GUARD, KM_FAST);
    if (!t->kstack) {
        uint32_t key = irq_lock();
        list_del(&t->all_node);
        irq_unlock(key);
        kfree(t);
        return nullptr;
    }
    t->kstack_size = CONFIG_KSTACK_SIZE + CONFIG_STACK_GUARD;
    task_fill_stack(t->kstack, t->kstack_size);
    t->ustack = ustack;
    t->ustack_size = size;
    task_fill_stack(t->ustack, size);
    t->proc = p;
    mpu_set_user_guards(t);
    /* Start in thread mode, unprivileged, on the user stack */
    t->sp = task_build_frame(t->ustack + size, entry, arg, 1u, 0u);
    /* a program that runs in place reaches its data through r9 (sp[0] CONTROL, sp[1..8]
     * R4..R11: see the context switch) */
    if (p->sb)
        t->sp[6] = p->sb;

    uint32_t key = irq_lock();
    if (p->state != PROC_ALIVE) {
        list_del(&t->all_node);
        irq_unlock(key);
        kfree(t->kstack);
        kfree(t);
        return nullptr;
    }
    list_add_tail(&t->proc_node, &p->threads);
    p->nthreads++;
    sched_ready(t);
    irq_unlock(key);
    return t;
}

task_t *proc_thread_create(struct proc *p, uint32_t entry, uint32_t arg, uint32_t ustack_size, int prio)
{
    ustack_size = ALIGN_UP(ustack_size ? ustack_size : USTACK_DEFAULT, 32u) + CONFIG_STACK_GUARD;

    uint32_t key = irq_lock();
    uint32_t base_off = 0;
    if (p->stack_floor >= ustack_size) /* the guard region needs its natural alignment */
        base_off = ALIGN_DOWN(p->stack_floor - ustack_size, CONFIG_STACK_GUARD);
    if (p->state != PROC_ALIVE || p->stack_floor < ustack_size || base_off < p->brk) {
        irq_unlock(key);
        return nullptr;
    }
    uint32_t old_floor = p->stack_floor;
    p->stack_floor = base_off;
    irq_unlock(key);

    task_t *t = thread_new(p, entry, arg, p->arena + base_off, old_floor - base_off, prio);
    if (!t) {
        key = irq_lock();
        if (p->stack_floor == base_off)
            p->stack_floor = old_floor;
        irq_unlock(key);
    }
    return t;
}

task_t *proc_thread_create_at(struct proc *p, uint32_t entry, uint32_t arg, uint8_t *stack, uint32_t size, int prio)
{
    uintptr_t off = (uintptr_t)stack - (uintptr_t)p->arena;
    if ((uintptr_t)stack < (uintptr_t)p->arena || off >= p->arena_size || size > p->arena_size - off ||
        (off & (CONFIG_STACK_GUARD - 1)) || size < CONFIG_STACK_GUARD + 512u)
        return nullptr;
    return thread_new(p, entry, arg, stack, size & ~7u, prio);
}

/* Take @p off its parent's list of children (it will not be waited for) */
void proc_detach(struct proc *p)
{
    uint32_t key = irq_lock();
    bool linked = p->parent != nullptr;
    if (linked) {
        list_del(&p->sibling);
        p->parent = nullptr;
    }
    irq_unlock(key);
    if (linked)
        proc_put(p);
}

void proc_get(struct proc *p)
{
    uint32_t key = irq_lock();
    p->refs++;
    irq_unlock(key);
}

static void proc_free(struct proc *p)
{
    uint32_t key = irq_lock();
    list_del(&p->all_node);
    s_nprocs--;
    irq_unlock(key);
    mpu_forget_proc(p);
    kfree(p->arena);
    kfree(p->fastcode);
    kfree(p->htab);
    kfree(p);
}

static void proc_free_work(void *arg)
{
    proc_free((struct proc *)arg);
}

void proc_put(struct proc *p)
{
    uint32_t key = irq_lock();
    BUG_ON(p->refs == 0);
    bool last = --p->refs == 0;
    irq_unlock(key);
    if (last) {
        if (in_interrupt())
            kworker_queue(proc_free_work, p);
        else
            proc_free(p);
    }
}

/* Free what a dead process holds; runs in the kernel worker (closing files may block) */
static void proc_release(struct proc *p)
{
    handle_close_all(p);
    shm_unmap_all(p);
    vmem_proc_release(p);
    mpu_forget_proc(p);
    if (p->xip_file) { /* its code in place may now go (the file system frees a removed file) */
        vfs_close(p->xip_file);
        p->xip_file = nullptr;
    }

    /* children live on without a parent (nobody waits for them any more) */
    struct list_head orphans;
    list_init(&orphans);
    uint32_t key = irq_lock();
    while (!list_empty(&p->children)) {
        struct proc *c = list_first_entry(&p->children, struct proc, sibling);
        list_del(&c->sibling);
        c->parent = nullptr;
        list_add_tail(&c->sibling, &orphans);
    }
    uint8_t *arena = p->arena, *fastcode = p->fastcode;
    p->arena = nullptr;
    p->fastcode = nullptr;
    for (int i = 0; i < SHM_WINDOWS; i++)
        if (!p->win[i].shm) /* (the fast code block's window; the others are unmapped) */
            mpu_empty_region(MPU_REGION_SHM0 + i, &p->win[i].rbar, &p->win[i].rasr);
    p->released = 1;
    irq_unlock(key);
    while (!list_empty(&orphans)) {
        struct proc *c = list_first_entry(&orphans, struct proc, sibling);
        list_del(&c->sibling);
        proc_put(c);
    }
    kfree(arena);
    kfree(fastcode);
    proc_put(p); /* its own reference */
}

static void proc_death_work(void *arg)
{
    proc_release((struct proc *)arg);
}

/* Called by task_exit() in the context of the exiting thread */
void proc_thread_exited(task_t *t, int code)
{
    struct proc *p = t->proc;
    uint32_t key = irq_lock();
    list_del(&t->proc_node);
    p->cycles_dead += t->cycles; /* the process's CPU time stays (not the last few us) */
    bool last = --p->nthreads == 0;
    if (last) {
        if (p->state == PROC_ALIVE)
            p->exit_code = code;
        p->state = PROC_DEAD;
        wq_wake_all(&p->exit_wq, 0);
        if (p->parent) /* the parent cannot go away while we are on its list */
            wq_wake_all(&p->parent->child_wq, 0);
    }
    irq_unlock(key);
    if (last) /* the arena may still be mapped for this thread: release it from the worker */
        kworker_queue(proc_death_work, p);
}

void proc_kill(struct proc *p, int code)
{
    task_t *self = nullptr;
    uint32_t key = irq_lock();
    if (p->state == PROC_ALIVE) {
        p->state = PROC_EXITING;
        p->exit_code = code;
    }
    struct list_head *pos;
    list_for_each(pos, &p->threads) {
        task_t *t = list_entry(pos, task_t, proc_node);
        if (t == g_current && !in_interrupt())
            self = t;
        else
            task_kill(t);
    }
    bool never_ran = p->nthreads == 0 && p->state != PROC_DEAD;
    if (never_ran) {
        p->state = PROC_DEAD;
        wq_wake_all(&p->exit_wq, 0);
        if (p->parent)
            wq_wake_all(&p->parent->child_wq, 0);
    }
    irq_unlock(key);
    if (never_ran) /* no thread will ever report the death */
        kworker_queue(proc_death_work, p);
    if (self)
        task_exit(code);
}

int proc_wait(struct proc *p, uint32_t timeout, int *code)
{
    uint32_t deadline = g_ticks + timeout;
    for (;;) {
        uint32_t key = irq_lock();
        if (p->state == PROC_DEAD) {
            irq_unlock(key);
            if (code)
                *code = p->exit_code;
            return 0;
        }
        uint32_t left = WAIT_FOREVER;
        if (timeout != WAIT_FOREVER) {
            int32_t rem = (int32_t)(deadline - g_ticks);
            if (rem <= 0) {
                irq_unlock(key);
                return -ETIMEDOUT;
            }
            left = (uint32_t)rem;
        }
        int r = sched_block(&p->exit_wq, left, key);
        if (r == -EINTR)
            return r;
    }
}

int proc_wait_child(struct proc *parent, int pid, uint32_t timeout, int *code)
{
    uint32_t deadline = g_ticks + timeout;
    for (;;) {
        uint32_t key = irq_lock();
        struct proc *found = nullptr;
        bool any = false;
        struct list_head *pos;
        list_for_each(pos, &parent->children) {
            struct proc *c = list_entry(pos, struct proc, sibling);
            if (pid > 0 && c->pid != pid)
                continue;
            any = true;
            if (c->state == PROC_DEAD) {
                found = c;
                break;
            }
        }
        if (found) {
            list_del(&found->sibling);
            found->parent = nullptr;
            irq_unlock(key);
            int id = found->pid;
            if (code)
                *code = found->exit_code;
            proc_put(found);
            return id;
        }
        if (!any) {
            irq_unlock(key);
            return -ECHILD;
        }
        uint32_t left = WAIT_FOREVER;
        if (timeout != WAIT_FOREVER) {
            int32_t rem = (int32_t)(deadline - g_ticks);
            if (timeout == NO_WAIT || rem <= 0) {
                irq_unlock(key);
                return -ETIMEDOUT;
            }
            left = (uint32_t)rem;
        }
        int r = sched_block(&parent->child_wq, left, key);
        if (r == -EINTR)
            return r;
    }
}

struct proc *proc_find(int pid)
{
    struct list_head *pos;
    list_for_each(pos, &s_procs) {
        struct proc *p = list_entry(pos, struct proc, all_node);
        if (p->pid == pid)
            return p;
    }
    return nullptr;
}

struct proc *proc_get_by_pid(int pid)
{
    uint32_t key = irq_lock();
    struct proc *p = proc_find(pid);
    if (p)
        p->refs++;
    irq_unlock(key);
    return p;
}

void proc_foreach(void (*fn)(struct proc *p, void *ctx), void *ctx)
{
    sched_lock(); /* procs are freed by the worker thread, which cannot run meanwhile */
    struct list_head *pos;
    list_for_each(pos, &s_procs)
        fn(list_entry(pos, struct proc, all_node), ctx);
    sched_unlock();
}

int proc_count(void)
{
    return s_nprocs;
}

/* ---- user memory -------------------------------------------------------------------------- */

/* Bytes accessible from @a on (0: not user memory) */
static size_t user_span(struct proc *p, uintptr_t a, bool write)
{
    uintptr_t base = (uintptr_t)p->arena;
    if (base && a >= base && a - base < p->arena_size)
        return p->arena_size - (a - base);
    /* a program that runs in place passes its constants (a path, a message) from its text */
    if (!write && p->xip_file && a >= p->text && a - p->text < p->text_size)
        return p->text_size - (a - p->text);
    /* a mapped shared memory object: the whole of it is mapped (all the pieces of a large one) */
    return shm_window_span(p, a);
}

/* Is [ptr, ptr+len) inside the calling process' memory? (kernel threads may pass anything) */
/* The guard below the calling thread's user stack stays on in system calls (it is no
 * access for the kernel as well): a pointer into it is refused, not a fault in the kernel */
static bool in_own_guard(uintptr_t a, size_t len)
{
    uintptr_t g = (uintptr_t)g_current->ustack;
    return g && a < g + CONFIG_STACK_GUARD && a + len > g;
}

int uaccess_ok(const void *ptr, size_t len, int write)
{
    struct proc *p = g_current->proc;
    if (!p || !len)
        return 1;
    return len <= user_span(p, (uintptr_t)ptr, write != 0) && !in_own_guard((uintptr_t)ptr, len);
}

int capable(uint32_t caps)
{
    struct proc *p = g_current->proc;
    return !p || (p->caps & caps) == caps;
}

/* (a program's emulated memory is reached through the page cache, vmem.cpp) */
int copy_from_user(void *dst, const void *src, size_t len)
{
    if (!uaccess_ok(src, len, 0))
        return vmem_contains(g_current->proc, src, len) ? vmem_copy_in(dst, src, len) : -EFAULT;
    memcpy(dst, src, len);
    return 0;
}

int copy_to_user(void *dst, const void *src, size_t len)
{
    if (!uaccess_ok(dst, len, 1))
        return vmem_contains(g_current->proc, dst, len) ? vmem_copy_out(dst, src, len) : -EFAULT;
    memcpy(dst, src, len);
    return 0;
}

int strncpy_from_user(char *dst, const char *src, size_t size)
{
    struct proc *p = g_current->proc;
    if (vmem_contains(p, src, 1))
        return vmem_strncpy_in(dst, src, size);
    size_t avail = p ? user_span(p, (uintptr_t)src, false) : size;
    uintptr_t a = (uintptr_t)src, g = (uintptr_t)g_current->ustack;
    if (p && g && a < g + CONFIG_STACK_GUARD && a + avail > g) /* stop at the stack guard */
        avail = a < g ? g - a : 0;
    size_t max = avail < size ? avail : size;
    for (size_t i = 0; i < max; i++) {
        dst[i] = src[i];
        if (!dst[i])
            return (int)i;
    }
    if (size)
        dst[size - 1] = 0;
    return max == size ? -ENAMETOOLONG : -EFAULT;
}
