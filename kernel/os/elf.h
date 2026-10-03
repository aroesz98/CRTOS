/*
 * kernel/os/elf.h - loading relocatable ELF objects (kernel modules and user programs).
 *
 * Both are "ld -r" outputs. All allocatable sections are placed in one block (code, then
 * data, then bss); undefined symbols are resolved by the caller's resolver, then the
 * relocations are applied. Supported relocations: ABS32/TARGET1, REL32, PREL31, THM_CALL,
 * THM_JUMP24, THM_JUMP19, THM_JUMP11, THM_MOVW_ABS_NC, THM_MOVT_ABS, V4BX, NONE.
 *
 * Programs may split the image (elf_split_fast): the sections named ".fast*" (code the build
 * picked for on-chip memory) go to a second block. A branch between the blocks that its
 * encoding cannot reach goes through a veneer (ldr pc, =target) in a pool at the end of the
 * code of the block it starts in.
 */
#ifndef KERNEL_ELF_H
#define KERNEL_ELF_H

#include "kernel.h"

struct Elf32_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};
struct Elf32_Shdr {
    uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize;
};
struct Elf32_Sym {
    uint32_t st_name, st_value, st_size;
    uint8_t st_info, st_other;
    uint16_t st_shndx;
};
struct Elf32_Phdr {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
};
struct Elf32_Rel {
    uint32_t r_offset, r_info;
};

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
#define SHF_ALLOC       2u
#define SHF_EXECINSTR   4u

struct elf_ctx {
    const uint8_t *file;
    size_t size;
    const char *who;            /* prefix for messages */
    const Elf32_Ehdr *eh;
    const Elf32_Shdr *sh;
    const char *shstr;
    uint32_t *secaddr;          /* per section: offset after elf_layout(), address after elf_place() */
    uint32_t *symaddr;
    const Elf32_Sym *syms;
    uint32_t nsyms;
    const char *strtab;
    uint32_t strsize;
    /* the split image (elf_split_fast) */
    uint8_t *fast;              /* per section: 1 = in the fast block; NULL: one block */
    uint32_t pool[2];           /* veneer pools of the main (0) and the fast (1) block: offset after
                                 * the layout, address after placing */
    uint32_t pool_used[2];      /* bytes of each pool handed out */
    struct elf_veneer *ven;     /* the veneers made (a hash on symbol and addend) */
    uint32_t ven_cap;
};

/* Splits the image: the sections named ".fast*" go to a second block. 0 with @size (main
 * block), @fsize (fast block, its code and veneers) and the alignments; -ENOENT if there are
 * no such sections, which leaves the context as it was. */
int elf_split_fast(struct elf_ctx *c, uint32_t *size, uint32_t *align, uint32_t *fsize, uint32_t *falign);
void elf_unsplit(struct elf_ctx *c);                                 /* back to one block */
void elf_place_split(struct elf_ctx *c, uint8_t *base, uint8_t *fbase);

/* Address for an undefined symbol, 0 if unknown (weak symbols may stay 0) */
typedef uintptr_t (*elf_resolve_fn)(const char *name, void *ctx);

int elf_check(struct elf_ctx *c);
int elf_layout(struct elf_ctx *c, uint32_t *size, uint32_t *align);
/* The same from a section header table alone (@secaddr may be NULL) */
int elf_layout_sections(const Elf32_Shdr *sh, uint32_t n, uint32_t *secaddr, uint32_t *size, uint32_t *align);
void elf_place(struct elf_ctx *c, uint8_t *base);
int elf_symbols(struct elf_ctx *c, elf_resolve_fn resolve, void *ctx);
int elf_relocate(struct elf_ctx *c);
const Elf32_Shdr *elf_section(const struct elf_ctx *c, const char *name, uint32_t *index);
uint32_t elf_symbol(const struct elf_ctx *c, const char *name);     /* defined symbol, 0 if none */
void elf_release(struct elf_ctx *c);                                 /* the tables, not the file */
void elf_forget_symbols(struct elf_ctx *c);                          /* before elf_symbols() again */

#endif
