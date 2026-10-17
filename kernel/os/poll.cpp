/*
 * kernel/os/poll.cpp - waiting for several handles at once.
 *
 * sys_poll() links one entry per handle into the objects' poll heads, checks their state
 * and sleeps until one of them calls poll_notify() or the timeout expires. poll_notify()
 * marks the task (poll_hit) and wakes it only while it sleeps in poll (poll_armed), so a
 * task blocked on something else inside an object's poll function is never disturbed. A
 * notification between the state check and the sleep is not lost: poll_hit is checked
 * under the same lock right before sleeping.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include <crtos/syscall.h>

#define POLL_MAX 32

void poll_head_init(struct poll_head *h)
{
    list_init(&h->entries);
}

void poll_add(struct poll_head *h, struct poll_entry *e)
{
    if (!e)
        return;
    uint32_t key = irq_lock();
    list_add_tail(&e->node, &h->entries);
    irq_unlock(key);
}

void poll_notify(struct poll_head *h)
{
    uint32_t key = irq_lock();
    struct list_head *pos;
    list_for_each(pos, &h->entries) {
        task_t *t = list_entry(pos, struct poll_entry, node)->task;
        t->poll_hit = 1;
        if (t->poll_armed && t->state == TASK_BLOCKED)
            sched_wake(t, 0);
    }
    irq_unlock(key);
}

struct poll_slot {
    struct poll_entry e;
    void *obj;
    uint8_t type, rights;
    int16_t events;
};

int64_t sys_poll(struct crtos_pollfd *ufds, uint32_t n, uint32_t timeout)
{
    task_t *t = g_current;
    struct proc *p = t->proc;
    if (n > POLL_MAX)
        return -EINVAL;
    if (n && !uaccess_ok(ufds, n * sizeof(*ufds), 1))
        return -EFAULT;
    struct poll_slot *s = nullptr;
    if (n) {
        s = (struct poll_slot *)kzalloc(n * sizeof(*s), KM_FAST);
        if (!s)
            return -ENOMEM;
    }
    for (uint32_t i = 0; i < n; i++) {
        list_init(&s[i].e.node);
        s[i].e.task = t;
        s[i].events = ufds[i].events;
        if (ufds[i].fd >= 0)
            s[i].obj = handle_ref(p, ufds[i].fd, &s[i].type, &s[i].rights);
    }

    uint32_t deadline = g_ticks + timeout;
    int ready = 0;
    for (bool first = true;; first = false) {
        t->poll_hit = 0;
        ready = 0;
        for (uint32_t i = 0; i < n; i++) {
            int rev = 0;
            if (ufds[i].fd < 0)
                rev = 0;
            else if (!s[i].obj)
                rev = POLLNVAL;
            else
                rev = obj_poll(s[i].type, s[i].rights, s[i].obj, first ? &s[i].e : nullptr) &
                      (s[i].events | POLLERR | POLLHUP | POLLNVAL);
            ufds[i].revents = (int16_t)rev;
            if (rev)
                ready++;
        }
        if (ready || timeout == NO_WAIT)
            break;
        uint32_t left = WAIT_FOREVER;
        if (timeout != WAIT_FOREVER) {
            int32_t rem = (int32_t)(deadline - g_ticks);
            if (rem <= 0)
                break;
            left = (uint32_t)rem;
        }
        uint32_t key = irq_lock();
        if (t->poll_hit) {
            irq_unlock(key);
            continue;
        }
        t->poll_armed = 1;
        int r = sched_block(nullptr, left, key);
        t->poll_armed = 0;
        if (r == -EINTR) {
            ready = -EINTR;
            break;
        }
    }

    uint32_t key = irq_lock();
    for (uint32_t i = 0; i < n; i++)
        list_del(&s[i].e.node);
    irq_unlock(key);
    for (uint32_t i = 0; i < n; i++)
        if (s[i].obj)
            obj_put(s[i].type, s[i].obj);
    kfree(s);
    return ready;
}
