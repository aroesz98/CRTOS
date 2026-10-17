/*
 * ping - ICMP echo requests and the round-trip times of the replies.
 *
 *   ping [-c count] [-i interval_ms] [-s size] [-W timeout_ms] host
 *
 * Uses a raw ICMP socket: replies arrive with their IP header, as on other systems.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <crtos.h>

struct icmp_echo
{
    uint8_t type, code;
    uint16_t cksum;
    uint16_t id, seq;
    uint64_t sent_us; /* ours: the time of sending */
};

#define ICMP_ECHO_REPLY 0
#define ICMP_ECHO_REQUEST 8
#define MAX_SIZE 1400

static uint16_t checksum(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t sum = 0;
    for (; len > 1; len -= 2, p += 2)
        sum += (uint32_t)(p[0] << 8 | p[1]);
    if (len)
        sum += (uint32_t)p[0] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFFu) + (sum >> 16);
    return htons((uint16_t)~sum);
}

static void usage(void)
{
    fprintf(stderr, "usage: ping [-c count] [-i interval_ms] [-s size] [-W timeout_ms] host\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int count = 4, interval = 1000, size = 56, wait_ms = 1000;
    int opt;
    while ((opt = getopt(argc, argv, "c:i:s:W:")) != -1)
    {
        switch (opt)
        {
            case 'c':
                count = atoi(optarg);
                break;
            case 'i':
                interval = atoi(optarg);
                break;
            case 's':
                size = atoi(optarg);
                break;
            case 'W':
                wait_ms = atoi(optarg);
                break;
            default:
                usage();
        }
    }

    if (optind != argc - 1 || size < (int)(sizeof(struct icmp_echo) - 8) || size > MAX_SIZE || interval < 10 || wait_ms < 1)
        usage();
    const char *host = argv[optind];
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;
    int e = getaddrinfo(host, NULL, &hints, &ai);
    if (e)
    {
        fprintf(stderr, "ping: %s: %s\n", host, gai_strerror(e));
        return 2;
    }
    struct sockaddr_in to = *(struct sockaddr_in *)ai->ai_addr;
    freeaddrinfo(ai);

    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0)
    {
        fprintf(stderr, "ping: socket: %s\n", strerror(errno));
        return 2;
    }
    char addr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &to.sin_addr, addr, sizeof(addr));
    printf("PING %s (%s) %d data bytes\n", host, addr, size);

    static uint8_t pkt[8 + MAX_SIZE] __attribute__((aligned(8))), reply[60 + 8 + MAX_SIZE] __attribute__((aligned(8)));
    uint16_t id = (uint16_t)(crtos_gettid() ^ (uint16_t)crtos_time_us());
    int sent = 0, received = 0;
    uint64_t tmin = ~0ull, tmax = 0, tsum = 0;
    for (int seq = 1; count <= 0 || seq <= count; seq++)
    {
        struct icmp_echo *req = (struct icmp_echo *)pkt;
        memset(pkt, 0, sizeof(pkt));
        for (int i = sizeof(*req); i < 8 + size; i++)
            pkt[i] = (uint8_t)i;
        req->type = ICMP_ECHO_REQUEST;
        req->id = htons(id);
        req->seq = htons((uint16_t)seq);
        req->sent_us = crtos_time_us();
        req->cksum = checksum(pkt, 8 + (size_t)size);
        uint64_t start = req->sent_us;
        if (sendto(fd, pkt, 8 + (size_t)size, 0, (struct sockaddr *)&to, sizeof(to)) < 0)
        {
            printf("ping: send: %s\n", strerror(errno));
        }
        else
        {
            sent++;
        }
        /* replies until the next request is due (or the time-out, for the last one) */
        uint64_t until = start + (uint64_t)(seq == count ? wait_ms : interval) * 1000u;
        for (;;)
        {
            uint64_t now = crtos_time_us();
            if (now >= until)
                break;
            uint64_t left = until - now;
            struct timeval tv = {(time_t)(left / 1000000u), (suseconds_t)(left % 1000000u)};
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            struct sockaddr_in from;
            socklen_t flen = sizeof(from);
            int n = (int)recvfrom(fd, reply, sizeof(reply), 0, (struct sockaddr *)&from, &flen);
            uint64_t t = crtos_time_us();
            if (n < 0)
                break;
            int ihl = (reply[0] & 0x0F) * 4;
            if (n < ihl + (int)sizeof(struct icmp_echo))
                continue;
            const struct icmp_echo *rep = (const struct icmp_echo *)(reply + ihl);
            if (rep->type != ICMP_ECHO_REPLY || ntohs(rep->id) != id)
                continue;
            uint64_t sent_us;
            memcpy(&sent_us, &rep->sent_us, sizeof(sent_us));
            uint64_t rtt = t - sent_us;
            received++;
            tsum += rtt;
            tmin = rtt < tmin ? rtt : tmin;
            tmax = rtt > tmax ? rtt : tmax;
            char fa[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from.sin_addr, fa, sizeof(fa));
            printf("%d bytes from %s: icmp_seq=%u ttl=%u time=%lu.%03lu ms\n", n - ihl, fa, ntohs(rep->seq), reply[8],
                   (unsigned long)(rtt / 1000u), (unsigned long)(rtt % 1000u));
            if (ntohs(rep->seq) == seq && seq == count)
                break;
        }
        uint64_t now = crtos_time_us();
        if (seq != count && now < until)
            crtos_sleep_ms((uint32_t)((until - now) / 1000u));
    }
    printf("--- %s ping statistics ---\n", host);
    printf("%d packets transmitted, %d received, %d%% packet loss\n", sent, received,
           sent ? (sent - received) * 100 / sent : 0);
    if (received)
    {
        uint64_t avg = tsum / (uint64_t)received;
        printf("rtt min/avg/max = %lu.%03lu/%lu.%03lu/%lu.%03lu ms\n", (unsigned long)(tmin / 1000u),
               (unsigned long)(tmin % 1000u), (unsigned long)(avg / 1000u), (unsigned long)(avg % 1000u),
               (unsigned long)(tmax / 1000u), (unsigned long)(tmax % 1000u));
    }
    close(fd);
    return received ? 0 : 1;
}
