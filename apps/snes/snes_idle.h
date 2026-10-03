/*
 * snes_idle.h - wait loops and WAI of the SNES's 65816 fast-forwarded (snes_idle.cpp): what
 * the core (core/cpumacro.h, core/cpuexec.cpp, core/ppu.cpp) and the program use.
 */
#ifndef SNES_IDLE_H
#define SNES_IDLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Counters the core bumps; a wait loop is skipped only while they stay */
struct snes_idle_shared {
    uint32_t events;        /* S9xDoHEventProcessing calls, interrupts taken */
    uint32_t io;            /* reads of the PPU's and the CPU's registers */
    int enabled;
};
extern struct snes_idle_shared g_idle;

struct snes_idle_stats {
    uint32_t loops;         /* wait loops found */
    uint32_t skips;         /* times one was fast-forwarded */
    uint64_t loop_cycles;   /* master cycles skipped in wait loops */
    uint64_t wai_cycles;    /* and in WAI */
};

void snes_idle_enable(int on);
void snes_idle_reset(void);     /* a new game: forget the loops found */
const struct snes_idle_stats *snes_idle_stats(void);

#ifdef __cplusplus
}

void S9xIdleBackEdge(uint32_t branch);  /* a branch at @branch (same bank) was taken backwards */
void S9xIdleWaitSkip(void);             /* WAI: to just before the next thing the CPU reacts to */
#endif

#endif
