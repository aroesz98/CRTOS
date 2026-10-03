/*
 * lib/crc32.h - CRC-32 (IEEE 802.3 / zlib polynomial)
 */
#ifndef CRTOS_LIB_CRC32_H
#define CRTOS_LIB_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* crc32(0, data, len) for a single buffer; pass the previous result to continue */
uint32_t crc32(uint32_t crc, const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
