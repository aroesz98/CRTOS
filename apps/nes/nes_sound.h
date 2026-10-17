/*
 * nes_sound.h - the sound of the NES: a synthesizer on the state of the core's APU
 * (nes_sound.c).
 */
#ifndef NES_SOUND_H
#define NES_SOUND_H

#include <stdint.h>

struct APU;

/* Starts making samples from the APU's channels (off: nothing is made, no cost) */
void nes_sound_start(struct APU *apu, uint32_t rate);
void nes_sound_stop(void);
/* CPU cycles per output sample, in 1/65536: sets the pitch and the number of samples */
void nes_sound_set_step(uint32_t cycles_q16);
/* the samples made so far (at most @max), then forgets them */
int nes_sound_take(int16_t *out, int max);
/* makes the samples up to the current CPU cycle (the core calls it before changes) */
void nes_sound_sync(void);

#endif
