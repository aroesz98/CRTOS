/*
 * nes_fast.c - the hot path of the emulation as one unit of compilation: the CPU and the
 * memory map of the core (core/cpu6502.c, mmu.c), its APU (core/apu.c) with the sound
 * synthesizer (nes_sound.c, which uses the APU's tables), the fast PPU, the master clock and
 * the 6502 recompiler (nes_jit.c).
 * Every emulated CPU cycle goes through all of them (a memory access, the clock, three PPU
 * dots, the APU); in one file the compiler inlines those calls.
 */
#include "cpu6502.c"
#include "mmu.c"
#include "core/apu.c"
#include "nes_sound.c"
#include "nes_ppu.c"
#include "nes_core.c"
#include "nes_jit.c"
