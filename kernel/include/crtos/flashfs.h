/*
 * crtos/flashfs.h - the flash file system (flashfs.ko, mounted on /flash0): its control device
 * /dev/flashfs0 for programs (the flashfs command).
 *
 * Files are contiguous in the flash, so a program stored there can run in place (XIP). A
 * file is written once, from the start (open with O_CREAT/O_TRUNC, then write); writing it
 * again makes a new one that replaces the old when it is closed. Every change is one record
 * in a log; the file systems's space is counted in erase blocks (256 KB on the HyperFlash).
 */
#ifndef CRTOS_FLASHFS_H
#define CRTOS_FLASHFS_H

#include <stdint.h>
#include <crtos/ioctl.h>

#define FLASHFS_STATE_MOUNTED       1
#define FLASHFS_STATE_UNFORMATTED   2   /* no valid log: "flashfs format" */

struct flashfs_info {
    uint32_t state;                     /* FLASHFS_STATE_* */
    uint32_t erasesize;                 /* bytes of an erase block */
    uint32_t blocks, free_blocks;       /* for data */
    uint32_t largest_free;              /* blocks: the largest file that fits */
    uint32_t files, dirs;
    uint32_t log_seq, log_used, log_slots;  /* the record log: generation, slots used/all */
    uint32_t open_files;
};

/* a file's place in the flash (FLASHFS_IOC_FILE) */
struct flashfs_file {
    char path[128];                     /* in: in the file system ("/bin/x.app") */
    uint32_t block, blocks;             /* out: first erase block, count */
    uint32_t size, crc32;               /* out: bytes, CRC-32 of them when written */
    uint32_t address;                   /* out: where it is in the address space */
    uint32_t opens;
};

#define FLASHFS_IOC_INFO    _IOR('F', 1, struct flashfs_info)
#define FLASHFS_IOC_FILE    _IOWR('F', 2, struct flashfs_file)
/* check every file's data against its CRC-32: the number of damaged files (names in dmesg) */
#define FLASHFS_IOC_CHECK   _IO('F', 3)
/* erase the log and start empty (CAP_SYS; not while files are open): every file is lost */
#define FLASHFS_IOC_FORMAT  _IO('F', 4)
/* on a file of /flash0 opened for writing, before the first write: the size it will have
 * (bytes) - it then goes into the shortest free run that holds it rather than the longest
 * (files cannot move, so this keeps room for long files); -ENOSPC at once if none holds it */
#define FLASHFS_IOC_RESERVE _IOW('F', 5, uint32_t)

#endif
