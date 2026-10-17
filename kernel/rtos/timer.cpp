/*
 * kernel/rtos/timer.cpp - software timers (crtos/timer.h).
 *
 * The armed timers are a list sorted by the tick they are due at. The tick interrupt only
 * compares the first one's (timer_tick, O(1)) and wakes the thread "ktimer" when it is due;
 * the thread takes the due timers off the list, arms the periodic ones again and runs their
 * functions - in thread context, so a function may lock a mutex or send to a queue. A periodic
 * timer that fell behind by more than a period (its function or a higher priority took too
 * long) skips the runs it missed instead of catching up in a burst.
 */
#include "kernel.h"
#include <crtos/timer.h>

static struct list_head s_armed = LIST_HEAD_INIT(s_armed);
static volatile uint32_t s_next;    /* due tick of the first armed timer */
static volatile bool s_any;         /* there is one (the thread looks after it) */
static struct semaphore s_due;

/* Into the sorted list (irq locked); the first one's tick for timer_tick() */
static void insert(struct timer *t)
{
    struct list_head *pos;
    list_for_each(pos, &s_armed) {
        struct timer *o = list_entry(pos, struct timer, node);
        if ((int32_t)(t->due - o->due) < 0)
            break;
    }
    list_insert_between(&t->node, pos->prev, pos);
    s_next = list_first_entry(&s_armed, struct timer, node)->due;
    s_any = true;
}

void timer_init(struct timer *t, timer_fn_t fn, void *arg)
{
    list_init(&t->node);
    t->due = 0;
    t->period = 0;
    t->fn = fn;
    t->arg = arg;
    t->armed = false;
}

void timer_start(struct timer *t, uint32_t delay_ms, uint32_t period_ms)
{
    uint32_t key = irq_lock();
    if (t->armed)
        list_del(&t->node);
    t->due = tick_get() + (delay_ms ? delay_ms : 1u);
    t->period = period_ms;
    t->armed = true;
    insert(t);
    irq_unlock(key);
}

bool timer_stop(struct timer *t)
{
    uint32_t key = irq_lock();
    bool was = t->armed;
    if (was) {
        list_del(&t->node);
        t->armed = false;
        s_any = !list_empty(&s_armed);
        if (s_any)
            s_next = list_first_entry(&s_armed, struct timer, node)->due;
    }
    irq_unlock(key);
    return was;
}

bool timer_active(const struct timer *t)
{
    return t->armed;
}

/* From the tick interrupt (irq locked) */
KERNEL_FAST void timer_tick(uint32_t now)
{
    if (s_any && (int32_t)(now - s_next) >= 0) {
        s_any = false; /* (the thread sets it again for the next one) */
        sem_give(&s_due);
    }
}

static void timer_thread(void *)
{
    for (;;) {
        sem_take(&s_due, WAIT_FOREVER);
        for (;;) {
            uint32_t key = irq_lock();
            uint32_t now = tick_get();
            if (list_empty(&s_armed)) {
                irq_unlock(key);
                break;
            }
            struct timer *t = list_first_entry(&s_armed, struct timer, node);
            if ((int32_t)(now - t->due) < 0) { /* the next one is not due yet */
                s_next = t->due;
                s_any = true;
                irq_unlock(key);
                break;
            }
            list_del(&t->node);
            if (t->period) {
                t->due += t->period;
                if ((int32_t)(now - t->due) >= 0) /* behind by a period or more: skip */
                    t->due = now + t->period;
                insert(t);
            } else {
                t->armed = false;
            }
            timer_fn_t fn = t->fn;
            void *arg = t->arg;
            irq_unlock(key);
            fn(arg);
        }
    }
}

void timers_start(void)
{
    sem_init(&s_due, 0, 1);
    if (!kthread_create("ktimer", timer_thread, nullptr, CONFIG_TIMER_PRIO, CONFIG_TIMER_STACK))
        panic("cannot start the timer thread");
}
