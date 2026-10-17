/*
 * kernel/rtos/kworker.cpp - deferred work in thread context.
 *
 * Interrupts, fault handlers and exiting tasks queue short jobs here (freeing memory,
 * reaping dead tasks). The queue is a fixed ring, so queuing never allocates.
 */
#include "kernel.h"

#define KWORK_SLOTS 32u

struct kwork {
    void (*fn)(void *);
    void *arg;
};

static struct kwork s_q[KWORK_SLOTS];
static uint32_t s_head, s_tail, s_lost;
static struct semaphore s_sem;

void kworker_queue(void (*fn)(void *), void *arg)
{
    uint32_t key = irq_lock();
    if (fn) {
        if (s_head - s_tail < KWORK_SLOTS) {
            s_q[s_head % KWORK_SLOTS].fn = fn;
            s_q[s_head % KWORK_SLOTS].arg = arg;
            s_head++;
        } else {
            s_lost++;
        }
    }
    irq_unlock(key);
    sem_give(&s_sem);
}

static void kworker(void *)
{
    uint32_t reported = 0;
    for (;;) {
        sem_take(&s_sem, WAIT_FOREVER);
        sched_reap();
        for (;;) {
            uint32_t key = irq_lock();
            if (s_head == s_tail) {
                irq_unlock(key);
                break;
            }
            struct kwork w = s_q[s_tail % KWORK_SLOTS];
            s_tail++;
            irq_unlock(key);
            w.fn(w.arg);
        }
        if (s_lost != reported) {
            reported = s_lost;
            printk("E: kworker queue overflow, %lu jobs lost\n", (unsigned long)reported);
        }
    }
}

void kworker_init(void)
{
    sem_init(&s_sem, 0, 0x7FFFFFFF);
    if (!kthread_create("kworker", kworker, nullptr, PRIO_HIGH + 4, 2048))
        panic("cannot start kworker");
}
