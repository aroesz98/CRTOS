/*
 * posix.c - POSIX functions newlib leaves out, for ported programs: select() (on poll()),
 * clock_getres(), timegm(), uname(), gethostname() (clock_gettime() is in syscalls.c).
 */
#define _POSIX_TIMERS 1
#define _POSIX_MONOTONIC_CLOCK 1
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <crtos.h>

int select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *tv)
{
    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }
    struct pollfd p[FD_SETSIZE];
    int n = 0;
    for (int fd = 0; fd < nfds; fd++) {
        short ev = 0;
        if (rd && FD_ISSET(fd, rd))
            ev |= POLLIN;
        if (wr && FD_ISSET(fd, wr))
            ev |= POLLOUT;
        if (ev || (ex && FD_ISSET(fd, ex))) {
            p[n].fd = fd;
            p[n].events = ev;
            p[n].revents = 0;
            n++;
        }
    }
    int timeout = -1;
    if (tv) {
        long long ms = (long long)tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000;
        timeout = ms > 0x7FFFFFFF ? 0x7FFFFFFF : (int)ms;
    }
    int r = poll(p, (nfds_t)n, timeout);
    if (r < 0)
        return -1;
    if (rd)
        FD_ZERO(rd);
    if (wr)
        FD_ZERO(wr);
    if (ex)
        FD_ZERO(ex);
    int count = 0;
    for (int i = 0; i < n; i++) {
        short re = p[i].revents;
        /* an error or hang-up makes the next read or write return at once */
        if (rd && (p[i].events & POLLIN) && (re & (POLLIN | POLLHUP | POLLERR))) {
            FD_SET(p[i].fd, rd);
            count++;
        }
        if (wr && (p[i].events & POLLOUT) && (re & (POLLOUT | POLLERR | POLLHUP))) {
            FD_SET(p[i].fd, wr);
            count++;
        }
        if (ex && (re & POLLERR)) {
            FD_SET(p[i].fd, ex);
            count++;
        }
    }
    return count;
}

int clock_getres(clockid_t clock_id, struct timespec *res)
{
    if (clock_id != CLOCK_MONOTONIC && clock_id != CLOCK_REALTIME) {
        errno = EINVAL;
        return -1;
    }
    if (res) {
        res->tv_sec = 0;
        res->tv_nsec = 1000;
    }
    return 0;
}

/* mktime() for UTC: days since 1970 from the civil date */
time_t timegm(struct tm *tm)
{
    long long y = tm->tm_year + 1900LL, m = tm->tm_mon;
    y += m / 12;
    m %= 12;
    if (m < 0) {
        m += 12;
        y--;
    }
    y -= m < 2; /* the year starts in March */
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 1 ? -2 : 10)) + 2) / 5 + tm->tm_mday - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long long days = era * 146097 + doe - 719468;
    return (time_t)(days * 86400 + tm->tm_hour * 3600LL + tm->tm_min * 60LL + tm->tm_sec);
}

int uname(struct utsname *buf)
{
    memset(buf, 0, sizeof(*buf));
    strcpy(buf->sysname, "CRTOS");
    strcpy(buf->nodename, "crtos");
    strcpy(buf->release, "1");
    strcpy(buf->version, "1");
    strcpy(buf->machine, "armv7em");
    return 0;
}

int gethostname(char *name, size_t len)
{
    if (len < 6) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strcpy(name, "crtos");
    return 0;
}
