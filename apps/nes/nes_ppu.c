/*
 * nes_ppu.c - a fast PPU for the NES emulator core, in place of the core's ppu.c.
 *
 * The core's PPU (ppu.c of github.com/ObaraEmmanuel/NES) models the chip dot by dot: sprite evaluation state machine,
 * shift registers, a mapper call for every fetch. Faithful, but on a 600 MHz Cortex-M7 it
 * alone takes about 62 ms per frame (60 frames need 16.6 ms for everything). This one keeps
 * the core's PPU structure and interface (ppu.h: the same registers, VRAM, palette, OAM and
 * the functions mmu.c calls) but draws a whole scanline at once, and runs only what a game
 * can observe at the dot it happens:
 *
 *   line 0-239, dot 0     the line is drawn from the current scroll (v, fine x), sprites
 *                         evaluated; a sprite 0 hit is scheduled at its dot
 *   dot 256 / 257         vertical increment of v / horizontal bits copied from t
 *   dot 257 / 260 / 321   the pattern table address lines as the mapper sees them (MMC3
 *                         counts scanlines by A12: background, sprite and prefetch fetches)
 *   line 241, dot 1       vertical blank and NMI
 *   pre-render line       flags cleared (dot 1), vertical bits copied (dot 304), the
 *                         frame complete (dot 340), odd frames one dot shorter (NTSC)
 *
 * Mappers that watch the PPU more closely (MMC5) get hooks (mapper.h): which pattern data
 * a fetch is for (sprites, background, CPU), the start of each line, and background tiles
 * with their own pattern bank and palette (MMC5 extended attributes).
 *
 * Between events a PPU clock is an addition and a compare (ppu_run() in nes_ppu.h).
 * Register writes take effect at once (the core delays $2007 by a few dots). Effects in the
 * middle of a visible line (a scroll or palette change halfway across) show from the next
 * line on; the games that use them are few.
 *
 * Based on the core's ppu.c (MIT licence, (c) 2023 Emmanuel Obara); the functions that only
 * access memory and registers follow it closely.
 */
#include <stdlib.h>
#include <string.h>

#include "cpu6502.h"
#include "emulator.h"
#include "nes_ppu.h"
#include "ppu.h"
#include "utils.h"

uint32_t nes_palette[64];
static uint16_t s_rgb565[64]; /* the NES colours as the display shows them; nes_colours() */
int g_ppu_next;         /* dot of the next event on the current line */
int g_ppu_skip;         /* the picture of this frame is not shown: draw nothing but sprite 0 */

uint32_t g_ppu_lag_cycles; /* mapper.h */

static int s_last;      /* the last event dot handled on this line (-1: none yet) */
static int s_a12;       /* A12 as the mapper last saw it (set_bus) */
static int s_hit_dot;   /* sprite 0 hits on this line at this dot, -1: not */
static int s_lut_mask;
static void init_tables(void);

/* where the lines go (nes_set_output): line s_out_first at s_out, s_out_lines of them */
static uint16_t *s_out;
static int s_out_stride, s_out_first, s_out_lines;

/* ---- memory ------------------------------------------------------------------------------------- */

static inline uint8_t read_nt(const PPU *ppu, uint16_t address)
{
    address = (address & 0xefff) - 0x2000;
    return ppu->V_RAM[ppu->mapper->name_table_map[(address / 0x400) & 3] + (address & 0x3ff)];
}

uint8_t read_vram(PPU *ppu, uint16_t address)
{
    address &= 0x3fff;
    if (address < 0x2000)
        return ppu->mapper->read_CHR(ppu->mapper, address);
    if (address < 0x3F00)
        return read_nt(ppu, address);
    /* palette RAM: 6 bits, the upper 2 are open bus */
    uint8_t val = (ppu->palette[(address - 0x3F00) % 0x20] & 0x3f) | (ppu->latch & 0xc0);
    if (ppu->mask & 0x1)
        return val & 0xf0; /* greyscale */
    return val;
}

void write_vram(PPU *ppu, uint16_t address, uint8_t value)
{
    address &= 0x3fff;
    if (address < 0x2000)
    {
        ppu->mapper->write_CHR(ppu->mapper, address, value);
    }
    else if (address < 0x3F00)
    {
        address = (address & 0xefff) - 0x2000;
        ppu->V_RAM[ppu->mapper->name_table_map[(address / 0x400) & 3] + (address & 0x3ff)] = value;
    }
    else
    {
        address = (address - 0x3F00) % 0x20;
        ppu->palette[address] = value;
        if (address % 4 == 0)
            ppu->palette[address ^ 0x10] = value;
        s_lut_mask = -1; /* line_colours() again */
    }
}

/* ---- set-up ------------------------------------------------------------------------------------- */

void init_ppu(struct Emulator *emulator)
{
    to_pixel_format(nes_palette_raw, nes_palette, 64, ABGR8888);
    for (int i = 0; i < 64; i++)
        s_rgb565[i] = (uint16_t)nes_palette[i];
    init_tables();
    PPU *ppu = &emulator->ppu;
    memset(ppu, 0, sizeof(PPU));
    /* RGB565 pixels (half the memory traffic of the core's 32 bits): the lines go here
     * unless nes_set_output() gives them another place; nes_screen() */
    ppu->screen = malloc(sizeof(uint16_t) * VISIBLE_SCANLINES * VISIBLE_DOTS);
    if (ppu->screen)
        memset(ppu->screen, 0, sizeof(uint16_t) * VISIBLE_SCANLINES * VISIBLE_DOTS);
    nes_set_output(ppu, NULL, 0, 0, 0);
    ppu->emulator = emulator;
    ppu->mapper = &emulator->mapper;
    ppu->pre_render = emulator->type == NTSC ? NTSC_SCANLINES_PER_FRAME : PAL_SCANLINES_PER_FRAME;
    reset_ppu(ppu);
}

void reset_ppu(PPU *ppu)
{
    ppu->t = ppu->x = 0;
    ppu->dots = 1;
    ppu->scanlines = ppu->pre_render;
    ppu->w = 0;
    ppu->ctrl &= ~0xFC;
    ppu->mask = 0;
    ppu->status = 0;
    ppu->frames = 0;
    ppu->render_status = 0;
    ppu->supress_vblank = 0;
    memset(ppu->OAM, 0, sizeof(ppu->OAM));
    s_last = 0; /* dot 0 of the pre-render line is behind */
    s_lut_mask = -1;
    s_hit_dot = -1;
    g_ppu_next = 1;
}

void exit_ppu(PPU *ppu)
{
    free(ppu->screen);
    ppu->screen = NULL;
    s_out = NULL;
    s_out_lines = 0;
}

/* ---- registers (mmu.c) -------------------------------------------------------------------------- */

void set_latch(PPU *ppu, uint8_t value, uint8_t mask)
{
    ppu->latch &= ~mask;
    ppu->latch |= value & mask;
    if (mask == 0xff || ppu->latch_decay == 0)
        ppu->latch_decay = 60000;
}

static void update_nmi(PPU *ppu)
{
    if ((ppu->ctrl & GENERATE_NMI) && (ppu->status & V_BLANK))
        interrupt(&ppu->emulator->cpu, NMI);
    else
        interrupt_clear(&ppu->emulator->cpu, NMI);
}

uint8_t read_status(PPU *ppu)
{
    uint8_t status = ppu->status;
    ppu->w = 0;
    ppu->status &= ~V_BLANK;
    if (ppu->scanlines == 241 && ppu->dots <= 3)
        ppu->supress_vblank = 1; /* read just as the flag is set: it never shows, no NMI */
    update_nmi(ppu);
    return status;
}

void set_ctrl(PPU *ppu, uint8_t ctrl)
{
    uint8_t was = ppu->ctrl;
    ppu->ctrl = ctrl;
    ppu->t &= ~0xc00;
    ppu->t |= (ctrl & BASE_NAMETABLE) << 10;
    if ((was ^ ctrl) & GENERATE_NMI)
        update_nmi(ppu); /* enabling it during the vertical blank fires it */
}

void set_mask(PPU *ppu, uint8_t mask)
{
    ppu->mask = mask;
    ppu->render_status = (mask & RENDER_BITS) != 0;
}

void set_scroll(PPU *ppu, uint8_t coord)
{
    if (!ppu->w)
    {
        ppu->t &= ~X_SCROLL_BITS;
        ppu->t |= (coord >> 3) & X_SCROLL_BITS;
        ppu->x = coord & 0x7;
        ppu->w = 1;
    }
    else
    {
        ppu->t &= ~Y_SCROLL_BITS;
        ppu->t |= ((coord & 0x7) << 12) | ((coord & 0xF8) << 2);
        ppu->w = 0;
    }
}

/* the pattern table address lines as the mapper sees them */
static inline void set_bus(PPU *ppu, uint16_t addr)
{
    s_a12 = (addr & 0x1000) != 0;
    ppu->mapper->set_bus(ppu->mapper, addr);
}

void set_address(PPU *ppu, uint8_t address)
{
    if (!ppu->w)
    {
        ppu->t &= 0xff;
        ppu->t |= (address & 0x3f) << 8;
        ppu->w = 1;
    }
    else
    {
        ppu->t &= 0xff00;
        ppu->t |= address;
        ppu->v = ppu->t;
        set_bus(ppu, ppu->v);
        ppu->w = 0;
    }
}

static void step_v(PPU *ppu)
{
    ppu->v = (ppu->v + ((ppu->ctrl & BIT_2) ? 32 : 1)) & 0x7fff;
    set_bus(ppu, ppu->v);
}

uint8_t read_ppu(PPU *ppu)
{
    uint8_t data = ppu->read_buffer;
    if ((ppu->v & 0x3fff) >= 0x3F00)
    {
        /* the palette answers at once; the buffer gets the name table "below" it */
        data = read_vram(ppu, ppu->v);
        ppu->read_buffer = read_vram(ppu, ppu->v & 0x2fff);
    }
    else
    {
        ppu->read_buffer = read_vram(ppu, ppu->v);
    }
    step_v(ppu);
    return data;
}

void write_ppu(PPU *ppu, uint8_t value)
{
    write_vram(ppu, ppu->v, value);
    step_v(ppu);
}

void set_oam_address(PPU *ppu, uint8_t address)
{
    ppu->oam_address = address;
}

uint8_t read_oam(PPU *ppu)
{
    /* bits 2-4 of the attribute byte do not exist */
    return ppu->OAM[ppu->oam_address] & ((ppu->oam_address & 0x03) == 0x02 ? 0xE3 : 0xFF);
}

void write_oam(PPU *ppu, uint8_t value)
{
    ppu->OAM[ppu->oam_address++] = value;
}

void dma(PPU *ppu, uint8_t address)
{
    schedule_dma(&ppu->emulator->cpu, DMA_OAM, address * 0x100, ppu->OAM, 256, ppu->oam_address);
}

/* the core's dot-by-dot entry: not used by this PPU (the clock is ppu_run()) */
void execute_ppu(PPU *ppu)
{
    ppu_run(ppu, 1);
}

/* ---- drawing ------------------------------------------------------------------------------------ */

static void inc_vert_v(PPU *ppu)
{
    if ((ppu->v & FINE_Y) != FINE_Y)
    {
        ppu->v += 0x1000;
        return;
    }
    ppu->v &= ~FINE_Y;
    uint16_t coarse_y = (ppu->v & COARSE_Y) >> 5;
    if (coarse_y == 29)
    {
        coarse_y = 0;
        ppu->v ^= 0x800;
    }
    else if (coarse_y == 31)
    {
        coarse_y = 0;
    }
    else
    {
        coarse_y++;
    }
    ppu->v = (ppu->v & ~COARSE_Y) | (coarse_y << 5);
}

/* the NES colour (0-63) of palette entry @index */
static inline uint8_t colour(const PPU *ppu, uint8_t index)
{
    uint8_t c = ppu->palette[index & 0x1f] & 0x3f;
    if (ppu->mask & 0x1)
        c &= 0x30;
    return c;
}

/*
 * The RGB565 colours of the line pixels draw_line() makes: background 0 | palette << 2 |
 * pattern (0-3), sprites 0x10 | palette << 2 | pattern (1-3). A transparent background
 * pixel (pattern 0) shows the backdrop, whatever its palette. Made again after a palette
 * write or a greyscale change.
 */
static uint16_t s_lut[32];      /* s_lut_mask: the greyscale bit it was made for, -1: not made */

static const uint16_t *line_colours(const PPU *ppu)
{
    if (s_lut_mask != (ppu->mask & 1))
    {
        for (int i = 0; i < 32; i++)
            s_lut[i] = s_rgb565[colour(ppu, (uint8_t)i)];
        s_lut[4] = s_lut[8] = s_lut[12] = s_lut[0];
        s_lut_mask = ppu->mask & 1;
    }
    return s_lut;
}

void nes_set_output(PPU *ppu, uint16_t *pix, int stride, int first, int lines)
{
    if (!pix)
    {
        pix = (uint16_t *)ppu->screen;
        stride = VISIBLE_DOTS;
        first = 0;
        lines = pix ? VISIBLE_SCANLINES : 0;
    }
    s_out = pix;
    s_out_stride = stride;
    s_out_first = first;
    s_out_lines = lines;
}

uint16_t *nes_screen(const PPU *ppu)
{
    return (uint16_t *)ppu->screen;
}

/* where line @line goes, NULL if nowhere */
static inline uint16_t *line_pixels(int line)
{
    unsigned n = (unsigned)(line - s_out_first);
    return n < (unsigned)s_out_lines ? s_out + (int)n * s_out_stride : NULL;
}

/* the backdrop while rendering is off (or the palette entry v points at) */
static void blank_line(PPU *ppu, int line)
{
    uint16_t *out = line_pixels(line);
    if (!out)
        return;
    uint16_t c = s_rgb565[colour(ppu, (ppu->v & 0x3f00) == 0x3f00 ? (uint8_t)(ppu->v & 0x1f) : 0)];
    for (int i = 0; i < VISIBLE_DOTS; i++)
        out[i] = c;
}

/* a pattern byte's bits one to a byte, the leftmost pixel (bit 7) in the lowest byte;
 * s_spread2 the same one bit up (the high plane) */
static uint64_t s_spread[256], s_spread2[256];
static uint8_t s_mirror[256];   /* a pattern byte left to right (sprites flipped) */

static void init_tables(void)
{
    for (int i = 0; i < 256; i++)
    {
        uint64_t v = 0;
        uint8_t r = 0;
        for (int b = 0; b < 8; b++)
        {
            if (i & (0x80 >> b))
                v |= (uint64_t)1 << (8 * b);
            if (i & (1 << b))
                r |= (uint8_t)(0x80 >> b);
        }
        s_spread[i] = v;
        s_spread2[i] = v << 1;
        s_mirror[i] = r;
    }
}

/* the pattern bytes at @addr and @addr + 8 (one tile row) */
static inline void read_row(Mapper *m, uint16_t addr, uint32_t *lo, uint32_t *hi)
{
    if (m->chr_ptr)
    {
        const uint8_t *p = m->chr_ptr(m, addr);
        *lo = p[0];
        *hi = p[8];
    }
    else
    {
        *lo = m->read_CHR(m, addr);
        *hi = m->read_CHR(m, addr + 8);
    }
}

/*
 * Draws visible line @line from v and the fine x scroll; returns the dot of a sprite 0 hit
 * on it, or -1. With @skip only the hit is worked out (when sprite 0 is on the line).
 *
 * The background is made 8 pixels (a tile row) at a time through s_spread; the sprites are
 * laid over it afterwards where they are.
 */
static int draw_line(PPU *ppu, int line, int skip)
{
    const uint8_t ctrl = ppu->ctrl, mask = ppu->mask;
    const int height = (ctrl & LONG_SPRITE) ? 16 : 8;
    const uint8_t *oam = ppu->OAM;
    Mapper *m = ppu->mapper;
    uint16_t *out = line_pixels(line);
    if (!out)
        skip = 1;

    /* sprites: the first 8 on the line (OAM order), from the line before (Y + 1) */
    int nspr = 0;
    uint8_t found[8];
    for (int i = 0; i < 64; i++)
    {
        if ((unsigned)(line - 1 - oam[i * 4]) >= (unsigned)height)
            continue;
        if (nspr == 8)
        {
            ppu->status |= SPRITE_OVERFLOW;
            break;
        }
        found[nspr++] = (uint8_t)i;
    }
    if (!(mask & SHOW_SPRITE))
        nspr = 0;
    const int zero_here = nspr && found[0] == 0;
    if (skip && !zero_here)
        return -1;

    /* their pattern rows, flipped as shown */
    uint32_t spr_lo[8], spr_hi[8];
    if (nspr)
    {
        if (m->ppu_fetch)
            m->ppu_fetch(m, PPU_FETCH_SPRITES);
        for (int k = 0; k < nspr; k++)
        {
            const uint8_t *s = &oam[found[k] * 4];
            uint8_t tile = s[1], attr = s[2];
            int row = line - 1 - s[0];
            if (attr & FLIP_VERTICAL)
                row = height - 1 - row;
            uint16_t addr;
            if (height == 16)
                addr = (uint16_t)(((tile & 1) << 12) | (((tile & 0xfe) << 4) + ((row & 8) << 1) + (row & 7)));
            else
                addr = (uint16_t)(((ctrl & SPRITE_TABLE) ? 0x1000 : 0) | (tile << 4)) + row;
            read_row(m, addr, &spr_lo[k], &spr_hi[k]);
            if (attr & FLIP_HORIZONTAL)
            {
                spr_lo[k] = s_mirror[spr_lo[k]];
                spr_hi[k] = s_mirror[spr_hi[k]];
            }
        }
    }

    /* background: 33 tiles from v, the fine x scroll into the first */
    uint8_t bg[VISIBLE_DOTS + 16] __attribute__((aligned(8)));
    if (mask & SHOW_BG)
    {
        uint16_t v = ppu->v;
        const uint16_t table = (ctrl & BG_TABLE) ? 0x1000 : 0;
        const uint16_t fine_y = (v >> 12) & 7;
        const uint8_t *vram = ppu->V_RAM;
        const uint8_t *nts = vram + m->name_table_map[(v >> 10) & 3];
        void (*ex_tile)(Mapper *, uint16_t, uint8_t, uint16_t, uint8_t *, uint8_t *, uint8_t *) = m->ppu_bg_tile;
        if (m->ppu_fetch)
            m->ppu_fetch(m, PPU_FETCH_BG);
        /* the pattern table's four 1 KB pages as the mapper has them now */
        const uint8_t *page[4] = { NULL, NULL, NULL, NULL };
        if (m->chr_ptr && !ex_tile)
            for (int k = 0; k < 4; k++)
                page[k] = m->chr_ptr(m, (uint16_t)(table + k * 0x400));
        uint8_t *o = bg;
        for (int t = 0; t < 33; t++)
        {
            uint32_t nt = nts[v & 0x3ff], lo, hi, pal;
            if (ex_tile)
            {
                uint8_t l, h, p;
                ex_tile(m, v, (uint8_t)nt, fine_y, &l, &h, &p);
                lo = l;
                hi = h;
                pal = p;
            }
            else
            {
                uint8_t at = nts[0x3C0 | ((v >> 4) & 0x38) | ((v >> 2) & 0x07)];
                pal = ((at >> (((v >> 4) & 4) | (v & 2))) & 3) << 2;
                if (page[0])
                {
                    const uint8_t *p = page[nt >> 6] + (((nt & 0x3f) << 4) | fine_y);
                    lo = p[0];
                    hi = p[8];
                }
                else
                {
                    read_row(m, (uint16_t)(table | nt << 4 | fine_y), &lo, &hi);
                }
            }
            uint64_t px = s_spread[lo] | s_spread2[hi] | (uint64_t)pal * 0x0101010101010101ull;
            memcpy(o, &px, 8);
            o += 8;
            if ((v & COARSE_X) == 31)
            {
                v &= ~COARSE_X;
                v ^= 0x400;
                nts = vram + m->name_table_map[(v >> 10) & 3];
            }
            else
            {
                v++;
            }
        }
        if (!(mask & SHOW_BG_8))
            memset(bg + ppu->x, 0, 8);
    }
    else
    {
        memset(bg, 0, sizeof(bg));
    }
    const uint8_t *bgp = bg + ppu->x;

    /* sprite 0 hit: an opaque sprite 0 pixel over an opaque background pixel (not at x 255) */
    int hit = -1;
    if (zero_here && !(ppu->status & SPRITE_0_HIT) && (mask & SHOW_BG))
    {
        uint32_t opaque = spr_lo[0] | spr_hi[0];
        int x = oam[3];
        for (int b = 0; b < 8 && x + b < 255; b++)
        {
            if (x + b < 8 && !(mask & SHOW_SPRITE_8))
                continue;
            if ((opaque << b & 0x80) && (bgp[x + b] & 3))
            {
                hit = x + b + 1;
                break;
            }
        }
    }
    if (skip)
        return hit;

    const uint16_t *lut = line_colours(ppu);
    for (int px = 0; px < VISIBLE_DOTS; px += 2)
    {
        uint32_t two = lut[bgp[px]] | (uint32_t)lut[bgp[px + 1]] << 16;
        memcpy(out + px, &two, 4);
    }
    if (!nspr)
        return hit;

    /* the sprites: the lowest OAM index wins, so they go from the last one to the first into
     * spr_pix (its priority bit into spr_behind), then onto the line where they are */
    uint8_t spr_pix[VISIBLE_DOTS + 8], spr_behind[VISIBLE_DOTS + 8];
    for (int k = 0; k < nspr; k++)
        memset(spr_pix + oam[found[k] * 4 + 3], 0, 8);
    for (int k = nspr - 1; k >= 0; k--)
    {
        const uint8_t *s = &oam[found[k] * 4];
        uint8_t pal = 0x10 | (s[2] & 3) << 2, behind = s[2] & BIT_5;
        uint32_t lo = spr_lo[k], hi = spr_hi[k];
        uint8_t *sp = spr_pix + s[3], *sb = spr_behind + s[3];
        for (int b = 0; b < 8; b++)
        {
            uint32_t p = (lo >> (7 - b) & 1) | (hi >> (7 - b) & 1) << 1;
            if (p)
            {
                sp[b] = pal | p;
                sb[b] = behind;
            }
        }
    }
    if (!(mask & SHOW_SPRITE_8))
        memset(spr_pix, 0, 8);
    for (int k = 0; k < nspr; k++)
    {
        int x = oam[found[k] * 4 + 3], end = x + 8 < VISIBLE_DOTS ? x + 8 : VISIBLE_DOTS;
        for (int px = x; px < end; px++)
        {
            uint8_t sc = spr_pix[px];
            if (sc && (!(bgp[px] & 3) || !spr_behind[px]))
                out[px] = lut[sc];
        }
    }
    return hit;
}

/* ---- the clock ---------------------------------------------------------------------------------- */

/* the pattern table address lines as the mapper sees them during the fetches of a line */
static int bg_a12(const PPU *ppu)
{
    return (ppu->ctrl & BG_TABLE) != 0;
}

/* 8x16 sprites fetch (at least the empty slots' tile $FF) from $1000 */
static int sprites_a12(const PPU *ppu)
{
    return (ppu->ctrl & LONG_SPRITE) || (ppu->ctrl & SPRITE_TABLE);
}

static void bus_bg(PPU *ppu)
{
    set_bus(ppu, bg_a12(ppu) ? 0x1000 : 0x0000);
}

static void bus_sprites(PPU *ppu)
{
    set_bus(ppu, sprites_a12(ppu) ? 0x1000 : 0x0000);
}

/* the next event after dot @d on the current line */
static int next_event(PPU *ppu, int d)
{
    int line = (int)ppu->scanlines;
    int n = DOTS_PER_SCANLINE;
    if (line < VISIBLE_SCANLINES)
    {
        static const int EV[] = {0, 256, 257, 260, 321};
        for (int i = 0; i < 5; i++)
            if (EV[i] > d)
            {
                n = EV[i];
                break;
            }
        if (s_hit_dot > d && s_hit_dot < n)
            n = s_hit_dot;
    }
    else if (line == 241)
    {
        if (d < 1)
            n = 1;
    }
    else if (line == ppu->pre_render)
    {
        static const int EV[] = {1, 256, 257, 260, 304, 321, 340};
        for (int i = 0; i < 7; i++)
            if (EV[i] > d)
            {
                n = EV[i];
                break;
            }
    }
    return n;
}

static void event(PPU *ppu, int d)
{
    int line = (int)ppu->scanlines;
    int rendering = ppu->render_status;
    Mapper *m = ppu->mapper;
    if (line < VISIBLE_SCANLINES)
    {
        switch (d)
        {
        case 0:
            if (m->ppu_line)
                m->ppu_line(m, line, rendering);
            if (rendering)
            {
                s_hit_dot = draw_line(ppu, line, g_ppu_skip);
                if (m->ppu_fetch)
                    m->ppu_fetch(m, PPU_FETCH_CPU);
            }
            else if (!g_ppu_skip)
            {
                blank_line(ppu, line);
            }
            break;
        case 256:
            if (rendering)
            {
                bus_bg(ppu);
                inc_vert_v(ppu);
            }
            break;
        case 257:
            if (rendering)
            {
                ppu->v &= ~HORIZONTAL_BITS;
                ppu->v |= ppu->t & HORIZONTAL_BITS;
            }
            break;
        case 260:
            if (rendering)
                bus_sprites(ppu);
            break;
        case 321:
            if (rendering)
                bus_bg(ppu);
            break;
        default:
            if (d == s_hit_dot)
                ppu->status |= SPRITE_0_HIT;
            break;
        }
        return;
    }
    if (line == 241)
    {
        if (m->ppu_line)
            m->ppu_line(m, 241, rendering);
        if (!ppu->supress_vblank)
            ppu->status |= V_BLANK;
        ppu->supress_vblank = 0;
        update_nmi(ppu);
        return;
    }
    /* the pre-render line */
    switch (d)
    {
    case 1:
        ppu->status &= ~(V_BLANK | SPRITE_0_HIT | SPRITE_OVERFLOW);
        update_nmi(ppu);
        break;
    case 256:
        if (rendering)
        {
            bus_bg(ppu);
            inc_vert_v(ppu);
        }
        break;
    case 257:
        if (rendering)
        {
            ppu->v &= ~HORIZONTAL_BITS;
            ppu->v |= ppu->t & HORIZONTAL_BITS;
        }
        break;
    case 260:
        if (rendering)
            bus_sprites(ppu);
        break;
    case 304:
        if (rendering)
        {
            ppu->v &= ~VERTICAL_BITS;
            ppu->v |= ppu->t & VERTICAL_BITS;
        }
        break;
    case 321:
        if (rendering)
            bus_bg(ppu);
        break;
    case 340:
        ppu->render = 1; /* the frame is complete */
        ppu->frames++;
        break;
    }
}

uint32_t ppu_dots_to_checkpoint(PPU *ppu)
{
    Mapper *m = ppu->mapper;
    int line = (int)ppu->scanlines, d = (int)ppu->dots;
    /* A12 IRQs: a mapper that can tell stops the clock only at the rise that raises its IRQ
     * (the others it counts as the PPU catches up); the bus events of a rendered line are
     * background (256), sprites (260), background (321) */
    int every_edge = m->a12_irq && !m->a12_rises_to_irq;
    int rises = 0, a12 = s_a12, bg = 0, spr = 0;
    if (m->a12_irq && m->a12_rises_to_irq && ppu->render_status)
    {
        rises = m->a12_rises_to_irq(m);
        bg = bg_a12(ppu);
        spr = sprites_a12(ppu);
    }
    uint32_t dist = 0;
    int first = 1;
    for (int n = 0; n < 400; n++)
    {
        int cps[4], k = 0;
        int bus_line = line < VISIBLE_SCANLINES || line == (int)ppu->pre_render;
        if (line < VISIBLE_SCANLINES && m->ppu_line)
            cps[k++] = 0;
        if (line == 241)
            cps[k++] = 1;
        if (bus_line && every_edge)
        {
            cps[k++] = 260;
            cps[k++] = 321;
        }
        if (bus_line && rises)
        {
            static const int BUS_DOTS[3] = { 256, 260, 321 };
            for (int i = 0; i < 3; i++)
            {
                if (first && BUS_DOTS[i] <= d)
                    continue; /* (already seen) */
                int level = i == 1 ? spr : bg;
                if (level && !a12 && --rises == 0)
                {
                    cps[k++] = BUS_DOTS[i];
                    break;
                }
                a12 = level;
            }
        }
        if (line == (int)ppu->pre_render)
            cps[k++] = 340;
        for (int i = 0; i < k; i++)
            if (first ? cps[i] > d : cps[i] >= 0)
                return dist + (uint32_t)(cps[i] - (first ? d : 0));
        dist += (uint32_t)(DOTS_PER_SCANLINE - (first ? d : 0));
        first = 0;
        line = line >= (int)ppu->pre_render ? 0 : line + 1;
    }
    return dist;
}

void ppu_events(PPU *ppu)
{
    for (;;)
    {
        int d = g_ppu_next;
        if ((int)ppu->dots < d)
            return;
        if (d >= DOTS_PER_SCANLINE)
        {
            /* the next line */
            ppu->dots -= DOTS_PER_SCANLINE;
            if (ppu->latch_decay)
            {
                ppu->latch_decay = ppu->latch_decay > DOTS_PER_SCANLINE ? ppu->latch_decay - DOTS_PER_SCANLINE : 0;
                if (!ppu->latch_decay)
                    ppu->latch = 0;
            }
            if (++ppu->scanlines > ppu->pre_render)
            {
                ppu->scanlines = 0;
                /* odd frames skip a dot with rendering on (NTSC) */
                if ((ppu->frames & 1) && ppu->render_status && ppu->emulator->type == NTSC)
                    ppu->dots++;
            }
            s_last = -1;
            s_hit_dot = -1;
        }
        else
        {
            /* the CPU cycles since the event's dot (NTSC 3 dots a cycle, PAL 3.2): the PPU may
             * catch up many lines at once, so PAL's fifths count (MMC3's A12 filter) */
            uint32_t lag = (uint32_t)((int)ppu->dots - d);
            g_ppu_lag_cycles = ppu->emulator->type == PAL ? lag * 5u / 16u : lag / 3u;
            event(ppu, d);
            g_ppu_lag_cycles = 0;
            s_last = d;
        }
        g_ppu_next = next_event(ppu, s_last);
    }
}
