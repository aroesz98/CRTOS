/*
 * tusb_os_custom.h - TinyUSB's OS abstraction (CFG_TUSB_OS = OPT_OS_CUSTOM) on the CRTOS
 * kernel: semaphores and mutexes are the kernel's, a spinlock masks interrupts, and a queue
 * is a ring of items guarded by irq_lock() with a counting semaphore for the waiting side.
 * Everything may be used from the USB interrupt except the waits.
 */
#ifndef TUSB_OS_CUSTOM_H
#define TUSB_OS_CUSTOM_H

#include <string.h>
#include <crtos/arch.h>
#include <crtos/sched.h>
#include <crtos/sync.h>

/* ---- task ------------------------------------------------------------------------------------ */

typedef void *osal_task_handle_t;

TU_ATTR_ALWAYS_INLINE static inline osal_task_handle_t osal_task_get_current_handle(void)
{
    return (osal_task_handle_t)task_current();
}

TU_ATTR_ALWAYS_INLINE static inline void osal_task_delay(uint32_t msec)
{
    task_sleep_ms(msec);
}

TU_ATTR_ALWAYS_INLINE static inline uint32_t osal_time_millis(void)
{
    return (uint32_t)(time_us() / 1000u);
}

/* ---- spinlock: interrupts masked (one core) ------------------------------------------------- */

typedef struct {
    uint32_t key;
} osal_spinlock_t;

#define OSAL_SPINLOCK_DEF(_name, _int_set) osal_spinlock_t _name = { 0 }

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_init(osal_spinlock_t *ctx)
{
    (void)ctx;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_deinit(osal_spinlock_t *ctx)
{
    (void)ctx;
}

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_lock(osal_spinlock_t *ctx, bool in_isr)
{
    if (!in_isr)
        ctx->key = irq_lock();
}

TU_ATTR_ALWAYS_INLINE static inline void osal_spin_unlock(osal_spinlock_t *ctx, bool in_isr)
{
    if (!in_isr)
        irq_unlock(ctx->key);
}

/* ---- binary semaphore ------------------------------------------------------------------------ */

typedef struct semaphore osal_semaphore_def_t;
typedef struct semaphore *osal_semaphore_t;

TU_ATTR_ALWAYS_INLINE static inline osal_semaphore_t osal_semaphore_create(osal_semaphore_def_t *semdef)
{
    sem_init(semdef, 0, 1);
    return semdef;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_delete(osal_semaphore_t sem)
{
    (void)sem;
    return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_post(osal_semaphore_t sem, bool in_isr)
{
    (void)in_isr;
    sem_give(sem);
    return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_semaphore_wait(osal_semaphore_t sem, uint32_t msec)
{
    return sem_take(sem, msec) == 0;    /* OSAL_TIMEOUT_WAIT_FOREVER == WAIT_FOREVER */
}

TU_ATTR_ALWAYS_INLINE static inline void osal_semaphore_reset(osal_semaphore_t sem)
{
    while (sem_take(sem, 0) == 0) {
    }
}

/* ---- mutex ----------------------------------------------------------------------------------- */

typedef struct mutex osal_mutex_def_t;
typedef struct mutex *osal_mutex_t;

TU_ATTR_ALWAYS_INLINE static inline osal_mutex_t osal_mutex_create(osal_mutex_def_t *mdef)
{
    mutex_init(mdef);
    return mdef;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_delete(osal_mutex_t mutex)
{
    (void)mutex;
    return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_lock(osal_mutex_t mutex, uint32_t msec)
{
    return mutex_lock(mutex, msec) == 0;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_mutex_unlock(osal_mutex_t mutex)
{
    mutex_unlock(mutex);
    return true;
}

/* ---- queue ----------------------------------------------------------------------------------- */

typedef struct {
    uint8_t *buf;
    uint16_t depth, item_size;
    volatile uint16_t rd, wr, count;
    struct semaphore items;             /* counts the queued items */
} osal_queue_def_t;

typedef osal_queue_def_t *osal_queue_t;

#define OSAL_QUEUE_DEF(_int_set, _name, _depth, _type)                                               \
    uint8_t _name##_buf[(_depth) * sizeof(_type)];                                                   \
    osal_queue_def_t _name = { .buf = _name##_buf, .depth = (_depth), .item_size = sizeof(_type) }

TU_ATTR_ALWAYS_INLINE static inline osal_queue_t osal_queue_create(osal_queue_def_t *q)
{
    q->rd = q->wr = q->count = 0;
    sem_init(&q->items, 0, (int32_t)q->depth);
    return q;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_delete(osal_queue_t q)
{
    (void)q;
    return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_receive(osal_queue_t q, void *data, uint32_t msec)
{
    if (sem_take(&q->items, msec))
        return false;
    uint32_t key = irq_lock();
    memcpy(data, q->buf + (uint32_t)q->rd * q->item_size, q->item_size);
    q->rd = (uint16_t)((q->rd + 1u) % q->depth);
    q->count--;
    irq_unlock(key);
    return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_send(osal_queue_t q, const void *data, bool in_isr)
{
    (void)in_isr;
    uint32_t key = irq_lock();
    if (q->count >= q->depth) {
        irq_unlock(key);
        return false;
    }
    memcpy(q->buf + (uint32_t)q->wr * q->item_size, data, q->item_size);
    q->wr = (uint16_t)((q->wr + 1u) % q->depth);
    q->count++;
    irq_unlock(key);
    sem_give(&q->items);
    return true;
}

TU_ATTR_ALWAYS_INLINE static inline bool osal_queue_empty(osal_queue_t q)
{
    return q->count == 0;
}

#endif
