/*
 * subsys/input.cpp - input event devices (/dev/eventN).
 *
 * Events are copied into the queue of every open file of the device; input_report() only
 * takes the kernel lock, so drivers may report from interrupt handlers.
 */
#include "kernel.h"
#include <crtos/input.h>
#include <crtos/poll.h>
#include <crtos/vfs.h>
#include <string.h>

#define MAX_INPUTS  8
#define QUEUE_LEN   64u

struct input_client {
    struct input_dev *dev;
    struct input_event q[QUEUE_LEN];
    uint32_t head, tail;
    uint32_t dropped;
    struct event ev;            /* bit 0: queue not empty; bit 1: device gone */
    struct poll_head ph;
    struct list_head node;
};

struct input_dev {
    char name[32];
    char devname[12];
    int index;
    bool gone;
    uint32_t refs;              /* 1 while registered + one per open file */
    struct input_absinfo abs[INPUT_ABS_AXES];
    uint8_t keybits[INPUT_KEYBITS_SIZE];    /* the keys it can report */
    struct list_head clients;
};

static struct input_dev *s_inputs[MAX_INPUTS];

static void dev_put(struct input_dev *d)
{
    uint32_t key = irq_lock();
    bool last = --d->refs == 0;
    irq_unlock(key);
    if (last)
        kfree(d);
}

static int ev_open(struct file *f)
{
    struct input_dev *d = (struct input_dev *)f->dev;
    struct input_client *c = (struct input_client *)kzalloc(sizeof(*c), KM_ANY);
    if (!c)
        return -ENOMEM;
    c->dev = d;
    event_init(&c->ev);
    poll_head_init(&c->ph);
    uint32_t key = irq_lock();
    if (d->gone) {
        irq_unlock(key);
        kfree(c);
        return -ENODEV;
    }
    d->refs++;
    list_add_tail(&c->node, &d->clients);
    irq_unlock(key);
    f->priv = c;
    return 0;
}

static int ev_read(struct file *f, void *buf, size_t len)
{
    struct input_client *c = (struct input_client *)f->priv;
    size_t max = len / sizeof(struct input_event);
    if (!max)
        return -EINVAL;
    struct input_event *out = (struct input_event *)buf;
    for (;;) {
        uint32_t key = irq_lock();
        size_t n = 0;
        while (n < max && c->tail != c->head) {
            out[n++] = c->q[c->tail % QUEUE_LEN];
            c->tail++;
        }
        bool gone = c->dev->gone;
        if (!n)
            event_clear(&c->ev, 1u);
        irq_unlock(key);
        if (n)
            return (int)(n * sizeof(struct input_event));
        if (gone)
            return -ENODEV;
        if (f->flags & VFS_O_NONBLOCK)
            return -EAGAIN;
        int32_t r = event_wait(&c->ev, 3u, EVENT_ANY, WAIT_FOREVER);
        if (r < 0)
            return r;
    }
}

static int ev_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct input_client *c = (struct input_client *)f->priv;
    switch (cmd) {
    case INPUT_IOC_GET_NAME:
        strncpy((char *)arg, c->dev->name, 32);
        return 0;
    case INPUT_IOC_GET_ABS_X:
    case INPUT_IOC_GET_ABS_Y:
        memcpy(arg, &c->dev->abs[cmd == INPUT_IOC_GET_ABS_Y], sizeof(struct input_absinfo));
        return 0;
    case INPUT_IOC_GET_KEYBITS:
        memcpy(arg, c->dev->keybits, sizeof(c->dev->keybits));
        return 0;
    default:
        if (cmd >= INPUT_IOC_GET_ABS(0) && cmd < INPUT_IOC_GET_ABS(INPUT_ABS_AXES)) {
            uint32_t key = irq_lock(); /* (the value may change meanwhile) */
            memcpy(arg, &c->dev->abs[cmd - INPUT_IOC_GET_ABS(0)], sizeof(struct input_absinfo));
            irq_unlock(key);
            return 0;
        }
        return -ENOTTY;
    }
}

static int ev_poll(struct file *f, struct poll_entry *e)
{
    struct input_client *c = (struct input_client *)f->priv;
    uint32_t key = irq_lock();
    int mask = (c->head != c->tail ? POLLIN : 0) | (c->dev->gone ? POLLHUP : 0);
    poll_add(&c->ph, e);
    irq_unlock(key);
    return mask;
}

static int ev_close(struct file *f)
{
    struct input_client *c = (struct input_client *)f->priv;
    struct input_dev *d = c->dev;
    uint32_t key = irq_lock();
    list_del(&c->node);
    irq_unlock(key);
    kfree(c);
    dev_put(d);
    return 0;
}

static const struct file_ops event_ops = {
    ev_open, ev_read, nullptr, nullptr, ev_ioctl, nullptr, nullptr, nullptr, ev_close, ev_poll,
};

struct input_dev *input_register(const char *name)
{
    struct input_dev *d = (struct input_dev *)kzalloc(sizeof(*d), KM_ANY);
    if (!d)
        return nullptr;
    strncpy(d->name, name ? name : "input", sizeof(d->name) - 1);
    list_init(&d->clients);
    d->refs = 1;
    uint32_t key = irq_lock();
    int idx = -1;
    for (int i = 0; i < MAX_INPUTS && idx < 0; i++)
        if (!s_inputs[i])
            idx = i;
    if (idx >= 0)
        s_inputs[idx] = d;
    irq_unlock(key);
    if (idx < 0) {
        kfree(d);
        return nullptr;
    }
    d->index = idx;
    ksnprintf(d->devname, sizeof(d->devname), "event%d", idx);
    if (devfs_register(d->devname, &event_ops, d)) {
        s_inputs[idx] = nullptr;
        kfree(d);
        return nullptr;
    }
    printk("input: %s as /dev/%s\n", d->name, d->devname);
    return d;
}

void input_set_abs(struct input_dev *d, unsigned axis, int32_t min, int32_t max)
{
    if (axis < INPUT_ABS_AXES) {
        d->abs[axis].minimum = min;
        d->abs[axis].maximum = max;
    }
}

void input_set_key(struct input_dev *d, uint16_t code)
{
    if (code <= KEY_MAX)
        d->keybits[code / 8] |= (uint8_t)(1u << (code % 8));
}

void input_unregister(struct input_dev *d)
{
    devfs_unregister(d->devname);
    uint32_t key = irq_lock();
    d->gone = true;
    s_inputs[d->index] = nullptr;
    struct list_head *pos;
    list_for_each(pos, &d->clients) {
        struct input_client *c = list_entry(pos, struct input_client, node);
        event_set(&c->ev, 2u);
        poll_notify(&c->ph);
    }
    irq_unlock(key);
    dev_put(d);
}

void input_report(struct input_dev *d, uint16_t type, uint16_t code, int32_t value)
{
    uint64_t us = time_us();
    struct input_event e;
    e.sec = (uint32_t)(us / 1000000u);
    e.usec = (uint32_t)(us % 1000000u);
    e.type = type;
    e.code = code;
    e.value = value;
    if (type == EV_ABS && code < INPUT_ABS_AXES)
        d->abs[code].value = value;
    if (type == EV_KEY)
        input_set_key(d, code);
    uint32_t key = irq_lock();
    struct list_head *pos;
    list_for_each(pos, &d->clients) {
        struct input_client *c = list_entry(pos, struct input_client, node);
        if (c->head - c->tail >= QUEUE_LEN) { /* full: drop the oldest */
            c->tail++;
            c->dropped++;
        }
        c->q[c->head % QUEUE_LEN] = e;
        c->head++;
        if (type == EV_SYN) {
            event_set(&c->ev, 1u);
            poll_notify(&c->ph);
        }
    }
    irq_unlock(key);
}

void input_sync(struct input_dev *d)
{
    input_report(d, EV_SYN, SYN_REPORT, 0);
}

const char *input_devname(const struct input_dev *d)
{
    return d->devname;
}
