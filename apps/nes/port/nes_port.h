/*
 * nes_port.h - the CRTOS side of the NES emulator core's platform (nes_port.c).
 */
#ifndef NES_PORT_H
#define NES_PORT_H

#include <setjmp.h>
#include <stdint.h>

/* While set, the core's fatal errors (quit()) jump here instead of ending the program */
void nes_set_guard(jmp_buf *guard);
/* the last error the core logged ("" if none) */
const char *nes_last_error(void);
void nes_clear_error(void);

/* where the APU's samples go (none: dropped) */
void nes_audio_set_sink(void (*sink)(const int16_t *samples, int count));
uint64_t nes_audio_samples(void);

#endif
