/*
 * nes_core.c - the emulator core (core/, MIT licence, (c) 2023 Emmanuel Obara) as a
 * session: what the core's emulator.c does, without its SDL window, events and timers.
 * reset_emulator() is the core's own.
 *
 * The clock is lazy: a CPU cycle only counts (tick_master_clock). The PPU and the APU run
 * behind the CPU and catch up (nes_clock_sync) when the CPU accesses their registers or a
 * mapper's (the memory map, mmu.c, asks for it), and at checkpoints: the moments at which
 * they could interrupt the CPU or the frame is complete (the vertical blank, mapper IRQs,
 * APU frame IRQ, DMC fetches). What the CPU sees is the same as with both clocked every
 * cycle.
 */
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <crtos.h>

#include "controller.h"
#include "emulator.h"
#include "nes_core.h"
#include "nes_port.h"
#include "nes_ppu.h"
#include "nes_jit.h"
#include "nes_sound.h"
#include "nsf.h"

static uint16_t s_turbo_skip = NTSC_FRAME_RATE / NTSC_TURBO_RATE;
static uint8_t s_pal;

int g_nes_jit = 1; /* translate the 6502 code (nes_jit.c); 0: interpret it */

/* ---- the lazy clock --------------------------------------------------------------------------- */

uint32_t g_clk_event;       /* CPU cycle of the next checkpoint */
static uint32_t s_ppu_cyc;  /* the PPU has run up to this CPU cycle */
static uint32_t s_pal_frac; /* PAL: 3.2 dots per cycle, the fifths carried */

/* The PPU and the APU up to the current CPU cycle */
void nes_clock_sync(Emulator *e)
{
    uint32_t now = (uint32_t)e->cpu.t_cycles;
    uint32_t n = now - s_ppu_cyc;
    if ((int32_t)n > 0) /* (never backwards: a cycle count set back would mean 2^32 cycles) */
    {
        uint32_t dots = n * 3u;
        s_ppu_cyc = now;
        if (s_pal)
        {
            s_pal_frac += n;
            dots += s_pal_frac / 5u;
            s_pal_frac %= 5u;
        }
        ppu_run(&e->ppu, (int)dots);
    }
    if ((int32_t)(now - g_apu_now) > 0)
        apu_run(&e->apu, now - g_apu_now);
}

/* The fewest CPU cycles in which the PPU runs @dots dots from now: NTSC 3 a cycle; PAL 3.2,
 * n cycles 3n + (s_pal_frac + n) / 5 dots - exactly, so that a checkpoint is the same cycle
 * whenever it was planned */
static uint32_t cycles_for_dots(uint32_t dots)
{
    if (!s_pal)
        return (dots + 2u) / 3u;
    uint32_t n = dots * 5u / 16u;
    while (3u * n + (s_pal_frac + n) / 5u < dots)
        n++;
    while (n && 3u * (n - 1u) + (s_pal_frac + n - 1u) / 5u >= dots)
        n--;
    return n;
}

/* The next checkpoint, after a sync or a register write that may move it */
void nes_clock_plan(Emulator *e)
{
    uint32_t dots = ppu_dots_to_checkpoint(&e->ppu);
    uint32_t cycles = cycles_for_dots(dots);
    uint32_t apu = apu_cycles_to_event(&e->apu);
    if (apu < cycles)
        cycles = apu;
    g_clk_event = (uint32_t)e->cpu.t_cycles + (cycles ? cycles : 1u);
}

static void clock_start(Emulator *e)
{
    s_ppu_cyc = g_apu_now = (uint32_t)e->cpu.t_cycles;
    s_pal_frac = 0;
    nes_clock_plan(e);
}

/* Every CPU cycle (the core calls it for every bus access) */
void tick_master_clock(Emulator *emulator)
{
    emulator->cpu.t_cycles++;
    if ((int32_t)((uint32_t)emulator->cpu.t_cycles - g_clk_event) >= 0)
    {
        nes_clock_sync(emulator);
        nes_clock_plan(emulator);
    }
}

void reset_emulator(Emulator *emulator)
{
    reset_cpu(&emulator->cpu);
    reset_APU(&emulator->apu);
    reset_ppu(&emulator->ppu);
    if (emulator->mapper.reset != NULL)
        emulator->mapper.reset(&emulator->mapper);
}

/* ---- ROM headers ---------------------------------------------------------------------------- */

struct mapper_name {
    uint16_t number;
    bool supported;             /* by the core (core/mappers/mapper.c) */
    const char *name;
};

static const struct mapper_name MAPPERS[] = {
    {0, true, "NROM"},
    {1, true, "MMC1"},
    {2, true, "UXROM"},
    {3, true, "CNROM"},
    {4, true, "MMC3"},
    {5, true, "MMC5"},
    {7, true, "AXROM"},
    {9, false, "MMC2"},
    {10, false, "MMC4"},
    {11, true, "COLOR DREAMS"},
    {13, true, "CPROM"},
    {19, false, "NAMCO 163"},
    {21, false, "VRC4"},
    {22, false, "VRC2"},
    {23, false, "VRC2/4"},
    {24, false, "VRC6"},
    {25, false, "VRC4"},
    {26, false, "VRC6"},
    {34, false, "BNROM"},
    {46, true, "RUMBLE STATION"},
    {66, true, "GXROM"},
    {69, false, "FME-7"},
    {71, false, "CAMERICA"},
    {75, true, "VRC1"},
    {85, false, "VRC7"},
    {94, true, "UN1ROM"},
    {180, true, "UNROM 180"},
    {185, true, "CNROM+"},
};

const char *nes_rom_info(const char *path, struct nes_rom_info *info)
{
    uint8_t h[16];
    memset(info, 0, sizeof(*info));
    info->mapper_name = "";
    FILE *f = fopen(path, "rb");
    if (!f)
        return "CANNOT OPEN THE FILE";
    size_t got = fread(h, 1, sizeof(h), f);
    fseek(f, 0, SEEK_END);
    info->size = ftell(f);
    fclose(f);
    if (got >= 5 && !memcmp(h, "NESM\x1a", 5))
        return "NSF MUSIC - NO SOUND OUTPUT YET";
    if (got >= 4 && !memcmp(h, "NSFE", 4))
        return "NSF MUSIC - NO SOUND OUTPUT YET";
    if (got < sizeof(h) || memcmp(h, "NES\x1a", 4))
        return "NOT AN NES ROM";

    /* the formats as the core tells them apart (load_data() in mapper.c) */
    bool zeros = !h[12] && !h[13] && !h[14] && !h[15];
    bool ines = (h[7] & 0x0c) == 0 && zeros;
    info->nes2 = (h[7] & 0x0c) == 0x08;
    info->mapper = h[6] >> 4;
    int prg = h[4], chr = h[5];
    if (ines || info->nes2)
        info->mapper |= h[7] & 0xf0;
    if (info->nes2)
    {
        info->mapper |= (h[8] & 0x0f) << 8;
        prg |= (h[9] & 0x0f) << 8;
        chr |= (h[9] & 0xf0) << 4;
        info->pal = (h[12] & 3) == 1;
        if ((h[12] & 3) == 3)
            return "DENDY GAMES ARE NOT SUPPORTED";
    }
    else
    {
        info->pal = ines ? (h[9] & 1) : false;
        if (strstr(path, "(E)") || strstr(path, "(Europe)"))
            info->pal = true;
    }
    info->battery = (h[6] & 0x02) != 0;
    info->prg_kb = prg * 16;
    info->chr_kb = chr * 8;

    const struct mapper_name *m = NULL;
    for (size_t i = 0; i < sizeof(MAPPERS) / sizeof(MAPPERS[0]); i++)
        if (MAPPERS[i].number == info->mapper)
            m = &MAPPERS[i];
    if (m)
        info->mapper_name = m->name;
    if (!m || !m->supported)
        return "THIS MAPPER IS NOT SUPPORTED";
    if (h[6] & 0x04)
        return "ROMS WITH A TRAINER ARE NOT SUPPORTED";
    if (!prg)
        return "THE ROM HAS NO PROGRAM";
    /* the core copies what the header promises: the file must hold it */
    if (info->size < 16 + (long)prg * 16384 + (long)chr * 8192)
        return "THE FILE IS TRUNCATED";
    return NULL;
}

/* ---- a game session ------------------------------------------------------------------------- */

bool nes_open(Emulator *e, const char *path, char *err, int errsize)
{
    static char name[256];
    struct nes_rom_info info;
    jmp_buf guard;
    memset(e, 0, sizeof(*e));
    const char *problem = nes_rom_info(path, &info);
    if (problem)
    {
        snprintf(err, (size_t)errsize, "%s", problem);
        return false;
    }
    nes_clear_error();
    snprintf(name, sizeof(name), "%s", path);
    if (setjmp(guard))
    {
        /* the core gave up (and would have ended the program) */
        nes_set_guard(NULL);
        free_mapper(&e->mapper);
        snprintf(err, (size_t)errsize, "%s", nes_last_error()[0] ? nes_last_error() : "cannot load the file");
        return false;
    }
    nes_set_guard(&guard);
    load_file(name, NULL, &e->mapper);
    nes_set_guard(NULL);
    if (e->mapper.is_nsf)
    {
        free_mapper(&e->mapper);
        snprintf(err, (size_t)errsize, "NSF MUSIC - NO SOUND OUTPUT YET");
        return false;
    }
    /* the core knows European games by "(E)" in the name; No-Intro names say "(Europe)" */
    if (info.pal)
        e->mapper.type = PAL;
    e->type = e->mapper.type;
    s_pal = e->type == PAL;
    e->mapper.emulator = e;
    s_turbo_skip = e->type == PAL ? PAL_FRAME_RATE / PAL_TURBO_RATE : NTSC_FRAME_RATE / NTSC_TURBO_RATE;
    e->g_ctx.width = NES_W;
    e->g_ctx.height = NES_H;
    e->g_ctx.scale = 1;
    init_mem(e);
    init_ppu(e);
    /* the mapper's power-on state (MMC5 puts its ExRAM into the PPU's memory), before the CPU
     * fetches its reset vector through it */
    if (e->mapper.reset)
        e->mapper.reset(&e->mapper);
    init_cpu(e);
    init_APU(e);
    clock_start(e);
    if (!g_nes_jit || jit_init(e))
        jit_enable(0); /* the interpreter alone */
    e->ppu.enabled = 1;
    return true;
}

void nes_frame(Emulator *e)
{
    if (e->ppu.frames % s_turbo_skip == 0)
    {
        turbo_trigger(&e->mem.joy1);
        turbo_trigger(&e->mem.joy2);
    }
    /* the CPU until the frame is complete: translated code, the interpreter where needed
     * (nes_jit.c; it also lets the graphics server in about every millisecond) */
    jit_frame(e);
    e->ppu.render = 0;
    nes_sound_sync(); /* the samples of the frame (nes_sound.c, when on) */
}

void nes_reset(Emulator *e)
{
    nes_clock_sync(e);
    reset_emulator(e);
    clock_start(e);
}

void nes_close(Emulator *e)
{
    exit_APU();
    exit_ppu(&e->ppu);
    free_mapper(&e->mapper);
}

uint32_t nes_frame_rate_mhz(const Emulator *e)
{
    return e->type == PAL ? 50007u : 60099u;
}
