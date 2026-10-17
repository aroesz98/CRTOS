/*
 * nes_jit.h - the 6502 recompiler of the NES emulator (nes_jit.c).
 */
#ifndef NES_JIT_H
#define NES_JIT_H

#include <stdint.h>

struct Emulator;

struct jit_stats {
    uint32_t compiled;      /* blocks translated */
    uint32_t blocks;        /* blocks in the cache now */
    uint32_t code_bytes;    /* of translations now */
    uint32_t flushes;       /* times the cache started over */
    uint32_t linked;        /* exits patched into direct jumps */
    uint32_t runs;          /* entries into translated code */
    uint32_t interpreted;   /* instructions left to the interpreter */
    /* why: reaching a checkpoint, an interrupt or DMA, code outside ROM, not translated */
    uint32_t why_budget, why_irq, why_ram, why_op;
    uint32_t ram_pc[8];     /* code outside ROM by 4 KB page of $0000-$7FFF */
};

/* For a game session (the cache starts empty); -1 without memory for it */
int jit_init(struct Emulator *e);
void jit_free(void);
/* 0: the interpreter alone */
void jit_enable(int on);
/* Runs the CPU until the PPU has finished the frame (in place of the interpreter's loop) */
void jit_frame(struct Emulator *e);
const struct jit_stats *jit_stats(void);
/* Compares translated code with the interpreter, @trials random cases for each opcode;
 * returns the number of differences (printed) */
int jit_selftest(int trials);

#endif
