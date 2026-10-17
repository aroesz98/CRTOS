/*
 * psram.h - the PSRAM of the STM32H7S78-DK (AP Memory APS256XXN on XSPI1) as the boot loader
 * sets it up (boot/xspi.c) and the kernel finds it before it switches the memory to variable
 * latency (board/platform.cpp): the register commands and the values of MR0 and MR1.
 */
#ifndef CRTOS_STM32_PSRAM_H
#define CRTOS_STM32_PSRAM_H

#define PSRAM_READ_REG      0x40u   /* mode register read, write */
#define PSRAM_WRITE_REG     0xC0u

/* MR0: read latency code 4 (7 clocks), full drive, fixed latency (bit 5: the boot loader's)
 * or variable latency */
#define PSRAM_MR0_FIXED     (0x20u | (4u << 2))
#define PSRAM_MR0_VARIABLE  (4u << 2)
/* MR1 as read: a register write in DTR carries two bytes, MR0's and then this (MR1 is read only) */
#define PSRAM_MR1           0x8Du

#endif
