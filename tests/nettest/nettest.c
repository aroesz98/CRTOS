/*
 * nettest - the board's side of "crtos netbench": network throughput tests over TCP and UDP.
 *
 *   nettest [-p port] [-n tests] [-t secs]   serve tests on the TCP port (5001); end after
 *                                            that many tests (0: never) or that long without
 *                                            a client (0: wait for ever)
 *
 * A client connects and sends a 12-byte request: "NTST", the test (a byte), its length in
 * seconds (a byte), 2 bytes unused, a UDP port (2 bytes, network order), 2 bytes unused.
 *   R  the client sends over this connection until it closes its side; we count and answer
 *      "rx BYTES MICROSECONDS"
 *   S  we send over this connection for the given seconds, then close our side
 *   U  we receive UDP datagrams on the UDP port of the same number as our TCP port, from
 *      "ready" until the client writes "end" on the connection; we answer
 *      "udp PACKETS BYTES MICROSECONDS"
 *   V  we send UDP datagrams of 1472 bytes as fast as we can from our UDP port to the client's
 *      address and the given UDP port for the given seconds; we answer
 *      "udp-sent PACKETS BYTES MICROSECONDS"
 * The time runs from the first byte received (from the start when sending) to the last one.
 * The data are only counted and dropped (sent from a constant buffer): the test is the network
 * and its stack, not the memory or the card.
 */
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <crtos.h>

#define DEFAULT_PORT 5001
#define BUF_SIZE     32768
#define DGRAM        1472           /* the largest UDP payload in one Ethernet frame */

static uint8_t s_buf[BUF_SIZE];

static uint64_t now_us(void)
{
    return crtos_time_us();
}

static int send_all(int fd, const void *data, size_t len)
{
    const char *p = (const char *)data;
    while (len) {
        int w = (int)send(fd, p, len, 0);
        if (w <= 0)
            return -1;
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

static int recv_all(int fd, void *data, size_t len)
{
    char *p = (char *)data;
    while (len) {
        int r = (int)recv(fd, p, len, 0);
        if (r <= 0)
            return -1;
        p += r;
        len -= (size_t)r;
    }
    return 0;
}

static void answer(int fd, const char *fmt, unsigned long a, unsigned long b, unsigned long c)
{
    char line[96];
    int n = snprintf(line, sizeof(line), fmt, a, b, c);
    send_all(fd, line, (size_t)n);
}

static double mbit(unsigned long bytes, unsigned long us)
{
    return us ? (double)bytes * 8.0 / (double)us : 0.0;
}

/* R: count what the client sends until it closes its side */
static void tcp_receive(int fd)
{
    unsigned long bytes = 0;
    uint64_t t0 = 0, t1 = 0;
    for (;;) {
        int r = (int)recv(fd, s_buf, sizeof(s_buf), 0);
        if (r <= 0)
            break;
        t1 = now_us();
        if (!bytes)
            t0 = t1;
        bytes += (unsigned)r;
    }
    printf("nettest: TCP received %lu bytes in %lu us: %.1f Mbit/s\n", bytes, (unsigned long)(t1 - t0),
           mbit(bytes, t1 - t0));
    answer(fd, "rx %lu %lu %lu\n", 0, bytes, (unsigned long)(t1 - t0));
}

/* S: send for @secs seconds */
static void tcp_send(int fd, int secs)
{
    unsigned long bytes = 0;
    uint64_t t0 = now_us(), end = t0 + (uint64_t)secs * 1000000u, t1 = t0;
    while (t1 < end) {
        int w = (int)send(fd, s_buf, sizeof(s_buf), 0);
        if (w <= 0)
            break;
        bytes += (unsigned)w;
        t1 = now_us();
    }
    shutdown(fd, SHUT_WR);
    printf("nettest: TCP sent %lu bytes in %lu us: %.1f Mbit/s\n", bytes, (unsigned long)(t1 - t0),
           mbit(bytes, t1 - t0));
    while (recv(fd, s_buf, sizeof(s_buf), 0) > 0) /* until the client has it all and closes */
        ;
}

static int udp_socket(int port)
{
    int u = socket(AF_INET, SOCK_DGRAM, 0);
    if (u < 0)
        return -1;
    int size = 65536;
    setsockopt(u, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (bind(u, (struct sockaddr *)&a, sizeof(a)) < 0) {
        close(u);
        return -1;
    }
    return u;
}

/* U: count UDP datagrams until the client says "end" */
static void udp_receive(int fd, int port)
{
    int u = udp_socket(port);
    if (u < 0) {
        answer(fd, "error %lu %lu %lu\n", (unsigned long)errno, 0, 0);
        return;
    }
    send_all(fd, "ready\n", 6);
    unsigned long packets = 0;
    unsigned long bytes = 0;
    uint64_t t0 = 0, t1 = 0;
    for (;;) {
        struct pollfd p[2] = { { u, POLLIN, 0 }, { fd, POLLIN, 0 } };
        if (poll(p, 2, 10000) <= 0)
            break;
        if (p[0].revents & POLLIN) {
            for (;;) { /* everything that waits, before looking at the connection again */
                int r = (int)recv(u, s_buf, sizeof(s_buf), MSG_DONTWAIT);
                if (r < 0)
                    break;
                t1 = now_us();
                if (!packets)
                    t0 = t1;
                packets++;
                bytes += (unsigned)r;
            }
        }
        if (p[1].revents) { /* "end" (or the client is gone): read, or closing would reset the
                             * connection and the client lose our answer */
            char end[8];
            recv(fd, end, sizeof(end), MSG_DONTWAIT);
            break;
        }
    }
    close(u);
    printf("nettest: UDP received %lu datagrams, %lu bytes in %lu us: %.1f Mbit/s\n", packets, bytes,
           (unsigned long)(t1 - t0), mbit(bytes, t1 - t0));
    answer(fd, "udp %lu %lu %lu\n", packets, bytes, (unsigned long)(t1 - t0));
}

/* V: send UDP datagrams to the client for @secs seconds, from our port of the test's number
 * (the client sent one there first: a firewall on its side lets the answers in) */
static void udp_send(int fd, int secs, int port, int uport)
{
    struct sockaddr_in to;
    socklen_t tlen = sizeof(to);
    int u = udp_socket(port);
    if (u < 0 || getpeername(fd, (struct sockaddr *)&to, &tlen) < 0) {
        answer(fd, "error %lu %lu %lu\n", (unsigned long)errno, 0, 0);
        if (u >= 0)
            close(u);
        return;
    }
    to.sin_port = htons((uint16_t)uport);
    unsigned long packets = 0;
    unsigned long bytes = 0;
    uint64_t t0 = now_us(), end = t0 + (uint64_t)secs * 1000000u, t1 = t0;
    while (t1 < end) {
        int w = (int)sendto(u, s_buf, DGRAM, 0, (struct sockaddr *)&to, sizeof(to));
        if (w > 0) {
            packets++;
            bytes += (unsigned)w;
        } else {
            crtos_yield(); /* (the driver's ring is full) */
        }
        if (!(packets & 15))
            t1 = now_us();
    }
    t1 = now_us();
    close(u);
    printf("nettest: UDP sent %lu datagrams, %lu bytes in %lu us: %.1f Mbit/s\n", packets, bytes,
           (unsigned long)(t1 - t0), mbit(bytes, t1 - t0));
    answer(fd, "udp-sent %lu %lu %lu\n", packets, bytes, (unsigned long)(t1 - t0));
}

/* One test; false: the connection was not a test request (e.g. a check that we listen) */
static bool serve(int fd, int port)
{
    uint8_t req[12];
    struct timeval tv = { 10, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (recv_all(fd, req, sizeof(req)) || memcmp(req, "NTST", 4))
        return false;
    tv.tv_sec = 15; /* a client that stops for that long has gone */
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int secs = req[5] ? req[5] : 5;
    int uport = (req[8] << 8) | req[9];
    switch (req[4]) {
    case 'R':
        tcp_receive(fd);
        break;
    case 'S':
        tcp_send(fd, secs);
        break;
    case 'U':
        udp_receive(fd, port);
        break;
    case 'V':
        udp_send(fd, secs, port, uport);
        break;
    default:
        printf("nettest: unknown test '%c'\n", req[4]);
    }
    return true;
}

int main(int argc, char **argv)
{
    int port = DEFAULT_PORT, tests = 0, idle = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
            tests = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            idle = atoi(argv[++i]);
        } else {
            fprintf(stderr, "usage: nettest [-p port] [-n tests] [-t idle-seconds]\n");
            return 2;
        }
    }
    for (size_t i = 0; i < sizeof(s_buf); i++)
        s_buf[i] = (uint8_t)i;
    int l = socket(AF_INET, SOCK_STREAM, 0);
    if (l < 0) {
        fprintf(stderr, "nettest: socket: %s\n", strerror(errno));
        return 1;
    }
    int on = 1;
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (bind(l, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(l, 2) < 0) {
        fprintf(stderr, "nettest: port %d: %s\n", port, strerror(errno));
        return 1;
    }
    if (idle) {
        struct timeval tv = { idle, 0 };
        setsockopt(l, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    printf("nettest: listening on port %d\n", port);
    for (int n = 0; !tests || n < tests;) {
        int fd = accept(l, NULL, NULL);
        if (fd < 0) {
            printf("nettest: no client for %d s\n", idle);
            break;
        }
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
        if (serve(fd, port))
            n++;
        close(fd);
    }
    close(l);
    return 0;
}
