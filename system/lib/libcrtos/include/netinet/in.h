/*
 * netinet/in.h - IPv4 addresses and the byte order conversions.
 */
#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _IN_ADDR_T_DECLARED
typedef uint32_t in_addr_t;
#define _IN_ADDR_T_DECLARED
#endif
#ifndef _IN_PORT_T_DECLARED
typedef uint16_t in_port_t;
#define _IN_PORT_T_DECLARED
#endif

struct in_addr {
    in_addr_t s_addr;               /* network byte order */
};

/* the same layout as the kernel's struct crtos_sockaddr_in */
struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;             /* network byte order */
    struct in_addr sin_addr;
    unsigned char sin_zero[8];
};

#define IPPROTO_RAW         255
#define INET_ADDRSTRLEN     16

static inline uint16_t htons(uint16_t x) { return __builtin_bswap16(x); }
static inline uint16_t ntohs(uint16_t x) { return __builtin_bswap16(x); }
static inline uint32_t htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint32_t ntohl(uint32_t x) { return __builtin_bswap32(x); }

#ifdef __cplusplus
}
#endif

#endif
