/*
 * arch/cc.h - lwIP on the CRTOS kernel: compiler, byte order, diagnostics.
 *
 * lwIP runs inside the kernel (net-lwip.ko): error numbers are the kernel's (newlib's,
 * the same as programs see), errno is a slot of the calling thread, memory comes from the
 * kernel heap.
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stddef.h>
#include <stdint.h>
#include <crtos/errno.h>
#include <crtos/printk.h>
#include <crtos/sched.h>

#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1 /* lwIP's own isdigit() etc.: no C library here */
#define X8_F "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"
#define SZT_F "u"

#define BYTE_ORDER LITTLE_ENDIAN
#define LWIP_TIMEVAL_PRIVATE 1 /* no struct timeval in the kernel: lwIP's own */

#define errno (*task_errno_ptr()) /* per thread: several programs call at once */

#define lwip_htons(x) ((u16_t)__builtin_bswap16(x))
#define lwip_htonl(x) ((u32_t)__builtin_bswap32(x))

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_FIELD(x) x

#define LWIP_PLATFORM_DIAG(x) \
    do                        \
    {                         \
        printk x;             \
    } while (0)
/* an assertion is reported, not fatal: the network should not take the system down */
#define LWIP_PLATFORM_ASSERT(x)                                                     \
    do                                                                              \
    {                                                                               \
        printk("E: lwIP: assertion '%s' failed at %s:%d\n", x, __FILE__, __LINE__); \
    } while (0)

uint32_t crtos_lwip_rand(void);
#define LWIP_RAND() crtos_lwip_rand()

#endif
