/*
 * image.h - the kernel image in the external NOR flash of the STM32H7S78-DK, as the boot
 * loader (internal flash) finds and checks it.
 *
 * The image starts with the kernel's vector table at CRTOS_IMAGE_BASE; at CRTOS_IMAGE_HDR_OFF
 * follows this header. tools/imagehdr.py fills in the size and the CRC-32 (zlib) of the whole
 * image, computed with the crc32 field as 0, after the link. A kernel update (the mtd driver)
 * checks the same before it writes anything.
 */
#ifndef CRTOS_STM32_IMAGE_H
#define CRTOS_STM32_IMAGE_H

#include <stdint.h>

#define CRTOS_IMAGE_BASE      0x70000000u     /* XSPI2: the NOR flash, memory-mapped */
#define CRTOS_IMAGE_MAX       (2u << 20)      /* the "kernel" partition */
#define CRTOS_IMAGE_HDR_OFF   0x400u
#define CRTOS_IMAGE_MAGIC     0x4E4B5243u     /* "CRKN" */

struct crtos_image_header {
    uint32_t magic;
    uint32_t header_size;       /* sizeof(struct crtos_image_header) */
    uint32_t image_size;        /* bytes from CRTOS_IMAGE_BASE */
    uint32_t crc32;             /* zlib CRC-32 of the image, this field counted as 0 */
    char build[32];             /* "built <date> <time>" */
};

#endif
