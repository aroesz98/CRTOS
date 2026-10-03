/*
 * CRTOS: MMC5 (mapper 5, ExROM boards: Castlevania III, Just Breed, Uncharted Waters, Gemfire,
 * L'Empereur, Metal Slader Glory, ...). Not in the core; written for CRTOS in its style of
 * mapper, MIT licence like the core.
 *
 * What it does:
 *   - PRG: modes 0-3 (32 / 16+16 / 16+8+8 / 8 KB windows at $8000), windows of RAM or ROM,
 *     8 KB RAM window at $6000, write protection ($5102 = 2 and $5103 = 1 allow writes).
 *     PRG RAM: 64 KB (the largest board), battery-backed if the header says so.
 *   - CHR: modes 0-3 (8 / 4 / 2 / 1 KB), the "A" set ($5120-$5127) and the "B" set
 *     ($5128-$512B): with 8x16 sprites A for sprites and B for the background, with 8x8
 *     sprites the set written last for everything; $5130 upper bank bits.
 *   - Nametables ($5105): each quarter CIRAM page 0 or 1, ExRAM, or the fill tile and colour
 *     ($5106, $5107). ExRAM and the fill page live in the upper half of the PPU's V_RAM
 *     (0x800, 0xC00), where the PPU's nametable fetches find them without help.
 *   - ExRAM ($5104): nametable, extended attributes (a 4 KB CHR bank and a palette for each
 *     background tile, through Mapper.ppu_bg_tile), RAM, read-only RAM.
 *   - Scanline IRQ ($5203, $5204 with the "in frame" flag), counted at the start of each
 *     visible line (Mapper.ppu_line); the multiplier ($5205, $5206).
 *
 * Not done: the vertical split ($5200-$5202) and the sound channels ($5000-$5015) - only
 * Famicom games use them (the NES does not mix cartridge sound).
 */
#include <emulator.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mapper.h"
#include "utils.h"
#include "cpu6502.h"

#define RAM_SIZE 0x10000u   /* PRG RAM: 8 banks of 8 KB */
#define EXRAM_OFF 0x800u    /* ExRAM in the PPU's V_RAM */
#define FILL_OFF 0xC00u     /* the fill-mode nametable in the PPU's V_RAM */

typedef struct {
    uint8_t *prg[4];        /* 8 KB windows at $8000, $A000, $C000, $E000 */
    uint8_t prg_ram[4];     /* the window is RAM (writable) */
    uint8_t *ram6;          /* the 8 KB at $6000 */
    uint8_t *chr_a[8];      /* 1 KB pages of the pattern tables: set A */
    uint8_t *chr_b[8];      /* set B */
    uint8_t **chr;          /* the set the next read_CHR uses */
    uint8_t prg_mode, chr_mode, ex_mode, nt_mode;
    uint8_t protect1, protect2;
    uint8_t prg_regs[5];    /* $5113-$5117 */
    uint16_t chr_regs[12];  /* $5120-$512B with the upper bits of $5130 */
    uint8_t chr_hi;         /* $5130 */
    uint8_t last_b;         /* the B set was written last */
    uint8_t fill_tile, fill_colour;
    uint8_t irq_target, irq_enabled, irq_pending, in_frame, counter;
    uint8_t mul_a, mul_b;
    uint32_t prg_8k;        /* PRG ROM in 8 KB banks */
    uint32_t chr_1k;        /* CHR in 1 KB banks */
} MMC5_t;

static uint8_t *exram(Mapper *mapper)
{
    return mapper->emulator->ppu.V_RAM + EXRAM_OFF;
}

/* ---- banks ------------------------------------------------------------------------------------- */

static void map_prg(Mapper *mapper, int window, uint8_t reg, int ram_possible)
{
    MMC5_t *m = mapper->extension;
    if (ram_possible && !(reg & 0x80)) {
        m->prg[window] = mapper->PRG_RAM + (reg & 7u) * 0x2000u;
        m->prg_ram[window] = 1;
    } else {
        m->prg[window] = mapper->PRG_ROM + ((reg & 0x7fu) % m->prg_8k) * 0x2000u;
        m->prg_ram[window] = 0;
    }
}

static void update_prg(Mapper *mapper)
{
    MMC5_t *m = mapper->extension;
    uint8_t *r = m->prg_regs + 1; /* $5114-$5117 */
    m->ram6 = mapper->PRG_RAM + (m->prg_regs[0] & 7u) * 0x2000u;
    switch (m->prg_mode) {
        case 0: /* 32 KB: $5117 */
            for (int i = 0; i < 4; i++)
                map_prg(mapper, i, (uint8_t)(0x80 | ((r[3] & 0x7c) + i)), 0);
            break;
        case 1: /* 16 KB: $5115, $5117 */
            map_prg(mapper, 0, (uint8_t)(r[1] & 0xfe), 1);
            map_prg(mapper, 1, (uint8_t)((r[1] & 0xfe) + 1), 1);
            map_prg(mapper, 2, (uint8_t)(0x80 | (r[3] & 0x7e)), 0);
            map_prg(mapper, 3, (uint8_t)(0x80 | ((r[3] & 0x7e) + 1)), 0);
            break;
        case 2: /* 16 KB $5115, 8 KB $5116, 8 KB $5117 */
            map_prg(mapper, 0, (uint8_t)(r[1] & 0xfe), 1);
            map_prg(mapper, 1, (uint8_t)((r[1] & 0xfe) + 1), 1);
            map_prg(mapper, 2, r[2], 1);
            map_prg(mapper, 3, (uint8_t)(0x80 | r[3]), 0);
            break;
        default: /* 8 KB each */
            map_prg(mapper, 0, r[0], 1);
            map_prg(mapper, 1, r[1], 1);
            map_prg(mapper, 2, r[2], 1);
            map_prg(mapper, 3, (uint8_t)(0x80 | r[3]), 0);
            break;
    }
}

static uint8_t *chr_page(Mapper *mapper, uint32_t bank_1k)
{
    MMC5_t *m = mapper->extension;
    return mapper->CHR_ROM + (bank_1k % m->chr_1k) * 0x400u;
}

static void update_chr(Mapper *mapper)
{
    MMC5_t *m = mapper->extension;
    const uint16_t *a = m->chr_regs, *b = m->chr_regs + 8;
    for (int i = 0; i < 8; i++) {
        uint32_t pa, pb;
        switch (m->chr_mode) {
            case 0: /* 8 KB */
                pa = a[7] * 8u + i;
                pb = b[3] * 8u + i;
                break;
            case 1: /* 4 KB */
                pa = a[i < 4 ? 3 : 7] * 4u + (i & 3);
                pb = b[3] * 4u + (i & 3);
                break;
            case 2: /* 2 KB */
                pa = a[(i & 6) + 1] * 2u + (i & 1);
                pb = b[(i & 2) + 1] * 2u + (i & 1);
                break;
            default: /* 1 KB */
                pa = a[i];
                pb = b[i & 3];
                break;
        }
        m->chr_a[i] = chr_page(mapper, pa);
        m->chr_b[i] = chr_page(mapper, pb);
    }
}

static void update_fill(Mapper *mapper)
{
    MMC5_t *m = mapper->extension;
    uint8_t *fill = mapper->emulator->ppu.V_RAM + FILL_OFF;
    memset(fill, m->fill_tile, 960);
    memset(fill + 960, (m->fill_colour & 3) * 0x55, 64);
}

static void update_nametables(Mapper *mapper)
{
    MMC5_t *m = mapper->extension;
    static const uint16_t OFF[4] = { 0x000, 0x400, EXRAM_OFF, FILL_OFF };
    for (int q = 0; q < 4; q++)
        mapper->name_table_map[q] = OFF[(m->nt_mode >> (2 * q)) & 3];
}

/* ---- the PPU's side ---------------------------------------------------------------------------- */

static void ppu_fetch(Mapper *mapper, int what)
{
    MMC5_t *m = mapper->extension;
    if (what != PPU_FETCH_CPU && (mapper->emulator->ppu.ctrl & LONG_SPRITE))
        m->chr = what == PPU_FETCH_BG ? m->chr_b : m->chr_a;
    else
        m->chr = m->last_b ? m->chr_b : m->chr_a;
}

static void ppu_line(Mapper *mapper, int line, int rendering)
{
    MMC5_t *m = mapper->extension;
    Emulator *e = mapper->emulator;
    if (line >= 240 || !rendering) {
        if (m->in_frame) {
            m->in_frame = 0;
            m->irq_pending = 0;
            interrupt_clear(&e->cpu, MAPPER_IRQ);
        }
        return;
    }
    if (!m->in_frame) {
        m->in_frame = 1;
        m->counter = 0;
        return;
    }
    m->counter++;
    if (m->counter == m->irq_target) {
        m->irq_pending = 1;
        if (m->irq_enabled)
            interrupt(&e->cpu, MAPPER_IRQ);
    }
}

/* ExRAM mode 1: each background tile's 4 KB CHR bank and palette from ExRAM */
static void ppu_bg_tile(Mapper *mapper, uint16_t v, uint8_t nt, uint16_t fine_y, uint8_t *lo, uint8_t *hi,
                        uint8_t *pal)
{
    MMC5_t *m = mapper->extension;
    uint8_t ex = exram(mapper)[v & 0x3ff];
    uint32_t bank_4k = (ex & 0x3fu) | ((uint32_t)(m->chr_hi & 3) << 6);
    uint32_t addr = ((bank_4k * 0x1000u) + (uint32_t)nt * 16u + fine_y) % (m->chr_1k * 0x400u);
    *lo = mapper->CHR_ROM[addr];
    *hi = mapper->CHR_ROM[(addr + 8u) % (m->chr_1k * 0x400u)];
    *pal = (uint8_t)((ex >> 6) << 2);
}

/* Mapper.prg_ptr: ROM windows only (code in RAM windows is not translated) */
static const uint8_t *prg_ptr(Mapper *mapper, uint16_t address)
{
    MMC5_t *m = mapper->extension;
    int w = (address - 0x8000) >> 13;
    return m->prg_ram[w] ? NULL : m->prg[w] + (address & 0x1fff);
}

static uint8_t read_CHR(Mapper *mapper, uint16_t address)
{
    MMC5_t *m = mapper->extension;
    return m->chr[address >> 10][address & 0x3ff];
}

/* Mapper.chr_ptr: in the set ppu_fetch chose, as read_CHR */
static const uint8_t *chr_ptr(Mapper *mapper, uint16_t address)
{
    MMC5_t *m = mapper->extension;
    return m->chr[address >> 10] + (address & 0x3ff);
}

static void write_CHR(Mapper *mapper, uint16_t address, uint8_t value)
{
    MMC5_t *m = mapper->extension;
    if (mapper->CHR_RAM_size)
        m->chr[address >> 10][address & 0x3ff] = value;
}

/* ---- the CPU's side ---------------------------------------------------------------------------- */

static int ram_writable(const MMC5_t *m)
{
    return m->protect1 == 2 && m->protect2 == 1;
}

static uint8_t read_ROM(Mapper *mapper, uint16_t address)
{
    MMC5_t *m = mapper->extension;
    if (address >= 0x8000)
        return m->prg[(address - 0x8000) >> 13][address & 0x1fff];
    if (address >= 0x6000)
        return m->ram6[address & 0x1fff];
    if (address >= 0x5c00)
        return m->ex_mode >= 2 ? exram(mapper)[address - 0x5c00] : mapper->emulator->mem.bus;
    switch (address) {
        case 0x5204: {
            uint8_t status = (uint8_t)((m->irq_pending ? 0x80 : 0) | (m->in_frame ? 0x40 : 0));
            m->irq_pending = 0;
            interrupt_clear(&mapper->emulator->cpu, MAPPER_IRQ);
            return status;
        }
        case 0x5205:
            return (uint8_t)(m->mul_a * m->mul_b);
        case 0x5206:
            return (uint8_t)((m->mul_a * m->mul_b) >> 8);
        case 0x5015:
            return 0; /* sound status: no channel plays */
        default:
            return mapper->emulator->mem.bus;
    }
}

static void write_ROM(Mapper *mapper, uint16_t address, uint8_t value)
{
    MMC5_t *m = mapper->extension;
    if (address >= 0x8000) {
        int w = (address - 0x8000) >> 13;
        if (m->prg_ram[w] && ram_writable(m))
            m->prg[w][address & 0x1fff] = value;
        return;
    }
    if (address >= 0x6000) {
        if (ram_writable(m))
            m->ram6[address & 0x1fff] = value;
        return;
    }
    if (address >= 0x5c00) {
        if (m->ex_mode != 3) /* read-only in mode 3 */
            exram(mapper)[address - 0x5c00] = value;
        return;
    }
    if (address >= 0x5120 && address <= 0x512b) {
        int i = address - 0x5120;
        m->chr_regs[i] = (uint16_t)(value | (m->chr_hi & 3) << 8);
        m->last_b = i >= 8;
        update_chr(mapper);
        ppu_fetch(mapper, PPU_FETCH_CPU);
        return;
    }
    if (address >= 0x5113 && address <= 0x5117) {
        m->prg_regs[address - 0x5113] = value;
        update_prg(mapper);
        return;
    }
    switch (address) {
        case 0x5100:
            m->prg_mode = value & 3;
            update_prg(mapper);
            break;
        case 0x5101:
            m->chr_mode = value & 3;
            update_chr(mapper);
            ppu_fetch(mapper, PPU_FETCH_CPU);
            break;
        case 0x5102:
            m->protect1 = value & 3;
            break;
        case 0x5103:
            m->protect2 = value & 3;
            break;
        case 0x5104:
            m->ex_mode = value & 3;
            mapper->ppu_bg_tile = m->ex_mode == 1 ? ppu_bg_tile : NULL;
            break;
        case 0x5105:
            m->nt_mode = value;
            update_nametables(mapper);
            break;
        case 0x5106:
            m->fill_tile = value;
            update_fill(mapper);
            break;
        case 0x5107:
            m->fill_colour = value & 3;
            update_fill(mapper);
            break;
        case 0x5130:
            m->chr_hi = value & 3;
            break;
        case 0x5203:
            m->irq_target = value;
            break;
        case 0x5204:
            m->irq_enabled = (value & 0x80) != 0;
            if (m->irq_enabled && m->irq_pending)
                interrupt(&mapper->emulator->cpu, MAPPER_IRQ);
            else
                interrupt_clear(&mapper->emulator->cpu, MAPPER_IRQ);
            break;
        case 0x5205:
            m->mul_a = value;
            break;
        case 0x5206:
            m->mul_b = value;
            break;
        default:
            break; /* sound, split screen: not done */
    }
}

/* ---- set-up ------------------------------------------------------------------------------------ */

static void reset(Mapper *mapper)
{
    MMC5_t *m = mapper->extension;
    m->prg_mode = 3;
    for (int i = 0; i < 5; i++)
        m->prg_regs[i] = 0xff;
    m->prg_regs[0] = 0;
    m->chr_mode = 0;
    memset(m->chr_regs, 0, sizeof(m->chr_regs));
    m->last_b = 0;
    m->ex_mode = 0;
    mapper->ppu_bg_tile = NULL;
    m->irq_enabled = m->irq_pending = m->in_frame = m->counter = 0;
    interrupt_clear(&mapper->emulator->cpu, MAPPER_IRQ);
    update_prg(mapper);
    update_chr(mapper);
    ppu_fetch(mapper, PPU_FETCH_CPU);
}

int load_MMC5(Mapper *mapper)
{
    MMC5_t *m = calloc(1, sizeof(MMC5_t));
    if (!m)
        return -1;
    /* PRG RAM: the largest board's 64 KB, whatever the header says (iNES headers often say
     * nothing) */
    uint8_t *ram = realloc(mapper->PRG_RAM, RAM_SIZE);
    if (!ram) {
        free(m);
        return -1;
    }
    if (mapper->RAM_size < RAM_SIZE)
        memset(ram + mapper->RAM_size, 0, RAM_SIZE - mapper->RAM_size);
    mapper->PRG_RAM = ram;
    mapper->RAM_size = RAM_SIZE;
    mapper->PRG_RAM_clamp = RAM_SIZE - 1;

    mapper->extension = m;
    m->prg_8k = mapper->PRG_banks * 2u;
    m->chr_1k = mapper->CHR_banks ? mapper->CHR_banks * 8u : (uint32_t)(mapper->CHR_RAM_size / 0x400u);
    if (!m->prg_8k || !m->chr_1k)
        return -1;
    mapper->read_ROM = read_ROM;
    mapper->write_ROM = write_ROM;
    mapper->prg_ptr = prg_ptr;
    mapper->read_CHR = read_CHR;
    mapper->chr_ptr = chr_ptr;
    mapper->write_CHR = write_CHR;
    mapper->reset = reset;
    mapper->ppu_fetch = ppu_fetch;
    mapper->ppu_line = ppu_line;
    /* the emulator is attached only after loading (nes_open): the rest waits for reset() */
    m->prg_mode = 3;
    for (int i = 1; i < 5; i++)
        m->prg_regs[i] = 0xff;
    m->ram6 = mapper->PRG_RAM;
    for (int i = 0; i < 4; i++)
        m->prg[i] = mapper->PRG_ROM + ((m->prg_8k - 1u) * 0x2000u);
    for (int i = 0; i < 8; i++)
        m->chr_a[i] = m->chr_b[i] = mapper->CHR_ROM + (i % m->chr_1k) * 0x400u;
    m->chr = m->chr_a;
    LOG(INFO, "Using mapper #005: MMC5");
    return 0;
}
