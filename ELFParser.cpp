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

#include <cstring>
#include <cstdlib>
//#include <cstdio>

#include <ELFParser.hpp>

int ElfFile::parse(const uint8_t *elf)
{
    // Set binary pointer to elf file temporary
    binary = (uint8_t*)elf;

    parse_elf();

    // We parsed the file so we can allocate required memory and copy file to RAM
    binary = (uint8_t*)malloc(phdr->p_offset + phdr->p_memsz);
    memcpy(&binary[0], &elf[0], phdr->p_offset + phdr->p_memsz);

    header = (Elf32_Ehdr*)&binary[0];
    program_header = (Elf32_Phdr*)&binary[header->e_phoff];

    ProgramInfoExtra info;
    memset(&info, 0, sizeof(ProgramInfoExtra));
    info.progInfo   = (ProgramInfo*)&binary[program_header->p_offset];

    info.stackSize  = info.progInfo->stackPointer - info.progInfo->msp_limit;
    info.ramSize    = info.progInfo->stackPointer - info.progInfo->section_data_dest_addr;

    uint8_t *stk = (uint8_t*)malloc(info.ramSize);
    memset(stk, 0, info.ramSize);

    // Find and copy data section to proper address
    for (int i = 0; i < ehdr->e_phnum; ++i)
    {
        if (info.progInfo->section_data_dest_addr == phdr[i].p_vaddr)
        {
            memcpy(&binary[program_header->p_offset + phdr[i].p_paddr], &binary[phdr[i].p_offset], phdr[i].p_memsz);
        }
    }

    // Magic things
    info.new_data_ram_addr = (uint32_t)stk;
    info.new_data_flash_addr = (uint32_t)&binary[program_header->p_offset + info.progInfo->section_data_start_addr];
    info.new_bss_addr = info.new_data_ram_addr + info.progInfo->section_data_size;
    info.new_msp = info.new_data_ram_addr + info.ramSize;
    info.new_msplim = info.new_msp - info.stackSize;
    info.new_entry = (uint32_t)&binary[program_header->p_offset + info.progInfo->entryPoint - 1];

    info.progInfo->section_data_dest_addr = info.new_data_ram_addr;
    info.progInfo->section_data_start_addr = info.new_data_flash_addr;
    info.progInfo->section_bss_start_addr = info.new_bss_addr;
    info.progInfo->stackPointer = info.new_msp;
    info.progInfo->msp_limit = info.new_msplim;
    info.progInfo->entryPoint = info.new_entry;
    info.progInfo->vtor_offset = (uint32_t)&binary[program_header->p_offset];

    entry_point = (void (*)(void *))info.progInfo->entryPoint;

    return 0;
}

int ElfFile::parse(const uint8_t *elf, uint32_t **stack, uint32_t *stackSize, uint32_t *vtor_offset)
{
    // Set binary pointer to elf file temporary
    binary = (uint8_t*)elf;

    parse_elf();

    binary = nullptr;

    // We parsed the file so we can allocate required memory and copy file to RAM
    binary = (uint8_t*)malloc(phdr->p_offset + phdr->p_memsz);
    memcpy(&binary[0], &elf[0], phdr->p_offset + phdr->p_memsz);

    header = (Elf32_Ehdr*)&binary[0];
    program_header = (Elf32_Phdr*)&binary[header->e_phoff];

    ProgramInfoExtra info;
    memset(&info, 0, sizeof(ProgramInfoExtra));
    info.progInfo   = (ProgramInfo*)&binary[program_header->p_offset];

    info.stackSize  = 0u;//info.progInfo->stackPointer - info.progInfo->msp_limit;
    info.ramSize    = info.progInfo->stackPointer - info.progInfo->section_data_dest_addr;

    uint8_t *stk = (uint8_t*)malloc(info.ramSize);
    memset(stk, 0, info.ramSize);

    uint32_t data_sum = 0u;

    // Find and copy data section to proper address
    for (int i = 0; i < ehdr->e_phnum; ++i)
    {
        if (phdr[i].p_flags == 6u)
        {
            data_sum += phdr[i].p_memsz;
        }
        if (info.progInfo->section_data_dest_addr == phdr[i].p_vaddr)
        {
            memcpy(&binary[program_header->p_offset + phdr[i].p_paddr], &binary[phdr[i].p_offset], phdr[i].p_memsz);
        }
    }

    info.stackSize = info.ramSize - data_sum;

    // Magic things
    info.new_data_ram_addr = (uint32_t)stk;
    info.new_data_flash_addr = (uint32_t)&binary[program_header->p_offset + info.progInfo->section_data_start_addr];
    info.new_bss_addr = info.new_data_ram_addr + info.progInfo->section_data_size;
    info.new_msp = info.new_data_ram_addr + info.ramSize;
    info.new_msplim = info.new_msp - info.stackSize;
    info.new_entry = (uint32_t)&binary[program_header->p_offset + info.progInfo->entryPoint - 1];

    *stack = (uint32_t*)(info.new_msplim);
    *stackSize = (info.stackSize / sizeof(uint32_t));

    info.progInfo->section_data_dest_addr = info.new_data_ram_addr;
    info.progInfo->section_data_start_addr = info.new_data_flash_addr;
    info.progInfo->section_bss_start_addr = info.new_bss_addr;
    info.progInfo->stackPointer = info.new_msp;
    info.progInfo->msp_limit = info.new_msplim;
    info.progInfo->entryPoint = info.new_entry;
    info.progInfo->vtor_offset = (uint32_t)&binary[program_header->p_offset];

    *vtor_offset = info.progInfo->vtor_offset;

    entry_point = (void (*)(void *))info.progInfo->entryPoint;

    return 0;
}

//void ElfFile::print_program_headers() const
//{
//    for (int i = 0; i < ehdr->e_phnum; ++i)
//    {
//        printf("-----------------------------------\n");
//        printf("-- Program Header %d:\n", i);
//        printf("* Type: %ld\n", phdr[i].p_type);
//        printf("* Offset: 0x%lx\n", phdr[i].p_offset);
//        printf("* Virtual Address: 0x%lx\n", phdr[i].p_vaddr);
//        printf("* Physical Address: 0x%lx\n", phdr[i].p_paddr);
//        printf("* File Size: %ld bytes\n", phdr[i].p_filesz);
//        printf("* Memory Size: %ld bytes\n", phdr[i].p_memsz);
//        printf("* Flags: %ld\n", phdr[i].p_flags);
//        printf("* Alignment: %ld\n", phdr[i].p_align);
//    }
//    printf("---------------------------------------\n");
//}

void ElfFile::parse_symbols() const
{
    const Elf32_Sym* symtab = nullptr;
    const char* strtab = nullptr;

    for (int i = 0; i < ehdr->e_shnum; ++i)
    {
        if (shdr[i].sh_type == 2)
        {  // 2 means SHT_SYMTAB (symbol table)
            symtab = reinterpret_cast<const Elf32_Sym*>(binary + shdr[i].sh_offset);
            strtab = reinterpret_cast<const char*>(binary + shdr[shdr[i].sh_link].sh_offset);
            break;
        }
    }

    if (symtab && strtab) {
        for (int i = 0; symtab[i].st_name != 0; ++i)
        {
            const char* symbol_name = strtab + symtab[i].st_name;
            if (std::strcmp(symbol_name, "ResetISR") == 0)
            {
//                print_symbol_info(symbol_name, symtab[i].st_value, symtab[i].st_size);
                break;
            }
        }
    }
}

//void ElfFile::print_section_info(const char* section_name, uint32_t addr, uint32_t size) const
//{
//    printf("%s section:\n", section_name);
//    printf("Start Address: 0x%lx\n", addr);
//    printf("Size: %ld bytes\n", size);
//}
//
//void ElfFile::print_symbol_info(const char* symbol_name, uint32_t addr, uint32_t size) const
//{
//    printf("%s function:\n", symbol_name);
//    printf("Address: 0x%lx\n", addr);
//    printf("Size: %ld bytes\n", size);
//}

void ElfFile::parse_sections() const
{
    const char* shstrtab = reinterpret_cast<const char*>(binary + shdr[ehdr->e_shstrndx].sh_offset);

    for (int i = 0; i < ehdr->e_shnum; ++i)
    {
        const char* section_name = shstrtab + shdr[i].sh_name;
        if (std::strcmp(section_name, ".text") == 0 ||
            std::strcmp(section_name, ".bss") == 0 ||
            std::strcmp(section_name, ".data") == 0)
        {
//            print_section_info(section_name, shdr[i].sh_addr, shdr[i].sh_size);
        }
    }

//    printf("Entry Point: 0x%lx\n", ehdr->e_entry);

    // Read MSP
    for (int i = 0; i < ehdr->e_phnum; ++i)
    {
        if (phdr[i].p_type == 1)
        {
            // 1 means PT_LOAD (loadable segment)
//            uint32_t msp = *(reinterpret_cast<const uint32_t*>(binary + phdr[i].p_offset));
//            printf("MSP: 0x%lx\n", msp);
            break;
        }
    }
}

void ElfFile::parse_elf()
{
    ehdr = reinterpret_cast<const Elf32_Ehdr*>(binary);
    phdr = reinterpret_cast<const Elf32_Phdr*>(binary + ehdr->e_phoff);
    shdr = reinterpret_cast<const Elf32_Shdr*>(binary + ehdr->e_shoff);

    parse_sections();
    parse_symbols();
//    print_program_headers();
}
