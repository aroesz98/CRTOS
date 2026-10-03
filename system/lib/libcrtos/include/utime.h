/*
 * utime.h - newlib's (struct utimbuf) and the function it leaves to each system: libcrtos
 * sets a file's time to now (times NULL); a given time cannot be set (ENOSYS).
 */
#ifndef CRTOS_UTIME_H
#define CRTOS_UTIME_H

#include_next <utime.h>

#ifdef __cplusplus
extern "C" {
#endif

int utime(const char *path, const struct utimbuf *times);

#ifdef __cplusplus
}
#endif

#endif
