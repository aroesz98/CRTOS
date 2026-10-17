/*
 * kernel/rtos/sync.cpp - mutexes (with priority inheritance), semaphores, event flags.
 */
#include "kernel.h"

/* ---- mutex ---------------------------------------------------------------------- */

void mutex_init(struct mutex *m)
{
    m->owner = nullptr;
    m->count = 0;
    wq_init(&m->wq);
    list_init(&m->held_node);
}

/* Effective priority = max(base priority, top waiter of every mutex the task holds) */
static void recompute_prio(task_t *t)
{
    int prio = t->base_prio;
    struct list_head *pos;
    list_for_each(pos, &t->mutexes_held) {
        struct mutex *m = list_entry(pos, struct mutex, held_node);
        if (!list_empty(&m->wq.waiters)) {
            task_t *w = list_first_entry(&m->wq.waiters, task_t, rq_node);
            if (w->prio > prio)
                prio = w->prio;
        }
    }
    sched_set_prio(t, prio);
}

/* Boost the owner chain of @m to at least @prio (irq locked) */
static void pi_boost(struct mutex *m, int prio)
{
    for (int depth = 0; m && m->owner && depth < 8; depth++) {
        task_t *owner = m->owner;
        if (owner->prio >= prio)
            return;
        sched_set_prio(owner, prio);
        m = owner->blocked_on_mutex;
    }
}

void mutex_waiter_changed(struct mutex *m)
{
    if (m->owner)
        recompute_prio(m->owner);
}

int mutex_lock(struct mutex *m, uint32_t timeout)
{
    task_t *t = g_current;
    uint32_t key = irq_lock();
    if (!m->owner) {
        m->owner = t;
        m->count = 1;
        list_add(&m->held_node, &t->mutexes_held);
        irq_unlock(key);
        return 0;
    }
    if (m->owner == t) {
        m->count++;
        irq_unlock(key);
        return 0;
    }
    if (timeout == NO_WAIT) {
        irq_unlock(key);
        return -ETIMEDOUT;
    }
    t->blocked_on_mutex = m;
    pi_boost(m, t->prio);
    /* On success mutex_unlock() hands ownership over and wakes us with 0 */
    int r = sched_block(&m->wq, timeout, key);
    if (r != 0) {
        key = irq_lock();
        t->blocked_on_mutex = nullptr;
        irq_unlock(key);
    }
    return r;
}

int mutex_trylock(struct mutex *m)
{
    return mutex_lock(m, NO_WAIT) == 0;
}

static void mutex_release(struct mutex *m, task_t *t)
{
    list_del(&m->held_node);
    if (!list_empty(&m->wq.waiters)) {
        task_t *w = list_first_entry(&m->wq.waiters, task_t, rq_node);
        m->owner = w;
        m->count = 1;
        list_add(&m->held_node, &w->mutexes_held);
        w->blocked_on_mutex = nullptr;
        sched_wake(w, 0);
        recompute_prio(w);
    } else {
        m->owner = nullptr;
        m->count = 0;
    }
    recompute_prio(t);
}

void mutex_unlock(struct mutex *m)
{
    task_t *t = g_current;
    uint32_t key = irq_lock();
    if (m->owner != t) {
        irq_unlock(key);
        panic("mutex_unlock by non-owner '%s'", t ? t->name : "?");
    }
    if (--m->count == 0)
        mutex_release(m, t);
    irq_unlock(key);
}

/* Release every mutex held by an exiting task so waiters do not deadlock */
void mutex_release_all(task_t *t)
{
    uint32_t key = irq_lock();
    while (!list_empty(&t->mutexes_held)) {
        struct mutex *m = list_first_entry(&t->mutexes_held, struct mutex, held_node);
        m->count = 1;
        mutex_release(m, t);
    }
    irq_unlock(key);
}

/* ---- semaphore ------------------------------------------------------------------- */

void sem_init(struct semaphore *s, int32_t initial, int32_t max)
{
    s->count = initial;
    s->max = max > 0 ? max : 1;
    wq_init(&s->wq);
}

int sem_take(struct semaphore *s, uint32_t timeout)
{
    uint32_t key = irq_lock();
    if (s->count > 0) {
        s->count--;
        irq_unlock(key);
        return 0;
    }
    if (timeout == NO_WAIT) {
        irq_unlock(key);
        return -ETIMEDOUT;
    }
    /* sem_give() hands the unit directly to the woken task (result 0) */
    return sched_block(&s->wq, timeout, key);
}

void sem_give(struct semaphore *s)
{
    uint32_t key = irq_lock();
    if (!list_empty(&s->wq.waiters))
        sched_wake(list_first_entry(&s->wq.waiters, task_t, rq_node), 0);
    else if (s->count < s->max)
        s->count++;
    irq_unlock(key);
}

/* ---- event flags ------------------------------------------------------------------ */

void event_init(struct event *e)
{
    e->bits = 0;
    wq_init(&e->wq);
}

void event_set(struct event *e, uint32_t bits)
{
    uint32_t key = irq_lock();
    e->bits |= bits;
    while (!list_empty(&e->wq.waiters)) /* waiters re-check their condition */
        sched_wake(list_first_entry(&e->wq.waiters, task_t, rq_node), 0);
    irq_unlock(key);
}

void event_clear(struct event *e, uint32_t bits)
{
    uint32_t key = irq_lock();
    e->bits &= ~bits;
    irq_unlock(key);
}

int32_t event_wait(struct event *e, uint32_t bits, uint32_t mode, uint32_t timeout)
{
    uint32_t deadline = g_ticks + timeout;
    bits &= 0x7FFFFFFFu; /* result must stay positive */
    for (;;) {
        uint32_t key = irq_lock();
        uint32_t match = e->bits & bits;
        bool ok = (mode & EVENT_ALL) ? (match == bits) : (match != 0);
        if (ok) {
            if (mode & EVENT_CLEAR)
                e->bits &= ~match;
            irq_unlock(key);
            return (int32_t)match;
        }
        uint32_t left;
        if (timeout == WAIT_FOREVER) {
            left = WAIT_FOREVER;
        } else {
            int32_t rem = (int32_t)(deadline - g_ticks);
            if (timeout == NO_WAIT || rem <= 0) {
                irq_unlock(key);
                return -ETIMEDOUT;
            }
            left = (uint32_t)rem;
        }
        int r = sched_block(&e->wq, left, key);
        if (r == -EINTR)
            return r;
    }
}
