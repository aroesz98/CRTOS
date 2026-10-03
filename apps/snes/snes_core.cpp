/*
 * snes_core.cpp - the Snes9x core (core/) in the SNES program (snes_core.h): its settings, the
 * calls every port of Snes9x provides (S9x*: the finished picture, the sound device, file
 * names, messages), the cartridge header for the browser, loading a game with the check for
 * the coprocessors this program leaves out (all but DSP-1 and the Super FX: snes_stubs.cpp),
 * frames, sound and battery RAM.
 */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "snes9x.h"
#include "memmap.h"
#include "apu/apu.h"
#include "controls.h"
#include "display.h"
#include "core/gfx.h"     /* (not libgfx's gfx.h, which comes first in the path) */
#include "ppu.h"
#include "snes_core.h"
#include "snes_idle.h"
#include "snes_fxjit.h"

bool g_snes_verbose;

static bool s_ready;            /* snes_init() done */
static bool s_loaded;
static int s_width = SNES_WIDTH, s_height = SNES_HEIGHT;   /* of the last picture */

/* ---- the port: what the core calls ----------------------------------------------------------- */

void S9xMessage(int type, int, const char *message)
{
    if (g_snes_verbose || type >= S9X_ERROR)
        printf("snes: %s\n", message);
}

bool8 S9xInitUpdate(void)
{
    return TRUE;
}

/* the picture is complete (GFX.Screen): the program shows it after the frame */
bool8 S9xDeinitUpdate(int width, int height)
{
    s_width = width;
    s_height = height;
    return TRUE;
}

bool8 S9xContinueUpdate(int width, int height)
{
    return S9xDeinitUpdate(width, height);
}

void S9xSyncSpeed(void)
{
    /* (the program paces the frames) */
}

bool8 S9xOpenSoundDevice(void)
{
    return TRUE; /* (the program writes the samples: snes_sound_take) */
}

void S9xAutoSaveSRAM(void)
{
    /* (the program saves the battery RAM when it changed) */
}

const char *S9xGetDirectory(enum s9x_getdirtype)
{
    return SNES_VAR_DIR;
}

/* SNES_VAR_DIR/<the ROM's name without its extension><ext>, in a buffer that lives until the
 * next call */
static const char *var_file(const char *rom, const char *ext)
{
    static char path[S9X_PATH_MAX];
    if (!rom)
        rom = "";
    const char *slash = strrchr(rom, '/');
    const char *name = slash ? slash + 1 : rom;
    const char *dot = strrchr(name, '.');
    int len = dot ? (int)(dot - name) : (int)strlen(name);
    snprintf(path, sizeof(path), "%s/%.*s%s", SNES_VAR_DIR, len, name, ext);
    return path;
}

const char *S9xGetFilename(const char *ext, enum s9x_getdirtype)
{
    return var_file(Memory.ROMFilename, ext);
}

const char *S9xGetFilename(const char *filename, const char *ext, enum s9x_getdirtype)
{
    return var_file(filename, ext);
}

const char *S9xGetFilenameInc(const char *, enum s9x_getdirtype)
{
    return "";
}

/* ---- the cartridge header -------------------------------------------------------------------- */

/* how much a header at @h looks like the real one of a @map cartridge */
static int header_score(const uint8 *h, int map)
{
    int score = 0;
    uint16 sum = h[0x1e] | (h[0x1f] << 8), complement = h[0x1c] | (h[0x1d] << 8);
    if ((uint16)(sum + complement) == 0xffff)
        score += 4;
    if ((h[0x15] & 0x0f) == map)
        score += 3;
    uint16 reset = h[0x3c] | (h[0x3d] << 8);
    if (reset >= 0x8000)
        score += 2;
    int printable = 0;
    for (int i = 0; i < 21; i++)
        printable += h[i] >= 0x20 && h[i] < 0x7f;
    if (printable == 21)
        score += 2;
    if (h[0x17] >= 7 && h[0x17] <= 13)
        score += 1; /* a sensible ROM size */
    return score;
}

/* the coprocessor a header asks for that this program cannot run (chip byte, and the byte
 * before the header for custom chips), NULL: none or one it runs (DSP-1, Super FX) */
static const char *header_chip(const uint8 *h, uint8 subtype)
{
    uint8 chip = h[0x16], speed = h[0x15];
    if ((chip & 0x0f) < 3)
        return NULL; /* ROM, RAM, battery */
    switch (chip >> 4)
    {
    case 0x0:
        /* which DSP, the way CMemory::InitROM tells */
        if (chip == 0x03 && speed == 0x30)
            return "DSP-4";
        if (chip == 0x05 && speed == 0x20)
            return "DSP-2";
        if (chip == 0x05 && speed == 0x30 && h[0x1a] == 0xb2)
            return "DSP-3";
        return NULL; /* DSP-1 */
    case 0x1: return NULL; /* Super FX */
    case 0x2: return "OBC1";
    case 0x3: return "SA-1";
    case 0x4: return "S-DD1";
    case 0x5: return "S-RTC";
    case 0xe: return "SUPER GAME BOY";
    case 0xf:
        switch (subtype)
        {
        case 0x00: return "SPC7110";
        case 0x01: return "ST010";
        case 0x02: return "ST018";
        case 0x10: return "CX4";
        default: return "CUSTOM";
        }
    default: return "UNKNOWN";
    }
}

const char *snes_rom_info(const char *path, struct snes_rom_info *info)
{
    memset(info, 0, sizeof(*info));
    info->map = "";
    FILE *f = fopen(path, "rb");
    if (!f)
        return "CANNOT OPEN THE FILE";
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long skip = (size & 0x3ff) == 0x200 ? 0x200 : 0; /* a copier header */
    static const struct {
        long offset;
        int map;
        const char *name;
    } TRY[] = {{0x7fc0, 0, "LOROM"}, {0xffc0, 1, "HIROM"}, {0x40ffc0, 5, "EXHIROM"}};
    uint8 best[64], h[64], subtype = 0;
    int best_score = -1, best_i = 0;
    for (int i = 0; i < 3; i++)
    {
        if (skip + TRY[i].offset + 64 > size)
            continue;
        fseek(f, skip + TRY[i].offset - 1, SEEK_SET);
        uint8 before = 0;
        if (fread(&before, 1, 1, f) != 1 || fread(h, 1, 64, f) != 64)
            continue;
        int score = header_score(h, TRY[i].map);
        if (score > best_score)
        {
            best_score = score;
            best_i = i;
            subtype = before;
            memcpy(best, h, 64);
        }
    }
    fclose(f);
    if (best_score < 0)
        return "NOT A SNES ROM (TOO SMALL)";
    int n = 21;
    while (n > 0 && (best[n - 1] == ' ' || best[n - 1] < 0x20 || best[n - 1] >= 0x7f))
        n--;
    for (int i = 0; i < n; i++)
        info->title[i] = best[i] >= 0x20 && best[i] < 0x7f ? (char)best[i] : '?';
    info->map = TRY[best_i].name;
    info->rom_kb = (unsigned)((size - skip) / 1024);
    info->sram_kb = best[0x18] && best[0x18] <= 8 ? 1u << best[0x18] : 0;
    info->pal = best[0x19] >= 2 && best[0x19] <= 12;
    info->chip = header_chip(best, subtype);
    if (best_score < 4)
        return "NOT A SNES ROM (NO HEADER FOUND)";
    if (size - skip > (long)CMemory::MAX_ROM_SIZE)
        return "TOO BIG (MORE THAN 6 MB)";
    if (info->chip)
    {
        static char why[48];
        snprintf(why, sizeof(why), "NEEDS THE %s CHIP", info->chip);
        return why;
    }
    return NULL;
}

/* ---- the session ----------------------------------------------------------------------------- */

static void settings(void)
{
    memset(&Settings, 0, sizeof(Settings));
    Settings.FrameTimePAL = 20000;
    Settings.FrameTimeNTSC = 16667;
    Settings.SixteenBitSound = TRUE;
    Settings.Stereo = TRUE;
    Settings.SoundPlaybackRate = 32000;
    Settings.SoundInputRate = 32040;
    Settings.DynamicRateControl = TRUE;
    Settings.DynamicRateLimit = 5; /* 0.5 % */
    Settings.InterpolationMethod = DSP_INTERPOLATION_GAUSSIAN;
    Settings.Transparency = TRUE;
    Settings.HDMATimingHack = 100;
    Settings.BlockInvalidVRAMAccessMaster = TRUE;
    Settings.OneClockCycle = 6;
    Settings.OneSlowClockCycle = 8;
    Settings.TwoClockCycles = 12;
    Settings.MaxSpriteTilesPerLine = 34;
    Settings.SuperFXClockMultiplier = 100;
    Settings.AutoSaveDelay = 1;
    Settings.DontSaveOopsSnapshot = TRUE;
    Settings.NoPatch = TRUE;
}

bool snes_init(void)
{
    if (s_ready)
        return true;
    settings();
    CPU.Flags = 0;
    if (!Memory.Init() || !S9xInitAPU())
    {
        Memory.Deinit();
        S9xDeinitAPU();
        return false;
    }
    S9xInitSound(32);
    S9xSetSoundMute(FALSE);
    if (!S9xGraphicsInit())
        return false;
    snes_idle_enable(1);
    /* (the controller ports: two joypads, snes_input.cpp) */
    s_ready = true;
    return true;
}

/* the coprocessor a loaded game turned on in the core, NULL: none */
static const char *needs_chip(void)
{
    if (Settings.SA1)
        return "SA-1";
    if (Settings.DSP > 1)
        return Settings.DSP == 2 ? "DSP-2" : Settings.DSP == 3 ? "DSP-3" : "DSP-4";
    if (Settings.C4)
        return "CX4";
    if (Settings.SDD1)
        return "S-DD1";
    if (Settings.SPC7110)
        return "SPC7110";
    if (Settings.OBC1)
        return "OBC1";
    if (Settings.SETA)
        return "SETA DSP";
    if (Settings.SRTC)
        return "S-RTC";
    if (Settings.BS || Settings.BSXItself)
        return "SATELLAVIEW";
    return NULL;
}

bool snes_open(const char *path, char *err, int errsize)
{
    if (!snes_init())
    {
        snprintf(err, errsize, "NOT ENOUGH MEMORY");
        return false;
    }
    s_loaded = false;
    snes_idle_reset(); /* the wait loops found point into the last game's ROM */
    fxjit_reset();     /* and the Super FX translations */
    if (!Memory.LoadROM(path))
    {
        snprintf(err, errsize, "CANNOT LOAD THE GAME");
        return false;
    }
    if (Settings.SuperFX && Memory.CalculatedSize > 0x200000)
    {
        /* (a ROM hack: the Super FX mirrors in core/fxemu.h have room behind 2 MB only) */
        snprintf(err, errsize, "A SUPER FX GAME OVER 2 MB");
        return false;
    }
    if (Settings.SuperFX)
        fxjit_init(); /* (its memory once; without it the interpreter runs the Super FX) */
    const char *chip = needs_chip();
    if (chip)
    {
        snprintf(err, errsize, "NEEDS THE %s CHIP", chip);
        return false;
    }
    s_width = SNES_WIDTH;
    s_height = SNES_HEIGHT;
    memset(GFX.Screen, 0, GFX.Pitch * SNES_HEIGHT_EXTENDED);
    s_loaded = true;
    if (g_snes_verbose)
        printf("snes: %s\n", Memory.GetMultilineROMInfo());
    return true;
}

void snes_close(void)
{
    s_loaded = false;
    S9xClearSamples();
}

void snes_reset(void)
{
    if (s_loaded)
        S9xSoftReset();
}

void snes_output(uint16_t *dst, int pitch, int first, int lines, int height)
{
    GFXOut.dst = dst;
    GFXOut.pitch = (uint32)pitch;
    GFXOut.first = (uint32)first;
    GFXOut.lines = (uint32)lines;
    GFXOut.height = (uint32)height;
    GFXOut.ok = dst != NULL;
}

bool snes_output_done(void)
{
    return GFXOut.dst && GFXOut.ok && s_width == SNES_WIDTH && s_height == (int)GFXOut.height;
}

void snes_frame(bool draw)
{
    if (!s_loaded)
        return;
    if (!draw)
        GFXOut.dst = NULL;
    IPPU.RenderThisFrame = draw;
    S9xMainLoop();
    /* the APU catches up only every few lines (core/cpuexec.cpp): all the sound of the frame */
    S9xAPUEndScanline();
}

const uint16_t *snes_screen(int *width, int *height, int *pitch)
{
    *width = s_width;
    *height = s_height;
    *pitch = (int)GFX.RealPPL;
    return GFX.Screen;
}

bool snes_is_pal(void)
{
    return Settings.PAL;
}

uint32_t snes_cpu_pc(void)
{
    return Registers.PBPC & 0xffffff;
}

uint32_t snes_frame_rate_mhz(void)
{
    /* the master clock over the dots of a frame: 60.099 Hz (NTSC), 50.007 Hz (PAL) */
    return Settings.PAL ? 50007u : 60099u;
}

int snes_sound_take(int16_t *buf, int max)
{
    int n = S9xGetSampleCount() & ~1;
    if (n > max * 2)
        n = max * 2;
    if (n <= 0)
        return 0;
    S9xMixSamples((uint8 *)buf, n);
    return n / 2;
}

void snes_sound_pace(int queued, int target)
{
    /* the core's rate control: @avail of @size free - half free is the aim */
    int size = target * 2, avail = size - queued;
    if (avail < 0)
        avail = 0;
    S9xUpdateDynamicRate(avail, size);
}

uint8_t *snes_sram(size_t *size)
{
    size_t n = Memory.SRAMSize ? (size_t)(1u << (Memory.SRAMSize + 3)) * 128u : 0;
    if (n > Memory.SRAM_SIZE)
        n = Memory.SRAM_SIZE;
    *size = s_loaded ? n : 0;
    return s_loaded && n ? Memory.SRAM : NULL;
}
