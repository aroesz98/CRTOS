/*
 * crtos-app - looks at CRTOS programs (.app files): on the PC (part of the toolchain, next to
 * arm-crtos-gcc) and on the board (/sd/crtos/bin/crtos-app.app, for the native compiler).
 *
 *     crtos-app info  PROGRAM...                     the header (stack, heap), memory, sections
 *     crtos-app set   PROGRAM [--stack N] [--heap N] change the stack or heap in the header
 *     crtos-app check [-q] PROGRAM...                what the kernel's loader would refuse
 *
 * N is bytes, or with a K or M suffix. A program is one of two kinds of ELF file for Arm
 * (kernel/os/app.cpp), and the check repeats the loader's rules for it:
 *
 *   relocatable (ld -r, the usual one): the kernel places its sections in the process arena,
 *     resolves its relocations and starts _start. Nothing undefined but the two symbols the
 *     loader supplies, no COMMON symbols, only the relocation types it applies, a Thumb
 *     _start, alignments of at most 4 KB, at most 16 MB.
 *   runs in place (ET_DYN, arm-crtos-gcc -mxip, crtos-xip.ld): a text segment from address 0
 *     and a data segment from 0x10000000, nothing else; a GOT; only R_ARM_RELATIVE dynamic
 *     relocations; a Thumb entry point in the text. The text stays in the flash (/flash0) or
 *     is copied into the arena (other file systems, then at most 16 MB).
 *
 * Both need a valid header with the stack and heap within the kernel's limits.
 *
 * The file is read piece by piece (headers, symbols, one relocation table at a time), so it
 * needs little memory on the board.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ELF (only what is needed; not every C library has elf.h) ----------------------------- */

typedef struct {
    unsigned char ident[16];
    uint16_t type, machine;
    uint32_t version, entry, phoff, shoff, flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} ehdr_t;

typedef struct {
    uint32_t name, type, flags, addr, offset, size, link, info, addralign, entsize;
} shdr_t;

typedef struct {
    uint32_t name, value, size;
    unsigned char info, other;
    uint16_t shndx;
} sym_t;

typedef struct {
    uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align;
} phdr_t;

#define ET_REL          1
#define ET_DYN          3
#define EM_ARM          40
#define PT_LOAD         1
#define PT_INTERP       3
#define PT_TLS          7
#define PF_X            1u
#define PF_W            2u
#define R_ARM_RELATIVE  23
#define SHT_SYMTAB      2
#define SHT_RELA        4
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHF_WRITE       0x1u
#define SHF_ALLOC       0x2u
#define SHF_EXECINSTR   0x4u
#define SHN_UNDEF       0
#define SHN_COMMON      0xfff2
#define STB_WEAK        2
#define STT_FUNC        2

/* the kernel's limits and the program header (kernel/include/crtos/syscall.h, app.cpp) */
#define APP_MAGIC       0x50415243u
#define APP_ABI         1u
#define MAX_FILE        (16u << 20)
#define MAX_STACK       (1u << 20)
#define MAX_HEAP        (24u << 20)
#define MAX_ALIGN       4096u
#define DEF_STACK       16384u
#define DEF_HEAP        65536u
#define GUARDS          512u        /* two 256-byte stack guards */
#define XIP_DATA_BASE   0x10000000u /* where crtos-xip.ld puts the data segment */
#define XIP_MAX_PHDRS   16u

struct app_info {
    uint32_t magic, abi, stack_size, heap_size, flags;
};

static const struct {
    unsigned type;
    const char *name;
} relocs[] = {
    { 0, "R_ARM_NONE" },            { 2, "R_ARM_ABS32" },       { 3, "R_ARM_REL32" },
    { 10, "R_ARM_THM_CALL" },       { 30, "R_ARM_THM_JUMP24" }, { 38, "R_ARM_TARGET1" },
    { 40, "R_ARM_V4BX" },           { 42, "R_ARM_PREL31" },     { 47, "R_ARM_THM_MOVW_ABS_NC" },
    { 48, "R_ARM_THM_MOVT_ABS" },   { 51, "R_ARM_THM_JUMP19" }, { 102, "R_ARM_THM_JUMP11" },
};

/* symbols the loader supplies */
static const char *const loader_symbols[] = { "__exidx_start", "__exidx_end" };

/* ---- reading ------------------------------------------------------------------------------ */

struct elf {
    const char *path;
    FILE *f;
    long size;
    ehdr_t eh;
    phdr_t *ph;                     /* program headers (ET_DYN), or NULL */
    shdr_t *sh;
    char *shstr;
    sym_t *syms;
    uint32_t nsyms;
    char *str;
    uint32_t strsize, shstrsize;
    int symtab;                     /* its section index, or -1 */
    int app;                        /* .crtos_app's section index, or -1 */
};

static uint16_t rd16(const unsigned char *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* @len bytes at @off, or NULL (the message is printed) */
static void *read_at(struct elf *e, uint32_t off, uint32_t len)
{
    if ((uint64_t)off + len > (uint64_t)e->size) {
        fprintf(stderr, "%s: damaged (data beyond the end of the file)\n", e->path);
        return NULL;
    }
    unsigned char *p = malloc(len ? len : 1);
    if (!p) {
        fprintf(stderr, "%s: out of memory (%lu bytes)\n", e->path, (unsigned long)len);
        return NULL;
    }
    if (fseek(e->f, (long)off, SEEK_SET) || fread(p, 1, len, e->f) != len) {
        fprintf(stderr, "%s: read error\n", e->path);
        free(p);
        return NULL;
    }
    return p;
}

static void elf_close(struct elf *e)
{
    if (e->f)
        fclose(e->f);
    free(e->ph);
    free(e->sh);
    free(e->shstr);
    free(e->syms);
    free(e->str);
    memset(e, 0, sizeof(*e));
}

static const char *sec_name(const struct elf *e, int i)
{
    uint32_t n = e->sh[i].name;
    return n < e->shstrsize ? e->shstr + n : "?";
}

static const char *sym_name(const struct elf *e, const sym_t *s)
{
    return s->name < e->strsize ? e->str + s->name : "?";
}

/* 0, or -1 with a message */
static int elf_open(struct elf *e, const char *path, const char *mode)
{
    memset(e, 0, sizeof(*e));
    e->path = path;
    e->symtab = e->app = -1;
    e->f = fopen(path, mode);
    if (!e->f) {
        fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return -1;
    }
    fseek(e->f, 0, SEEK_END);
    e->size = ftell(e->f);
    unsigned char *h = read_at(e, 0, 52);
    if (!h)
        return -1;
    memcpy(e->eh.ident, h, 16);
    e->eh.type = rd16(h + 16);
    e->eh.machine = rd16(h + 18);
    e->eh.entry = rd32(h + 24);
    e->eh.phoff = rd32(h + 28);
    e->eh.shoff = rd32(h + 32);
    e->eh.phentsize = rd16(h + 42);
    e->eh.phnum = rd16(h + 44);
    e->eh.shentsize = rd16(h + 46);
    e->eh.shnum = rd16(h + 48);
    e->eh.shstrndx = rd16(h + 50);
    free(h);
    if (memcmp(e->eh.ident, "\177ELF", 4) || e->eh.ident[4] != 1 || e->eh.ident[5] != 1) {
        fprintf(stderr, "%s: not a 32-bit little-endian ELF file\n", path);
        return -1;
    }
    if (e->eh.machine != EM_ARM) {
        fprintf(stderr, "%s: not for Arm (machine %u)\n", path, e->eh.machine);
        return -1;
    }
    if (e->eh.shentsize != 40 || !e->eh.shnum || e->eh.shstrndx >= e->eh.shnum) {
        fprintf(stderr, "%s: no usable section table\n", path);
        return -1;
    }
    unsigned char *t = read_at(e, e->eh.shoff, (uint32_t)e->eh.shnum * 40u);
    if (!t)
        return -1;
    e->sh = calloc(e->eh.shnum, sizeof(shdr_t));
    if (!e->sh) {
        free(t);
        return -1;
    }
    for (int i = 0; i < e->eh.shnum; i++) {
        const unsigned char *p = t + i * 40;
        shdr_t *s = &e->sh[i];
        s->name = rd32(p);
        s->type = rd32(p + 4);
        s->flags = rd32(p + 8);
        s->addr = rd32(p + 12);
        s->offset = rd32(p + 16);
        s->size = rd32(p + 20);
        s->link = rd32(p + 24);
        s->info = rd32(p + 28);
        s->addralign = rd32(p + 32);
        s->entsize = rd32(p + 36);
    }
    free(t);
    if (e->eh.type == ET_DYN && e->eh.phnum) {
        if (e->eh.phentsize != 32 || e->eh.phnum > XIP_MAX_PHDRS) {
            fprintf(stderr, "%s: unusable program headers\n", path);
            return -1;
        }
        unsigned char *p = read_at(e, e->eh.phoff, (uint32_t)e->eh.phnum * 32u);
        e->ph = p ? calloc(e->eh.phnum, sizeof(phdr_t)) : NULL;
        if (!e->ph) {
            free(p);
            return -1;
        }
        for (int i = 0; i < e->eh.phnum; i++) {
            const unsigned char *q = p + i * 32;
            phdr_t *ph = &e->ph[i];
            ph->type = rd32(q);
            ph->offset = rd32(q + 4);
            ph->vaddr = rd32(q + 8);
            ph->paddr = rd32(q + 12);
            ph->filesz = rd32(q + 16);
            ph->memsz = rd32(q + 20);
            ph->flags = rd32(q + 24);
            ph->align = rd32(q + 28);
        }
        free(p);
    }
    const shdr_t *ss = &e->sh[e->eh.shstrndx];
    e->shstr = read_at(e, ss->offset, ss->size + 1);
    if (!e->shstr)
        return -1;
    e->shstr[ss->size] = 0;
    e->shstrsize = ss->size;
    for (int i = 0; i < e->eh.shnum; i++) {
        if (e->sh[i].type == SHT_SYMTAB && e->symtab < 0)
            e->symtab = i;
        if (!strcmp(sec_name(e, i), ".crtos_app"))
            e->app = i;
    }
    return 0;
}

static int load_symbols(struct elf *e)
{
    if (e->symtab < 0)
        return 0;
    const shdr_t *s = &e->sh[e->symtab];
    if (s->link >= e->eh.shnum || (s->entsize && s->entsize != 16)) {
        fprintf(stderr, "%s: damaged symbol table\n", e->path);
        return -1;
    }
    unsigned char *raw = read_at(e, s->offset, s->size);
    if (!raw)
        return -1;
    e->nsyms = s->size / 16;
    e->syms = calloc(e->nsyms ? e->nsyms : 1, sizeof(sym_t));
    if (!e->syms) {
        free(raw);
        return -1;
    }
    for (uint32_t i = 0; i < e->nsyms; i++) {
        const unsigned char *p = raw + i * 16;
        e->syms[i].name = rd32(p);
        e->syms[i].value = rd32(p + 4);
        e->syms[i].size = rd32(p + 8);
        e->syms[i].info = p[12];
        e->syms[i].other = p[13];
        e->syms[i].shndx = rd16(p + 14);
    }
    free(raw);
    const shdr_t *st = &e->sh[s->link];
    e->str = read_at(e, st->offset, st->size + 1);
    if (!e->str)
        return -1;
    e->str[st->size] = 0;
    e->strsize = st->size;
    return 0;
}

/* the header, or the loader's defaults with *present = 0; -1 if it is damaged */
static int read_info(struct elf *e, struct app_info *ai, int *present)
{
    ai->magic = APP_MAGIC;
    ai->abi = APP_ABI;
    ai->stack_size = DEF_STACK;
    ai->heap_size = DEF_HEAP;
    ai->flags = 0;
    *present = e->app >= 0;
    if (e->app < 0)
        return 0;
    if (e->sh[e->app].size < sizeof(struct app_info)) {
        fprintf(stderr, "%s: .crtos_app is too short\n", e->path);
        return -1;
    }
    unsigned char *p = read_at(e, e->sh[e->app].offset, sizeof(struct app_info));
    if (!p)
        return -1;
    ai->magic = rd32(p);
    ai->abi = rd32(p + 4);
    ai->stack_size = rd32(p + 8);
    ai->heap_size = rd32(p + 12);
    ai->flags = rd32(p + 16);
    free(p);
    return 0;
}

/* ---- the commands ------------------------------------------------------------------------- */

static int is_loader_symbol(const char *n)
{
    for (size_t i = 0; i < sizeof(loader_symbols) / sizeof(loader_symbols[0]); i++)
        if (!strcmp(n, loader_symbols[i]))
            return 1;
    return 0;
}

static const char *reloc_name(unsigned type)
{
    for (size_t i = 0; i < sizeof(relocs) / sizeof(relocs[0]); i++)
        if (relocs[i].type == type)
            return relocs[i].name;
    return NULL;
}

/* how much memory the loader gives the program: the arena (kernel/os/app.cpp, proc.cpp) */
static void layout(const struct elf *e, uint32_t *code, uint32_t *data, uint32_t *bss)
{
    *code = *data = *bss = 0;
    for (int i = 0; i < e->eh.shnum; i++) {
        const shdr_t *s = &e->sh[i];
        if (!(s->flags & SHF_ALLOC))
            continue;
        uint32_t a = s->addralign > 1 ? s->addralign : 1;
        uint32_t *acc = s->type == SHT_NOBITS ? bss : (s->flags & SHF_WRITE) ? data : code;
        *acc = (*acc + a - 1) & ~(a - 1);
        *acc += s->size;
    }
}

/* the two segments of a program that runs in place (crtos-xip.ld, the kernel's xip_peek());
 * NULL, or why the loader refuses it */
static const char *xip_segments(const struct elf *e, const phdr_t **text, const phdr_t **data)
{
    *text = *data = NULL;
    if (!e->ph)
        return "no program headers (link with arm-crtos-gcc -mxip)";
    for (int i = 0; i < e->eh.phnum; i++) {
        const phdr_t *p = &e->ph[i];
        if (p->type == PT_INTERP || p->type == PT_TLS)
            return "a dynamic linker or thread-local storage segment (CRTOS has neither)";
        if (p->type != PT_LOAD)
            continue;
        if (!(p->flags & PF_W) && !*text) {
            if (p->offset || p->vaddr || p->filesz != p->memsz || p->filesz >= XIP_DATA_BASE)
                return "the text segment does not start the file at address 0 (link with crtos-xip.ld)";
            *text = p;
        } else if ((p->flags & PF_W) && !(p->flags & PF_X) && !*data) {
            if (p->vaddr < XIP_DATA_BASE || p->filesz > p->memsz)
                return "the data segment is not at 0x10000000 (link with crtos-xip.ld)";
            *data = p;
        } else {
            return "more segments than the text and the data of crtos-xip.ld";
        }
    }
    if (!*text || !*data)
        return "not both a text and a data segment (link with crtos-xip.ld)";
    return NULL;
}

static uint32_t arena_size(uint32_t image, const struct app_info *ai)
{
    uint64_t need = ((uint64_t)image + 7) / 8 * 8 + ((ai->heap_size + 31u) & ~31u) + ((ai->stack_size + 31u) & ~31u) +
                    GUARDS + 64u;
    uint64_t full = 256;
    while (full < need)
        full <<= 1;
    uint64_t sub = full / 8;
    return (uint32_t)((need + sub - 1) / sub * sub);
}

/* the problems of one file: the first 20 are printed */
struct report {
    const char *path;
    int bad, quiet;
};

static void problem(struct report *r, const char *fmt, ...)
{
    if (r->bad < 20) {
        va_list ap;
        va_start(ap, fmt);
        fprintf(stderr, "%s: ", r->path);
        vfprintf(stderr, fmt, ap);
        fputc('\n', stderr);
        va_end(ap);
    }
    r->bad++;
}

static void check_header(struct elf *e, struct report *r)
{
    struct app_info ai;
    int present;
    if (read_info(e, &ai, &present)) {
        r->bad++;
        return;
    }
    if (!present)
        return;
    if (ai.magic != APP_MAGIC)
        problem(r, ".crtos_app: not a program header");
    else if (ai.abi != APP_ABI)
        problem(r, ".crtos_app: program ABI %lu, this toolchain's is %u", (unsigned long)ai.abi, APP_ABI);
    if (ai.stack_size > MAX_STACK)
        problem(r, "stack %lu bytes: at most %u", (unsigned long)ai.stack_size, MAX_STACK);
    if (ai.heap_size > MAX_HEAP)
        problem(r, "heap %lu bytes: at most %u", (unsigned long)ai.heap_size, MAX_HEAP);
}

/* a relocatable program (ld -r): the kernel's elf.cpp */
static void check_rel(struct elf *e, struct report *r)
{
    if ((unsigned long)e->size > MAX_FILE)
        problem(r, "%lu bytes: the loader takes at most %u MB", (unsigned long)e->size, MAX_FILE >> 20);
    for (int i = 0; i < e->eh.shnum; i++) {
        const shdr_t *s = &e->sh[i];
        if ((s->flags & SHF_ALLOC) && s->addralign > 1 && ((s->addralign & (s->addralign - 1)) || s->addralign > MAX_ALIGN))
            problem(r, "section %s: alignment %lu (at most %u, a power of two)", sec_name(e, i),
                    (unsigned long)s->addralign, MAX_ALIGN);
    }
    if (load_symbols(e))
        r->bad++;
    else if (e->symtab < 0)
        problem(r, "no symbol table (was it stripped completely? use strip --strip-debug)");
    else {
        int start = 0;
        for (uint32_t i = 1; i < e->nsyms; i++) {
            const sym_t *s = &e->syms[i];
            const char *n = sym_name(e, s);
            if (s->shndx == SHN_COMMON)
                problem(r, "%s: a COMMON symbol (build with -fno-common)", n);
            else if (s->shndx == SHN_UNDEF && (s->info >> 4) != STB_WEAK && *n && !is_loader_symbol(n))
                problem(r, "%s: undefined (programs cannot use the kernel's symbols: link the library it is in)", n);
            if (!strcmp(n, "_start") && s->shndx != SHN_UNDEF) {
                start = 1;
                if ((s->info & 15) != STT_FUNC || !(s->value & 1))
                    problem(r, "_start is not a Thumb function");
            }
        }
        if (!start)
            problem(r, "no entry point _start (link with crtos.specs: it brings crt0)");
    }
    /* relocations: one table at a time */
    unsigned seen[256] = { 0 };
    for (int i = 0; i < e->eh.shnum; i++) {
        const shdr_t *s = &e->sh[i];
        if (s->type != SHT_REL && s->type != SHT_RELA)
            continue;
        if (s->info >= e->eh.shnum || !(e->sh[s->info].flags & SHF_ALLOC))
            continue; /* debugging information: the loader never reads it */
        uint32_t ent = s->type == SHT_REL ? 8 : 12;
        unsigned char *rel = read_at(e, s->offset, s->size);
        if (!rel) {
            r->bad++;
            continue;
        }
        for (uint32_t k = 0; k + ent <= s->size; k += ent) {
            unsigned type = rd32(rel + k + 4) & 0xff;
            if (!reloc_name(type) && !seen[type]++)
                problem(r, "relocation type %u (in %s) is not one the loader applies", type, sec_name(e, s->info));
        }
        free(rel);
    }
}

/* a program that runs in place (ET_DYN): the kernel's xip_peek() and xip_relocate() */
static void check_xip(struct elf *e, struct report *r)
{
    const phdr_t *text, *data;
    const char *why = xip_segments(e, &text, &data);
    if (why) {
        problem(r, "%s", why);
        return;
    }
    if (text->filesz > MAX_FILE && !r->quiet)
        printf("%s: note: %lu KB of text - it runs only in place (from /flash0); other file systems "
               "would have to copy it, and the loader copies at most %u MB\n",
               r->path, (unsigned long)(text->filesz / 1024u), MAX_FILE >> 20);
    if (data->memsz > MAX_HEAP)
        problem(r, "data and bss %lu bytes: at most %u", (unsigned long)data->memsz, MAX_HEAP);
    if (!(e->eh.entry & 1u) || e->eh.entry >= text->filesz)
        problem(r, "no Thumb entry point in the text (link with crtos.specs: it brings crt0)");
    int got = 0;
    for (int i = 0; i < e->eh.shnum; i++) {
        const shdr_t *s = &e->sh[i];
        const char *n = sec_name(e, i);
        int in_data = s->addr >= data->vaddr && s->addr - data->vaddr <= data->memsz &&
                      s->size <= data->memsz - (s->addr - data->vaddr);
        if ((s->flags & SHF_ALLOC) && s->addr >= XIP_DATA_BASE && s->addralign > MAX_ALIGN)
            problem(r, "section %s: alignment %lu (at most %u)", n, (unsigned long)s->addralign, MAX_ALIGN);
        if (!strcmp(n, ".got")) {
            got = in_data;
        } else if (!strcmp(n, ".preinit_array") || !strcmp(n, ".init_array") || !strcmp(n, ".fini_array")) {
            if (s->size && !in_data)
                problem(r, "%s is not in the data segment (link with crtos-xip.ld)", n);
        } else if ((s->type == SHT_REL || s->type == SHT_RELA) && (s->flags & SHF_ALLOC)) {
            if (strcmp(n, ".rel.dyn") || s->type != SHT_REL) {
                problem(r, "relocations in %s: the loader applies only .rel.dyn (link with crtos-xip.ld)", n);
                continue;
            }
            if (s->offset > text->filesz || s->size > text->filesz - s->offset) {
                problem(r, ".rel.dyn is not in the text segment (link with crtos-xip.ld)");
                continue;
            }
            unsigned char *rel = read_at(e, s->offset, s->size);
            if (!rel) {
                r->bad++;
                continue;
            }
            int other = 0, outside = 0;
            for (uint32_t k = 0; k + 8 <= s->size; k += 8) {
                uint32_t off = rd32(rel + k), info = rd32(rel + k + 4);
                if ((info & 0xff) == 0)
                    continue;
                if ((info & 0xff) != R_ARM_RELATIVE || (info >> 8))
                    other++;
                else if (off < data->vaddr || off - data->vaddr > data->memsz - 4u || (off & 3u))
                    outside++;
            }
            free(rel);
            if (other)
                problem(r, "%d dynamic relocations other than R_ARM_RELATIVE (a symbol left to a dynamic "
                           "linker: link statically with arm-crtos-gcc -mxip)", other);
            if (outside)
                problem(r, "%d relocations outside the data segment (code that needs changing: compile "
                           "everything with -mxip)", outside);
        }
    }
    if (!got)
        problem(r, "no GOT in the data segment (compile with -mxip)");
}

static int check(const char *path, int quiet)
{
    struct elf e;
    if (elf_open(&e, path, "rb")) {
        elf_close(&e);
        return 1;
    }
    struct report r = { path, 0, quiet };
    if (e.eh.type == ET_DYN)
        check_xip(&e, &r);
    else if (e.eh.type == ET_REL)
        check_rel(&e, &r);
    else
        problem(&r, "neither a relocatable file nor one that runs in place (type %u): link with arm-crtos-gcc",
                e.eh.type);
    check_header(&e, &r);
    if (r.bad > 20)
        fprintf(stderr, "%s: ... %d problems in all\n", path, r.bad);
    if (!r.bad && !quiet)
        printf("%s: ok (%lu bytes%s)\n", path, (unsigned long)e.size, e.eh.type == ET_DYN ? ", runs in place" : "");
    elf_close(&e);
    return r.bad ? 1 : 0;
}

static void print_header(const struct app_info *ai, int present)
{
    if (present && ai->magic == APP_MAGIC)
        printf("  header:  ABI %lu, stack %lu, heap %lu\n", (unsigned long)ai->abi, (unsigned long)ai->stack_size,
               (unsigned long)ai->heap_size);
    else if (present)
        printf("  header:  damaged (magic %08lx)\n", (unsigned long)ai->magic);
    else
        printf("  header:  none (the loader's defaults: stack %u, heap %u)\n", DEF_STACK, DEF_HEAP);
}

static int info(const char *path)
{
    struct elf e;
    if (elf_open(&e, path, "rb")) {
        elf_close(&e);
        return 1;
    }
    struct app_info ai;
    int present;
    if (read_info(&e, &ai, &present)) {
        elf_close(&e);
        return 1;
    }
    if (e.eh.type == ET_DYN) {
        const phdr_t *text, *data;
        const char *why = xip_segments(&e, &text, &data);
        if (why) {
            fprintf(stderr, "%s: %s\n", path, why);
            elf_close(&e);
            return 1;
        }
        uint32_t copied = ((text->filesz + 31u) & ~31u) + data->memsz + 32u;
        printf("%s: CRTOS program that runs in place (ELF ET_DYN, %lu bytes)\n", path, (unsigned long)e.size);
        print_header(&ai, present);
        printf("  memory:  text (code, constants) %lu, data %lu, bss %lu bytes\n", (unsigned long)text->filesz,
               (unsigned long)data->filesz, (unsigned long)(data->memsz - data->filesz));
        printf("  arena:   about %lu KB in place (/flash0), %lu KB when copied (other file systems)\n",
               (unsigned long)(arena_size(data->memsz, &ai) + 1023) / 1024,
               (unsigned long)(arena_size(copied, &ai) + 1023) / 1024);
        elf_close(&e);
        return 0;
    }
    uint32_t code, data, bss;
    layout(&e, &code, &data, &bss);
    uint32_t image = code + data + bss;
    printf("%s: CRTOS program (ELF relocatable, %lu bytes)\n", path, (unsigned long)e.size);
    print_header(&ai, present);
    printf("  memory:  code+constants %lu, data %lu, bss %lu = %lu bytes\n", (unsigned long)code, (unsigned long)data,
           (unsigned long)bss, (unsigned long)image);
    printf("  arena:   about %lu KB with heap and stack\n", (unsigned long)(arena_size(image, &ai) + 1023) / 1024);
    elf_close(&e);
    return 0;
}

/* "123", "64K", "2M" -> bytes; 0 on error */
static int parse_size(const char *s, uint32_t *out)
{
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if (end == s)
        return 0;
    if (*end == 'k' || *end == 'K')
        v *= 1024, end++;
    else if (*end == 'm' || *end == 'M')
        v *= 1024 * 1024, end++;
    if (*end || v > 0xffffffffu)
        return 0;
    *out = (uint32_t)v;
    return 1;
}

static int set(const char *path, int have_stack, uint32_t stack, int have_heap, uint32_t heap)
{
    struct elf e;
    if (elf_open(&e, path, "r+b")) {
        elf_close(&e);
        return 1;
    }
    struct app_info ai;
    int present;
    if (read_info(&e, &ai, &present)) {
        elf_close(&e);
        return 1;
    }
    if (!present || ai.magic != APP_MAGIC) {
        fprintf(stderr, "%s: no program header to change (link with crtos.specs: libcrtos brings one)\n", path);
        elf_close(&e);
        return 1;
    }
    if (have_stack)
        ai.stack_size = stack;
    if (have_heap)
        ai.heap_size = heap;
    if (ai.stack_size > MAX_STACK || ai.heap_size > MAX_HEAP) {
        fprintf(stderr, "%s: stack at most %u, heap at most %u bytes\n", path, MAX_STACK, MAX_HEAP);
        elf_close(&e);
        return 1;
    }
    unsigned char b[8];
    wr32(b, ai.stack_size);
    wr32(b + 4, ai.heap_size);
    int r = 0;
    if (fseek(e.f, (long)(e.sh[e.app].offset + 8), SEEK_SET) || fwrite(b, 1, 8, e.f) != 8 || fflush(e.f)) {
        fprintf(stderr, "%s: write error\n", path);
        r = 1;
    } else {
        printf("%s: stack %lu, heap %lu\n", path, (unsigned long)ai.stack_size, (unsigned long)ai.heap_size);
    }
    elf_close(&e);
    return r;
}

static int usage(void)
{
    fprintf(stderr, "usage: crtos-app info PROGRAM...\n"
                    "       crtos-app set PROGRAM [--stack N] [--heap N]   (N: bytes, or with K or M)\n"
                    "       crtos-app check [-q] PROGRAM...\n");
    return 2;
}

int main(int argc, char **argv)
{
    if (argc < 3)
        return usage();
    const char *cmd = argv[1];
    int r = 0;
    if (!strcmp(cmd, "info")) {
        for (int i = 2; i < argc; i++)
            r |= info(argv[i]);
    } else if (!strcmp(cmd, "check")) {
        int quiet = 0, first = 2;
        if (!strcmp(argv[2], "-q"))
            quiet = 1, first = 3;
        for (int i = first; i < argc; i++)
            r |= check(argv[i], quiet);
    } else if (!strcmp(cmd, "set")) {
        uint32_t stack = 0, heap = 0;
        int hs = 0, hh = 0;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--stack") && i + 1 < argc && parse_size(argv[i + 1], &stack))
                hs = 1, i++;
            else if (!strcmp(argv[i], "--heap") && i + 1 < argc && parse_size(argv[i + 1], &heap))
                hh = 1, i++;
            else
                return usage();
        }
        if (!hs && !hh)
            return usage();
        r = set(argv[2], hs, stack, hh, heap);
    } else {
        return usage();
    }
    return r;
}
