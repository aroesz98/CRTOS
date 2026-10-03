/*
 * lw.h - between the two halves of net-lwip.ko: lwip_side.c (sees only lwIP's headers)
 * and crtos_lwip.c (sees only the kernel's). Their socket definitions clash, so only plain
 * types cross here. Addresses and ports are in network byte order; functions return >= 0
 * or -errno and never block, except lw_resolve().
 */
#ifndef LW_H
#define LW_H

#include <stddef.h>
#include <stdint.h>

int lw_start(void); /* the tcpip thread */

/* interfaces */
#define LW_TX_CSUM 1 /* the interface fills in the checksums of what it sends */
#define LW_RX_CSUM 2 /* ... and drops what it receives with a wrong one */
void *lw_if_add(void *nd, const uint8_t mac[6], int mtu, unsigned csum); /* csum: LW_*_CSUM */
void lw_if_remove(void *li);
int lw_if_input(void *li, const void *frame, size_t len); /* -ENOMEM: dropped */
void lw_if_link(void *li, int up);
int lw_if_up(void *li, int up);
int lw_if_static(void *li, uint32_t addr, uint32_t mask, uint32_t gw);
int lw_if_dhcp(void *li);
void lw_if_addr(void *li, uint32_t *addr, uint32_t *mask, uint32_t *gw, int *up, int *dhcp, int *bound);
void lw_dns_get(uint32_t server[2]);
void lw_dns_set(const uint32_t server[2]);
int lw_resolve(const char *name, uint32_t *addr); /* blocks until the answer or time-out */

/* sockets; @owner is handed back through crtos_lwip_event() */
#define LW_IN 1
#define LW_OUT 2
#define LW_ERR 4
#define LW_HUP 8

int lw_socket(int domain, int type, int protocol, void *owner);
int lw_close(int s);
int lw_bind(int s, uint32_t addr, uint16_t port);
int lw_connect(int s, uint32_t addr, uint16_t port);
int lw_disconnect(int s); /* datagram sockets: forget the peer */
int lw_listen(int s, int backlog);
int lw_accept(int s, uint32_t *addr, uint16_t *port, void *owner);
int lw_send(int s, const void *buf, size_t len, int flags, int to, uint32_t addr, uint16_t port);
int lw_recv(int s, void *buf, size_t len, int flags, uint32_t *addr, uint16_t *port);
int lw_shutdown(int s, int how);
int lw_setsockopt(int s, int level, int name, const void *val, uint32_t len);
int lw_getsockopt(int s, int level, int name, void *val, uint32_t *len);
int lw_getname(int s, int peer, uint32_t *addr, uint16_t *port);
int lw_poll(int s); /* LW_* */
int lw_fionread(int s, uint32_t *n);

/* provided by crtos_lwip.c */
int crtos_lwip_xmit(void *nd, const void *frame, size_t len);
void crtos_lwip_event(void *owner);
void crtos_lwip_status(void *nd, uint32_t addr, uint32_t mask, uint32_t gw, int dhcp); /* address changed */

#endif
