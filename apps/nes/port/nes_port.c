/*
 * nes_port.c - what the NES emulator core (core/, MIT licence, (c) 2023 Emmanuel Obara)
 * expects from its platform, on CRTOS: the helpers of its utils.c and controller.c without
 * SDL, the few SDL calls left in the core (see SDL.h here) and a way back from its fatal
 * errors.
 *
 * The core ends the program on errors (quit()). Here a load runs under nes_guard(): quit()
 * returns there instead, and the program shows the message that was logged last.
 */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apu.h"
#include "controller.h"
#include "nes_port.h"
#include "utils.h"

/* ---- errors ---------------------------------------------------------------------------------- */

static jmp_buf *s_guard;
static char s_last_error[96];

void nes_set_guard(jmp_buf *guard)
{
    s_guard = guard;
}

const char *nes_last_error(void)
{
    return s_last_error;
}

void nes_clear_error(void)
{
    s_last_error[0] = 0;
}

void quit(int code)
{
    if (s_guard)
        longjmp(*s_guard, code ? code : 1);
    exit(code);
}

void LOG(enum LogLevel level, const char *fmt, ...)
{
    va_list ap;
    if (level == ERROR)
    {
        va_start(ap, fmt);
        vsnprintf(s_last_error, sizeof(s_last_error), fmt, ap);
        va_end(ap);
    }
    if (level < LOGLEVEL || level == INFO)
        return; /* INFO: every load prints a few lines - not on the system log */
    va_start(ap, fmt);
    printf("nes: ");
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

/* ---- utils.c --------------------------------------------------------------------------------- */

size_t file_size(FILE *file)
{
    fseek(file, 0, SEEK_END);
    size_t size = (size_t)ftell(file);
    rewind(file);
    return size;
}

/* The palette the PPU writes into its picture: RGB565 in the low 16 bits (the window's
 * format), whatever format the core asks for */
void to_pixel_format(const uint32_t *restrict in, uint32_t *restrict out, size_t size, ColorFormat format)
{
    (void)format;
    for (size_t i = 0; i < size; i++)
    {
        uint32_t c = in[i]; /* ARGB8888 */
        out[i] = ((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu);
    }
}

char *get_file_name(char *path)
{
    char *p = path + strlen(path);
    for (; p > path; p--)
        if (*p == '\\' || *p == '/')
            return p + 1;
    return p;
}

uint64_t next_power_of_2(uint64_t num)
{
    uint64_t power = 1;
    while (power < num)
        power *= 2;
    return power;
}

void SDL_PauseAudio(SDL_AudioStream *stream, int flag)
{
    (void)stream;
    (void)flag;
}

/* ---- controller.c ---------------------------------------------------------------------------- */

void init_joypad(struct JoyPad *pad, uint8_t player)
{
    pad->status = 0;
    pad->reg = 0;
    pad->player = player;
}

uint8_t read_joypad(struct JoyPad *pad)
{
    uint8_t val = pad->reg & 1;
    pad->reg >>= 1;
    pad->reg |= 0x80; /* refill bit 7 with 1 */
    return val;
}

void turbo_trigger(struct JoyPad *pad)
{
    /* toggle A and B while turbo A and turbo B are held */
    pad->status ^= pad->status >> 8;
}

/* ---- SDL: files ------------------------------------------------------------------------------ */

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode)
{
    return (SDL_IOStream *)fopen(file, mode);
}

long long SDL_SeekIO(SDL_IOStream *s, long long offset, int whence)
{
    FILE *f = (FILE *)s;
    if (fseek(f, (long)offset, whence == SDL_IO_SEEK_END ? SEEK_END : whence == SDL_IO_SEEK_CUR ? SEEK_CUR : SEEK_SET))
        return -1;
    return ftell(f);
}

size_t SDL_ReadIO(SDL_IOStream *s, void *ptr, size_t size)
{
    FILE *f = (FILE *)s;
    size_t n = fread(ptr, 1, size, f);
    fclose(f); /* the core reads a file once, whole, and never closes it */
    return n;
}

int SDL_CloseIO(SDL_IOStream *s)
{
    return fclose((FILE *)s);
}

const char *SDL_GetError(void)
{
    return "not available";
}

/* ---- SDL: audio ------------------------------------------------------------------------------ */

static void (*s_audio_sink)(const int16_t *samples, int count);
static uint64_t s_samples;
int g_nes_audio_on; /* the APU mixes samples only while set (core/apu.c) */

void nes_audio_set_sink(void (*sink)(const int16_t *samples, int count))
{
    s_audio_sink = sink;
    g_nes_audio_on = sink != NULL;
}

uint64_t nes_audio_samples(void)
{
    return s_samples;
}

SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID dev, const SDL_AudioSpec *spec, void *callback,
                                           void *userdata)
{
    (void)dev;
    (void)spec;
    (void)callback;
    (void)userdata;
    static int stream;
    return (SDL_AudioStream *)&stream;
}

/* the APU keeps the queue of the audio device near its nominal size by tuning its sampling
 * rate; without a device it is always just right */
int SDL_GetAudioStreamQueued(SDL_AudioStream *stream)
{
    (void)stream;
    return NOMINAL_QUEUE_SIZE;
}

int SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len)
{
    (void)stream;
    s_samples += (uint64_t)(len / 2);
    if (s_audio_sink)
        s_audio_sink((const int16_t *)buf, len / 2);
    return 0;
}
