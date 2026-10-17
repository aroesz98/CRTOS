/*
 * crtos/ioctl.h - ioctl command encoding (as in Linux): direction, argument size, type and
 * number. For commands with a size the argument is a pointer; when user space calls such an
 * ioctl the kernel checks that the whole argument lies in memory the caller may access
 * before the driver sees it. _IO() commands take a plain integer argument.
 */
#ifndef CRTOS_IOCTL_H
#define CRTOS_IOCTL_H

#define _IOC_NONE   0u
#define _IOC_WRITE  1u      /* caller -> driver (driver reads *arg) */
#define _IOC_READ   2u      /* driver -> caller (driver writes *arg) */

#define _IOC(dir, type, nr, size) \
    (((unsigned)(dir) << 30) | ((unsigned)(size) << 16) | ((unsigned)(type) << 8) | (unsigned)(nr))
#define _IO(type, nr)          _IOC(_IOC_NONE, (type), (nr), 0u)
#define _IOR(type, nr, T)      _IOC(_IOC_READ, (type), (nr), sizeof(T))
#define _IOW(type, nr, T)      _IOC(_IOC_WRITE, (type), (nr), sizeof(T))
#define _IOWR(type, nr, T)     _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(T))

#define _IOC_DIR(cmd)   (((unsigned)(cmd) >> 30) & 3u)
#define _IOC_SIZE(cmd)  (((unsigned)(cmd) >> 16) & 0x3FFFu)
#define _IOC_TYPE(cmd)  (((unsigned)(cmd) >> 8) & 0xFFu)
#define _IOC_NR(cmd)    ((unsigned)(cmd) & 0xFFu)

/* For every open file, answered by the VFS itself (fcntl F_GETFL / F_SETFL) */
#define FIO_GETFL   _IOC(_IOC_READ, 'f', 1, 4u)     /* the open flags (VFS_O_*) */
#define FIO_SETFL   _IO('f', 2)                     /* arg: flags; O_NONBLOCK and O_APPEND change */

#endif
