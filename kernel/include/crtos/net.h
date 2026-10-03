/*
 * crtos/net.h - the network subsystem inside the kernel.
 *
 * Two parties meet here:
 *   - drivers of network interfaces register a struct netdev: they send frames given to
 *     ->xmit() and hand received frames to netdev_rx(), and report the link;
 *   - a protocol stack module (net-lwip.ko) registers struct net_stack_ops: it gets the
 *     interfaces (attach / detach), the received frames, and implements the sockets.
 *
 * Sockets are files (see subsys/net.cpp for the system calls). The stack never blocks in
 * its socket operations - it answers -EAGAIN - and calls net_sock_event() whenever a
 * socket may have become readable, writable or failed; the kernel does the waiting
 * (interruptible, with the socket's time-outs) and wakes poll().
 */
#ifndef CRTOS_NET_H
#define CRTOS_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <crtos/list.h>
#include <crtos/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- interfaces (drivers) --------------------------------------------------------------- */

struct netdev;

/* What the hardware does for the stack (struct netdev.features) */
#define NETDEV_F_TX_CSUM 0x1u   /* fills in the IPv4 header and TCP/UDP/ICMP checksums of the
                                 * frames it sends (the stack leaves them 0) */
#define NETDEV_F_RX_CSUM 0x2u   /* drops received IPv4 frames whose header or TCP/UDP/ICMP
                                 * checksum is wrong (the stack need not check them) */

struct netdev_ops {
    int (*open)(struct netdev *nd);                             /* start receiving */
    void (*stop)(struct netdev *nd);
    /* Send one frame (Ethernet header included, no FCS); may be called by several threads.
     * 0, or -EAGAIN when the transmit ring is full (the frame is dropped). */
    int (*xmit)(struct netdev *nd, const void *frame, size_t len);
};

struct netdev {
    /* filled in by the driver */
    const struct netdev_ops *ops;
    void *priv;
    uint8_t mac[6];
    uint16_t mtu;
    uint32_t features;                                          /* NETDEV_F_* */
    /* kept by the kernel */
    char name[NET_IFNAMSIZ];
    bool link_up, full_duplex;
    uint32_t speed;                                            /* Mbit/s */
    uint32_t rx_packets, tx_packets, rx_bytes, tx_bytes;
    uint32_t rx_errors, tx_errors, rx_dropped, tx_dropped;
    void *stack;                                                /* the stack's per-interface data */
    struct list_head node;
};

int netdev_register(struct netdev *nd);                         /* names it ethN */
void netdev_unregister(struct netdev *nd);
/* A received frame (the driver's buffer, copied by the stack before this returns); from a
 * thread, not an interrupt handler */
void netdev_rx(struct netdev *nd, const void *frame, size_t len);
void netdev_set_link(struct netdev *nd, bool up, uint32_t speed, bool full_duplex);
int netdev_count(void);
struct netdev *netdev_get(int index);                           /* call under net_lock() */
struct netdev *netdev_by_name(const char *name);                /* call under net_lock() */
void net_lock(void);                                            /* the interface list */
void net_unlock(void);

/* ---- the protocol stack ------------------------------------------------------------------- */

struct net_sock;

/* All return >= 0 or -errno; none of them blocks (-EAGAIN / -EINPROGRESS instead). Socket
 * addresses are struct crtos_sockaddr_in in kernel memory; data buffers may be user
 * memory of the calling process (checked by the kernel). */
struct net_stack_ops {
    void (*attach)(void *ctx, struct netdev *nd);
    void (*detach)(void *ctx, struct netdev *nd);
    void (*rx)(void *ctx, struct netdev *nd, const void *frame, size_t len);
    void (*link)(void *ctx, struct netdev *nd);                 /* link state changed */

    int (*socket)(void *ctx, int domain, int type, int protocol, struct net_sock *ks);   /* -> s */
    int (*close)(void *ctx, int s);
    int (*bind)(void *ctx, int s, const struct crtos_sockaddr_in *a);
    int (*connect)(void *ctx, int s, const struct crtos_sockaddr_in *a);
    int (*listen)(void *ctx, int s, int backlog);
    int (*accept)(void *ctx, int s, struct crtos_sockaddr_in *peer, struct net_sock *nks);  /* -> new s */
    int (*send)(void *ctx, int s, const void *buf, size_t len, int flags, const struct crtos_sockaddr_in *to);
    int (*recv)(void *ctx, int s, void *buf, size_t len, int flags, struct crtos_sockaddr_in *from);
    int (*shutdown)(void *ctx, int s, int how);
    int (*setsockopt)(void *ctx, int s, int level, int name, const void *val, uint32_t len);
    int (*getsockopt)(void *ctx, int s, int level, int name, void *val, uint32_t *len);
    int (*getname)(void *ctx, int s, int peer, struct crtos_sockaddr_in *a);
    int (*poll)(void *ctx, int s);                              /* POLLIN | POLLOUT | POLLERR | POLLHUP */
    int (*ioctl)(void *ctx, int s, unsigned cmd, void *arg);    /* NET_IOC_* (kernel copies of the args) */
};

int net_stack_register(const struct net_stack_ops *ops, void *ctx);   /* one stack at a time */
void net_stack_unregister(void);

/* The stack's socket @ks may have changed state: wake whoever waits for it */
void net_sock_event(struct net_sock *ks);

#ifdef __cplusplus
}
#endif

#endif
