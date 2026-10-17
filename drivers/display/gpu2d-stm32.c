/*
 * gpu2d-stm32.ko - the STM32H7S7's NeoChrom GPU2D as the 2D accelerator (gpu2d): rectangle
 * fills, copies with format conversion, scaled copies (bilinear), alpha blending of an ARGB8888
 * surface over another surface, and textured primitives.
 *
 * No vendor library: the driver writes the GPU's command lists itself (the encoding was
 * worked out from ST's NemaGFX and checked on the board). A command is a pair of words,
 * [31:16] flags | [15:0] register offset, then the value; flag 0xff00 on DRAW (R_DRAW) starts a
 * raster operation. The GPU reads a ring of commands; the CPU appends to it and moves the
 * write pointer (RB_CTRL). A list is called from the ring (CL_ADDR, CL_SIZE with the "execute"
 * flag) and followed by a write of its number to CLID and of 1 to INT_CTRL, which interrupts
 * once the list is done; the caller waits for that, so the ring is consumed when the next
 * list comes.
 *
 * A batch (a frame of the graphics server) is one list: registers the list has already set
 * to the same value are left out, so a run of copies from one window costs a few words each.
 * The GPU's read cache (ICACHE, in front of its bus port) is on: reads from the PSRAM in
 * whole lines. It knows nothing of writes - the processor's or the GPU's own - so it is
 * emptied before each list, and an operation that reads what an earlier one of its list
 * wrote (blending over what was just composed) starts a list of its own.
 *
 * Blending uses the GPU's fixed-function ROP blender (BLEND: source factor | destination
 * factor << 8, as NemaGFX numbers them - 0x504: source x alpha + destination x (1 - alpha),
 * 0x501: source + destination x (1 - alpha)); the fragment pipeline only samples the source
 * (FRAG_CFG 0x141f801f) or passes the raster colour (0x141f001f), with the fixed program slots
 * that the start-up loads.
 *
 * Primitives (gpu2d.h, for 3D programs): triangles and quadrilaterals (DRAW 4 and 5, points
 * in 16.16 fixed point) whose texture comes through the full 3x3 texture matrix (u, v and
 * their divisor q) - the GPU textures in perspective itself. Thousands of them make many
 * lists: two buffers take turns, the processor writing one while the GPU runs the other. The
 * lists are in cached memory, cleaned before the GPU gets one: a word at a time into memory
 * that is not cached took the processor longer than the GPU took to draw.
 *
 * The GPU is a bus master that sees AXI SRAM, PSRAM and the NOR flash, not the TCMs. Sources
 * in cached memory are cleaned before a batch, destinations in cached memory cleaned and
 * invalidated before and after it (the whole data cache when there is much of it).
 *
 * The DMA2D (Chrom-ART) next to it does fills, copies with format conversion and blending
 * as well, reading and writing whole lines in bursts - from the PSRAM several times faster
 * than the GPU's texture unit, which waits for each line it misses. So those operations go to
 * the DMA2D, one after the other, the short ones waited for by polling, the long ones by its
 * interrupt; the GPU does what only it can.
 *
 * Device tree: the GPU (reg 0, interrupts 0 and 1, clock "gpu"), its read cache (reg 1), the
 * DMA2D (reg 2, interrupt 2, clock "dma2d"), on-chip memory for primitives (reg 3, the tile);
 * "crtos,no-gpu-cache" leaves the read cache off, "crtos,no-dma2d" gives everything to the GPU.
 */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <crtos/arch.h>
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/gpu2d.h>
#include <crtos/irq.h>
#include <crtos/mm.h>
#include <crtos/module.h>
#include <crtos/sync.h>
#include "stm32h7rsxx.h"

/* registers (offsets in the GPU2D window, the same in command lists) */
#define R_DST_ADDR 0x000
#define R_DST_FMT 0x004 /* [31:24] format, [15:0] stride */
#define R_DST_SIZE 0x008 /* [31:16] height, [15:0] width */
#define R_SRC_ADDR 0x010
#define R_SRC_FMT 0x014 /* [31:24] format, [23:16] sampling, [15:0] stride */
#define R_SRC_SIZE 0x018
#define R_SHADER_ADDR 0x0c4
#define R_SHADER_LO 0x0c8
#define R_SHADER_HI 0x0cc
#define R_RB_CTRL 0x0ec
#define R_CL_ADDR 0x0f0
#define R_CL_SIZE 0x0f4
#define R_INT_CTRL 0x0f8 /* bit 0: list done */
#define R_STATUS 0x0fc   /* non-zero while a unit works */
#define R_DRAW 0x100
#define R_DST_START 0x104 /* [31:16] y, [15:0] x */
#define R_DST_END 0x108   /* exclusive */
#define R_CLIP_MIN 0x110
#define R_CLIP_MAX 0x114
#define R_TEX0_ADDR 0x118
#define R_FRAG_CFG 0x11c
#define R_RASTER_COLOR 0x12c /* 0xAABBGGRR */
#define R_CLID 0x148
#define R_TEX_UX 0x160 /* IEEE floats: u = ux x + uy y + u0, v = vx x + vy y + v0 */
#define R_TEX_UY 0x164
#define R_TEX_U0 0x168
#define R_TEX_VX 0x16c
#define R_TEX_VY 0x170
#define R_TEX_V0 0x174
#define R_TEX_QX 0x178 /* the divisor: q = qx x + qy y + q0 (perspective; 0, 0, 1 for the rest) */
#define R_TEX_QY 0x17c
#define R_TEX_Q0 0x180
#define R_P0_X 0x120   /* points of a triangle or quadrilateral, 16.16 fixed point */
#define R_BLEND 0x1d0
#define R_ID 0x1ec
#define R_CONFIG 0x1f0
#define R_CONFIG_H 0x1f4
#define R_REVISION 0x1dc
#define R_HOLD 0xff4    /* [3:0] hold signals */
#define R_SYS_INT 0xff8 /* hold points reached, errors */

#define GPU_ID 0x86362000u
#define CFG_ROP_BLENDER 0x10000000u

#define F_EXEC 0x0002u /* on CL_SIZE: run the list at CL_ADDR */
#define F_DRAW 0xff00u
#define CMD(flags, reg) (((uint32_t)(flags) << 16) | (reg))
#define NOP 0x00010000u
#define JUMP_ADDR 0xff0000f0u
#define JUMP_SIZE 0xff0000f4u

#define FMT_RGB565 0x04u
#define FMT_BGRA8888 0x10u /* the word 0xAARRGGBB */
#define SAMPLE_BILINEAR (0x01u << 16) /* SRC_FMT [23:16]: filtering; 0: the nearest texel, clamped */
#define SAMPLE_REPEAT (0x04u << 16)   /* ... the texture repeats */
#define FRAG_FILL 0x141f001fu
#define FRAG_BLIT 0x141f801fu
#define BLEND_SRC 0x001u
#define BLEND_SIMPLE 0x504u
#define BLEND_PREMUL 0x501u
#define DRAW_RECT 2u
#define DRAW_TRIANGLE 4u
#define DRAW_QUAD 5u

#define RING_BYTES 1024u
/* a command list: 8 KB, two take turns, on chip if there is room; a frame of the compositor fits one */
#define CL_WORDS 2048u
#define OP_WORDS 48u   /* the most one operation takes */
#define PRIM_WORDS 48u /* ... one primitive */
#define GPU_BANDS_MAX 64u /* bands of the tile in a destination */
#define SHADOW 128u    /* registers 0x000-0x1fc: the value last written by this batch */
#define TIMEOUT_MS 200
#define WHOLE_CACHE (64u * 1024u) /* more to clean than this: the whole data cache is quicker */

/* DMA2D */
#define D2D_M2M 0u
#define D2D_M2M_PFC 1u
#define D2D_M2M_BLEND 2u
#define D2D_R2M 3u
#define D2D_ARGB8888 0u
#define D2D_RGB565 2u
#define D2D_POLL_PX 4096u /* an operation this small is waited for by polling (~20 us) */
#define D2D_ERRORS (DMA2D_ISR_TEIF | DMA2D_ISR_CEIF)

struct gpu
{
    struct device *dev;
    uintptr_t base;
    ICACHE_TypeDef *icache; /* the GPU's read cache; NULL or off: not used */
    bool icache_on;
    DMA2D_TypeDef *d2d;     /* NULL: everything on the GPU */
    uint8_t *tile;          /* on-chip memory for drawing primitives a band at a time, or NULL */
    uint32_t tile_bytes;
    uint8_t *pband;         /* the bands of each primitive (first, last) */
    uint16_t *bin;          /* the primitives of each band, band after band */
    uint32_t pband_cap, bin_cap;
    int irq_d2d;
    struct semaphore d2d_done;
    uint32_t d2d_ops, d2d_errors;
    int irq, irq_err;
    struct semaphore done;
    uint32_t *ring;      /* not cached; the last 4 words jump back to its start */
    uint32_t ring_words; /* usable words before that jump */
    uint32_t wr;         /* the CPU's write index */
    uint32_t *cl;        /* the command list being built (cached memory) */
    uint32_t n;          /* its words */
    uint32_t *cl_buf[2]; /* the two list buffers, cl one of them */
    unsigned cl_cur;
    uint32_t cl_id[2];   /* the list each buffer last held, 0: done with */
    uint32_t id;         /* number of the last list submitted */
    volatile uint32_t done_id;
    uint32_t shadow[SHADOW];      /* register values the list has set */
    uint32_t known[SHADOW / 32u]; /* ... valid ones */
    uintptr_t wlo, whi;           /* memory the list writes (so far) */
    uint32_t batches, runs, splits, timeouts, errors, ops, prims;
    uint64_t busy_us, hits, misses;
};

static uint32_t rd(struct gpu *g, uint32_t reg)
{
    return *(volatile uint32_t *)(g->base + reg);
}

static void wr(struct gpu *g, uint32_t reg, uint32_t v)
{
    __asm volatile("dsb 0xF" ::: "memory"); /* the ring's words before the doorbell */
    *(volatile uint32_t *)(g->base + reg) = v;
}

static uint32_t fbits(float f)
{
    union
    {
        float f;
        uint32_t u;
    } c = {.f = f};
    return c.u;
}

/* ---- the GPU's read cache ---------------------------------------------------------------------- */

static void icache_wait(ICACHE_TypeDef *ic)
{
    uint32_t t0 = cpu_cycles();
    while ((ic->SR & ICACHE_SR_BUSYF) && cpu_cycles() - t0 < SystemCoreClock / 1000u)
    {
    }
}

/* empty it: the GPU reads the ring, the lists and the sources anew */
static void icache_invalidate(struct gpu *g)
{
    if (!g->icache_on)
        return;
    ICACHE_TypeDef *ic = g->icache;
    icache_wait(ic);
    ic->CR |= ICACHE_CR_CACHEINV;
    icache_wait(ic);
    ic->FCR = ICACHE_FCR_CBSYENDF;
}

static void icache_set(struct gpu *g, bool on)
{
    ICACHE_TypeDef *ic = g->icache;
    if (!ic)
        return;
    g->icache_on = false;
    ic->CR &= ~ICACHE_CR_EN;
    icache_wait(ic);
    if (!on)
        return;
    ic->CR |= ICACHE_CR_CACHEINV;
    icache_wait(ic);
    ic->FCR = ICACHE_FCR_CBSYENDF;
    ic->CR |= ICACHE_CR_EN | ICACHE_CR_HITMEN | ICACHE_CR_MISSMEN; /* (and its hit / miss counters) */
    g->icache_on = true;
}

/* ---- the ring --------------------------------------------------------------------------------- */

static void put(struct gpu *g, uint32_t w)
{
    g->ring[g->wr] = w;
    if (++g->wr >= g->ring_words)
        g->wr = 0;
}

/* room for @words at a 4-word boundary (padding with no-ops, back to the start at the end) */
static void align(struct gpu *g, uint32_t words)
{
    if (g->wr + words >= g->ring_words)
        while (g->wr)
            put(g, NOP);
    while (g->wr & 3u)
        put(g, NOP);
}

/* the GPU may go on to the write position (its cache emptied first: it may hold old words of
 * the ring and the list) */
static void doorbell(struct gpu *g)
{
    icache_invalidate(g);
    wr(g, R_RB_CTRL, ((uint32_t)(uintptr_t)g->ring + g->wr * 4u) | 4u);
}

static void ring_reset(struct gpu *g)
{
    uint32_t phys = (uint32_t)(uintptr_t)g->ring;
    g->ring_words = RING_BYTES / 4u - 4u;
    g->wr = 0;
    g->id = 0;
    g->done_id = 0;
    memset(g->ring, 0, RING_BYTES);
    g->ring[g->ring_words + 0] = JUMP_ADDR;
    g->ring[g->ring_words + 1] = phys;
    g->ring[g->ring_words + 2] = JUMP_SIZE;
    g->ring[g->ring_words + 3] = RING_BYTES;
    icache_invalidate(g);
    wr(g, R_CLID, 0);
    wr(g, R_RB_CTRL, phys | 6u);
    wr(g, R_CL_ADDR, phys);
    wr(g, R_CL_SIZE, RING_BYTES);
}

/* a register write straight in the ring */
static void inline_cmd(struct gpu *g, uint32_t reg, uint32_t v)
{
    if (g->wr + 1u >= g->ring_words)
        while (g->wr)
            put(g, NOP);
    put(g, reg);
    put(g, v);
}

/* the next list number, written to CLID when the GPU gets there, and the interrupt; a jump to
 * the following ring position makes the GPU fetch it all (as NemaGFX does); then the doorbell,
 * once for all of it */
static uint32_t submit_id(struct gpu *g)
{
    g->id = g->id >= 0xffffffu ? 1u : g->id + 1u;
    inline_cmd(g, R_CLID, g->id);
    inline_cmd(g, R_INT_CTRL, 1);
    align(g, 4);
    uint32_t next = (uint32_t)(uintptr_t)g->ring + g->wr * 4u + 16u;
    put(g, JUMP_ADDR);
    put(g, next);
    put(g, JUMP_SIZE);
    put(g, RING_BYTES);
    doorbell(g);
    return g->id;
}

static void gpu_irq(int irq, void *ctx)
{
    (void)irq;
    struct gpu *g = ctx;
    uint32_t it = rd(g, R_INT_CTRL);
    if (it & 1u)
    {
        *(volatile uint32_t *)(g->base + R_INT_CTRL) = it & ~1u;
        g->done_id = rd(g, R_CLID);
        sem_give(&g->done);
    }
}

/* errors, and hold points (asked for by command lists that want the GPU cache handled:
 * ours do not, they are released at once) */
static void gpu_err_irq(int irq, void *ctx)
{
    (void)irq;
    struct gpu *g = ctx;
    uint32_t v = rd(g, R_SYS_INT);
    *(volatile uint32_t *)(g->base + R_SYS_INT) = v;
    if (v & 0xcu)
        *(volatile uint32_t *)(g->base + R_HOLD) = rd(g, R_HOLD) & ~(v & 0xcu);
    if (v & ~0xcu)
        g->errors++;
    sem_give(&g->done);
}

static int setup(struct gpu *g);

/* the GPU back to its reset state and set up again (after a list that never finished) */
static void recover(struct gpu *g)
{
    RCC->AHB5RSTR |= RCC_AHB5RSTR_GPU2DRST;
    (void)RCC->AHB5RSTR;
    RCC->AHB5RSTR &= ~RCC_AHB5RSTR_GPU2DRST;
    setup(g);
}

/* list @id or a later one is done (the numbers count to 0xffffff and over) */
static bool id_done(struct gpu *g, uint32_t id)
{
    return ((g->done_id - id) & 0xffffffu) < 0x800000u;
}

/* waits for list @id; then for the pixel writes to drain */
static int wait_done(struct gpu *g, uint32_t id)
{
    while (!id_done(g, id))
    {
        if (sem_take(&g->done, TIMEOUT_MS))
        {
            if (id_done(g, id))
                break;
            if (++g->timeouts <= 5)
                dev_err(g->dev, "list %lu not done (CLID %lu, status %08lx): reset\n", (unsigned long)id,
                        (unsigned long)rd(g, R_CLID), (unsigned long)rd(g, R_STATUS));
            recover(g);
            return -ETIMEDOUT;
        }
    }
    for (int i = 0; i < 4000 && rd(g, R_STATUS); i++)
    {
    }
    return 0;
}

static void forget(struct gpu *g)
{
    memset(g->known, 0, sizeof(g->known));
}

/* the GPU is done with the other buffer (its last list waited for): the next list goes there */
static int swap_buffers(struct gpu *g)
{
    g->cl_cur ^= 1u;
    g->cl = g->cl_buf[g->cl_cur];
    int r = 0;
    if (g->cl_id[g->cl_cur])
    {
        uint64_t t0 = time_us();
        r = wait_done(g, g->cl_id[g->cl_cur]);
        g->busy_us += time_us() - t0;
        g->cl_id[g->cl_cur] = 0;
    }
    if (r)
    {
        g->cl_id[0] = g->cl_id[1] = 0; /* (the GPU was reset: nothing runs) */
        forget(g);
    }
    return r;
}

/* gives the GPU the command list g->cl[0..n) - called from the ring, its number and the
 * interrupt after it, one doorbell - and goes on building in the other buffer */
static int kick(struct gpu *g)
{
    if (!g->n)
        return 0;
    if (!g->cl_id[g->cl_cur ^ 1u])
        while (sem_take(&g->done, 0) == 0)
        {
        }
    SCB_CleanDCache_by_Addr(g->cl, (int32_t)(g->n * 4u));
    align(g, 4);
    put(g, CMD(0, R_CL_ADDR));
    put(g, (uint32_t)(uintptr_t)g->cl);
    put(g, CMD(F_EXEC, R_CL_SIZE));
    put(g, g->n);
    g->cl_id[g->cl_cur] = submit_id(g);
    g->n = 0;
    g->wlo = UINTPTR_MAX;
    g->whi = 0;
    g->runs++;
    return swap_buffers(g);
}

/* runs the command list being built and waits until every list is done */
static int run(struct gpu *g)
{
    int r = kick(g);
    if (!r)
        r = swap_buffers(g); /* (the list just given, in the buffer kick() left) */
    g->cl_cur ^= 1u;         /* (and back: the next list goes after it in the other one) */
    g->cl = g->cl_buf[g->cl_cur];
    if (g->icache_on)
    {
        /* (the counters stop at their top: taken and restarted after each batch) */
        g->hits += g->icache->HMONR;
        g->misses += g->icache->MMONR & ICACHE_MMONR_MISSMON;
        g->icache->CR |= ICACHE_CR_HITMRST | ICACHE_CR_MISSMRST;
    }
    return r;
}

static void cmd(struct gpu *g, uint32_t flags, uint32_t reg, uint32_t v)
{
    g->cl[g->n++] = CMD(flags, reg);
    g->cl[g->n++] = v;
}

/* a register write, left out when the list has set the register to @v already */
static void w(struct gpu *g, uint32_t reg, uint32_t v)
{
    uint32_t i = reg / 4u;
    if (i < SHADOW)
    {
        uint32_t bit = 1u << (i & 31u);
        if ((g->known[i / 32u] & bit) && g->shadow[i] == v)
            return;
        g->known[i / 32u] |= bit;
        g->shadow[i] = v;
    }
    cmd(g, 0, reg, v);
}

/* the fixed fragment program slots and the blender, as NemaGFX's start-up has them */
static int setup(struct gpu *g)
{
    wr(g, 0x0e8, 0);
    wr(g, R_STATUS, 0);
    wr(g, 0x388, 1);
    ring_reset(g);
    inline_cmd(g, R_INT_CTRL, 0);
    for (uint32_t slot = 0x18; slot <= 0x1f; slot++)
    {
        inline_cmd(g, R_SHADER_ADDR, slot);
        inline_cmd(g, R_SHADER_LO, slot == 0x1f ? 0x08000002u : 0u);
        inline_cmd(g, R_SHADER_HI, slot == 0x1f ? 0x80000009u : 0u);
    }
    inline_cmd(g, R_BLEND, BLEND_SRC);
    inline_cmd(g, 0x200, 0);
    inline_cmd(g, 0x204, 0xffffffffu);
    if (rd(g, R_CONFIG_H) & (1u << 9))
    {
        wr(g, 0x374, 0);
        wr(g, 0x368, 0);
    }
    while (sem_take(&g->done, 0) == 0)
    {
    }
    forget(g);
    uint32_t id = submit_id(g);
    while (g->done_id != id)
        if (sem_take(&g->done, TIMEOUT_MS) && g->done_id != id)
            return -ETIMEDOUT;
    return 0;
}

/* ---- operations -------------------------------------------------------------------------------- */

static int gpu_format(uint32_t fmt, uint32_t *code)
{
    switch (fmt)
    {
    case GPU2D_FMT_RGB565:
        *code = FMT_RGB565;
        return 0;
    case GPU2D_FMT_XRGB8888:
    case GPU2D_FMT_ARGB8888:
        *code = FMT_BGRA8888;
        return 0;
    default:
        return -EINVAL;
    }
}

/* memory the GPU reaches: AXI SRAM, PSRAM, the NOR flash (reading) */
static bool reachable_bytes(uintptr_t a, uint64_t bytes)
{
    uint64_t e = (uint64_t)a + bytes;
    return (a >= 0x24000000u && e <= 0x24072000u) || (a >= 0x90000000u && e <= 0x92000000u) ||
           (a >= 0x70000000u && e <= 0x78000000u);
}

static bool reachable(const struct gpu2d_surface *s)
{
    return reachable_bytes(s->addr, (uint64_t)s->height * s->stride);
}

static bool cached(uintptr_t a)
{
    return (a >= 0x24000000u && a < 0x24072000u) || (a >= 0x90000000u && a < 0x91e00000u);
}

/* an operation clipped to its surfaces: the destination rectangle, the source's corner */
struct clipped
{
    int x, y, w, h, sx, sy;
};

/* a scale: the destination rectangle clipped to the destination, the source rectangle to the
 * source (sx, sy: its corner; the scales come from the op) */
static bool clip_scale(const struct gpu2d_op *o, struct clipped *c)
{
    const struct gpu2d_surface *d = &o->dst, *s = &o->src;
    int sx = o->rect.x < 0 ? 0 : o->rect.x, sy = o->rect.y < 0 ? 0 : o->rect.y;
    int sw = o->rect.x + o->rect.w > s->width ? s->width - sx : o->rect.x + o->rect.w - sx;
    int sh = o->rect.y + o->rect.h > s->height ? s->height - sy : o->rect.y + o->rect.h - sy;
    int x = o->drect.x, y = o->drect.y, w_ = o->drect.w, h = o->drect.h;
    if (x < 0)
    {
        w_ += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x + w_ > d->width)
        w_ = d->width - x;
    if (y + h > d->height)
        h = d->height - y;
    *c = (struct clipped){x, y, w_, h, sx, sy};
    return w_ > 0 && h > 0 && sw > 0 && sh > 0 && o->drect.w && o->drect.h;
}

static bool clip(const struct gpu2d_op *o, struct clipped *c)
{
    if (o->op == GPU2D_OP_SCALE)
        return clip_scale(o, c);
    const struct gpu2d_surface *d = &o->dst;
    int x, y, w_ = o->rect.w, h = o->rect.h, sx = 0, sy = 0;
    if (o->op == GPU2D_OP_FILL)
    {
        x = o->rect.x;
        y = o->rect.y;
    }
    else
    {
        const struct gpu2d_surface *s = &o->src;
        sx = o->rect.x;
        sy = o->rect.y;
        x = o->dx;
        y = o->dy;
        /* to the source first, both origins moving together */
        if (sx < 0)
        {
            x -= sx;
            w_ += sx;
            sx = 0;
        }
        if (sy < 0)
        {
            y -= sy;
            h += sy;
            sy = 0;
        }
        if (sx + w_ > s->width)
            w_ = s->width - sx;
        if (sy + h > s->height)
            h = s->height - sy;
    }
    if (x < 0)
    {
        sx -= x;
        w_ += x;
        x = 0;
    }
    if (y < 0)
    {
        sy -= y;
        h += y;
        y = 0;
    }
    if (x + w_ > d->width)
        w_ = d->width - x;
    if (y + h > d->height)
        h = d->height - y;
    *c = (struct clipped){x, y, w_, h, sx, sy};
    return w_ > 0 && h > 0;
}

/* the rows of @s an operation touches */
static uintptr_t rows_lo(const struct gpu2d_surface *s, int y)
{
    return s->addr + (uint32_t)y * s->stride;
}

static uintptr_t rows_hi(const struct gpu2d_surface *s, int y, int h)
{
    return s->addr + (uint32_t)(y + h) * s->stride;
}

/* the source rows an operation reads */
static int src_rows(const struct gpu2d_op *o, const struct clipped *c)
{
    if (o->op != GPU2D_OP_SCALE)
        return c->h;
    int sh = o->rect.y + o->rect.h > o->src.height ? o->src.height - c->sy : o->rect.y + o->rect.h - c->sy;
    return sh;
}

static bool written(const struct gpu *g, uintptr_t lo, uintptr_t hi)
{
    return lo < g->whi && g->wlo < hi;
}

static void bind_dst(struct gpu *g, const struct gpu2d_surface *d, uint32_t code)
{
    w(g, R_DST_ADDR, (uint32_t)d->addr);
    w(g, R_DST_FMT, (code << 24) | (d->stride & 0xffffu));
    w(g, R_DST_SIZE, ((uint32_t)d->height << 16) | d->width);
    w(g, R_CLIP_MIN, 0);
    w(g, R_CLIP_MAX, ((uint32_t)d->height << 16) | d->width);
}

static void rect(struct gpu *g, const struct clipped *c)
{
    w(g, R_DST_START, ((uint32_t)c->y << 16) | (uint32_t)c->x);
    w(g, R_DST_END, ((uint32_t)(c->y + c->h) << 16) | (uint32_t)(c->x + c->w));
    cmd(g, F_DRAW, R_DRAW, DRAW_RECT);
}

static void emit(struct gpu *g, const struct gpu2d_op *o, const struct clipped *c)
{
    uint32_t dcode = 0, scode = 0;
    gpu_format(o->dst.format, &dcode);
    bind_dst(g, &o->dst, dcode);
    if (o->op == GPU2D_OP_FILL)
    {
        uint32_t argb = o->argb | (o->dst.format == GPU2D_FMT_XRGB8888 ? 0xff000000u : 0u);
        w(g, R_BLEND, BLEND_SRC);
        w(g, R_TEX0_ADDR, (uint32_t)o->dst.addr);
        w(g, R_FRAG_CFG, FRAG_FILL);
        w(g, R_RASTER_COLOR, (argb & 0xff00ff00u) | ((argb >> 16) & 0xffu) | ((argb & 0xffu) << 16));
    }
    else if (o->op == GPU2D_OP_SCALE)
    {
        /* the texture matrix maps the destination rectangle onto the source rectangle */
        const struct gpu2d_surface *s = &o->src;
        bool blend = (o->flags & GPU2D_BLEND) && s->format == GPU2D_FMT_ARGB8888;
        gpu_format(s->format, &scode);
        int sw = o->rect.x + o->rect.w > s->width ? s->width - c->sx : o->rect.x + o->rect.w - c->sx;
        int sh = o->rect.y + o->rect.h > s->height ? s->height - c->sy : o->rect.y + o->rect.h - c->sy;
        float us = (float)sw / (float)o->drect.w, vs = (float)sh / (float)o->drect.h;
        w(g, R_SRC_ADDR, (uint32_t)s->addr);
        w(g, R_SRC_FMT, (scode << 24) | ((o->flags & GPU2D_FILTER) ? SAMPLE_BILINEAR : 0u) | (s->stride & 0xffffu));
        w(g, R_SRC_SIZE, ((uint32_t)s->height << 16) | s->width);
        w(g, R_BLEND, blend ? BLEND_SIMPLE : BLEND_SRC);
        w(g, R_TEX0_ADDR, 0);
        w(g, R_FRAG_CFG, FRAG_BLIT);
        w(g, R_TEX_V0, fbits((float)c->sy - (float)o->drect.y * vs));
        w(g, R_TEX_U0, fbits((float)c->sx - (float)o->drect.x * us));
        w(g, R_TEX_UX, fbits(us));
        w(g, R_TEX_UY, 0);
        w(g, R_TEX_VX, 0);
        w(g, R_TEX_VY, fbits(vs));
        w(g, R_TEX_QX, 0);
        w(g, R_TEX_QY, 0);
        w(g, R_TEX_Q0, fbits(1.0f));
    }
    else
    {
        const struct gpu2d_surface *s = &o->src;
        bool blend = (o->flags & GPU2D_BLEND) && s->format == GPU2D_FMT_ARGB8888;
        gpu_format(s->format, &scode);
        w(g, R_SRC_ADDR, (uint32_t)s->addr);
        w(g, R_SRC_FMT, (scode << 24) | (s->stride & 0xffffu)); /* point sampling, clamped */
        w(g, R_SRC_SIZE, ((uint32_t)s->height << 16) | s->width);
        w(g, R_BLEND, blend ? BLEND_SIMPLE : BLEND_SRC);
        w(g, R_TEX0_ADDR, 0);
        w(g, R_FRAG_CFG, FRAG_BLIT);
        /* 1:1, destination (x, y) shows source (sx, sy); the scales once per batch */
        w(g, R_TEX_V0, fbits((float)(c->sy - c->y)));
        w(g, R_TEX_U0, fbits((float)(c->sx - c->x)));
        w(g, R_TEX_UX, fbits(1.0f));
        w(g, R_TEX_UY, 0);
        w(g, R_TEX_VX, 0);
        w(g, R_TEX_VY, fbits(1.0f));
        w(g, R_TEX_QX, 0);
        w(g, R_TEX_QY, 0);
        w(g, R_TEX_Q0, fbits(1.0f));
    }
    rect(g, c);
}

static int d2d_op(struct gpu *g, const struct gpu2d_op *o, const struct clipped *c);
static uint32_t bpp(uint32_t fmt);

/*
 * A batch as one command list (more when it does not fit). The GPU's cache only knows what
 * it read: an operation that reads memory an earlier one of the same list wrote - blending
 * over what was just composed, copying from a surface just drawn - starts a list of its own,
 * after which the cache is emptied (doorbell()). The data cache of the processor: sources
 * cleaned, cached destinations cleaned and invalidated, once for the batch.
 */
static int gpu_batch(void *ctx, const struct gpu2d_op *ops, unsigned n)
{
    struct gpu *g = ctx;
    uint32_t src_bytes = 0, dst_bytes = 0;
    bool dst_cached = false;
    for (unsigned i = 0; i < n; i++)
    {
        const struct gpu2d_op *o = &ops[i];
        uint32_t code;
        struct clipped c;
        if ((o->op != GPU2D_OP_FILL && o->op != GPU2D_OP_BLIT && o->op != GPU2D_OP_SCALE) ||
            gpu_format(o->dst.format, &code) || !reachable(&o->dst))
            return -EINVAL;
        if (o->op != GPU2D_OP_FILL && (gpu_format(o->src.format, &code) || !reachable(&o->src)))
            return -EINVAL;
        if (!clip(o, &c))
            continue;
        if (o->op != GPU2D_OP_FILL && cached(o->src.addr))
            src_bytes += (uint32_t)src_rows(o, &c) * o->src.stride;
        if (cached(o->dst.addr))
        {
            dst_cached = true;
            dst_bytes += (uint32_t)c.h * o->dst.stride;
        }
    }
    /* before: what the processor wrote reaches memory, cached destinations hold nothing old */
    if (dst_cached && dst_bytes + src_bytes > WHOLE_CACHE)
        SCB_CleanInvalidateDCache();
    else if (src_bytes + dst_bytes > WHOLE_CACHE)
        SCB_CleanDCache();
    else
        for (unsigned i = 0; i < n; i++)
        {
            struct clipped c;
            const struct gpu2d_op *o = &ops[i];
            if (!clip(o, &c))
                continue;
            if (o->op != GPU2D_OP_FILL && cached(o->src.addr))
                SCB_CleanDCache_by_Addr((volatile void *)rows_lo(&o->src, c.sy),
                                        (int32_t)(src_rows(o, &c) * o->src.stride));
            if (cached(o->dst.addr))
                SCB_CleanInvalidateDCache_by_Addr((volatile void *)rows_lo(&o->dst, c.y), (int32_t)(c.h * o->dst.stride));
        }

    int r = 0;
    forget(g);
    g->n = 0;
    g->wlo = UINTPTR_MAX;
    g->whi = 0;
    for (unsigned i = 0; i < n && !r; i++)
    {
        const struct gpu2d_op *o = &ops[i];
        struct clipped c;
        if (!clip(o, &c))
            continue;
        if (g->d2d && o->op != GPU2D_OP_SCALE)
        {
            /* the DMA2D's, after what the GPU has to do before it */
            r = run(g);
            if (!r)
                r = d2d_op(g, o, &c);
            continue;
        }
        uintptr_t dlo = rows_lo(&o->dst, c.y), dhi = rows_hi(&o->dst, c.y, c.h);
        bool reads_old = false;
        if (g->icache_on && o->op != GPU2D_OP_FILL)
        {
            reads_old = written(g, rows_lo(&o->src, c.sy), rows_hi(&o->src, c.sy, src_rows(o, &c)));
            if ((o->flags & GPU2D_BLEND) && o->src.format == GPU2D_FMT_ARGB8888)
                reads_old = reads_old || written(g, dlo, dhi);
        }
        if (reads_old)
            g->splits++;
        if (reads_old || g->n + OP_WORDS > CL_WORDS)
            r = run(g);
        if (r)
            break;
        emit(g, o, &c);
        g->ops++;
        if (dlo < g->wlo)
            g->wlo = dlo;
        if (dhi > g->whi)
            g->whi = dhi;
    }
    if (!r)
        r = run(g);
    /* after: cached destinations as the GPU left them */
    if (dst_cached)
    {
        if (dst_bytes > WHOLE_CACHE)
            SCB_CleanInvalidateDCache();
        else
            for (unsigned i = 0; i < n; i++)
            {
                struct clipped c;
                const struct gpu2d_op *o = &ops[i];
                if (clip(o, &c) && cached(o->dst.addr))
                    SCB_CleanInvalidateDCache_by_Addr((volatile void *)rows_lo(&o->dst, c.y),
                                                      (int32_t)(c.h * o->dst.stride));
            }
    }
    g->batches++;
    return r;
}

static int gpu_fill(void *ctx, const struct gpu2d_surface *dst, const struct gpu2d_rect *r, uint32_t argb)
{
    struct gpu2d_op o;
    memset(&o, 0, sizeof(o));
    o.op = GPU2D_OP_FILL;
    o.dst = *dst;
    o.rect = *r;
    o.argb = argb;
    return gpu_batch(ctx, &o, 1);
}

static int gpu_blit(void *ctx, const struct gpu2d_surface *dst, int16_t dx, int16_t dy, const struct gpu2d_surface *src,
                    const struct gpu2d_rect *sr, uint32_t flags)
{
    struct gpu2d_op o;
    memset(&o, 0, sizeof(o));
    o.op = GPU2D_OP_BLIT;
    o.dst = *dst;
    o.src = *src;
    o.rect = *sr;
    o.dx = dx;
    o.dy = dy;
    o.flags = flags;
    return gpu_batch(ctx, &o, 1);
}

static void gpu_stats(void *ctx, struct gpu2d_stats *st)
{
    struct gpu *g = ctx;
    strcpy(st->name, g->d2d ? "DMA2D+NeoChrom" : "NeoChrom");
    st->batches = g->batches;
    st->ops = g->ops + g->d2d_ops + g->prims;
    st->lists = g->runs;
    st->busy_us = (uint32_t)g->busy_us;
    st->cache_hits = (uint32_t)g->hits;
    st->cache_misses = (uint32_t)g->misses;
    st->caps = GPU2D_CAP_SCALE | GPU2D_CAP_PRIMS;
}

/* ---- primitives -------------------------------------------------------------------------------- */

/*
 * The destination is drawn a band at a time in on-chip memory, the tile (AXI SRAM1's upper
 * half, which stays AXI unless the option bytes give ITCM 192 KB): with the destination in the
 * PSRAM the GPU took several times longer for each primitive - it writes and blends pixel by
 * pixel there. The DMA2D brings the band in (unless the primitives cover all of it) and takes it
 * back, in whole lines. Each band gets the primitives that reach into it, in their order.
 */

static uint64_t array_bytes(const struct gpu2d_texarray *t)
{
    return (uint64_t)t->s.stride * t->s.height * t->layers;
}

static void clean_range(uintptr_t a, uint32_t bytes)
{
    if (cached(a) && bytes)
        SCB_CleanDCache_by_Addr((volatile void *)(a & ~31u), (int32_t)(bytes + (a & 31u)));
}

/* a primitive the GPU may draw: its texture and layer there, its numbers (an AGAIN one: only
 * its head was read) */
static bool prim_ok(const struct gpu2d_prim *q, const struct gpu2d_texarray *tex, unsigned ntex)
{
    if (q->tex >= ntex || q->layer >= tex[q->tex].layers || q->blend > GPU2D_PRIM_PREMUL)
        return false;
    bool again = q->flags & GPU2D_PRIM_AGAIN;
    const float *f = again ? q->again : q->m;
    for (int k = 0; k < (again ? 4 : 9); k++)
        if ((fbits(f[k]) & 0x7f800000u) == 0x7f800000u)
            return false; /* (infinite, not a number) */
    return true;
}

/* the rows a primitive can reach: [*lo, *hi) */
static void prim_rows(const struct gpu2d_prim *q, int *lo, int *hi)
{
    int n = (q->flags & GPU2D_PRIM_QUAD) ? 4 : 3, a = q->y[0], b = q->y[0];
    for (int k = 1; k < n; k++)
    {
        a = q->y[k] < a ? q->y[k] : a;
        b = q->y[k] > b ? q->y[k] : b;
    }
    *lo = (a >> 4) - 1;
    *hi = (b >> 4) + 2;
}

/* @n_ bytes at *@p (grown, the old contents lost) */
static bool room(void **p, uint32_t *cap, uint32_t n_)
{
    if (*cap >= n_)
        return true;
    kfree(*p);
    *p = kmalloc(n_, KM_LARGE);
    *cap = *p ? n_ : 0;
    return *p != NULL;
}

/* The bands each primitive reaches (an AGAIN one: those of the one before) and, band after
 * band, the primitives in each, in their order: @first[b] .. @first[b + 1] in g->bin. False:
 * no memory for it. */
static bool bin_prims(struct gpu *g, const struct gpu2d_prim *p, const int16_t *rows, unsigned n, int y0, int band, int nb,
                      uint32_t *first)
{
    if (!room((void **)&g->pband, &g->pband_cap, 2u * n))
        return false;
    uint32_t count[GPU_BANDS_MAX + 1];
    memset(count, 0, sizeof(count));
    uint32_t total = 0;
    int b0 = 0, b1 = -1;
    for (unsigned i = 0; i < n; i++)
    {
        struct gpu2d_prim q;
        if (rows)
        {
            q.flags = 0; /* (the caller says which rows: the primitive is not read) */
        }
        else
        {
            __builtin_prefetch(&p[i + 8]);
            memcpy(&q, &p[i], offsetof(struct gpu2d_prim, m)); /* (its head: flags to the points) */
        }
        if (!(q.flags & GPU2D_PRIM_AGAIN))
        {
            int lo, hi;
            if (rows)
            {
                lo = rows[2 * i];
                hi = rows[2 * i + 1];
            }
            else
            {
                prim_rows(&q, &lo, &hi);
            }
            b0 = (lo - y0) / band;
            b1 = (hi - 1 - y0) / band;
            b0 = b0 < 0 ? 0 : b0;
            b1 = b1 >= nb ? nb - 1 : b1;
        }
        g->pband[2 * i] = (uint8_t)b0;
        g->pband[2 * i + 1] = (uint8_t)(b1 < b0 ? 255 : b1);
        for (int b = b0; b <= b1; b++)
            count[b]++;
        if (b1 >= b0)
            total += (uint32_t)(b1 - b0 + 1);
    }
    if (!room((void **)&g->bin, &g->bin_cap, 2u * total))
        return false;
    first[0] = 0;
    for (int b = 0; b < nb; b++)
        first[b + 1] = first[b] + count[b];
    uint32_t at[GPU_BANDS_MAX];
    memcpy(at, first, sizeof(uint32_t) * (unsigned)nb);
    for (unsigned i = 0; i < n; i++)
        for (int b = g->pband[2 * i]; b <= g->pband[2 * i + 1] && g->pband[2 * i + 1] != 255; b++)
            g->bin[at[b]++] = (uint16_t)i;
    return true;
}

/* the texture and blending through the shadow (they repeat), the matrix and the points
 * straight (they do not: never through w(), so the shadow knows nothing of them); an AGAIN
 * primitive: the matrix of @last scaled, the points as they are */
static void emit_prim(struct gpu *g, struct gpu2d_prim *q, const struct gpu2d_texarray *t, uint32_t code,
                      const struct gpu2d_prim *last)
{
    static const uint16_t MATRIX[9] = {R_TEX_UX, R_TEX_UY, R_TEX_U0, R_TEX_VX, R_TEX_VY,
                                       R_TEX_V0, R_TEX_QX, R_TEX_QY, R_TEX_Q0};
    bool quad = q->flags & GPU2D_PRIM_QUAD;
    w(g, R_SRC_ADDR, (uint32_t)t->s.addr + (uint32_t)q->layer * t->s.height * t->s.stride);
    w(g, R_SRC_FMT, (code << 24) | ((q->flags & GPU2D_PRIM_SMOOTH) ? SAMPLE_BILINEAR : 0u) |
                        ((q->flags & GPU2D_PRIM_REPEAT) ? SAMPLE_REPEAT : 0u) | (t->s.stride & 0xffffu));
    w(g, R_SRC_SIZE, ((uint32_t)t->s.height << 16) | t->s.width);
    w(g, R_BLEND, q->blend == GPU2D_PRIM_ALPHA ? BLEND_SIMPLE : q->blend == GPU2D_PRIM_PREMUL ? BLEND_PREMUL : BLEND_SRC);
    uint32_t *o = g->cl + g->n;
    if (q->flags & GPU2D_PRIM_AGAIN)
    {
        /* u' = su u + ou: the rows of u and v scaled, the divisor's added */
        float su = q->again[0], ou = q->again[1], sv = q->again[2], ov = q->again[3];
        for (int k = 0; k < 3; k++)
        {
            q->m[k] = su * last->m[k] + ou * last->m[6 + k];
            q->m[3 + k] = sv * last->m[3 + k] + ov * last->m[6 + k];
            q->m[6 + k] = last->m[6 + k];
        }
        quad = last->flags & GPU2D_PRIM_QUAD;
    }
    const uint32_t *m = (const uint32_t *)q->m;
    for (int k = 0; k < 9; k++)
    {
        *o++ = MATRIX[k];
        *o++ = m[k];
    }
    for (int k = 0; k < (quad ? 4 : 3) && !(q->flags & GPU2D_PRIM_AGAIN); k++)
    {
        *o++ = R_P0_X + 16u * (uint32_t)k;
        *o++ = (uint32_t)((int32_t)q->x[k] * 4096);
        *o++ = R_P0_X + 16u * (uint32_t)k + 4u;
        *o++ = (uint32_t)((int32_t)q->y[k] * 4096);
    }
    *o++ = CMD(F_DRAW, R_DRAW);
    *o++ = quad ? DRAW_QUAD : DRAW_TRIANGLE;
    g->n = (uint32_t)(o - g->cl);
}

/* a band between the destination and the tile, by the DMA2D */
static int band_copy(struct gpu *g, const struct gpu2d_surface *dst, const struct gpu2d_surface *tile, int x, int y,
                     int w_, int h, bool in)
{
    struct gpu2d_op o;
    memset(&o, 0, sizeof(o));
    o.op = GPU2D_OP_BLIT;
    o.dst = in ? *tile : *dst;
    o.src = in ? *dst : *tile;
    struct clipped c = in ? (struct clipped){0, 0, w_, h, x, y} : (struct clipped){x, y, w_, h, 0, 0};
    return d2d_op(g, &o, &c);
}

static int gpu_prims(void *ctx, const struct gpu2d_surface *dst, const struct gpu2d_rect *clip, uint32_t flags,
                     const struct gpu2d_prim *p, const int16_t *rows, unsigned n, const struct gpu2d_texarray *tex,
                     unsigned ntex)
{
    struct gpu *g = ctx;
    uint32_t dcode, codes[GPU2D_PRIM_TEXARRAYS];
    if (gpu_format(dst->format, &dcode) || !reachable(dst) || ntex > GPU2D_PRIM_TEXARRAYS)
        return -EINVAL;
    uint32_t tex_bytes = 0;
    for (unsigned i = 0; i < ntex; i++)
    {
        if (gpu_format(tex[i].s.format, &codes[i]) || !reachable_bytes(tex[i].s.addr, array_bytes(&tex[i])))
            return -EINVAL;
        if (cached(tex[i].s.addr))
            tex_bytes += (uint32_t)array_bytes(&tex[i]); /* (each one under 32 MB: reachable) */
    }
    int x0 = clip->x < 0 ? 0 : clip->x, y0 = clip->y < 0 ? 0 : clip->y;
    int x1 = clip->x + clip->w > dst->width ? dst->width : clip->x + clip->w;
    int y1 = clip->y + clip->h > dst->height ? dst->height : clip->y + clip->h;
    if (x0 >= x1 || y0 >= y1 || !n)
        return 0;
    /* the textures as the processor wrote them; nothing old of the destination cached */
    uint32_t dst_bytes = cached(dst->addr) ? (uint32_t)(y1 - y0) * dst->stride : 0u;
    if (tex_bytes + dst_bytes > WHOLE_CACHE)
    {
        SCB_CleanInvalidateDCache();
    }
    else
    {
        for (unsigned i = 0; i < ntex; i++)
            clean_range(tex[i].s.addr, (uint32_t)array_bytes(&tex[i]));
        if (dst_bytes)
            SCB_CleanInvalidateDCache_by_Addr((volatile void *)rows_lo(dst, y0), (int32_t)dst_bytes);
    }

    /* bands of the tile, or the whole destination at once */
    uint32_t b = bpp(dst->format), tstride = ((uint32_t)(x1 - x0) * b + 7u) & ~7u;
    int band = g->tile && g->d2d ? (int)(g->tile_bytes / tstride) : 0;
    if (band < 8 || (y1 - y0 + band - 1) / band > GPU_BANDS_MAX)
        band = y1 - y0;
    bool tiled = band < y1 - y0 || (g->tile && g->d2d);
    int nb = (y1 - y0 + band - 1) / band;
    static uint32_t first[GPU_BANDS_MAX + 1];
    if (nb > 1 && !bin_prims(g, p, rows, n, y0, band, nb, first))
    {
        band = y1 - y0; /* (no memory for the bins: the whole destination at once, in the PSRAM) */
        nb = 1;
        tiled = false;
    }
    struct gpu2d_surface tile = {(uintptr_t)g->tile, (uint16_t)(x1 - x0), 0, tstride, dst->format};
    forget(g);
    g->n = 0;
    int r = 0;
    for (int bi = 0, by = y0; by < y1 && !r; bi++, by += band)
    {
        int bh = y1 - by < band ? y1 - by : band;
        uint32_t base = (uint32_t)dst->addr, stride = dst->stride;
        if (tiled)
        {
            tile.height = (uint16_t)bh;
            if (!(flags & GPU2D_PRIMS_DISCARD))
                r = band_copy(g, dst, &tile, x0, by, x1 - x0, bh, true);
            /* (the band's top left pixel lands at the tile's start) */
            base = (uint32_t)(uintptr_t)g->tile - (uint32_t)by * tstride - (uint32_t)x0 * b;
            stride = tstride;
        }
        w(g, R_DST_ADDR, base);
        w(g, R_DST_FMT, (dcode << 24) | (stride & 0xffffu));
        w(g, R_DST_SIZE, ((uint32_t)dst->height << 16) | dst->width);
        w(g, R_CLIP_MIN, ((uint32_t)by << 16) | (uint32_t)x0);
        w(g, R_CLIP_MAX, ((uint32_t)(by + bh) << 16) | (uint32_t)x1);
        w(g, R_TEX0_ADDR, 0);
        w(g, R_FRAG_CFG, FRAG_BLIT);
        /* the band's primitives (all of them in one band) */
        uint32_t k0 = nb > 1 ? first[bi] : 0, k1 = nb > 1 ? first[bi + 1] : n;
        struct gpu2d_prim last;
        bool have_last = false;
        for (uint32_t k = k0; k < k1 && !r; k++)
        {
            unsigned i = nb > 1 ? g->bin[k] : k;
            unsigned ahead = nb > 1 ? (k + 4 < k1 ? g->bin[k + 4] : i) : i + 4;
            __builtin_prefetch(&p[ahead]);
            __builtin_prefetch((const uint8_t *)&p[ahead] + 32);
            /* (the caller's memory: read once, then checked; of an AGAIN one its head only) */
            struct gpu2d_prim q;
            memcpy(&q, &p[i], offsetof(struct gpu2d_prim, m));
            bool again = q.flags & GPU2D_PRIM_AGAIN;
            if (!again)
                memcpy(&q.m, &p[i].m, sizeof(q) - offsetof(struct gpu2d_prim, m));
            if (!prim_ok(&q, tex, ntex) || (again && !have_last))
            {
                have_last = false;
                continue;
            }
            if (g->n + PRIM_WORDS > CL_WORDS)
                r = kick(g);
            emit_prim(g, &q, &tex[q.tex], codes[q.tex], &last);
            g->prims++;
            if (!again)
            {
                last = q;
                have_last = true;
            }
        }
        if (!r)
            r = run(g);
        if (!r && tiled)
            r = band_copy(g, dst, &tile, x0, by, x1 - x0, bh, false);
    }
    if (r)
        run(g);
    if (dst_bytes)
    {
        if (dst_bytes > WHOLE_CACHE)
            SCB_CleanInvalidateDCache();
        else
            SCB_CleanInvalidateDCache_by_Addr((volatile void *)rows_lo(dst, y0), (int32_t)dst_bytes);
    }
    g->batches++;
    return r;
}

static const struct gpu2d_ops gpu_ops = {gpu_fill, gpu_blit, gpu_batch, gpu_stats, gpu_prims};

/* ---- the DMA2D ---------------------------------------------------------------------------------- */

static void d2d_irq(int irq, void *ctx)
{
    (void)irq;
    struct gpu *g = ctx;
    g->d2d->CR &= ~(DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE);
    sem_give(&g->d2d_done);
}

static uint32_t d2d_mode(uint32_t fmt)
{
    return fmt == GPU2D_FMT_RGB565 ? D2D_RGB565 : D2D_ARGB8888;
}

static uint32_t bpp(uint32_t fmt)
{
    return fmt == GPU2D_FMT_RGB565 ? 2u : 4u;
}

/* starts the operation programmed and waits for its end */
static int d2d_start(struct gpu *g, uint32_t mode, uint32_t px)
{
    DMA2D_TypeDef *d = g->d2d;
    bool poll = px <= D2D_POLL_PX;
    d->IFCR = 0x3fu;
    if (!poll)
        while (sem_take(&g->d2d_done, 0) == 0)
        {
        }
    d->CR = (mode << DMA2D_CR_MODE_Pos) | (poll ? 0u : DMA2D_CR_TCIE | DMA2D_CR_TEIE | DMA2D_CR_CEIE) | DMA2D_CR_START;
    if (poll)
    {
        uint32_t t0 = cpu_cycles();
        while ((d->CR & DMA2D_CR_START) && cpu_cycles() - t0 < SystemCoreClock / 20u)
        {
        }
    }
    else
    {
        sem_take(&g->d2d_done, TIMEOUT_MS);
    }
    uint32_t isr = d->ISR;
    if ((d->CR & DMA2D_CR_START) || (isr & D2D_ERRORS))
    {
        d->CR |= DMA2D_CR_ABORT;
        for (int i = 0; i < 100000 && (d->CR & DMA2D_CR_START); i++)
        {
        }
        d->IFCR = 0x3fu;
        if (++g->d2d_errors <= 5)
            dev_err(g->dev, "DMA2D: %s (ISR %02lx, mode %lu, %lu pixels)\n", isr & D2D_ERRORS ? "error" : "timeout",
                    (unsigned long)isr, (unsigned long)mode, (unsigned long)px);
        return isr & D2D_ERRORS ? -EIO : -ETIMEDOUT;
    }
    g->d2d_ops++;
    return 0;
}

static int d2d_op(struct gpu *g, const struct gpu2d_op *o, const struct clipped *c)
{
    DMA2D_TypeDef *d = g->d2d;
    const struct gpu2d_surface *dst = &o->dst;
    uint32_t db = bpp(dst->format);
    d->OMAR = (uint32_t)(dst->addr + (uint32_t)c->y * dst->stride + (uint32_t)c->x * db);
    d->OOR = dst->stride / db - (uint32_t)c->w;
    d->OPFCCR = d2d_mode(dst->format);
    d->NLR = ((uint32_t)c->w << DMA2D_NLR_PL_Pos) | (uint32_t)c->h;
    uint32_t px = (uint32_t)c->w * (uint32_t)c->h;
    if (o->op == GPU2D_OP_FILL)
    {
        uint32_t argb = o->argb | (dst->format == GPU2D_FMT_XRGB8888 ? 0xff000000u : 0u);
        d->OCOLR = dst->format == GPU2D_FMT_RGB565
                       ? ((argb >> 8) & 0xf800u) | ((argb >> 5) & 0x07e0u) | ((argb >> 3) & 0x001fu)
                       : argb;
        return d2d_start(g, D2D_R2M, px);
    }
    const struct gpu2d_surface *src = &o->src;
    uint32_t sb = bpp(src->format);
    d->FGMAR = (uint32_t)(src->addr + (uint32_t)c->sy * src->stride + (uint32_t)c->sx * sb);
    d->FGOR = src->stride / sb - (uint32_t)c->w;
    d->FGPFCCR = d2d_mode(src->format); /* (its own alpha) */
    if ((o->flags & GPU2D_BLEND) && src->format == GPU2D_FMT_ARGB8888)
    {
        d->BGMAR = d->OMAR;
        d->BGOR = d->OOR;
        d->BGPFCCR = d2d_mode(dst->format);
        return d2d_start(g, D2D_M2M_BLEND, px);
    }
    return d2d_start(g, sb == db ? D2D_M2M : D2D_M2M_PFC, px);
}

/* ---- the device -------------------------------------------------------------------------------- */

static void free_mem(void *arg)
{
    kfree(arg);
}

static int gpu_probe(struct device *dev)
{
    struct gpu *g = devm_kzalloc(dev, sizeof(*g), 0);
    if (!g)
        return -ENOMEM;
    g->dev = dev;
    g->base = (uintptr_t)device_map(dev, 0);
    g->icache = device_map(dev, 1);
    if (!g->base)
        return -EINVAL;
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, "gpu", &clk);
    if (r)
        return r;
    g->irq = device_get_irq(dev, 0);
    g->irq_err = device_get_irq(dev, 1);
    if (g->irq < 0 || g->irq_err < 0)
        return -EINVAL;
    uint32_t id = rd(g, R_ID), cfg = rd(g, R_CONFIG);
    if (id != GPU_ID)
    {
        dev_err(dev, "not a NeoChrom (ID %08lx)\n", (unsigned long)id);
        return -ENODEV;
    }
    if (!(cfg & CFG_ROP_BLENDER))
    {
        dev_err(dev, "no ROP blender (config %08lx)\n", (unsigned long)cfg);
        return -ENODEV;
    }
    icache_set(g, false); /* (on after the start-up, unless the device tree says no) */
    if (!of_property_read_bool(dev->of_node, "crtos,no-dma2d"))
    {
        g->d2d = device_map(dev, 2);
        g->irq_d2d = device_get_irq(dev, 2);
        struct clk *dclk;
        if (!g->d2d || g->irq_d2d < 0 || devm_clk_get_enabled(dev, "dma2d", &dclk))
            g->d2d = NULL;
    }
    sem_init(&g->d2d_done, 0, 1);
    /* the tile (reg 3): AXI SRAM1's upper half - AXI SRAM unless the option bytes give all of
     * SRAM1 to the ITCM (ITCM_AXI_SHARE 2: 192 KB) */
    uint32_t taddr, tsize;
    uint32_t share = (FLASH->OBW2SR & FLASH_OBW2SR_ITCM_AXI_SHARE_Msk) >> FLASH_OBW2SR_ITCM_AXI_SHARE_Pos;
    if (g->d2d && share != 2 && !device_get_reg(dev, 3, &taddr, &tsize))
    {
        g->tile = (uint8_t *)(uintptr_t)taddr;
        g->tile_bytes = tsize;
    }

    uint32_t *mem = kmalloc_aligned(RING_BYTES, 64, KM_NOCACHE | KM_DMA);
    uint32_t *lists = kmalloc_aligned(2u * CL_WORDS * 4u, 32, KM_DMA); /* (on chip, if there is room) */
    if (!mem || !lists)
    {
        kfree(mem);
        kfree(lists);
        return -ENOMEM;
    }
    devm_add_action(dev, free_mem, mem);
    devm_add_action(dev, free_mem, lists);
    g->ring = mem;
    g->cl_buf[0] = lists;
    g->cl_buf[1] = lists + CL_WORDS;
    g->cl = g->cl_buf[0];
    sem_init(&g->done, 0, 1);

    r = irq_request(g->irq, gpu_irq, g, 7, dev->name);
    if (!r)
    {
        r = irq_request(g->irq_err, gpu_err_irq, g, 7, dev->name);
        if (r)
            irq_free(g->irq);
    }
    if (!r && g->d2d && irq_request(g->irq_d2d, d2d_irq, g, 7, dev->name))
        g->d2d = NULL;
    if (r)
        return r;
    r = setup(g);
    if (r)
    {
        dev_err(dev, "the GPU does not run its command ring\n");
    }
    else
    {
        icache_set(g, !of_property_read_bool(dev->of_node, "crtos,no-gpu-cache"));
        r = gpu2d_register(&gpu_ops, g);
    }
    if (r)
    {
        icache_set(g, false);
        irq_free(g->irq);
        irq_free(g->irq_err);
        if (g->d2d)
            irq_free(g->irq_d2d);
        return r;
    }
    dev_set_drvdata(dev, g);
    dev_info(dev, "NeoChrom GPU2D (revision %08lx, %s)%s for /dev/gpu2d, %lu KB on chip for primitives\n",
             (unsigned long)rd(g, R_REVISION), g->icache_on ? "its read cache on" : "no read cache",
             g->d2d ? " and DMA2D" : "", (unsigned long)(g->tile_bytes / 1024u));
    return 0;
}

static void gpu_remove(struct device *dev)
{
    struct gpu *g = dev_get_drvdata(dev);
    gpu2d_unregister();
    icache_set(g, false);
    irq_free(g->irq);
    irq_free(g->irq_err);
    if (g->d2d)
        irq_free(g->irq_d2d);
    if (g->timeouts || g->errors)
        dev_info(dev, "%lu batches in %lu lists (%lu split for the cache), %lu timeouts, %lu errors\n",
                 (unsigned long)g->batches, (unsigned long)g->runs, (unsigned long)g->splits,
                 (unsigned long)g->timeouts, (unsigned long)g->errors);
}

static const struct of_device_id gpu_ids[] = {
    {"st,stm32-gpu2d", NULL},
    {NULL, NULL},
};

static struct driver gpu_driver = {
    .name = "gpu2d-stm32",
    .of_match_table = gpu_ids,
    .probe = gpu_probe,
    .remove = gpu_remove,
};

static int init(void)
{
    return driver_register(&gpu_driver);
}

static void fini(void)
{
    driver_unregister(&gpu_driver);
}

MODULE("gpu2d-stm32", "STM32 NeoChrom GPU2D 2D accelerator", init, fini);
