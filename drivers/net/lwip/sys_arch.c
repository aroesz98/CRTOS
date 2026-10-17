/*
 * sys_arch.c - lwIP's operating system layer on the CRTOS kernel: semaphores, mutexes,
 * mailboxes, threads, time, memory.
 *
 * Waits inside lwIP are not interrupted when the waiting thread's process is ended: lwIP
 * expects them to end only by the event or the time-out. Program threads only wait here for
 * the core lock and, briefly, for a name lookup; the long waits (data, connections) are the
 * kernel socket layer's, which can be interrupted.
 */
#include <string.h>
#include <crtos/arch.h>
#include <crtos/errno.h>
#include <crtos/mm.h>
#include <crtos/sched.h>
#include <crtos/sync.h>
#include "lwip/opt.h"
#include "lwip/sys.h"

struct crtos_mbox
{
    struct semaphore items; /* messages waiting */
    struct semaphore slots; /* room left */
    int size;
    uint32_t head, tail;
    void *msg[];
};

void sys_init(void)
{
}

u32_t sys_now(void)
{
    return tick_get();
}

/* ---- memory ---------------------------------------------------------------------------------- */

void *crtos_lwip_malloc(unsigned size)
{
    return kmalloc(size, KM_ANY);
}

void crtos_lwip_free(void *p)
{
    kfree(p);
}

void *crtos_lwip_calloc(unsigned n, unsigned size)
{
    return kzalloc((size_t)n * size, KM_ANY);
}

/* ---- critical sections --------------------------------------------------------------------------- */

sys_prot_t sys_arch_protect(void)
{
    return irq_lock();
}

void sys_arch_unprotect(sys_prot_t key)
{
    irq_unlock(key);
}

/* ---- semaphores ----------------------------------------------------------------------------------- */

err_t sys_sem_new(sys_sem_t *sem, u8_t count)
{
    struct semaphore *s = kmalloc(sizeof(*s), KM_ANY);
    if (!s)
        return ERR_MEM;
    sem_init(s, count, 0x7FFFFFFF);
    *sem = s;
    return ERR_OK;
}

void sys_sem_free(sys_sem_t *sem)
{
    kfree(*sem);
    *sem = NULL;
}

void sys_sem_signal(sys_sem_t *sem)
{
    sem_give(*sem);
}

/* Wait for a count (@timeout ms, 0: forever); the milliseconds waited or SYS_ARCH_TIMEOUT */
static u32_t take(struct semaphore *s, u32_t timeout)
{
    uint32_t t0 = tick_get();
    for (;;)
    {
        uint32_t left = WAIT_FOREVER;
        if (timeout)
        {
            uint32_t spent = tick_get() - t0;
            if (spent >= timeout)
                return SYS_ARCH_TIMEOUT;
            left = timeout - spent;
        }
        int r = sem_take(s, left);
        if (!r)
            return tick_get() - t0;
        if (r == -ETIMEDOUT)
            return SYS_ARCH_TIMEOUT;
        /* -EINTR: the process is being ended; lwIP needs the wait to finish */
    }
}

u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
    return take(*sem, timeout);
}

/* ---- mutexes ---------------------------------------------------------------------------------------- */

err_t sys_mutex_new(sys_mutex_t *mutex)
{
    struct mutex *m = kmalloc(sizeof(*m), KM_ANY);
    if (!m)
        return ERR_MEM;
    mutex_init(m);
    *mutex = m;
    return ERR_OK;
}

void sys_mutex_free(sys_mutex_t *mutex)
{
    kfree(*mutex);
    *mutex = NULL;
}

void sys_mutex_lock(sys_mutex_t *mutex)
{
    while (mutex_lock(*mutex, WAIT_FOREVER))
    {
    }
}

void sys_mutex_unlock(sys_mutex_t *mutex)
{
    mutex_unlock(*mutex);
}

/* ---- mailboxes -------------------------------------------------------------------------------------- */

err_t sys_mbox_new(sys_mbox_t *mbox, int size)
{
    if (size <= 0)
        size = 16;
    struct crtos_mbox *b = kzalloc(sizeof(*b) + (size_t)size * sizeof(void *), KM_ANY);
    if (!b)
        return ERR_MEM;
    b->size = size;
    sem_init(&b->items, 0, size);
    sem_init(&b->slots, size, size);
    *mbox = b;
    return ERR_OK;
}

void sys_mbox_free(sys_mbox_t *mbox)
{
    kfree(*mbox);
    *mbox = NULL;
}

static void put(struct crtos_mbox *b, void *msg)
{
    uint32_t key = irq_lock();
    b->msg[b->head++ % (uint32_t)b->size] = msg;
    irq_unlock(key);
    sem_give(&b->items);
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg)
{
    take(&(*mbox)->slots, 0);
    put(*mbox, msg);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
    if (sem_take(&(*mbox)->slots, NO_WAIT))
        return ERR_MEM;
    put(*mbox, msg);
    return ERR_OK;
}

err_t sys_mbox_trypost_fromisr(sys_mbox_t *mbox, void *msg)
{
    return sys_mbox_trypost(mbox, msg);
}

static void *get(struct crtos_mbox *b)
{
    uint32_t key = irq_lock();
    void *msg = b->msg[b->tail++ % (uint32_t)b->size];
    irq_unlock(key);
    sem_give(&b->slots);
    return msg;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout)
{
    u32_t r = take(&(*mbox)->items, timeout);
    if (r == SYS_ARCH_TIMEOUT)
        return r;
    void *m = get(*mbox);
    if (msg)
        *msg = m;
    return r;
}

u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg)
{
    if (sem_take(&(*mbox)->items, NO_WAIT))
        return SYS_MBOX_EMPTY;
    void *m = get(*mbox);
    if (msg)
        *msg = m;
    return 0;
}

/* ---- threads ----------------------------------------------------------------------------------------- */

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio)
{
    return kthread_create(name, (task_fn_t)thread, arg, prio, (size_t)stacksize);
}

/* ---- random numbers (ports, IDs, DHCP transaction IDs) ---------------------------------------------- */

static uint32_t s_rand;

uint32_t crtos_lwip_rand(void)
{
    if (!s_rand)
        s_rand = 0x9E3779B9u ^ (uint32_t)time_us();
    s_rand ^= s_rand << 13;
    s_rand ^= s_rand >> 17;
    s_rand ^= s_rand << 5;
    return s_rand ^ (uint32_t)time_us();
}
