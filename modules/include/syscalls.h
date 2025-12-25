#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Optional hook: provide a board-specific byte output used by _write if present.
// Implement this weak symbol elsewhere to route stdout to UART/ITM/etc.
__attribute__((weak)) int crt_putchar(int ch);

#ifdef __cplusplus
}
#endif
