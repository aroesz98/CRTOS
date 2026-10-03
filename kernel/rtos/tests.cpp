/*
 * kernel/rtos/tests.cpp - kernel self tests (run from kmon: "test <name>" / "test all").
 *
 * Besides functional checks they measure the basic costs (context switch, interrupt
 * latency, allocation) and verify that faults - kernel stack overflow, NULL access - only
 * terminate the culprit. With the operating system part (CONFIG_OS) os/tests_os.cpp adds the
 * tests of processes: user access to kernel memory, user stack overflow, system calls.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include "ktest.h"
#include <crtos/irq.h>
#include <crtos/queue.h>
#include <crtos/timer.h>
#include "fsl_device_registers.h"
#include <string.h>

int ktest_failures;

static uint32_t cyc_to_ns(uint32_t cyc)
{
    return (uint32_t)((uint64_t)cyc * 1000u / (CONFIG_CORE_CLOCK_HZ / 1000000u));
}

bool ktest_wait_task_gone(int id, uint32_t timeout_ms)
{
    uint32_t t0 = tick_get();
    for (;;) {
        sched_lock();
        task_t *t = task_find(id);
        sched_unlock();
        if (!t)
            return true;
        if (tick_get() - t0 > timeout_ms)
            return false;
        task_sleep_ms(2);
    }
}

static uint32_t s_rng = 0x12345678u;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* ---- scheduler ------------------------------------------------------------------------- */

/* The spinners run above every service and driver thread (so a busy system does not skew
 * the shares) and stop by themselves at a deadline, as the test's own thread cannot run
 * while they do. */
#define SPIN_PRIO 23

static volatile uint32_t s_spin_count[4];
static volatile uint64_t s_spin_end;

static void spinner(void *arg)
{
    int i = (int)(intptr_t)arg;
    while (time_us() < s_spin_end)
        s_spin_count[i]++;
}

/* a higher priority that wakes every tick until the spinners' deadline */
static void ticker(void *)
{
    while (time_us() < s_spin_end)
        task_sleep_ms(1);
}

static void test_sched(void)
{
    int ids[3];
    memset((void *)s_spin_count, 0, sizeof(s_spin_count));
    sched_lock(); /* all start together */
    for (int i = 0; i < 3; i++) {
        task_t *t = kthread_create("spin", spinner, (void *)(intptr_t)i, SPIN_PRIO, 1024);
        ids[i] = t ? task_id(t) : -1;
    }
    s_spin_end = time_us() + 300000u;
    sched_unlock();
    for (int i = 0; i < 3; i++)
        CHECK(ids[i] >= 0, "kthread_create");
    for (int i = 0; i < 3; i++)
        CHECK(ktest_wait_task_gone(ids[i], 500), "spinner %d did not exit", i);
    uint32_t mn = 0xFFFFFFFFu, mx = 0;
    for (int i = 0; i < 3; i++) {
        mn = s_spin_count[i] < mn ? s_spin_count[i] : mn;
        mx = s_spin_count[i] > mx ? s_spin_count[i] : mx;
    }
    cprintf("  round robin: %lu %lu %lu loops\n", (unsigned long)s_spin_count[0], (unsigned long)s_spin_count[1],
            (unsigned long)s_spin_count[2]);
    CHECK(mn > 0 && (mx - mn) < mx / 10, "equal priorities did not share the CPU evenly");

    memset((void *)s_spin_count, 0, sizeof(s_spin_count));
    sched_lock(); /* both ready at once: the lower one must not run before the deadline */
    task_t *lo = kthread_create("spin-lo", spinner, (void *)0, SPIN_PRIO, 1024);
    task_t *hi = kthread_create("spin-hi", spinner, (void *)1, SPIN_PRIO + 1, 1024);
    int lo_id = lo ? task_id(lo) : -1, hi_id = hi ? task_id(hi) : -1;
    s_spin_end = time_us() + 100000u;
    sched_unlock();
    CHECK(ktest_wait_task_gone(hi_id, 500) && ktest_wait_task_gone(lo_id, 500), "spinners did not exit");
    cprintf("  priority: high %lu loops, low %lu loops\n", (unsigned long)s_spin_count[1],
            (unsigned long)s_spin_count[0]);
    CHECK(s_spin_count[0] == 0 && s_spin_count[1] > 0, "lower priority ran while a higher one was ready");

    /* equals still take turns while a higher priority preempts them every tick (as drivers
     * and services do): a preempted task keeps what is left of its slice, it gets no new one */
    memset((void *)s_spin_count, 0, sizeof(s_spin_count));
    sched_lock();
    task_t *a = kthread_create("spin-a", spinner, (void *)0, SPIN_PRIO, 1024);
    task_t *b = kthread_create("spin-b", spinner, (void *)1, SPIN_PRIO, 1024);
    task_t *tk = kthread_create("spin-tick", ticker, nullptr, SPIN_PRIO + 1, 1024);
    int a_id = a ? task_id(a) : -1, b_id = b ? task_id(b) : -1, tk_id = tk ? task_id(tk) : -1;
    s_spin_end = time_us() + 200000u;
    sched_unlock();
    CHECK(ktest_wait_task_gone(a_id, 500) && ktest_wait_task_gone(b_id, 500) && ktest_wait_task_gone(tk_id, 500),
          "spinners did not exit");
    uint32_t sa = s_spin_count[0], sb = s_spin_count[1];
    cprintf("  round robin, preempted every tick: %lu %lu loops\n", (unsigned long)sa, (unsigned long)sb);
    uint32_t smax = sa > sb ? sa : sb, smin = sa > sb ? sb : sa;
    CHECK(smin > 0 && (smax - smin) < smax / 5, "a preempted task kept the CPU from its equals");
}

/* ---- mutex with priority inheritance ------------------------------------------------- */

static struct mutex s_mtx;
static struct semaphore s_sem_locked;
static volatile uint32_t s_hi_wait_us;
static volatile int s_lo_prio_seen;

static void busy_us(uint32_t us)
{
    uint64_t end = time_us() + us;
    while (time_us() < end) {
    }
}

static void pi_low(void *)
{
    mutex_lock(&s_mtx, WAIT_FOREVER);
    sem_give(&s_sem_locked);
    busy_us(30000);
    s_lo_prio_seen = task_priority(task_current());
    mutex_unlock(&s_mtx);
}

static void pi_mid(void *)
{
    busy_us(150000);
}

static void pi_high(void *)
{
    uint64_t t0 = time_us();
    mutex_lock(&s_mtx, WAIT_FOREVER);
    s_hi_wait_us = (uint32_t)(time_us() - t0);
    mutex_unlock(&s_mtx);
}

static void test_mutex(void)
{
    mutex_init(&s_mtx);
    sem_init(&s_sem_locked, 0, 1);
    s_hi_wait_us = 0;
    s_lo_prio_seen = 0;
    task_t *lo = kthread_create("pi-lo", pi_low, nullptr, 5, 1024);
    CHECK(sem_take(&s_sem_locked, 1000) == 0, "low task did not take the mutex");
    task_t *hi = kthread_create("pi-hi", pi_high, nullptr, 15, 1024);
    task_t *mid = kthread_create("pi-mid", pi_mid, nullptr, 10, 1024);
    int ids[3] = { lo ? task_id(lo) : -1, hi ? task_id(hi) : -1, mid ? task_id(mid) : -1 };
    for (int i = 0; i < 3; i++)
        CHECK(ktest_wait_task_gone(ids[i], 1000), "task %d did not finish", ids[i]);
    cprintf("  high priority waited %lu us, holder ran at priority %d\n", (unsigned long)s_hi_wait_us, s_lo_prio_seen);
    CHECK(s_lo_prio_seen == 15, "priority not inherited");
    CHECK(s_hi_wait_us > 0 && s_hi_wait_us < 80000, "priority inversion (medium task ran first)");
}

/* ---- semaphore ping-pong: context switch cost ------------------------------------------- */

#define PP_ROUNDS 20000
static struct semaphore s_ping, s_pong;
static volatile uint32_t s_pp_cycles;

static void pp_a(void *)
{
    uint32_t t0 = cpu_cycles();
    for (int i = 0; i < PP_ROUNDS; i++) {
        sem_give(&s_ping);
        sem_take(&s_pong, WAIT_FOREVER);
    }
    s_pp_cycles = cpu_cycles() - t0;
}

static void pp_b(void *)
{
    for (int i = 0; i < PP_ROUNDS; i++) {
        sem_take(&s_ping, WAIT_FOREVER);
        sem_give(&s_pong);
    }
}

static void test_sem(void)
{
    sem_init(&s_ping, 0, 1);
    sem_init(&s_pong, 0, 1);
    s_pp_cycles = 0;
    task_t *a = kthread_create("pp-a", pp_a, nullptr, 15, 1024);
    task_t *b = kthread_create("pp-b", pp_b, nullptr, 15, 1024);
    int ida = a ? task_id(a) : -1, idb = b ? task_id(b) : -1;
    CHECK(ktest_wait_task_gone(ida, 3000) && ktest_wait_task_gone(idb, 3000), "ping-pong did not finish");
    uint32_t per = s_pp_cycles / (2 * PP_ROUNDS);
    cprintf("  %d round trips: %lu cycles per switch incl. semaphore ops (%lu ns)\n", PP_ROUNDS,
            (unsigned long)per, (unsigned long)cyc_to_ns(per));
    CHECK(s_pp_cycles, "no result");
}

/* ---- interrupt latency ------------------------------------------------------------------ */

#define TEST_IRQ Reserved68_IRQn
static volatile uint32_t s_irq_stamp;
static volatile bool s_irq_hit;

static void lat_handler(int, void *)
{
    s_irq_stamp = cpu_cycles();
    s_irq_hit = true;
}

static void test_irq(void)
{
    int r = irq_request(TEST_IRQ, lat_handler, nullptr, CONFIG_IRQ_KERNEL_PRIO, "test");
    CHECK(r == 0, "irq_request: %d", r);
    if (r)
        return;
    uint32_t mn = 0xFFFFFFFFu, mx = 0, sum = 0;
    const int n = 1000;
    for (int i = 0; i < n; i++) {
        s_irq_hit = false;
        uint32_t t0 = cpu_cycles();
        NVIC->ISPR[TEST_IRQ >> 5] = 1u << (TEST_IRQ & 31);
        while (!s_irq_hit) {
        }
        uint32_t d = s_irq_stamp - t0;
        mn = d < mn ? d : mn;
        mx = d > mx ? d : mx;
        sum += d;
    }
    irq_free(TEST_IRQ);
    cprintf("  pend -> handler: min %lu avg %lu max %lu cycles (avg %lu ns)\n", (unsigned long)mn,
            (unsigned long)(sum / n), (unsigned long)mx, (unsigned long)cyc_to_ns(sum / n));
}

/* ---- heap ------------------------------------------------------------------------------- */

static void test_heap(void)
{
    static const unsigned flags[] = { KM_ANY, KM_FAST, KM_LARGE, KM_NOCACHE, KM_EXEC, KM_FAST | KM_EXEC };
    struct {
        uint8_t *p;
        uint32_t n;
        uint8_t tag;
    } slot[48];
    memset(slot, 0, sizeof(slot));
    uint32_t alloc_cyc = 0, free_cyc = 0, nalloc = 0, nfree = 0;
    for (int it = 0; it < 4000; it++) {
        int i = (int)(rnd() % ARRAY_SIZE(slot));
        if (slot[i].p) {
            for (uint32_t j = 0; j < slot[i].n; j++) {
                if (slot[i].p[j] != slot[i].tag) {
                    CHECK(0, "heap data corrupted");
                    return;
                }
            }
            uint32_t t0 = cpu_cycles();
            kfree(slot[i].p);
            free_cyc += cpu_cycles() - t0;
            nfree++;
            slot[i].p = nullptr;
        } else {
            uint32_t n = (rnd() % 8 == 0) ? rnd() % 16384 : rnd() % 256;
            size_t align = (rnd() % 4 == 0) ? (size_t)8 << (rnd() % 8) : 8;
            unsigned f = flags[rnd() % ARRAY_SIZE(flags)];
            uint32_t t0 = cpu_cycles();
            uint8_t *p = (uint8_t *)kmalloc_aligned(n, align, f);
            alloc_cyc += cpu_cycles() - t0;
            nalloc++;
            if (!p)
                continue;
            CHECK(((uintptr_t)p & (align - 1)) == 0, "misaligned block");
            slot[i].p = p;
            slot[i].n = n;
            slot[i].tag = (uint8_t)rnd();
            memset(p, slot[i].tag, n);
        }
    }
    for (size_t i = 0; i < ARRAY_SIZE(slot); i++)
        kfree(slot[i].p);
    CHECK(mm_check() == 0, "heap corrupted");
    cprintf("  kmalloc avg %lu cycles, kfree avg %lu cycles\n", (unsigned long)(nalloc ? alloc_cyc / nalloc : 0),
            (unsigned long)(nfree ? free_cyc / nfree : 0));
}

/* ---- FPU context in kernel threads ------------------------------------------------------ */

static volatile float s_fpu_result[3];

static float fpu_work(float seed)
{
    volatile float x = seed;
    float acc = 0.0f;
    for (int i = 1; i <= 20000; i++) {
        acc += x / (float)i;
        acc *= 0.9999f;
        if ((i & 1023) == 0)
            task_yield();
    }
    return acc;
}

void ktest_fpu_thread(void *arg)
{
    int i = (int)(intptr_t)arg;
    s_fpu_result[i] = fpu_work(1.0f + (float)i);
}

static void test_fpu(void)
{
    /* reference values computed without competition */
    float ref0 = fpu_work(1.0f), ref1 = fpu_work(2.0f);
    s_fpu_result[0] = s_fpu_result[1] = 0.0f;
    task_t *a = kthread_create("fpu0", ktest_fpu_thread, (void *)0, PRIO_NORMAL, 1024);
    task_t *b = kthread_create("fpu1", ktest_fpu_thread, (void *)1, PRIO_NORMAL, 1024);
    int ida = a ? task_id(a) : -1, idb = b ? task_id(b) : -1;
    CHECK(ktest_wait_task_gone(ida, 2000) && ktest_wait_task_gone(idb, 2000), "FPU threads did not finish");
    CHECK(s_fpu_result[0] == ref0 && s_fpu_result[1] == ref1, "FPU registers corrupted across context switches");
}

/* ---- message queues ----------------------------------------------------------------------- */

#define Q_ITEMS 3000
static struct queue *s_q;
static volatile uint32_t s_q_bad, s_q_got;

/* sends 0, 1, 2, ... - the small queue is full most of the time, so it waits for room */
static void q_producer(void *)
{
    for (uint32_t i = 0; i < Q_ITEMS; i++)
        if (queue_send(s_q, &i, 1000))
            s_q_bad++;
}

static void q_consumer(void *)
{
    for (uint32_t i = 0; i < Q_ITEMS; i++) {
        uint32_t v = ~0u;
        if (queue_recv(s_q, &v, 1000) || v != i)
            s_q_bad++;
        else
            s_q_got++;
    }
}

static volatile int s_q_isr_result = 1;

static void q_isr(int, void *)
{
    uint32_t v = 0xC0FFEEu;
    s_q_isr_result = queue_send(s_q, &v, WAIT_FOREVER); /* (from an interrupt: no waiting) */
}

static volatile int s_q_cancel_result = 1;

static void q_waiter(void *)
{
    uint32_t v;
    s_q_cancel_result = queue_recv(s_q, &v, WAIT_FOREVER);
}

static void test_queue(void)
{
    s_q = queue_create(sizeof(uint32_t), 4);
    CHECK(s_q, "queue_create");
    if (!s_q)
        return;
    /* empty: no item, at once or after the timeout */
    uint32_t v;
    CHECK(queue_recv(s_q, &v, NO_WAIT) == -ETIMEDOUT, "receive from an empty queue");
    uint64_t t0 = time_us();
    int r = queue_recv(s_q, &v, 20);
    uint32_t waited = (uint32_t)(time_us() - t0);
    CHECK(r == -ETIMEDOUT && waited >= 19000 && waited < 30000, "timeout: %d after %lu us", r, (unsigned long)waited);
    /* full: no room */
    for (uint32_t i = 0; i < 4; i++)
        CHECK(queue_send(s_q, &i, NO_WAIT) == 0, "send %lu", (unsigned long)i);
    CHECK(queue_send(s_q, &v, NO_WAIT) == -ETIMEDOUT && queue_count(s_q) == 4, "send to a full queue");
    for (uint32_t i = 0; i < 4; i++)
        CHECK(queue_recv(s_q, &v, NO_WAIT) == 0 && v == i, "order: item %lu is %lu", (unsigned long)i, (unsigned long)v);
    /* two tasks, in order, waiting on both sides */
    s_q_bad = s_q_got = 0;
    t0 = time_us();
    task_t *c = kthread_create("q-cons", q_consumer, nullptr, 14, 1024);
    task_t *p = kthread_create("q-prod", q_producer, nullptr, 15, 1024);
    int idc = c ? task_id(c) : -1, idp = p ? task_id(p) : -1;
    CHECK(ktest_wait_task_gone(idp, 5000) && ktest_wait_task_gone(idc, 5000), "producer/consumer did not finish");
    uint32_t us = (uint32_t)(time_us() - t0);
    CHECK(!s_q_bad && s_q_got == Q_ITEMS, "%lu items wrong or missing", (unsigned long)(Q_ITEMS - s_q_got));
    cprintf("  %d items through a queue of 4: %lu ns per item\n", Q_ITEMS, (unsigned long)(us * 1000u / Q_ITEMS));
    /* from an interrupt */
    r = irq_request(TEST_IRQ, q_isr, nullptr, CONFIG_IRQ_DEFAULT_PRIO, "test");
    CHECK(r == 0, "irq_request: %d", r);
    if (!r) {
        NVIC->ISPR[TEST_IRQ >> 5] = 1u << (TEST_IRQ & 31);
        task_sleep_ms(2);
        irq_free(TEST_IRQ);
        CHECK(s_q_isr_result == 0 && queue_recv(s_q, &v, NO_WAIT) == 0 && v == 0xC0FFEEu,
              "send from an interrupt: %d", s_q_isr_result);
    }
    /* the end of a queue wakes who waits on it */
    task_t *w = kthread_create("q-wait", q_waiter, nullptr, 15, 1024);
    int idw = w ? task_id(w) : -1;
    task_sleep_ms(5);
    queue_delete(s_q);
    CHECK(ktest_wait_task_gone(idw, 1000) && s_q_cancel_result == -ECANCELED, "waiter: %d", s_q_cancel_result);
    s_q = nullptr;
}

/* ---- software timers ---------------------------------------------------------------------- */

static volatile uint32_t s_t_count;
static volatile uint64_t s_t_first_us;

static void t_fn(void *)
{
    if (!s_t_count++)
        s_t_first_us = time_us();
}

static void test_timer(void)
{
    struct timer t;
    timer_init(&t, t_fn, nullptr);
    /* one-shot */
    s_t_count = 0;
    uint64_t t0 = time_us();
    timer_start(&t, 20, 0);
    task_sleep_ms(60);
    uint32_t after = (uint32_t)(s_t_first_us - t0);
    CHECK(s_t_count == 1 && !timer_active(&t), "one-shot ran %lu times", (unsigned long)s_t_count);
    CHECK(after >= 19000 && after < 22000, "one-shot after %lu us (20 ms)", (unsigned long)after);
    /* periodic, then stopped */
    s_t_count = 0;
    timer_start(&t, 5, 5);
    task_sleep_ms(102);
    bool was = timer_stop(&t);
    uint32_t n = s_t_count;
    task_sleep_ms(20);
    CHECK(was && n >= 19 && n <= 21, "periodic 5 ms: %lu runs in 102 ms", (unsigned long)n);
    CHECK(s_t_count == n, "ran %lu times after timer_stop", (unsigned long)(s_t_count - n));
    /* a stopped one never runs */
    s_t_count = 0;
    timer_start(&t, 10, 0);
    CHECK(timer_stop(&t) && !timer_stop(&t), "stop");
    task_sleep_ms(20);
    CHECK(s_t_count == 0, "a stopped timer ran");
    cprintf("  one-shot after %lu.%03lu ms, periodic 5 ms: %lu runs in 102 ms\n", (unsigned long)(after / 1000u),
            (unsigned long)(after % 1000u), (unsigned long)n);
}

/* ---- kernel faults ----------------------------------------------------------------------- */

static int __attribute__((noinline)) recurse(int depth)
{
    volatile uint8_t buf[96];
    buf[0] = (uint8_t)depth;
    if (depth > 1000000)
        return 0;
    return recurse(depth + 1) + buf[0];
}

static void stack_victim(void *)
{
    recurse(0);
}

static void test_kstack(void)
{
    task_t *t = kthread_create("stack-victim", stack_victim, nullptr, PRIO_NORMAL, 1024);
    int id = t ? task_id(t) : -1;
    CHECK(ktest_wait_task_gone(id, 1000), "kernel thread survived its stack overflow");
}

static void null_victim(void *arg)
{
    volatile uintptr_t addr = (uintptr_t)arg;
    *(volatile uint32_t *)addr = 0x1234u;
}

static void test_null(void)
{
    task_t *t = kthread_create("null-victim", null_victim, (void *)0x10, PRIO_NORMAL, 1024);
    int id = t ? task_id(t) : -1;
    CHECK(ktest_wait_task_gone(id, 1000), "NULL pointer write was not caught");
}

/* ---- runner ------------------------------------------------------------------------------- */

static const struct ktest s_tests[] = {
    { "sched", test_sched, "round robin and strict priorities" },
    { "mutex", test_mutex, "priority inheritance" },
    { "sem", test_sem, "semaphore ping-pong, context switch cost" },
    { "irq", test_irq, "interrupt latency" },
    { "heap", test_heap, "allocator stress + integrity" },
    { "queue", test_queue, "message queue: order, timeouts, waiting both sides, interrupt, delete" },
    { "timer", test_timer, "software timers: one-shot, periodic, stop" },
    { "fpu", test_fpu, "FPU context of kernel threads" },
    { "kstack", test_kstack, "kernel thread stack overflow is contained" },
    { "null", test_null, "NULL pointer access is contained" },
};

/* The tests of the core, then (CONFIG_OS) those of processes */
static const struct ktest *test_at(size_t i)
{
    if (i < ARRAY_SIZE(s_tests))
        return &s_tests[i];
#if CONFIG_OS
    if (i - ARRAY_SIZE(s_tests) < kernel_os_ntests)
        return &kernel_os_tests[i - ARRAY_SIZE(s_tests)];
#endif
    return nullptr;
}

static size_t heap_free_total(void)
{
    size_t total = 0;
    for (int i = 0; i < mm_pool_count(); i++) {
        struct mm_pool_info pi;
        if (mm_pool_info(i, &pi) == 0)
            total += pi.free;
    }
    return total;
}

void kernel_tests(const char *name)
{
    bool all = !strcmp(name, "all");
    int run = 0, failed = 0;
    const struct ktest *t;
    for (size_t i = 0; (t = test_at(i)); i++) {
        if (!all && strcmp(name, t->name))
            continue;
        run++;
        cprintf("[%s] %s\n", t->name, t->what);
        ktest_failures = 0;
        task_sleep_ms(20); /* let earlier exits be reaped */
        size_t before = heap_free_total();
        t->fn();
        /* Programs that run meanwhile (the graphics server sends ~60 messages a second) have
         * allocations in flight: look a few times before calling a difference a leak */
        size_t after = 0;
        for (int tries = 0; tries < 20; tries++) {
            task_sleep_ms(tries ? 7 : 20);
            after = heap_free_total();
            if (after == before)
                break;
        }
        CHECK(after == before, "memory leak: %ld bytes", (long)(before - after));
        CHECK(mm_check() == 0, "heap corrupted");
        cprintf("  -> %s\n", ktest_failures ? "FAILED" : "ok");
        if (ktest_failures)
            failed++;
    }
    if (!run) {
        cprintf("tests:\n");
        for (size_t i = 0; (t = test_at(i)); i++)
            cprintf("  %-10s %s\n", t->name, t->what);
        cprintf("  all        run everything\n");
        return;
    }
    cprintf("%d test(s), %d failed\n", run, failed);
}
