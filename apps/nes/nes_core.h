/*
 * nes_core.h - one game session of the NES emulator core (core/) for the CRTOS program:
 * load a ROM, run a frame, reset, close. Replaces the core's emulator.c, whose main loop
 * belongs to SDL.
 */
#ifndef NES_CORE_H
#define NES_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "emulator.h"

#define NES_W 256
#define NES_H 240

/* What the header of a ROM file says */
struct nes_rom_info {
    int mapper;
    const char *mapper_name;    /* "" if unknown */
    int prg_kb;                 /* program ROM */
    int chr_kb;                 /* picture ROM; 0: the cartridge has picture RAM */
    bool pal;                   /* a 50 Hz (European) game */
    bool battery;               /* keeps its save RAM (a .sav file) */
    bool nes2;                  /* NES 2.0 header */
    long size;                  /* of the file */
};

/* Reads the header of @path into *info. Returns NULL if this program can run the game, else
 * why not (upper case, for the screen): not a ROM, unsupported mapper, truncated file... */
const char *nes_rom_info(const char *path, struct nes_rom_info *info);

/* Loads @path (an iNES / NES 2.0 file); false with a message in *err on failure */
bool nes_open(Emulator *e, const char *path, char *err, int errsize);
/* Runs the emulation until the next picture is complete (nes_screen() in nes_ppu.h) */
void nes_frame(Emulator *e);
void nes_reset(Emulator *e);
void nes_close(Emulator *e);

/* translate the 6502 code (default) or 0: interpret it (set before nes_open) */
extern int g_nes_jit;

/* the frame rate of the console (60.1 NTSC, 50.0 PAL), in frames per 1000 s */
uint32_t nes_frame_rate_mhz(const Emulator *e);

#endif
