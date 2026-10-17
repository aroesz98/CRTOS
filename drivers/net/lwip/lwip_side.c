/*
 * lwip_side.c - the lwIP half of net-lwip.ko: network interfaces on lwIP's netif (Ethernet,
 * ARP), their addresses (static, DHCP), DNS, and the sockets (lwIP's sockets.c, always
 * non-blocking). See lw.h.
 */
#include <string.h>
#include "lwip/opt.h"
#include "lwip/api.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "netif/ethernet.h"
#include "lw.h"

struct lw_if
{
    struct netif netif;
    void *nd;
    uint32_t reported; /* the address last reported */
    uint8_t mac[6];
    int mtu;
    unsigned csum;    /* LW_*_CSUM: checksums the hardware does */
    uint8_t tx[1536]; /* a chained frame made contiguous (under the core lock) */
};

static void *s_owner[MEMP_NUM_NETCONN]; /* the kernel's socket of each lwIP socket */

static int neg_errno(void)
{
    int e = errno;
    return e ? -e : -EIO;
}

/* ---- the stack thread ---------------------------------------------------------------------------- */

static void init_done(void *arg)
{
    sys_sem_signal((sys_sem_t *)arg);
}

int lw_start(void)
{
    sys_sem_t done;
    if (sys_sem_new(&done, 0) != ERR_OK)
        return -ENOMEM;
    tcpip_init(init_done, &done);
    sys_sem_wait(&done);
    sys_sem_free(&done);
    return 0;
}

/* ---- interfaces ------------------------------------------------------------------------------------ */

static err_t lw_linkoutput(struct netif *netif, struct pbuf *p)
{
    struct lw_if *li = netif->state;
    const void *data = p->payload;
    if (p->next)
    {
        if (p->tot_len > sizeof(li->tx))
            return ERR_BUF;
        pbuf_copy_partial(p, li->tx, p->tot_len, 0);
        data = li->tx;
    }
    return crtos_lwip_xmit(li->nd, data, p->tot_len) ? ERR_IF : ERR_OK;
}

static void lw_status(struct netif *netif)
{
    struct lw_if *li = netif->state;
    uint32_t addr = ip4_addr_get_u32(netif_ip4_addr(netif));
    if (addr == li->reported)
        return;
    li->reported = addr;
    crtos_lwip_status(li->nd, addr, ip4_addr_get_u32(netif_ip4_netmask(netif)), ip4_addr_get_u32(netif_ip4_gw(netif)),
                      dhcp_supplied_address(netif));
}

static err_t lw_netif_init(struct netif *netif)
{
    struct lw_if *li = netif->state;
    netif->name[0] = 'e';
    netif->name[1] = 'n';
    netif->hwaddr_len = ETH_HWADDR_LEN;
    memcpy(netif->hwaddr, li->mac, ETH_HWADDR_LEN);
    netif->mtu = (u16_t)li->mtu;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP;
    /* the checksums the hardware does are not computed here (they were a twentieth of the
     * processor at full speed). ICMP's stay: the hardware neither fills in nor checks the
     * protocol's checksum of a fragment, and the answer to a large ping is fragments (a UDP
     * datagram in fragments goes with 0: no checksum, which IPv4 allows) */
    u16_t off = 0;
    if (li->csum & LW_TX_CSUM)
        off |= NETIF_CHECKSUM_GEN_IP | NETIF_CHECKSUM_GEN_UDP | NETIF_CHECKSUM_GEN_TCP;
    if (li->csum & LW_RX_CSUM)
        off |= NETIF_CHECKSUM_CHECK_IP | NETIF_CHECKSUM_CHECK_UDP | NETIF_CHECKSUM_CHECK_TCP;
    NETIF_SET_CHECKSUM_CTRL(netif, NETIF_CHECKSUM_ENABLE_ALL & ~off);
    netif->output = etharp_output;
    netif->linkoutput = lw_linkoutput;
    netif_set_hostname(netif, "crtos");
    netif_set_status_callback(netif, lw_status);
    return ERR_OK;
}

void *lw_if_add(void *nd, const uint8_t mac[6], int mtu, unsigned csum)
{
    struct lw_if *li = mem_calloc(1, sizeof(*li));
    if (!li)
        return NULL;
    li->nd = nd;
    memcpy(li->mac, mac, 6);
    li->mtu = mtu;
    li->csum = csum;
    LOCK_TCPIP_CORE();
    if (!netif_add(&li->netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, li, lw_netif_init, tcpip_input))
    {
        UNLOCK_TCPIP_CORE();
        mem_free(li);
        return NULL;
    }
    if (!netif_default)
        netif_set_default(&li->netif);
    UNLOCK_TCPIP_CORE();
    return li;
}

void lw_if_remove(void *p)
{
    struct lw_if *li = p;
    if (!li)
        return;
    LOCK_TCPIP_CORE();
    dhcp_stop(&li->netif);
    netif_remove(&li->netif);
    UNLOCK_TCPIP_CORE();
    mem_free(li);
}

int lw_if_input(void *p, const void *frame, size_t len)
{
    struct lw_if *li = p;
    if (!li || len > 0xFFFF)
        return -EINVAL;
    struct pbuf *pb = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
    if (!pb)
        return -ENOMEM;
    pbuf_take(pb, frame, (u16_t)len);
    if (li->netif.input(pb, &li->netif) != ERR_OK)
    { /* tcpip_input: in this thread, under the core lock */
        pbuf_free(pb);
        return -ENOMEM;
    }
    return 0;
}

void lw_if_link(void *p, int up)
{
    struct lw_if *li = p;
    if (!li)
        return;
    LOCK_TCPIP_CORE();
    if (up)
        netif_set_link_up(&li->netif);
    else
        netif_set_link_down(&li->netif);
    UNLOCK_TCPIP_CORE();
}

int lw_if_up(void *p, int up)
{
    struct lw_if *li = p;
    LOCK_TCPIP_CORE();
    if (up)
    {
        netif_set_up(&li->netif);
    }
    else
    {
        dhcp_release_and_stop(&li->netif);
        netif_set_down(&li->netif);
    }
    UNLOCK_TCPIP_CORE();
    return 0;
}

int lw_if_static(void *p, uint32_t addr, uint32_t mask, uint32_t gw)
{
    struct lw_if *li = p;
    ip4_addr_t a, m, g;
    ip4_addr_set_u32(&a, addr);
    ip4_addr_set_u32(&m, mask);
    ip4_addr_set_u32(&g, gw);
    LOCK_TCPIP_CORE();
    dhcp_release_and_stop(&li->netif);
    netif_set_addr(&li->netif, &a, &m, &g);
    netif_set_up(&li->netif);
    UNLOCK_TCPIP_CORE();
    return 0;
}

int lw_if_dhcp(void *p)
{
    struct lw_if *li = p;
    LOCK_TCPIP_CORE();
    netif_set_up(&li->netif);
    err_t e = dhcp_start(&li->netif);
    UNLOCK_TCPIP_CORE();
    return e == ERR_OK ? 0 : -ENOMEM;
}

void lw_if_addr(void *p, uint32_t *addr, uint32_t *mask, uint32_t *gw, int *up, int *dhcp, int *bound)
{
    struct lw_if *li = p;
    LOCK_TCPIP_CORE();
    *addr = ip4_addr_get_u32(netif_ip4_addr(&li->netif));
    *mask = ip4_addr_get_u32(netif_ip4_netmask(&li->netif));
    *gw = ip4_addr_get_u32(netif_ip4_gw(&li->netif));
    *up = netif_is_up(&li->netif);
    *dhcp = netif_dhcp_data(&li->netif) != NULL;
    *bound = dhcp_supplied_address(&li->netif);
    UNLOCK_TCPIP_CORE();
}

void lw_dns_get(uint32_t server[2])
{
    LOCK_TCPIP_CORE();
    for (int i = 0; i < 2; i++)
        server[i] = ip4_addr_get_u32(ip_2_ip4(dns_getserver((u8_t)i)));
    UNLOCK_TCPIP_CORE();
}

void lw_dns_set(const uint32_t server[2])
{
    LOCK_TCPIP_CORE();
    for (int i = 0; i < 2; i++)
    {
        ip_addr_t a;
        ip_addr_set_ip4_u32(&a, server[i]);
        dns_setserver((u8_t)i, &a);
    }
    UNLOCK_TCPIP_CORE();
}

int lw_resolve(const char *name, uint32_t *addr)
{
    ip_addr_t a;
    err_t e = netconn_gethostbyname(name, &a);
    if (e != ERR_OK)
        return e == ERR_ARG || e == ERR_VAL ? -EINVAL : -EHOSTUNREACH;
    *addr = ip4_addr_get_u32(ip_2_ip4(&a));
    return 0;
}

/* ---- sockets ------------------------------------------------------------------------------------------ */

void crtos_lwip_socket_event(int s)
{
    if (s >= 0 && s < MEMP_NUM_NETCONN && s_owner[s])
        crtos_lwip_event(s_owner[s]);
}

static void own(int s, void *owner)
{
    if (s < 0 || s >= MEMP_NUM_NETCONN)
        return;
    LOCK_TCPIP_CORE(); /* the event hook runs under it */
    s_owner[s] = owner;
    UNLOCK_TCPIP_CORE();
}

static void nonblocking(int s)
{
    int on = 1;
    lwip_ioctl(s, FIONBIO, &on);
}

int lw_socket(int domain, int type, int protocol, void *owner)
{
    int s = lwip_socket(domain, type, protocol);
    if (s < 0)
        return neg_errno();
    nonblocking(s);
    own(s, owner);
    return s;
}

int lw_close(int s)
{
    own(s, NULL);
    return lwip_close(s) < 0 ? neg_errno() : 0;
}

static void sa_set(struct sockaddr_in *sa, uint32_t addr, uint16_t port)
{
    memset(sa, 0, sizeof(*sa));
    sa->sin_len = sizeof(*sa);
    sa->sin_family = AF_INET;
    sa->sin_port = port;
    sa->sin_addr.s_addr = addr;
}

int lw_bind(int s, uint32_t addr, uint16_t port)
{
    struct sockaddr_in sa;
    sa_set(&sa, addr, port);
    return lwip_bind(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 ? neg_errno() : 0;
}

int lw_connect(int s, uint32_t addr, uint16_t port)
{
    struct sockaddr_in sa;
    sa_set(&sa, addr, port);
    return lwip_connect(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 ? neg_errno() : 0;
}

int lw_disconnect(int s)
{
    struct sockaddr sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_len = sizeof(sa);
    sa.sa_family = AF_UNSPEC;
    return lwip_connect(s, &sa, sizeof(sa)) < 0 ? neg_errno() : 0;
}

int lw_listen(int s, int backlog)
{
    return lwip_listen(s, backlog) < 0 ? neg_errno() : 0;
}

int lw_accept(int s, uint32_t *addr, uint16_t *port, void *owner)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    int n = lwip_accept(s, (struct sockaddr *)&sa, &len);
    if (n < 0)
        return neg_errno();
    nonblocking(n);
    own(n, owner);
    *addr = sa.sin_addr.s_addr;
    *port = sa.sin_port;
    return n;
}

int lw_send(int s, const void *buf, size_t len, int flags, int to, uint32_t addr, uint16_t port)
{
    int n;
    if (to)
    {
        struct sockaddr_in sa;
        sa_set(&sa, addr, port);
        n = lwip_sendto(s, buf, len, flags, (struct sockaddr *)&sa, sizeof(sa));
    }
    else
    {
        n = lwip_send(s, buf, len, flags);
    }
    return n < 0 ? neg_errno() : n;
}

int lw_recv(int s, void *buf, size_t len, int flags, uint32_t *addr, uint16_t *port)
{
    struct sockaddr_in sa;
    socklen_t salen = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    int n = lwip_recvfrom(s, buf, len, flags, (struct sockaddr *)&sa, &salen);
    if (n < 0)
        return neg_errno();
    if (addr)
        *addr = sa.sin_addr.s_addr;
    if (port)
        *port = sa.sin_port;
    return n;
}

int lw_shutdown(int s, int how)
{
    return lwip_shutdown(s, how) < 0 ? neg_errno() : 0;
}

int lw_setsockopt(int s, int level, int name, const void *val, uint32_t len)
{
    return lwip_setsockopt(s, level, name, val, (socklen_t)len) < 0 ? neg_errno() : 0;
}

int lw_getsockopt(int s, int level, int name, void *val, uint32_t *len)
{
    socklen_t l = (socklen_t)*len;
    if (lwip_getsockopt(s, level, name, val, &l) < 0)
        return neg_errno();
    *len = (uint32_t)l;
    return 0;
}

int lw_getname(int s, int peer, uint32_t *addr, uint16_t *port)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    memset(&sa, 0, sizeof(sa));
    int r = peer ? lwip_getpeername(s, (struct sockaddr *)&sa, &len) : lwip_getsockname(s, (struct sockaddr *)&sa, &len);
    if (r < 0)
        return neg_errno();
    *addr = sa.sin_addr.s_addr;
    *port = sa.sin_port;
    return 0;
}

int lw_poll(int s)
{
    struct pollfd pf;
    pf.fd = s;
    pf.events = POLLIN | POLLOUT;
    pf.revents = 0;
    if (lwip_poll(&pf, 1, 0) < 0)
        return LW_ERR;
    int m = 0;
    if (pf.revents & POLLIN)
        m |= LW_IN;
    if (pf.revents & POLLOUT)
        m |= LW_OUT;
    if (pf.revents & (POLLERR | POLLNVAL))
        m |= LW_ERR;
    if (pf.revents & POLLHUP)
        m |= LW_HUP;
    return m;
}

int lw_fionread(int s, uint32_t *n)
{
    int v = 0;
    if (lwip_ioctl(s, FIONREAD, &v) < 0)
        return neg_errno();
    *n = (uint32_t)v;
    return 0;
}
