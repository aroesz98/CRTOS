/*
 * platform_ksyms.h - the board support's functions the kernel exports to modules besides its
 * own API (kernel/os/ksyms.cpp): NXP SDK services that live in the kernel image.
 */
#ifndef CRTOS_PLATFORM_KSYMS_H
#define CRTOS_PLATFORM_KSYMS_H

#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_cache.h"

#define PLATFORM_KSYMS                                                                                 \
    KSYM(CLOCK_GetFreq) KSYM(CLOCK_GetPllFreq) KSYM(CLOCK_GetSysPfdFreq) KSYM(CLOCK_GetUsb1PfdFreq)    \
    KSYM(CLOCK_InitVideoPll) KSYM(CLOCK_DeinitVideoPll) KSYM(CLOCK_InitSysPfd) KSYM(CLOCK_InitUsb1Pfd) \
    KSYM(CLOCK_InitEnetPll) KSYM(SDK_DelayAtLeastUs) KSYM(SystemCoreClock)                             \
    KSYM(DCACHE_CleanByRange) KSYM(DCACHE_InvalidateByRange) KSYM(DCACHE_CleanInvalidateByRange)

#endif
