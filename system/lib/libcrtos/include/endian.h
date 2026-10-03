/*
 * endian.h - byte order, with the names glibc gives it (for ported code).
 */
#ifndef _CRTOS_ENDIAN_H
#define _CRTOS_ENDIAN_H

#include <machine/endian.h>

#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#define __BIG_ENDIAN    _BIG_ENDIAN
#define __BYTE_ORDER    _BYTE_ORDER

#endif
