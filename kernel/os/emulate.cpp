/*
 * kernel/os/emulate.cpp - carrying out a program's load or store instruction in software.
 *
 * Emulated memory (vmem.cpp) has addresses but no memory behind them: a program's access
 * there raises a MemManage fault, and the fault handler decodes the Thumb-2 instruction and
 * performs it on the page's copy in the kernel's page cache. Everything a compiler generates
 * for data is covered: LDR/STR of words, halfwords and bytes (signed too) in all addressing
 * modes (immediate, register, pre- and post-indexed, write-back), LDRD/STRD, LDM/STM (IA and
 * DB), LDREX/STREX (a software monitor), and the floating-point VLDR/VSTR/VLDM/VSTM. Loads
 * into the PC branch. SP-relative forms (the stack is never emulated memory), literal loads
 * and anything else are refused, and so is an access that is not entirely in memory the
 * callback accepts - the caller then treats the fault as a real one.
 *
 * All the bytes one instruction touches are contiguous (at most 128, so at most two pages).
 * Both pages must be present before anything changes: an instruction whose page is missing
 * is left untouched and restarted once the page is in (EMU_MISS).
 *
 * Registers: R0-R3, R12, LR, PC and xPSR are in the exception frame, R4-R11 where the fault
 * entry saved them. With a floating-point frame (EXC_RETURN bit 4 clear), S0-S15 are in the
 * frame (their lazy save is forced first) and S16-S31 still in the FPU.
 */
#define CRTOS_KERNEL 1
#include "kernel.h"
#include "fsl_device_registers.h"
#include <string.h>

#define FR_SP_OFFSET_BASIC 8u   /* words in a frame without FP state */
#define FR_SP_OFFSET_FP    26u

/* ---- registers ---------------------------------------------------------------------------- */

static uint32_t *core_reg(struct emu *e, unsigned n)
{
    switch (n) {
    case 0: case 1: case 2: case 3: return &e->frame[n];
    case 12: return &e->frame[4];
    case 14: return &e->frame[5];
    case 13: case 15: return nullptr; /* SP and PC: never a base or data register here */
    default: return &e->regs[n - 4];
    }
}

/* S16-S31 are live in the FPU (no kernel code on this path uses them) */
#define S_HI(n) \
    case n: if (set) __asm volatile("vmov s" #n ", %0" ::"r"(v)); else __asm volatile("vmov %0, s" #n : "=r"(v)); break;

static uint32_t s_hi(unsigned n, bool set, uint32_t v)
{
    switch (n) {
    S_HI(16) S_HI(17) S_HI(18) S_HI(19) S_HI(20) S_HI(21) S_HI(22) S_HI(23)
    S_HI(24) S_HI(25) S_HI(26) S_HI(27) S_HI(28) S_HI(29) S_HI(30) S_HI(31)
    default: break;
    }
    return v;
}

static uint32_t fp_get(struct emu *e, unsigned n)
{
    return n < 16 ? e->frame[8 + n] : s_hi(n, false, 0);
}

static void fp_set(struct emu *e, unsigned n, uint32_t v)
{
    if (n < 16)
        e->frame[8 + n] = v;
    else
        s_hi(n, true, v);
}

/* ---- memory ------------------------------------------------------------------------------- */

/* Pointers to the bytes [a, a + n) (n <= 128) in at most two pages; nothing is touched unless
 * both are there */
static int span(struct emu *e, uintptr_t a, unsigned n, bool write, uint8_t **p0, uint8_t **p1, unsigned *n0)
{
    uintptr_t last = a + n - 1;
    if (last < a)
        return EMU_BAD;
    int r = e->page(e->ctx, a, write, p0);
    if (r)
        return r == EMU_MISS ? (e->miss = a, EMU_MISS) : EMU_BAD;
    *n0 = n;
    *p1 = nullptr;
    if ((a ^ last) & ~(uintptr_t)(EMU_PAGE - 1)) {
        uintptr_t b = (last & ~(uintptr_t)(EMU_PAGE - 1));
        r = e->page(e->ctx, b, write, p1);
        if (r)
            return r == EMU_MISS ? (e->miss = b, EMU_MISS) : EMU_BAD;
        *n0 = (unsigned)(b - a);
    }
    return EMU_DONE;
}

static int mem_read(struct emu *e, uintptr_t a, void *buf, unsigned n)
{
    uint8_t *p0, *p1;
    unsigned n0;
    int r = span(e, a, n, false, &p0, &p1, &n0);
    if (r)
        return r;
    memcpy(buf, p0, n0);
    if (p1)
        memcpy((uint8_t *)buf + n0, p1, n - n0);
    return EMU_DONE;
}

static int mem_write(struct emu *e, uintptr_t a, const void *buf, unsigned n)
{
    uint8_t *p0, *p1;
    unsigned n0;
    int r = span(e, a, n, true, &p0, &p1, &n0);
    if (r)
        return r;
    memcpy(p0, buf, n0);
    if (p1)
        memcpy(p1, (const uint8_t *)buf + n0, n - n0);
    return EMU_DONE;
}

/* ---- the instructions ----------------------------------------------------------------------- */

static int load_to_pc(struct emu *e, uint32_t v)
{
    if (!(v & 1u))
        return EMU_BAD; /* would leave Thumb state */
    e->frame[6] = v & ~1u;
    e->branched = true;
    return EMU_DONE;
}

/* LDR/STR of one register: @size 1, 2 or 4 bytes */
static int single(struct emu *e, unsigned rt, uintptr_t a, unsigned size, bool load, bool sign)
{
    if (load) {
        uint32_t v = 0;
        int r = mem_read(e, a, &v, size);
        if (r)
            return r;
        if (sign && size == 1)
            v = (uint32_t)(int32_t)(int8_t)v;
        else if (sign && size == 2)
            v = (uint32_t)(int32_t)(int16_t)v;
        if (rt == 15)
            return size == 4 ? load_to_pc(e, v) : EMU_BAD;
        uint32_t *d = core_reg(e, rt);
        if (!d)
            return EMU_BAD;
        *d = v;
        return EMU_DONE;
    }
    uint32_t *s = core_reg(e, rt);
    if (!s)
        return EMU_BAD;
    uint32_t v = *s;
    return mem_write(e, a, &v, size);
}

/* LDM/STM: the registers of @list (lowest first) at [a, a + 4n) */
static int multiple(struct emu *e, uint32_t list, uintptr_t a, bool load)
{
    uint32_t buf[16];
    unsigned n = (unsigned)__builtin_popcount(list);
    if (!n || (a & 3u) || (list & (1u << 13)) || (!load && (list & (1u << 15))))
        return EMU_BAD;
    if (load) {
        int r = mem_read(e, a, buf, 4 * n);
        if (r)
            return r;
        unsigned k = 0;
        uint32_t pc = 0;
        bool to_pc = false;
        for (unsigned i = 0; i < 16; i++) {
            if (!(list & (1u << i)))
                continue;
            if (i == 15) {
                pc = buf[k++];
                to_pc = true;
                continue;
            }
            uint32_t *d = core_reg(e, i);
            if (!d)
                return EMU_BAD;
            *d = buf[k++];
        }
        return to_pc ? load_to_pc(e, pc) : EMU_DONE;
    }
    unsigned k = 0;
    for (unsigned i = 0; i < 16; i++) {
        if (!(list & (1u << i)))
            continue;
        uint32_t *s = core_reg(e, i);
        if (!s)
            return EMU_BAD;
        buf[k++] = *s;
    }
    return mem_write(e, a, buf, 4 * n);
}

/* Exclusive access (LDREX/STREX): the monitor is the task and its switch count at the load,
 * so a store after the task was switched out fails and the program's loop tries again */
static int exclusive(struct emu *e, unsigned rt, int rd, uintptr_t a, unsigned size)
{
    task_t *t = g_current;
    if (a & (size - 1u))
        return EMU_BAD;
    if (rd < 0) {
        int r = single(e, rt, a, size, true, false);
        if (r == EMU_DONE) {
            t->excl_addr = a;
            t->excl_switch = t->nswitch;
        }
        return r;
    }
    uint32_t *d = core_reg(e, (unsigned)rd);
    if (!d)
        return EMU_BAD;
    bool ok = t->excl_addr == a && t->excl_switch == t->nswitch;
    t->excl_addr = 0;
    if (!ok) {
        *d = 1;
        return EMU_DONE;
    }
    int r = single(e, rt, a, size, false, false);
    if (r == EMU_DONE)
        *d = 0;
    return r;
}

/* VLDR/VSTR/VLDM/VSTM: @n registers from S@first (@dbl: D registers, two S each) */
static int vfp(struct emu *e, unsigned first, unsigned nwords, uintptr_t a, bool load)
{
    uint32_t buf[32];
    if (!e->fpframe || (a & 3u) || !nwords || first + nwords > 32)
        return EMU_BAD;
    /* S0-S15 of the program are in its frame: have the lazy save done before using them */
    if (FPU->FPCCR & FPU_FPCCR_LSPACT_Msk)
        __asm volatile("vmrs r0, fpscr" ::: "r0", "memory");
    if (load) {
        int r = mem_read(e, a, buf, 4 * nwords);
        if (r)
            return r;
        for (unsigned i = 0; i < nwords; i++)
            fp_set(e, first + i, buf[i]);
        return EMU_DONE;
    }
    for (unsigned i = 0; i < nwords; i++)
        buf[i] = fp_get(e, first + i);
    return mem_write(e, a, buf, 4 * nwords);
}

static inline uint32_t reg(struct emu *e, unsigned n, bool *ok)
{
    uint32_t *r = core_reg(e, n);
    *ok = r != nullptr;
    return r ? *r : 0;
}

static int emulate16(struct emu *e, uint16_t hw)
{
    bool ok;
    if ((hw >> 12) == 0x5) { /* register offset */
        static const uint8_t size[8] = { 4, 2, 1, 1, 4, 2, 1, 2 };
        unsigned op = (hw >> 9) & 7u, rm = (hw >> 6) & 7u, rn = (hw >> 3) & 7u, rt = hw & 7u;
        uintptr_t a = reg(e, rn, &ok) + reg(e, rm, &ok);
        return single(e, rt, a, size[op], op >= 3, op == 3 || op == 7);
    }
    if ((hw >> 13) == 0x3 || (hw >> 12) == 0x8) { /* immediate offset: word, byte, halfword */
        unsigned size = (hw >> 12) == 0x8 ? 2 : (hw & 0x1000) ? 1 : 4;
        unsigned imm = ((hw >> 6) & 31u) * size, rn = (hw >> 3) & 7u, rt = hw & 7u;
        uintptr_t a = reg(e, rn, &ok) + imm;
        return single(e, rt, a, size, (hw >> 11) & 1u, false);
    }
    if ((hw >> 12) == 0xC) { /* LDMIA/STMIA Rn!, {list} */
        unsigned rn = (hw >> 8) & 7u;
        uint32_t list = hw & 0xFFu;
        bool load = (hw >> 11) & 1u;
        uintptr_t a = reg(e, rn, &ok);
        int r = multiple(e, list, a, load);
        if (r == EMU_DONE && !(load && (list & (1u << rn))))
            *core_reg(e, rn) = (uint32_t)(a + 4u * (unsigned)__builtin_popcount(list));
        return r;
    }
    return EMU_BAD;
}

static int emulate32(struct emu *e, uint16_t hw1, uint16_t hw2)
{
    bool ok;
    unsigned rn = hw1 & 15u;
    if (rn == 13 || rn == 15)
        return EMU_BAD; /* the stack and literals are never emulated memory */
    uint32_t base = reg(e, rn, &ok);
    if (!ok)
        return EMU_BAD;

    if ((hw1 & 0xFE00u) == 0xF800u) { /* LDR/STR{B,H,SB,SH} (immediate, register) */
        unsigned sz = (hw1 >> 5) & 3u, rt = hw2 >> 12;
        bool load = (hw1 >> 4) & 1u, sign = (hw1 >> 8) & 1u;
        if (sz == 3 || (sign && (!load || sz == 2)))
            return EMU_BAD;
        unsigned size = 1u << sz;
        if (load && rt == 15 && size < 4) /* PLD, PLI: hints */
            return EMU_DONE;
        uintptr_t a, wb = 0;
        bool write_back = false;
        if (hw1 & 0x0080u) { /* imm12 */
            a = base + (hw2 & 0xFFFu);
        } else if (hw2 & 0x0800u) { /* imm8, P U W */
            bool p = (hw2 >> 10) & 1u, u = (hw2 >> 9) & 1u, w = (hw2 >> 8) & 1u;
            uintptr_t off = u ? base + (hw2 & 0xFFu) : base - (hw2 & 0xFFu);
            a = p ? off : base;
            write_back = w || !p;
            wb = off;
            if (write_back && rt == rn)
                return EMU_BAD;
        } else if (!(hw2 & 0x0FC0u)) { /* register, LSL #imm2 */
            uint32_t m = reg(e, hw2 & 15u, &ok);
            if (!ok)
                return EMU_BAD;
            a = base + (m << ((hw2 >> 4) & 3u));
        } else {
            return EMU_BAD;
        }
        int r = single(e, rt, a, size, load, sign);
        if (r == EMU_DONE && write_back)
            *core_reg(e, rn) = (uint32_t)wb;
        return r;
    }

    if ((hw1 & 0xFE40u) == 0xE840u && (hw1 & 0x0120u)) { /* LDRD/STRD (immediate) */
        bool p = (hw1 >> 8) & 1u, u = (hw1 >> 7) & 1u, w = (hw1 >> 5) & 1u, load = (hw1 >> 4) & 1u;
        unsigned rt = hw2 >> 12, rt2 = (hw2 >> 8) & 15u;
        uintptr_t off = u ? base + 4u * (hw2 & 0xFFu) : base - 4u * (hw2 & 0xFFu);
        uintptr_t a = p ? off : base;
        uint32_t *d1 = core_reg(e, rt), *d2 = core_reg(e, rt2);
        if ((a & 3u) || !d1 || !d2 || (w && (rt == rn || rt2 == rn)) || (load && rt == rt2))
            return EMU_BAD;
        uint32_t v[2];
        int r;
        if (load) {
            r = mem_read(e, a, v, 8);
            if (r == EMU_DONE) {
                *d1 = v[0];
                *d2 = v[1];
            }
        } else {
            v[0] = *d1;
            v[1] = *d2;
            r = mem_write(e, a, v, 8);
        }
        if (r == EMU_DONE && w)
            *core_reg(e, rn) = (uint32_t)off;
        return r;
    }

    switch (hw1 & 0xFFF0u) { /* exclusive loads and stores */
    case 0xE840u: return exclusive(e, hw2 >> 12, (hw2 >> 8) & 15u, base + 4u * (hw2 & 0xFFu), 4);
    case 0xE850u: return exclusive(e, hw2 >> 12, -1, base + 4u * (hw2 & 0xFFu), 4);
    case 0xE8C0u:
    case 0xE8D0u: {
        unsigned op3 = (hw2 >> 4) & 15u;
        if (op3 != 4 && op3 != 5)
            return EMU_BAD; /* TBB/TBH, LDREXD/STREXD */
        unsigned size = op3 == 4 ? 1 : 2;
        return (hw1 & 0x10u) ? exclusive(e, hw2 >> 12, -1, base, size)
                             : exclusive(e, hw2 >> 12, hw2 & 15u, base, size);
    }
    default: break;
    }

    if ((hw1 & 0xFFC0u) == 0xE880u || (hw1 & 0xFFC0u) == 0xE900u) { /* LDM/STM IA, DB */
        bool db = (hw1 & 0xFFC0u) == 0xE900u, w = (hw1 >> 5) & 1u, load = (hw1 >> 4) & 1u;
        uint32_t list = hw2;
        unsigned n = (unsigned)__builtin_popcount(list);
        uintptr_t a = db ? base - 4u * n : base;
        if (w && (list & (1u << rn)))
            return EMU_BAD;
        int r = multiple(e, list, a, load);
        if (r == EMU_DONE && w)
            *core_reg(e, rn) = db ? (uint32_t)a : (uint32_t)(base + 4u * n);
        return r;
    }

    if ((hw1 & 0xFE00u) == 0xEC00u && (hw2 & 0x0E00u) == 0x0A00u) { /* VLDR/VSTR/VLDM/VSTM */
        bool p = (hw1 >> 8) & 1u, u = (hw1 >> 7) & 1u, d = (hw1 >> 6) & 1u, w = (hw1 >> 5) & 1u;
        bool load = (hw1 >> 4) & 1u, dbl = (hw2 >> 8) & 1u;
        unsigned vd = (hw2 >> 12) & 15u, imm = hw2 & 0xFFu;
        unsigned first = dbl ? 2u * ((d << 4) | vd) : (vd << 1) | d;
        if (p && !w) { /* VLDR/VSTR */
            uintptr_t a = u ? base + 4u * imm : base - 4u * imm;
            return vfp(e, first, dbl ? 2 : 1, a, load);
        }
        if (p == u) /* (VMOV between core and FP registers, not a memory access) */
            return EMU_BAD;
        uintptr_t a = p ? base - 4u * imm : base; /* DB : IA */
        int r = vfp(e, first, imm, a, load);
        if (r == EMU_DONE && w)
            *core_reg(e, rn) = p ? (uint32_t)a : (uint32_t)(base + 4u * imm);
        return r;
    }
    return EMU_BAD;
}

/* The IT block goes on by one instruction (ARMv7-M ITAdvance) */
static uint32_t it_advance(uint32_t xpsr)
{
    uint32_t it = ((xpsr >> 25) & 3u) | ((xpsr >> 8) & 0xFCu);
    if (!(it & 7u))
        it = 0;
    else
        it = (it & 0xE0u) | ((it << 1) & 0x1Fu);
    xpsr &= ~((3u << 25) | (0x3Fu << 10));
    return xpsr | ((it & 3u) << 25) | ((it >> 2) << 10);
}

int emulate_access(struct emu *e)
{
    uint32_t pc = e->frame[6];
    uint16_t hw1 = *(const volatile uint16_t *)pc;
    bool wide = (hw1 >> 11) >= 0x1Du;
    e->branched = false;
    int r = wide ? emulate32(e, hw1, *(const volatile uint16_t *)(pc + 2)) : emulate16(e, hw1);
    if (r != EMU_DONE)
        return r;
    if (e->branched) {
        e->frame[7] &= ~((3u << 25) | (0x3Fu << 10)); /* a branch ends the IT block */
    } else {
        e->frame[6] = pc + (wide ? 4u : 2u);
        e->frame[7] = it_advance(e->frame[7]);
    }
    return EMU_DONE;
}
