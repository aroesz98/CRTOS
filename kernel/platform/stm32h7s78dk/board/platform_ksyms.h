/*
 * platform_ksyms.h - the board support's symbols the kernel exports to modules besides its own
 * API (kernel/os/ksyms.cpp). STM32 modules reach the registers through the CMSIS device header
 * and the caches through CMSIS' inline functions; only the core clock comes from here.
 */
#ifndef CRTOS_PLATFORM_KSYMS_H
#define CRTOS_PLATFORM_KSYMS_H

#define PLATFORM_KSYMS KSYM(SystemCoreClock)

#endif
