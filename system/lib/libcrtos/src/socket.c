/*
 * socket.c - BSD sockets on the kernel's socket calls, IPv4 address conversions
 * (arpa/inet.h) and host/service lookups (netdb.h) with the kernel's resolver.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <crtos.h>

static long sys6(long id, long a0, long a1, long a2, long a3, long a4, long a5)
{
    long r = __crtos_syscall6(id, a0, a1, a2, a3, a4, a5);
    if ((unsigned long)r >= (unsigned long)-4095) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

#define SYS6(id, a0, a1, a2, a3, a4, a5) sys6((id), (long)(a0), (long)(a1), (long)(a2), (long)(a3), (long)(a4), (long)(a5))

/* ---- sockets ---------------------------------------------------------------------------------- */

int socket(int domain, int type, int protocol)
{
    return (int)SYS6(SYS_SOCKET, domain, type, protocol, 0, 0, 0);
}

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    return (int)SYS6(SYS_BIND, fd, addr, len, 0, 0, 0);
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    return (int)SYS6(SYS_CONNECT, fd, addr, len, 0, 0, 0);
}

int listen(int fd, int backlog)
{
    return (int)SYS6(SYS_LISTEN, fd, backlog, 0, 0, 0, 0);
}

int accept(int fd, struct sockaddr *addr, socklen_t *len)
{
    return (int)SYS6(SYS_ACCEPT, fd, addr, len, 0, 0, 0);
}

ssize_t sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen)
{
    return (ssize_t)SYS6(SYS_SENDTO, fd, buf, len, flags, to, tolen);
}

ssize_t send(int fd, const void *buf, size_t len, int flags)
{
    return sendto(fd, buf, len, flags, NULL, 0);
}

ssize_t recvfrom(int fd, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
    return (ssize_t)SYS6(SYS_RECVFROM, fd, buf, len, flags, from, fromlen);
}

ssize_t recv(int fd, void *buf, size_t len, int flags)
{
    return recvfrom(fd, buf, len, flags, NULL, NULL);
}

int shutdown(int fd, int how)
{
    return (int)SYS6(SYS_SHUTDOWN, fd, how, 0, 0, 0, 0);
}

int setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    return (int)SYS6(SYS_SETSOCKOPT, fd, level, name, val, len, 0);
}

int getsockopt(int fd, int level, int name, void *val, socklen_t *len)
{
    return (int)SYS6(SYS_GETSOCKOPT, fd, level, name, val, len, 0);
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *len)
{
    return (int)SYS6(SYS_GETSOCKNAME, fd, addr, len, 0, 0, 0);
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *len)
{
    return (int)SYS6(SYS_GETPEERNAME, fd, addr, len, 0, 0, 0);
}

/* ---- addresses as text --------------------------------------------------------------------------- */

int inet_aton(const char *s, struct in_addr *a)
{
    uint32_t v = 0;
    for (int part = 0; part < 4; part++) {
        if (part && *s++ != '.')
            return 0;
        if (*s < '0' || *s > '9')
            return 0;
        unsigned n = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            n = n * 10u + (unsigned)(*s++ - '0');
            if (++digits > 3 || n > 255)
                return 0;
        }
        v = v << 8 | n;
    }
    if (*s)
        return 0;
    a->s_addr = htonl(v);
    return 1;
}

in_addr_t inet_addr(const char *s)
{
    struct in_addr a;
    return inet_aton(s, &a) ? a.s_addr : INADDR_NONE;
}

int inet_pton(int af, const char *src, void *dst)
{
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return -1;
    }
    return inet_aton(src, (struct in_addr *)dst);
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    const uint8_t *b = (const uint8_t *)src;
    if ((socklen_t)snprintf(dst, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]) >= size) {
        errno = ENOSPC;
        return NULL;
    }
    return dst;
}

char *inet_ntoa(struct in_addr a)
{
    static char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    return buf;
}

/* ---- names ------------------------------------------------------------------------------------------ */

int h_errno;

/* A host name or address to an address (network byte order): 0 or an EAI_* error */
static int resolve(const char *name, in_addr_t *addr)
{
    struct in_addr a;
    if (inet_aton(name, &a)) {
        *addr = a.s_addr;
        return 0;
    }
    if (!strcmp(name, "localhost")) {
        *addr = htonl(INADDR_LOOPBACK);
        return 0;
    }
    struct net_resolve r;
    if (strlen(name) >= sizeof(r.name))
        return EAI_NONAME;
    memset(&r, 0, sizeof(r));
    strcpy(r.name, name);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return EAI_SYSTEM;
    int e = ioctl(fd, NET_IOC_RESOLVE, &r);
    int err = errno;
    close(fd);
    if (e < 0) {
        errno = err;
        return err == EHOSTUNREACH || err == EINVAL ? EAI_NONAME : EAI_AGAIN;
    }
    *addr = r.addr;
    return 0;
}

struct hostent *gethostbyname(const char *name)
{
    static struct hostent h;
    static in_addr_t addr;
    static char *list[2];
    static char hname[128];
    int e = resolve(name, &addr);
    if (e) {
        h_errno = e == EAI_AGAIN ? TRY_AGAIN : HOST_NOT_FOUND;
        return NULL;
    }
    strncpy(hname, name, sizeof(hname) - 1);
    list[0] = (char *)&addr;
    list[1] = NULL;
    h.h_name = hname;
    h.h_aliases = &list[1];
    h.h_addrtype = AF_INET;
    h.h_length = sizeof(addr);
    h.h_addr_list = list;
    return &h;
}

static const struct {
    const char *name;
    uint16_t port;
} s_services[] = {
    { "echo", 7 }, { "ftp", 21 }, { "ssh", 22 }, { "telnet", 23 }, { "domain", 53 }, { "http", 80 },
    { "ntp", 123 }, { "https", 443 },
};

static int service_port(const char *service, int numeric, uint16_t *port)
{
    char *end;
    unsigned long n = strtoul(service, &end, 10);
    if (*service && !*end) {
        if (n > 65535)
            return EAI_SERVICE;
        *port = (uint16_t)n;
        return 0;
    }
    if (numeric)
        return EAI_NONAME;
    for (size_t i = 0; i < sizeof(s_services) / sizeof(s_services[0]); i++) {
        if (!strcmp(service, s_services[i].name)) {
            *port = s_services[i].port;
            return 0;
        }
    }
    return EAI_SERVICE;
}

struct ai_block {
    struct addrinfo ai;
    struct sockaddr_in sin;
    char canon[128];
};

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo h;
    memset(&h, 0, sizeof(h));
    if (hints)
        h = *hints;
    if (!node && !service)
        return EAI_NONAME;
    if (h.ai_family != AF_UNSPEC && h.ai_family != AF_INET)
        return EAI_FAMILY;
    if (h.ai_socktype && h.ai_socktype != SOCK_STREAM && h.ai_socktype != SOCK_DGRAM && h.ai_socktype != SOCK_RAW)
        return EAI_SOCKTYPE;

    uint16_t port = 0;
    if (service) {
        int e = service_port(service, h.ai_flags & AI_NUMERICSERV, &port);
        if (e)
            return e;
    }
    in_addr_t addr;
    if (!node) {
        addr = h.ai_flags & AI_PASSIVE ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    } else {
        struct in_addr a;
        if (inet_aton(node, &a)) {
            addr = a.s_addr;
        } else {
            if (h.ai_flags & AI_NUMERICHOST)
                return EAI_NONAME;
            int e = resolve(node, &addr);
            if (e)
                return e;
        }
    }

    static const int types[] = { SOCK_STREAM, SOCK_DGRAM };
    struct addrinfo *first = NULL, **tail = &first;
    for (size_t i = 0; i < 2; i++) {
        int type = h.ai_socktype ? h.ai_socktype : types[i];
        if (h.ai_socktype && i)
            break;
        struct ai_block *b = (struct ai_block *)calloc(1, sizeof(*b));
        if (!b) {
            freeaddrinfo(first);
            return EAI_MEMORY;
        }
        b->sin.sin_family = AF_INET;
        b->sin.sin_port = htons(port);
        b->sin.sin_addr.s_addr = addr;
        b->ai.ai_family = AF_INET;
        b->ai.ai_socktype = type;
        b->ai.ai_protocol = h.ai_protocol ? h.ai_protocol
                            : type == SOCK_STREAM ? IPPROTO_TCP
                            : type == SOCK_DGRAM  ? IPPROTO_UDP
                                                  : 0;
        b->ai.ai_addrlen = sizeof(b->sin);
        b->ai.ai_addr = (struct sockaddr *)&b->sin;
        if ((h.ai_flags & AI_CANONNAME) && node) {
            strncpy(b->canon, node, sizeof(b->canon) - 1);
            b->ai.ai_canonname = b->canon;
        }
        *tail = &b->ai;
        tail = &b->ai.ai_next;
    }
    *res = first;
    return 0;
}

void freeaddrinfo(struct addrinfo *ai)
{
    while (ai) {
        struct addrinfo *next = ai->ai_next;
        free(ai); /* the first member of its struct ai_block */
        ai = next;
    }
}

const char *gai_strerror(int err)
{
    switch (err) {
    case 0:
        return "Success";
    case EAI_BADFLAGS:
        return "Bad flags";
    case EAI_NONAME:
        return "Name or service not known";
    case EAI_AGAIN:
        return "Temporary failure in name resolution";
    case EAI_FAIL:
        return "Non-recoverable failure in name resolution";
    case EAI_FAMILY:
        return "Address family not supported";
    case EAI_SOCKTYPE:
        return "Socket type not supported";
    case EAI_SERVICE:
        return "Service not supported for the socket type";
    case EAI_MEMORY:
        return "Out of memory";
    case EAI_SYSTEM:
        return "System error";
    }
    return "Unknown error";
}
