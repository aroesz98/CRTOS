/*
 * crtos/sync.h - blocking synchronisation primitives.
 *
 * All objects can be embedded in other structures and are initialised with *_init().
 * Timeouts are in ticks (1 ms); WAIT_FOREVER / NO_WAIT from crtos/sched.h.
 * sem_give(), event_set() and completion_done() may be called from interrupts.
 */
#ifndef CRTOS_SYNC_H
#define CRTOS_SYNC_H

#include <stdint.h>
#include <crtos/list.h>
#include <crtos/sched.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tasks blocked on an object, highest priority first */
struct wait_queue {
    struct list_head waiters;
};

void wq_init(struct wait_queue *wq);
/* Wake the highest priority waiter / all waiters with the given wait result. */
int wq_wake_one(struct wait_queue *wq, int result);
int wq_wake_all(struct wait_queue *wq, int result);

/* Mutex with priority inheritance and recursive locking by the owner */
struct mutex {
    task_t *owner;
    uint32_t count;
    struct wait_queue wq;
    struct list_head held_node;   /* in owner's list of held mutexes */
};

/* a mutex defined statically: static struct mutex m = MUTEX_INIT(m); */
#define MUTEX_INIT(m) { 0, 0, { LIST_HEAD_INIT((m).wq.waiters) }, LIST_HEAD_INIT((m).held_node) }

void mutex_init(struct mutex *m);
int mutex_lock(struct mutex *m, uint32_t timeout);  /* 0, -ETIMEDOUT, -EINTR */
int mutex_trylock(struct mutex *m);
void mutex_unlock(struct mutex *m);

/* Counting semaphore */
struct semaphore {
    int32_t count;
    int32_t max;
    struct wait_queue wq;
};

void sem_init(struct semaphore *s, int32_t initial, int32_t max);
int sem_take(struct semaphore *s, uint32_t timeout);   /* 0, -ETIMEDOUT, -EINTR */
void sem_give(struct semaphore *s);

/* Event flags: wait for any/all of a set of bits */
struct event {
    uint32_t bits;
    struct wait_queue wq;
};

#define EVENT_ANY   0u
#define EVENT_ALL   1u
#define EVENT_CLEAR 2u   /* clear the matched bits on success */

void event_init(struct event *e);
void event_set(struct event *e, uint32_t bits);
void event_clear(struct event *e, uint32_t bits);
/* Returns the matching bits (>0) or -ETIMEDOUT / -EINTR */
int32_t event_wait(struct event *e, uint32_t bits, uint32_t mode, uint32_t timeout);

#ifdef __cplusplus
}
#endif

#endif
