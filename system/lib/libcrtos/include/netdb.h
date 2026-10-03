/*
 * netdb.h - host and service lookups. Names go to the kernel's DNS resolver (servers from
 * DHCP or netmgr); services are numbers or a few well-known names.
 */
#ifndef _NETDB_H
#define _NETDB_H

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};
#define h_addr h_addr_list[0]

#define HOST_NOT_FOUND  1
#define TRY_AGAIN       2
#define NO_RECOVERY     3
#define NO_DATA         4

extern int h_errno;
struct hostent *gethostbyname(const char *name);        /* a static result */

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    socklen_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE      0x01
#define AI_CANONNAME    0x02
#define AI_NUMERICHOST  0x04
#define AI_NUMERICSERV  0x0400

#define EAI_BADFLAGS    -1
#define EAI_NONAME      -2
#define EAI_AGAIN       -3
#define EAI_FAIL        -4
#define EAI_FAMILY      -6
#define EAI_SOCKTYPE    -7
#define EAI_SERVICE     -8
#define EAI_MEMORY      -10
#define EAI_SYSTEM      -11

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *ai);
const char *gai_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif
