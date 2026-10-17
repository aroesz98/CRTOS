/*
 * boot/boot.h - the boot loader of the STM32H7S78-DK (internal flash).
 */
#ifndef CRTOS_BOOT_H
#define CRTOS_BOOT_H

#include "stm32h7rsxx_hal.h"

void boot_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void boot_panic(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));

/* boot/xspi.c: the NOR flash on XSPI2 (0x70000000) and the PSRAM on XSPI1 (0x90000000),
 * both memory-mapped at 200 MHz; 0 or a negative step number that failed */
int nor_init(void);
int psram_init(void);
uint32_t nor_id(void);

#endif
