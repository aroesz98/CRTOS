/*
 * pthread.c - POSIX threads on CRTOS threads (see pthread.h).
 *
 * A pthread_t holds a pointer to a control block for a CRTOS thread. Joining one frees it;
 * a detached thread cannot free its own stack, so detached threads that have ended are
 * reaped by the next pthread_create() or pthread_detach(). pthread_exit() leaves the thread
 * function with longjmp to the start routine.
 *
 * A mutex handle points to a CRTOS mutex, made by pthread_mutex_init() or, for one set up
 * with PTHREAD_MUTEX_INITIALIZER, at its first use. A condition variable is the sequence
 * number its waiters sleep on (futex); every signal or broadcast bumps it.
 */
#define _POSIX_TIMERS 1
#include <errno.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <crtos.h>

#define DEFAULT_STACK   (16u * 1024u)
#define MUTEX_STATIC    0xFFFFFFFFu     /* PTHREAD_MUTEX_INITIALIZER: made at the first use */

struct crtos_pthread {
    crtos_thread_t *t;
    void *(*fn)(void *);
    void *arg;
    void *ret;
    int tid;
    bool detached;
    jmp_buf exit_jb;
    struct crtos_pthread *next;     /* all threads made here */
};

static crtos_mutex_t s_lock = CRTOS_MUTEX_INIT;
static struct crtos_pthread *s_threads;
static struct crtos_pthread s_main;     /* the thread main() runs in */

static inline struct crtos_pthread *thr(pthread_t t)
{
    return (struct crtos_pthread *)(uintptr_t)t;
}

static inline pthread_t handle(struct crtos_pthread *p)
{
    return (pthread_t)(uintptr_t)p;
}

static int my_tid(void)
{
    return (int)__crtos_syscall(SYS_GETTID, 0, 0, 0, 0);
}

/* Free detached threads that have ended (s_lock held) */
static void reap(void)
{
    struct crtos_pthread **pp = &s_threads;
    while (*pp) {
        struct crtos_pthread *p = *pp;
        if (p->detached && crtos_thread_done(p->t)) {
            *pp = p->next;
            crtos_thread_join(p->t);
            free(p);
        } else {
            pp = &p->next;
        }
    }
}

static void *start(void *arg)
{
    struct crtos_pthread *p = arg;
    if (!setjmp(p->exit_jb))
        p->ret = p->fn(p->arg);
    return p->ret;
}

/* ---- threads ------------------------------------------------------------------------------ */

int pthread_attr_init(pthread_attr_t *attr)
{
    memset(attr, 0, sizeof(*attr));
    attr->is_initialized = 1;
    attr->detachstate = PTHREAD_CREATE_JOINABLE;
    return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
    attr->is_initialized = 0;
    return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size)
{
    if (size < PTHREAD_STACK_MIN || size > 0x7FFFFFFF)
        return EINVAL;
    attr->stacksize = (int)size;
    return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size)
{
    *size = attr->stacksize > 0 ? (size_t)attr->stacksize : DEFAULT_STACK;
    return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int state)
{
    if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
        return EINVAL;
    attr->detachstate = state;
    return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state)
{
    *state = attr->detachstate;
    return 0;
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg)
{
    struct crtos_pthread *p = calloc(1, sizeof(*p));
    if (!p)
        return EAGAIN;
    p->fn = fn;
    p->arg = arg;
    p->detached = attr && attr->detachstate == PTHREAD_CREATE_DETACHED;
    size_t stack = attr && attr->stacksize > 0 ? (size_t)attr->stacksize : DEFAULT_STACK;
    crtos_mutex_lock(&s_lock);
    reap();
    p->t = crtos_thread_start(start, p, stack, 0);
    if (!p->t) {
        crtos_mutex_unlock(&s_lock);
        free(p);
        return EAGAIN;
    }
    p->tid = crtos_thread_id(p->t);
    p->next = s_threads;
    s_threads = p;
    crtos_mutex_unlock(&s_lock);
    *thread = handle(p);
    return 0;
}

int pthread_join(pthread_t thread, void **ret)
{
    struct crtos_pthread *p = thr(thread);
    if (!p || p == &s_main || p->detached)
        return EINVAL;
    if (p->tid == my_tid())
        return EDEADLK;
    crtos_thread_join(p->t); /* waits, then frees the CRTOS thread */
    crtos_mutex_lock(&s_lock);
    for (struct crtos_pthread **pp = &s_threads; *pp; pp = &(*pp)->next) {
        if (*pp == p) {
            *pp = p->next;
            break;
        }
    }
    crtos_mutex_unlock(&s_lock);
    if (ret)
        *ret = p->ret;
    free(p);
    return 0;
}

int pthread_detach(pthread_t thread)
{
    struct crtos_pthread *p = thr(thread);
    if (!p || p == &s_main)
        return EINVAL;
    crtos_mutex_lock(&s_lock);
    p->detached = true;
    reap();
    crtos_mutex_unlock(&s_lock);
    return 0;
}

pthread_t pthread_self(void)
{
    int tid = my_tid();
    struct crtos_pthread *p;
    crtos_mutex_lock(&s_lock);
    for (p = s_threads; p && p->tid != tid; p = p->next)
        ;
    crtos_mutex_unlock(&s_lock);
    if (p)
        return handle(p);
    s_main.tid = tid; /* not made here: the main thread */
    return handle(&s_main);
}

int pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}

void pthread_exit(void *ret)
{
    struct crtos_pthread *p = thr(pthread_self());
    if (p == &s_main)
        exit(0);
    p->ret = ret;
    longjmp(p->exit_jb, 1);
}

/* ---- mutexes ------------------------------------------------------------------------------ */

static crtos_mutex_t *mutex_of(pthread_mutex_t *m)
{
    uint32_t h = __atomic_load_n(m, __ATOMIC_ACQUIRE);
    if (h != MUTEX_STATIC && h != 0)
        return (crtos_mutex_t *)(uintptr_t)h;
    crtos_mutex_t *made = calloc(1, sizeof(*made));
    if (!made)
        return NULL;
    if (__atomic_compare_exchange_n(m, &h, (uint32_t)(uintptr_t)made, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
        return made;
    free(made); /* another thread made it first: h is its mutex */
    return (crtos_mutex_t *)(uintptr_t)h;
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
    memset(attr, 0, sizeof(*attr));
    attr->is_initialized = 1;
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
    attr->is_initialized = 0;
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
    if (type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_DEFAULT)
        return EINVAL;
    attr->recursive = type == PTHREAD_MUTEX_RECURSIVE; /* all CRTOS mutexes are */
    return 0;
}

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr)
{
    (void)attr;
    crtos_mutex_t *made = calloc(1, sizeof(*made));
    if (!made)
        return ENOMEM;
    *m = (pthread_mutex_t)(uintptr_t)made;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    uint32_t h = *m;
    if (h != MUTEX_STATIC && h != 0) {
        crtos_mutex_t *cm = (crtos_mutex_t *)(uintptr_t)h;
        if (cm->state)
            return EBUSY;
        free(cm);
    }
    *m = MUTEX_STATIC;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
    crtos_mutex_t *cm = mutex_of(m);
    if (!cm)
        return ENOMEM;
    crtos_mutex_lock(cm);
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    crtos_mutex_t *cm = mutex_of(m);
    if (!cm)
        return ENOMEM;
    return crtos_mutex_trylock(cm) ? 0 : EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    crtos_mutex_t *cm = mutex_of(m);
    if (!cm || cm->owner != my_tid())
        return EPERM;
    crtos_mutex_unlock(cm);
    return 0;
}

/* ---- condition variables ------------------------------------------------------------------ */

int pthread_condattr_init(pthread_condattr_t *attr)
{
    memset(attr, 0, sizeof(*attr));
    attr->is_initialized = 1;
    return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
    attr->is_initialized = 0;
    return 0;
}

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *attr)
{
    (void)attr;
    *c = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
    (void)c;
    return 0;
}

/* Microseconds until @t (CLOCK_REALTIME), <= 0 once it has passed */
static long long remaining_us(const struct timespec *t)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (long long)(t->tv_sec - now.tv_sec) * 1000000 + (t->tv_nsec - now.tv_nsec) / 1000;
}

int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime)
{
    crtos_mutex_t *cm = mutex_of(m);
    if (!cm || cm->owner != my_tid())
        return EPERM;
    int saved_errno = errno, result = 0;
    uint32_t seq = __atomic_load_n(c, __ATOMIC_ACQUIRE);
    /* the mutex may be held more than once: release it completely, then restore */
    int depth = cm->count;
    cm->count = 1;
    crtos_mutex_unlock(cm);
    for (;;) {
        uint32_t timeout = CRTOS_FOREVER;
        if (abstime) {
            long long us = remaining_us(abstime);
            if (us <= 0) {
                result = ETIMEDOUT;
                break;
            }
            /* whole milliseconds, rounded up: never back before the time */
            timeout = us >= 0x7FFFFFFFLL * 1000 ? 0x7FFFFFFFu : (uint32_t)((us + 999) / 1000);
        }
        if (crtos_futex_wait((volatile uint32_t *)c, seq, timeout) == 0 || errno != ETIMEDOUT)
            break; /* woken, or signalled before it slept */
    }
    crtos_mutex_lock(cm);
    cm->count = depth;
    errno = saved_errno;
    return result; /* 0 may be a spurious wake-up: the caller checks its condition */
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    return pthread_cond_timedwait(c, m, NULL);
}

int pthread_cond_signal(pthread_cond_t *c)
{
    __atomic_fetch_add(c, 1u, __ATOMIC_RELEASE);
    crtos_futex_wake((volatile uint32_t *)c, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
    __atomic_fetch_add(c, 1u, __ATOMIC_RELEASE);
    crtos_futex_wake((volatile uint32_t *)c, 0x7FFFFFFFu);
    return 0;
}

/* ---- once --------------------------------------------------------------------------------- */

int pthread_once(pthread_once_t *once, void (*fn)(void))
{
    volatile uint32_t *state = (volatile uint32_t *)&once->init_executed; /* 0, 1 running, 2 done */
    uint32_t expected = 0;
    if (__atomic_compare_exchange_n(state, &expected, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        fn();
        __atomic_store_n(state, 2u, __ATOMIC_RELEASE);
        crtos_futex_wake(state, 0x7FFFFFFFu);
        return 0;
    }
    while (__atomic_load_n(state, __ATOMIC_ACQUIRE) != 2u)
        crtos_futex_wait(state, 1u, CRTOS_FOREVER);
    return 0;
}
