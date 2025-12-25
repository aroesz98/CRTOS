#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Must match CRTOS::ProgramInfo layout exactly */
typedef struct ProgramInfo {
    uint32_t stackPointer;
    uint32_t entryPoint;         /* offset from segment base; CRTOS will relocate and set new_entry */
    uint32_t vectors[74];        /* not used by parser currently */
    uint32_t section_data_start_addr; /* offset from segment base (flash) to data load image */
    uint32_t section_data_dest_addr;  /* VMA address in RAM for .data */
    uint32_t section_data_size;       /* size of .data */
    uint32_t section_bss_start_addr;  /* VMA address in RAM for .bss */
    uint32_t section_bss_size;        /* size of .bss */
    uint32_t reserved[22];
    uint32_t vtor_offset;        /* will be set by loader to segment base pointer */
    uint32_t msp_limit;          /* initial limit; loader will update */
} ProgramInfo;

#ifdef __cplusplus
}
#endif
