/*
 * snes_fxjit.cpp - a dynamic recompiler for the Super FX (GSU) of Yoshi's Island, Star Fox and
 * the other Super FX games: the GSU's program in the cartridge ROM is translated to Thumb-2 in
 * blocks, once, and then runs natively with exactly the interpreter's (core/fxinst.cpp) results
 * and cycle counts. The interpreter stays for what is not translated.
 *
 * The core's GSU (cycle mode): fx_run() runs instructions while GSU.vCycles, master cycles, is
 * below the budget of the scan line. An instruction costs its fetches - from the GSU cache
 * (the first fetch from a 16-byte line fills it) or from ROM/RAM - and its data accesses. The
 * byte after an instruction is always fetched already (GSU.vPipe), so the byte after a jump
 * or branch runs before its target (the delay slot). The prefixes (ALT1-3, WITH, TO, FROM)
 * change what the next instruction does; branches keep them, and a prefix in a delay slot
 * applies to the target's first instruction.
 *
 * Blocks start where the pipe holds the byte before R15, with any prefixes pending. They run
 * up to an instruction not translated (STOP, CACHE, LJMP), a jump to a register's address
 * (JMP, a write to R15), or MAX_INSNS instructions. An unconditional jump (BRA, IWT R15) is
 * followed within the block (up to MAX_TRACES of them, not back into the block): its delay
 * slot becomes an ordinary instruction, after which comes the target - so that instruction is
 * no place to enter or leave the block (a state at its address means the next address comes
 * after it: a subroutine's return, JMP R11 with the same byte in its delay slot, looks just
 * like that).
 * Conditional branches and LOOP leave the block when taken, or jump back into it where a
 * group (below) starts with the same prefixes; their delay slot, a one-byte instruction, is
 * translated on both ways. The prefixes are resolved when translating. A block is keyed by its
 * address, the prefixes pending, the program bank and what its costs depend on: the clock and
 * multiplier speeds, the cache base (when the cache is on) and the plot mode. A program in the
 * GSU's RAM is interpreted.
 *
 * Exactness: the interpreter checks the budget before every instruction. Blocks are cut into
 * groups, and one check at the start of a group tells that all of the group will run: that the
 * cycles before its last instruction stay below the budget, with the cache lines it reads
 * filled (a fill takes a slower path, fxjit_fill). If not, the block leaves at the group start
 * and the interpreter runs the rest of the budget step by step. Groups end after a delay slot
 * and after RPIX, which leaves prefixes set when the pixel is outside the screen - then the
 * block leaves with the interpreter's state as it is.
 *
 * Registers of translated code: r4 &GSU, r5 GSU.vCycles, r6 the budget, r7 a LOOP's target.
 * The GSU's registers and flags stay in GSU; the prefixes in GSU (SFR, pvSreg, pvDreg) are
 * stored when leaving and before the handlers that read them. PLOT, RPIX, COLOR, CMODE, GETC,
 * RAMB and ROMB call the core's handlers. A block that leaves for a known address is linked
 * straight to the block there once that exists (the same key: nothing a block runs changes the
 * bank, the cache or the costs).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crtos.h>

#include "snes9x.h"
#include "fxinst.h"
#include "fxemu.h"
#include "snes_fxjit.h"
#include "snes_fxjit_emit.h"
#include "snes_fxjit_ops.h"

#define CODE_BYTES (512u * 1024u)
#define HASH_BITS 14
#define HASH_SIZE (1u << HASH_BITS)
#define MAX_LINKS 4000u
#define MAX_INSNS 80
#define MAX_STUBS (MAX_INSNS * 3 + 8)
#define MAX_TRACES 4            /* unconditional jumps a block follows */
#define GROUP_OPS 8             /* instructions of a group at most (the interpreter's part after it
                                 * leaves for the budget) */
#define MAX_SLOTS 2048u         /* target caches of jumps to a register's address */
#define ARM_AFTER 64            /* interpreter steps without a block before translating anyway */
#define NOJIT ((uint16_t *)1)   /* a hash entry: nothing to translate here */

#define OFF(f) ((uint32_t)offsetof(struct FxRegs_s, f))
static_assert(offsetof(struct FxRegs_s, avReg) == 0, "the GSU's registers at r4 + 4 * n");
static_assert(offsetof(struct FxRegs_s, vZero) == offsetof(struct FxRegs_s, vSign) + 4, "STRD vSign, vZero");
static_assert(offsetof(struct FxRegs_s, vSign) <= 1020, "STRD's offset");
static_assert(offsetof(struct FxRegs_s, bCycleMode) < 4096, "12-bit offsets");

enum { X_BUDGET, X_STATIC, X_DYN, X_END, X_ODD }; /* how translated code left */

/* the prefixes pending: ALT mode | B << 2 | SREG << 3 | DREG << 7 (0: none) */
#define ST(alt, b, s, d) ((uint32_t)(alt) | (uint32_t)(b) << 2 | (uint32_t)(s) << 3 | (uint32_t)(d) << 7)

struct fxblock {
    uint32_t r15, ctx;
    uint32_t st;
    uint16_t *code;             /* NULL: a free entry */
};

/* the target cache of a jump to a register's address (JMP, LOOP, a write to R15): the address
 * it went to last (R15 at the delay slot), the block there, that address' fetch cost - the
 * cache line (0: outside the cache) and cycles; translated code reads these four */
struct dslot {
    uint32_t target;
    uint16_t *code;             /* Thumb address; NULL: empty */
    uint32_t bit, cost;
    uint32_t ctx, st;           /* the jumping block's key and the prefixes at the target */
};

typedef void (*enter_fn)(const void *code, uint32_t budget);

static uint16_t *s_code;        /* the translations (NULL: no JIT) */
static uint16_t *s_first;       /* the first block (after the fixed code) */
static emit_t s_e;
static enter_fn s_enter;
static uint16_t *s_leave_state, *s_leave_plain;
static struct fxblock *s_hash;
static uint32_t s_hash_used;
static uint16_t **s_links;      /* link index -> the exit to patch */
static uint32_t s_nlinks;
static struct dslot *s_slots;
static uint32_t s_nslots;
static uint32_t s_pend_slot, s_pend_target, s_pend_flushes; /* a target cache to fill */
static volatile uint32_t s_exit; /* stored by translated code: X_* | link << 4 */
static int s_enabled, s_arm;
static uint32_t s_miss;
static uint8_t s_kind_off[K_COUNT];
static struct fxjit_stats s_stats;

extern "C" uint32_t fxjit_fill(uint32_t lines, uint32_t cycles, uint32_t budget);

/* ---- the fixed code ---------------------------------------------------------------------------- */

static void emit_fixed(void)
{
    emit_t *e = &s_e;
    /* enter(code, budget): the registers of translated code, then the block */
    uint16_t *enter = e->p;
    push(e, 0x4ff8); /* r3-r11, lr: 10 registers, the stack stays 8-byte aligned */
    mov32(e, Q4, (uint32_t)(uintptr_t)&GSU);
    ldr_imm(e, Q5, Q4, OFF(vCycles));
    mov_reg(e, Q6, Q1);
    bx(e, Q0);
    /* leaving with R15 in r0, the pipe in r1, how in r2 and the prefixes (ST) in r3 */
    s_leave_state = e->p;
    str_imm(e, Q0, Q4, 15 * 4);
    strb_imm(e, Q1, Q4, OFF(vPipe));
    ldr_imm(e, Q0, Q4, OFF(vStatusReg));
    dp_imm(e, OP_BIC, 0, Q0, Q0, FLG_ALT1 | FLG_ALT2 | FLG_B);
    dp_imm(e, OP_AND, 0, Q1, Q3, 3);
    dp_reg(e, OP_ORR, 0, Q0, Q0, Q1, SH_LSL, 8);
    ubfx(e, Q1, Q3, 2, 1);
    dp_reg(e, OP_ORR, 0, Q0, Q0, Q1, SH_LSL, 12);
    str_imm(e, Q0, Q4, OFF(vStatusReg));
    ubfx(e, Q1, Q3, 3, 4);
    dp_reg(e, OP_ADD, 0, Q1, Q4, Q1, SH_LSL, 2);
    str_imm(e, Q1, Q4, OFF(pvSreg));
    ubfx(e, Q1, Q3, 7, 4);
    dp_reg(e, OP_ADD, 0, Q1, Q4, Q1, SH_LSL, 2);
    str_imm(e, Q1, Q4, OFF(pvDreg));
    /* leaving with the GSU's state already stored */
    s_leave_plain = e->p;
    str_imm(e, Q5, Q4, OFF(vCycles));
    mov32(e, Q3, (uint32_t)(uintptr_t)&s_exit);
    str_imm(e, Q2, Q3, 0);
    pop(e, 0x8ff8); /* r3-r11, pc */
    s_enter = (enter_fn)((uintptr_t)enter | 1u);
}

static void flush_all(void)
{
    s_e.p = s_first;
    s_e.end = s_code + CODE_BYTES / 2u;
    s_e.full = 0;
    memset(s_hash, 0, HASH_SIZE * sizeof(s_hash[0]));
    s_hash_used = 0;
    s_nlinks = 1;
    s_nslots = 1;
    s_pend_slot = 0;
    s_stats.flushes++;
}

int fxjit_init(void)
{
    if (!s_code) {
        s_code = (uint16_t *)malloc(CODE_BYTES);
        s_hash = (struct fxblock *)malloc(HASH_SIZE * sizeof(s_hash[0]));
        s_links = (uint16_t **)malloc(MAX_LINKS * sizeof(s_links[0]));
        s_slots = (struct dslot *)malloc(MAX_SLOTS * sizeof(s_slots[0]));
        if (!s_code || !s_hash || !s_links || !s_slots) {
            free(s_code);
            free(s_hash);
            free(s_links);
            free(s_slots);
            s_code = NULL;
            s_hash = NULL;
            s_links = NULL;
            s_slots = NULL;
            s_enabled = 0;
            return -1;
        }
        s_e.p = s_code;
        s_e.end = s_code + CODE_BYTES / 2u;
        s_e.full = 0;
        emit_fixed();
        s_first = s_e.p;
        crtos_cache_sync(s_code, (size_t)(s_first - s_code) * 2u);
    }
    fxjit_reset();
    s_enabled = 1;
    return 0;
}

void fxjit_reset(void)
{
    if (!s_code)
        return;
    flush_all();
    memset(&s_stats, 0, sizeof(s_stats));
    s_arm = 0;
    s_miss = 0;
}

void fxjit_enable(int on)
{
    s_enabled = on && s_code;
}

/* ---- checking against the interpreter (fxjit_verify) ---------------------------------------------- */

static int s_verify;
static int s_vbank = -1;                /* checked only in this program bank (-1: all) */
static uint8_t *s_vram0, *s_vram1;      /* the GSU's RAM before a run, and after it (translated) */
static uint32_t s_vram_size;
static struct FxRegs_s s_v0, s_v1;      /* the GSU before a run, and after it (translated) */
static uint32_t s_vchecked, s_vbad;

void fxjit_verify(int on, int bank)
{
    s_verify = on;
    s_vbank = bank;
}

/* 1: registers only (the interpreter's run writes the RAM again); 2: the RAM compared too */
static int verify_ready(void)
{
    uint32_t size = GSU.nRamBanks * 0x10000u;
    if (s_vbank >= 0 && (int)(GSU.vPrgBankReg & 0x7f) != s_vbank)
        return 0;
    if (s_verify == 1)
        return 1;
    if (!s_verify || !GSU.pvRam || !size)
        return 0;
    if (size != s_vram_size) {
        free(s_vram0);
        free(s_vram1);
        s_vram0 = (uint8_t *)malloc(size);
        s_vram1 = (uint8_t *)malloc(size);
        s_vram_size = s_vram0 && s_vram1 ? size : 0;
    }
    return s_vram_size != 0;
}

#define VFIELD(f) \
    if (s_v1.f != GSU.f) { \
        printf("fxjit: verify: %s translated %08lx, interpreter %08lx\n", #f, (unsigned long)(uintptr_t)s_v1.f, \
               (unsigned long)(uintptr_t)GSU.f); \
        bad = 1; \
    }

/* The run from s_v0 (block @r15 - 1, prefixes @st) again, by the interpreter to the same cycle
 * count, and the two results compared; the interpreter's stays (@how: how the block left) */
static void verify_run(uint32_t r15, uint32_t ctx, uint32_t st, uint32_t how)
{
    int ram = s_verify == 2;
    s_v1 = GSU;
    if (ram) {
        memcpy(s_vram1, GSU.pvRam, s_vram_size);
        memcpy(GSU.pvRam, s_vram0, s_vram_size);
    }
    GSU = s_v0;
    unsigned steps = 0;
    while ((GSU.vStatusReg & FLG_G) && GSU.vCycles < s_v1.vCycles && steps < 1000000u) {
        FX_STEP;
        steps++;
    }
    s_vchecked++;
    int bad = 0;
    for (int i = 0; i < 16; i++)
        VFIELD(avReg[i]);
    VFIELD(vColorReg);
    VFIELD(vPlotOptionReg);
    VFIELD(vStatusReg);
    VFIELD(vPrgBankReg);
    VFIELD(vRomBankReg);
    VFIELD(vRamBankReg);
    VFIELD(vCacheBaseReg);
    VFIELD(vLastRamAdr);
    VFIELD(pvDreg);
    VFIELD(pvSreg);
    VFIELD(vRomBuffer);
    VFIELD(vPipe);
    VFIELD(vSign);
    VFIELD(vZero);
    VFIELD(vCarry);
    VFIELD(vOverflow);
    VFIELD(pvRamBank);
    VFIELD(pvRomBank);
    VFIELD(pvPrgBank);
    VFIELD(bCacheActive);
    VFIELD(vCacheMask);
    VFIELD(vCycles);
    for (uint32_t i = 0; ram && i < s_vram_size; i++)
        if (s_vram1[i] != GSU.pvRam[i]) {
            printf("fxjit: verify: RAM %05lx translated %02x, interpreter %02x\n", (unsigned long)i, s_vram1[i],
                   GSU.pvRam[i]);
            bad = 1;
            break;
        }
    if (!bad)
        return;
    s_vbad++;
    printf("fxjit: verify: run %lu from %02lx:%04lx (prefixes %03lx, key %08lx) left %lu at R15 %08lx, %lu steps\n",
           (unsigned long)s_vchecked, (unsigned long)(s_v0.vPrgBankReg & 0x7f), (unsigned long)((r15 - 1) & 0xffff),
           (unsigned long)st, (unsigned long)ctx, (unsigned long)(how & 15), (unsigned long)s_v1.avReg[15],
           (unsigned long)steps);
    printf("fxjit: verify: before: R0-15");
    for (int i = 0; i < 16; i++)
        printf(" %lx", (unsigned long)s_v0.avReg[i]);
    printf(", SFR %lx, sign %lx zero %lx carry %lx ov %lx, cycles %lu -> %lu\n", (unsigned long)s_v0.vStatusReg,
           (unsigned long)s_v0.vSign, (unsigned long)s_v0.vZero, (unsigned long)s_v0.vCarry,
           (unsigned long)s_v0.vOverflow, (unsigned long)s_v0.vCycles, (unsigned long)s_v1.vCycles);
}

const struct fxjit_verify_stats *fxjit_verify_stats(void)
{
    static struct fxjit_verify_stats v;
    v.checked = s_vchecked;
    v.bad = s_vbad;
    return &v;
}

void fxjit_kind_off(int kind)
{
    if (kind >= 0 && kind < K_COUNT)
        s_kind_off[kind] = 1;
}

const struct fxjit_stats *fxjit_stats(void)
{
    s_stats.code_bytes = s_code ? (uint32_t)((s_e.p - s_first) * 2) : 0;
    s_stats.blocks = s_hash_used;
    s_stats.code = s_code;
    return &s_stats;
}

/* A group needs cache lines not filled yet: charges them, if its instructions still run then
 * (@cycles: the count before its last one without the fills); returns their cycles + 1, or 0
 * to leave */
extern "C" uint32_t fxjit_fill(uint32_t lines, uint32_t cycles, uint32_t budget)
{
    uint32_t need = lines & ~GSU.vCacheMask;
    uint32_t n = 0;
    for (uint32_t m = need; m; m &= m - 1)
        n++;
    uint32_t fill = n * (GSU.vCostMem << 4);
    if (cycles + fill >= budget)
        return 0;
    GSU.vCacheMask |= need;
    return fill + 1u;
}

/* ---- blocks ------------------------------------------------------------------------------------ */

static uint32_t hash_of(uint32_t r15, uint32_t ctx, uint32_t st)
{
    return ((r15 ^ ctx * 0x9e3779b1u ^ st * 0x85ebca6bu) * 2654435761u) >> (32 - HASH_BITS);
}

static struct fxblock *lookup(uint32_t r15, uint32_t ctx, uint32_t st)
{
    for (uint32_t i = hash_of(r15, ctx, st);; i = (i + 1) & (HASH_SIZE - 1)) {
        struct fxblock *b = &s_hash[i];
        if (!b->code)
            return NULL;
        if (b->r15 == r15 && b->ctx == ctx && b->st == st)
            return b;
    }
}

static void insert(uint32_t r15, uint32_t ctx, uint32_t st, uint16_t *code)
{
    uint32_t i = hash_of(r15, ctx, st);
    while (s_hash[i].code)
        i = (i + 1) & (HASH_SIZE - 1);
    s_hash[i].r15 = r15;
    s_hash[i].ctx = ctx;
    s_hash[i].st = st;
    s_hash[i].code = code;
    s_hash_used++;
}

/* what a block's translation depends on besides its address and the prefixes */
static uint32_t context(void)
{
    uint32_t c = (GSU.vCostCache == 1) | (GSU.vCostMult == 1) << 1 | (GSU.vMode & 3) << 2 |
                 (GSU.vPrgBankReg & 0x7f) << 4;
    if (GSU.bCacheActive)
        c |= 1u << 11 | (GSU.vCacheBaseReg & 0xffff) << 12;
    return c;
}

/* the prefixes pending in the interpreter's state */
static uint32_t prefixes(void)
{
    uint32_t sfr = GSU.vStatusReg;
    return ST(sfr >> 8 & 3, sfr >> 12 & 1, (uint32_t)(GSU.pvSreg - GSU.avReg) & 15,
              (uint32_t)(GSU.pvDreg - GSU.avReg) & 15);
}

/* ---- decoding ---------------------------------------------------------------------------------- */

struct insn {
    uint32_t pc;                /* its opcode's address (as R15 counts: 32 bits) */
    uint32_t imm;               /* immediate; branches, IWT R15 and a known LOOP: the target */
    uint32_t cost;              /* cycles of its fetches (a delay slot's: none) and data accesses */
    uint32_t own;               /* of those, what its translation adds (PLOT, RPIX: the handler the rest) */
    uint32_t lines;             /* the cache lines its fetches read */
    uint16_t idx;               /* ALT mode << 8 | opcode */
    uint16_t tst;               /* a branch, LOOP or IWT R15: the prefixes at the target */
    uint8_t kind, arg, len;
    uint8_t alt, b, s, d;       /* the prefixes it runs with */
    uint8_t delay;              /* a delay slot: its fetch depends on the way taken */
    uint8_t known;              /* LOOP: R13 is imm */
    uint8_t group;              /* a group starts here */
    uint8_t traced;             /* an unconditional jump the block follows */
    uint8_t virt;               /* the delay slot of such a jump: the jump's target comes after it,
                                 * not the next address, so it is no place to enter or leave at */
};

enum stub_kind { S_BUDGET, S_FILL, S_TAKEN, S_ODD };

struct stub {
    uint16_t *from;             /* the branch to the stub */
    uint16_t *back;             /* S_FILL: where the block goes on */
    int kind, at;               /* S_BUDGET, S_FILL: the group; S_TAKEN: the branch */
    uint32_t lines, check;      /* S_FILL */
};

static struct tr {
    const uint8_t *bank;        /* the program bank */
    uint32_t start, st0, ctx;   /* the block's key */
    int resume;                 /* where the interpreter stopped for the budget: up to a block */
    uint32_t cbr;               /* the cache base */
    int active;                 /* the cache is on */
    uint32_t ccache, cmem, cmult, cfmult, cplot, crpix;
    struct insn in[MAX_INSNS + 2];
    int n;
    int open;                   /* the block ends with an exit to end_pc */
    uint32_t end_pc, end_st;
    uint16_t *label[MAX_INSNS + 2];     /* the code of group starts */
    uint16_t *budget[MAX_INSNS + 2];    /* their budget exits */
    struct stub stubs[MAX_STUBS];
    int nstubs;
    struct {
        uint16_t *at;
        int group;
    } fixes[MAX_STUBS];
    int nfixes;
} t;

static uint8_t mem(uint32_t addr)
{
    return t.bank[addr & 0xffff];
}

static uint32_t st_of(const struct insn *x)
{
    return ST(x->alt, x->b, x->s, x->d);
}

/* the cycles of fetching @addr (without a line fill); the line into *lines */
static uint32_t fetch_cost(uint32_t addr, uint32_t *lines)
{
    uint32_t o = (addr - t.cbr) & 0xffff;
    if (t.active && o < 512) {
        *lines |= 1u << (o >> 4);
        return t.ccache;
    }
    return t.cmem;
}

static int is_prefix(const struct insn *x)
{
    return x->kind == K_ALT || x->kind == K_WITH || ((x->kind == K_TO || x->kind == K_FROM) && !x->b);
}

/* the prefixes after prefix @x */
static void apply_prefix(const struct insn *x, int *alt, int *b, int *s, int *d)
{
    if (x->kind == K_ALT) {
        *alt |= x->arg;
        *b = 0;
    } else if (x->kind == K_WITH) {
        *b = 1;
        *s = *d = x->arg;
    } else if (x->kind == K_TO) {
        *d = x->arg;
    } else {
        *s = x->arg;
    }
}

/* whether it stores a result through the destination register (TESTR14 follows) */
static int writes_dreg(int kind, int b)
{
    switch (kind) {
    case K_LSR: case K_ROL: case K_ASR: case K_DIV2: case K_ROR: case K_SWAP: case K_NOT: case K_SEX:
    case K_LOB: case K_HIB: case K_MERGE: case K_ADD: case K_ADDI: case K_ADC: case K_ADCI: case K_SUB:
    case K_SUBI: case K_SBC: case K_AND: case K_ANDI: case K_BIC: case K_BICI: case K_OR: case K_ORI:
    case K_XOR: case K_XORI: case K_MULT: case K_MULTI: case K_UMULT: case K_UMULTI: case K_FMULT:
    case K_LMULT: case K_LDW: case K_LDB: case K_GETB: case K_GETBH: case K_GETBL: case K_GETBS: case K_RPIX:
        return 1;
    case K_FROM:
        return b; /* MOVES */
    default:
        return 0;
    }
}

/* whether it reads the register in arg */
static int reads_arg(int kind)
{
    switch (kind) {
    case K_ADD: case K_ADC: case K_SUB: case K_SBC: case K_CMP: case K_AND: case K_BIC: case K_OR: case K_XOR:
    case K_MULT: case K_UMULT: case K_FROM: case K_JMP: case K_LDW: case K_LDB: case K_STW: case K_STB:
    case K_SMS: case K_SM: case K_INC: case K_DEC:
        return 1;
    default:
        return 0;
    }
}

static int writes_r13(const struct insn *x)
{
    switch (x->kind) {
    case K_IWT: case K_IBT: case K_LMS: case K_LM: case K_INC: case K_DEC:
        return x->arg == 13;
    case K_TO:
        return x->b && x->arg == 13;
    default:
        return writes_dreg(x->kind, x->b) && x->d == 13;
    }
}

/* a prefix or: 0 not translated; 1 an instruction; 2 a branch, LOOP or IWT R15 (its delay slot
 * in the block); 3 JMP or another write to R15 (the block leaves before its delay slot) */
static int classify(const struct insn *x)
{
    int k = x->kind, n = x->arg;
    if (s_kind_off[k])
        return 0;
    switch (k) {
    case K_STOP: case K_CACHE: case K_LJMP:
        return 0;
    case K_BRANCH: case K_LOOP:
        return 2;
    case K_JMP:
        return 3;
    case K_IWT:
        return n == 15 ? 2 : 1;
    case K_IBT: case K_LMS: case K_LM: case K_TO:
        return n == 15 ? 3 : 1; /* (TO here: MOVE) */
    case K_RPIX:
        return x->d == 15 ? 0 : 1;
    default:
        return writes_dreg(k, x->b) && x->d == 15 ? 3 : 1;
    }
}

/* can it be translated as a delay slot: one byte, not R15 */
static int simple(const struct insn *x)
{
    if (is_prefix(x))
        return !s_kind_off[x->kind];
    switch (x->kind) {
    case K_NOP: case K_LSR: case K_ROL: case K_SWAP: case K_NOT: case K_ADD: case K_ADDI: case K_ADC:
    case K_ADCI: case K_SUB: case K_SUBI: case K_SBC: case K_CMP: case K_MERGE: case K_AND: case K_ANDI:
    case K_BIC: case K_BICI: case K_OR: case K_ORI: case K_XOR: case K_XORI: case K_MULT: case K_MULTI:
    case K_UMULT: case K_UMULTI: case K_SBK: case K_SEX: case K_ASR: case K_DIV2: case K_ROR: case K_LOB:
    case K_HIB: case K_INC: case K_DEC: case K_LDW: case K_LDB: case K_STW: case K_STB: case K_GETB:
    case K_GETBH: case K_GETBL: case K_GETBS: case K_FMULT: case K_LMULT: case K_TO: case K_FROM:
    case K_PLOT: case K_COLOR: case K_CMODE: case K_GETC: case K_RAMB: case K_ROMB:
        break;
    default:
        return 0;
    }
    if (x->s == 15 || x->d == 15 || (x->arg == 15 && (reads_arg(x->kind) || x->kind == K_TO)))
        return 0;
    return classify(x) == 1;
}

static uint32_t data_cost(int kind)
{
    switch (kind) {
    case K_LDW: case K_STW: case K_SBK: case K_LMS: case K_SMS: case K_LM: case K_SM:
        return t.cmem << 1;
    case K_LDB: case K_STB: case K_GETB: case K_GETBH: case K_GETBL: case K_GETBS:
        return t.cmem;
    case K_MULT: case K_MULTI: case K_UMULT: case K_UMULTI:
        return t.cmult;
    case K_FMULT: case K_LMULT:
        return t.cfmult;
    case K_PLOT:
        return t.cplot;
    case K_RPIX:
        return t.crpix;
    default:
        return 0;
    }
}

static void set_insn(struct insn *x, uint32_t pc, int alt, int b, int s, int d)
{
    uint8_t op = mem(pc);
    const struct fx_op *o = &FX_OPS[alt << 8 | op];
    memset(x, 0, sizeof(*x));
    x->pc = pc;
    x->idx = (uint16_t)(alt << 8 | op);
    x->kind = o->kind;
    x->arg = o->arg;
    x->len = 1;
    x->alt = (uint8_t)alt;
    x->b = (uint8_t)b;
    x->s = (uint8_t)s;
    x->d = (uint8_t)d;
}

/* the block ends before @pc, with prefixes @st pending */
static void cut(uint32_t pc, uint32_t st)
{
    t.open = 1;
    t.end_pc = pc;
    t.end_st = st;
}

static int find_insn(uint32_t pc);

static void decode(void)
{
    uint32_t pc = t.start;
    int alt = t.st0 & 3, b = t.st0 >> 2 & 1, s = t.st0 >> 3 & 15, d = t.st0 >> 7 & 15;
    int k13 = 0, w13 = -1, traces = 0;
    uint32_t v13 = 0;
    t.n = 0;
    t.open = 0;
    for (;;) {
        /* (after a jump followed: back where the block has been, it goes there; a resume
         * block ends where another block can be entered) */
        if (t.n >= MAX_INSNS || (traces && find_insn(pc) >= 0) ||
            (t.resume && t.n && lookup(pc + 1, t.ctx, ST(alt, b, s, d)))) {
            cut(pc, ST(alt, b, s, d));
            break;
        }
        struct insn *x = &t.in[t.n];
        set_insn(x, pc, alt, b, s, d);
        int k = x->kind, n = x->arg;
        if (is_prefix(x)) {
            if (s_kind_off[k]) {
                cut(pc, ST(alt, b, s, d));
                break;
            }
            x->cost = x->own = fetch_cost(pc + 1, &x->lines);
            t.n++;
            apply_prefix(x, &alt, &b, &s, &d);
            pc++;
            continue;
        }
        int c = classify(x);
        if (!c) {
            cut(pc, ST(alt, b, s, d));
            break;
        }
        switch (k) {
        case K_IBT: case K_LMS: case K_SMS:
            x->len = 2;
            x->imm = mem(pc + 1);
            break;
        case K_IWT: case K_LM: case K_SM:
            x->len = 3;
            x->imm = mem(pc + 1) | (uint32_t)mem(pc + 2) << 8;
            break;
        case K_BRANCH:
            x->len = 2;
            x->imm = pc + 2 + (uint32_t)(int32_t)(int8_t)mem(pc + 1);
            break;
        }
        for (int i = 1; i <= x->len; i++)
            x->cost += fetch_cost(pc + i, &x->lines);
        x->own = x->cost;
        x->cost += data_cost(k);
        if (k != K_PLOT && k != K_RPIX)
            x->own = x->cost;
        int nodelay = 0;
        if (c >= 2) {
            /* the delay slot: after a branch with the prefixes, after the others without */
            int da = alt, db = b, ds = s, dd = d;
            if (k != K_BRANCH)
                da = db = ds = dd = 0;
            struct insn *y = &t.in[t.n + 1];
            set_insn(y, pc + x->len, da, db, ds, dd);
            if (!simple(y) && c == 2) {
                cut(pc, ST(alt, b, s, d));
                break;
            }
            nodelay = !simple(y); /* (a jump to a register's address leaves before it) */
            y->delay = 1;
            if (is_prefix(y)) {
                apply_prefix(y, &da, &db, &ds, &dd);
            } else {
                y->cost = data_cost(y->kind);
                y->own = y->kind == K_PLOT ? 0 : y->cost; /* (the handler charges a plot) */
                da = db = ds = dd = 0;
            }
            x->tst = (uint16_t)ST(da, db, ds, dd);
        }
        /* R13 for LOOP */
        if ((k == K_IWT || k == K_IBT) && n == 13) {
            k13 = 1;
            w13 = t.n;
            v13 = k == K_IWT ? x->imm : (uint32_t)(int32_t)(int8_t)x->imm;
        } else if (k == K_TO && n == 13 && x->s == 15) {
            k13 = 1;
            w13 = t.n;
            v13 = pc + 1; /* MOVE R13, R15 */
        } else if (writes_r13(x)) {
            k13 = 0;
        }
        if (k == K_LOOP && k13) {
            x->known = 1;
            x->imm = v13;
        }
        t.n++;
        if (nodelay)
            break;
        if (c >= 2) {
            struct insn *y = &t.in[t.n++];
            if (c == 3)
                break;
            if (k == K_IWT || (k == K_BRANCH && n == B_BRA)) {
                /* an unconditional jump: the block goes on at the target (once there) */
                if (find_insn(x->imm) >= 0 || t.n > MAX_INSNS - 8 || traces >= MAX_TRACES)
                    break;
                traces++;
                x->traced = 1;
                y->delay = 0;
                y->virt = 1;
                uint32_t f = fetch_cost(x->imm, &y->lines);
                y->cost += f;
                y->own += f;
                pc = x->imm;
            } else {
                /* not taken: on after the delay slot */
                pc = y->pc + 1;
            }
            alt = x->tst & 3;
            b = x->tst >> 2 & 1;
            s = x->tst >> 3 & 15;
            d = x->tst >> 7 & 15;
            continue;
        }
        pc += x->len;
        alt = b = s = d = 0;
    }
    /* a LOOP's R13 is known if the block sets it once and nothing else writes it */
    for (int i = 0; i < t.n; i++) {
        struct insn *x = &t.in[i];
        if (x->kind != K_LOOP || !x->known)
            continue;
        for (int j = 0; j < t.n; j++)
            if (j != w13 && writes_r13(&t.in[j]))
                x->known = 0;
    }
}

static int find_insn(uint32_t pc)
{
    for (int i = 0; i < t.n; i++)
        if (t.in[i].pc == pc && !t.in[i].delay && !t.in[i].virt)
            return i;
    return -1;
}

static void mark_groups(void)
{
    t.in[0].group = 1;
    for (int i = 0; i + 1 < t.n; i++)
        if (t.in[i].delay || t.in[i].kind == K_RPIX)
            t.in[i + 1].group = 1;
    for (int i = 0; i < t.n; i++) {
        const struct insn *x = &t.in[i];
        if (x->delay || x->traced ||
            !(x->kind == K_BRANCH || (x->kind == K_IWT && x->arg == 15) || (x->kind == K_LOOP && x->known)))
            continue;
        int j = find_insn(x->imm);
        if (j >= 0 && st_of(&t.in[j]) == x->tst)
            t.in[j].group = 1;
    }
    if (t.open) {
        int j = find_insn(t.end_pc);
        if (j >= 0 && st_of(&t.in[j]) == t.end_st)
            t.in[j].group = 1;
    }
    /* groups of GROUP_OPS instructions at most */
    for (int i = 0, ops = 0; i < t.n; i++) {
        if (t.in[i].group)
            ops = 0;
        if (is_prefix(&t.in[i]))
            continue;
        if (++ops > GROUP_OPS && !t.in[i].delay && !t.in[i].virt) {
            t.in[i].group = 1;
            ops = 1;
        }
    }
}

/* ---- emitting ---------------------------------------------------------------------------------- */

static emit_t *E = &s_e;

static void add_const(int rd, int rn, uint32_t v)
{
    if (!v) {
        if (rd != rn)
            mov_reg(E, rd, rn);
    } else if (t2_imm(v) >= 0) {
        dp_imm(E, OP_ADD, 0, rd, rn, v);
    } else {
        mov32(E, QLR, v);
        dp_reg(E, OP_ADD, 0, rd, rn, QLR, SH_LSL, 0);
    }
}

/* host register @h = GSU register @n as the instruction at @pc reads it */
static void ld_r(int h, int n, uint32_t pc)
{
    if (n == 15)
        mov32(E, h, pc + 1);
    else
        ldr_imm(E, h, Q4, (uint32_t)n * 4u);
}

static void st_r(int h, int n)
{
    str_imm(E, h, Q4, (uint32_t)n * 4u);
}

/* READR14: GSU.vRomBuffer = ROM(value in @h) */
static void read_r14(int h)
{
    ldr_imm(E, Q12, Q4, OFF(pvRomBank));
    uxth(E, QLR, h);
    ldrb_reg(E, QLR, Q12, QLR);
    strb_imm(E, QLR, Q4, OFF(vRomBuffer));
}

/* DREG = @h, TESTR14 */
static void set_dreg(int h, const struct insn *x)
{
    st_r(h, x->d);
    if (x->d == 14)
        read_r14(h);
}

/* a register written directly, READR14 when it is R14 */
static void set_reg(int h, int n)
{
    st_r(h, n);
    if (n == 14)
        read_r14(h);
}

/* vSign = vZero = @h */
static void set_sz(int h)
{
    strd_imm(E, h, h, Q4, OFF(vSign));
}

static void ld_ram_base(void)
{
    ldr_imm(E, Q12, Q4, OFF(pvRamBank));
}

/* GSU.pvSreg for a handler that reads SREG */
static void store_sreg(int s)
{
    add_const(Q0, Q4, (uint32_t)s * 4u);
    str_imm(E, Q0, Q4, OFF(pvSreg));
}

/* the instruction @x without its fetch costs and without leaving */
static void emit_op(const struct insn *x)
{
    int k = x->kind, n = x->arg, s = x->s;
    uint32_t pc = x->pc;
    if (is_prefix(x))
        return;
    switch (k) {
    case K_NOP:
        break;
    case K_LSR:
        ld_r(Q0, s, pc);
        dp_imm(E, OP_AND, 0, Q3, Q0, 1);
        str_imm(E, Q3, Q4, OFF(vCarry));
        uxth(E, Q2, Q0);
        shift_imm(E, 0, Q2, Q2, SH_LSR, 1);
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_ROL:
        ld_r(Q0, s, pc);
        ldr_imm(E, Q3, Q4, OFF(vCarry));
        dp_reg(E, OP_ADD, 0, Q2, Q3, Q0, SH_LSL, 1);
        uxth(E, Q2, Q2);
        ubfx(E, Q3, Q0, 15, 1);
        str_imm(E, Q3, Q4, OFF(vCarry));
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_ASR:
        ld_r(Q0, s, pc);
        dp_imm(E, OP_AND, 0, Q3, Q0, 1);
        str_imm(E, Q3, Q4, OFF(vCarry));
        sxth(E, Q2, Q0);
        shift_imm(E, 0, Q2, Q2, SH_ASR, 1);
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_DIV2:
        ld_r(Q0, s, pc);
        sxth(E, Q0, Q0);
        dp_imm(E, OP_AND, 0, Q3, Q0, 1);
        str_imm(E, Q3, Q4, OFF(vCarry));
        shift_imm(E, 0, Q2, Q0, SH_ASR, 1);
        cmn_imm(E, Q0, 1);
        it(E, C_EQ);
        mov_imm(E, Q2, 0); /* -1 / 2 is 0 */
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_ROR:
        ld_r(Q0, s, pc);
        ldr_imm(E, Q3, Q4, OFF(vCarry));
        uxth(E, Q2, Q0);
        shift_imm(E, 0, Q2, Q2, SH_LSR, 1);
        dp_reg(E, OP_ORR, 0, Q2, Q2, Q3, SH_LSL, 15);
        dp_imm(E, OP_AND, 0, Q3, Q0, 1);
        str_imm(E, Q3, Q4, OFF(vCarry));
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_SWAP:
        ld_r(Q0, s, pc);
        rev16(E, Q2, Q0);
        uxth(E, Q2, Q2);
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_NOT:
        ld_r(Q0, s, pc);
        mvn_reg(E, Q2, Q0);
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_SEX:
        ld_r(Q0, s, pc);
        sxtb(E, Q2, Q0);
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_LOB:
    case K_HIB:
        ld_r(Q0, s, pc);
        if (k == K_LOB)
            uxtb(E, Q2, Q0);
        else
            ubfx(E, Q2, Q0, 8, 8);
        shift_imm(E, 0, Q3, Q2, SH_LSL, 8);
        set_sz(Q3);
        set_dreg(Q2, x);
        break;
    case K_MERGE:
        ldr_imm(E, Q0, Q4, 7 * 4);
        ldr_imm(E, Q1, Q4, 8 * 4);
        dp_imm(E, OP_AND, 0, Q2, Q0, 0xff00);
        ubfx(E, Q3, Q1, 8, 8);
        dp_reg(E, OP_ORR, 0, Q2, Q2, Q3, SH_LSL, 0);
        movw(E, Q3, 0xc0c0);
        dp_reg(E, OP_AND, 0, Q3, Q2, Q3, SH_LSL, 0);
        shift_imm(E, 0, Q3, Q3, SH_LSL, 16);
        str_imm(E, Q3, Q4, OFF(vOverflow));
        movw(E, Q1, 0xf0f0);
        tst_reg(E, Q2, Q1);
        mov_imm(E, Q3, 0);
        it(E, C_EQ);
        mov_imm(E, Q3, 1);
        str_imm(E, Q3, Q4, OFF(vZero));
        dp_reg(E, OP_ORR, 0, Q3, Q2, Q2, SH_LSL, 8);
        dp_imm(E, OP_AND, 0, Q3, Q3, 0x8000);
        str_imm(E, Q3, Q4, OFF(vSign));
        movw(E, Q1, 0xe0e0);
        tst_reg(E, Q2, Q1);
        mov_imm(E, Q3, 0);
        it(E, C_NE);
        mov_imm(E, Q3, 1);
        str_imm(E, Q3, Q4, OFF(vCarry));
        set_dreg(Q2, x);
        break;
    case K_ADD: case K_ADDI: case K_ADC: case K_ADCI:
        ld_r(Q0, s, pc);
        if (k == K_ADD || k == K_ADC)
            ld_r(Q1, n, pc);
        else
            mov_imm(E, Q1, (uint32_t)n);
        uxth(E, Q2, Q0);
        uxth(E, Q3, Q1);
        dp_reg(E, OP_ADD, 0, Q2, Q2, Q3, SH_LSL, 0);
        if (k == K_ADC || k == K_ADCI) {
            /* (the carry is always 0 or 1: SEX16 and SUSEX16 of it are the same) */
            ldr_imm(E, Q3, Q4, OFF(vCarry));
            dp_reg(E, OP_ADD, 0, Q2, Q2, Q3, SH_LSL, 0);
        }
        shift_imm(E, 0, Q3, Q2, SH_LSR, 16); /* the sum is below 0x20000 */
        str_imm(E, Q3, Q4, OFF(vCarry));
        dp_reg(E, OP_EOR, 0, Q3, Q0, Q1, SH_LSL, 0);
        dp_reg(E, OP_EOR, 0, Q12, Q1, Q2, SH_LSL, 0);
        dp_reg(E, OP_BIC, 0, Q3, Q12, Q3, SH_LSL, 0);
        dp_imm(E, OP_AND, 0, Q3, Q3, 0x8000);
        str_imm(E, Q3, Q4, OFF(vOverflow));
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_SUB: case K_SUBI: case K_SBC: case K_CMP:
        ld_r(Q0, s, pc);
        if (k == K_SUBI)
            mov_imm(E, Q1, (uint32_t)n);
        else
            ld_r(Q1, n, pc);
        uxth(E, Q2, Q0);
        uxth(E, Q3, Q1);
        dp_reg(E, OP_SUB, 0, Q2, Q2, Q3, SH_LSL, 0);
        if (k == K_SBC) {
            ldr_imm(E, Q3, Q4, OFF(vCarry));
            dp_imm(E, OP_EOR, 0, Q3, Q3, 1);
            uxth(E, Q3, Q3);
            dp_reg(E, OP_SUB, 0, Q2, Q2, Q3, SH_LSL, 0);
        }
        mvn_reg(E, Q3, Q2);
        shift_imm(E, 0, Q3, Q3, SH_LSR, 31); /* s >= 0 */
        str_imm(E, Q3, Q4, OFF(vCarry));
        dp_reg(E, OP_EOR, 0, Q3, Q0, Q1, SH_LSL, 0);
        dp_reg(E, OP_EOR, 0, Q12, Q0, Q2, SH_LSL, 0);
        dp_reg(E, OP_AND, 0, Q3, Q3, Q12, SH_LSL, 0);
        dp_imm(E, OP_AND, 0, Q3, Q3, 0x8000);
        str_imm(E, Q3, Q4, OFF(vOverflow));
        set_sz(Q2);
        if (k != K_CMP)
            set_dreg(Q2, x);
        break;
    case K_AND: case K_BIC: case K_OR: case K_XOR:
    case K_ANDI: case K_BICI: case K_ORI: case K_XORI: {
        int op = (k == K_AND || k == K_ANDI) ? OP_AND : (k == K_BIC || k == K_BICI) ? OP_BIC
               : (k == K_OR || k == K_ORI) ? OP_ORR : OP_EOR;
        ld_r(Q0, s, pc);
        if (k == K_AND || k == K_BIC || k == K_OR || k == K_XOR) {
            ld_r(Q1, n, pc);
            dp_reg(E, op, 0, Q2, Q0, Q1, SH_LSL, 0);
        } else {
            dp_imm(E, op, 0, Q2, Q0, (uint32_t)n);
        }
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    }
    case K_MULT: case K_MULTI: case K_UMULT: case K_UMULTI:
        ld_r(Q0, s, pc);
        if (k == K_MULT || k == K_MULTI)
            sxtb(E, Q0, Q0);
        else
            uxtb(E, Q0, Q0);
        if (k == K_MULT || k == K_UMULT) {
            ld_r(Q1, n, pc);
            if (k == K_MULT)
                sxtb(E, Q1, Q1);
            else
                uxtb(E, Q1, Q1);
        } else {
            mov_imm(E, Q1, (uint32_t)n);
        }
        mul(E, Q2, Q0, Q1);
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_FMULT: case K_LMULT:
        ld_r(Q0, s, pc);
        ldr_imm(E, Q1, Q4, 6 * 4);
        sxth(E, Q0, Q0);
        sxth(E, Q1, Q1);
        mul(E, Q3, Q0, Q1);
        shift_imm(E, 0, Q2, Q3, SH_LSR, 16);
        if (k == K_LMULT)
            st_r(Q3, 4);
        set_sz(Q2);
        set_dreg(Q2, x);
        /* the carry: bit 15 of the product (LMULT: of R4, the result if that is DREG) */
        ubfx(E, Q3, (k == K_LMULT && x->d == 4) ? Q2 : Q3, 15, 1);
        str_imm(E, Q3, Q4, OFF(vCarry));
        break;
    case K_INC: case K_DEC:
        ldr_imm(E, Q2, Q4, (uint32_t)n * 4u);
        dp_imm(E, k == K_INC ? OP_ADD : OP_SUB, 0, Q2, Q2, 1);
        set_sz(Q2);
        set_reg(Q2, n);
        break;
    case K_IBT:
        mov32(E, Q2, (uint32_t)(int32_t)(int8_t)x->imm);
        set_reg(Q2, n);
        break;
    case K_IWT:
        mov32(E, Q2, x->imm);
        set_reg(Q2, n);
        break;
    case K_LMS: case K_LM: {
        uint32_t a = k == K_LMS ? x->imm << 1 : x->imm;
        uint32_t a2 = k == K_LMS ? a + 1 : a ^ 1;
        ld_ram_base();
        mov32(E, Q1, a);
        ldrb_reg(E, Q2, Q12, Q1);
        mov32(E, Q1, a2);
        ldrb_reg(E, Q3, Q12, Q1);
        dp_reg(E, OP_ORR, 0, Q2, Q2, Q3, SH_LSL, 8);
        mov32(E, Q1, a);
        str_imm(E, Q1, Q4, OFF(vLastRamAdr));
        set_reg(Q2, n);
        break;
    }
    case K_SMS: case K_SM: {
        uint32_t a = k == K_SMS ? x->imm << 1 : x->imm;
        uint32_t a2 = k == K_SMS ? a + 1 : a ^ 1;
        ld_r(Q2, n, pc);
        ld_ram_base();
        mov32(E, Q1, a);
        strb_reg(E, Q2, Q12, Q1);
        str_imm(E, Q1, Q4, OFF(vLastRamAdr));
        mov32(E, Q1, a2);
        shift_imm(E, 0, Q3, Q2, SH_LSR, 8);
        strb_reg(E, Q3, Q12, Q1);
        break;
    }
    case K_LDW: case K_LDB:
        ld_r(Q0, n, pc);
        str_imm(E, Q0, Q4, OFF(vLastRamAdr));
        ld_ram_base();
        uxth(E, Q1, Q0);
        ldrb_reg(E, Q2, Q12, Q1);
        if (k == K_LDW) {
            dp_imm(E, OP_EOR, 0, Q1, Q1, 1);
            ldrb_reg(E, Q3, Q12, Q1);
            dp_reg(E, OP_ORR, 0, Q2, Q2, Q3, SH_LSL, 8);
        }
        set_dreg(Q2, x);
        break;
    case K_STW: case K_STB: case K_SBK:
        if (k == K_SBK) {
            ldr_imm(E, Q0, Q4, OFF(vLastRamAdr));
        } else {
            ld_r(Q0, n, pc);
            str_imm(E, Q0, Q4, OFF(vLastRamAdr));
        }
        ld_r(Q1, s, pc);
        ld_ram_base();
        uxth(E, Q2, Q0);
        strb_reg(E, Q1, Q12, Q2);
        if (k != K_STB) {
            dp_imm(E, OP_EOR, 0, Q2, Q2, 1);
            shift_imm(E, 0, Q3, Q1, SH_LSR, 8);
            strb_reg(E, Q3, Q12, Q2);
        }
        break;
    case K_LINK:
        mov32(E, Q2, pc + 1 + (uint32_t)n);
        st_r(Q2, 11);
        break;
    case K_GETB:
        ldrb_imm(E, Q2, Q4, OFF(vRomBuffer));
        set_dreg(Q2, x);
        break;
    case K_GETBH:
        ld_r(Q0, s, pc);
        uxtb(E, Q2, Q0);
        ldrb_imm(E, Q3, Q4, OFF(vRomBuffer));
        dp_reg(E, OP_ORR, 0, Q2, Q2, Q3, SH_LSL, 8);
        set_dreg(Q2, x);
        break;
    case K_GETBL:
        ld_r(Q0, s, pc);
        dp_imm(E, OP_AND, 0, Q2, Q0, 0xff00);
        ldrb_imm(E, Q3, Q4, OFF(vRomBuffer));
        dp_reg(E, OP_ORR, 0, Q2, Q2, Q3, SH_LSL, 0);
        set_dreg(Q2, x);
        break;
    case K_GETBS:
        ldrb_imm(E, Q3, Q4, OFF(vRomBuffer));
        sxtb(E, Q2, Q3);
        set_dreg(Q2, x);
        break;
    case K_TO: /* MOVE: R[n] = SREG */
        ld_r(Q2, s, pc);
        set_reg(Q2, n);
        break;
    case K_FROM: /* MOVES: DREG = R[n], with the flags */
        ld_r(Q2, n, pc);
        dp_imm(E, OP_AND, 0, Q3, Q2, 0x80);
        shift_imm(E, 0, Q3, Q3, SH_LSL, 16);
        str_imm(E, Q3, Q4, OFF(vOverflow));
        set_sz(Q2);
        set_dreg(Q2, x);
        break;
    case K_COLOR: case K_CMODE: case K_RAMB: case K_ROMB: case K_GETC:
        /* the core's handler: it reads SREG (GETC nothing) and ends with CLRFLAGS */
        if (k != K_GETC)
            store_sreg(s);
        mov32(E, Q12, (uint32_t)(uintptr_t)fx_OpcodeTable[x->idx]);
        blx(E, Q12);
        break;
    case K_PLOT:
        /* GSU.pfPlot for the mode (part of the block's key): it charges its cycles, ends with
         * CLRFLAGS and reads neither SREG nor DREG */
        str_imm(E, Q5, Q4, OFF(vCycles));
        ldr_imm(E, Q12, Q4, OFF(pfPlot));
        blx(E, Q12);
        ldr_imm(E, Q5, Q4, OFF(vCycles));
        break;
    default:
        E->full = 1; /* (cannot happen: classify() let it through) */
        break;
    }
}

static void add_stub(uint16_t *from, int kind, int at)
{
    if (t.nstubs >= MAX_STUBS || !from) {
        E->full = 1;
        return;
    }
    struct stub *st = &t.stubs[t.nstubs++];
    memset(st, 0, sizeof(*st));
    st->from = from;
    st->kind = kind;
    st->at = at;
}

/* RPIX: the handler with the interpreter's state; the block leaves if it returned early (the
 * pixel outside the screen: prefixes still set) */
static void emit_rpix(const struct insn *x)
{
    mov32(E, Q0, x->pc + 1);
    st_r(Q0, 15);
    mov_imm(E, Q0, mem(x->pc + 1));
    strb_imm(E, Q0, Q4, OFF(vPipe));
    ldr_imm(E, Q0, Q4, OFF(vStatusReg));
    dp_imm(E, OP_BIC, 0, Q0, Q0, FLG_ALT1 | FLG_ALT2 | FLG_B);
    dp_imm(E, OP_ORR, 0, Q0, Q0, (uint32_t)x->alt << 8); /* (RPIX has ALT1) */
    if (x->b)
        dp_imm(E, OP_ORR, 0, Q0, Q0, FLG_B);
    str_imm(E, Q0, Q4, OFF(vStatusReg));
    store_sreg(x->s);
    add_const(Q0, Q4, (uint32_t)x->d * 4u);
    str_imm(E, Q0, Q4, OFF(pvDreg));
    str_imm(E, Q5, Q4, OFF(vCycles));
    ldr_imm(E, Q12, Q4, OFF(pfRpix));
    blx(E, Q12);
    ldr_imm(E, Q5, Q4, OFF(vCycles));
    ldr_imm(E, Q0, Q4, OFF(vStatusReg));
    tst_imm(E, Q0, FLG_ALT1 | FLG_ALT2 | FLG_B);
    add_stub(b_fwd(E, C_NE), S_ODD, 0);
    ldr_imm(E, Q0, Q4, 15 * 4);
    mov32(E, Q1, x->pc + 2);
    cmp_reg(E, Q0, Q1);
    add_stub(b_fwd(E, C_NE), S_ODD, 0);
}

static uint32_t new_link(void)
{
    if (s_nlinks >= MAX_LINKS)
        return 0;
    return s_nlinks++;
}

/* leave (R15 in r0, the pipe in r1) */
static void leave(uint32_t how, uint32_t st)
{
    movw(E, Q2, how);
    movw(E, Q3, st);
    b_to(E, C_AL, s_leave_state);
}

/* leave for @target with prefixes @st: the first instruction is replaced by a branch when the
 * block there is linked */
static void emit_exit_static(uint32_t target, uint32_t st, int kind)
{
    uint32_t link = new_link();
    uint16_t *at = E->p;
    movw(E, Q0, (target + 1) & 0xffff);
    movt(E, Q0, (target + 1) >> 16);
    mov_imm(E, Q1, mem(target));
    leave((uint32_t)kind | link << 4, st);
    if (link && !E->full)
        s_links[link] = at;
}

/* on at @target with prefixes @st: into this block where a group starts there, else leave
 * (X_STATIC or X_END) */
static void goto_target(uint32_t target, uint32_t st, int kind = X_STATIC)
{
    int j = find_insn(target);
    if (j < 0 || !t.in[j].group || st_of(&t.in[j]) != st) {
        emit_exit_static(target, st, kind);
    } else if (t.label[j]) {
        b_to(E, C_AL, t.label[j]);
    } else if (t.nfixes < MAX_STUBS) {
        t.fixes[t.nfixes].at = b_fwd(E, C_AL);
        t.fixes[t.nfixes].group = j;
        t.nfixes++;
    } else {
        E->full = 1;
    }
}

/* the fetch of @addr (by a delay slot) and @extra cycles */
static void fetch_add(uint32_t addr, uint32_t extra)
{
    uint32_t o = (addr - t.cbr) & 0xffff;
    if (t.active && o < 512) {
        uint32_t bit = 1u << (o >> 4);
        ldr_imm(E, Q0, Q4, OFF(vCacheMask));
        tst_imm(E, Q0, bit);
        uint16_t *filled = b_fwd(E, C_NE);
        dp_imm(E, OP_ORR, 0, Q0, Q0, bit);
        str_imm(E, Q0, Q4, OFF(vCacheMask));
        add_const(Q5, Q5, t.cmem << 4);
        b_patch(filled, E->p);
        add_const(Q5, Q5, t.ccache + extra);
    } else {
        add_const(Q5, Q5, t.cmem + extra);
    }
}

/* the delay slot @y, then on at @target with prefixes @st */
static void delay_path(const struct insn *y, uint32_t target, uint32_t st)
{
    emit_op(y);
    fetch_add(target, y->own);
    goto_target(target, st);
}

static uint32_t new_slot(uint32_t st)
{
    if (s_nslots >= MAX_SLOTS)
        return 0;
    struct dslot *sl = &s_slots[s_nslots];
    memset(sl, 0, sizeof(*sl));
    sl->ctx = t.ctx;
    sl->st = st;
    return s_nslots++;
}

/* on at the address in r7 after the delay slot @y, with prefixes @st: through the jump's
 * target cache if it holds that address, else leave before the delay slot (the dispatcher
 * fills the cache) */
static void emit_dyn(const struct insn *y, uint32_t st)
{
    uint32_t slot = new_slot(st);
    if (slot) {
        uint32_t a = (uint32_t)(uintptr_t)&s_slots[slot];
        mov32(E, Q12, a);
        ldr_imm(E, Q0, Q12, 0);
        ldr_imm(E, Q1, Q12, 4);
        cmp_reg(E, Q0, Q7);
        uint16_t *miss1 = b_fwd(E, C_NE);
        cmp_imm(E, Q1, 0);
        uint16_t *miss2 = b_fwd(E, C_EQ);
        emit_op(y);
        mov32(E, Q12, a);
        ldr_imm(E, Q1, Q12, 8);
        cmp_imm(E, Q1, 0);
        uint16_t *nofill = b_fwd(E, C_EQ);
        ldr_imm(E, Q0, Q4, OFF(vCacheMask));
        tst_reg(E, Q0, Q1);
        uint16_t *filled = b_fwd(E, C_NE);
        dp_reg(E, OP_ORR, 0, Q0, Q0, Q1, SH_LSL, 0);
        str_imm(E, Q0, Q4, OFF(vCacheMask));
        add_const(Q5, Q5, t.cmem << 4);
        b_patch(nofill, E->p);
        b_patch(filled, E->p);
        ldr_imm(E, Q1, Q12, 12);
        dp_reg(E, OP_ADD, 0, Q5, Q5, Q1, SH_LSL, 0);
        add_const(Q5, Q5, y->own);
        ldr_imm(E, QPC, Q12, 4);
        b_patch(miss1, E->p);
        b_patch(miss2, E->p);
    }
    mov_reg(E, Q0, Q7);
    mov_imm(E, Q1, mem(y->pc));
    leave(X_DYN | slot << 4, 0);
}

/* the flags for branch condition @c; returns the host condition of "taken" */
static int emit_cond(int c)
{
    switch (c) {
    case B_BNE: case B_BEQ:
        ldr_imm(E, Q0, Q4, OFF(vZero));
        shift_imm(E, 1, Q0, Q0, SH_LSL, 16);
        return c == B_BNE ? C_NE : C_EQ;
    case B_BPL: case B_BMI:
        ldr_imm(E, Q0, Q4, OFF(vSign));
        tst_imm(E, Q0, 0x8000);
        return c == B_BMI ? C_NE : C_EQ;
    case B_BCC: case B_BCS:
        ldr_imm(E, Q0, Q4, OFF(vCarry));
        tst_imm(E, Q0, 1);
        return c == B_BCS ? C_NE : C_EQ;
    case B_BVC: case B_BVS:
        /* overflow: vOverflow outside -0x8000..0x7fff */
        ldr_imm(E, Q0, Q4, OFF(vOverflow));
        add_const(Q0, Q0, 0x8000);
        shift_imm(E, 1, Q0, Q0, SH_LSR, 16);
        return c == B_BVS ? C_NE : C_EQ;
    default: /* B_BGE, B_BLT: sign against overflow */
        ldr_imm(E, Q0, Q4, OFF(vOverflow));
        add_const(Q0, Q0, 0x8000);
        shift_imm(E, 1, Q0, Q0, SH_LSR, 16);
        it(E, C_NE);
        mov_imm(E, Q0, 1);
        ldr_imm(E, Q1, Q4, OFF(vSign));
        ubfx(E, Q1, Q1, 15, 1);
        dp_reg(E, OP_EOR, 1, Q0, Q0, Q1, SH_LSL, 0);
        return c == B_BLT ? C_NE : C_EQ;
    }
}

/* the jump or branch in[@k] (its delay slot follows it, but after JMP and other writes to R15) */
static void emit_jump(int k)
{
    const struct insn *x = &t.in[k], *y = &t.in[k + 1];
    if (classify(x) == 3) {
        /* the target to r7; without its delay slot in the block, leave before it (the
         * interpreter runs it, then the target's block) */
        if (x->kind == K_JMP) {
            ldr_imm(E, Q7, Q4, (uint32_t)x->arg * 4u);
        } else {
            emit_op(x);
            ldr_imm(E, Q7, Q4, 15 * 4);
        }
        if (k + 1 < t.n && y->delay) {
            emit_dyn(y, x->tst);
        } else {
            mov_reg(E, Q0, Q7);
            mov_imm(E, Q1, mem(x->pc + x->len));
            leave(X_DYN, 0);
        }
        return;
    }
    switch (x->kind) {
    case K_IWT: /* R15 */
        delay_path(y, x->imm, x->tst);
        break;
    case K_BRANCH:
        if (x->arg == B_BRA) {
            delay_path(y, x->imm, x->tst);
            break;
        }
        add_stub(b_fwd(E, emit_cond(x->arg)), S_TAKEN, k);
        emit_op(y);
        fetch_add(y->pc + 1, y->own);
        break;
    case K_LOOP:
        ldr_imm(E, Q2, Q4, 12 * 4);
        dp_imm(E, OP_SUB, 0, Q2, Q2, 1);
        st_r(Q2, 12);
        set_sz(Q2);
        if (!x->known)
            ldr_imm(E, Q7, Q4, 13 * 4); /* R15 = R13 happens before the delay slot */
        shift_imm(E, 1, Q3, Q2, SH_LSL, 16);
        add_stub(b_fwd(E, C_NE), S_TAKEN, k);
        emit_op(y);
        fetch_add(y->pc + 1, y->own);
        break;
    }
}

/* the check at the start of group [@a, @z]: see the top of the file */
static void emit_group(int a, int z)
{
    uint32_t check = 0, add = 0, lines = 0;
    for (int i = a; i <= z; i++) {
        const struct insn *x = &t.in[i];
        lines |= x->lines;
        if (!x->delay)
            add += x->own;
        if (i != z)
            check += x->cost;
    }
    t.label[a] = E->p;
    uint16_t *fill = NULL;
    if (lines) {
        ldr_imm(E, Q0, Q4, OFF(vCacheMask));
        if (t2_imm(lines) >= 0) {
            dp_imm(E, OP_AND, 0, Q1, Q0, lines);
            cmp_imm(E, Q1, lines);
        } else {
            mov32(E, Q2, lines);
            dp_reg(E, OP_AND, 0, Q1, Q0, Q2, SH_LSL, 0);
            cmp_reg(E, Q1, Q2);
        }
        fill = b_fwd(E, C_NE);
    }
    if (check) {
        add_const(Q1, Q5, check);
        cmp_reg(E, Q1, Q6);
    } else {
        cmp_reg(E, Q5, Q6);
    }
    add_stub(b_fwd(E, C_CS), S_BUDGET, a);
    uint16_t *back = E->p;
    add_const(Q5, Q5, add);
    if (fill) {
        add_stub(fill, S_FILL, a);
        if (!E->full) {
            t.stubs[t.nstubs - 1].back = back;
            t.stubs[t.nstubs - 1].lines = lines;
            t.stubs[t.nstubs - 1].check = check;
        }
    }
}

static void emit_stub(struct stub *st)
{
    b_patch(st->from, E->p);
    switch (st->kind) {
    case S_BUDGET: {
        /* leave at the group start: the interpreter runs the rest of the budget */
        const struct insn *x = &t.in[st->at];
        t.budget[st->at] = E->p;
        mov32(E, Q0, x->pc + 1);
        mov_imm(E, Q1, mem(x->pc));
        leave(X_BUDGET, st_of(x));
        break;
    }
    case S_FILL:
        mov32(E, Q0, st->lines);
        add_const(Q1, Q5, st->check);
        mov_reg(E, Q2, Q6);
        mov32(E, Q12, (uint32_t)(uintptr_t)fxjit_fill);
        blx(E, Q12);
        cmp_imm(E, Q0, 0);
        b_to(E, C_EQ, t.budget[st->at]);
        dp_imm(E, OP_SUB, 0, Q0, Q0, 1);
        dp_reg(E, OP_ADD, 0, Q5, Q5, Q0, SH_LSL, 0);
        b_to(E, C_AL, st->back);
        break;
    case S_TAKEN: {
        const struct insn *x = &t.in[st->at], *y = &t.in[st->at + 1];
        if (x->kind == K_LOOP && !x->known) {
            /* R13 (in r7): back to the block start, or leave before the delay slot */
            if (x->tst == t.st0) {
                mov32(E, Q0, t.start);
                cmp_reg(E, Q7, Q0);
                uint16_t *dyn = b_fwd(E, C_NE);
                delay_path(y, t.start, x->tst);
                b_patch(dyn, E->p);
            }
            emit_dyn(y, x->tst);
        } else {
            delay_path(y, x->imm, x->tst);
        }
        break;
    }
    case S_ODD:
        movw(E, Q2, X_ODD);
        b_to(E, C_AL, s_leave_plain);
        break;
    }
}

static uint16_t *translate(void)
{
    uint16_t *start = E->p;
    t.nstubs = 0;
    t.nfixes = 0;
    memset(t.label, 0, sizeof(t.label));
    for (int a = 0; a < t.n;) {
        int z = a;
        while (z + 1 < t.n && !t.in[z + 1].group)
            z++;
        emit_group(a, z);
        for (int i = a; i <= z; i++) {
            const struct insn *x = &t.in[i];
            if (x->delay || is_prefix(x))
                continue;
            int c = classify(x);
            if (x->traced)
                continue; /* (its delay slot is an ordinary instruction) */
            if (c >= 2)
                emit_jump(i);
            else if (x->kind == K_RPIX)
                emit_rpix(x);
            else
                emit_op(x);
        }
        a = z + 1;
    }
    if (t.open)
        goto_target(t.end_pc, t.end_st, X_END);
    /* (the budget exits first: the fill paths jump to them) */
    for (int i = 0; i < t.nstubs; i++)
        if (t.stubs[i].kind == S_BUDGET)
            emit_stub(&t.stubs[i]);
    for (int i = 0; i < t.nstubs; i++)
        if (t.stubs[i].kind != S_BUDGET)
            emit_stub(&t.stubs[i]);
    for (int i = 0; i < t.nfixes; i++)
        b_patch(t.fixes[i].at, t.label[t.fixes[i].group]);
    return E->full ? NULL : start;
}

/* Translates the block at @start with prefixes @st (the key: @start + 1, @ctx, @st); NULL if
 * there is none */
static uint16_t *compile(uint32_t start, uint32_t ctx, uint32_t st, int resume)
{
    if (s_e.end - s_e.p < 8192 || s_hash_used > HASH_SIZE * 3 / 4 || s_nlinks > MAX_LINKS - MAX_STUBS)
        flush_all();
    t.bank = GSU.pvPrgBank;
    t.start = start;
    t.st0 = st;
    t.ctx = ctx;
    t.resume = resume;
    t.cbr = GSU.vCacheBaseReg;
    t.active = GSU.bCacheActive != 0;
    t.ccache = GSU.vCostCache;
    t.cmem = GSU.vCostMem;
    t.cmult = GSU.vCostMult;
    t.cfmult = GSU.vCostFmult;
    switch (GSU.vMode & 3) {
    case 0:
        t.cplot = ((t.cmem << 1) >> 3) + 1;
        t.crpix = t.cmem << 1;
        break;
    case 3:
        t.cplot = t.cmem + 1;
        t.crpix = t.cmem << 3;
        break;
    default:
        t.cplot = ((t.cmem << 2) >> 3) + 1;
        t.crpix = t.cmem << 2;
        break;
    }
    decode();
    /* (prefixes alone: nothing to run) */
    int ops = 0;
    for (int i = 0; i < t.n; i++)
        ops += !is_prefix(&t.in[i]);
    if (!ops) {
        insert(start + 1, ctx, st, NOJIT);
        return NULL;
    }
    mark_groups();
    uint32_t links0 = s_nlinks;
    uint16_t *p0 = s_e.p;
    uint16_t *code = translate();
    if (!code) {
        s_nlinks = links0;
        if (s_e.end - p0 > 16384) {
            /* not for want of room: a translator bug; this address is interpreted */
            s_e.p = p0;
            s_e.full = 0;
            s_stats.bugs++;
            insert(start + 1, ctx, st, NOJIT);
        } else {
            flush_all();
        }
        return NULL;
    }
    crtos_cache_sync(code, (size_t)(s_e.p - code) * 2u);
    insert(start + 1, ctx, st, code);
    /* its groups are entries too: where resume blocks go on */
    for (int i = 1; i < t.n; i++) {
        const struct insn *x = &t.in[i];
        if (x->group && t.label[i] && !lookup(x->pc + 1, ctx, st_of(x)))
            insert(x->pc + 1, ctx, st_of(x), t.label[i]);
    }
    s_stats.compiled++;
    return code;
}

/* ---- running ----------------------------------------------------------------------------------- */

static void link_exit(uint32_t link)
{
    uint32_t r15 = GSU.avReg[15], ctx = context(), st = prefixes();
    struct fxblock *b = lookup(r15, ctx, st);
    uint32_t flushes = s_stats.flushes;
    uint16_t *target = b ? b->code : compile(r15 - 1, ctx, st, 0);
    /* (a compile that started over has thrown away the exit to patch) */
    if (target && target != NOJIT && flushes == s_stats.flushes && s_links[link]) {
        b_encode(s_links[link], target);
        crtos_cache_sync(s_links[link], 4);
        s_links[link] = NULL;
        s_stats.linked++;
    }
}

int S9xFxJitRun(uint32_t budget)
{
    if (!s_enabled)
        return 0;
    uint32_t r15 = GSU.avReg[15];
    if ((GSU.vPrgBankReg & 0x7c) == 0x70 || GSU.pvPrgBank[(r15 - 1) & 0xffff] != GSU.vPipe) {
        s_stats.interp++;
        return 0;
    }
    uint32_t ctx = context(), st = prefixes();
    struct fxblock *b = lookup(r15, ctx, st);
    uint16_t *code;
    if (b) {
        code = b->code;
    } else if (s_arm || ++s_miss >= ARM_AFTER) {
        code = compile(r15 - 1, ctx, st, s_arm != 1);
        if (!code)
            code = NOJIT;
    } else {
        code = NOJIT;
    }
    if (s_pend_slot) {
        /* the jump that left before its delay slot comes here: its target cache */
        struct dslot *sl = &s_slots[s_pend_slot];
        if (code != NOJIT && s_pend_flushes == s_stats.flushes && r15 == s_pend_target + 1 && ctx == sl->ctx &&
            st == sl->st) {
            uint32_t o = (s_pend_target - GSU.vCacheBaseReg) & 0xffff;
            int cached = GSU.bCacheActive && o < 512;
            sl->target = s_pend_target;
            sl->bit = cached ? 1u << (o >> 4) : 0;
            sl->cost = cached ? GSU.vCostCache : GSU.vCostMem;
            sl->code = (uint16_t *)((uintptr_t)code | 1u);
            s_stats.slots++;
        }
        s_pend_slot = 0;
    }
    if (code == NOJIT) {
        s_stats.interp++;
        return 0;
    }
    s_arm = 0;
    s_miss = 0;
    s_stats.runs++;
    int verify = verify_ready();
    if (verify) {
        s_v0 = GSU;
        if (s_verify == 2)
            memcpy(s_vram0, GSU.pvRam, s_vram_size);
    }
    s_enter((const uint8_t *)code + 1, budget);
    uint32_t x = s_exit, link = x >> 4;
    if (verify)
        verify_run(r15, ctx, st, x);
    switch (x & 15) {
    case X_BUDGET:
        /* the interpreter finishes the scan line's budget; where it stops, a resume block */
        s_stats.x_budget++;
        while ((GSU.vStatusReg & FLG_G) && GSU.vCycles < budget)
            FX_STEP;
        s_arm = 2;
        break;
    case X_STATIC:
    case X_END:
        if ((x & 15) == X_STATIC)
            s_stats.x_static++;
        else
            s_stats.x_end++;
        s_arm = 1;
        if (link && s_links[link])
            link_exit(link);
        break;
    case X_DYN:
        s_stats.x_dyn++;
        s_arm = 1;
        if (link) {
            s_pend_slot = link;
            s_pend_target = GSU.avReg[15];
            s_pend_flushes = s_stats.flushes;
        }
        break;
    default:
        s_stats.x_odd++;
        s_arm = 1;
        break;
    }
    return 1;
}
