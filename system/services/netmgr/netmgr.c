/*
 * netmgr - the network manager (layer 2): configures the interfaces from
 * /sd/crtos/etc/network.cfg, reports their state and sets the clock from an NTP server.
 *
 *   iface <name> dhcp
 *   iface <name> static <addr>/<bits> [gw <addr>]
 *   dns <server> [<server>]     fixed DNS servers (else the ones from DHCP)
 *   ntp <server>                set the clock at start and every hour (caps=sys)
 *
 * Without the file: eth0 with DHCP, time from pool.ntp.org. Interfaces may appear later
 * (driver modules load in the background); each is configured when it shows up.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <crtos.h>

#define CONFIG          "/sd/crtos/etc/network.cfg"
#define MAX_IF          4
#define NTP_EVERY_S     3600
#define NTP_RETRY_S     30

struct iface {
    char name[NET_IFNAMSIZ];
    bool dhcp;
    uint32_t addr, mask, gw;
    bool configured;
    uint32_t flags, cur_addr;       /* last reported */
};

static struct iface s_if[MAX_IF];
static int s_nif;
static uint32_t s_dns[2];
static char s_ntp[64] = "pool.ntp.org";
static int s_fd = -1;

static const char *ip(uint32_t a)
{
    static char buf[4][INET_ADDRSTRLEN];
    static int n;
    char *b = buf[n++ & 3];
    inet_ntop(AF_INET, &a, b, INET_ADDRSTRLEN);
    return b;
}

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

static void load_config(void)
{
    FILE *f = fopen(CONFIG, "r");
    if (!f) {
        strcpy(s_if[0].name, "eth0");
        s_if[0].dhcp = true;
        s_nif = 1;
        return;
    }
    char line[160];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *argv[8];
        int argc = 0;
        for (char *t = strtok(line, " \t\r\n"); t && argc < 8; t = strtok(NULL, " \t\r\n"))
            argv[argc++] = t;
        if (!argc || argv[0][0] == '#')
            continue;
        bool ok = false;
        if (!strcmp(argv[0], "iface") && argc >= 3 && s_nif < MAX_IF) {
            struct iface *i = &s_if[s_nif];
            memset(i, 0, sizeof(*i));
            strncpy(i->name, argv[1], sizeof(i->name) - 1);
            if (!strcmp(argv[2], "dhcp") && argc == 3) {
                i->dhcp = ok = true;
            } else if (!strcmp(argv[2], "static") && argc >= 4 && !parse_cidr(argv[3], &i->addr, &i->mask)) {
                ok = argc == 4 || (argc == 6 && !strcmp(argv[4], "gw") && inet_pton(AF_INET, argv[5], &i->gw));
            }
            if (ok)
                s_nif++;
        } else if (!strcmp(argv[0], "dns") && argc >= 2 && argc <= 3) {
            ok = inet_pton(AF_INET, argv[1], &s_dns[0]) && (argc == 2 || inet_pton(AF_INET, argv[2], &s_dns[1]));
        } else if (!strcmp(argv[0], "ntp") && argc == 2) {
            strncpy(s_ntp, strcmp(argv[1], "off") ? argv[1] : "", sizeof(s_ntp) - 1);
            ok = true;
        }
        if (!ok)
            printf("netmgr: %s:%d: not understood\n", CONFIG, lineno);
    }
    fclose(f);
}

static void set_dns(void)
{
    if (!s_dns[0])
        return;
    struct net_dns d;
    memcpy(d.server, s_dns, sizeof(d.server));
    if (ioctl(s_fd, NET_IOC_DNS_SET, &d) < 0)
        printf("netmgr: cannot set the DNS servers: %s\n", strerror(errno));
}

static void configure(struct iface *i)
{
    struct net_ifconf c;
    memset(&c, 0, sizeof(c));
    memcpy(c.name, i->name, sizeof(c.name));
    c.what = i->dhcp ? NET_CONF_DHCP : NET_CONF_STATIC;
    c.addr = i->addr;
    c.netmask = i->mask;
    c.gw = i->gw;
    if (ioctl(s_fd, NET_IOC_IFCONF, &c) < 0) {
        printf("netmgr: %s: %s\n", i->name, strerror(errno));
        return;
    }
    i->configured = true;
    if (i->dhcp)
        printf("netmgr: %s: asking DHCP for an address\n", i->name);
    set_dns();
}

/* Configure new interfaces, report changes; true if one has an address */
static bool check(void)
{
    bool any = false;
    for (int n = 0; n < s_nif; n++) {
        struct iface *i = &s_if[n];
        struct net_ifinfo in;
        memset(&in, 0, sizeof(in));
        in.index = -1;
        memcpy(in.name, i->name, sizeof(in.name));
        if (ioctl(s_fd, NET_IOC_IFINFO, &in) < 0)
            continue; /* not there (yet) */
        if (!i->configured) {
            configure(i);
            continue;
        }
        if ((in.flags & NET_IF_LINK) != (i->flags & NET_IF_LINK) && !(in.flags & NET_IF_LINK))
            printf("netmgr: %s: no link\n", i->name);
        if (in.addr != i->cur_addr && in.addr) {
            struct net_dns d;
            memset(&d, 0, sizeof(d));
            if (s_dns[0])
                set_dns();
            ioctl(s_fd, NET_IOC_DNS_GET, &d);
            printf("netmgr: %s: %s/%d gateway %s dns %s%s\n", i->name, ip(in.addr), __builtin_popcount(in.netmask),
                   ip(in.gw), ip(d.server[0]), in.flags & NET_IF_DHCP ? " (DHCP)" : "");
        }
        i->flags = in.flags;
        i->cur_addr = in.addr;
        if (in.addr && (in.flags & NET_IF_LINK))
            any = true;
    }
    return any;
}

/* ---- the clock from NTP (RFC 5905, client mode) ------------------------------------------------ */

#define NTP_UNIX_OFFSET 2208988800u         /* 1900 -> 1970 */

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static int ntp_sync(void)
{
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    int e = getaddrinfo(s_ntp, "123", &hints, &ai);
    if (e) {
        printf("netmgr: ntp: %s: %s\n", s_ntp, gai_strerror(e));
        return -1;
    }
    struct sockaddr_in server = *(struct sockaddr_in *)ai->ai_addr;
    freeaddrinfo(ai);
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0)
        return -1;
    struct timeval tv = { 2, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int result = -1;
    for (int attempt = 0; attempt < 3 && result; attempt++) {
        uint8_t pkt[48];
        memset(pkt, 0, sizeof(pkt));
        pkt[0] = 0x23; /* version 4, client */
        uint32_t cookie = (uint32_t)crtos_time_us() ^ 0x5A5A1234u;
        memcpy(pkt + 40, &cookie, 4); /* transmit time: any value, the server echoes it */
        uint64_t t1 = crtos_time_us();
        if (sendto(s, pkt, sizeof(pkt), 0, (struct sockaddr *)&server, sizeof(server)) < 0)
            continue;
        for (;;) {
            struct sockaddr_in from;
            socklen_t flen = sizeof(from);
            int n = (int)recvfrom(s, pkt, sizeof(pkt), 0, (struct sockaddr *)&from, &flen);
            if (n < 0)
                break;
            uint32_t echoed;
            memcpy(&echoed, pkt + 24, 4);
            if (n < 48 || (pkt[0] & 7) != 4 || !pkt[1] || echoed != cookie || from.sin_addr.s_addr != server.sin_addr.s_addr)
                continue;
            uint64_t t4 = crtos_time_us();
            /* the server's transmit time plus half the round trip */
            uint64_t us = (uint64_t)(get32(pkt + 40) - NTP_UNIX_OFFSET) * 1000000u +
                          (((uint64_t)get32(pkt + 44) * 1000000u) >> 32) + (t4 - t1) / 2;
            struct timeval now;
            gettimeofday(&now, NULL);
            int64_t before = (int64_t)now.tv_sec * 1000000 + now.tv_usec;
            struct timeval set = { (time_t)(us / 1000000u), (suseconds_t)(us % 1000000u) };
            if (settimeofday(&set, NULL)) {
                printf("netmgr: ntp: cannot set the clock: %s\n", strerror(errno));
                result = -2;
                break;
            }
            time_t sec = set.tv_sec;
            struct tm tm;
            gmtime_r(&sec, &tm);
            int64_t diff = (int64_t)us - before;
            uint64_t off_ms = (uint64_t)(diff < 0 ? -diff : diff) / 1000u;
            char off[32];
            if (off_ms < 100000u)
                snprintf(off, sizeof(off), "%lu ms", (unsigned long)off_ms);
            else
                snprintf(off, sizeof(off), "%lu s", (unsigned long)(off_ms / 1000u));
            printf("netmgr: clock set from %s: %04d-%02d-%02d %02d:%02d:%02d UTC (was %s%s off, rtt %lu ms)\n", s_ntp,
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, diff < 0 ? "-" : "+",
                   off, (unsigned long)((t4 - t1) / 1000u));
            result = 0;
            break;
        }
    }
    close(s);
    if (result == -1)
        printf("netmgr: ntp: no answer from %s\n", s_ntp);
    return result;
}

int main(void)
{
    load_config();
    bool said = false;
    while ((s_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { /* net-lwip.ko loads in the background */
        if (!said)
            printf("netmgr: waiting for the TCP/IP stack\n");
        said = true;
        crtos_sleep_ms(500);
    }
    uint64_t next_ntp = 0;
    for (;;) {
        bool up = check();
        uint64_t now = crtos_time_us();
        if (up && s_ntp[0] && now >= next_ntp) {
            int r = ntp_sync();
            next_ntp = now + (uint64_t)(r == 0 ? NTP_EVERY_S : r == -2 ? 24 * 3600 : NTP_RETRY_S) * 1000000u;
        }
        crtos_sleep_ms(1000);
    }
}
