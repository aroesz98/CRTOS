/*
 * sys/statvfs.h - file system size and free space (POSIX; newlib has no such header).
 * libcrtos fills it from crtos_statfs(): blocks of 4096 bytes, no inode counts.
 */
#ifndef CRTOS_SYS_STATVFS_H
#define CRTOS_SYS_STATVFS_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ST_RDONLY   0x1u
#define ST_NOSUID   0x2u

struct statvfs {
    unsigned long f_bsize;      /* preferred block size */
    unsigned long f_frsize;     /* the unit of f_blocks, f_bfree, f_bavail */
    fsblkcnt_t f_blocks, f_bfree, f_bavail;
    fsfilcnt_t f_files, f_ffree, f_favail;
    unsigned long f_fsid, f_flag, f_namemax;
};

int statvfs(const char *path, struct statvfs *buf);
int fstatvfs(int fd, struct statvfs *buf);     /* ENOSYS: needs a path */

#ifdef __cplusplus
}
#endif

#endif
