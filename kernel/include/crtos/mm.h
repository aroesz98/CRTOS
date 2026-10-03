/*
 * crtos/mm.h - kernel memory allocation.
 *
 * The allocator manages several pools (ITCM, DTCM, OCRAM, SDRAM, non-cacheable SDRAM),
 * each with its own O(1) TLSF heap. Flags select which pools may serve a request; pools are
 * tried from fastest to largest.
 */
#ifndef CRTOS_MM_H
#define CRTOS_MM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KM_ANY      0x00u   /* any cacheable pool, fastest first */
#define KM_FAST     0x01u   /* tightly coupled memory only */
#define KM_EXEC     0x02u   /* memory that may hold code (module text) */
#define KM_LARGE    0x04u   /* big pools first (SDRAM) */
#define KM_NOCACHE  0x08u   /* non-cacheable (DMA buffers, framebuffers) */
#define KM_DMA      0x10u   /* reachable by bus-master DMA and not TCM */
#define KM_ONCHIP   0x20u   /* with KM_FAST | KM_EXEC: ITCM, else OCRAM, never SDRAM */
#define KM_ZERO     0x100u  /* zero the memory */

void *kmalloc(size_t size, unsigned flags);
void *kmalloc_aligned(size_t size, size_t align, unsigned flags);
/* aligned, and not crossing a multiple of @boundary (a power of two; 0: any) */
void *kmalloc_bounded(size_t size, size_t align, size_t boundary, unsigned flags);
void *kzalloc(size_t size, unsigned flags);
void kfree(void *ptr);
size_t ksize(const void *ptr);     /* usable size of an allocation */

struct mm_pool_info {
    const char *name;
    uintptr_t base;
    size_t size;
    size_t free;
    size_t largest_free;
    unsigned flags;
};
/* The fast code area (the end of ITCM): one program's hottest code (app.cpp). A block of at
 * least @size bytes at the top of ITCM that one MPU region describes (8 KB steps up to 64 KB,
 * 16 KB steps above), its size in @got; NULL if it is taken or too small. kfree() gives it
 * back. */
void *kmalloc_fastcode(size_t size, size_t *got);
int mm_pool_count(void);
int mm_pool_info(int index, struct mm_pool_info *info);
int mm_check(void);                /* verify heap integrity, 0 if intact */

#ifdef __cplusplus
}
#endif

#endif
