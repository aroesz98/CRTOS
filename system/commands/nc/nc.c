/*
 * nc - TCP and UDP from the command line.
 *
 *   nc [-u] [-w secs] host port        connect: standard input to the peer, the peer to
 *                                      standard output
 *   nc -l [-u] [-k] [-e] port          listen: one client at a time; -k: go on listening
 *                                      after it leaves; -e: echo what arrives back
 */
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <crtos.h>

static bool s_udp, s_echo;
static int s_wait_s;
static char s_buf[2048];

static int send_all(int fd, const char *p, int n)
{
    while (n > 0) {
        int w = (int)send(fd, p, (size_t)n, 0);
        if (w < 0)
            return -1;
        p += w;
        n -= w;
    }
    return 0;
}

static int write_all(int fd, const char *p, int n)
{
    while (n > 0) {
        int w = (int)write(fd, p, (size_t)n);
        if (w <= 0)
            return -1;
        p += w;
        n -= w;
    }
    return 0;
}

/* Move data between standard input/output and the connected socket until either side ends */
static void relay(int s)
{
    bool in_open = !s_echo;
    for (;;) {
        struct pollfd p[2] = { { s, POLLIN, 0 }, { 0, POLLIN, 0 } };
        int r = poll(p, in_open ? 2 : 1, s_wait_s ? s_wait_s * 1000 : -1);
        if (r == 0) {
            fprintf(stderr, "nc: idle for %d s\n", s_wait_s);
            return;
        }
        if (r < 0)
            return;
        if (p[0].revents) {
            int n = (int)recv(s, s_buf, sizeof(s_buf), 0);
            if (n <= 0) {
                if (n < 0)
                    fprintf(stderr, "nc: %s\n", strerror(errno));
                return;
            }
            if (s_echo ? send_all(s, s_buf, n) : write_all(1, s_buf, n))
                return;
        }
        if (in_open && p[1].revents) {
            int n = (int)read(0, s_buf, sizeof(s_buf));
            if (n <= 0) {
                in_open = false;
                if (!s_udp)
                    shutdown(s, SHUT_WR);
            } else if (send_all(s, s_buf, n)) {
                fprintf(stderr, "nc: %s\n", strerror(errno));
                return;
            }
        }
    }
}

/* UDP server: datagrams from anyone; the answers (echo, or standard input) go to the last sender */
static void udp_serve(int s)
{
    struct sockaddr_in peer;
    socklen_t plen = 0;
    memset(&peer, 0, sizeof(peer));
    bool in_open = !s_echo;
    for (;;) {
        struct pollfd p[2] = { { s, POLLIN, 0 }, { 0, POLLIN, 0 } };
        if (poll(p, in_open ? 2 : 1, -1) < 0)
            return;
        if (p[0].revents) {
            plen = sizeof(peer);
            int n = (int)recvfrom(s, s_buf, sizeof(s_buf), 0, (struct sockaddr *)&peer, &plen);
            if (n < 0)
                return;
            if (s_echo)
                sendto(s, s_buf, (size_t)n, 0, (struct sockaddr *)&peer, plen);
            else
                write_all(1, s_buf, n);
        }
        if (in_open && p[1].revents) {
            int n = (int)read(0, s_buf, sizeof(s_buf));
            if (n <= 0)
                in_open = false;
            else if (plen)
                sendto(s, s_buf, (size_t)n, 0, (struct sockaddr *)&peer, plen);
        }
    }
}

static int listen_on(int port, bool keep)
{
    int s = socket(AF_INET, s_udp ? SOCK_DGRAM : SOCK_STREAM, 0);
    if (s < 0) {
        fprintf(stderr, "nc: socket: %s\n", strerror(errno));
        return 1;
    }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)port);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0) {
        fprintf(stderr, "nc: bind to port %d: %s\n", port, strerror(errno));
        return 1;
    }
    if (s_udp) {
        udp_serve(s);
        return 0;
    }
    if (listen(s, 4) < 0) {
        fprintf(stderr, "nc: listen: %s\n", strerror(errno));
        return 1;
    }
    do {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int c = accept(s, (struct sockaddr *)&peer, &plen);
        if (c < 0) {
            fprintf(stderr, "nc: accept: %s\n", strerror(errno));
            return 1;
        }
        char pa[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, pa, sizeof(pa));
        fprintf(stderr, "nc: connection from %s:%u\n", pa, ntohs(peer.sin_port));
        relay(c);
        close(c);
    } while (keep);
    close(s);
    return 0;
}

static int connect_to(const char *host, const char *port)
{
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = s_udp ? SOCK_DGRAM : SOCK_STREAM;
    int e = getaddrinfo(host, port, &hints, &ai);
    if (e) {
        fprintf(stderr, "nc: %s: %s\n", host, gai_strerror(e));
        return 1;
    }
    int s = socket(AF_INET, ai->ai_socktype, 0);
    if (s < 0) {
        fprintf(stderr, "nc: socket: %s\n", strerror(errno));
        freeaddrinfo(ai);
        return 1;
    }
    if (s_wait_s) {
        struct timeval tv = { s_wait_s, 0 };
        setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    }
    if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
        fprintf(stderr, "nc: connect to %s port %s: %s\n", host, port, strerror(errno));
        freeaddrinfo(ai);
        return 1;
    }
    freeaddrinfo(ai);
    relay(s);
    close(s);
    return 0;
}

static void usage(void)
{
    fprintf(stderr, "usage: nc [-u] [-w secs] host port\n       nc -l [-u] [-k] [-e] port\n");
    exit(2);
}

int main(int argc, char **argv)
{
    bool listen_mode = false, keep = false;
    int opt;
    while ((opt = getopt(argc, argv, "lukew:")) != -1) {
        switch (opt) {
        case 'l':
            listen_mode = true;
            break;
        case 'u':
            s_udp = true;
            break;
        case 'k':
            keep = true;
            break;
        case 'e':
            s_echo = true;
            break;
        case 'w':
            s_wait_s = atoi(optarg);
            break;
        default:
            usage();
        }
    }
    if (listen_mode) {
        if (optind != argc - 1)
            usage();
        return listen_on(atoi(argv[optind]), keep);
    }
    if (optind != argc - 2 || s_echo)
        usage();
    return connect_to(argv[optind], argv[optind + 1]);
}
