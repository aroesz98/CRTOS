/*
 * thread.c - threads and futex-based locks, including the C library's locks (newlib's
 * retargetable locking), so malloc and stdio are safe with several threads.
 *
 * Mutex states: 0 free, 1 locked, 2 locked and somebody may sleep on it (the unlocking
 * thread then wakes one). Recursive: the owner may lock again.
 */
#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/lock.h>
#include <crtos.h>

#define GUARD 256u     /* the kernel puts a no-access MPU region at the bottom of every stack */

static inline int my_tid(void)
{
    return (int)__crtos_syscall(SYS_GETTID, 0, 0, 0, 0);
}

int crtos_mutex_trylock(crtos_mutex_t *m)
{
    int tid = my_tid();
    if (m->owner == tid) {
        m->count++;
        return 1;
    }
    uint32_t c = 0;
    if (!__atomic_compare_exchange_n(&m->state, &c, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return 0;
    m->owner = tid;
    m->count = 1;
    return 1;
}

void crtos_mutex_lock(crtos_mutex_t *m)
{
    int tid = my_tid();
    if (m->owner == tid) {
        m->count++;
        return;
    }
    uint32_t c = 0;
    if (!__atomic_compare_exchange_n(&m->state, &c, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        if (c != 2u)
            c = __atomic_exchange_n(&m->state, 2u, __ATOMIC_ACQUIRE);
        while (c != 0u) {
            crtos_futex_wait(&m->state, 2u, CRTOS_FOREVER);
            c = __atomic_exchange_n(&m->state, 2u, __ATOMIC_ACQUIRE);
        }
    }
    m->owner = tid;
    m->count = 1;
}

void crtos_mutex_unlock(crtos_mutex_t *m)
{
    if (--m->count > 0)
        return;
    m->owner = 0;
    if (__atomic_exchange_n(&m->state, 0u, __ATOMIC_RELEASE) == 2u)
        crtos_futex_wake(&m->state, 1);
}

/* ---- newlib locks --------------------------------------------------------------------------- */

struct __lock {
    crtos_mutex_t m;
};

struct __lock __lock___arc4random_mutex, __lock___at_quick_exit_mutex, __lock___atexit_recursive_mutex,
    __lock___dd_hash_mutex, __lock___env_recursive_mutex, __lock___malloc_recursive_mutex,
    __lock___sfp_recursive_mutex, __lock___tz_mutex;

void __retarget_lock_init(_LOCK_T *lock)
{
    *lock = (_LOCK_T)calloc(1, sizeof(struct __lock));
}

void __retarget_lock_init_recursive(_LOCK_T *lock)
{
    *lock = (_LOCK_T)calloc(1, sizeof(struct __lock));
}

void __retarget_lock_close(_LOCK_T lock)
{
    free(lock);
}

void __retarget_lock_close_recursive(_LOCK_T lock)
{
    free(lock);
}

void __retarget_lock_acquire(_LOCK_T lock)
{
    if (lock)
        crtos_mutex_lock(&lock->m);
}

void __retarget_lock_acquire_recursive(_LOCK_T lock)
{
    if (lock)
        crtos_mutex_lock(&lock->m);
}

int __retarget_lock_try_acquire(_LOCK_T lock)
{
    return lock ? crtos_mutex_trylock(&lock->m) : 1;
}

int __retarget_lock_try_acquire_recursive(_LOCK_T lock)
{
    return lock ? crtos_mutex_trylock(&lock->m) : 1;
}

void __retarget_lock_release(_LOCK_T lock)
{
    if (lock)
        crtos_mutex_unlock(&lock->m);
}

void __retarget_lock_release_recursive(_LOCK_T lock)
{
    if (lock)
        crtos_mutex_unlock(&lock->m);
}

/* ---- threads ---------------------------------------------------------------------------------- */

struct crtos_thread {
    void *(*fn)(void *);
    void *arg;
    void *ret;
    void *stack;
    int tid;
    volatile uint32_t done;     /* set by the kernel once the thread left user mode */
};

static void thread_entry(struct crtos_thread *t)
{
    t->ret = t->fn(t->arg);
    __crtos_syscall(SYS_THREAD_EXIT, 0, (long)&t->done, 0, 0);
    for (;;) {
    }
}

crtos_thread_t *crtos_thread_start(void *(*fn)(void *), void *arg, size_t stack_size, int prio)
{
    struct crtos_thread *t = (struct crtos_thread *)calloc(1, sizeof(*t));
    if (!t)
        return NULL;
    size_t size = ((stack_size ? stack_size : 8192u) + GUARD + GUARD - 1u) & ~(size_t)(GUARD - 1u);
    t->stack = memalign(GUARD, size);
    if (!t->stack) {
        free(t);
        errno = ENOMEM;
        return NULL;
    }
    t->fn = fn;
    t->arg = arg;
    long r = __crtos_syscall6(SYS_THREAD_CREATE, (long)thread_entry, (long)t, (long)t->stack, (long)size, prio, 0);
    if ((unsigned long)r >= (unsigned long)-4095) {
        errno = (int)-r;
        free(t->stack);
        free(t);
        return NULL;
    }
    t->tid = (int)r;
    return t;
}

void *crtos_thread_join(crtos_thread_t *t)
{
    while (!t->done)
        crtos_futex_wait(&t->done, 0, CRTOS_FOREVER);
    void *ret = t->ret;
    free(t->stack);
    free(t);
    return ret;
}

int crtos_thread_id(const crtos_thread_t *t)
{
    return t->tid;
}

int crtos_thread_done(const crtos_thread_t *t)
{
    return __atomic_load_n(&t->done, __ATOMIC_ACQUIRE) != 0;
}
