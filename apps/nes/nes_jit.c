/*
 * nes_jit.c - a dynamic recompiler for the NES's 6502: blocks of ROM code are translated to
 * Thumb-2 once and then run natively; the interpreter (core/cpu6502.c) stays for what is not
 * translated. Compiled in nes_fast.c after the core, whose functions it calls.
 *
 * Registers of translated code:
 *     r4 A    r5 X    r6 Y
 *     r7 N and Z: the last result sign-extended (N = bit 31, Z = low byte is 0)
 *     r8 C (0 or 1)
 *     r9 the cycle budget: CPU cycles left until the lazy clock's next checkpoint
 *     r10 the 2 KB RAM      r11 the context (struct jit_ctx: SP, V, D and I flags, helpers)
 * They are callee-saved, so the helpers (C functions for everything but RAM) keep them.
 *
 * Timing: every instruction first subtracts its cycles from the budget; the instruction that
 * would reach the checkpoint is not run but handed to the interpreter, which ticks through
 * it cycle by cycle - the PPU/APU catch-up and the interrupt polling happen exactly as
 * without the JIT. Accesses to I/O and mappers go through helpers that set the CPU's cycle
 * count first (the memory map syncs the PPU and APU to it).
 *
 * Blocks: straight-line code from a ROM address until a jump, a return, an I/O or mapper
 * write, a change of the interrupt flag, an instruction it does not translate, or the end of
 * the 8 KB window. Conditional branches leave through exits; a backward branch into the same
 * block is a native loop. Blocks are keyed by where their first byte is in the ROM (the
 * mapper's prg_ptr), so a bank switch cannot run stale code; an exit to an address in the same
 * 8 KB window is linked straight to its block once that exists (the window's bank is the
 * running block's). Code in RAM is interpreted. Interrupts are taken by the interpreter
 * between blocks; the handlers run translated (a translated RTI tells run_block to end the
 * core's CPU_ISR mode, as the interpreter's RTI does).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <crtos.h>

#include "nes_jit.h"
#include "nes_jit_emit.h"

#define CODE_HALFWORDS (256u * 1024u)     /* 512 KB of translations */
#define HASH_BITS 14
#define HASH_SIZE (1u << HASH_BITS)
#define MAX_LINKS 32000u
#define LINK_INTERP 0xffffu               /* exit: the interpreter runs this instruction */
#define MAX_INSNS 48
#define MAX_STUBS (MAX_INSNS * 2 + 4)

/* ---- the context of translated code ------------------------------------------------------------ */

struct jit_ctx {
    uint32_t a, x, y, nz, c, budget;    /* r4-r9, kept here between blocks */
    uint8_t *ram;                       /* r10 */
    void *leave;                        /* jit_leave */
    uint32_t pc;                        /* at leaving: the next PC | link << 16 */
    uint8_t sp, v, other, rti;          /* stack pointer, V, bits 2-5 of P (I, D, B, 1), RTI run */
    void *h_read, *h_write;
    uint32_t tmp;
    Emulator *emu;
};

enum {
    CTX_BUDGET = offsetof(struct jit_ctx, budget),
    CTX_LEAVE = offsetof(struct jit_ctx, leave),
    CTX_SP = offsetof(struct jit_ctx, sp),
    CTX_V = offsetof(struct jit_ctx, v),
    CTX_OTHER = offsetof(struct jit_ctx, other),
    CTX_HREAD = offsetof(struct jit_ctx, h_read),
    CTX_HWRITE = offsetof(struct jit_ctx, h_write),
    CTX_TMP = offsetof(struct jit_ctx, tmp),
    CTX_RTI = offsetof(struct jit_ctx, rti),
};
_Static_assert(offsetof(struct jit_ctx, ram) == 24, "jit_enter loads r10 from offset 24");
_Static_assert(offsetof(struct jit_ctx, pc) == 32, "jit_leave stores r0 at offset 32");

/* Runs translated code at @code (Thumb address) with the registers from @ctx */
__attribute__((naked, noinline)) static void jit_enter(struct jit_ctx *ctx, const void *code)
{
    __asm volatile("push {r3-r11, lr}\n\t"  /* 10 registers: the stack stays 8-byte aligned */
                   "mov r11, r0\n\t"
                   "ldmia r0, {r4-r9}\n\t"
                   "ldr r10, [r11, #24]\n\t"
                   "bx r1\n\t");
}

/* Where translated code leaves (ldr pc, [r11, #CTX_LEAVE]) with r0 = next PC | link << 16 */
__attribute__((naked, noinline)) static void jit_leave(void)
{
    __asm volatile("str r0, [r11, #32]\n\t"
                   "stmia r11, {r4-r9}\n\t"
                   "pop {r3-r11, pc}\n\t");
}

/* ---- state ------------------------------------------------------------------------------------- */

struct block {
    const uint8_t *key;                 /* where its first byte is in the ROM */
    uint16_t *code;
};

static uint16_t *s_code;                /* the translations (NULL: no JIT) */
static emit_t s_e;
static struct block *s_hash;
static uint32_t s_hash_used;
static uint16_t **s_links;              /* link index -> the exit stub to patch */
static uint32_t s_nlinks;
static struct jit_ctx s_ctx;
static struct jit_stats s_stats;
static int s_enabled;

static uint32_t jit_h_read(struct jit_ctx *j, uint32_t addr);
static uint32_t jit_h_write(struct jit_ctx *j, uint32_t addr, uint32_t value);

static void flush_all(void)
{
    s_e.p = s_code;
    s_e.end = s_code + CODE_HALFWORDS;
    s_e.full = 0;
    memset(s_hash, 0, HASH_SIZE * sizeof(s_hash[0]));
    s_hash_used = 0;
    s_nlinks = 1;
    s_stats.flushes++;
}

int jit_init(Emulator *e)
{
    if (!s_code) {
        s_code = malloc(CODE_HALFWORDS * 2u + 32u);
        s_hash = malloc(HASH_SIZE * sizeof(s_hash[0]));
        s_links = malloc(MAX_LINKS * sizeof(s_links[0]));
        if (!s_code || !s_hash || !s_links) {
            free(s_code);
            free(s_hash);
            free(s_links);
            s_code = NULL;
            s_hash = NULL;
            s_links = NULL;
            return -1;
        }
    }
    memset(&s_stats, 0, sizeof(s_stats));
    flush_all();
    s_stats.flushes = 0;
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.emu = e;
    s_ctx.ram = e->mem.RAM;
    s_ctx.leave = (void *)jit_leave;
    s_ctx.h_read = (void *)jit_h_read;
    s_ctx.h_write = (void *)jit_h_write;
    s_enabled = 1;
    return 0;
}

void jit_free(void)
{
    free(s_code);
    free(s_hash);
    free(s_links);
    s_code = NULL;
    s_hash = NULL;
    s_links = NULL;
    s_enabled = 0;
}

void jit_enable(int on)
{
    s_enabled = on && s_code;
}

const struct jit_stats *jit_stats(void)
{
    s_stats.code_bytes = s_code ? (uint32_t)((s_e.p - s_code) * 2) : 0;
    s_stats.blocks = s_hash_used;
    return &s_stats;
}

/* ---- helpers called by translated code ---------------------------------------------------------- */

/* ctx->budget: the budget at the access; set to the (maybe moved) budget afterwards */
static uint32_t jit_h_read(struct jit_ctx *j, uint32_t addr)
{
    Emulator *e = j->emu;
    e->cpu.t_cycles = g_clk_event - j->budget;
    if ((int32_t)j->budget <= 0) {
        nes_clock_sync(e);
        nes_clock_plan(e);
    }
    e->mem.bus = (uint8_t)(addr >> 8); /* open bus: the address' high byte was fetched last */
    uint8_t v = read_mem(&e->mem, (uint16_t)addr);
    j->budget = g_clk_event - (uint32_t)e->cpu.t_cycles;
    return v;
}

/* returns non-zero if the block must end (an I/O or mapper register was written) */
static uint32_t jit_h_write(struct jit_ctx *j, uint32_t addr, uint32_t value)
{
    Emulator *e = j->emu;
    e->cpu.t_cycles = g_clk_event - j->budget;
    if ((int32_t)j->budget <= 0) {
        nes_clock_sync(e);
        nes_clock_plan(e);
    }
    write_mem(&e->mem, (uint16_t)addr, (uint8_t)value);
    j->budget = g_clk_event - (uint32_t)e->cpu.t_cycles;
    return (addr >= 0x2000 && addr < 0x6000) || addr >= 0x8000;
}

/* ---- the 6502 instructions ---------------------------------------------------------------------- */

enum am { AM_NONE, AM_IMP, AM_ACC, AM_IMM, AM_ZP, AM_ZPX, AM_ZPY, AM_ABS, AM_ABX, AM_ABY, AM_INX, AM_INY,
          AM_IND, AM_REL };
enum mn { M_NONE, M_LDA, M_LDX, M_LDY, M_STA, M_STX, M_STY, M_ADC, M_SBC, M_AND, M_ORA, M_EOR, M_CMP,
          M_CPX, M_CPY, M_BIT, M_INC, M_DEC, M_ASL, M_LSR, M_ROL, M_ROR, M_INX, M_INY, M_DEX, M_DEY,
          M_TAX, M_TAY, M_TXA, M_TYA, M_TSX, M_TXS, M_PHA, M_PLA, M_PHP, M_PLP, M_JMP, M_JSR, M_RTS,
          M_RTI, M_BPL, M_BMI, M_BVC, M_BVS, M_BCC, M_BCS, M_BNE, M_BEQ, M_CLC, M_SEC, M_CLI, M_SEI,
          M_CLV, M_CLD, M_SED, M_NOP };

struct op {
    uint8_t mn, am, cycles;
};

/* the official opcodes (the others, and BRK, go to the interpreter); cycles without the
 * page crossing and taken branch extras */
static const struct op OPS[256] = {
    [0xA9] = {M_LDA, AM_IMM, 2}, [0xA5] = {M_LDA, AM_ZP, 3}, [0xB5] = {M_LDA, AM_ZPX, 4}, [0xAD] = {M_LDA, AM_ABS, 4},
    [0xBD] = {M_LDA, AM_ABX, 4}, [0xB9] = {M_LDA, AM_ABY, 4}, [0xA1] = {M_LDA, AM_INX, 6}, [0xB1] = {M_LDA, AM_INY, 5},
    [0xA2] = {M_LDX, AM_IMM, 2}, [0xA6] = {M_LDX, AM_ZP, 3}, [0xB6] = {M_LDX, AM_ZPY, 4}, [0xAE] = {M_LDX, AM_ABS, 4},
    [0xBE] = {M_LDX, AM_ABY, 4},
    [0xA0] = {M_LDY, AM_IMM, 2}, [0xA4] = {M_LDY, AM_ZP, 3}, [0xB4] = {M_LDY, AM_ZPX, 4}, [0xAC] = {M_LDY, AM_ABS, 4},
    [0xBC] = {M_LDY, AM_ABX, 4},
    [0x85] = {M_STA, AM_ZP, 3}, [0x95] = {M_STA, AM_ZPX, 4}, [0x8D] = {M_STA, AM_ABS, 4}, [0x9D] = {M_STA, AM_ABX, 5},
    [0x99] = {M_STA, AM_ABY, 5}, [0x81] = {M_STA, AM_INX, 6}, [0x91] = {M_STA, AM_INY, 6},
    [0x86] = {M_STX, AM_ZP, 3}, [0x96] = {M_STX, AM_ZPY, 4}, [0x8E] = {M_STX, AM_ABS, 4},
    [0x84] = {M_STY, AM_ZP, 3}, [0x94] = {M_STY, AM_ZPX, 4}, [0x8C] = {M_STY, AM_ABS, 4},
    [0x69] = {M_ADC, AM_IMM, 2}, [0x65] = {M_ADC, AM_ZP, 3}, [0x75] = {M_ADC, AM_ZPX, 4}, [0x6D] = {M_ADC, AM_ABS, 4},
    [0x7D] = {M_ADC, AM_ABX, 4}, [0x79] = {M_ADC, AM_ABY, 4}, [0x61] = {M_ADC, AM_INX, 6}, [0x71] = {M_ADC, AM_INY, 5},
    [0xE9] = {M_SBC, AM_IMM, 2}, [0xE5] = {M_SBC, AM_ZP, 3}, [0xF5] = {M_SBC, AM_ZPX, 4}, [0xED] = {M_SBC, AM_ABS, 4},
    [0xFD] = {M_SBC, AM_ABX, 4}, [0xF9] = {M_SBC, AM_ABY, 4}, [0xE1] = {M_SBC, AM_INX, 6}, [0xF1] = {M_SBC, AM_INY, 5},
    [0x29] = {M_AND, AM_IMM, 2}, [0x25] = {M_AND, AM_ZP, 3}, [0x35] = {M_AND, AM_ZPX, 4}, [0x2D] = {M_AND, AM_ABS, 4},
    [0x3D] = {M_AND, AM_ABX, 4}, [0x39] = {M_AND, AM_ABY, 4}, [0x21] = {M_AND, AM_INX, 6}, [0x31] = {M_AND, AM_INY, 5},
    [0x09] = {M_ORA, AM_IMM, 2}, [0x05] = {M_ORA, AM_ZP, 3}, [0x15] = {M_ORA, AM_ZPX, 4}, [0x0D] = {M_ORA, AM_ABS, 4},
    [0x1D] = {M_ORA, AM_ABX, 4}, [0x19] = {M_ORA, AM_ABY, 4}, [0x01] = {M_ORA, AM_INX, 6}, [0x11] = {M_ORA, AM_INY, 5},
    [0x49] = {M_EOR, AM_IMM, 2}, [0x45] = {M_EOR, AM_ZP, 3}, [0x55] = {M_EOR, AM_ZPX, 4}, [0x4D] = {M_EOR, AM_ABS, 4},
    [0x5D] = {M_EOR, AM_ABX, 4}, [0x59] = {M_EOR, AM_ABY, 4}, [0x41] = {M_EOR, AM_INX, 6}, [0x51] = {M_EOR, AM_INY, 5},
    [0xC9] = {M_CMP, AM_IMM, 2}, [0xC5] = {M_CMP, AM_ZP, 3}, [0xD5] = {M_CMP, AM_ZPX, 4}, [0xCD] = {M_CMP, AM_ABS, 4},
    [0xDD] = {M_CMP, AM_ABX, 4}, [0xD9] = {M_CMP, AM_ABY, 4}, [0xC1] = {M_CMP, AM_INX, 6}, [0xD1] = {M_CMP, AM_INY, 5},
    [0xE0] = {M_CPX, AM_IMM, 2}, [0xE4] = {M_CPX, AM_ZP, 3}, [0xEC] = {M_CPX, AM_ABS, 4},
    [0xC0] = {M_CPY, AM_IMM, 2}, [0xC4] = {M_CPY, AM_ZP, 3}, [0xCC] = {M_CPY, AM_ABS, 4},
    [0x24] = {M_BIT, AM_ZP, 3}, [0x2C] = {M_BIT, AM_ABS, 4},
    [0xE6] = {M_INC, AM_ZP, 5}, [0xF6] = {M_INC, AM_ZPX, 6}, [0xEE] = {M_INC, AM_ABS, 6}, [0xFE] = {M_INC, AM_ABX, 7},
    [0xC6] = {M_DEC, AM_ZP, 5}, [0xD6] = {M_DEC, AM_ZPX, 6}, [0xCE] = {M_DEC, AM_ABS, 6}, [0xDE] = {M_DEC, AM_ABX, 7},
    [0x0A] = {M_ASL, AM_ACC, 2}, [0x06] = {M_ASL, AM_ZP, 5}, [0x16] = {M_ASL, AM_ZPX, 6}, [0x0E] = {M_ASL, AM_ABS, 6},
    [0x1E] = {M_ASL, AM_ABX, 7},
    [0x4A] = {M_LSR, AM_ACC, 2}, [0x46] = {M_LSR, AM_ZP, 5}, [0x56] = {M_LSR, AM_ZPX, 6}, [0x4E] = {M_LSR, AM_ABS, 6},
    [0x5E] = {M_LSR, AM_ABX, 7},
    [0x2A] = {M_ROL, AM_ACC, 2}, [0x26] = {M_ROL, AM_ZP, 5}, [0x36] = {M_ROL, AM_ZPX, 6}, [0x2E] = {M_ROL, AM_ABS, 6},
    [0x3E] = {M_ROL, AM_ABX, 7},
    [0x6A] = {M_ROR, AM_ACC, 2}, [0x66] = {M_ROR, AM_ZP, 5}, [0x76] = {M_ROR, AM_ZPX, 6}, [0x6E] = {M_ROR, AM_ABS, 6},
    [0x7E] = {M_ROR, AM_ABX, 7},
    [0xE8] = {M_INX, AM_IMP, 2}, [0xC8] = {M_INY, AM_IMP, 2}, [0xCA] = {M_DEX, AM_IMP, 2}, [0x88] = {M_DEY, AM_IMP, 2},
    [0xAA] = {M_TAX, AM_IMP, 2}, [0xA8] = {M_TAY, AM_IMP, 2}, [0x8A] = {M_TXA, AM_IMP, 2}, [0x98] = {M_TYA, AM_IMP, 2},
    [0xBA] = {M_TSX, AM_IMP, 2}, [0x9A] = {M_TXS, AM_IMP, 2},
    [0x48] = {M_PHA, AM_IMP, 3}, [0x68] = {M_PLA, AM_IMP, 4}, [0x08] = {M_PHP, AM_IMP, 3}, [0x28] = {M_PLP, AM_IMP, 4},
    [0x4C] = {M_JMP, AM_ABS, 3}, [0x6C] = {M_JMP, AM_IND, 5}, [0x20] = {M_JSR, AM_ABS, 6}, [0x60] = {M_RTS, AM_IMP, 6},
    [0x40] = {M_RTI, AM_IMP, 6},
    [0x10] = {M_BPL, AM_REL, 2}, [0x30] = {M_BMI, AM_REL, 2}, [0x50] = {M_BVC, AM_REL, 2}, [0x70] = {M_BVS, AM_REL, 2},
    [0x90] = {M_BCC, AM_REL, 2}, [0xB0] = {M_BCS, AM_REL, 2}, [0xD0] = {M_BNE, AM_REL, 2}, [0xF0] = {M_BEQ, AM_REL, 2},
    [0x18] = {M_CLC, AM_IMP, 2}, [0x38] = {M_SEC, AM_IMP, 2}, [0x58] = {M_CLI, AM_IMP, 2}, [0x78] = {M_SEI, AM_IMP, 2},
    [0xB8] = {M_CLV, AM_IMP, 2}, [0xD8] = {M_CLD, AM_IMP, 2}, [0xF8] = {M_SED, AM_IMP, 2}, [0xEA] = {M_NOP, AM_IMP, 2},
};

static int op_len(int am)
{
    switch (am) {
    case AM_IMP:
    case AM_ACC:
        return 1;
    case AM_ABS:
    case AM_ABX:
    case AM_ABY:
    case AM_IND:
        return 3;
    default:
        return 2;
    }
}

/* ---- translating ------------------------------------------------------------------------------- */

/* where an operand is */
enum loc {
    L_RAMC,     /* in RAM at the constant offset ea.addr */
    L_RAMR1,    /* in RAM at the offset in r1 */
    L_HIGHC,    /* at the constant address ea.addr >= $2000 (a helper) */
    L_DYN       /* at the address in r1 (RAM or a helper, decided at run time) */
};

struct ea {
    int loc;
    uint32_t addr;
};

enum stub_kind { S_BUDGET, S_EXIT_IO, S_BRANCH };

struct stub {
    uint16_t *from;     /* the branch to this stub */
    int kind;
    uint32_t pc;        /* S_BUDGET: the instruction; S_EXIT/S_BRANCH: the target */
    uint32_t extra;     /* S_BUDGET: its cycles; S_BRANCH: the taken branch's extra cycles */
    uint16_t *native;   /* S_BRANCH into this block: the target's code */
    int link;           /* S_EXIT/S_BRANCH: link index (0: none) */
};

struct tr {
    emit_t *e;
    uint32_t pc0, win;          /* the block's first address and 8 KB window */
    uint32_t npc;               /* the address after the current instruction */
    uint32_t pcs[MAX_INSNS];
    uint16_t *natives[MAX_INSNS];
    int n;
    struct stub stubs[MAX_STUBS];
    int nstubs;
};

static int new_link(uint32_t pc, uint32_t win)
{
    if (pc < 0x8000 || (pc >> 13) != win || s_nlinks >= MAX_LINKS)
        return 0;
    return (int)s_nlinks++;
}

static void add_stub(struct tr *t, uint16_t *from, int kind, uint32_t pc, uint32_t extra)
{
    if (t->nstubs >= MAX_STUBS || !from) {
        t->e->full = 1; /* cannot happen with the limits; abandon the block */
        return;
    }
    struct stub *s = &t->stubs[t->nstubs++];
    s->from = from;
    s->kind = kind;
    s->pc = pc;
    s->extra = extra;
    s->native = NULL;
    s->link = 0;
    if (kind == S_BRANCH) {
        for (int i = 0; i < t->n; i++)
            if (t->pcs[i] == pc)
                s->native = t->natives[i];
        if (!s->native)
            s->link = new_link(pc, t->win);
    }
}

/* leave to @pc: movw r0, #pc; movt r0, #link; ldr pc, [r11, #leave] - the movw is patched into
 * a direct branch when the target is linked */
static void emit_exit(struct tr *t, uint32_t pc, int link)
{
    emit_t *e = t->e;
    uint16_t *at = e->p;
    movw(e, R0, pc & 0xffff);
    if (link)
        movt(e, R0, (uint32_t)link);
    ldr_pc(e, R11, CTX_LEAVE);
    if (link && !e->full)
        s_links[link] = at;
}

/* the end of a block: straight on to @pc */
static void end_to(struct tr *t, uint32_t pc, int linkable)
{
    for (int i = 0; i < t->n; i++)
        if (t->pcs[i] == pc) {
            b_to(t->e, C_AL, t->natives[i]);
            return;
        }
    emit_exit(t, pc, linkable ? new_link(pc, t->win) : 0);
}

/* calls a helper (ctx->budget = the budget at the access: the instruction's last cycle) */
static void call_helper(emit_t *e, uint32_t off)
{
    dp_imm(e, OP_ADD, 0, R0, R9, 1);
    str_imm(e, R0, R11, CTX_BUDGET);
    mov_reg(e, R0, R11);
    ldr_imm(e, R3, R11, off);
    blx(e, R3);
    ldr_imm(e, R9, R11, CTX_BUDGET);
    dp_imm(e, OP_SUB, 0, R9, R9, 1);
}

/* the effective address (indexed reads that cross a page cost a cycle: @penalty) */
static struct ea emit_ea(emit_t *e, int am, const uint8_t *s, int penalty)
{
    uint32_t lo = s[1], abs = s[1] | (uint32_t)s[2] << 8;
    struct ea ea = {L_DYN, 0};
    switch (am) {
    case AM_ZP:
        ea.loc = L_RAMC;
        ea.addr = lo;
        break;
    case AM_ZPX:
    case AM_ZPY:
        dp_imm(e, OP_ADD, 0, R1, am == AM_ZPX ? R5 : R6, lo);
        uxtb(e, R1, R1);
        ea.loc = L_RAMR1;
        break;
    case AM_ABS:
        if (abs < 0x2000) {
            ea.loc = L_RAMC;
            ea.addr = abs & 0x7ff;
        } else {
            ea.loc = L_HIGHC;
            ea.addr = abs;
        }
        break;
    case AM_ABX:
    case AM_ABY: {
        int ri = am == AM_ABX ? R5 : R6;
        if (penalty && (abs & 0xff)) {
            dp_imm(e, OP_ADD, 0, R2, ri, abs & 0xff);
            shift_imm(e, R2, R2, SH_LSR, 8);
            dp_reg(e, OP_SUB, 0, R9, R9, R2, SH_LSL, 0);
        }
        movw(e, R1, abs);
        dp_reg(e, OP_ADD, 0, R1, R1, ri, SH_LSL, 0);
        if (abs + 0xff > 0xffff)
            uxth(e, R1, R1);
        if (abs + 0xff < 0x2000) {
            ubfx(e, R1, R1, 0, 11);
            ea.loc = L_RAMR1;
        }
        break;
    }
    case AM_INX:
        dp_imm(e, OP_ADD, 0, R0, R5, lo);
        uxtb(e, R0, R0);
        ldrb_reg(e, R1, R10, R0);
        dp_imm(e, OP_ADD, 0, R0, R0, 1);
        uxtb(e, R0, R0);
        ldrb_reg(e, R2, R10, R0);
        dp_reg(e, OP_ORR, 0, R1, R1, R2, SH_LSL, 8);
        break;
    case AM_INY:
        ldrb_imm(e, R1, R10, lo);
        ldrb_imm(e, R2, R10, (lo + 1) & 0xff);
        if (penalty) {
            dp_reg(e, OP_ADD, 0, R3, R1, R6, SH_LSL, 0);
            shift_imm(e, R3, R3, SH_LSR, 8);
            dp_reg(e, OP_SUB, 0, R9, R9, R3, SH_LSL, 0);
        }
        dp_reg(e, OP_ORR, 0, R1, R1, R2, SH_LSL, 8);
        dp_reg(e, OP_ADD, 0, R1, R1, R6, SH_LSL, 0);
        uxth(e, R1, R1);
        break;
    }
    return ea;
}

/* the operand into @rd */
static void emit_load(emit_t *e, struct ea ea, int rd)
{
    switch (ea.loc) {
    case L_RAMC:
        ldrb_imm(e, rd, R10, ea.addr);
        break;
    case L_RAMR1:
        ldrb_reg(e, rd, R10, R1);
        break;
    case L_HIGHC:
        movw(e, R1, ea.addr);
        call_helper(e, CTX_HREAD);
        if (rd != R0)
            mov_reg(e, rd, R0);
        break;
    default: {
        cmp_imm(e, R1, 0x2000);
        uint16_t *slow = b_fwd(e, C_CS);
        ubfx(e, R2, R1, 0, 11);
        ldrb_reg(e, rd, R10, R2);
        uint16_t *done = b_fwd(e, C_AL);
        b_patch(slow, e->p);
        call_helper(e, CTX_HREAD);
        if (rd != R0)
            mov_reg(e, rd, R0);
        b_patch(done, e->p);
        break;
    }
    }
}

/* stores @rv (r0 or r4-r6); returns 1 if the block must end here */
static int emit_store(struct tr *t, struct ea ea, int rv)
{
    emit_t *e = t->e;
    switch (ea.loc) {
    case L_RAMC:
        strb_imm(e, rv, R10, ea.addr);
        return 0;
    case L_RAMR1:
        strb_reg(e, rv, R10, R1);
        return 0;
    case L_HIGHC:
        mov_reg(e, R2, rv);
        movw(e, R1, ea.addr);
        call_helper(e, CTX_HWRITE);
        return !(ea.addr >= 0x6000 && ea.addr < 0x8000);
    default: {
        cmp_imm(e, R1, 0x2000);
        uint16_t *slow = b_fwd(e, C_CS);
        ubfx(e, R3, R1, 0, 11);
        strb_reg(e, rv, R10, R3);
        uint16_t *done = b_fwd(e, C_AL);
        b_patch(slow, e->p);
        mov_reg(e, R2, rv);
        call_helper(e, CTX_HWRITE);
        cmp_imm(e, R0, 0);
        add_stub(t, b_fwd(e, C_NE), S_EXIT_IO, t->npc, 0); /* an I/O or mapper write: leave */
        b_patch(done, e->p);
        return 0;
    }
    }
}

static void set_nz(emit_t *e, int r)
{
    sxtb(e, R7, r);
}

static void push_reg(emit_t *e, int rv) /* rv != r0, r12 */
{
    ldrb_imm(e, R0, R11, CTX_SP);
    dp_imm(e, OP_ADD, 0, R12, R10, 0x100);
    strb_reg(e, rv, R12, R0);
    dp_imm(e, OP_SUB, 0, R0, R0, 1);
    strb_imm(e, R0, R11, CTX_SP);
}

static void pop_reg(emit_t *e, int rd)
{
    ldrb_imm(e, R0, R11, CTX_SP);
    dp_imm(e, OP_ADD, 0, R0, R0, 1);
    uxtb(e, R0, R0);
    strb_imm(e, R0, R11, CTX_SP);
    dp_imm(e, OP_ADD, 0, R12, R10, 0x100);
    ldrb_reg(e, rd, R12, R0);
}

/* P from the flag registers into r2 (for PHP: B and bit 5 set) */
static void pack_p(emit_t *e)
{
    shift_imm(e, R2, R7, SH_LSR, 31);
    shift_imm(e, R2, R2, SH_LSL, 7);
    tst_imm(e, R7, 0xff);
    it(e, C_EQ);
    dp_imm(e, OP_ORR, 0, R2, R2, 2);
    dp_reg(e, OP_ORR, 0, R2, R2, R8, SH_LSL, 0);
    ldrb_imm(e, R3, R11, CTX_V);
    dp_reg(e, OP_ORR, 0, R2, R2, R3, SH_LSL, 6);
    ldrb_imm(e, R3, R11, CTX_OTHER);
    dp_reg(e, OP_ORR, 0, R2, R2, R3, SH_LSL, 0);
    dp_imm(e, OP_ORR, 0, R2, R2, 0x30);
}

/* the flag registers from P in r2; bits 4 and 5 (no effect) as the interpreter does: from the
 * stack for PLP, kept for RTI (@keep45) */
static void unpack_p(emit_t *e, int keep45)
{
    dp_imm(e, OP_AND, 0, R8, R2, 1);
    ubfx(e, R3, R2, 6, 1);
    strb_imm(e, R3, R11, CTX_V);
    if (keep45) {
        ldrb_imm(e, R0, R11, CTX_OTHER);
        dp_imm(e, OP_AND, 0, R0, R0, 0x30);
        dp_imm(e, OP_AND, 0, R3, R2, 0x0c);
        dp_reg(e, OP_ORR, 0, R3, R3, R0, SH_LSL, 0);
    } else {
        dp_imm(e, OP_AND, 0, R3, R2, 0x3c);
    }
    strb_imm(e, R3, R11, CTX_OTHER);
    shift_imm(e, R7, R2, SH_LSL, 24);
    dp_imm(e, OP_AND, 0, R7, R7, 0x80000000u);
    tst_imm(e, R2, 2);
    it(e, C_EQ);
    dp_imm(e, OP_ORR, 0, R7, R7, 1);
}

/* r1 = A + r0 + C with C, V, N, Z (ADC; SBC with r0 inverted) */
static void emit_adc(emit_t *e)
{
    dp_reg(e, OP_ADD, 0, R1, R4, R0, SH_LSL, 0);
    dp_reg(e, OP_ADD, 0, R1, R1, R8, SH_LSL, 0);
    dp_reg(e, OP_EOR, 0, R2, R4, R1, SH_LSL, 0);
    dp_reg(e, OP_EOR, 0, R3, R0, R1, SH_LSL, 0);
    dp_reg(e, OP_AND, 0, R2, R2, R3, SH_LSL, 0);
    ubfx(e, R2, R2, 7, 1);
    strb_imm(e, R2, R11, CTX_V);
    shift_imm(e, R8, R1, SH_LSR, 8);
    uxtb(e, R4, R1);
    set_nz(e, R4);
}

static void emit_cmp(emit_t *e, int rr)
{
    dp_reg(e, OP_SUB, 0, R1, rr, R0, SH_LSL, 0);
    shift_imm(e, R8, R1, SH_LSR, 31);
    dp_imm(e, OP_EOR, 0, R8, R8, 1);
    set_nz(e, R1);
}

/* the read-modify-write operations on r0 */
static void emit_rmw_op(emit_t *e, int mn)
{
    switch (mn) {
    case M_INC:
        dp_imm(e, OP_ADD, 0, R0, R0, 1);
        uxtb(e, R0, R0);
        break;
    case M_DEC:
        dp_imm(e, OP_SUB, 0, R0, R0, 1);
        uxtb(e, R0, R0);
        break;
    case M_ASL:
        shift_imm(e, R0, R0, SH_LSL, 1);
        shift_imm(e, R8, R0, SH_LSR, 8);
        uxtb(e, R0, R0);
        break;
    case M_LSR:
        dp_imm(e, OP_AND, 0, R8, R0, 1);
        shift_imm(e, R0, R0, SH_LSR, 1);
        break;
    case M_ROL:
        shift_imm(e, R0, R0, SH_LSL, 1);
        dp_reg(e, OP_ORR, 0, R0, R0, R8, SH_LSL, 0);
        shift_imm(e, R8, R0, SH_LSR, 8);
        uxtb(e, R0, R0);
        break;
    case M_ROR:
        dp_reg(e, OP_ORR, 0, R0, R0, R8, SH_LSL, 8);
        dp_imm(e, OP_AND, 0, R8, R0, 1);
        shift_imm(e, R0, R0, SH_LSR, 1);
        break;
    }
    set_nz(e, R0);
}

/* Translates one instruction; returns 1 if the block ends with it */
static int translate(struct tr *t, const uint8_t *s, const struct op *o)
{
    emit_t *e = t->e;
    uint32_t pc = t->npc - (uint32_t)op_len(o->am);
    int ra = 0;
    switch (o->mn) {
    case M_LDA:
    case M_LDX:
    case M_LDY:
        ra = o->mn == M_LDA ? R4 : o->mn == M_LDX ? R5 : R6;
        if (o->am == AM_IMM)
            mov_imm(e, ra, s[1]);
        else
            emit_load(e, emit_ea(e, o->am, s, 1), ra);
        set_nz(e, ra);
        return 0;
    case M_STA:
    case M_STX:
    case M_STY:
        ra = o->mn == M_STA ? R4 : o->mn == M_STX ? R5 : R6;
        return emit_store(t, emit_ea(e, o->am, s, 0), ra);
    case M_ADC:
    case M_SBC:
    case M_AND:
    case M_ORA:
    case M_EOR:
    case M_CMP:
    case M_CPX:
    case M_CPY:
    case M_BIT:
        if (o->am == AM_IMM)
            mov_imm(e, R0, s[1]);
        else
            emit_load(e, emit_ea(e, o->am, s, 1), R0);
        switch (o->mn) {
        case M_SBC:
            dp_imm(e, OP_EOR, 0, R0, R0, 0xff);
            /* fall through */
        case M_ADC:
            emit_adc(e);
            break;
        case M_AND:
        case M_ORA:
        case M_EOR:
            dp_reg(e, o->mn == M_AND ? OP_AND : o->mn == M_ORA ? OP_ORR : OP_EOR, 0, R4, R4, R0, SH_LSL, 0);
            set_nz(e, R4);
            break;
        case M_CMP:
            emit_cmp(e, R4);
            break;
        case M_CPX:
            emit_cmp(e, R5);
            break;
        case M_CPY:
            emit_cmp(e, R6);
            break;
        case M_BIT:
            ubfx(e, R2, R0, 6, 1);
            strb_imm(e, R2, R11, CTX_V);
            dp_reg(e, OP_AND, 0, R1, R4, R0, SH_LSL, 0);
            shift_imm(e, R7, R0, SH_LSL, 24);
            dp_imm(e, OP_AND, 0, R7, R7, 0x80000000u);
            cmp_imm(e, R1, 0);
            it(e, C_NE);
            dp_imm(e, OP_ORR, 0, R7, R7, 1);
            break;
        }
        return 0;
    case M_INC:
    case M_DEC:
    case M_ASL:
    case M_LSR:
    case M_ROL:
    case M_ROR:
        if (o->am == AM_ACC) {
            mov_reg(e, R0, R4);
            emit_rmw_op(e, o->mn);
            mov_reg(e, R4, R0);
            return 0;
        } else {
            struct ea ea = emit_ea(e, o->am, s, 0);
            if (ea.loc == L_DYN)
                str_imm(e, R1, R11, CTX_TMP); /* the address outlives a helper call */
            emit_load(e, ea, R0);
            emit_rmw_op(e, o->mn);
            if (ea.loc == L_DYN)
                ldr_imm(e, R1, R11, CTX_TMP);
            return emit_store(t, ea, R0);
        }
    case M_INX:
    case M_INY:
    case M_DEX:
    case M_DEY:
        ra = (o->mn == M_INX || o->mn == M_DEX) ? R5 : R6;
        dp_imm(e, (o->mn == M_INX || o->mn == M_INY) ? OP_ADD : OP_SUB, 0, ra, ra, 1);
        uxtb(e, ra, ra);
        set_nz(e, ra);
        return 0;
    case M_TAX:
        mov_reg(e, R5, R4);
        set_nz(e, R5);
        return 0;
    case M_TAY:
        mov_reg(e, R6, R4);
        set_nz(e, R6);
        return 0;
    case M_TXA:
        mov_reg(e, R4, R5);
        set_nz(e, R4);
        return 0;
    case M_TYA:
        mov_reg(e, R4, R6);
        set_nz(e, R4);
        return 0;
    case M_TSX:
        ldrb_imm(e, R5, R11, CTX_SP);
        set_nz(e, R5);
        return 0;
    case M_TXS:
        strb_imm(e, R5, R11, CTX_SP);
        return 0;
    case M_PHA:
        push_reg(e, R4);
        return 0;
    case M_PLA:
        pop_reg(e, R4);
        set_nz(e, R4);
        return 0;
    case M_PHP:
        pack_p(e);
        push_reg(e, R2);
        return 0;
    case M_PLP:
        pop_reg(e, R2);
        unpack_p(e, 0);
        end_to(t, t->npc, 0); /* the I flag may have changed: see to interrupts */
        return 1;
    case M_CLC:
    case M_SEC:
        mov_imm(e, R8, o->mn == M_SEC);
        return 0;
    case M_CLV:
        mov_imm(e, R0, 0);
        strb_imm(e, R0, R11, CTX_V);
        return 0;
    case M_CLD:
    case M_SED:
    case M_CLI:
    case M_SEI: {
        uint32_t bit = (o->mn == M_CLD || o->mn == M_SED) ? 0x08 : 0x04;
        ldrb_imm(e, R0, R11, CTX_OTHER);
        dp_imm(e, (o->mn == M_SED || o->mn == M_SEI) ? OP_ORR : OP_BIC, 0, R0, R0, bit);
        strb_imm(e, R0, R11, CTX_OTHER);
        if (o->mn == M_CLI) {
            end_to(t, t->npc, 0);
            return 1;
        }
        return 0;
    }
    case M_NOP:
        return 0;
    case M_JMP:
        if (o->am == AM_ABS) {
            end_to(t, s[1] | (uint32_t)s[2] << 8, 1);
        } else {
            /* JMP (ptr): the high byte comes from the same page (the 6502's wrap) */
            uint32_t p = s[1] | (uint32_t)s[2] << 8, p2 = (p & 0xff00) | ((p + 1) & 0xff);
            if (p < 0x2000) {
                ldrb_imm(e, R0, R10, p & 0x7ff);
                ldrb_imm(e, R1, R10, p2 & 0x7ff);
            } else {
                movw(e, R1, p);
                call_helper(e, CTX_HREAD);
                str_imm(e, R0, R11, CTX_TMP);
                movw(e, R1, p2);
                call_helper(e, CTX_HREAD);
                mov_reg(e, R1, R0);
                ldr_imm(e, R0, R11, CTX_TMP);
            }
            dp_reg(e, OP_ORR, 0, R0, R0, R1, SH_LSL, 8);
            ldr_pc(e, R11, CTX_LEAVE);
        }
        return 1;
    case M_JSR: {
        uint32_t ret = pc + 2;
        mov_imm(e, R2, ret >> 8);
        push_reg(e, R2);
        mov_imm(e, R2, ret & 0xff);
        push_reg(e, R2);
        end_to(t, s[1] | (uint32_t)s[2] << 8, 1);
        return 1;
    }
    case M_RTS:
        pop_reg(e, R2);
        str_imm(e, R2, R11, CTX_TMP);
        pop_reg(e, R1);
        ldr_imm(e, R2, R11, CTX_TMP);
        dp_reg(e, OP_ORR, 0, R0, R2, R1, SH_LSL, 8);
        dp_imm(e, OP_ADD, 0, R0, R0, 1);
        uxth(e, R0, R0);
        ldr_pc(e, R11, CTX_LEAVE);
        return 1;
    case M_RTI:
        pop_reg(e, R2);
        unpack_p(e, 1);
        pop_reg(e, R2);
        str_imm(e, R2, R11, CTX_TMP);
        pop_reg(e, R1);
        ldr_imm(e, R2, R11, CTX_TMP);
        dp_reg(e, OP_ORR, 0, R0, R2, R1, SH_LSL, 8);
        mov_imm(e, R3, 1);
        strb_imm(e, R3, R11, CTX_RTI); /* the interrupt handler has ended */
        ldr_pc(e, R11, CTX_LEAVE);
        return 1;
    default: { /* branches */
        uint32_t target = (t->npc + (uint32_t)(int8_t)s[1]) & 0xffff;
        uint32_t extra = 1 + ((target & 0xff00) != (t->npc & 0xff00));
        int cond;
        switch (o->mn) {
        case M_BPL:
            cmp_imm(e, R7, 0);
            cond = C_GE;
            break;
        case M_BMI:
            cmp_imm(e, R7, 0);
            cond = C_LT;
            break;
        case M_BEQ:
            tst_imm(e, R7, 0xff);
            cond = C_EQ;
            break;
        case M_BNE:
            tst_imm(e, R7, 0xff);
            cond = C_NE;
            break;
        case M_BCC:
            cmp_imm(e, R8, 0);
            cond = C_EQ;
            break;
        case M_BCS:
            cmp_imm(e, R8, 0);
            cond = C_NE;
            break;
        case M_BVC:
            ldrb_imm(e, R0, R11, CTX_V);
            cmp_imm(e, R0, 0);
            cond = C_EQ;
            break;
        default: /* BVS */
            ldrb_imm(e, R0, R11, CTX_V);
            cmp_imm(e, R0, 0);
            cond = C_NE;
            break;
        }
        add_stub(t, b_fwd(e, cond), S_BRANCH, target, extra);
        return 0;
    }
    }
}

/* ---- blocks ------------------------------------------------------------------------------------ */

static uint32_t hash_of(const uint8_t *key)
{
    return ((uint32_t)(uintptr_t)key * 2654435761u) >> (32 - HASH_BITS);
}

static uint16_t *lookup(const uint8_t *key)
{
    for (uint32_t i = hash_of(key);; i = (i + 1) & (HASH_SIZE - 1)) {
        if (s_hash[i].key == key)
            return s_hash[i].code;
        if (!s_hash[i].key)
            return NULL;
    }
}

static void insert(const uint8_t *key, uint16_t *code)
{
    uint32_t i = hash_of(key);
    while (s_hash[i].key)
        i = (i + 1) & (HASH_SIZE - 1);
    s_hash[i].key = key;
    s_hash[i].code = code;
    s_hash_used++;
}

/* Translates the block at @pc (its first byte at @src); NULL if its first instruction is not
 * translated */
static uint16_t *compile(uint32_t pc, const uint8_t *src)
{
    if (s_e.end - s_e.p < 8192 || s_hash_used > HASH_SIZE * 3 / 4 || s_nlinks > MAX_LINKS - 2 * MAX_STUBS)
        flush_all();
    static struct tr t;
    t.e = &s_e;
    t.pc0 = pc;
    t.win = pc >> 13;
    t.n = 0;
    t.nstubs = 0;
    uint16_t *start = s_e.p;
    uint32_t links0 = s_nlinks;
    uint32_t win_end = (pc | 0x1fff) + 1;
    for (;;) {
        const uint8_t *s = src + (pc - t.pc0);
        const struct op *o = &OPS[s[0]];
        if (!o->mn || pc + (uint32_t)op_len(o->am) > win_end || t.n == MAX_INSNS) {
            if (!t.n)
                return NULL;
            end_to(&t, pc, o->mn && t.n == MAX_INSNS);
            break;
        }
        t.pcs[t.n] = pc;
        t.natives[t.n] = s_e.p;
        t.n++;
        t.npc = pc + (uint32_t)op_len(o->am);
        dp_imm(&s_e, OP_SUB, 1, R9, R9, o->cycles);
        add_stub(&t, b_fwd(&s_e, C_LE), S_BUDGET, pc, o->cycles);
        if (translate(&t, s, o)) {
            if (!(o->mn == M_JMP || o->mn == M_JSR || o->mn == M_RTS || o->mn == M_RTI || o->mn == M_PLP ||
                  o->mn == M_CLI))
                emit_exit(&t, t.npc, 0); /* after an I/O or mapper write */
            break;
        }
        pc = t.npc;
    }
    /* the stubs */
    for (int i = 0; i < t.nstubs; i++) {
        struct stub *st = &t.stubs[i];
        b_patch(st->from, s_e.p);
        switch (st->kind) {
        case S_BUDGET:
            dp_imm(&s_e, OP_ADD, 0, R9, R9, st->extra);
            movw(&s_e, R0, st->pc);
            movt(&s_e, R0, LINK_INTERP);
            ldr_pc(&s_e, R11, CTX_LEAVE);
            break;
        case S_BRANCH:
            dp_imm(&s_e, OP_SUB, 0, R9, R9, st->extra);
            /* fall through */
        default:
            if (st->native)
                b_to(&s_e, C_AL, st->native);
            else
                emit_exit(&t, st->pc, st->link);
            break;
        }
    }
    if (s_e.full) {
        /* out of room: start over next time */
        s_nlinks = links0;
        flush_all();
        return NULL;
    }
    crtos_cache_sync(start, (size_t)(s_e.p - start) * 2u);
    insert(src, start);
    s_stats.compiled++;
    return start;
}

/* ---- running ----------------------------------------------------------------------------------- */

static int must_interpret(const c6502 *cpu)
{
    return cpu->polled_interrupt || cpu->oam.phase != DMA_CLEAR || cpu->dmc.phase != DMA_CLEAR ||
           (cpu->mode & ~CPU_ISR) != CPU_EXEC || (cpu->interrupt & ~IRQ) || ((cpu->interrupt & IRQ) && !(cpu->sr & INTERRUPT));
}

/* one pass through translated code from cpu->pc; 0 if there is none for it */
static int run_block(Emulator *e)
{
    c6502 *cpu = &e->cpu;
    if (cpu->pc < 0x8000 || !e->mapper.prg_ptr)
        return 0;
    const uint8_t *key = e->mapper.prg_ptr(&e->mapper, cpu->pc);
    if (!key)
        return 0;
    uint16_t *code = lookup(key);
    if (!code && !(code = compile(cpu->pc, key)))
        return 0;
    struct jit_ctx *j = &s_ctx;
    uint8_t sr = cpu->sr;
    j->a = cpu->ac;
    j->x = cpu->x;
    j->y = cpu->y;
    j->nz = ((sr & NEGATIVE) ? 0x80000000u : 0) | ((sr & ZERO) ? 0 : 1);
    j->c = sr & CARRY;
    j->v = (sr & OVERFLW) != 0;
    j->other = sr & 0x3c;
    j->sp = cpu->sp;
    j->budget = g_clk_event - (uint32_t)cpu->t_cycles;
    jit_enter(j, (const uint8_t *)code + 1);
    cpu->ac = (uint8_t)j->a;
    cpu->x = (uint8_t)j->x;
    cpu->y = (uint8_t)j->y;
    cpu->sr = (uint8_t)(((int32_t)j->nz < 0 ? NEGATIVE : 0) | ((j->nz & 0xff) ? 0 : ZERO) | (j->c & 1) |
                        (j->v ? OVERFLW : 0) | j->other);
    cpu->sp = j->sp;
    cpu->t_cycles = g_clk_event - j->budget;
    cpu->pc = (uint16_t)j->pc;
    uint32_t link = j->pc >> 16;
    if (j->rti) {
        j->rti = 0;
        cpu->mode &= ~CPU_ISR; /* as the interpreter's RTI */
    }
    s_stats.runs++;
    if (link == LINK_INTERP) {
        s_stats.interpreted++;
        s_stats.why_budget++;
        execute(cpu); /* it reaches the checkpoint: cycle by cycle */
    } else if (link && s_links[link]) {
        /* a static exit in the same 8 KB window: jump there directly from now on */
        const uint8_t *tkey = e->mapper.prg_ptr(&e->mapper, cpu->pc);
        uint32_t flushes = s_stats.flushes;
        uint16_t *target = tkey ? lookup(tkey) : NULL;
        if (!target && tkey)
            target = compile(cpu->pc, tkey);
        /* (a compile that started over has thrown away the exit to patch) */
        if (target && flushes == s_stats.flushes && s_links[link]) {
            b_encode(s_links[link], target);
            crtos_cache_sync(s_links[link], 4);
            s_links[link] = NULL;
            s_stats.linked++;
        }
    }
    return 1;
}

void jit_frame(Emulator *e)
{
    c6502 *cpu = &e->cpu;
    uint32_t yield_at = (uint32_t)cpu->t_cycles + 1800u;
    while (!e->ppu.render) {
        if ((int32_t)((uint32_t)cpu->t_cycles - g_clk_event) >= 0) {
            nes_clock_sync(e);
            nes_clock_plan(e);
            continue;
        }
        if (!s_enabled || must_interpret(cpu)) {
            if (!cpu->polled_interrupt && ((cpu->interrupt & ~IRQ) || ((cpu->interrupt & IRQ) && !(cpu->sr & INTERRUPT))))
                cpu->polled_interrupt = cpu->interrupt; /* the interpreter takes it */
            execute(cpu);
            s_stats.interpreted++;
            s_stats.why_irq++;
        } else if (!run_block(e)) {
            if (cpu->pc < 0x8000) {
                s_stats.why_ram++;
                s_stats.ram_pc[cpu->pc >> 12]++;
            } else {
                s_stats.why_op++;
            }
            execute(cpu);
            s_stats.interpreted++;
        }
        if ((int32_t)((uint32_t)cpu->t_cycles - yield_at) >= 0) {
            crtos_yield();
            yield_at = (uint32_t)cpu->t_cycles + 1800u;
        }
    }
}

/* ---- self-test --------------------------------------------------------------------------------- */

static uint32_t s_rng = 0x2545f491u;

static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

/* an address in RAM or ROM, never I/O (whatever index is added to it) */
static uint32_t test_addr(void)
{
    return (rnd() & 1) ? (rnd() % 0x1f00u) : (0x8000u | (rnd() & 0x7fffu));
}

struct cpu_snap {
    uint8_t a, x, y, sp, p;
    uint16_t pc;
    uint32_t cycles;
    uint8_t ram[0x800];
};

static void snap(const Emulator *e, struct cpu_snap *s, uint32_t t0)
{
    s->a = e->cpu.ac;
    s->x = e->cpu.x;
    s->y = e->cpu.y;
    s->sp = e->cpu.sp;
    s->p = e->cpu.sr;
    s->pc = e->cpu.pc;
    s->cycles = (uint32_t)e->cpu.t_cycles - t0;
    memcpy(s->ram, e->mem.RAM, sizeof(s->ram));
}

static void restore(Emulator *e, const struct cpu_snap *s, uint32_t t0)
{
    e->cpu.ac = s->a;
    e->cpu.x = s->x;
    e->cpu.y = s->y;
    e->cpu.sp = s->sp;
    e->cpu.sr = s->p;
    e->cpu.pc = s->pc;
    e->cpu.t_cycles = t0;
    memcpy(e->mem.RAM, s->ram, sizeof(s->ram));
}

static Emulator s_test;

int jit_selftest(int trials)
{
    static uint8_t image[16 + 0x8000 + 0x2000];
    static struct cpu_snap s0, s1, s2;
    memset(image, 0, sizeof(image));
    memcpy(image, "NES\x1a", 4);
    image[4] = 2; /* 32 KB PRG: NROM */
    image[5] = 1;
    ROMData rd;
    memset(&rd, 0, sizeof(rd));
    rd.rom = image;
    rd.rom_size = sizeof(image);
    Emulator *e = &s_test;
    memset(e, 0, sizeof(*e));
    if (load_data(&rd, &e->mapper) < 0)
        return -1;
    e->type = NTSC;
    e->mapper.emulator = e;
    init_mem(e);
    init_ppu(e);
    init_cpu(e);
    init_APU(e);
    clock_start(e);
    if (jit_init(e))
        return -1;
    c6502 *cpu = &e->cpu;
    uint8_t *prg = e->mapper.PRG_ROM;
    int bad = 0, tested = 0;
    for (int op = 0; op < 256; op++) {
        const struct op *o = &OPS[op];
        if (!o->mn)
            continue;
        int len = op_len(o->am), opbad = 0;
        for (int i = 0; i < 0x8000; i++)
            prg[i] = (uint8_t)rnd();
        crtos_yield(); /* the others at this priority get their turn */
        for (int k = 0; k < trials; k++) {
            uint32_t addr = test_addr();
            uint8_t b1 = (uint8_t)rnd(), b2 = (uint8_t)rnd();
            if (o->am == AM_ABS || o->am == AM_ABX || o->am == AM_ABY || o->am == AM_IND) {
                b1 = (uint8_t)addr;
                b2 = (uint8_t)(addr >> 8);
            }
            /* a jump or branch onto itself loops natively until the budget ends: not
             * comparable with one interpreted instruction */
            if (((op == 0x4C || op == 0x20) && b1 == 0x00 && b2 == 0x80) || (o->am == AM_REL && b1 == 0xfe))
                b1 ^= 0x10;
            prg[0] = (uint8_t)op;
            prg[1] = b1;
            prg[2] = b2;
            prg[len] = 0x02; /* not translated: the block ends before it */
            for (int i = 0; i < 0x800; i++)
                e->mem.RAM[i] = (uint8_t)rnd();
            if (o->am == AM_INX || o->am == AM_INY) {
                /* the pointer (zero page, wrapping) into RAM or ROM */
                uint32_t zp = o->am == AM_INX ? ((b1 + (rnd() & 0xff)) & 0xff) : b1;
                uint32_t p = test_addr();
                if (o->am == AM_INX) {
                    cpu->x = (uint8_t)((zp - b1) & 0xff);
                } else {
                    cpu->y = (uint8_t)rnd();
                }
                e->mem.RAM[zp] = (uint8_t)p;
                e->mem.RAM[(zp + 1) & 0xff] = (uint8_t)(p >> 8);
            } else {
                cpu->x = (uint8_t)rnd();
                cpu->y = (uint8_t)rnd();
            }
            if (o->am == AM_IND) {
                uint32_t p = test_addr();
                /* the pointer is read at addr (RAM: set it; ROM: it is random anyway) */
                if (addr < 0x2000) {
                    e->mem.RAM[addr & 0x7ff] = (uint8_t)p;
                    e->mem.RAM[((addr & 0xff00) | ((addr + 1) & 0xff)) & 0x7ff] = (uint8_t)(p >> 8);
                }
            }
            cpu->ac = (uint8_t)rnd();
            cpu->sp = (uint8_t)rnd();
            cpu->sr = (uint8_t)((rnd() & 0xcf) | 0x20);
            cpu->pc = 0x8000;
            cpu->interrupt = 0;
            cpu->polled_interrupt = 0;
            uint32_t t0 = 1000;
            cpu->t_cycles = t0;
            clock_start(e); /* the PPU and the APU at t0 too (mapper writes sync them) */
            g_clk_event = t0 + 1000000u;
            snap(e, &s0, t0);
            s0.cycles = 0;

            execute(cpu); /* the reference */
            snap(e, &s1, t0);

            restore(e, &s0, t0);
            clock_start(e);
            g_clk_event = t0 + 1000000u;
            flush_all();
            int ran = run_block(e);
            snap(e, &s2, t0);
            tested++;

            int diff = !ran || s1.a != s2.a || s1.x != s2.x || s1.y != s2.y || s1.sp != s2.sp || s1.p != s2.p ||
                       s1.pc != s2.pc || s1.cycles != s2.cycles || memcmp(s1.ram, s2.ram, sizeof(s1.ram));
            if (diff) {
                bad++;
                if (++opbad <= 2) {
                    int ri = -1;
                    for (int i = 0; i < 0x800; i++)
                        if (s1.ram[i] != s2.ram[i]) {
                            ri = i;
                            break;
                        }
                    printf("jit: %02x %02x %02x (A %02x X %02x Y %02x SP %02x P %02x)%s\n"
                           "     interp: A %02x X %02x Y %02x SP %02x P %02x PC %04x %lu cycles\n"
                           "     jit:    A %02x X %02x Y %02x SP %02x P %02x PC %04x %lu cycles%s",
                           op, b1, b2, s0.a, s0.x, s0.y, s0.sp, s0.p, ran ? "" : " NOT TRANSLATED", s1.a, s1.x, s1.y,
                           s1.sp, s1.p, s1.pc, (unsigned long)s1.cycles, s2.a, s2.x, s2.y, s2.sp, s2.p, s2.pc,
                           (unsigned long)s2.cycles, ri >= 0 ? "" : "\n");
                    if (ri >= 0)
                        printf(", RAM[%03x] %02x vs %02x\n", ri, s1.ram[ri], s2.ram[ri]);
                }
            }
        }
    }
    printf("jit: self-test: %d cases, %d different\n", tested, bad);
    exit_ppu(&e->ppu);
    free_mapper(&e->mapper);
    return bad;
}
