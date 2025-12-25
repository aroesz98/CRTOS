#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODULE_MAGIC 0x4D4F4455u /* 'MODU' */
#define MODULE_DESC_VERSION 0x0001u

/* Max 32-byte ASCII name, null-terminated if shorter */
typedef struct __attribute__((packed)) ModuleDescriptor {
    uint32_t magic;           /* MODULE_MAGIC */
    uint16_t desc_version;    /* descriptor format version */
    uint16_t reserved0;       /* align */

    uint32_t api_version;     /* ABI/API version expected by host */

    uint8_t  name[32];        /* module name */

    uint8_t  semver_major;    /* module version: major */
    uint8_t  semver_minor;    /* module version: minor */
    uint16_t semver_patch;    /* module version: patch */

    uint32_t build_timestamp; /* UNIX epoch seconds at build time */

    uint32_t image_size;      /* total binary size in bytes (filled by linker) */

    uint32_t entry;           /* entry point address (function pointer) */

    uint32_t reserved[6];     /* future use (checksum, sig ptr, etc.) */
} ModuleDescriptor;

/* The module must export this entry symbol. */
typedef int (*module_entry_t)(uint32_t reason, void* ctx);
int module_entry(uint32_t reason, void* ctx);

/* Pointer to the descriptor at image base. */
extern const ModuleDescriptor __module_desc__;

#ifdef __cplusplus
}
#endif
