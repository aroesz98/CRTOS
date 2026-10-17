/*
 * arpa/inet.h - IPv4 address text conversions.
 */
#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

in_addr_t inet_addr(const char *s);                     /* INADDR_NONE if not an address */
int inet_aton(const char *s, struct in_addr *a);        /* 1 if valid */
char *inet_ntoa(struct in_addr a);                      /* a static buffer */
const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
int inet_pton(int af, const char *src, void *dst);

#ifdef __cplusplus
}
#endif

#endif
