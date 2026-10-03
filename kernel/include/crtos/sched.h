/*
 * crtos/sched.h - tasks (threads) and time.
 *
 * Priorities: 0 is idle, CONFIG_NUM_PRIO-1 the highest. Scheduling is preemptive by
 * priority, round-robin (CONFIG_TIMESLICE_TICKS) among equal priorities.
 */
#ifndef CRTOS_SCHED_H
#define CRTOS_SCHED_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct task;
typedef struct task task_t;
typedef void (*task_fn_t)(void *arg);

#define WAIT_FOREVER 0xFFFFFFFFu
#define NO_WAIT      0u

#define PRIO_IDLE    0
#define PRIO_LOW     4
#define PRIO_NORMAL  10
#define PRIO_HIGH    20
#define PRIO_MAX     31

/* Kernel thread (privileged). stack_size 0 = CONFIG_KTHREAD_STACK. */
task_t *kthread_create(const char *name, task_fn_t fn, void *arg, int prio, size_t stack_size);

void task_exit(int code) __attribute__((noreturn));

/* Ask a kernel thread to finish and wait until it did (its code may be unloaded then).
 * The thread sees it through task_should_stop() or an -EINTR from a blocking call. */
int kthread_stop(task_t *t);
int task_should_stop(void);
task_t *task_current(void);
const char *task_name(const task_t *t);
int task_id(const task_t *t);
int task_priority(const task_t *t);
int task_set_priority(task_t *t, int prio);
int task_kill(task_t *t);
void task_yield(void);

/* Sleep; returns 0, or -EINTR when the task is being killed */
int task_sleep_ticks(uint32_t ticks);
int task_sleep_ms(uint32_t ms);

/* Time */
uint32_t tick_get(void);
int *task_errno_ptr(void);                  /* a per-thread errno for libraries in the kernel */
uint64_t tick_get64(void);
uint64_t time_us(void);
static inline uint32_t ms_to_ticks(uint32_t ms)
{
    return ms; /* CONFIG_TICK_HZ == 1000; WAIT_FOREVER maps to itself */
}

/* Disable/enable preemption (not interrupts) around short sequences */
void sched_lock(void);
void sched_unlock(void);

#ifdef __cplusplus
}
#endif

#endif
