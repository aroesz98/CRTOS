/*
 * crtos/uaccess.h - memory of the calling program, for drivers.
 *
 * A system call checks its own arguments, an ioctl() the structure its number declares
 * (_IOR/_IOW size); addresses inside such a structure (buffers of a transfer, an image) are
 * the driver's to check before it touches them. Kernel threads may access anything.
 */
#ifndef CRTOS_UACCESS_H
#define CRTOS_UACCESS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int uaccess_ok(const void *ptr, size_t len, int write);    /* 1: the program may access it */
int copy_from_user(void *dst, const void *src, size_t len);  /* 0 or -EFAULT */
int copy_to_user(void *dst, const void *src, size_t len);
/* The calling process holds all of @caps (CAP_* of crtos/syscall.h); kernel threads do */
int capable(uint32_t caps);

#ifdef __cplusplus
}
#endif

#endif
