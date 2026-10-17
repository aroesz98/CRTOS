/*
 * subsys/net.cpp - network interfaces and sockets (crtos/net.h, crtos/socket.h).
 *
 * Interfaces: drivers register struct netdev; the protocol stack (a module) attaches to
 * each and gets its frames. Sockets are files whose operations go to the stack. The stack
 * never blocks: it answers -EAGAIN, and reports changes with net_sock_event(). The waiting
 * happens here - interruptible (a process being ended leaves at once) and with the
 * SO_RCVTIMEO / SO_SNDTIMEO time-outs, which are kept here and not passed on. Because the
 * stack only runs in the calling thread while a call lasts, it never keeps a pointer into
 * a process's memory after the call returns.
 *
 * The path of the data (send, receive, the stack's events, received frames) runs from ITCM
 * (KERNEL_FAST): from flash, through the instruction cache shared with the stack and the
 * program, it took a tenth of the processor at full speed.
 */
#include "kernel.h"
#include <crtos/net.h>
#include <crtos/poll.h>
#include <crtos/syscall.h>
#include <crtos/vfs.h>
#include <string.h>

static struct mutex s_lock = MUTEX_INIT(s_lock);
static struct list_head s_devs = LIST_HEAD_INIT(s_devs);
static int s_ndevs, s_next_eth;
static const struct net_stack_ops *s_stack;
static void *s_stack_ctx;
static int s_nsocks;

struct net_sock {
    int s;                          /* the stack's socket, -1: closed */
    int type;
    volatile uint32_t events;       /* moves on at every net_sock_event() */
    struct wait_queue wq;
    struct poll_head ph;
    uint32_t rcvtimeo, sndtimeo;    /* ms, 0: none */
};

/* ---- interfaces ------------------------------------------------------------------------------ */

void net_lock(void)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
}

void net_unlock(void)
{
    mutex_unlock(&s_lock);
}

int netdev_register(struct netdev *nd)
{
    net_lock();
    ksnprintf(nd->name, sizeof(nd->name), "eth%d", s_next_eth++);
    if (!nd->mtu)
        nd->mtu = 1500;
    list_add_tail(&nd->node, &s_devs);
    s_ndevs++;
    if (s_stack)
        s_stack->attach(s_stack_ctx, nd);
    net_unlock();
    printk("net: %s, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", nd->name, nd->mac[0], nd->mac[1], nd->mac[2], nd->mac[3],
           nd->mac[4], nd->mac[5]);
    return 0;
}

void netdev_unregister(struct netdev *nd)
{
    net_lock();
    if (s_stack)
        s_stack->detach(s_stack_ctx, nd);
    list_del(&nd->node);
    s_ndevs--;
    net_unlock();
}

KERNEL_FAST void netdev_rx(struct netdev *nd, const void *frame, size_t len)
{
    nd->rx_packets++;
    nd->rx_bytes += (uint32_t)len;
    const struct net_stack_ops *st = s_stack;
    if (st && nd->stack)
        st->rx(s_stack_ctx, nd, frame, len);
    else
        nd->rx_dropped++;
}

void netdev_set_link(struct netdev *nd, bool up, uint32_t speed, bool full_duplex)
{
    bool changed = nd->link_up != up || nd->speed != speed || nd->full_duplex != full_duplex;
    nd->link_up = up;
    nd->speed = speed;
    nd->full_duplex = full_duplex;
    if (!changed)
        return;
    if (up)
        printk("net: %s link up, %lu Mbit/s %s duplex\n", nd->name, (unsigned long)speed, full_duplex ? "full" : "half");
    else
        printk("net: %s link down\n", nd->name);
    net_lock();
    if (s_stack && nd->stack)
        s_stack->link(s_stack_ctx, nd);
    net_unlock();
}

int netdev_count(void)
{
    return s_ndevs;
}

struct netdev *netdev_get(int index)
{
    struct list_head *pos;
    list_for_each(pos, &s_devs) {
        if (!index--)
            return list_entry(pos, struct netdev, node);
    }
    return nullptr;
}

struct netdev *netdev_by_name(const char *name)
{
    struct list_head *pos;
    list_for_each(pos, &s_devs) {
        struct netdev *nd = list_entry(pos, struct netdev, node);
        if (!strcmp(nd->name, name))
            return nd;
    }
    return nullptr;
}

int net_stack_register(const struct net_stack_ops *ops, void *ctx)
{
    net_lock();
    if (s_stack) {
        net_unlock();
        return -EBUSY;
    }
    s_stack = ops;
    s_stack_ctx = ctx;
    struct list_head *pos;
    list_for_each(pos, &s_devs) ops->attach(ctx, list_entry(pos, struct netdev, node));
    net_unlock();
    return 0;
}

void net_stack_unregister(void)
{
    net_lock();
    if (s_nsocks)
        printk("W: net: the stack goes while %d socket(s) are open\n", s_nsocks);
    struct list_head *pos;
    if (s_stack)
        list_for_each(pos, &s_devs) s_stack->detach(s_stack_ctx, list_entry(pos, struct netdev, node));
    s_stack = nullptr;
    s_stack_ctx = nullptr;
    net_unlock();
}

KERNEL_FAST void net_sock_event(struct net_sock *ks)
{
    uint32_t key = irq_lock();
    ks->events++;
    wq_wake_all(&ks->wq, 0);
    poll_notify(&ks->ph);
    irq_unlock(key);
}

/* ---- sockets as files ------------------------------------------------------------------------ */

static struct net_sock *sock_new(int type)
{
    struct net_sock *ks = (struct net_sock *)kzalloc(sizeof(*ks), KM_ANY);
    if (!ks)
        return nullptr;
    ks->s = -1;
    ks->type = type;
    wq_init(&ks->wq);
    poll_head_init(&ks->ph);
    return ks;
}

static void sock_free(struct net_sock *ks)
{
    const struct net_stack_ops *st = s_stack;
    if (ks->s >= 0 && st)
        st->close(s_stack_ctx, ks->s);
    ks->s = -1;
    uint32_t key = irq_lock();
    s_nsocks--;
    irq_unlock(key);
    kfree(ks);
}

/* Wait until the socket's state may have changed after the events count @seen */
static KERNEL_FAST int sock_wait(struct net_sock *ks, uint32_t seen, uint64_t deadline_us)
{
    uint32_t timeout = WAIT_FOREVER;
    if (deadline_us) {
        uint64_t now = time_us();
        if (now >= deadline_us)
            return -ETIMEDOUT;
        uint64_t left = deadline_us - now; /* (a 64-bit division here took more than the rest) */
        timeout = left < 0xFFFF0000u ? ((uint32_t)left + 999u) / 1000u : (uint32_t)(left / 1000u);
    }
    uint32_t key = irq_lock();
    if (ks->events != seen) {
        irq_unlock(key);
        return 0;
    }
    int r = sched_block(&ks->wq, timeout, key);
    return r == -ETIMEDOUT || r == -EINTR ? r : 0;
}

static inline uint64_t deadline(uint32_t timeout_ms)
{
    return timeout_ms ? time_us() + (uint64_t)timeout_ms * 1000u : 0;
}

static inline bool nonblocking(struct file *f, int flags)
{
    return (f->flags & VFS_O_NONBLOCK) || (flags & MSG_DONTWAIT);
}

static KERNEL_FAST int sock_recv(struct file *f, struct net_sock *ks, void *buf, size_t len, int flags,
                                 struct crtos_sockaddr_in *from)
{
    uint64_t dl = deadline(ks->rcvtimeo);
    for (;;) {
        const struct net_stack_ops *st = s_stack;
        if (!st || ks->s < 0)
            return -ENETDOWN;
        uint32_t seen = ks->events;
        int r = st->recv(s_stack_ctx, ks->s, buf, len, (flags & ~MSG_WAITALL) | MSG_DONTWAIT, from);
        if (r != -EAGAIN || nonblocking(f, flags))
            return r;
        r = sock_wait(ks, seen, dl);
        if (r)
            return r == -ETIMEDOUT ? -EAGAIN : r;
    }
}

static KERNEL_FAST int sock_send(struct file *f, struct net_sock *ks, const void *buf, size_t len, int flags,
                                 const struct crtos_sockaddr_in *to)
{
    uint64_t dl = deadline(ks->sndtimeo);
    size_t done = 0;
    for (;;) {
        const struct net_stack_ops *st = s_stack;
        if (!st || ks->s < 0)
            return done ? (int)done : -ENETDOWN;
        uint32_t seen = ks->events;
        int r = st->send(s_stack_ctx, ks->s, (const uint8_t *)buf + done, len - done, flags | MSG_DONTWAIT, to);
        if (r > 0) {
            done += (size_t)r;
            if (done >= len || ks->type != SOCK_STREAM) /* datagrams go whole */
                return (int)done;
            continue;
        }
        if (r == 0 && len)
            r = -EAGAIN;
        if (r != -EAGAIN || nonblocking(f, flags))
            return done ? (int)done : r;
        r = sock_wait(ks, seen, dl);
        if (r)
            return done ? (int)done : (r == -ETIMEDOUT ? -EAGAIN : r);
    }
}

static KERNEL_FAST int sock_read(struct file *f, void *buf, size_t len)
{
    return sock_recv(f, (struct net_sock *)f->priv, buf, len, 0, nullptr);
}

static KERNEL_FAST int sock_write(struct file *f, const void *buf, size_t len)
{
    return sock_send(f, (struct net_sock *)f->priv, buf, len, 0, nullptr);
}

static int sock_ioctl(struct file *f, unsigned cmd, void *arg)
{
    struct net_sock *ks = (struct net_sock *)f->priv;
    const struct net_stack_ops *st = s_stack;
    if (!st)
        return -ENETDOWN;
    if ((cmd == NET_IOC_IFCONF || cmd == NET_IOC_DNS_SET) && !(g_current->proc->caps & CAP_SYS))
        return -EPERM;
    /* copy the argument in and out: the stack sees kernel memory only */
    union {
        uint32_t u;
        struct net_ifinfo info;
        struct net_ifconf conf;
        struct net_dns dns;
        struct net_resolve res;
    } k;
    size_t size = _IOC_SIZE(cmd);
    if (size > sizeof(k))
        return -ENOTTY;
    if (size && copy_from_user(&k, arg, size))
        return -EFAULT;
    if (cmd == NET_IOC_RESOLVE)
        k.res.name[sizeof(k.res.name) - 1] = 0;
    int r = st->ioctl(s_stack_ctx, ks->s, cmd, &k);
    if (!r && size && (_IOC_DIR(cmd) & _IOC_READ) && copy_to_user(arg, &k, size))
        return -EFAULT;
    return r;
}

static KERNEL_FAST int sock_poll(struct file *f, struct poll_entry *e)
{
    struct net_sock *ks = (struct net_sock *)f->priv;
    const struct net_stack_ops *st = s_stack;
    uint32_t key = irq_lock();
    poll_add(&ks->ph, e);
    irq_unlock(key);
    if (!st || ks->s < 0)
        return POLLERR | POLLHUP;
    return st->poll(s_stack_ctx, ks->s);
}

static int sock_fstat(struct file *, struct vfs_stat *st)
{
    memset(st, 0, sizeof(*st));
    st->mode = VFS_S_IFSOCK;
    return 0;
}

static int sock_close(struct file *f)
{
    sock_free((struct net_sock *)f->priv);
    return 0;
}

static const struct file_ops sock_ops = {
    nullptr, sock_read, sock_write, nullptr, sock_ioctl, nullptr, sock_fstat, nullptr, sock_close, sock_poll,
};

/* ---- system calls ------------------------------------------------------------------------------- */

/* The socket behind handle @h (a reference: vfs_close() it), or NULL with *err set */
static KERNEL_FAST struct file *sock_file(int h, struct net_sock **ks, int *err)
{
    uint8_t type = H_FILE;
    struct file *f = (struct file *)handle_ref(g_current->proc, h, &type, nullptr);
    if (!f) {
        *err = -EBADF;
        return nullptr;
    }
    if (f->ops != &sock_ops) {
        vfs_close(f);
        *err = -ENOTSOCK;
        return nullptr;
    }
    *ks = (struct net_sock *)f->priv;
    return f;
}

#define SOCK_FILE(h)                                                                                    \
    struct net_sock *ks;                                                                                \
    int ferr_ = 0;                                                                                      \
    struct file *f = sock_file((h), &ks, &ferr_);                                                       \
    if (!f)                                                                                             \
        return ferr_;

static int addr_in(const void *uaddr, uint32_t len, struct crtos_sockaddr_in *a)
{
    memset(a, 0, sizeof(*a));
    if (len < 8)
        return -EINVAL;
    if (copy_from_user(a, uaddr, len < sizeof(*a) ? len : sizeof(*a)))
        return -EFAULT;
    return a->sin_family == AF_INET || a->sin_family == AF_UNSPEC ? 0 : -EAFNOSUPPORT;
}

static int addr_out(const struct crtos_sockaddr_in *a, void *uaddr, uint32_t *ulen)
{
    if (!uaddr || !ulen)
        return 0;
    uint32_t len;
    if (copy_from_user(&len, ulen, sizeof(len)))
        return -EFAULT;
    if (len > sizeof(*a))
        len = sizeof(*a);
    uint32_t full = sizeof(*a);
    if (copy_to_user(uaddr, a, len) || copy_to_user(ulen, &full, sizeof(full)))
        return -EFAULT;
    return 0;
}

static int64_t sock_install(struct net_sock *ks, uint32_t flags)
{
    struct file *f = vfs_file_new(&sock_ops, ks, VFS_O_RDWR | (flags & SOCK_NONBLOCK ? VFS_O_NONBLOCK : 0u));
    if (!f) {
        sock_free(ks);
        return -ENOMEM;
    }
    int h = handle_install(g_current->proc, H_FILE, 0, f, 0);
    if (h < 0)
        vfs_close(f);
    return h;
}

int64_t sys_socket(int domain, int type, int protocol)
{
    const struct net_stack_ops *st = s_stack;
    if (!st)
        return -EAFNOSUPPORT;
    int base = type & 0xFF;
    if (domain != AF_INET || (base != SOCK_STREAM && base != SOCK_DGRAM && base != SOCK_RAW))
        return domain != AF_INET ? -EAFNOSUPPORT : -EPROTOTYPE;
    struct net_sock *ks = sock_new(base);
    if (!ks)
        return -ENOMEM;
    uint32_t key = irq_lock();
    s_nsocks++;
    irq_unlock(key);
    int s = st->socket(s_stack_ctx, domain, base, protocol, ks);
    if (s < 0) {
        sock_free(ks);
        return s;
    }
    ks->s = s;
    return sock_install(ks, (uint32_t)type);
}

int64_t sys_bind(int h, const void *uaddr, uint32_t len)
{
    SOCK_FILE(h);
    struct crtos_sockaddr_in a;
    int r = addr_in(uaddr, len, &a);
    if (!r)
        r = s_stack ? s_stack->bind(s_stack_ctx, ks->s, &a) : -ENETDOWN;
    vfs_close(f);
    return r;
}

int64_t sys_connect(int h, const void *uaddr, uint32_t len)
{
    SOCK_FILE(h);
    struct crtos_sockaddr_in a;
    int r = addr_in(uaddr, len, &a);
    if (!r) {
        uint32_t seen = ks->events;
        r = s_stack ? s_stack->connect(s_stack_ctx, ks->s, &a) : -ENETDOWN;
        if (r == -EINPROGRESS && !nonblocking(f, 0)) { /* wait for the handshake */
            uint64_t dl = deadline(ks->sndtimeo);
            for (;;) {
                const struct net_stack_ops *st = s_stack;
                if (!st) {
                    r = -ENETDOWN;
                    break;
                }
                int m = st->poll(s_stack_ctx, ks->s);
                if (m & (POLLOUT | POLLERR | POLLHUP)) {
                    int err = 0;
                    uint32_t el = sizeof(err);
                    st->getsockopt(s_stack_ctx, ks->s, SOL_SOCKET, SO_ERROR, &err, &el);
                    r = err ? -err : 0;
                    break;
                }
                r = sock_wait(ks, seen, dl);
                if (r) {
                    r = r == -ETIMEDOUT ? -ETIMEDOUT : r;
                    break;
                }
                seen = ks->events;
            }
        }
    }
    vfs_close(f);
    return r;
}

int64_t sys_listen(int h, int backlog)
{
    SOCK_FILE(h);
    int r = s_stack ? s_stack->listen(s_stack_ctx, ks->s, backlog) : -ENETDOWN;
    vfs_close(f);
    return r;
}

int64_t sys_accept(int h, void *uaddr, uint32_t *ulen)
{
    SOCK_FILE(h);
    struct net_sock *nks = sock_new(ks->type);
    if (!nks) {
        vfs_close(f);
        return -ENOMEM;
    }
    uint32_t key = irq_lock();
    s_nsocks++;
    irq_unlock(key);
    struct crtos_sockaddr_in peer;
    memset(&peer, 0, sizeof(peer));
    int r;
    uint64_t dl = deadline(ks->rcvtimeo);
    for (;;) {
        const struct net_stack_ops *st = s_stack;
        if (!st) {
            r = -ENETDOWN;
            break;
        }
        uint32_t seen = ks->events;
        r = st->accept(s_stack_ctx, ks->s, &peer, nks);
        if (r != -EAGAIN || nonblocking(f, 0))
            break;
        r = sock_wait(ks, seen, dl);
        if (r) {
            r = r == -ETIMEDOUT ? -EAGAIN : r;
            break;
        }
    }
    vfs_close(f);
    if (r < 0) {
        sock_free(nks);
        return r;
    }
    nks->s = r;
    int64_t nh = sock_install(nks, 0);
    if (nh >= 0 && addr_out(&peer, uaddr, ulen)) {
        handle_close(g_current->proc, (int)nh);
        return -EFAULT;
    }
    return nh;
}

KERNEL_FAST int64_t sys_sendto(int h, const void *buf, uint32_t len, int flags, const void *uaddr, uint32_t alen)
{
    SOCK_FILE(h);
    int r = 0;
    struct crtos_sockaddr_in a;
    if (len && !uaccess_ok(buf, len, 0))
        r = -EFAULT;
    else if (uaddr)
        r = addr_in(uaddr, alen, &a);
    if (!r)
        r = sock_send(f, ks, buf, len, flags, uaddr ? &a : nullptr);
    vfs_close(f);
    return r;
}

KERNEL_FAST int64_t sys_recvfrom(int h, void *buf, uint32_t len, int flags, void *uaddr, uint32_t *ulen)
{
    SOCK_FILE(h);
    if (len && !uaccess_ok(buf, len, 1)) {
        vfs_close(f);
        return -EFAULT;
    }
    struct crtos_sockaddr_in from;
    memset(&from, 0, sizeof(from));
    int r = sock_recv(f, ks, buf, len, flags, &from);
    if (r >= 0 && uaddr && addr_out(&from, uaddr, ulen))
        r = -EFAULT;
    vfs_close(f);
    return r;
}

int64_t sys_shutdown(int h, int how)
{
    SOCK_FILE(h);
    int r = s_stack ? s_stack->shutdown(s_stack_ctx, ks->s, how) : -ENETDOWN;
    vfs_close(f);
    return r;
}

/* struct timeval of newlib: 64-bit seconds, 32-bit microseconds */
struct k_timeval {
    int64_t tv_sec;
    int32_t tv_usec;
    int32_t pad;
};

int64_t sys_setsockopt(int h, int level, int name, const void *uval, uint32_t len)
{
    SOCK_FILE(h);
    int r;
    uint8_t val[16];
    if (len > sizeof(val) || (len && copy_from_user(val, uval, len))) {
        r = len > sizeof(val) ? -EINVAL : -EFAULT;
    } else if (level == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO)) {
        struct k_timeval tv;
        memset(&tv, 0, sizeof(tv));
        memcpy(&tv, val, len < sizeof(tv) ? len : sizeof(tv));
        uint64_t ms = (uint64_t)(tv.tv_sec < 0 ? 0 : tv.tv_sec) * 1000u + (uint32_t)(tv.tv_usec < 0 ? 0 : tv.tv_usec) / 1000u;
        uint32_t v = ms > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)ms;
        if (!v && (tv.tv_sec || tv.tv_usec))
            v = 1;
        if (name == SO_RCVTIMEO)
            ks->rcvtimeo = v;
        else
            ks->sndtimeo = v;
        r = 0;
    } else {
        r = s_stack ? s_stack->setsockopt(s_stack_ctx, ks->s, level, name, val, len) : -ENETDOWN;
    }
    vfs_close(f);
    return r;
}

int64_t sys_getsockopt(int h, int level, int name, void *uval, uint32_t *ulen)
{
    SOCK_FILE(h);
    uint32_t len;
    int r = copy_from_user(&len, ulen, sizeof(len));
    uint8_t val[16];
    memset(val, 0, sizeof(val));
    if (!r && len > sizeof(val))
        len = sizeof(val);
    if (!r) {
        if (level == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO)) {
            uint32_t ms = name == SO_RCVTIMEO ? ks->rcvtimeo : ks->sndtimeo;
            struct k_timeval tv = { ms / 1000u, (int32_t)(ms % 1000u) * 1000, 0 };
            memcpy(val, &tv, sizeof(tv));
            len = len < sizeof(tv) ? len : sizeof(tv);
        } else if (level == SOL_SOCKET && name == SO_TYPE) {
            memcpy(val, &ks->type, sizeof(int));
            len = len < sizeof(int) ? len : sizeof(int);
        } else {
            r = s_stack ? s_stack->getsockopt(s_stack_ctx, ks->s, level, name, val, &len) : -ENETDOWN;
        }
    }
    if (!r && (copy_to_user(uval, val, len) || copy_to_user(ulen, &len, sizeof(len))))
        r = -EFAULT;
    vfs_close(f);
    return r;
}

static int64_t getname(int h, int peer, void *uaddr, uint32_t *ulen)
{
    SOCK_FILE(h);
    struct crtos_sockaddr_in a;
    memset(&a, 0, sizeof(a));
    int r = s_stack ? s_stack->getname(s_stack_ctx, ks->s, peer, &a) : -ENETDOWN;
    if (!r)
        r = addr_out(&a, uaddr, ulen);
    vfs_close(f);
    return r;
}

int64_t sys_getsockname(int h, void *uaddr, uint32_t *ulen)
{
    return getname(h, 0, uaddr, ulen);
}

int64_t sys_getpeername(int h, void *uaddr, uint32_t *ulen)
{
    return getname(h, 1, uaddr, ulen);
}

/* kmon: interfaces and the socket count */
void net_dump(void)
{
    net_lock();
    struct list_head *pos;
    list_for_each(pos, &s_devs) {
        struct netdev *nd = list_entry(pos, struct netdev, node);
        cprintf("%-5s %02x:%02x:%02x:%02x:%02x:%02x link %s %lu Mbit/s  rx %lu (%lu B, %lu dropped, %lu errors)"
                "  tx %lu (%lu B, %lu dropped, %lu errors)\n",
                nd->name, nd->mac[0], nd->mac[1], nd->mac[2], nd->mac[3], nd->mac[4], nd->mac[5],
                nd->link_up ? "up" : "down", (unsigned long)nd->speed, (unsigned long)nd->rx_packets,
                (unsigned long)nd->rx_bytes, (unsigned long)nd->rx_dropped, (unsigned long)nd->rx_errors,
                (unsigned long)nd->tx_packets, (unsigned long)nd->tx_bytes, (unsigned long)nd->tx_dropped,
                (unsigned long)nd->tx_errors);
    }
    cprintf("stack: %s, %d socket(s) open\n", s_stack ? "loaded" : "none", s_nsocks);
    net_unlock();
}
