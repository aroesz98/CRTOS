/*
 * crtos/socket.h - the sockets ABI shared by the kernel and programs: address families,
 * socket types, address structures, options and the network interface ioctls.
 *
 * Programs use the POSIX headers of libcrtos (<sys/socket.h>, <netinet/in.h>, <arpa/inet.h>,
 * <netdb.h>), which are built on these definitions. Addresses and ports are in network
 * byte order, as usual. Sockets are files: read, write, poll, close and dup work on them.
 */
#ifndef CRTOS_SOCKET_H
#define CRTOS_SOCKET_H

#include <stdint.h>
#include <crtos/ioctl.h>

#define AF_UNSPEC       0
#define AF_INET         2
#define PF_UNSPEC       AF_UNSPEC
#define PF_INET         AF_INET

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3
#define SOCK_NONBLOCK   0x4000      /* with the type: a non-blocking socket (= O_NONBLOCK) */
#define SOCK_CLOEXEC    0x80000     /* accepted, ignored */

#define IPPROTO_IP      0
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

/* send / recv flags */
#define MSG_PEEK        0x01
#define MSG_WAITALL     0x02
#define MSG_OOB         0x04
#define MSG_DONTWAIT    0x08
#define MSG_MORE        0x10
#define MSG_NOSIGNAL    0x20

#define SHUT_RD         0
#define SHUT_WR         1
#define SHUT_RDWR       2

/* options: levels and names */
#define SOL_SOCKET      0xfff
#define SO_REUSEADDR    0x0004
#define SO_KEEPALIVE    0x0008
#define SO_BROADCAST    0x0020
#define SO_LINGER       0x0080
#define SO_SNDBUF       0x1001
#define SO_RCVBUF       0x1002
#define SO_SNDTIMEO     0x1005      /* struct timeval */
#define SO_RCVTIMEO     0x1006      /* struct timeval */
#define SO_ERROR        0x1007
#define SO_TYPE         0x1008

#define IP_TOS          1
#define IP_TTL          2

#define TCP_NODELAY     0x01
#define TCP_KEEPALIVE   0x02
#define TCP_KEEPIDLE    0x03
#define TCP_KEEPINTVL   0x04
#define TCP_KEEPCNT     0x05

#define INADDR_ANY          0x00000000u
#define INADDR_BROADCAST    0xffffffffu
#define INADDR_NONE         0xffffffffu
#define INADDR_LOOPBACK     0x7f000001u     /* host byte order, as in POSIX */

/* the IPv4 socket address, as on Linux (16 bytes) */
struct crtos_sockaddr_in {
    uint16_t sin_family;        /* AF_INET */
    uint16_t sin_port;          /* network byte order */
    uint32_t sin_addr;          /* network byte order */
    uint8_t sin_zero[8];
};

/* ---- network interfaces: ioctls on any socket ---------------------------------------- */

#define NET_IFNAMSIZ    8

#define NET_IF_UP       0x01u       /* administratively up */
#define NET_IF_LINK     0x02u       /* carrier */
#define NET_IF_DHCP     0x04u       /* the address comes from DHCP */
#define NET_IF_BOUND    0x08u       /* DHCP: a lease is held */
#define NET_IF_FULL_DUPLEX 0x10u
#define NET_IF_LOOPBACK 0x20u

struct net_ifinfo {
    int32_t index;              /* in: which one (0..count-1), or -1 to look up @name */
    char name[NET_IFNAMSIZ];
    uint8_t mac[6];
    uint16_t mtu;
    uint32_t flags;             /* NET_IF_* */
    uint32_t speed;             /* Mbit/s */
    uint32_t addr, netmask, gw; /* network byte order */
    uint32_t rx_packets, tx_packets, rx_bytes, tx_bytes;
    uint32_t rx_errors, tx_errors, rx_dropped, tx_dropped;
};

/* Configure an interface (needs the sys capability) */
#define NET_CONF_UP     1u          /* bring it up */
#define NET_CONF_DOWN   2u
#define NET_CONF_STATIC 3u          /* addr, netmask, gw */
#define NET_CONF_DHCP   4u          /* get the address with DHCP */

struct net_ifconf {
    char name[NET_IFNAMSIZ];
    uint32_t what;              /* NET_CONF_* */
    uint32_t addr, netmask, gw; /* network byte order */
};

struct net_dns {
    uint32_t server[2];         /* network byte order, 0: none */
};

/* Look a host name up (DNS), blocking up to the resolver's time-out */
struct net_resolve {
    char name[128];
    uint32_t addr;              /* out, network byte order */
};

#define NET_IOC_IFCOUNT     _IOR('n', 1, uint32_t)
#define NET_IOC_IFINFO      _IOWR('n', 2, struct net_ifinfo)
#define NET_IOC_IFCONF      _IOW('n', 3, struct net_ifconf)
#define NET_IOC_DNS_GET     _IOR('n', 4, struct net_dns)
#define NET_IOC_DNS_SET     _IOW('n', 5, struct net_dns)
#define NET_IOC_RESOLVE     _IOWR('n', 6, struct net_resolve)
#define NET_IOC_FIONREAD    _IOR('n', 7, uint32_t)      /* bytes waiting to be read */

#endif
