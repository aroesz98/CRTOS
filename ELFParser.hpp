/*
 * ELFParser
 * Author: Arkadiusz Szlanta
 * Date: 07 Feb 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 */

#ifndef ELF_PARSER_H
#define ELF_PARSER_H

#include <cstdint>

#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7

struct Elf32_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Phdr {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
};

struct Elf32_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
};

struct ProgramInfo
{
    uint32_t stackPointer;
    uint32_t entryPoint;
    uint32_t vectors[74];
    uint32_t section_data_start_addr;
    uint32_t section_data_dest_addr;
    uint32_t section_data_size;
    uint32_t section_bss_start_addr;
    uint32_t section_bss_size;
    uint32_t reserved[22]; // TODO: Find a solution to overwrite MSPLIM/VTOR values.
    uint32_t vtor_offset;
    uint32_t msp_limit;
};

struct ProgramInfoExtra
{
    ProgramInfo *progInfo;
    uint32_t new_data_flash_addr;
    uint32_t new_data_ram_addr;
    uint32_t new_bss_addr;
    uint32_t new_msp;
    uint32_t new_msplim;
    uint32_t new_entry;
    uint32_t ramSize;
    uint32_t stackSize;
};

// Symbol Table Entry structure
struct Elf32_Sym {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
};

class ElfFile {
public:
    ElfFile() : entry_point(nullptr) {}
    int parse(const uint8_t *buffer);
    int parse(const uint8_t *elf, uint32_t **stack, uint32_t *stackSize, uint32_t *vtor_offset);
    uint32_t getMemSize(void);

    void (*entry_point)(void *);

private:
    uint8_t *binary;
    Elf32_Ehdr *header;
    Elf32_Phdr *program_header;

    const Elf32_Ehdr* ehdr;
    const Elf32_Phdr* phdr;
    const Elf32_Shdr* shdr;

//    void print_program_headers() const;

//    void print_section_info(const char* section_name, uint32_t addr, uint32_t size) const;

//    void print_symbol_info(const char* symbol_name, uint32_t addr, uint32_t size) const;

    void parse_symbols() const;

    void parse_sections() const;

    void parse_elf();
};


#endif // ELF_PARSER_H
