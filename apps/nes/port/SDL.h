/*
 * SDL.h - the little of SDL 3 the NES emulator core (core/) refers to, for CRTOS.
 *
 * The core is compiled unchanged; its SDL front end (window, renderer, SDL events, audio
 * device) is replaced by the CRTOS program (apps/nes). What stays in the core files are
 * a few types in headers, reading the ROM file (SDL_IO*, here on the C library) and the
 * audio stream the APU writes its samples to (nes_port.c: counted and dropped for now -
 * the system has no sound output yet).
 */
#ifndef NES_SDL_SHIM_H
#define NES_SDL_SHIM_H

/* like SDL_stdinc.h: the core relies on these coming with SDL.h */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;
typedef struct SDL_AudioStream SDL_AudioStream;
typedef uint32_t SDL_AudioDeviceID;

typedef struct SDL_FRect {
    float x, y, w, h;
} SDL_FRect;

/* only for prototypes of the core's own (unused) SDL front end */
typedef union SDL_Event {
    uint32_t type;
} SDL_Event;

/* files */
typedef struct SDL_IOStream SDL_IOStream;
#define SDL_IO_SEEK_SET 0
#define SDL_IO_SEEK_CUR 1
#define SDL_IO_SEEK_END 2
SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode);
long long SDL_SeekIO(SDL_IOStream *s, long long offset, int whence);
size_t SDL_ReadIO(SDL_IOStream *s, void *ptr, size_t size);
int SDL_CloseIO(SDL_IOStream *s);

/* audio */
#define SDL_AUDIO_S16 0x8010u
#define SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK ((SDL_AudioDeviceID)0xFFFFFFFFu)
typedef struct SDL_AudioSpec {
    uint32_t format;
    int channels;
    int freq;
} SDL_AudioSpec;
SDL_AudioStream *SDL_OpenAudioDeviceStream(SDL_AudioDeviceID dev, const SDL_AudioSpec *spec, void *callback,
                                           void *userdata);
int SDL_GetAudioStreamQueued(SDL_AudioStream *stream);
int SDL_PutAudioStreamData(SDL_AudioStream *stream, const void *buf, int len);

const char *SDL_GetError(void);

#ifdef __cplusplus
}
#endif

#endif
