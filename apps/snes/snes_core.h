/*
 * snes_core.h - the Snes9x core (core/) for the C side of the SNES program (snes_core.cpp):
 * a game session, its frames, picture, sound, controllers and battery RAM.
 */
#ifndef SNES_CORE_H
#define SNES_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SNES_VAR_DIR "/sd/crtos/var/snes"
#define SNES_W 256      /* the picture on the window */
#define SNES_H 224

/* the buttons of a joypad, as the console reads them */
#define SNES_B       0x8000u
#define SNES_Y       0x4000u
#define SNES_SELECT  0x2000u
#define SNES_START   0x1000u
#define SNES_UP      0x0800u
#define SNES_DOWN    0x0400u
#define SNES_LEFT    0x0200u
#define SNES_RIGHT   0x0100u
#define SNES_A       0x0080u
#define SNES_X       0x0040u
#define SNES_L       0x0020u
#define SNES_R       0x0010u

struct snes_rom_info {
    char title[24];         /* from the cartridge header */
    const char *map;        /* "LOROM", "HIROM", "EXHIROM" */
    unsigned rom_kb;        /* of the file (without a copier header) */
    unsigned sram_kb;       /* battery RAM, 0: none */
    bool pal;
    const char *chip;       /* a coprocessor this program does not have, NULL: none */
};

/* The cartridge header of @path; NULL if the game can be played, else why not (upper case,
 * for the screen) */
const char *snes_rom_info(const char *path, struct snes_rom_info *info);

/* The core's memory, sound and graphics, once; false without enough memory */
bool snes_init(void);
/* Loads @path; false with a message in *err */
bool snes_open(const char *path, char *err, int errsize);
void snes_close(void);
void snes_reset(void);

/* Runs one frame; @draw false: the picture is not drawn (skipped) */
void snes_frame(bool draw);
/* The next drawn frame's lines @first to @first + @lines - 1 go to @dst too (rows @pitch
 * pixels apart), as they are drawn, if the picture is @height lines of 256 (as the last one
 * mostly is); NULL: not. After the frame: true if the whole part came there */
void snes_output(uint16_t *dst, int pitch, int first, int lines, int height);
bool snes_output_done(void);
/* The last picture drawn: RGB565, *width 256 or 512 (hi-res), *height 224 or 239 (doubled
 * with interlace), *pitch in pixels */
const uint16_t *snes_screen(int *width, int *height, int *pitch);
bool snes_is_pal(void);
uint32_t snes_frame_rate_mhz(void);     /* frames per 1000 s */
uint32_t snes_cpu_pc(void);             /* the 65816's program bank and counter (a test's trace) */

/* Up to @max stereo sample pairs (32 kHz) made since the last call into @buf; returns how
 * many */
int snes_sound_take(int16_t *buf, int max);
/* Sound rate control: @queued of @target samples wait in the device (a little faster or
 * slower, at most 0.5 %) */
void snes_sound_pace(int queued, int target);

/* The buttons held on joypad @port (0, 1): SNES_* */
void snes_input_set(int port, uint16_t buttons);

/* The battery RAM of the game (NULL, 0: none) */
uint8_t *snes_sram(size_t *size);

extern bool g_snes_verbose;     /* the core's messages to the console */

#ifdef __cplusplus
}
#endif

#endif
