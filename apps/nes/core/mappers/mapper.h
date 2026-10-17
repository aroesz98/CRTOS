#pragma once

#include <stdint.h>
#include <stddef.h>

#define INES_HEADER_SIZE 16

typedef enum TVSystem {
    NTSC = 0,
    DUAL,
    PAL,
    DENDY,
} TVSystem;

typedef enum {
    NO_MIRRORING,
    VERTICAL,
    HORIZONTAL,
    ONE_SCREEN,
    ONE_SCREEN_LOWER,
    ONE_SCREEN_UPPER,
    FOUR_SCREEN,
} Mirroring;

typedef enum MapperFormat {
    ARCHAIC_INES,
    INES,
    NES2,
} MapperFormat;

struct Genie;
struct Emulator;
struct NSF;

typedef struct ROMData {
    char *rom_name;
    uint8_t *rom;
    uint8_t *genie_rom;
    uint64_t rom_size;
    uint64_t genie_rom_size;
} ROMData;

typedef struct Mapper {
    uint8_t *CHR_ROM;
    uint8_t *PRG_ROM;
    uint8_t *PRG_RAM;
    uint8_t *PRG_ptrs[8];
    uint8_t *CHR_ptrs[8];
    uint8_t PRG_regs[8];
    uint8_t CHR_regs[8];
    uint16_t PRG_banks;
    uint16_t CHR_banks;
    size_t CHR_RAM_size;
    uint8_t RAM_banks;
    size_t RAM_size;
    Mirroring mirroring;
    TVSystem type;
    MapperFormat format;
    uint16_t name_table_map[4];
    uint32_t clamp;
    uint32_t PRG_RAM_clamp;
    uint16_t mapper_num;
    uint8_t submapper;
    uint8_t is_nsf;
    void (*set_bus)(struct Mapper *, uint16_t);
    uint8_t (*read_ROM)(struct Mapper *, uint16_t);
    void (*write_ROM)(struct Mapper *, uint16_t, uint8_t);
    uint8_t (*read_PRG)(struct Mapper *, uint16_t);
    void (*write_PRG)(struct Mapper *, uint16_t, uint8_t);
    uint8_t (*read_CHR)(struct Mapper *, uint16_t);
    void (*write_CHR)(struct Mapper *, uint16_t, uint8_t);
    void (*reset)(struct Mapper *);

    // mapper extension structs would be attached here
    // memory should be allocated dynamically and should
    // not be freed since this is done by the generic mapper functions
    void *extension;
    // pointer to game genie if any
    struct Genie *genie;
    struct NSF *NSF;
    struct Emulator *emulator;

    // CRTOS: hooks of the fast PPU (apps/nes/nes_ppu.c) for mappers that watch the PPU more
    // closely than through set_bus (MMC5); NULL for the others.
    // what pattern data the next read_CHR calls fetch (PPU_FETCH_*)
    void (*ppu_fetch)(struct Mapper *, int what);
    // the start of visible line @line (0-239) and the vertical blank (241)
    void (*ppu_line)(struct Mapper *, int line, int rendering);
    // the pattern and palette of the background tile at v (nametable byte nt) instead of the
    // attribute table and read_CHR: *lo, *hi pattern bytes, *pal palette << 2
    void (*ppu_bg_tile)(struct Mapper *, uint16_t v, uint8_t nt, uint16_t fine_y, uint8_t *lo, uint8_t *hi,
                        uint8_t *pal);
    // the mapper counts the PPU's A12 edges (dots 260 and 321 of a line) and may raise an IRQ
    // there: the lazy clock (apps/nes/nes_core.c) stops at those dots
    uint8_t a12_irq;
    // the A12 rises (set_bus) until the mapper raises its IRQ, 1: the next one; 0: none will.
    // The lazy clock then stops only at that one (NULL: at every A12 edge)
    int (*a12_rises_to_irq)(struct Mapper *);
    // where the PRG ROM byte at CPU address addr (>= $8000) is now, NULL if not ROM (the JIT,
    // apps/nes/nes_jit.c, keys its translations by it)
    const uint8_t *(*prg_ptr)(struct Mapper *, uint16_t addr);
    // where read_CHR reads the pattern byte at addr now, valid to the end of its 1 KB page;
    // NULL if the PPU must call read_CHR (the fast PPU takes the pages once a line)
    const uint8_t *(*chr_ptr)(struct Mapper *, uint16_t addr);
} Mapper;

// CRTOS: Mapper.ppu_fetch
enum { PPU_FETCH_CPU, PPU_FETCH_SPRITES, PPU_FETCH_BG };
// CRTOS: CPU cycles the PPU is behind the CPU while it catches up (apps/nes/nes_ppu.c), 0 at
// other times: a set_bus call happened at cpu.t_cycles minus this
extern uint32_t g_ppu_lag_cycles;

void load_file(char *file_name, char *game_genie, Mapper *mapper);
int load_data(ROMData *data, Mapper *mapper);
void free_mapper(Mapper *mapper);
void set_mirroring(Mapper *mapper, Mirroring mirroring);

// mapper specifics

int load_UXROM(Mapper *mapper);
int load_MMC1(Mapper *mapper);
int load_CNROM(Mapper *mapper);
int load_GNROM(Mapper *mapper);
int load_AOROM(Mapper *mapper);
int load_MMC3(Mapper *mapper);
int load_colordreams(Mapper *mapper);
int load_colordreams46(Mapper *mapper);
int load_VRC1(Mapper *mapper);
int load_UN1ROM(Mapper *mapper);
int load_mapper180(Mapper *mapper);
int load_mapper185(Mapper *mapper);
int load_CPROM(Mapper *mapper);
int load_MMC5(Mapper *mapper); // CRTOS
