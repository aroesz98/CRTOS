/*
 * kernel/rtos/sched.cpp - O(1) priority scheduler.
 *
 * Ready tasks sit in one FIFO list per priority; a 32-bit bitmap tells which lists are
 * non-empty, so picking the next task is a CLZ. The running task stays at the head of its
 * list; the time slice rotates it to the tail. A task gets a whole slice when it becomes
 * ready and when it goes to the tail; preempted by a higher priority it keeps the rest of it
 * (a new slice at every switch-in would never run out for a busy task that interrupts and
 * higher-priority threads keep preempting, and its equals would starve). Blocked tasks with
 * a timeout are also kept on a sleep queue sorted by wake-up tick, so the tick handler only
 * looks at its head.
 *
 * All scheduler state is protected by irq_lock() (BASEPRI), PendSV performs the switch.
 */
#include "kernel.h"
#include "fsl_device_registers.h"
#include <string.h>

task_t *volatile g_current;
volatile uint32_t g_ticks;
static volatile uint32_t s_ticks_hi;

static uint32_t s_ready_bitmap;
static struct list_head s_ready[CONFIG_NUM_PRIO];
static struct list_head s_sleepq = LIST_HEAD_INIT(s_sleepq);
static struct list_head s_all = LIST_HEAD_INIT(s_all);
static struct list_head s_dead = LIST_HEAD_INIT(s_dead);
static int s_next_id = 1;
static volatile uint32_t s_lock_count;
static volatile uint32_t s_switch_deferred;
static task_t *s_idle;

static volatile bool s_started;

/* A tick held off for longer than a period (interrupts masked, a long interrupt handler)
 * would be lost: the cycle counter tells how many periods went by and they are counted
 * after all, so the clock does not fall behind the CPU time of the tasks */
static uint32_t s_tick_cyc;         /* cycle counter at the start of the last counted period */
static struct tick_stats s_tick_stats;

/* Before sched_start() tasks are only made ready: switching away from main() (which runs
 * the kernel initialisation on the MSP) would abandon it */
void sched_pend_switch(void)
{
    if (likely(s_started)) {
        SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
        dsb();
    }
}

/* ---- queues (irq locked) ------------------------------------------------------ */

static inline void rq_add(task_t *t)
{
    list_add_tail(&t->rq_node, &s_ready[t->prio]);
    s_ready_bitmap |= 1u << t->prio;
}

static inline void rq_del(task_t *t)
{
    list_del(&t->rq_node);
    if (list_empty(&s_ready[t->prio]))
        s_ready_bitmap &= ~(1u << t->prio);
}

static void sleepq_add(task_t *t, uint32_t wake)
{
    struct list_head *pos;
    t->wake_tick = wake;
    t->flags |= TF_SLEEPQ;
    list_for_each(pos, &s_sleepq) {
        task_t *o = list_entry(pos, task_t, sl_node);
        if ((int32_t)(o->wake_tick - wake) > 0)
            break;
    }
    list_insert_between(&t->sl_node, pos->prev, pos);
}

static void sleepq_del(task_t *t)
{
    if (t->flags & TF_SLEEPQ) {
        list_del(&t->sl_node);
        t->flags &= ~TF_SLEEPQ;
    }
}

void wq_init(struct wait_queue *wq)
{
    list_init(&wq->waiters);
}

/* Highest priority first, FIFO among equals */
void wq_insert(struct wait_queue *wq, task_t *t)
{
    struct list_head *pos;
    list_for_each(pos, &wq->waiters) {
        task_t *o = list_entry(pos, task_t, rq_node);
        if (o->prio < t->prio)
            break;
    }
    list_insert_between(&t->rq_node, pos->prev, pos);
    t->waiting_on = wq;
}

int wq_wake_one(struct wait_queue *wq, int result)
{
    uint32_t key = irq_lock();
    int woken = 0;
    if (!list_empty(&wq->waiters)) {
        sched_wake(list_first_entry(&wq->waiters, task_t, rq_node), result);
        woken = 1;
    }
    irq_unlock(key);
    return woken;
}

int wq_wake_all(struct wait_queue *wq, int result)
{
    uint32_t key = irq_lock();
    int woken = 0;
    while (!list_empty(&wq->waiters)) {
        sched_wake(list_first_entry(&wq->waiters, task_t, rq_node), result);
        woken++;
    }
    irq_unlock(key);
    return woken;
}

/* ---- blocking / waking ------------------------------------------------------- */

void sched_ready(task_t *t)
{
    t->state = TASK_READY;
    t->slice = CONFIG_TIMESLICE_TICKS;
    rq_add(t);
    if (!g_current || t->prio > g_current->prio)
        sched_pend_switch();
}

/*
 * Block the current task on @wq (may be NULL: plain sleep) for at most @timeout ticks.
 * Must be entered with irq_lock() held; @key is the value irq_lock() returned and must be
 * 0 (no outer critical section), because the switch happens when the lock is released.
 */
int sched_block(struct wait_queue *wq, uint32_t timeout, uint32_t key)
{
    task_t *t = g_current;

    if (unlikely(key != 0 || in_interrupt() || s_lock_count))
        panic("sched_block from %s (key=%lx lock=%lu)", in_interrupt() ? "ISR" : "locked context",
              (unsigned long)key, (unsigned long)s_lock_count);
    if (t->flags & TF_KILLED) {
        irq_unlock(key);
        return -EINTR;
    }
    rq_del(t);
    t->state = TASK_BLOCKED;
    t->wait_result = -ETIMEDOUT;
    t->waiting_on = nullptr;
    if (wq)
        wq_insert(wq, t);
    if (timeout != WAIT_FOREVER)
        sleepq_add(t, g_ticks + (timeout ? timeout : 1u));
    sched_pend_switch();
    irq_unlock(key); /* PendSV runs here */
    return t->wait_result;
}

void sched_wake(task_t *t, int result)
{
    if (t->state != TASK_BLOCKED)
        return;
    if (t->waiting_on) {
        list_del(&t->rq_node);
        t->waiting_on = nullptr;
    }
    sleepq_del(t);
    struct mutex *m = t->blocked_on_mutex;
    t->blocked_on_mutex = nullptr;
    t->wait_result = result;
    sched_ready(t);
    if (m)
        mutex_waiter_changed(m); /* a waiter left: owner's inherited priority may drop */
}

void sched_set_prio(task_t *t, int prio)
{
    if (t->prio == prio)
        return;
    if (t->state == TASK_READY) {
        rq_del(t);
        t->prio = (uint8_t)prio;
        if (t == g_current)
            list_add(&t->rq_node, &s_ready[t->prio]); /* keep running at the head */
        else
            list_add_tail(&t->rq_node, &s_ready[t->prio]);
        s_ready_bitmap |= 1u << t->prio;
        if (t == g_current || t->prio > g_current->prio)
            sched_pend_switch();
    } else if (t->state == TASK_BLOCKED && t->waiting_on) {
        struct wait_queue *wq = t->waiting_on;
        list_del(&t->rq_node);
        t->prio = (uint8_t)prio;
        wq_insert(wq, t);
        if (t->blocked_on_mutex)
            mutex_waiter_changed(t->blocked_on_mutex);
    } else {
        t->prio = (uint8_t)prio;
    }
}

/* CPU time of the running task since it was last accounted (irq locked). A task is
 * accounted at least every tick, so a difference of half the counter's range can only be
 * a counter that went wrong: it is not counted. */
static KERNEL_FAST void account(task_t *t, uint32_t now)
{
    uint32_t d = now - t->switch_in;
    if (likely(d < 0x80000000u))
        t->cycles += d;
}

/* ---- the context switch decision (called from PendSV with irq locked) ----------- */

extern "C" KERNEL_FAST void sched_switch(void)
{
    task_t *prev = g_current;
    uint32_t now = cpu_cycles();

    if (prev) {
        account(prev, now);
        if (unlikely(prev->flags & TF_REDIRECT)) { /* saved context is unusable */
            prev->flags &= ~TF_REDIRECT;
            task_redirect_to_exit(prev, -EFAULT);
        }
        if (s_lock_count && prev->state == TASK_READY) {
            s_switch_deferred = 1;
            prev->switch_in = now;
            return;
        }
    }
    int top = 31 - __builtin_clz(s_ready_bitmap); /* idle is always ready */
    task_t *next = list_first_entry(&s_ready[top], task_t, rq_node);
    if (next != prev) {
        next->nswitch++; /* (the slice goes on where it was: see the top of the file) */
        mpu_switch(prev, next);
        g_current = next;
    }
    next->switch_in = now;
}

/* Called from PendSV when the outgoing task's stack has no room for its context */
extern "C" void sched_context_overflow(task_t *t)
{
    printk("*** stack overflow: task %d '%s' (no room to save its context below %08lx) - %s killed\n",
           t->id, t->name, (unsigned long)t->stack_limit, t->proc ? "process" : "task");
#if CONFIG_OS
    if (t->proc)
        proc_kill(t->proc, -EFAULT); /* the other threads of the process */
#endif
    task_redirect_to_exit(t, -EFAULT);
}

/* ---- tick ------------------------------------------------------------------------- */

/* Start the processor cycle counter (DWT CYCCNT): CPU time accounting and short waits use it */
static void cycles_enable(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    *(volatile uint32_t *)0xE0001FB0u = 0xC5ACCE55u; /* DWT lock access (not in this CMSIS) */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Where the SysTick period running now started (cycle counter) and how far it has got;
 * returns how many periods have started since the last counted one (irq locked). The
 * counter does not count while a debugger has switched it off: then fewer come out. */
static KERNEL_FAST uint32_t periods_since_tick(uint32_t *start, uint32_t *elapsed)
{
    uint32_t period = SysTick->LOAD + 1u, v1, v2, cyc;
    do {
        v1 = SysTick->VAL;
        cyc = cpu_cycles();
        v2 = SysTick->VAL;
    } while (v2 > v1); /* reloaded in between */
    *elapsed = SysTick->LOAD - v2;
    *start = cyc - *elapsed;
    uint32_t gap = *start - s_tick_cyc;
    return gap < 0x80000000u ? (gap + period / 2) / period : 0;
}

KERNEL_FAST void sched_tick(void)
{
    uint32_t key = irq_lock();
    /* A debugger that disconnects clears DEMCR (pyOCD writes 0), which stops the counter:
     * waits measured with it would never end. Start it again. */
    if (unlikely(!(CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk) || !(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)))
        cycles_enable();
    uint32_t start, elapsed;
    uint32_t n = periods_since_tick(&start, &elapsed);
    uint32_t cyc = start + elapsed;
    if (unlikely(n > 1)) { /* held off: the ticks that went by count too */
        uint32_t gap = cyc - s_tick_cyc;
        s_tick_stats.late++;
        s_tick_stats.lost += n - 1;
        if (gap > s_tick_stats.max_gap_cycles) {
            s_tick_stats.max_gap_cycles = gap;
            s_tick_stats.max_gap_at = g_ticks;
            strncpy(s_tick_stats.max_gap_task, g_current ? g_current->name : "-", sizeof(s_tick_stats.max_gap_task) - 1);
        }
    } else {
        n = 1; /* (0: the counter was off for a while) */
    }
    s_tick_cyc = start;
    uint32_t prev = g_ticks, now = prev + n;
    g_ticks = now;
    if (now < prev)
        s_ticks_hi++;

    while (!list_empty(&s_sleepq)) {
        task_t *t = list_first_entry(&s_sleepq, task_t, sl_node);
        if ((int32_t)(now - t->wake_tick) < 0)
            break;
        sched_wake(t, -ETIMEDOUT);
    }
    timer_tick(now);

    task_t *c = g_current;
    if (c) {
        /* account CPU time every tick: the 32-bit cycle counter wraps after ~7 s */
        account(c, cyc);
        c->switch_in = cyc;
    }
    if (c && c->state == TASK_READY && c->prio != PRIO_IDLE && (c->slice <= 1 || --c->slice == 0)) {
        c->slice = CONFIG_TIMESLICE_TICKS;
        if (!list_is_singular(&s_ready[c->prio])) {
            list_move_tail(&c->rq_node, &s_ready[c->prio]);
            sched_pend_switch();
        }
    }
    irq_unlock(key);
}

uint32_t tick_get(void)
{
    return g_ticks;
}

uint64_t tick_get64(void)
{
    uint32_t hi, lo;
    do {
        hi = s_ticks_hi;
        lo = g_ticks;
    } while (hi != s_ticks_hi);
    return ((uint64_t)hi << 32) | lo;
}

static_assert(CONFIG_CORE_CLOCK_HZ / CONFIG_TICK_HZ <= 0xFFFFFFFFu / 1000u, "time_us: 32-bit fraction");

uint64_t time_us(void)
{
    if (unlikely(!s_started)) /* (SysTick is not set up yet) */
        return 0;
    uint32_t period = SysTick->LOAD + 1u, start, elapsed;
    uint32_t key = irq_lock();
    uint64_t ms = tick_get64();
    /* periods not counted yet: the tick is pending, maybe held off for a while */
    uint32_t n = periods_since_tick(&start, &elapsed);
    if (!n && (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk))
        n = 1; /* (the cycle counter was off) */
    irq_unlock(key);
    return (ms + n) * 1000u + elapsed * 1000u / period; /* (32 bits: elapsed < period) */
}

void sched_tick_stats(struct tick_stats *out)
{
    uint32_t key = irq_lock();
    *out = s_tick_stats;
    irq_unlock(key);
}

/* ---- tasks ------------------------------------------------------------------------ */

task_t *task_current(void) { return g_current; }
int *task_errno_ptr(void) { return &g_current->net_errno; }
const char *task_name(const task_t *t) { return t ? t->name : "?"; }
int task_id(const task_t *t) { return t ? t->id : -1; }
int task_priority(const task_t *t) { return t->prio; }

task_t *task_create_raw(const char *name, int prio, uint32_t flags)
{
    task_t *t = (task_t *)kzalloc(sizeof(task_t), KM_FAST);
    if (!t)
        return nullptr;
    if (prio < 0)
        prio = 0;
    if (prio >= CONFIG_NUM_PRIO)
        prio = CONFIG_NUM_PRIO - 1;
    t->prio = t->base_prio = (uint8_t)prio;
    t->flags = flags;
    t->state = TASK_SUSPENDED;
    list_init(&t->rq_node);
    list_init(&t->sl_node);
    list_init(&t->proc_node);
    list_init(&t->mutexes_held);
    strncpy(t->name, name ? name : "task", sizeof(t->name) - 1);
    uint32_t key = irq_lock();
    t->id = s_next_id++;
    list_add_tail(&t->all_node, &s_all);
    irq_unlock(key);
    return t;
}

/* Build an exception return frame + our saved-context frame (see PendSV) */
uint32_t *task_build_frame(uint8_t *stack_top, uint32_t pc, uint32_t r0, uint32_t control, uint32_t lr)
{
    uint32_t *sp = (uint32_t *)((uintptr_t)stack_top & ~7u);
    *--sp = 0x01000000u;  /* xPSR: Thumb */
    *--sp = pc & ~1u;     /* PC */
    *--sp = lr;           /* LR */
    *--sp = 0;            /* R12 */
    *--sp = 0;            /* R3 */
    *--sp = 0;            /* R2 */
    *--sp = 0;            /* R1 */
    *--sp = r0;           /* R0 */
    *--sp = 0xFFFFFFFDu;  /* EXC_RETURN: thread mode, PSP, basic frame */
    for (int i = 0; i < 8; i++)
        *--sp = 0;        /* R11..R4 */
    *--sp = control;      /* CONTROL.nPRIV */
    return sp;
}

static void kthread_start(void *arg)
{
    task_t *t = (task_t *)arg;
    t->entry(t->arg);
    task_exit(0);
}

void task_start_kernel(task_t *t, task_fn_t fn, void *arg)
{
    t->entry = fn;
    t->arg = arg;
    t->sp = task_build_frame(t->kstack + t->kstack_size, (uint32_t)kthread_start, (uint32_t)t, 0, 0);
}

static void kill_trampoline(void *arg)
{
    task_exit((int)(intptr_t)arg);
}

/*
 * Discard a task's current context and make it run task_exit() in kernel mode on the top
 * of its kernel stack. Used for kills and faults; the task must not be running (or it is
 * the faulting task, whose context the fault handler replaces).
 */
void task_redirect_to_exit(task_t *t, int code)
{
    t->flags |= TF_KILLED;
    t->flags &= ~TF_IN_SYSCALL;
    t->sp = task_build_frame(t->kstack + t->kstack_size, (uint32_t)kill_trampoline, (uint32_t)code, 0, 0);
    mpu_set_guard(t, t->kstack);
}

void task_fill_stack(uint8_t *stack, uint32_t size)
{
    uint32_t *p = (uint32_t *)stack;
    for (uint32_t i = 0; i < size / 4; i++)
        p[i] = STACK_FILL;
}

/* The guard at the bottom is never readable while the owner runs: start above it */
uint32_t stack_unused(const uint8_t *base, uint32_t size)
{
    const uint32_t *p = (const uint32_t *)(base + CONFIG_STACK_GUARD);
    const uint32_t *end = (const uint32_t *)(base + size);
    uint32_t n = CONFIG_STACK_GUARD;
    while (p < end && *p == STACK_FILL) {
        p++;
        n += 4;
    }
    return n;
}

task_t *kthread_create(const char *name, task_fn_t fn, void *arg, int prio, size_t stack_size)
{
    if (!stack_size)
        stack_size = CONFIG_KTHREAD_STACK;
    stack_size = ALIGN_UP(stack_size, 32u) + CONFIG_STACK_GUARD; /* usable size + guard */
    task_t *t = task_create_raw(name, prio, TF_KTHREAD);
    if (!t)
        return nullptr;
    t->kstack = (uint8_t *)kmalloc_aligned(stack_size, CONFIG_STACK_GUARD, KM_FAST);
    if (!t->kstack) {
        uint32_t key = irq_lock();
        list_del(&t->all_node);
        irq_unlock(key);
        kfree(t);
        return nullptr;
    }
    t->kstack_size = stack_size;
    task_fill_stack(t->kstack, stack_size);
    mpu_set_guard(t, t->kstack);
    task_start_kernel(t, fn, arg);
    uint32_t key = irq_lock();
    sched_ready(t);
    irq_unlock(key);
    return t;
}

void task_exit(int code)
{
    task_t *t = g_current;
#if CONFIG_OS
    if (t->proc)
        proc_thread_exited(t, code);
#endif
    mutex_release_all(t);

    uint32_t key = irq_lock();
    t->exit_code = code;
    rq_del(t);
    sleepq_del(t);
    t->state = TASK_DEAD;
    t->flags |= TF_NOSAVE;
    list_add_tail(&t->rq_node, &s_dead);
    sched_pend_switch();
    kworker_queue(nullptr, nullptr); /* wake the reaper */
    irq_unlock(key);
    for (;;)
        cpu_wfi();
}

/* Free dead tasks (runs in the kworker thread, never on the dead task's stack) */
void sched_reap(void)
{
    for (;;) {
        uint32_t key = irq_lock();
        if (list_empty(&s_dead)) {
            irq_unlock(key);
            return;
        }
        task_t *t = list_first_entry(&s_dead, task_t, rq_node);
        list_del(&t->rq_node);
        list_del(&t->all_node);
        irq_unlock(key);
        kfree(t->kstack);
        kfree(t);
    }
}

int task_kill(task_t *t)
{
    if (!t || t == s_idle)
        return -EINVAL;
    if (t == g_current && !in_interrupt())
        task_exit(-EINTR);

    uint32_t key = irq_lock();
    if (t->state == TASK_DEAD) {
        irq_unlock(key);
        return 0;
    }
    t->flags |= TF_KILLED;
    if (t->state == TASK_BLOCKED) {
        sched_wake(t, -EINTR);          /* blocking call returns -EINTR, syscall exit ends it */
    } else if ((t->flags & TF_USER) && !(t->flags & TF_IN_SYSCALL) && t != g_current) {
        task_redirect_to_exit(t, -EINTR); /* preempted in user code: exit when scheduled */
        if (t->state == TASK_SUSPENDED)
            sched_ready(t);
    }
    irq_unlock(key);
    return 0;
}

int task_set_priority(task_t *t, int prio)
{
    if (prio < 1 || prio >= CONFIG_NUM_PRIO)
        return -EINVAL;
    uint32_t key = irq_lock();
    t->base_prio = (uint8_t)prio;
    /* Keep an inherited (higher) priority until the mutexes are released */
    int eff = prio;
    struct list_head *pos;
    list_for_each(pos, &t->mutexes_held) {
        struct mutex *m = list_entry(pos, struct mutex, held_node);
        if (!list_empty(&m->wq.waiters)) {
            task_t *w = list_first_entry(&m->wq.waiters, task_t, rq_node);
            if (w->prio > eff)
                eff = w->prio;
        }
    }
    sched_set_prio(t, eff);
    irq_unlock(key);
    return 0;
}

void task_yield(void)
{
    uint32_t key = irq_lock();
    task_t *c = g_current;
    if (!list_is_singular(&s_ready[c->prio])) {
        c->slice = CONFIG_TIMESLICE_TICKS; /* a whole one when its turn comes again */
        list_move_tail(&c->rq_node, &s_ready[c->prio]);
        sched_pend_switch();
    }
    irq_unlock(key);
}

int task_sleep_ticks(uint32_t ticks)
{
    uint32_t key = irq_lock();
    int r = sched_block(nullptr, ticks ? ticks : 1u, key);
    return r == -ETIMEDOUT ? 0 : r;
}

int task_sleep_ms(uint32_t ms)
{
    return task_sleep_ticks(ms_to_ticks(ms));
}

void sched_lock(void)
{
    uint32_t key = irq_lock();
    s_lock_count++;
    irq_unlock(key);
}

void sched_unlock(void)
{
    uint32_t key = irq_lock();
    if (s_lock_count && --s_lock_count == 0 && s_switch_deferred) {
        s_switch_deferred = 0;
        sched_pend_switch();
    }
    irq_unlock(key);
}

int task_should_stop(void)
{
    return (g_current->flags & TF_KILLED) != 0;
}

int kthread_stop(task_t *t)
{
    int id = t->id;
    task_kill(t);
    for (int i = 0; i < 2000; i++) {
        sched_lock();
        task_t *x = task_find(id);
        sched_unlock();
        if (!x)
            return 0; /* exited: no longer runs any of its own code */
        task_sleep_ms(2);
    }
    return -ETIMEDOUT;
}

int sched_is_locked(void)
{
    return s_lock_count != 0;
}

task_t *sched_idle_task(void)
{
    return s_idle;
}

task_t *task_find(int id)
{
    struct list_head *pos;
    list_for_each(pos, &s_all) {
        task_t *t = list_entry(pos, task_t, all_node);
        if (t->id == id && t->state != TASK_DEAD)
            return t;
    }
    return nullptr;
}

void task_foreach(void (*fn)(task_t *t, void *ctx), void *ctx)
{
    /* Snapshot under the lock would need allocation; walk with preemption disabled instead.
     * Tasks are only freed by the reaper, which cannot run while we hold the scheduler. */
    sched_lock();
    struct list_head *pos;
    list_for_each(pos, &s_all)
        fn(list_entry(pos, task_t, all_node), ctx);
    sched_unlock();
}

/* ---- startup ----------------------------------------------------------------------- */

static void idle_task(void *)
{
    for (;;)
        cpu_wfi();
}

void sched_init(void)
{
    for (int i = 0; i < CONFIG_NUM_PRIO; i++)
        list_init(&s_ready[i]);
    /* Exception priorities: faults highest (fixed/0), SVC, then PendSV/SysTick lowest */
    NVIC_SetPriority(SVCall_IRQn, 15);
    NVIC_SetPriority(PendSV_IRQn, 15);
    NVIC_SetPriority(SysTick_IRQn, 15);
    s_idle = kthread_create("idle", idle_task, nullptr, PRIO_IDLE, 512);
    if (!s_idle)
        panic("cannot create idle task");
}

extern "C" void sched_start_asm(void) __attribute__((noreturn));

void sched_start(void)
{
    /* Cycle counter for statistics */
    cycles_enable();
    DWT->CYCCNT = 0;

    SysTick->LOAD = SystemCoreClock / CONFIG_TICK_HZ - 1u;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_TICKINT_Msk | SysTick_CTRL_ENABLE_Msk;
    g_current = nullptr;
    s_started = true;
    sched_start_asm();
}
