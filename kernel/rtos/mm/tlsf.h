/*
 * kernel/rtos/mm/tlsf.h - Two-Level Segregated Fit allocator (O(1) malloc/free).
 * Not thread safe: callers serialise access.
 */
#ifndef KERNEL_MM_TLSF_H
#define KERNEL_MM_TLSF_H

#include <stddef.h>
#include <stdint.h>

struct tlsf;

/* Build an allocator inside [mem, mem+bytes): control structure followed by the pool */
struct tlsf *tlsf_create(void *mem, size_t bytes);
void *tlsf_malloc(struct tlsf *t, size_t size);
void *tlsf_memalign(struct tlsf *t, size_t align, size_t size);
/* ... and not crossing a multiple of @boundary (a power of two >= align and >= size) */
void *tlsf_memalign_bounded(struct tlsf *t, size_t align, size_t size, size_t boundary);
int tlsf_free(struct tlsf *t, void *ptr);           /* 0, or <0 for an invalid pointer */
size_t tlsf_block_size(const void *ptr);
int tlsf_owns(const struct tlsf *t, const void *ptr);

struct tlsf_stats {
    size_t total;       /* bytes managed (payload + headers) */
    size_t free;        /* payload bytes in free blocks */
    size_t used;        /* payload bytes in allocated blocks */
    size_t largest;     /* largest free block */
    uint32_t nused;     /* allocated blocks */
};
void tlsf_get_stats(const struct tlsf *t, struct tlsf_stats *st);

/* Walk the pool and verify all invariants; returns 0 or a negative error location */
int tlsf_check(const struct tlsf *t);

#endif
