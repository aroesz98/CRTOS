/*
 * kernel/os/ipc.cpp - message ports.
 *
 * A port is a message queue with one receiving end (the handle returned by port_create)
 * and any number of sending ends (port_connect by name, or a handle passed in a message).
 * Messages are up to MSG_MAX bytes and may carry one handle (file, port send right, shared
 * memory); they are copied into kernel memory when sent and out of it when received, so
 * no process ever touches another one's memory. msg_call() sends and waits for the reply
 * that the receiver returns with msg_reply(token). When the receiving end is closed (or its
 * process ends) the port is dead: queued and pending calls fail with -EPIPE.
 *
 * Queues, the name registry and call states are protected by irq_lock(); copies and
 * allocations happen outside of it.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include <crtos/syscall.h>
#include <string.h>

#define PORT_QUEUE_MAX 32u

struct port {
    uint32_t refs;
    char name[PORT_NAME_MAX];
    bool dead;
    bool registered;
    int owner_pid;
    struct list_head reg_node;
    struct list_head queue;
    uint32_t qlen;
    struct wait_queue recv_wq;
    struct wait_queue send_wq;
    struct poll_head ph;
};

struct call;

struct kmsg {
    struct list_head node;
    struct call *call;          /* the sender waits for a reply */
    int pid;
    uint32_t len;
    uint8_t xtype, xrights;     /* handle that travels with the message */
    void *xobj;
    uint8_t data[];
};

enum : uint8_t { CALL_QUEUED, CALL_RECEIVED, CALL_DONE };

struct call {
    struct list_head node;      /* in s_calls while the server works on it */
    uint32_t token;
    struct port *port;
    struct wait_queue wq;       /* the caller sleeps here */
    uint8_t state;
    int result;                 /* reply length or -errno */
    struct kmsg *msg;           /* request while queued */
    struct kmsg *reply;
};

static struct list_head s_registry = LIST_HEAD_INIT(s_registry);
static struct wait_queue s_reg_wq = { LIST_HEAD_INIT(s_reg_wq.waiters) };
static struct list_head s_calls = LIST_HEAD_INIT(s_calls);
static uint32_t s_next_token = 1;

void port_get(struct port *pt)
{
    uint32_t key = irq_lock();
    pt->refs++;
    irq_unlock(key);
}

void port_put(struct port *pt)
{
    uint32_t key = irq_lock();
    bool last = --pt->refs == 0;
    irq_unlock(key);
    if (last)
        kfree(pt);
}

static void msg_free(struct kmsg *m)
{
    if (m->xobj)
        obj_put(m->xtype, m->xobj);
    kfree(m);
}

/* irq locked: finish a call and wake its caller */
static void call_finish(struct call *c, int result, struct kmsg *reply)
{
    c->state = CALL_DONE;
    c->result = result;
    c->reply = reply;
    wq_wake_all(&c->wq, 0);
}

void port_close_recv(struct port *pt)
{
    struct list_head dropped;
    list_init(&dropped);
    uint32_t key = irq_lock();
    pt->dead = true;
    if (pt->registered) {
        list_del(&pt->reg_node);
        pt->registered = false;
    }
    while (!list_empty(&pt->queue)) {
        struct kmsg *m = list_first_entry(&pt->queue, struct kmsg, node);
        list_del(&m->node);
        if (m->call) {
            m->call->msg = nullptr;
            call_finish(m->call, -EPIPE, nullptr);
        }
        list_add_tail(&m->node, &dropped);
    }
    pt->qlen = 0;
    struct list_head *pos, *tmp;
    list_for_each_safe(pos, tmp, &s_calls) { /* received but never answered */
        struct call *c = list_entry(pos, struct call, node);
        if (c->port == pt) {
            list_del(&c->node);
            call_finish(c, -EPIPE, nullptr);
        }
    }
    wq_wake_all(&pt->send_wq, -EPIPE);
    wq_wake_all(&pt->recv_wq, -EPIPE);
    poll_notify(&pt->ph);
    irq_unlock(key);
    while (!list_empty(&dropped)) {
        struct kmsg *m = list_first_entry(&dropped, struct kmsg, node);
        list_del(&m->node);
        msg_free(m);
    }
}

int port_poll(struct port *pt, uint8_t rights, struct poll_entry *e)
{
    uint32_t key = irq_lock();
    int mask = 0;
    if (rights & HR_RECV)
        mask |= pt->qlen ? POLLIN : 0;
    else
        mask |= pt->qlen < PORT_QUEUE_MAX ? POLLOUT : 0;
    if (pt->dead)
        mask |= POLLHUP;
    poll_add(&pt->ph, e);
    irq_unlock(key);
    return mask;
}

static int remaining(uint32_t timeout, uint32_t deadline, uint32_t *left)
{
    if (timeout == WAIT_FOREVER) {
        *left = WAIT_FOREVER;
        return 1;
    }
    int32_t rem = (int32_t)(deadline - g_ticks);
    if (timeout == NO_WAIT || rem <= 0)
        return 0;
    *left = (uint32_t)rem;
    return 1;
}

/* Build a message: copy the data from the caller, take the handle to pass along */
static int msg_build(struct proc *p, const void *data, uint32_t len, int xh, struct kmsg **out)
{
    if (len > MSG_MAX)
        return -EMSGSIZE;
    if (len && !uaccess_ok(data, len, 0))
        return -EFAULT;
    struct kmsg *m = (struct kmsg *)kmalloc(sizeof(*m) + len, KM_ANY);
    if (!m)
        return -ENOMEM;
    list_init(&m->node);
    m->call = nullptr;
    m->pid = p ? p->pid : 0;
    m->len = len;
    m->xobj = nullptr;
    m->xtype = H_FREE;
    m->xrights = 0;
    if (len)
        memcpy(m->data, data, len);
    if (xh >= 0) {
        uint8_t type = H_FREE, rights = 0;
        void *obj = handle_ref(p, xh, &type, &rights);
        if (!obj) {
            kfree(m);
            return -EBADF;
        }
        m->xobj = obj;
        m->xtype = type;
        m->xrights = rights & (uint8_t)~HR_RECV; /* the receiving end never moves */
    }
    *out = m;
    return 0;
}

/* Queue @m on @pt, waiting for room */
static int enqueue(struct port *pt, struct kmsg *m, uint32_t timeout)
{
    uint32_t deadline = g_ticks + timeout;
    for (;;) {
        uint32_t key = irq_lock();
        if (pt->dead) {
            irq_unlock(key);
            return -EPIPE;
        }
        if (pt->qlen < PORT_QUEUE_MAX) {
            list_add_tail(&m->node, &pt->queue);
            pt->qlen++;
            if (m->call)
                m->call->state = CALL_QUEUED;
            wq_wake_one(&pt->recv_wq, 0);
            poll_notify(&pt->ph);
            irq_unlock(key);
            return 0;
        }
        uint32_t left;
        if (!remaining(timeout, deadline, &left)) {
            irq_unlock(key);
            return -ETIMEDOUT;
        }
        int r = sched_block(&pt->send_wq, left, key);
        if (r == -EINTR || r == -EPIPE)
            return r;
    }
}

/* Put a received handle into @p's table; -1 if there is none or no room */
static int32_t take_handle(struct proc *p, uint8_t type, uint8_t rights, void *obj)
{
    if (!obj)
        return -1;
    int h = handle_install(p, type, rights, obj, 0);
    if (h < 0) {
        obj_put(type, obj);
        return -1;
    }
    return h;
}

/* ---- system calls ---------------------------------------------------------------------------- */

static struct port *port_of(struct proc *p, int h, uint8_t *rights)
{
    uint8_t type = H_PORT;
    return (struct port *)handle_ref(p, h, &type, rights);
}

int64_t sys_port_create(const char *uname, uint32_t flags)
{
    (void)flags;
    struct proc *p = g_current->proc;
    struct port *pt = (struct port *)kzalloc(sizeof(*pt), KM_ANY);
    if (!pt)
        return -ENOMEM;
    if (uname) {
        int n = strncpy_from_user(pt->name, uname, sizeof(pt->name));
        if (n <= 0) {
            kfree(pt);
            return n < 0 ? n : -EINVAL;
        }
    }
    pt->refs = 1;
    pt->owner_pid = p ? p->pid : 0;
    list_init(&pt->queue);
    list_init(&pt->reg_node);
    wq_init(&pt->recv_wq);
    wq_init(&pt->send_wq);
    poll_head_init(&pt->ph);
    if (pt->name[0]) {
        uint32_t key = irq_lock();
        struct list_head *pos;
        list_for_each(pos, &s_registry) {
            if (!strcmp(list_entry(pos, struct port, reg_node)->name, pt->name)) {
                irq_unlock(key);
                kfree(pt);
                return -EEXIST;
            }
        }
        list_add_tail(&pt->reg_node, &s_registry);
        pt->registered = true;
        wq_wake_all(&s_reg_wq, 0);
        irq_unlock(key);
    }
    int h = handle_install(p, H_PORT, HR_RECV, pt, 0);
    if (h < 0) {
        port_close_recv(pt);
        port_put(pt);
    }
    return h;
}

int64_t sys_port_connect(const char *uname, uint32_t timeout)
{
    char name[PORT_NAME_MAX];
    int n = strncpy_from_user(name, uname, sizeof(name));
    if (n <= 0)
        return n < 0 ? n : -EINVAL;
    uint32_t deadline = g_ticks + timeout;
    for (;;) {
        uint32_t key = irq_lock();
        struct port *found = nullptr;
        struct list_head *pos;
        list_for_each(pos, &s_registry) {
            struct port *pt = list_entry(pos, struct port, reg_node);
            if (!strcmp(pt->name, name)) {
                found = pt;
                pt->refs++;
                break;
            }
        }
        if (found) {
            irq_unlock(key);
            int h = handle_install(g_current->proc, H_PORT, 0, found, 0);
            if (h < 0)
                port_put(found);
            return h;
        }
        uint32_t left;
        if (!remaining(timeout, deadline, &left)) {
            irq_unlock(key);
            return -ENOENT;
        }
        int r = sched_block(&s_reg_wq, left, key);
        if (r == -EINTR)
            return r;
    }
}

int64_t sys_msg_send(int h, const void *data, uint32_t len, int xh, uint32_t timeout)
{
    struct proc *p = g_current->proc;
    struct port *pt = port_of(p, h, nullptr);
    if (!pt)
        return -EBADF;
    struct kmsg *m;
    int r = msg_build(p, data, len, xh, &m);
    if (!r) {
        r = enqueue(pt, m, timeout);
        if (r)
            msg_free(m);
    }
    port_put(pt);
    return r;
}

int64_t sys_msg_recv(int h, void *buf, uint32_t max, struct crtos_msginfo *uinfo, uint32_t timeout)
{
    struct proc *p = g_current->proc;
    uint8_t rights = 0;
    struct port *pt = port_of(p, h, &rights);
    if (!pt)
        return -EBADF;
    if (!(rights & HR_RECV)) {
        port_put(pt);
        return -EPERM;
    }
    if ((max && !uaccess_ok(buf, max, 1)) || (uinfo && !uaccess_ok(uinfo, sizeof(*uinfo), 1))) {
        port_put(pt);
        return -EFAULT;
    }
    uint32_t deadline = g_ticks + timeout;
    struct kmsg *m = nullptr;
    uint32_t token = 0;
    int r = 0;
    for (;;) {
        uint32_t key = irq_lock();
        if (!list_empty(&pt->queue)) {
            m = list_first_entry(&pt->queue, struct kmsg, node);
            list_del(&m->node);
            pt->qlen--;
            if (m->call) {
                struct call *c = m->call;
                c->state = CALL_RECEIVED;
                c->msg = nullptr;
                token = c->token;
                list_add_tail(&c->node, &s_calls);
            }
            wq_wake_one(&pt->send_wq, 0);
            poll_notify(&pt->ph);
            irq_unlock(key);
            break;
        }
        if (pt->dead) {
            irq_unlock(key);
            r = -EPIPE;
            break;
        }
        uint32_t left;
        if (!remaining(timeout, deadline, &left)) {
            irq_unlock(key);
            r = -ETIMEDOUT;
            break;
        }
        r = sched_block(&pt->recv_wq, left, key);
        if (r == -EINTR || r == -EPIPE)
            break;
        r = 0;
    }
    port_put(pt);
    if (!m)
        return r;
    uint32_t n = m->len < max ? m->len : max;
    memcpy(buf, m->data, n);
    int32_t xh = take_handle(p, m->xtype, m->xrights, m->xobj);
    m->xobj = nullptr;
    if (uinfo) {
        uinfo->pid = m->pid;
        uinfo->len = m->len;
        uinfo->token = token;
        uinfo->handle = xh;
    }
    kfree(m);
    return (int64_t)n;
}

int64_t sys_msg_reply(uint32_t token, const void *data, uint32_t len, int xh)
{
    struct proc *p = g_current->proc;
    struct kmsg *rm;
    int r = msg_build(p, data, len, xh, &rm);
    if (r)
        return r;
    uint32_t key = irq_lock();
    struct call *found = nullptr;
    struct list_head *pos;
    list_for_each(pos, &s_calls) {
        struct call *c = list_entry(pos, struct call, node);
        if (c->token == token) {
            found = c;
            break;
        }
    }
    if (found) {
        list_del(&found->node);
        call_finish(found, (int)len, rm);
    }
    irq_unlock(key);
    if (!found) { /* the caller gave up (timeout, killed) */
        msg_free(rm);
        return -ENOENT;
    }
    return 0;
}

int64_t sys_msg_call(int h, struct crtos_call *uc, uint32_t timeout)
{
    struct proc *p = g_current->proc;
    struct crtos_call c;
    if (copy_from_user(&c, uc, sizeof(c)))
        return -EFAULT;
    if (c.rep_max && !uaccess_ok(c.rep, c.rep_max, 1))
        return -EFAULT;
    struct port *pt = port_of(p, h, nullptr);
    if (!pt)
        return -EBADF;
    struct kmsg *m;
    int r = msg_build(p, c.req, c.req_len, c.req_handle, &m);
    if (r) {
        port_put(pt);
        return r;
    }
    struct call call;
    memset(&call, 0, sizeof(call));
    list_init(&call.node);
    wq_init(&call.wq);
    call.port = pt;
    call.msg = m;
    uint32_t key = irq_lock();
    call.token = s_next_token++;
    if (!call.token)
        call.token = s_next_token++;
    irq_unlock(key);
    m->call = &call;

    uint32_t deadline = g_ticks + timeout;
    r = enqueue(pt, m, timeout);
    if (r) {
        msg_free(m);
        port_put(pt);
        return r;
    }
    for (;;) {
        key = irq_lock();
        if (call.state == CALL_DONE) {
            irq_unlock(key);
            break;
        }
        uint32_t left;
        int w = -ETIMEDOUT;
        if (remaining(timeout, deadline, &left))
            w = sched_block(&call.wq, left, key);
        else
            irq_unlock(key);
        if (w == -EINTR || w == -ETIMEDOUT) {
            /* give up, unless the reply made it meanwhile */
            struct kmsg *drop = nullptr;
            key = irq_lock();
            if (call.state == CALL_QUEUED) {
                list_del(&call.msg->node);
                pt->qlen--;
                drop = call.msg;
                wq_wake_one(&pt->send_wq, 0);
            } else if (call.state == CALL_RECEIVED) {
                list_del(&call.node);
            }
            bool done = call.state == CALL_DONE;
            if (!done)
                call.state = CALL_DONE;
            irq_unlock(key);
            if (drop)
                msg_free(drop);
            if (!done) {
                port_put(pt);
                return w;
            }
            break;
        }
    }
    port_put(pt);
    if (call.result < 0)
        return call.result;
    struct kmsg *rm = call.reply;
    uint32_t n = rm->len < c.rep_max ? rm->len : c.rep_max;
    memcpy(c.rep, rm->data, n);
    int32_t xh = take_handle(p, rm->xtype, rm->xrights, rm->xobj);
    rm->xobj = nullptr;
    kfree(rm);
    uc->rep_handle = xh; /* uc was checked by copy_from_user */
    return (int64_t)n;
}
