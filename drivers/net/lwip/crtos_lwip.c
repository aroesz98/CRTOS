/*
 * net-lwip.ko - the TCP/IP stack: lwIP 2.2 behind the kernel's network subsystem
 * (crtos/net.h). This half speaks the kernel's language (struct netdev, struct net_sock,
 * struct crtos_sockaddr_in, the NET_IOC_* ioctls), lwip_side.c lwIP's; lw.h is the narrow
 * interface between the two (their socket headers cannot be mixed).
 *
 * Threads: lwIP's tcpip thread runs the timers (TCP, ARP, DHCP, DNS). Received frames are
 * processed in the driver's receive thread and socket calls in the calling thread, both
 * under lwIP's core lock (LWIP_TCPIP_CORE_LOCKING). The sockets are used non-blocking only;
 * lwIP's socket events (sockets.c) wake the kernel socket, which does the waiting.
 * lwIP cannot be stopped once started, so the module stays loaded.
 */
#include <stdbool.h>
#include <string.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/module.h>
#include <crtos/net.h>
#include <crtos/poll.h>
#include <crtos/printk.h>
#include "lw.h"

static bool s_started;

/* ---- interfaces ------------------------------------------------------------------------------ */

int crtos_lwip_xmit(void *p, const void *frame, size_t len)
{
    struct netdev *nd = p;
    return nd->ops->xmit(nd, frame, len);
}

void crtos_lwip_status(void *p, uint32_t addr, uint32_t mask, uint32_t gw, int dhcp)
{
    struct netdev *nd = p;
    const uint8_t *a = (const uint8_t *)&addr, *g = (const uint8_t *)&gw;
    int bits = __builtin_popcount(mask);
    if (addr)
        printk("net: %s %u.%u.%u.%u/%d gateway %u.%u.%u.%u%s\n", nd->name, a[0], a[1], a[2], a[3], bits, g[0], g[1], g[2],
               g[3], dhcp ? " (DHCP)" : "");
    else
        printk("net: %s no address\n", nd->name);
}

static void st_attach(void *ctx, struct netdev *nd)
{
    (void)ctx;
    unsigned csum = (nd->features & NETDEV_F_TX_CSUM ? LW_TX_CSUM : 0u) | (nd->features & NETDEV_F_RX_CSUM ? LW_RX_CSUM : 0u);
    void *li = lw_if_add(nd, nd->mac, nd->mtu, csum);
    if (!li)
    {
        printk("E: net-lwip: cannot add %s\n", nd->name);
        return;
    }
    if (nd->link_up)
        lw_if_link(li, 1);
    nd->stack = li;
}

static void st_detach(void *ctx, struct netdev *nd)
{
    (void)ctx;
    void *li = nd->stack;
    nd->stack = NULL;
    lw_if_remove(li);
}

static void st_rx(void *ctx, struct netdev *nd, const void *frame, size_t len)
{
    (void)ctx;
    if (lw_if_input(nd->stack, frame, len))
        nd->rx_dropped++;
}

static void st_link(void *ctx, struct netdev *nd)
{
    (void)ctx;
    lw_if_link(nd->stack, nd->link_up);
}

/* ---- sockets --------------------------------------------------------------------------------- */

static void sa_out(struct crtos_sockaddr_in *a, uint32_t addr, uint16_t port)
{
    memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_port = port;
    a->sin_addr = addr;
}

static int st_socket(void *ctx, int domain, int type, int protocol, struct net_sock *ks)
{
    (void)ctx;
    return lw_socket(domain, type, protocol, ks);
}

static int st_close(void *ctx, int s)
{
    (void)ctx;
    return lw_close(s);
}

static int st_bind(void *ctx, int s, const struct crtos_sockaddr_in *a)
{
    (void)ctx;
    return lw_bind(s, a->sin_addr, a->sin_port);
}

static int st_connect(void *ctx, int s, const struct crtos_sockaddr_in *a)
{
    (void)ctx;
    if (a->sin_family == AF_UNSPEC)
        return lw_disconnect(s);
    return lw_connect(s, a->sin_addr, a->sin_port);
}

static int st_listen(void *ctx, int s, int backlog)
{
    (void)ctx;
    return lw_listen(s, backlog);
}

static int st_accept(void *ctx, int s, struct crtos_sockaddr_in *peer, struct net_sock *nks)
{
    (void)ctx;
    uint32_t addr = 0;
    uint16_t port = 0;
    int n = lw_accept(s, &addr, &port, nks);
    if (n >= 0)
        sa_out(peer, addr, port);
    return n;
}

static int st_send(void *ctx, int s, const void *buf, size_t len, int flags, const struct crtos_sockaddr_in *to)
{
    (void)ctx;
    return lw_send(s, buf, len, flags, to != NULL, to ? to->sin_addr : 0, to ? to->sin_port : 0);
}

static int st_recv(void *ctx, int s, void *buf, size_t len, int flags, struct crtos_sockaddr_in *from)
{
    (void)ctx;
    uint32_t addr = 0;
    uint16_t port = 0;
    int n = lw_recv(s, buf, len, flags, &addr, &port);
    if (n >= 0 && from)
        sa_out(from, addr, port);
    return n;
}

static int st_shutdown(void *ctx, int s, int how)
{
    (void)ctx;
    return lw_shutdown(s, how);
}

static int st_setsockopt(void *ctx, int s, int level, int name, const void *val, uint32_t len)
{
    (void)ctx;
    return lw_setsockopt(s, level, name, val, len);
}

static int st_getsockopt(void *ctx, int s, int level, int name, void *val, uint32_t *len)
{
    (void)ctx;
    return lw_getsockopt(s, level, name, val, len);
}

static int st_getname(void *ctx, int s, int peer, struct crtos_sockaddr_in *a)
{
    (void)ctx;
    uint32_t addr = 0;
    uint16_t port = 0;
    int r = lw_getname(s, peer, &addr, &port);
    if (!r)
        sa_out(a, addr, port);
    return r;
}

static int st_poll(void *ctx, int s)
{
    (void)ctx;
    int m = lw_poll(s), r = 0;
    if (m & LW_IN)
        r |= POLLIN;
    if (m & LW_OUT)
        r |= POLLOUT;
    if (m & LW_ERR)
        r |= POLLERR;
    if (m & LW_HUP)
        r |= POLLHUP;
    return r;
}

void crtos_lwip_event(void *owner)
{
    net_sock_event(owner);
}

/* ---- interface configuration (ioctls on any socket) ---------------------------------------------- */

static int if_info(struct net_ifinfo *info)
{
    info->name[NET_IFNAMSIZ - 1] = 0;
    net_lock();
    struct netdev *nd = info->index >= 0 ? netdev_get(info->index) : netdev_by_name(info->name);
    if (!nd)
    {
        net_unlock();
        return -ENODEV;
    }
    memcpy(info->name, nd->name, sizeof(info->name));
    memcpy(info->mac, nd->mac, sizeof(info->mac));
    info->mtu = nd->mtu;
    info->speed = nd->speed;
    info->flags = (nd->link_up ? NET_IF_LINK : 0u) | (nd->full_duplex ? NET_IF_FULL_DUPLEX : 0u);
    info->addr = info->netmask = info->gw = 0;
    if (nd->stack)
    {
        int up, dhcp, bound;
        lw_if_addr(nd->stack, &info->addr, &info->netmask, &info->gw, &up, &dhcp, &bound);
        info->flags |= (up ? NET_IF_UP : 0u) | (dhcp ? NET_IF_DHCP : 0u) | (bound ? NET_IF_BOUND : 0u);
    }
    info->rx_packets = nd->rx_packets;
    info->tx_packets = nd->tx_packets;
    info->rx_bytes = nd->rx_bytes;
    info->tx_bytes = nd->tx_bytes;
    info->rx_errors = nd->rx_errors;
    info->tx_errors = nd->tx_errors;
    info->rx_dropped = nd->rx_dropped;
    info->tx_dropped = nd->tx_dropped;
    net_unlock();
    return 0;
}

static int if_conf(struct net_ifconf *c)
{
    c->name[NET_IFNAMSIZ - 1] = 0;
    net_lock();
    struct netdev *nd = netdev_by_name(c->name);
    int r = -ENODEV;
    if (nd && nd->stack)
    {
        switch (c->what)
        {
        case NET_CONF_UP:
            r = lw_if_up(nd->stack, 1);
            break;
        case NET_CONF_DOWN:
            r = lw_if_up(nd->stack, 0);
            break;
        case NET_CONF_STATIC:
            r = lw_if_static(nd->stack, c->addr, c->netmask, c->gw);
            break;
        case NET_CONF_DHCP:
            r = lw_if_dhcp(nd->stack);
            break;
        default:
            r = -EINVAL;
        }
    }
    net_unlock();
    return r;
}

static int st_ioctl(void *ctx, int s, unsigned cmd, void *arg)
{
    (void)ctx;
    switch (cmd)
    {
    case NET_IOC_IFCOUNT:
        *(uint32_t *)arg = (uint32_t)netdev_count();
        return 0;
    case NET_IOC_IFINFO:
        return if_info(arg);
    case NET_IOC_IFCONF:
        return if_conf(arg);
    case NET_IOC_DNS_GET:
        lw_dns_get(((struct net_dns *)arg)->server);
        return 0;
    case NET_IOC_DNS_SET:
        lw_dns_set(((struct net_dns *)arg)->server);
        return 0;
    case NET_IOC_RESOLVE:
    {
        struct net_resolve *r = arg;
        return lw_resolve(r->name, &r->addr);
    }
    case NET_IOC_FIONREAD:
        return lw_fionread(s, arg);
    }
    return -ENOTTY;
}

static const struct net_stack_ops s_ops = {
    .attach = st_attach,
    .detach = st_detach,
    .rx = st_rx,
    .link = st_link,
    .socket = st_socket,
    .close = st_close,
    .bind = st_bind,
    .connect = st_connect,
    .listen = st_listen,
    .accept = st_accept,
    .send = st_send,
    .recv = st_recv,
    .shutdown = st_shutdown,
    .setsockopt = st_setsockopt,
    .getsockopt = st_getsockopt,
    .getname = st_getname,
    .poll = st_poll,
    .ioctl = st_ioctl,
};

/* ---- the device tree node (compatible = "crtos,lwip") --------------------------------------------- */

static int lwip_probe(struct device *dev)
{
    (void)dev;
    if (!s_started)
    {
        int r = lw_start();
        if (r)
            return r;
        s_started = true;
    }
    return net_stack_register(&s_ops, NULL);
}

static void lwip_remove(struct device *dev)
{
    (void)dev;
    net_stack_unregister();
}

static const struct of_device_id lwip_ids[] = {
    {"crtos,lwip", NULL},
    {NULL, NULL},
};

static struct driver lwip_driver = {
    .name = "net-lwip",
    .of_match_table = lwip_ids,
    .probe = lwip_probe,
    .remove = lwip_remove,
};

static int init(void)
{
    module_pin();
    return driver_register(&lwip_driver);
}

static void fini(void)
{
    driver_unregister(&lwip_driver);
}

MODULE("net-lwip", "TCP/IP stack (lwIP)", init, fini);
