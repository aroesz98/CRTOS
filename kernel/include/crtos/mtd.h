/*
 * crtos/mtd.h - flash memory (MTD) for programs: /dev/mtd<n>, one per partition of the
 * device tree (as Linux mtd-user).
 *
 * read() and write() at the file position (lseek); a write can only turn bits 1 -> 0, so
 * what it goes to must have been erased (MEMERASE, whole erase blocks). Read-only
 * partitions (the kernel) refuse both.
 */
#ifndef CRTOS_MTD_H
#define CRTOS_MTD_H

#include <stdint.h>
#include <crtos/ioctl.h>

#define MTD_NORFLASH        3
#define MTD_WRITEABLE       0x400u
#define MTD_BIT_WRITEABLE   0x800u

struct mtd_info_user {
    uint8_t type;           /* MTD_NORFLASH */
    uint32_t flags;         /* MTD_WRITEABLE | MTD_BIT_WRITEABLE, 0 when read-only */
    uint32_t size;          /* bytes */
    uint32_t erasesize;     /* erase block */
    uint32_t writesize;     /* smallest write */
    uint32_t oobsize;
    uint64_t padding;
};

struct erase_info_user {
    uint32_t start;         /* in the partition, a multiple of erasesize */
    uint32_t length;        /* a multiple of erasesize */
};

#define MEMGETINFO  _IOR('M', 1, struct mtd_info_user)
#define MEMERASE    _IOW('M', 2, struct erase_info_user)

/* CRTOS: a new kernel image into the kernel partition, then a restart (CAP_SYS). It is
 * checked first (the boot header and the CRC-32 of the image), then written and read back
 * with interrupts off; the running kernel never runs again. A power cut meanwhile leaves a
 * board that only the debug probe brings back ("crtos flash"). */
struct mtd_kernel_update {
    uint64_t image;         /* address of the image (build/kernel/crtos.bin) */
    uint32_t len;
    uint32_t crc32;         /* of the image (crc32 as zlib) */
};
#define MTD_IOC_KERNEL_UPDATE _IOW('M', 64, struct mtd_kernel_update)

#if !defined(__ASSEMBLER__) && !defined(CRTOS_USER)
/* ---- for other modules (a file system on a partition) -------------------------------------
 * The flash driver (flexspi-mtd.ko) exports these; a module using them names it in
 * MODULE_DEPENDS. Offsets are in the partition. Programming and erasing stop the whole system
 * for their duration (the kernel runs from this flash): a 512-byte page about 0.5 ms, an
 * erase block up to about a second. A claimed partition refuses writes through /dev/mtd<n>. */
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

struct mtd_part;
struct device_node;

struct mtd_geometry {
    uint32_t size;              /* bytes */
    uint32_t erasesize;         /* erase block */
    const uint8_t *mapped;      /* the partition in the address space (reads, code in place) */
};

struct mtd_part *mtd_part_of_node(const struct device_node *np);   /* NULL: none */
int mtd_part_geometry(struct mtd_part *p, struct mtd_geometry *g);
int mtd_part_claim(struct mtd_part *p, const char *owner);          /* -EBUSY: claimed */
void mtd_part_release(struct mtd_part *p);
/* bits 1 -> 0 only (write into erased flash); any length and alignment */
int mtd_part_program(struct mtd_part *p, uint32_t off, const void *buf, size_t len);
int mtd_part_erase(struct mtd_part *p, uint32_t off, uint32_t len);   /* whole erase blocks */

#ifdef __cplusplus
}
#endif
#endif

#endif
