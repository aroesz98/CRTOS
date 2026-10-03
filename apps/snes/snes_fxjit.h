/*
 * snes_fxjit.h - the Super FX (GSU) recompiler (snes_fxjit.cpp): what the core (core/fxinst.cpp
 * fx_run) and the program use.
 */
#ifndef SNES_FXJIT_H
#define SNES_FXJIT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fxjit_stats {
    uint32_t blocks;        /* translated (and addresses found untranslatable) */
    uint32_t code_bytes;
    uint32_t runs;          /* entries from the interpreter's loop */
    uint32_t interp;        /* interpreter steps it did not take */
    uint32_t x_budget;      /* left at a group start: the budget ran out */
    uint32_t x_static;      /* left for a known address (a block not linked yet) */
    uint32_t x_dyn;         /* left for JMP or LOOP to a register's address */
    uint32_t x_end;         /* left before what it does not translate */
    uint32_t x_odd;         /* left after RPIX outside the screen */
    uint32_t linked;
    uint32_t slots;         /* target caches filled */
    uint32_t compiled;
    uint32_t flushes;
    uint32_t bugs;          /* blocks given up (an immediate it cannot encode) */
    const void *code;       /* where the translations are */
};

int fxjit_init(void);           /* -1: no memory (then the interpreter does everything) */
void fxjit_reset(void);         /* a new game: forget the translations */
void fxjit_enable(int on);
void fxjit_kind_off(int kind);  /* translate no instruction of this K_* kind (to find a bug) */

/* A debug aid: each run of translated code is run again by the interpreter from the same
 * state and the results compared (@on 1: the registers, 2: and the RAM - slow); a difference
 * is printed, the interpreter's result stays */
struct fxjit_verify_stats {
    uint32_t checked, bad;
};
void fxjit_verify(int on, int bank);    /* bank: the program bank checked, -1: all */
const struct fxjit_verify_stats *fxjit_verify_stats(void);
const struct fxjit_stats *fxjit_stats(void);

#ifdef __cplusplus
}

/* fx_run(): runs translated code from the GSU's state if it can; 0: the interpreter steps */
int S9xFxJitRun(uint32_t budget);
#endif

#endif
