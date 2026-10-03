/*
 * crtos/timer.h - software timers, one-shot or periodic.
 *
 * A timer's function runs in the kernel thread "ktimer" (priority CONFIG_TIMER_PRIO), not in an
 * interrupt: it may use the kernel's calls but should be short and not wait long, because the
 * other timers wait for it. Times in milliseconds (ticks).
 */
#ifndef CRTOS_TIMER_H
#define CRTOS_TIMER_H

#include <stdbool.h>
#include <stdint.h>
#include <crtos/list.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*timer_fn_t)(void *arg);

struct timer {
    struct list_head node;      /* in the list of armed timers, the earliest first */
    uint32_t due;               /* tick it runs at */
    uint32_t period;            /* ticks between runs, 0: once */
    timer_fn_t fn;
    void *arg;
    bool armed;
};

void timer_init(struct timer *t, timer_fn_t fn, void *arg);
/* Arm (or arm again): the function runs @delay_ms from now (at least 1), then every @period_ms
 * (0: once). May be called from interrupts and from the timer's own function. */
void timer_start(struct timer *t, uint32_t delay_ms, uint32_t period_ms);
/* Disarm: true if it was armed. The function may be running in "ktimer" at this moment. */
bool timer_stop(struct timer *t);
bool timer_active(const struct timer *t);

#ifdef __cplusplus
}
#endif

#endif
