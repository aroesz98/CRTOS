/*
 * kernel/os/elf.cpp - relocatable ELF loading shared by modules and programs (see elf.h).
 */
#include "elf.h"
#include <string.h>

#define ET_REL          1
#define EM_ARM          40
#define SHN_UNDEF       0
#define SHN_ABS         0xFFF1
#define SHN_COMMON      0xFFF2
#define STB_WEAK        2
#define STT_FUNC        2
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i)&0xF)
#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((i)&0xFF)

#define R_ARM_NONE              0
#define R_ARM_ABS32             2
#define R_ARM_REL32             3
#define R_ARM_THM_CALL          10
#define R_ARM_THM_JUMP24        30
#define R_ARM_TARGET1           38
#define R_ARM_V4BX              40
#define R_ARM_PREL31            42
#define R_ARM_THM_MOVW_ABS_NC   47
#define R_ARM_THM_MOVT_ABS      48
#define R_ARM_THM_JUMP19        51
#define R_ARM_THM_JUMP11        102

/* a veneer of the split image: a branch to symbol @sym + @addend from block @pool that cannot
 * reach it goes to @addr (0: not made yet), which jumps on */
struct elf_veneer {
    uint32_t sym;
    int32_t addend;
    uint8_t pool, used;
    uint32_t addr;
};

/* ---- Thumb-2 branch/immediate encodings --------------------------------------------------------- */

static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

static int32_t bl_decode(uint16_t hi, uint16_t lo)
{
    uint32_t s = (hi >> 10) & 1u, j1 = (lo >> 13) & 1u, j2 = (lo >> 11) & 1u;
    uint32_t off = (s << 24) | ((~(j1 ^ s) & 1u) << 23) | ((~(j2 ^ s) & 1u) << 22) | ((uint32_t)(hi & 0x3FFu) << 12) |
                   ((uint32_t)(lo & 0x7FFu) << 1);
    return (int32_t)(off << 7) >> 7; /* sign-extend 25 bits */
}

static void bl_encode(uint16_t *hi, uint16_t *lo, int32_t off)
{
    uint32_t s = ((uint32_t)off >> 24) & 1u;
    uint32_t j1 = s ^ (~((uint32_t)off >> 23) & 1u);
    uint32_t j2 = s ^ (~((uint32_t)off >> 22) & 1u);
    *hi = (uint16_t)((*hi & 0xF800u) | (s << 10) | (((uint32_t)off >> 12) & 0x3FFu));
    *lo = (uint16_t)((*lo & 0xD000u) | (j1 << 13) | (j2 << 11) | (((uint32_t)off >> 1) & 0x7FFu));
}

static int32_t bcond_decode(uint16_t hi, uint16_t lo)
{
    uint32_t s = (hi >> 10) & 1u, j1 = (lo >> 13) & 1u, j2 = (lo >> 11) & 1u;
    uint32_t off = (s << 20) | (j2 << 19) | (j1 << 18) | ((uint32_t)(hi & 0x3Fu) << 12) | ((uint32_t)(lo & 0x7FFu) << 1);
    return (int32_t)(off << 11) >> 11; /* sign-extend 21 bits */
}

static void bcond_encode(uint16_t *hi, uint16_t *lo, int32_t off)
{
    uint32_t u = (uint32_t)off;
    *hi = (uint16_t)((*hi & 0xFBC0u) | (((u >> 20) & 1u) << 10) | ((u >> 12) & 0x3Fu));
    *lo = (uint16_t)((*lo & 0xD000u) | (((u >> 18) & 1u) << 13) | (((u >> 19) & 1u) << 11) | ((u >> 1) & 0x7FFu));
}

static uint32_t ven_hash(uint32_t sym, int32_t addend, int pool)
{
    return (sym * 2654435761u) ^ ((uint32_t)addend * 40503u) ^ (uint32_t)pool;
}

/* The entry of (@sym, @addend, @pool) in the veneer table; with @add, a new one if missing */
static struct elf_veneer *ven_find(struct elf_ctx *c, uint32_t sym, int32_t addend, int pool, bool add)
{
    if (!c->ven)
        return nullptr;
    for (uint32_t h = ven_hash(sym, addend, pool) & (c->ven_cap - 1u);; h = (h + 1u) & (c->ven_cap - 1u)) {
        struct elf_veneer *e = &c->ven[h];
        if (!e->used) {
            if (!add)
                return nullptr;
            e->used = 1;
            e->sym = sym;
            e->addend = addend;
            e->pool = (uint8_t)pool;
            e->addr = 0;
            return e;
        }
        if (e->sym == sym && e->addend == addend && e->pool == pool)
            return e;
    }
}

/* Address of a veneer in block @pool's pool that jumps to @target (a Thumb address) for the
 * branch to symbol @sym + @addend, made at the first use; 0 if there is none */
static uint32_t veneer(struct elf_ctx *c, uint32_t sym, int32_t addend, int pool, uint32_t target)
{
    struct elf_veneer *e = ven_find(c, sym, addend, pool, false);
    if (!e)
        return 0;
    if (!e->addr) {
        uint8_t *v = (uint8_t *)(uintptr_t)(c->pool[pool] + c->pool_used[pool]);
        c->pool_used[pool] += 8u;
        wr16(v, 0xF8DFu); /* ldr.w pc, [pc, #0]: the word after it */
        wr16(v + 2, 0xF000u);
        wr32(v + 4, target | 1u);
        e->addr = (uint32_t)(uintptr_t)v;
    }
    return e->addr;
}

static int apply_reloc(struct elf_ctx *c, uint8_t *P, uint32_t type, uint32_t S, bool rela, int32_t addend,
                       bool arm_func, bool undef_weak, const char *sym, uint32_t symi, int pool)
{
    uint32_t p = (uint32_t)(uintptr_t)P;
    if (undef_weak && (type == R_ARM_THM_CALL || type == R_ARM_THM_JUMP24 || type == R_ARM_THM_JUMP19)) {
        /* branch to a weak function that is not there: the code checks for NULL first; a
         * linker makes the instruction a no-op (NOP.W) */
        wr16(P, 0xF3AFu);
        wr16(P + 2, 0x8000u);
        return 0;
    }
    switch (type) {
    case R_ARM_NONE:
    case R_ARM_V4BX:
        return 0;
    case R_ARM_ABS32:
    case R_ARM_TARGET1: {
        uint32_t a = rela ? (uint32_t)addend : rd32(P);
        wr32(P, S + a);
        return 0;
    }
    case R_ARM_REL32: {
        uint32_t a = rela ? (uint32_t)addend : rd32(P);
        wr32(P, S + a - p);
        return 0;
    }
    case R_ARM_PREL31: {
        uint32_t v = rd32(P);
        int32_t a = rela ? addend : ((int32_t)(v << 1) >> 1);
        int32_t off = (int32_t)(S + (uint32_t)a - p);
        if (off >= 0x40000000 || off < -0x40000000) {
            /* an unwind table entry of the other block in a split image: programs never
             * unwind (no exceptions), the entry just points at itself */
            if (!c->fast)
                return -ERANGE;
            off = 0;
        }
        wr32(P, (v & 0x80000000u) | ((uint32_t)off & 0x7FFFFFFFu));
        return 0;
    }
    case R_ARM_THM_CALL:
    case R_ARM_THM_JUMP24: {
        if (arm_func) {
            printk("E: %s: '%s' is not a Thumb function\n", c->who, sym);
            return -ENOEXEC;
        }
        uint16_t hi = rd16(P), lo = rd16(P + 2);
        int32_t a = rela ? addend : bl_decode(hi, lo);
        int32_t off = (int32_t)(S + (uint32_t)a - p);
        if (off <= (int32_t)0xFF000000 || off >= 0x01000000) {
            uint32_t v = veneer(c, symi, a, pool, S + (uint32_t)a + 4u);
            off = (int32_t)(v - (p + 4u));
            if (!v || off <= (int32_t)0xFF000000 || off >= 0x01000000) {
                printk("E: %s: call to '%s' out of range (build with -mlong-calls)\n", c->who, sym);
                return -ERANGE;
            }
        }
        bl_encode(&hi, &lo, off);
        wr16(P, hi);
        wr16(P + 2, lo);
        return 0;
    }
    case R_ARM_THM_JUMP19: {
        uint16_t hi = rd16(P), lo = rd16(P + 2);
        int32_t a = rela ? addend : bcond_decode(hi, lo);
        int32_t off = (int32_t)(S + (uint32_t)a - p);
        if (off < -0x100000 || off >= 0x100000) {
            uint32_t v = veneer(c, symi, a, pool, S + (uint32_t)a + 4u);
            off = (int32_t)(v - (p + 4u));
            if (!v || off < -0x100000 || off >= 0x100000)
                return -ERANGE;
        }
        bcond_encode(&hi, &lo, off);
        wr16(P, hi);
        wr16(P + 2, lo);
        return 0;
    }
    case R_ARM_THM_JUMP11: {
        uint16_t ins = rd16(P);
        int32_t a = rela ? addend : ((int32_t)((uint32_t)(ins & 0x7FFu) << 21) >> 20);
        int32_t off = (int32_t)(S + (uint32_t)a - p);
        if (off < -2048 || off >= 2048)
            return -ERANGE;
        wr16(P, (uint16_t)((ins & 0xF800u) | (((uint32_t)off >> 1) & 0x7FFu)));
        return 0;
    }
    case R_ARM_THM_MOVW_ABS_NC:
    case R_ARM_THM_MOVT_ABS: {
        uint16_t hi = rd16(P), lo = rd16(P + 2);
        int32_t a;
        if (rela) {
            a = addend;
        } else {
            uint32_t imm = ((hi & 0x000Fu) << 12) | ((hi & 0x0400u) << 1) | ((lo & 0x7000u) >> 4) | (lo & 0x00FFu);
            a = (int32_t)((imm ^ 0x8000u) - 0x8000u);
        }
        uint32_t v = S + (uint32_t)a;
        if (type == R_ARM_THM_MOVT_ABS)
            v >>= 16;
        hi = (uint16_t)((hi & 0xFBF0u) | ((v & 0xF000u) >> 12) | ((v & 0x0800u) >> 1));
        lo = (uint16_t)((lo & 0x8F00u) | ((v & 0x0700u) << 4) | (v & 0x00FFu));
        wr16(P, hi);
        wr16(P + 2, lo);
        return 0;
    }
    default:
        printk("E: %s: unsupported relocation type %lu (symbol '%s')\n", c->who, (unsigned long)type, sym);
        return -ENOEXEC;
    }
}

/* ---- loading ------------------------------------------------------------------------------------ */

static bool in_file(const struct elf_ctx *c, uint32_t off, uint32_t len)
{
    return off <= c->size && len <= c->size - off;
}

int elf_check(struct elf_ctx *c)
{
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)c->file;
    if (c->size < sizeof(*eh) || memcmp(eh->e_ident, "\177ELF", 4) || eh->e_ident[4] != 1 /* 32-bit */ ||
        eh->e_ident[5] != 1 /* LE */)
        return -ENOEXEC;
    if (eh->e_type != ET_REL || eh->e_machine != EM_ARM || eh->e_shentsize != sizeof(Elf32_Shdr) ||
        !in_file(c, eh->e_shoff, (uint32_t)eh->e_shnum * sizeof(Elf32_Shdr)) || eh->e_shstrndx >= eh->e_shnum)
        return -ENOEXEC;
    c->eh = eh;
    c->sh = (const Elf32_Shdr *)(c->file + eh->e_shoff);
    const Elf32_Shdr *ss = &c->sh[eh->e_shstrndx];
    if (!in_file(c, ss->sh_offset, ss->sh_size) || !ss->sh_size || c->file[ss->sh_offset + ss->sh_size - 1])
        return -ENOEXEC;
    c->shstr = (const char *)c->file + ss->sh_offset;
    for (uint32_t i = 0; i < eh->e_shnum; i++) {
        const Elf32_Shdr *s = &c->sh[i];
        if (s->sh_type != SHT_NOBITS && s->sh_type != 0 && !in_file(c, s->sh_offset, s->sh_size))
            return -ENOEXEC;
        if (s->sh_name >= ss->sh_size)
            return -ENOEXEC;
    }
    c->secaddr = (uint32_t *)kzalloc(eh->e_shnum * sizeof(uint32_t), KM_LARGE);
    return c->secaddr ? 0 : -ENOMEM;
}

int elf_layout_sections(const Elf32_Shdr *sh, uint32_t n, uint32_t *secaddr, uint32_t *size, uint32_t *maxalign)
{
    uint32_t off = 0, align_max = 8;
    for (int pass = 0; pass < 3; pass++) {
        for (uint32_t i = 0; i < n; i++) {
            const Elf32_Shdr *s = &sh[i];
            if (!(s->sh_flags & SHF_ALLOC))
                continue;
            bool exec = s->sh_flags & SHF_EXECINSTR, bss = s->sh_type == SHT_NOBITS;
            if ((pass == 0 && !exec) || (pass == 1 && (exec || bss)) || (pass == 2 && !bss))
                continue;
            uint32_t align = s->sh_addralign ? s->sh_addralign : 1;
            if (align & (align - 1))
                return -ENOEXEC;
            if (align > align_max)
                align_max = align;
            off = ALIGN_UP(off, align);
            if (secaddr)
                secaddr[i] = off;
            if (s->sh_size > 0x7FFFFFFFu - off)
                return -ENOEXEC;
            off += s->sh_size;
        }
    }
    if (!off)
        return -ENOEXEC;
    *size = off;
    *maxalign = align_max;
    return 0;
}

int elf_layout(struct elf_ctx *c, uint32_t *size, uint32_t *maxalign)
{
    return elf_layout_sections(c->sh, c->eh->e_shnum, c->secaddr, size, maxalign);
}

void elf_place(struct elf_ctx *c, uint8_t *base)
{
    for (uint32_t i = 0; i < c->eh->e_shnum; i++) {
        const Elf32_Shdr *s = &c->sh[i];
        if (!(s->sh_flags & SHF_ALLOC))
            continue;
        uint8_t *dst = base + c->secaddr[i];
        if (s->sh_type == SHT_NOBITS)
            memset(dst, 0, s->sh_size);
        else
            memcpy(dst, c->file + s->sh_offset, s->sh_size);
        c->secaddr[i] = (uint32_t)(uintptr_t)dst;
    }
}

/* ---- the split image ------------------------------------------------------------------------- */

/* Lays out the sections of one block (@want: 0 main, 1 fast) like elf_layout_sections(), with a
 * pool of @pool_bytes for veneers after its code */
static int layout_block(const struct elf_ctx *c, int want, uint32_t pool_bytes, uint32_t *pool, uint32_t *size,
                        uint32_t *maxalign)
{
    uint32_t off = 0, align_max = 8;
    for (int pass = 0; pass < 3; pass++) {
        for (uint32_t i = 0; i < c->eh->e_shnum; i++) {
            const Elf32_Shdr *s = &c->sh[i];
            if (!(s->sh_flags & SHF_ALLOC) || c->fast[i] != want)
                continue;
            bool exec = s->sh_flags & SHF_EXECINSTR, bss = s->sh_type == SHT_NOBITS;
            if ((pass == 0 && !exec) || (pass == 1 && (exec || bss)) || (pass == 2 && !bss))
                continue;
            uint32_t align = s->sh_addralign ? s->sh_addralign : 1;
            if (align & (align - 1))
                return -ENOEXEC;
            if (align > align_max)
                align_max = align;
            off = ALIGN_UP(off, align);
            c->secaddr[i] = off;
            if (s->sh_size > 0x7FFFFFFFu - off)
                return -ENOEXEC;
            off += s->sh_size;
        }
        if (pass == 0) {
            off = ALIGN_UP(off, 4u);
            *pool = off;
            off += pool_bytes;
        }
    }
    *size = off;
    *maxalign = align_max;
    return 0;
}

/* Each branch between the blocks (to a symbol and addend) needs a veneer in the pool of the
 * block it starts in: counted into the veneer table (@add) or just counted (an upper bound) */
static uint32_t count_crossings(struct elf_ctx *c, const Elf32_Sym *syms, uint32_t nsyms, bool add, uint32_t count[2])
{
    uint32_t n = c->eh->e_shnum, total = 0;
    for (uint32_t i = 0; i < n; i++) {
        const Elf32_Shdr *rs = &c->sh[i];
        if ((rs->sh_type != SHT_REL && rs->sh_type != SHT_RELA) || rs->sh_info >= n ||
            !(c->sh[rs->sh_info].sh_flags & SHF_ALLOC))
            continue;
        bool rela = rs->sh_type == SHT_RELA;
        uint32_t entsize = rela ? 12u : 8u;
        const Elf32_Shdr *t = &c->sh[rs->sh_info];
        int from = c->fast[rs->sh_info];
        for (uint32_t k = 0; k < rs->sh_size / entsize; k++) {
            const uint8_t *e = c->file + rs->sh_offset + k * entsize;
            uint32_t r_offset = rd32(e), r_info = rd32(e + 4), symi = ELF32_R_SYM(r_info), type = ELF32_R_TYPE(r_info);
            if ((type != R_ARM_THM_CALL && type != R_ARM_THM_JUMP24 && type != R_ARM_THM_JUMP19) || symi >= nsyms ||
                r_offset + 4 > t->sh_size || t->sh_type == SHT_NOBITS)
                continue;
            uint16_t shndx = syms[symi].st_shndx;
            int to = shndx != SHN_UNDEF && shndx < n ? c->fast[shndx] : 0;
            if (to == from)
                continue;
            total++;
            if (!add)
                continue;
            const uint8_t *ins = c->file + t->sh_offset + r_offset;
            int32_t a = rela ? (int32_t)rd32(e + 8)
                             : type == R_ARM_THM_JUMP19 ? bcond_decode(rd16(ins), rd16(ins + 2)) : bl_decode(rd16(ins), rd16(ins + 2));
            struct elf_veneer *v = ven_find(c, symi, a, from, false);
            if (!v) {
                ven_find(c, symi, a, from, true);
                count[from]++;
            }
        }
    }
    return total;
}

int elf_split_fast(struct elf_ctx *c, uint32_t *size, uint32_t *align, uint32_t *fsize, uint32_t *falign)
{
    uint32_t n = c->eh->e_shnum, nfast = 0;
    c->fast = (uint8_t *)kzalloc(n, KM_LARGE);
    if (!c->fast)
        return -ENOMEM;
    for (uint32_t i = 0; i < n; i++)
        if ((c->sh[i].sh_flags & SHF_ALLOC) && !strncmp(c->shstr + c->sh[i].sh_name, ".fast", 5)) {
            c->fast[i] = 1;
            nfast++;
        }
    const Elf32_Shdr *symsec = nullptr;
    for (uint32_t i = 0; i < n; i++)
        if (c->sh[i].sh_type == SHT_SYMTAB)
            symsec = &c->sh[i];
    int r = !nfast ? -ENOENT : !symsec ? -ENOEXEC : 0;
    if (!r) {
        const Elf32_Sym *syms = (const Elf32_Sym *)(c->file + symsec->sh_offset);
        uint32_t nsyms = symsec->sh_size / sizeof(Elf32_Sym), count[2] = { 0, 0 };
        uint32_t cap = 16;
        while (cap < 2u * count_crossings(c, syms, nsyms, false, count))
            cap <<= 1;
        c->ven = (struct elf_veneer *)kzalloc(cap * sizeof(struct elf_veneer), KM_LARGE);
        c->ven_cap = cap;
        if (!c->ven)
            r = -ENOMEM;
        if (!r) {
            count_crossings(c, syms, nsyms, true, count);
            c->pool_used[0] = c->pool_used[1] = 0;
            r = layout_block(c, 0, count[0] * 8u, &c->pool[0], size, align);
            if (!r)
                r = layout_block(c, 1, count[1] * 8u, &c->pool[1], fsize, falign);
        }
    }
    if (r)
        elf_unsplit(c);
    return r;
}

void elf_unsplit(struct elf_ctx *c)
{
    kfree(c->fast);
    kfree(c->ven);
    c->fast = nullptr;
    c->ven = nullptr;
    c->ven_cap = 0;
    c->pool[0] = c->pool[1] = 0;
}

void elf_place_split(struct elf_ctx *c, uint8_t *base, uint8_t *fbase)
{
    for (uint32_t i = 0; i < c->eh->e_shnum; i++) {
        const Elf32_Shdr *s = &c->sh[i];
        if (!(s->sh_flags & SHF_ALLOC))
            continue;
        uint8_t *dst = (c->fast[i] ? fbase : base) + c->secaddr[i];
        if (s->sh_type == SHT_NOBITS)
            memset(dst, 0, s->sh_size);
        else
            memcpy(dst, c->file + s->sh_offset, s->sh_size);
        c->secaddr[i] = (uint32_t)(uintptr_t)dst;
    }
    c->pool[0] += (uint32_t)(uintptr_t)base;
    c->pool[1] += (uint32_t)(uintptr_t)fbase;
}

int elf_symbols(struct elf_ctx *c, elf_resolve_fn resolve, void *ctx)
{
    const Elf32_Shdr *symsec = nullptr;
    for (uint32_t i = 0; i < c->eh->e_shnum; i++)
        if (c->sh[i].sh_type == SHT_SYMTAB)
            symsec = &c->sh[i];
    if (!symsec || symsec->sh_link >= c->eh->e_shnum)
        return -ENOEXEC;
    const Elf32_Shdr *strsec = &c->sh[symsec->sh_link];
    c->syms = (const Elf32_Sym *)(c->file + symsec->sh_offset);
    c->nsyms = symsec->sh_size / sizeof(Elf32_Sym);
    c->strtab = (const char *)c->file + strsec->sh_offset;
    c->strsize = strsec->sh_size;
    if (!c->strsize || c->strtab[c->strsize - 1])
        return -ENOEXEC;
    c->symaddr = (uint32_t *)kzalloc(c->nsyms * sizeof(uint32_t), KM_LARGE);
    if (!c->symaddr)
        return -ENOMEM;
    int err = 0;
    for (uint32_t i = 1; i < c->nsyms; i++) {
        const Elf32_Sym *s = &c->syms[i];
        const char *name = s->st_name < c->strsize ? c->strtab + s->st_name : "?";
        if (s->st_shndx == SHN_UNDEF) {
            if (!name[0])
                continue;
            uintptr_t a = resolve ? resolve(name, ctx) : 0;
            if (!a) {
                if (ELF32_ST_BIND(s->st_info) == STB_WEAK)
                    continue; /* weak: stays 0 */
                printk("E: %s: unresolved symbol '%s'\n", c->who, name);
                err = -ENOENT;
                continue;
            }
            c->symaddr[i] = (uint32_t)a;
        } else if (s->st_shndx == SHN_ABS) {
            c->symaddr[i] = s->st_value;
        } else if (s->st_shndx == SHN_COMMON) {
            printk("E: %s: common symbol '%s' (build with -fno-common)\n", c->who, name);
            err = -ENOEXEC;
        } else if (s->st_shndx < c->eh->e_shnum) {
            uint32_t base = c->secaddr[s->st_shndx];
            c->symaddr[i] = base ? base + s->st_value : 0;
        }
    }
    return err;
}

int elf_relocate(struct elf_ctx *c)
{
    for (uint32_t i = 0; i < c->eh->e_shnum; i++) {
        const Elf32_Shdr *rs = &c->sh[i];
        if (rs->sh_type != SHT_REL && rs->sh_type != SHT_RELA)
            continue;
        if (rs->sh_info >= c->eh->e_shnum || !c->secaddr[rs->sh_info] || !(c->sh[rs->sh_info].sh_flags & SHF_ALLOC))
            continue; /* relocations of a section that is not loaded (debug info) */
        bool rela = rs->sh_type == SHT_RELA;
        uint32_t entsize = rela ? 12u : 8u;
        uint32_t count = rs->sh_size / entsize;
        const Elf32_Shdr *target = &c->sh[rs->sh_info];
        uint8_t *tbase = (uint8_t *)(uintptr_t)c->secaddr[rs->sh_info];
        for (uint32_t k = 0; k < count; k++) {
            const uint8_t *e = c->file + rs->sh_offset + k * entsize;
            uint32_t r_offset = rd32(e), r_info = rd32(e + 4);
            int32_t addend = rela ? (int32_t)rd32(e + 8) : 0;
            uint32_t symi = ELF32_R_SYM(r_info), type = ELF32_R_TYPE(r_info);
            if (symi >= c->nsyms || r_offset + 4 > target->sh_size + (type == R_ARM_THM_JUMP11 ? 2u : 0u))
                return -ENOEXEC;
            const Elf32_Sym *s = &c->syms[symi];
            bool arm_func = ELF32_ST_TYPE(s->st_info) == STT_FUNC && s->st_shndx != SHN_UNDEF && !(s->st_value & 1u);
            bool undef_weak = s->st_shndx == SHN_UNDEF && ELF32_ST_BIND(s->st_info) == STB_WEAK && !c->symaddr[symi];
            const char *name = s->st_name < c->strsize ? c->strtab + s->st_name : "?";
            int r = apply_reloc(c, tbase + r_offset, type, c->symaddr[symi], rela, addend, arm_func, undef_weak, name,
                                symi, c->fast ? c->fast[rs->sh_info] : 0);
            if (r)
                return r;
        }
    }
    return 0;
}

const Elf32_Shdr *elf_section(const struct elf_ctx *c, const char *name, uint32_t *index)
{
    for (uint32_t i = 0; i < c->eh->e_shnum; i++) {
        if (!strcmp(c->shstr + c->sh[i].sh_name, name)) {
            if (index)
                *index = i;
            return &c->sh[i];
        }
    }
    return nullptr;
}

uint32_t elf_symbol(const struct elf_ctx *c, const char *name)
{
    for (uint32_t i = 1; i < c->nsyms; i++) {
        const Elf32_Sym *s = &c->syms[i];
        if (s->st_shndx == SHN_UNDEF || s->st_name >= c->strsize)
            continue;
        if (!strcmp(c->strtab + s->st_name, name))
            return c->symaddr[i];
    }
    return 0;
}

void elf_forget_symbols(struct elf_ctx *c)
{
    kfree(c->symaddr);
    c->symaddr = nullptr;
}

void elf_release(struct elf_ctx *c)
{
    elf_unsplit(c);
    kfree(c->symaddr);
    kfree(c->secaddr);
    c->symaddr = nullptr;
    c->secaddr = nullptr;
}
