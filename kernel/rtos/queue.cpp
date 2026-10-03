/*
 * kernel/rtos/queue.cpp - message queues (crtos/queue.h).
 *
 * A ring of fixed-size items with two wait queues: receivers wait for an item, senders for
 * room. A task woken by the other side tries again (another task of higher priority may have
 * been quicker), so the wait goes on until its own deadline.
 */
#include "kernel.h"
#include <crtos/queue.h>
#include <string.h>

int queue_init(struct queue *q, void *buf, uint32_t item_size, uint32_t count)
{
    if (!q || !buf || !item_size || !count || item_size > 0xFFFFFFFFu / count)
        return -EINVAL;
    q->buf = (uint8_t *)buf;
    q->item_size = item_size;
    q->count = count;
    q->head = q->tail = 0;
    wq_init(&q->rx);
    wq_init(&q->tx);
    q->allocated = 0;
    return 0;
}

struct queue *queue_create(uint32_t item_size, uint32_t count)
{
    if (!item_size || !count || item_size > (0x7FFFFFFFu - sizeof(struct queue)) / count)
        return nullptr;
    struct queue *q = (struct queue *)kmalloc(sizeof(*q) + item_size * count, KM_ANY);
    if (!q)
        return nullptr;
    queue_init(q, q + 1, item_size, count);
    q->allocated = 1;
    return q;
}

void queue_delete(struct queue *q)
{
    if (!q)
        return;
    uint32_t key = irq_lock();
    wq_wake_all(&q->rx, -ECANCELED);
    wq_wake_all(&q->tx, -ECANCELED);
    irq_unlock(key);
    if (q->allocated)
        kfree(q);
}

/* Ticks left until @deadline (at least 1 while it has not passed), 0 when it has */
static uint32_t left(uint32_t deadline)
{
    int32_t d = (int32_t)(deadline - tick_get());
    return d > 0 ? (uint32_t)d : 0u;
}

int queue_send(struct queue *q, const void *item, uint32_t timeout)
{
    uint32_t deadline = tick_get() + timeout;
    for (;;) {
        uint32_t key = irq_lock();
        if (q->head - q->tail < q->count) {
            memcpy(q->buf + (q->head % q->count) * q->item_size, item, q->item_size);
            q->head++;
            wq_wake_one(&q->rx, 0);
            irq_unlock(key);
            return 0;
        }
        uint32_t wait = timeout == WAIT_FOREVER ? WAIT_FOREVER : left(deadline);
        if (wait == NO_WAIT || in_interrupt()) {
            irq_unlock(key);
            return -ETIMEDOUT;
        }
        int r = sched_block(&q->tx, wait, key);
        if (r)
            return r; /* -ETIMEDOUT, -EINTR, -ECANCELED */
    }
}

int queue_recv(struct queue *q, void *item, uint32_t timeout)
{
    uint32_t deadline = tick_get() + timeout;
    for (;;) {
        uint32_t key = irq_lock();
        if (q->head != q->tail) {
            memcpy(item, q->buf + (q->tail % q->count) * q->item_size, q->item_size);
            q->tail++;
            wq_wake_one(&q->tx, 0);
            irq_unlock(key);
            return 0;
        }
        uint32_t wait = timeout == WAIT_FOREVER ? WAIT_FOREVER : left(deadline);
        if (wait == NO_WAIT || in_interrupt()) {
            irq_unlock(key);
            return -ETIMEDOUT;
        }
        int r = sched_block(&q->rx, wait, key);
        if (r)
            return r;
    }
}

uint32_t queue_count(const struct queue *q)
{
    return q->head - q->tail;
}
