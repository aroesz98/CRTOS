/*
 * pthread.h - POSIX threads, the subset ported programs use, on CRTOS threads and futexes:
 * create/join/detach/self/equal/exit with a stack-size attribute, mutexes (recursive, as
 * all CRTOS mutexes are), condition variables and once.
 *
 * The types are newlib's (<sys/_pthreadtypes.h>, visible with POSIX): a thread, a mutex
 * and a condition variable are 32-bit handles. Note newlib's values of
 * PTHREAD_CREATE_DETACHED (0) and PTHREAD_CREATE_JOINABLE (1).
 */
#ifndef _CRTOS_PTHREAD_H
#define _CRTOS_PTHREAD_H

#include <sys/types.h>
#include <sys/sched.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PTHREAD_MUTEX_INITIALIZER _PTHREAD_MUTEX_INITIALIZER
#define PTHREAD_COND_INITIALIZER  _PTHREAD_COND_INITIALIZER
#define PTHREAD_ONCE_INIT         _PTHREAD_ONCE_INIT

#ifndef PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_NORMAL     0
#define PTHREAD_MUTEX_RECURSIVE  1
#define PTHREAD_MUTEX_ERRORCHECK 2
#define PTHREAD_MUTEX_DEFAULT    3
#endif

#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 2048
#endif

int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t size);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *size);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int state);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *state);

int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*fn)(void *), void *arg);
int pthread_join(pthread_t thread, void **ret);
int pthread_detach(pthread_t thread);
pthread_t pthread_self(void);
int pthread_equal(pthread_t a, pthread_t b);
void pthread_exit(void *ret) __attribute__((noreturn));

int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *m);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_trylock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);

int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);
int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *c);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);

int pthread_once(pthread_once_t *once, void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif
