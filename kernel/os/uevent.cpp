/*
 * kernel/os/uevent.cpp - device notifications (/dev/uevent), read by the device manager.
 *
 * Every open file has its own queue of text lines: "add fb0", "remove event2". A new reader
 * first gets an "add" line for each device node that already exists, so it never misses
 * one (it may see a node twice when one appears while it opens). read() returns whole lines
 * and blocks while the queue is empty (unless O_NONBLOCK); poll() reports POLLIN.
 */
#include "kernel.h"
#include <crtos/vfs.h>
#include <string.h>

#define UEV_QUEUE   64u
#define UEV_LINE    32u

struct uev_client {
    struct list_head node;
    char q[UEV_QUEUE][UEV_LINE];
    uint32_t head, tail;
    uint32_t lost;
    struct wait_queue wq;
    struct poll_head ph;
};

static struct list_head s_clients = LIST_HEAD_INIT(s_clients);

/* irq locked */
static void push(struct uev_client *c, const char *line)
{
    if (c->head - c->tail >= UEV_QUEUE) {
        c->lost++;
        return;
    }
    strncpy(c->q[c->head % UEV_QUEUE], line, UEV_LINE - 1);
    c->q[c->head % UEV_QUEUE][UEV_LINE - 1] = 0;
    c->head++;
    wq_wake_all(&c->wq, 0);
    poll_notify(&c->ph);
}

void uevent_emit(const char *action, const char *name)
{
    char line[UEV_LINE];
    ksnprintf(line, sizeof(line), "%s %s\n", action, name);
    uint32_t key = irq_lock();
    struct list_head *pos;
    list_for_each(pos, &s_clients)
        push(list_entry(pos, struct uev_client, node), line);
    irq_unlock(key);
}

static void coldplug_one(const char *name, void *ctx)
{
    char line[UEV_LINE];
    ksnprintf(line, sizeof(line), "add %s\n", name);
    uint32_t key = irq_lock();
    push((struct uev_client *)ctx, line);
    irq_unlock(key);
}

static int uev_open(struct file *f)
{
    struct uev_client *c = (struct uev_client *)kzalloc(sizeof(*c), KM_ANY);
    if (!c)
        return -ENOMEM;
    wq_init(&c->wq);
    poll_head_init(&c->ph);
    uint32_t key = irq_lock();
    list_add_tail(&c->node, &s_clients);
    irq_unlock(key);
    devfs_foreach(coldplug_one, c);
    f->priv = c;
    return 0;
}

static int uev_read(struct file *f, void *buf, size_t len)
{
    struct uev_client *c = (struct uev_client *)f->priv;
    char *out = (char *)buf;
    for (;;) {
        uint32_t key = irq_lock();
        size_t n = 0;
        while (c->tail != c->head) {
            const char *line = c->q[c->tail % UEV_QUEUE];
            size_t l = strlen(line);
            if (n + l > len)
                break;
            memcpy(out + n, line, l);
            n += l;
            c->tail++;
        }
        if (n || (f->flags & VFS_O_NONBLOCK)) {
            irq_unlock(key);
            return n ? (int)n : -EAGAIN;
        }
        if (len < UEV_LINE) {
            irq_unlock(key);
            return -EINVAL;
        }
        int r = sched_block(&c->wq, WAIT_FOREVER, key);
        if (r == -EINTR)
            return r;
    }
}

static int uev_poll(struct file *f, struct poll_entry *e)
{
    struct uev_client *c = (struct uev_client *)f->priv;
    uint32_t key = irq_lock();
    int mask = c->head != c->tail ? POLLIN : 0;
    poll_add(&c->ph, e);
    irq_unlock(key);
    return mask;
}

static int uev_close(struct file *f)
{
    struct uev_client *c = (struct uev_client *)f->priv;
    uint32_t key = irq_lock();
    list_del(&c->node);
    irq_unlock(key);
    kfree(c);
    return 0;
}

static const struct file_ops uev_ops = {
    uev_open, uev_read, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, uev_close, uev_poll,
};

void uevent_init(void)
{
    devfs_register("uevent", &uev_ops, nullptr);
}
