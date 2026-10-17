/*
 * poll.h - wait for several handles (files, devices, IPC ports).
 */
#ifndef _CRTOS_POLL_H
#define _CRTOS_POLL_H

#include <crtos/poll.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int nfds_t;

struct pollfd {
    int fd;
    short events;
    short revents;
};

/* timeout in milliseconds, < 0: no limit. Returns the number of ready handles. */
int poll(struct pollfd *fds, nfds_t n, int timeout);

#ifdef __cplusplus
}
#endif

#endif
