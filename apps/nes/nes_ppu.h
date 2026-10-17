/*
 * nes_ppu.h - the clock of the fast PPU (nes_ppu.c): between two events of a scanline a PPU
 * dot is an addition and a compare.
 */
#ifndef NES_PPU_H
#define NES_PPU_H

#include "ppu.h"

extern int g_ppu_next;  /* dot of the next event on the current line */
extern int g_ppu_skip;  /* 1: the frame is not shown - draw only what the game can notice */

void ppu_events(PPU *ppu);
/* The lines of the picture (RGB565) from now on: line @first (0-239) at @pix, the next ones
 * @stride pixels further, @lines of them (the others are not drawn). @pix NULL: into the
 * PPU's own 256 x 240 picture (nes_screen), where they go after init_ppu(). */
void nes_set_output(PPU *ppu, uint16_t *pix, int stride, int first, int lines);
/* the PPU's own picture: 256 x 240 RGB565 pixels (the program's to use while the lines go
 * elsewhere) */
uint16_t *nes_screen(const PPU *ppu);
/* PPU dots until the next moment the CPU must see: an interrupt can come (vertical blank,
 * mapper IRQs) or the frame is complete */
uint32_t ppu_dots_to_checkpoint(PPU *ppu);

static inline void ppu_run(PPU *ppu, int dots)
{
    ppu->dots += (size_t)dots;
    if ((int)ppu->dots >= g_ppu_next)
        ppu_events(ppu);
}

#endif
