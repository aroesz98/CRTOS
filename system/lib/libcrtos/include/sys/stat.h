/*
 * sys/stat.h - newlib's, plus what it keeps for Cygwin and RTEMS only: lstat() (libcrtos:
 * there are no symbolic links, so it is stat()) and the special times of utimensat()
 * (libcrtos: "now" is the only time a file's time can be set to).
 */
#ifndef CRTOS_SYS_STAT_H
#define CRTOS_SYS_STAT_H

#include_next <sys/stat.h>

#ifndef UTIME_NOW
#define UTIME_NOW   -2L
#define UTIME_OMIT  -1L
#endif

#if !defined(__SPU__) && !defined(__rtems__) && !defined(__CYGWIN__)
#ifdef __cplusplus
extern "C" {
#endif
int lstat(const char *__restrict __path, struct stat *__restrict __buf);
#ifdef __cplusplus
}
#endif
#endif

#endif
