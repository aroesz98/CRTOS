/*
 * snes_idle.cpp - the SNES's 65816 waiting, fast-forwarded (snes_idle.h), with exactly the
 * interpreter's results.
 *
 * Games spend much of a frame waiting for the next NMI or IRQ: in WAI, or in a loop like
 * "wait: LDA $10 / BEQ wait" that polls memory an interrupt handler will change. Emulated
 * instruction by instruction that costs a lot of host time for nothing.
 *
 * Wait loops: when a branch goes back (in the same 4 KB of the map), the instructions from its
 * target to it are decoded once (per place and mode); a loop of only reads, compares,
 * transfers and branches is a wait loop candidate. At its branch back, the state an iteration
 * could change is compared with the previous iteration's: registers, flags, the open bus,
 * and the counts of events, interrupts and register reads (g_idle). If nothing changed and
 * no event or interrupt came in between, every further iteration will be the same one, until
 * an event, the IRQ timer or an NMI: the iterations that end before the first of those are
 * skipped at once (CPU.Cycles += n * the cycles of one). Memory cannot change meanwhile -
 * the loop writes nothing, DMA and HDMA only come from writes and events.
 *
 * WAI: the main loop adds ONE_CYCLE per step until something happens; the steps before the
 * next event, IRQ or NMI are added at once.
 */
#include <stddef.h>
#include <string.h>

#include "snes9x.h"
#include "memmap.h"
#include "cpuexec.h"
#include "snes_idle.h"

struct snes_idle_shared g_idle;

static struct snes_idle_stats s_stats;

#define LOOP_MAX 12             /* bytes of code a wait loop may have (core/cpumacro.h too) */
#define CACHE_BITS 8

void snes_idle_enable(int on)
{
    g_idle.enabled = on;
}

/* ---- the loops found --------------------------------------------------------------------------- */

struct verdict {
    const uint8 *at;            /* the branch in the ROM or RAM */
    const struct SOpcodes *table;
    uint16 target;
    uint8 wait;                 /* a wait loop candidate */
};

static struct verdict s_cache[1u << CACHE_BITS];

void snes_idle_reset(void)
{
    memset(s_cache, 0, sizeof(s_cache));
    memset(&s_stats, 0, sizeof(s_stats));
}

const struct snes_idle_stats *snes_idle_stats(void)
{
    return &s_stats;
}

/* whether @op only reads memory and changes no more than registers and flags */
static bool pure(uint8 op)
{
    switch (op) {
    /* branches (BRA, JMP a: only back to the start) */
    case 0x10: case 0x30: case 0x50: case 0x70: case 0x90: case 0xB0: case 0xD0: case 0xF0: case 0x80:
    /* LDA LDX LDY */
    case 0xA9: case 0xA5: case 0xB5: case 0xA1: case 0xB1: case 0xA7: case 0xB7: case 0xAD:
    case 0xBD: case 0xB9: case 0xAF: case 0xBF: case 0xA3: case 0xB3: case 0xB2:
    case 0xA2: case 0xA6: case 0xB6: case 0xAE: case 0xBE:
    case 0xA0: case 0xA4: case 0xB4: case 0xAC: case 0xBC:
    /* CMP CPX CPY BIT */
    case 0xC9: case 0xC5: case 0xD5: case 0xC1: case 0xD1: case 0xC7: case 0xD7: case 0xCD:
    case 0xDD: case 0xD9: case 0xCF: case 0xDF: case 0xC3: case 0xD3: case 0xD2:
    case 0xE0: case 0xE4: case 0xEC: case 0xC0: case 0xC4: case 0xCC:
    case 0x89: case 0x24: case 0x34: case 0x2C: case 0x3C:
    /* AND ORA EOR */
    case 0x29: case 0x25: case 0x35: case 0x21: case 0x31: case 0x27: case 0x37: case 0x2D:
    case 0x3D: case 0x39: case 0x2F: case 0x3F: case 0x23: case 0x33: case 0x32:
    case 0x09: case 0x05: case 0x15: case 0x01: case 0x11: case 0x07: case 0x17: case 0x0D:
    case 0x1D: case 0x19: case 0x0F: case 0x1F: case 0x03: case 0x13: case 0x12:
    case 0x49: case 0x45: case 0x55: case 0x41: case 0x51: case 0x47: case 0x57: case 0x4D:
    case 0x5D: case 0x59: case 0x4F: case 0x5F: case 0x43: case 0x53: case 0x52:
    /* transfers, XBA, flags, NOP */
    case 0xAA: case 0xA8: case 0x8A: case 0x98: case 0x9B: case 0xBB: case 0xBA: case 0x5B:
    case 0x7B: case 0x1B: case 0x3B: case 0xEB: case 0x18: case 0x38: case 0xB8: case 0xEA:
        return true;
    default:
        return false;
    }
}

/* whether the code from @target to the branch at @branch (@code: the branch's bytes) is a
 * wait loop candidate */
static bool classify(const uint8 *code, uint32 target, uint32 branch)
{
    const uint8 *p = code - (branch - target);
    uint32 pc = target;
    while (pc < branch) {
        if (!pure(*p))
            return false;
        uint32 len = ICPU.S9xOpLengths[*p];
        pc += len;
        p += len;
    }
    return pc == branch; /* the decoding lands on the branch itself */
}

/* ---- the fast-forwarding ----------------------------------------------------------------------- */

/* everything an iteration of a wait loop could change */
struct state {
    const uint8 *loop;
    uint32 io, pbpc;
    uint16 a, x, y, s, d, p;
    uint8 db, c, z, n, v, open_bus;
};

static struct state s_last;
static int32 s_last_cycles;
static uint32 s_last_events;

static void capture(struct state *st, const uint8 *loop)
{
    memset(st, 0, sizeof(*st));
    st->loop = loop;
    st->io = g_idle.io;
    st->pbpc = Registers.PBPC;
    st->a = Registers.A.W;
    st->x = Registers.X.W;
    st->y = Registers.Y.W;
    st->s = Registers.S.W;
    st->d = Registers.D.W;
    st->p = Registers.P.W;
    st->db = Registers.DB;
    st->c = ICPU._Carry;
    st->z = ICPU._Zero;
    st->n = ICPU._Negative;
    st->v = ICPU._Overflow;
    st->open_bus = OpenBus;
}

/* the first cycle count at which an event, the IRQ timer or a pending NMI does something */
static int32 next_stop(void)
{
    int32 limit = CPU.NextEvent;
    if (Timings.NextIRQTimer < limit)
        limit = Timings.NextIRQTimer;
    if (CPU.NMIPending && Timings.NMITriggerPos < limit)
        limit = Timings.NMITriggerPos;
    return limit;
}

void S9xIdleBackEdge(uint32_t branch)
{
    if (!g_idle.enabled || !CPU.PCBase)
        return;
    uint32 target = Registers.PCw;
    if (branch - target > LOOP_MAX || ((branch ^ target) & ~MEMMAP_MASK))
        return;
    const uint8 *at = CPU.PCBase + branch;
    struct verdict *v = &s_cache[((uintptr_t)at >> 1 ^ (uintptr_t)at >> (CACHE_BITS + 1)) & ((1u << CACHE_BITS) - 1)];
    if (v->at != at || v->table != ICPU.S9xOpcodes || v->target != target) {
        v->at = at;
        v->table = ICPU.S9xOpcodes;
        v->target = (uint16)target;
        v->wait = classify(at, target, branch);
        s_stats.loops += v->wait;
    }
    if (!v->wait)
        return;
    /* the main loop is about to take an interrupt: nothing to skip */
    if ((CPU.IRQLine || CPU.IRQExternal) && !CheckFlag(IRQ))
        return;
    bool same_run = s_last.loop == at && g_idle.events == s_last_events;
    if (same_run && s_last.io != g_idle.io) {
        /* it reads a register every time round (a port of the APU, the H/V status...): no
         * wait loop to skip, and no more checks at this branch */
        v->wait = 0;
        return;
    }
    struct state now;
    capture(&now, at);
    if (same_run && !memcmp(&now, &s_last, sizeof(now))) {
        int32 per = CPU.Cycles - s_last_cycles;
        if (per > 0) {
            int32 k = (next_stop() - 1 - CPU.Cycles) / per;
            if (k > 0) {
                CPU.Cycles += k * per;
                s_stats.skips++;
                s_stats.loop_cycles += (uint64)k * (uint32)per;
            }
        }
    }
    s_last = now;
    s_last_cycles = CPU.Cycles;
    s_last_events = g_idle.events;
}

void S9xIdleWaitSkip(void)
{
    /* each WAI step of the main loop: its checks, then ONE_CYCLE and the events due */
    if (!g_idle.enabled)
        return;
    int32 k = (next_stop() - 1 - CPU.Cycles) / ONE_CYCLE;
    if (k > 0) {
        CPU.Cycles += k * ONE_CYCLE;
        s_stats.wai_cycles += (uint64)k * ONE_CYCLE;
    }
}
