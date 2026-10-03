/*
 * crtos/queue.h - message queues: a fixed number of items of a fixed size, copied in and out.
 *
 * Senders wait while the queue is full, receivers while it is empty (timeouts in ticks of
 * 1 ms, WAIT_FOREVER / NO_WAIT from crtos/sched.h); the waiting tasks of the highest priority
 * go first. An interrupt handler may send and receive with NO_WAIT only (another timeout is
 * taken as NO_WAIT there). An item is copied with interrupts held off, so keep items small (a
 * few words; a pointer to a bigger buffer otherwise).
 */
#ifndef CRTOS_QUEUE_H
#define CRTOS_QUEUE_H

#include <stdint.h>
#include <crtos/sync.h>

#ifdef __cplusplus
extern "C" {
#endif

struct queue {
    uint8_t *buf;               /* count items of item_size bytes */
    uint32_t item_size, count;
    uint32_t head, tail;        /* items put, items taken (counting on, wrapping) */
    struct wait_queue rx;       /* receivers waiting for an item */
    struct wait_queue tx;       /* senders waiting for room */
    uint8_t allocated;          /* queue_create() made it: queue_delete() frees it */
};

/* A queue in memory of the caller: @buf holds @count items of @item_size bytes. 0 or -EINVAL. */
int queue_init(struct queue *q, void *buf, uint32_t item_size, uint32_t count);
/* A queue and its buffer from the kernel heap (NULL: no memory, bad sizes) */
struct queue *queue_create(uint32_t item_size, uint32_t count);
/* The end of a queue_create() queue; tasks still waiting on it get -ECANCELED */
void queue_delete(struct queue *q);

/* Copy @item to the end of the queue: 0, -ETIMEDOUT (still full), -EINTR (the task is ended) */
int queue_send(struct queue *q, const void *item, uint32_t timeout);
/* Copy the first item to @item and take it out: 0, -ETIMEDOUT (still empty), -EINTR */
int queue_recv(struct queue *q, void *item, uint32_t timeout);
/* Items waiting now */
uint32_t queue_count(const struct queue *q);

#ifdef __cplusplus
}
#endif

#endif
