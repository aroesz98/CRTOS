/*
 * ifconfig - show and configure network interfaces.
 *
 *   ifconfig                              all interfaces
 *   ifconfig eth0                         one of them
 *   ifconfig eth0 up | down | dhcp
 *   ifconfig eth0 192.168.1.50/24 [gw 192.168.1.1]
 *   ifconfig dns [server [server]]        show / set the DNS servers
 *
 * Changes need the sys capability.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <crtos.h>

static int s_fd;

static const char *ip(uint32_t a)
{
    static char buf[4][INET_ADDRSTRLEN];
    static int n;
    char *b = buf[n++ & 3];
    inet_ntop(AF_INET, &a, b, INET_ADDRSTRLEN);
    return b;
}

static void show(const struct net_ifinfo *in)
{
    uint32_t f = in->flags;
    printf("%s: <%s%s%s%s> mtu %u\n", in->name, f & NET_IF_UP ? "UP" : "DOWN", f & NET_IF_LINK ? ",RUNNING" : "",
           f & NET_IF_DHCP ? ",DHCP" : "", f & NET_IF_DHCP && !(f & NET_IF_BOUND) ? " (no lease)" : "", in->mtu);
    if (in->addr)
        printf("    inet %s/%d  gateway %s\n", ip(in->addr), __builtin_popcount(in->netmask), ip(in->gw));
    printf("    ether %02x:%02x:%02x:%02x:%02x:%02x", in->mac[0], in->mac[1], in->mac[2], in->mac[3], in->mac[4],
           in->mac[5]);
    if (f & NET_IF_LINK)
        printf("  %lu Mbit/s %s duplex", (unsigned long)in->speed, f & NET_IF_FULL_DUPLEX ? "full" : "half");
    printf("\n    RX %lu packets, %lu bytes, %lu dropped, %lu errors\n", (unsigned long)in->rx_packets,
           (unsigned long)in->rx_bytes, (unsigned long)in->rx_dropped, (unsigned long)in->rx_errors);
    printf("    TX %lu packets, %lu bytes, %lu dropped, %lu errors\n", (unsigned long)in->tx_packets,
           (unsigned long)in->tx_bytes, (unsigned long)in->tx_dropped, (unsigned long)in->tx_errors);
}

static int info(const char *name, struct net_ifinfo *in)
{
    memset(in, 0, sizeof(*in));
    in->index = -1;
    strncpy(in->name, name, sizeof(in->name) - 1);
    if (ioctl(s_fd, NET_IOC_IFINFO, in) < 0) {
        fprintf(stderr, "ifconfig: %s: %s\n", name, strerror(errno));
        return -1;
    }
    return 0;
}

static int conf(const char *name, uint32_t what, uint32_t addr, uint32_t mask, uint32_t gw)
{
    struct net_ifconf c;
    memset(&c, 0, sizeof(c));
    strncpy(c.name, name, sizeof(c.name) - 1);
    c.what = what;
    c.addr = addr;
    c.netmask = mask;
    c.gw = gw;
    if (ioctl(s_fd, NET_IOC_IFCONF, &c) < 0) {
        fprintf(stderr, "ifconfig: %s: %s\n", name, strerror(errno));
        return -1;
    }
    return 0;
}

static int dns(int argc, char **argv)
{
    struct net_dns d;
    memset(&d, 0, sizeof(d));
    if (argc > 2) {
        for (int i = 0; i < 2 && i + 2 < argc; i++) {
            if (!inet_pton(AF_INET, argv[i + 2], &d.server[i])) {
                fprintf(stderr, "ifconfig: bad address %s\n", argv[i + 2]);
                return 1;
            }
        }
        if (ioctl(s_fd, NET_IOC_DNS_SET, &d) < 0) {
            fprintf(stderr, "ifconfig: dns: %s\n", strerror(errno));
            return 1;
        }
    }
    if (ioctl(s_fd, NET_IOC_DNS_GET, &d) < 0) {
        fprintf(stderr, "ifconfig: dns: %s\n", strerror(errno));
        return 1;
    }
    printf("dns %s %s\n", ip(d.server[0]), ip(d.server[1]));
    return 0;
}

/* "a.b.c.d/bits" (bits 24 if left out) */
static int parse_cidr(const char *s, uint32_t *addr, uint32_t *mask)
{
    char buf[32];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    int bits = 24;
    char *slash = strchr(buf, '/');
    if (slash) {
        *slash = 0;
        bits = atoi(slash + 1);
        if (bits < 1 || bits > 32)
            return -1;
    }
    if (!inet_pton(AF_INET, buf, addr))
        return -1;
    *mask = htonl(bits == 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> bits));
    return 0;
}

int main(int argc, char **argv)
{
    s_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_fd < 0) {
        fprintf(stderr, "ifconfig: no TCP/IP stack (%s)\n", strerror(errno));
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "dns"))
        return dns(argc, argv);

    struct net_ifinfo in;
    if (argc == 1) {
        uint32_t n = 0;
        ioctl(s_fd, NET_IOC_IFCOUNT, &n);
        for (uint32_t i = 0; i < n; i++) {
            memset(&in, 0, sizeof(in));
            in.index = (int32_t)i;
            if (!ioctl(s_fd, NET_IOC_IFINFO, &in))
                show(&in);
        }
        if (!n)
            printf("no network interfaces\n");
        return 0;
    }
    const char *name = argv[1];
    if (argc == 2) {
        if (info(name, &in))
            return 1;
        show(&in);
        return 0;
    }
    const char *what = argv[2];
    int r;
    if (!strcmp(what, "up")) {
        r = conf(name, NET_CONF_UP, 0, 0, 0);
    } else if (!strcmp(what, "down")) {
        r = conf(name, NET_CONF_DOWN, 0, 0, 0);
    } else if (!strcmp(what, "dhcp")) {
        r = conf(name, NET_CONF_DHCP, 0, 0, 0);
    } else {
        uint32_t addr, mask, gw = 0;
        if (parse_cidr(what, &addr, &mask) ||
            (argc > 3 && (argc != 5 || strcmp(argv[3], "gw") || !inet_pton(AF_INET, argv[4], &gw)))) {
            fprintf(stderr, "usage: ifconfig [name [up|down|dhcp|addr[/bits] [gw addr]]]\n"
                            "       ifconfig dns [server [server]]\n");
            return 2;
        }
        r = conf(name, NET_CONF_STATIC, addr, mask, gw);
    }
    if (r)
        return 1;
    if (!info(name, &in))
        show(&in);
    return 0;
}
