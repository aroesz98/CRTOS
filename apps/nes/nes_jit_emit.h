/*
 * nes_jit_emit.h - a small Thumb-2 (ARMv7-M) code emitter for the 6502 recompiler
 * (nes_jit.c): the instructions it needs, 32-bit encodings where both exist, so an
 * instruction's size does not depend on its registers. Branch targets are halfword
 * addresses in the same buffer.
 */
#ifndef NES_JIT_EMIT_H
#define NES_JIT_EMIT_H

#include <stdint.h>

typedef struct {
    uint16_t *p;        /* next halfword */
    uint16_t *end;      /* the buffer ends here: past it nothing is written, full is set */
    int full;
} emit_t;

enum { R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, RSP, RLR, RPC };
enum { C_EQ, C_NE, C_CS, C_CC, C_MI, C_PL, C_VS, C_VC, C_HI, C_LS, C_GE, C_LT, C_GT, C_LE, C_AL };
/* data processing opcodes (the op field of the 32-bit encodings) */
enum { OP_AND = 0, OP_BIC = 1, OP_ORR = 2, OP_ORN = 3, OP_EOR = 4, OP_ADD = 8, OP_ADC = 10, OP_SBC = 11,
       OP_SUB = 13, OP_RSB = 14 };
enum { SH_LSL, SH_LSR, SH_ASR, SH_ROR };

static inline void e16(emit_t *e, uint16_t h)
{
    if (e->p < e->end)
        *e->p++ = h;
    else
        e->full = 1;
}

static inline void e32(emit_t *e, uint16_t a, uint16_t b)
{
    e16(e, a);
    e16(e, b);
}

/* ThumbExpandImm in reverse: the 12-bit encoding of v, or -1 */
static inline int t2_imm(uint32_t v)
{
    if (v < 256)
        return (int)v;
    uint32_t b = v & 0xff, b2 = (v >> 8) & 0xff;
    if (b && v == (b | b << 16))
        return 0x100 | (int)b;
    if (b2 && v == (b2 << 8 | b2 << 24))
        return 0x200 | (int)b2;
    if (b && v == (b | b << 8 | b << 16 | b << 24))
        return 0x300 | (int)b;
    for (int rot = 8; rot < 32; rot++) {
        /* v == ROR(0b1xxxxxxx, rot) */
        uint32_t x = (v << rot) | (v >> (32 - rot)); /* rotate left: undo the rotate right */
        if (x >= 0x80 && x < 0x100)
            return (rot << 7) | (int)(x & 0x7f);
    }
    return -1;
}

/* Rd = Rn op #imm (S: set the flags); the immediate must be encodable (t2_imm >= 0) */
static inline void dp_imm(emit_t *e, int op, int s, int rd, int rn, uint32_t imm)
{
    int i12 = t2_imm(imm);
    if (i12 < 0)
        i12 = 0; /* the caller checks; never happens with the values used */
    e32(e, (uint16_t)(0xF000 | ((i12 >> 11) & 1) << 10 | op << 5 | s << 4 | rn),
        (uint16_t)(((i12 >> 8) & 7) << 12 | rd << 8 | (i12 & 0xff)));
}

/* Rd = Rn op (Rm shifted) */
static inline void dp_reg(emit_t *e, int op, int s, int rd, int rn, int rm, int sh, int amount)
{
    e32(e, (uint16_t)(0xEA00 | op << 5 | s << 4 | rn),
        (uint16_t)(((amount >> 2) & 7) << 12 | rd << 8 | (amount & 3) << 6 | sh << 4 | rm));
}

static inline void mov_reg(emit_t *e, int rd, int rm)
{
    dp_reg(e, OP_ORR, 0, rd, RPC, rm, SH_LSL, 0);
}

static inline void shift_imm(emit_t *e, int rd, int rm, int sh, int amount)
{
    dp_reg(e, OP_ORR, 0, rd, RPC, rm, sh, amount);
}

static inline void mov_imm(emit_t *e, int rd, uint32_t imm) /* imm encodable */
{
    dp_imm(e, OP_ORR, 0, rd, RPC, imm);
}

static inline void cmp_imm(emit_t *e, int rn, uint32_t imm)
{
    dp_imm(e, OP_SUB, 1, RPC, rn, imm);
}

static inline void tst_imm(emit_t *e, int rn, uint32_t imm)
{
    dp_imm(e, OP_AND, 1, RPC, rn, imm);
}

static inline void cmp_reg(emit_t *e, int rn, int rm)
{
    dp_reg(e, OP_SUB, 1, RPC, rn, rm, SH_LSL, 0);
}

static inline void movw(emit_t *e, int rd, uint32_t imm16)
{
    e32(e, (uint16_t)(0xF240 | ((imm16 >> 11) & 1) << 10 | ((imm16 >> 12) & 0xf)),
        (uint16_t)(((imm16 >> 8) & 7) << 12 | rd << 8 | (imm16 & 0xff)));
}

static inline void movt(emit_t *e, int rd, uint32_t imm16)
{
    e32(e, (uint16_t)(0xF2C0 | ((imm16 >> 11) & 1) << 10 | ((imm16 >> 12) & 0xf)),
        (uint16_t)(((imm16 >> 8) & 7) << 12 | rd << 8 | (imm16 & 0xff)));
}

static inline void mov32(emit_t *e, int rd, uint32_t v)
{
    movw(e, rd, v & 0xffff);
    if (v >> 16)
        movt(e, rd, v >> 16);
}

/* memory: 12-bit immediate offsets, or a register */
static inline void ldrb_imm(emit_t *e, int rt, int rn, uint32_t off) { e32(e, (uint16_t)(0xF890 | rn), (uint16_t)(rt << 12 | off)); }
static inline void strb_imm(emit_t *e, int rt, int rn, uint32_t off) { e32(e, (uint16_t)(0xF880 | rn), (uint16_t)(rt << 12 | off)); }
static inline void ldr_imm(emit_t *e, int rt, int rn, uint32_t off) { e32(e, (uint16_t)(0xF8D0 | rn), (uint16_t)(rt << 12 | off)); }
static inline void str_imm(emit_t *e, int rt, int rn, uint32_t off) { e32(e, (uint16_t)(0xF8C0 | rn), (uint16_t)(rt << 12 | off)); }
static inline void ldrb_reg(emit_t *e, int rt, int rn, int rm) { e32(e, (uint16_t)(0xF810 | rn), (uint16_t)(rt << 12 | rm)); }
static inline void strb_reg(emit_t *e, int rt, int rn, int rm) { e32(e, (uint16_t)(0xF800 | rn), (uint16_t)(rt << 12 | rm)); }

/* extensions and bit fields */
static inline void uxtb(emit_t *e, int rd, int rm) { e32(e, 0xFA5F, (uint16_t)(0xF080 | rd << 8 | rm)); }
static inline void sxtb(emit_t *e, int rd, int rm) { e32(e, 0xFA4F, (uint16_t)(0xF080 | rd << 8 | rm)); }
static inline void uxth(emit_t *e, int rd, int rm) { e32(e, 0xFA1F, (uint16_t)(0xF080 | rd << 8 | rm)); }

static inline void ubfx(emit_t *e, int rd, int rn, int lsb, int width)
{
    e32(e, (uint16_t)(0xF3C0 | rn), (uint16_t)(((lsb >> 2) & 7) << 12 | rd << 8 | (lsb & 3) << 6 | (width - 1)));
}

/* IT with one conditional instruction following */
static inline void it(emit_t *e, int cond) { e16(e, (uint16_t)(0xBF08 | cond << 4)); }

static inline void blx(emit_t *e, int rm) { e16(e, (uint16_t)(0x4780 | rm << 3)); }

/* ldr pc, [rn, #off]: a jump through a pointer (Thumb address, bit 0 set) */
static inline void ldr_pc(emit_t *e, int rn, uint32_t off) { ldr_imm(e, RPC, rn, off); }

static inline void nop32(emit_t *e) { e32(e, 0xF3AF, 0x8000); }

/* ---- branches (targets in the same buffer) --------------------------------------------------- */

/* B.W at @at to @target (±16 MB) */
static inline void b_encode(uint16_t *at, const uint16_t *target)
{
    int32_t off = (int32_t)((const uint8_t *)target - ((const uint8_t *)at + 4));
    uint32_t s = (off >> 24) & 1, i1 = (off >> 23) & 1, i2 = (off >> 22) & 1;
    uint32_t j1 = (~(i1 ^ s)) & 1, j2 = (~(i2 ^ s)) & 1;
    at[0] = (uint16_t)(0xF000 | s << 10 | ((off >> 12) & 0x3ff));
    at[1] = (uint16_t)(0x9000 | j1 << 13 | j2 << 11 | ((off >> 1) & 0x7ff));
}

/* B<cond>.W at @at to @target (±1 MB) */
static inline void bcond_encode(uint16_t *at, int cond, const uint16_t *target)
{
    int32_t off = (int32_t)((const uint8_t *)target - ((const uint8_t *)at + 4));
    uint32_t s = (off >> 20) & 1, j2 = (off >> 19) & 1, j1 = (off >> 18) & 1;
    at[0] = (uint16_t)(0xF000 | s << 10 | cond << 6 | ((off >> 12) & 0x3f));
    at[1] = (uint16_t)(0x8000 | j1 << 13 | j2 << 11 | ((off >> 1) & 0x7ff));
}

/* A branch whose target comes later: returns where to patch (or NULL when the buffer is full) */
static inline uint16_t *b_fwd(emit_t *e, int cond)
{
    uint16_t *at = e->p;
    e32(e, 0, 0);
    if (e->full)
        return 0;
    at[1] = (uint16_t)cond; /* remembered until patched */
    return at;
}

static inline void b_patch(uint16_t *at, const uint16_t *target)
{
    if (!at)
        return;
    int cond = at[1];
    if (cond == C_AL)
        b_encode(at, target);
    else
        bcond_encode(at, cond, target);
}

static inline void b_to(emit_t *e, int cond, const uint16_t *target)
{
    uint16_t *at = e->p;
    e32(e, 0, 0);
    if (e->full)
        return;
    if (cond == C_AL)
        b_encode(at, target);
    else
        bcond_encode(at, cond, target);
}

#endif
