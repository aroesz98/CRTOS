/*
 * ModuleLoader.cpp - CRTOS Dynamic Module Loader Implementation
 * Author: Arkadiusz Szlanta
 * Date: 03 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

// IMPORTANT: Device header must be included BEFORE core_cm7.h to define cache presence
#include "fsl_device_registers.h"

#include "ModuleLoader.hpp"
#include "Drivers/DriverBase.hpp"
#include "Drivers/DriverManager.hpp"
#include "HeapAllocator.hpp"
#include "KernelAPI.hpp"
#include "Task.hpp"
#include "BinarySemaphore.hpp"
#include "DPCWorker.hpp"
#include "HAL/HAL_UART.hpp"
#include "HAL/HAL_Display.hpp"
#include "HAL/HAL_SPI.hpp"
#include "HAL/HAL_PinMux.hpp"
#include "fsl_debug_console.h"
#include "core_cm7.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>

// Memory allocation wrappers (defined in KernelExports.cpp)
extern "C" void* crtos_malloc(uint32_t size);
extern "C" void crtos_free(void* ptr);

// FatFS for file operations
extern "C" {
#include "ff.h"
}

// ELF constants
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4
#define EI_DATA         5

#define ELFMAG0         0x7f
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'

#define ELFCLASS32      1
#define ELFDATA2LSB     1   // Little endian

#define ET_REL          1   // Relocatable file
#define EM_ARM          40  // ARM architecture

#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_NOBITS      8
#define SHT_REL         9

#define SHF_ALLOC       0x2
#define SHF_EXECINSTR   0x4

#define STB_LOCAL       0
#define STB_GLOBAL      1

#define STT_NOTYPE      0
#define STT_FUNC        2

#define SHN_UNDEF       0
#define SHN_ABS         0xFFF1

// ELF type - support both relocatable and shared objects
#define ET_DYN          3   // Shared object (for PIC modules)

// ARM relocation types - full set for PIC/GOT/PLT support
#define R_ARM_NONE            0
#define R_ARM_PC24            1    // PC-relative 24-bit branch
#define R_ARM_ABS32           2    // Direct 32-bit
#define R_ARM_REL32           3    // PC-relative 32-bit
#define R_ARM_THM_CALL        10   // Thumb BL/BLX
#define R_ARM_COPY            20   // Copy symbol at runtime
#define R_ARM_GLOB_DAT        21   // Create GOT entry
#define R_ARM_JUMP_SLOT       22   // Create PLT entry
#define R_ARM_RELATIVE        23   // Adjust by program base
#define R_ARM_GOTOFF32        24   // 32-bit offset from GOT
#define R_ARM_BASE_PREL       25   // PC-relative to GOT
#define R_ARM_GOT_BREL        26   // GOT(S) + A - GOT_ORG
#define R_ARM_PLT32           27   // 32-bit PLT address
#define R_ARM_CALL            28   // ARM BL/BLX
#define R_ARM_JUMP24          29   // ARM B/BL<cond>
#define R_ARM_THM_JUMP24      30   // Thumb B.W
#define R_ARM_TARGET1         38   // Target-specific (=ABS32 or REL32)
#define R_ARM_V4BX            40   // BX instruction
#define R_ARM_PREL31          42   // PC-relative 31-bit
#define R_ARM_MOVW_ABS_NC     43   // ARM MOVW (lower 16 bits)
#define R_ARM_MOVT_ABS        44   // ARM MOVT (upper 16 bits)
#define R_ARM_THM_MOVW_ABS_NC 47   // Thumb MOVW absolute
#define R_ARM_THM_MOVT_ABS    48   // Thumb MOVT absolute
#define R_ARM_THM_MOVW_PREL_NC 49  // Thumb MOVW PC-relative
#define R_ARM_THM_MOVT_PREL   50   // Thumb MOVT PC-relative
#define R_ARM_GOT_ABS         95   // GOT entry absolute
#define R_ARM_GOT_PREL        96   // GOT entry PC-relative

// Thumb veneer (trampoline) for long branches
// This is 8 bytes: LDR PC, [PC, #0] followed by the target address
// Used when branch target is more than ±16MB away
struct ThumbVeneer {
    uint16_t ldr_pc;     // 0xF8DF 0xF000 = LDR.W PC, [PC, #0]
    uint16_t ldr_pc2;
    uint32_t target;     // Target address (with Thumb bit set)
};

// Maximum veneers per module
static constexpr size_t MAX_VENEERS = 64;

// ELF structures
struct Elf32_Ehdr {
    uint8_t  e_ident[16];
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

struct Elf32_Sym {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
};

struct Elf32_Rel {
    uint32_t r_offset;
    uint32_t r_info;
};

struct Elf32_Rela {
    uint32_t r_offset;
    uint32_t r_info;
    int32_t  r_addend;
};

#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xff)
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)

namespace CRTOS
{

// Singleton instance
ModuleLoader& ModuleLoader::getInstance()
{
    static ModuleLoader instance;
    return instance;
}

ModuleLoader::ModuleLoader()
    : _symbolCount(0)
    , _moduleCount(0)
    , _initialized(false)
{
    memset(_kernelSymbols, 0, sizeof(_kernelSymbols));
    memset(_modules, 0, sizeof(_modules));
}

ModuleLoader::~ModuleLoader()
{
    // Unload all modules
    for (size_t i = 0; i < MAX_MODULES; i++)
    {
        if (_modules[i].isLoaded)
        {
            unloadModule(_modules[i].name);
        }
    }
}

ModuleResult ModuleLoader::init()
{
    if (_initialized)
    {
        return ModuleResult::SUCCESS;
    }

    // Initialize kernel API singleton
    InitKernelAPI();
    
    // Register kernel API accessor - this is the primary way modules get kernel functions
    registerSymbol("crtos_get_kernel_api", (void*)&crtos_get_kernel_api);

    // Register default kernel symbols that modules commonly need
    // Note: C++ mangled names are NOT registered here - use C wrappers from KernelExports.cpp
    // Call RegisterKernelSymbols() to register all exported kernel functions
    
    // HAL UART functions - multi-instance API
    registerSymbol("hal_uart_get_default_config", (void*)&hal_uart_get_default_config);
    registerSymbol("hal_uart_init_instance", (void*)&hal_uart_init_instance);
    registerSymbol("hal_uart_deinit_instance", (void*)&hal_uart_deinit_instance);
    registerSymbol("hal_uart_is_initialized_instance", (void*)&hal_uart_is_initialized_instance);
    registerSymbol("hal_uart_write_byte_instance", (void*)&hal_uart_write_byte_instance);
    registerSymbol("hal_uart_write_instance", (void*)&hal_uart_write_instance);
    registerSymbol("hal_uart_read_byte_instance", (void*)&hal_uart_read_byte_instance);
    registerSymbol("hal_uart_flush_tx_instance", (void*)&hal_uart_flush_tx_instance);
    registerSymbol("hal_uart_get_status", (void*)&hal_uart_get_status);
    registerSymbol("hal_uart_clear_errors", (void*)&hal_uart_clear_errors);
    registerSymbol("hal_uart_get_irq_number", (void*)&hal_uart_get_irq_number);
    registerSymbol("hal_uart_enable_rx_irq", (void*)&hal_uart_enable_rx_irq);
    registerSymbol("hal_uart_disable_rx_irq", (void*)&hal_uart_disable_rx_irq);
    registerSymbol("hal_uart_enable_tx_irq", (void*)&hal_uart_enable_tx_irq);
    registerSymbol("hal_uart_disable_tx_irq", (void*)&hal_uart_disable_tx_irq);
    
    // HAL UART legacy API (backwards compatibility)
    registerSymbol("hal_uart_init", (void*)&hal_uart_init);
    registerSymbol("hal_uart_deinit", (void*)&hal_uart_deinit);
    registerSymbol("hal_uart_write_byte", (void*)&hal_uart_write_byte);
    registerSymbol("hal_uart_write", (void*)&hal_uart_write);
    registerSymbol("hal_uart_read_byte", (void*)&hal_uart_read_byte);
    registerSymbol("hal_uart_is_initialized", (void*)&hal_uart_is_initialized);
    registerSymbol("hal_uart_flush_tx", (void*)&hal_uart_flush_tx);
    
    // HAL Display API
    registerSymbol("hal_display_get_default_config", (void*)&hal_display_get_default_config);
    registerSymbol("hal_display_init", (void*)&hal_display_init);
    registerSymbol("hal_display_deinit", (void*)&hal_display_deinit);
    registerSymbol("hal_display_is_initialized", (void*)&hal_display_is_initialized);
    registerSymbol("hal_display_enable", (void*)&hal_display_enable);
    registerSymbol("hal_display_disable", (void*)&hal_display_disable);
    registerSymbol("hal_display_get_framebuffer", (void*)&hal_display_get_framebuffer);
    registerSymbol("hal_display_get_backbuffer", (void*)&hal_display_get_backbuffer);
    registerSymbol("hal_display_swap_buffers", (void*)&hal_display_swap_buffers);
    registerSymbol("hal_display_flush_cache", (void*)&hal_display_flush_cache);
    registerSymbol("hal_display_clear", (void*)&hal_display_clear);
    registerSymbol("hal_display_fill_rect", (void*)&hal_display_fill_rect);
    registerSymbol("hal_display_draw_pixel", (void*)&hal_display_draw_pixel);
    registerSymbol("hal_display_get_pixel", (void*)&hal_display_get_pixel);
    registerSymbol("hal_display_wait_vsync", (void*)&hal_display_wait_vsync);
    registerSymbol("hal_display_get_irq_number", (void*)&hal_display_get_irq_number);
    registerSymbol("hal_display_get_width", (void*)&hal_display_get_width);
    registerSymbol("hal_display_get_height", (void*)&hal_display_get_height);
    registerSymbol("hal_display_get_format", (void*)&hal_display_get_format);
    registerSymbol("hal_display_get_bytes_per_pixel", (void*)&hal_display_get_bytes_per_pixel);
    registerSymbol("hal_display_get_status", (void*)&hal_display_get_status);
    
    // HAL SPI API
    registerSymbol("hal_spi_get_default_config", (void*)&hal_spi_get_default_config);
    registerSymbol("hal_spi_init", (void*)&hal_spi_init);
    registerSymbol("hal_spi_deinit", (void*)&hal_spi_deinit);
    registerSymbol("hal_spi_is_initialized", (void*)&hal_spi_is_initialized);
    registerSymbol("hal_spi_set_baudrate", (void*)&hal_spi_set_baudrate);
    registerSymbol("hal_spi_set_mode", (void*)&hal_spi_set_mode);
    registerSymbol("hal_spi_set_pcs", (void*)&hal_spi_set_pcs);
    registerSymbol("hal_spi_set_transfer_mode", (void*)&hal_spi_set_transfer_mode);
    registerSymbol("hal_spi_transfer_blocking", (void*)&hal_spi_transfer_blocking);
    registerSymbol("hal_spi_transfer_nonblocking", (void*)&hal_spi_transfer_nonblocking);
    registerSymbol("hal_spi_get_status", (void*)&hal_spi_get_status);
    registerSymbol("hal_spi_abort_transfer", (void*)&hal_spi_abort_transfer);
    registerSymbol("hal_spi_wait_complete", (void*)&hal_spi_wait_complete);
    registerSymbol("hal_spi_write", (void*)&hal_spi_write);
    registerSymbol("hal_spi_read", (void*)&hal_spi_read);
    registerSymbol("hal_spi_write_read", (void*)&hal_spi_write_read);
    registerSymbol("hal_spi_get_irq_number", (void*)&hal_spi_get_irq_number);
    registerSymbol("hal_spi_get_clock_freq", (void*)&hal_spi_get_clock_freq);
    
    // HAL PinMux API
    registerSymbol("hal_pinmux_init", (void*)&hal_pinmux_init);
    registerSymbol("hal_pinmux_is_initialized", (void*)&hal_pinmux_is_initialized);
    registerSymbol("hal_pinmux_set_mux", (void*)&hal_pinmux_set_mux);
    registerSymbol("hal_pinmux_set_mux_raw", (void*)&hal_pinmux_set_mux_raw);
    registerSymbol("hal_pinmux_get_mux_mode", (void*)&hal_pinmux_get_mux_mode);
    registerSymbol("hal_pinmux_set_pad_config", (void*)&hal_pinmux_set_pad_config);
    registerSymbol("hal_pinmux_set_pad_raw", (void*)&hal_pinmux_set_pad_raw);
    registerSymbol("hal_pinmux_get_pad_config", (void*)&hal_pinmux_get_pad_config);
    registerSymbol("hal_pinmux_build_pad_value", (void*)&hal_pinmux_build_pad_value);
    registerSymbol("hal_pinmux_get_default_pad_config", (void*)&hal_pinmux_get_default_pad_config);
    registerSymbol("hal_pinmux_set_gpio_direction", (void*)&hal_pinmux_set_gpio_direction);
    registerSymbol("hal_pinmux_gpio_write", (void*)&hal_pinmux_gpio_write);
    registerSymbol("hal_pinmux_gpio_read", (void*)&hal_pinmux_gpio_read);
    registerSymbol("hal_pinmux_gpio_toggle", (void*)&hal_pinmux_gpio_toggle);
    
    // Standard C functions
    registerSymbol("memset", (void*)&memset);
    registerSymbol("memcpy", (void*)&memcpy);
    registerSymbol("strlen", (void*)&strlen);
    registerSymbol("strcmp", (void*)&strcmp);
    
    // Memory allocation (via HeapAllocator)
    registerSymbol("malloc", (void*)&crtos_malloc);
    registerSymbol("free", (void*)&crtos_free);

    _initialized = true;
    return ModuleResult::SUCCESS;
}

ModuleResult ModuleLoader::registerSymbol(const char* name, void* address)
{
    if (name == nullptr || address == nullptr)
    {
        return ModuleResult::ERROR_SYMBOL_NOT_FOUND;
    }

    // Check for duplicate
    for (size_t i = 0; i < _symbolCount; i++)
    {
        if (strcmp(_kernelSymbols[i].name, name) == 0)
        {
            // Update existing
            _kernelSymbols[i].address = address;
            return ModuleResult::SUCCESS;
        }
    }

    if (_symbolCount >= MAX_KERNEL_SYMBOLS)
    {
        return ModuleResult::ERROR_NO_MEMORY;
    }

    _kernelSymbols[_symbolCount].name = name;
    _kernelSymbols[_symbolCount].address = address;
    _symbolCount++;

    return ModuleResult::SUCCESS;
}

bool ModuleLoader::validateElf(const uint8_t* elfData, size_t elfSize)
{
    if (elfSize < sizeof(Elf32_Ehdr))
    {
        return false;
    }

    const Elf32_Ehdr* ehdr = reinterpret_cast<const Elf32_Ehdr*>(elfData);

    // Check magic number
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3)
    {
        return false;
    }

    // Check class (32-bit)
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32)
    {
        return false;
    }

    // Check endianness (little endian)
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
    {
        return false;
    }

    // Check type (relocatable OR shared object for PIC)
    if (ehdr->e_type != ET_REL && ehdr->e_type != ET_DYN)
    {
        return false;
    }

    // Check architecture (ARM)
    if (ehdr->e_machine != EM_ARM)
    {
        return false;
    }

    return true;
}

void* ModuleLoader::lookupSymbol(const char* name)
{
    for (size_t i = 0; i < _symbolCount; i++)
    {
        if (strcmp(_kernelSymbols[i].name, name) == 0)
        {
            return _kernelSymbols[i].address;
        }
    }
    return nullptr;
}

void* ModuleLoader::resolveSymbol(const char* name)
{
    return lookupSymbol(name);
}

ModuleResult ModuleLoader::loadModule(const uint8_t* elfData, size_t elfSize, const char* moduleName)
{
    if (!_initialized)
    {
        init();
    }

    // Check if already loaded
    if (findModule(moduleName) != nullptr)
    {
        return ModuleResult::ERROR_ALREADY_LOADED;
    }

    // Validate ELF
    if (!validateElf(elfData, elfSize))
    {
        return ModuleResult::ERROR_INVALID_ELF;
    }

    // Find free slot
    LoadedModule* module = nullptr;
    for (size_t i = 0; i < MAX_MODULES; i++)
    {
        if (!_modules[i].isLoaded)
        {
            module = &_modules[i];
            break;
        }
    }

    if (module == nullptr)
    {
        return ModuleResult::ERROR_NO_MEMORY;
    }

    const Elf32_Ehdr* ehdr = reinterpret_cast<const Elf32_Ehdr*>(elfData);
    const Elf32_Shdr* shdr = reinterpret_cast<const Elf32_Shdr*>(elfData + ehdr->e_shoff);

    // Calculate total memory needed for loadable sections
    size_t totalSize = 0;
    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        if (shdr[i].sh_flags & SHF_ALLOC)
        {
            // Align section
            size_t align = shdr[i].sh_addralign > 0 ? shdr[i].sh_addralign : 1;
            totalSize = (totalSize + align - 1) & ~(align - 1);
            totalSize += shdr[i].sh_size;
        }
    }

    // Add space for veneers (trampolines) for long jumps to kernel functions
    size_t veneerAreaOffset = totalSize;
    totalSize += MAX_VENEERS * sizeof(ThumbVeneer);

    // Allocate memory for module from SDRAM (large memory pool)
    uint8_t* moduleBase = static_cast<uint8_t*>(MEM_ALLOC_LARGE(totalSize));
    if (moduleBase == nullptr)
    {
        PRINTF("ModuleLoader: Failed to allocate %u bytes for module\r\n", (unsigned)totalSize);
        PRINTF("ModuleLoader: MEM_LARGE flag = 0x%08X\r\n", (unsigned)Memory::MEM_LARGE);
        return ModuleResult::ERROR_NO_MEMORY;
    }
    PRINTF("ModuleLoader: Allocated %u bytes at %p\r\n", (unsigned)totalSize, moduleBase);
    
    // CRITICAL: Invalidate cache for newly allocated region BEFORE writing
    // This ensures no stale cache lines exist for this memory region
    uint32_t alignedTotalSize = (totalSize + 31) & ~31;
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(moduleBase), 
                                  static_cast<int32_t>(alignedTotalSize));
    __DSB();
    
    memset(moduleBase, 0, totalSize);

    // Create section address mapping
    uint32_t* sectionAddrs = static_cast<uint32_t*>(crtos_malloc(ehdr->e_shnum * sizeof(uint32_t)));
    if (sectionAddrs == nullptr)
    {
        PRINTF("ModuleLoader: Failed to allocate sectionAddrs\r\n");
        MEM_FREE(moduleBase);
        return ModuleResult::ERROR_NO_MEMORY;
    }
    memset(sectionAddrs, 0, ehdr->e_shnum * sizeof(uint32_t));

    // Load sections
    size_t currentOffset = 0;
    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        if (shdr[i].sh_flags & SHF_ALLOC)
        {
            // Align
            size_t align = shdr[i].sh_addralign > 0 ? shdr[i].sh_addralign : 1;
            currentOffset = (currentOffset + align - 1) & ~(align - 1);

            sectionAddrs[i] = reinterpret_cast<uint32_t>(moduleBase + currentOffset);

            if (shdr[i].sh_type != SHT_NOBITS)
            {
                // Copy section data
                memcpy(moduleBase + currentOffset, elfData + shdr[i].sh_offset, shdr[i].sh_size);
            }

            currentOffset += shdr[i].sh_size;
        }
    }

    // Find symbol table and string table
    const Elf32_Shdr* symtabShdr = nullptr;
    const char* strtab = nullptr;

    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        if (shdr[i].sh_type == SHT_SYMTAB)
        {
            symtabShdr = &shdr[i];
            strtab = reinterpret_cast<const char*>(elfData + shdr[shdr[i].sh_link].sh_offset);
            break;
        }
    }

    if (symtabShdr == nullptr)
    {
        PRINTF("ModuleLoader: No symbol table found in ELF (checked %d sections)\\r\\n", ehdr->e_shnum);
        crtos_free(sectionAddrs);
        MEM_FREE(moduleBase);
        return ModuleResult::ERROR_SYMBOL_NOT_FOUND;
    }

    const Elf32_Sym* symtab = reinterpret_cast<const Elf32_Sym*>(elfData + symtabShdr->sh_offset);
    size_t symCount = symtabShdr->sh_size / sizeof(Elf32_Sym);

    // Create resolved symbol addresses
    uint32_t* symAddrs = static_cast<uint32_t*>(crtos_malloc(symCount * sizeof(uint32_t)));
    if (symAddrs == nullptr)
    {
        PRINTF("ModuleLoader: Failed to allocate symAddrs\r\n");
        crtos_free(sectionAddrs);
        MEM_FREE(moduleBase);
        return ModuleResult::ERROR_NO_MEMORY;
    }

    // Resolve all symbols
    for (size_t i = 0; i < symCount; i++)
    {
        const Elf32_Sym* sym = &symtab[i];

        if (sym->st_shndx == SHN_UNDEF)
        {
            // Undefined symbol - look up in kernel symbols
            if (sym->st_name != 0)
            {
                const char* symName = strtab + sym->st_name;
                void* addr = resolveSymbol(symName);
                if (addr != nullptr)
                {
                    symAddrs[i] = reinterpret_cast<uint32_t>(addr);

                    // if ((symName[0] == 'h' && symName[1] == 'a' && symName[2] == 'l' && symName[3] == '_') ||
                    //     (symName[0] == 'c' && symName[1] == 'r' && symName[2] == 't' && symName[3] == 'o' && symName[4] == 's'))
                    // {
                    //     PRINTF("ModuleLoader: Resolved %s -> 0x%08X\r\n", symName, symAddrs[i]);
                    // }
                }
                else
                {
                    PRINTF("ModuleLoader: Unresolved symbol: %s\r\n", symName);
                    symAddrs[i] = 0;
                }
            }
            else
            {
                symAddrs[i] = 0;
            }
        }
        else if (sym->st_shndx == SHN_ABS)
        {
            // Absolute symbol
            symAddrs[i] = sym->st_value;
        }
        else if (sym->st_shndx < ehdr->e_shnum)
        {
            // Symbol in a section
            symAddrs[i] = sectionAddrs[sym->st_shndx] + sym->st_value;
        }
        else
        {
            symAddrs[i] = 0;
        }
    }

    // Build GOT (Global Offset Table) for R_ARM_GOT_BREL relocations
    // First pass: count unique GOT entries needed
    size_t gotEntryCount = 0;
    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        if (shdr[i].sh_type == SHT_REL)
        {
            const Elf32_Rel* rel = reinterpret_cast<const Elf32_Rel*>(elfData + shdr[i].sh_offset);
            size_t relCount = shdr[i].sh_size / sizeof(Elf32_Rel);
            for (size_t j = 0; j < relCount; j++)
            {
                if (ELF32_R_TYPE(rel[j].r_info) == R_ARM_GOT_BREL)
                {
                    uint32_t symIdx = ELF32_R_SYM(rel[j].r_info);
                    if (symIdx >= gotEntryCount)
                    {
                        gotEntryCount = symIdx + 1;
                    }
                }
            }
        }
    }

    // Allocate GOT (one entry per symbol that needs it)
    uint32_t* got = nullptr;
    uint32_t gotBase = 0;
    if (gotEntryCount > 0)
    {
        got = static_cast<uint32_t*>(MEM_ALLOC_LARGE(gotEntryCount * sizeof(uint32_t)));
        if (got == nullptr)
        {
            PRINTF("ModuleLoader: Failed to allocate GOT (%u entries)\r\n", (unsigned)gotEntryCount);
            crtos_free(symAddrs);
            crtos_free(sectionAddrs);
            MEM_FREE(moduleBase);
            return ModuleResult::ERROR_NO_MEMORY;
        }
        gotBase = reinterpret_cast<uint32_t>(got);
        
        // Fill GOT with symbol addresses
        for (size_t i = 0; i < gotEntryCount && i < symCount; i++)
        {
            got[i] = symAddrs[i];
        }
        PRINTF("ModuleLoader: GOT allocated at 0x%08X with %u entries\r\n", gotBase, (unsigned)gotEntryCount);
    }

    // Setup veneer area for long jumps
    ThumbVeneer* veneerArea = reinterpret_cast<ThumbVeneer*>(moduleBase + veneerAreaOffset);
    size_t veneerCount = 0;
    
    // Helper lambda to create a veneer for a given target address
    auto createVeneer = [&](uint32_t targetAddr) -> uint32_t {
        if (veneerCount >= MAX_VENEERS) {
            PRINTF("ModuleLoader: Too many veneers needed!\r\n");
            return 0;
        }
        ThumbVeneer* v = &veneerArea[veneerCount++];
        // LDR.W PC, [PC, #0] = 0xF8DF 0xF000
        v->ldr_pc = 0xF8DF;
        v->ldr_pc2 = 0xF000;
        v->target = targetAddr | 1;  // Set Thumb bit
        // Return address of veneer with Thumb bit
        return reinterpret_cast<uint32_t>(v) | 1;
    };

    // Process relocations
    for (int i = 0; i < ehdr->e_shnum; i++)
    {
        if (shdr[i].sh_type == SHT_REL)
        {
            // Find target section
            uint32_t targetSection = shdr[i].sh_info;
            if (sectionAddrs[targetSection] == 0)
            {
                continue; // Target section not loaded
            }

            const Elf32_Rel* rel = reinterpret_cast<const Elf32_Rel*>(elfData + shdr[i].sh_offset);
            size_t relCount = shdr[i].sh_size / sizeof(Elf32_Rel);

            for (size_t j = 0; j < relCount; j++)
            {
                uint32_t symIdx = ELF32_R_SYM(rel[j].r_info);
                uint32_t relType = ELF32_R_TYPE(rel[j].r_info);
                uint32_t* target = reinterpret_cast<uint32_t*>(sectionAddrs[targetSection] + rel[j].r_offset);
                uint32_t symAddr = symAddrs[symIdx];

                switch (relType)
                {
                    case R_ARM_NONE:
                        break;

                    case R_ARM_ABS32:
                    case R_ARM_TARGET1:
                        *target += symAddr;
                        break;

                    case R_ARM_REL32:
                        *target += symAddr - reinterpret_cast<uint32_t>(target);
                        break;

                    case R_ARM_THM_CALL:
                    case R_ARM_THM_JUMP24:
                    {
                        // Thumb branch relocation
                        // Calculate offset from instruction to target
                        int32_t branchOffset = static_cast<int32_t>(symAddr) - 
                                               static_cast<int32_t>(reinterpret_cast<uint32_t>(target) + 4);
                        
                        // Check if offset is within ±16MB range for BL instruction
                        // BL can reach ±16MB (24-bit signed offset * 2)
                        const int32_t MAX_BRANCH_OFFSET = 0x00FFFFFE;  // ~16MB
                        const int32_t MIN_BRANCH_OFFSET = -0x01000000; // -16MB
                        
                        uint32_t actualTarget = symAddr;
                        
                        if (branchOffset > MAX_BRANCH_OFFSET || branchOffset < MIN_BRANCH_OFFSET)
                        {
                            // Need a veneer (trampoline) for long jump
                            uint32_t veneerAddr = createVeneer(symAddr);
                            if (veneerAddr == 0) {
                                PRINTF("ModuleLoader: Failed to create veneer for %08X\r\n", symAddr);
                                break;
                            }
                            actualTarget = veneerAddr & ~1;  // Remove Thumb bit for offset calculation
                            branchOffset = static_cast<int32_t>(actualTarget) - 
                                          static_cast<int32_t>(reinterpret_cast<uint32_t>(target) + 4);
                        }
                        
                        // Encode BL/BLX instruction
                        uint16_t* hw = reinterpret_cast<uint16_t*>(target);
                        uint32_t S = (branchOffset >> 24) & 1;
                        uint32_t I1 = (branchOffset >> 23) & 1;
                        uint32_t I2 = (branchOffset >> 22) & 1;
                        uint32_t imm10 = (branchOffset >> 12) & 0x3FF;
                        uint32_t imm11 = (branchOffset >> 1) & 0x7FF;
                        
                        // J1 = NOT(I1 XOR S), J2 = NOT(I2 XOR S)
                        uint32_t J1 = ((I1 ^ S) ^ 1) & 1;
                        uint32_t J2 = ((I2 ^ S) ^ 1) & 1;
                        
                        hw[0] = 0xF000 | (S << 10) | imm10;
                        hw[1] = 0xD000 | (J1 << 13) | (J2 << 11) | imm11;  // BL
                        break;
                    }

                    case R_ARM_V4BX:
                        // BX Rm -> MOV PC, Rm for ARMv4 compatibility - ignore
                        break;

                    case R_ARM_PREL31:
                        *target = ((*target) + symAddr - reinterpret_cast<uint32_t>(target)) & 0x7FFFFFFF;
                        break;

                    case R_ARM_THM_MOVW_ABS_NC:
                    {
                        uint16_t* hw = reinterpret_cast<uint16_t*>(target);
                        uint32_t val = (hw[0] & 0x000F) << 12 | (hw[0] & 0x0400) << 1 |
                                       (hw[1] & 0x7000) >> 4 | (hw[1] & 0x00FF);
                        val += symAddr;
                        hw[0] = (hw[0] & 0xFBF0) | ((val >> 12) & 0x000F) | ((val >> 1) & 0x0400);
                        hw[1] = (hw[1] & 0x8F00) | ((val << 4) & 0x7000) | (val & 0x00FF);
                        break;
                    }

                    case R_ARM_THM_MOVT_ABS:
                    {
                        uint16_t* hw = reinterpret_cast<uint16_t*>(target);
                        uint32_t val = (hw[0] & 0x000F) << 12 | (hw[0] & 0x0400) << 1 |
                                       (hw[1] & 0x7000) >> 4 | (hw[1] & 0x00FF);
                        val += symAddr >> 16;
                        hw[0] = (hw[0] & 0xFBF0) | ((val >> 12) & 0x000F) | ((val >> 1) & 0x0400);
                        hw[1] = (hw[1] & 0x8F00) | ((val << 4) & 0x7000) | (val & 0x00FF);
                        break;
                    }

                    case R_ARM_GOT_BREL:
                    {
                        // GOT-relative relocation: S + A - GOT_ORG
                        // The instruction loads from GOT[symIdx], so we store offset to GOT entry
                        if (got != nullptr && symIdx < gotEntryCount)
                        {
                            uint32_t gotEntryAddr = gotBase + (symIdx * sizeof(uint32_t));
                            *target = gotEntryAddr - gotBase + *target;
                        }
                        else
                        {
                            // Fallback: store symbol address directly
                            *target += symAddr;
                        }
                        break;
                    }

                    case R_ARM_CALL:
                    case R_ARM_JUMP24:
                    {
                        // ARM branch relocation (BL/B instruction)
                        int32_t offset = (*target & 0x00FFFFFF) << 2;
                        if (offset & 0x02000000) offset |= 0xFC000000; // Sign extend
                        offset += symAddr - (reinterpret_cast<uint32_t>(target) + 8);
                        *target = (*target & 0xFF000000) | ((offset >> 2) & 0x00FFFFFF);
                        break;
                    }

                    case R_ARM_MOVW_ABS_NC:
                    {
                        // ARM MOVW instruction (lower 16 bits)
                        uint32_t insn = *target;
                        uint32_t val = ((insn & 0xF0000) >> 4) | (insn & 0xFFF);
                        val += symAddr & 0xFFFF;
                        *target = (insn & 0xFFF0F000) | ((val & 0xF000) << 4) | (val & 0xFFF);
                        break;
                    }

                    case R_ARM_MOVT_ABS:
                    {
                        // ARM MOVT instruction (upper 16 bits)
                        uint32_t insn = *target;
                        uint32_t val = ((insn & 0xF0000) >> 4) | (insn & 0xFFF);
                        val += symAddr >> 16;
                        *target = (insn & 0xFFF0F000) | ((val & 0xF000) << 4) | (val & 0xFFF);
                        break;
                    }

                    // =========================================================
                    // PIC/GOT/PLT relocation types for shared object modules
                    // =========================================================
                    
                    case R_ARM_RELATIVE:
                    {
                        // R_ARM_RELATIVE: B(S) + A
                        // Used in .rel.dyn for data pointers that need base adjustment
                        // The addend is stored at the target location
                        uint32_t addend = *target;
                        *target = reinterpret_cast<uint32_t>(moduleBase) + addend;
                        break;
                    }

                    case R_ARM_GLOB_DAT:
                    {
                        // R_ARM_GLOB_DAT: S
                        // Used to initialize GOT entries with symbol addresses
                        *target = symAddr;
                        break;
                    }

                    case R_ARM_JUMP_SLOT:
                    {
                        // R_ARM_JUMP_SLOT: S
                        // Used for PLT entries (lazy binding, but we resolve immediately)
                        *target = symAddr;
                        break;
                    }

                    case R_ARM_GOTOFF32:
                    {
                        // R_ARM_GOTOFF32: S + A - GOT_ORG
                        // Offset from GOT base to symbol
                        *target = symAddr + *target - gotBase;
                        break;
                    }

                    case R_ARM_BASE_PREL:
                    {
                        // R_ARM_BASE_PREL: B(S) + A - P
                        // PC-relative offset to segment base
                        *target = reinterpret_cast<uint32_t>(moduleBase) + *target - 
                                  reinterpret_cast<uint32_t>(target);
                        break;
                    }

                    case R_ARM_GOT_PREL:
                    {
                        // R_ARM_GOT_PREL: GOT(S) + A - P
                        // PC-relative offset to GOT entry for symbol
                        if (got != nullptr && symIdx < gotEntryCount)
                        {
                            uint32_t gotEntryAddr = gotBase + (symIdx * sizeof(uint32_t));
                            *target = gotEntryAddr + *target - reinterpret_cast<uint32_t>(target);
                        }
                        else
                        {
                            PRINTF("ModuleLoader: R_ARM_GOT_PREL without GOT for sym %lu\r\n", symIdx);
                        }
                        break;
                    }

                    case R_ARM_PLT32:
                    {
                        // R_ARM_PLT32: ((S + A) | T) - P
                        // PC-relative call through PLT (we resolve directly)
                        int32_t offset = static_cast<int32_t>(symAddr) - 
                                        static_cast<int32_t>(reinterpret_cast<uint32_t>(target));
                        *target = (*target & 0xFF000000) | ((offset >> 2) & 0x00FFFFFF);
                        break;
                    }

                    case R_ARM_THM_MOVW_PREL_NC:
                    {
                        // Thumb MOVW PC-relative (lower 16 bits)
                        uint16_t* hw = reinterpret_cast<uint16_t*>(target);
                        uint32_t val = (hw[0] & 0x000F) << 12 | (hw[0] & 0x0400) << 1 |
                                       (hw[1] & 0x7000) >> 4 | (hw[1] & 0x00FF);
                        val += (symAddr - reinterpret_cast<uint32_t>(target)) & 0xFFFF;
                        hw[0] = (hw[0] & 0xFBF0) | ((val >> 12) & 0x000F) | ((val >> 1) & 0x0400);
                        hw[1] = (hw[1] & 0x8F00) | ((val << 4) & 0x7000) | (val & 0x00FF);
                        break;
                    }

                    case R_ARM_THM_MOVT_PREL:
                    {
                        // Thumb MOVT PC-relative (upper 16 bits)
                        uint16_t* hw = reinterpret_cast<uint16_t*>(target);
                        uint32_t val = (hw[0] & 0x000F) << 12 | (hw[0] & 0x0400) << 1 |
                                       (hw[1] & 0x7000) >> 4 | (hw[1] & 0x00FF);
                        val += ((symAddr - reinterpret_cast<uint32_t>(target)) >> 16);
                        hw[0] = (hw[0] & 0xFBF0) | ((val >> 12) & 0x000F) | ((val >> 1) & 0x0400);
                        hw[1] = (hw[1] & 0x8F00) | ((val << 4) & 0x7000) | (val & 0x00FF);
                        break;
                    }

                    case R_ARM_GOT_ABS:
                    {
                        // R_ARM_GOT_ABS: GOT(S) + A
                        // Absolute address of GOT entry
                        if (got != nullptr && symIdx < gotEntryCount)
                        {
                            *target = gotBase + (symIdx * sizeof(uint32_t)) + *target;
                        }
                        break;
                    }

                    case R_ARM_COPY:
                    {
                        // R_ARM_COPY: Copy data from shared object
                        // For kernel modules we don't support this - symbols should be imported
                        PRINTF("ModuleLoader: R_ARM_COPY not supported for kernel modules\r\n");
                        break;
                    }

                    default:
                        PRINTF("ModuleLoader: Unknown relocation type: %lu\r\n", relType);
                        break;
                }
            }
        }
    }

    // Find module init/exit functions
    void* initFunc = nullptr;
    void* exitFunc = nullptr;

    for (size_t i = 0; i < symCount; i++)
    {
        if (symtab[i].st_name != 0)
        {
            const char* symName = strtab + symtab[i].st_name;
            
            if (strcmp(symName, "driver_module_init") == 0)
            {
                initFunc = reinterpret_cast<void*>(symAddrs[i]);
            }
            else if (strcmp(symName, "driver_module_exit") == 0)
            {
                exitFunc = reinterpret_cast<void*>(symAddrs[i]);
            }
        }
    }

    crtos_free(symAddrs);
    crtos_free(sectionAddrs);

    if (initFunc == nullptr)
    {
        if (got != nullptr) MEM_FREE(got);
        MEM_FREE(moduleBase);
        return ModuleResult::ERROR_NO_INIT_FUNC;
    }

    // Synchronize caches - CRITICAL for code and data access from SDRAM
    // Align size to 32 bytes for cache operations
    uint32_t alignedModuleSize = (totalSize + 31) & ~31;
    
    // Clean AND Invalidate D-Cache to ensure all module data is written to SDRAM
    // and any stale cache lines are invalidated
    SCB_CleanInvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(moduleBase), 
                                       static_cast<int32_t>(alignedModuleSize));
    
    // If GOT was allocated separately, also clean/invalidate it
    if (got != nullptr)
    {
        uint32_t gotTotalSize = ((gotEntryCount * sizeof(uint32_t)) + 31) & ~31;
        SCB_CleanInvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(got), 
                                           static_cast<int32_t>(gotTotalSize));
    }
    
    // Invalidate I-Cache to ensure fresh code is fetched
    SCB_InvalidateICache();
    // Memory barriers
    __DSB();
    __ISB();

    // Call module init with GOT base in r9
    // The module was compiled with -msingle-pic-base which uses r9 as GOT pointer
    typedef Drivers::DriverBase* (*ModuleInitFunc)();
    Drivers::DriverBase* driver = nullptr;
    
    if (got != nullptr)
    {
        // Use inline assembly to ensure r9 is set and preserved during the call
        // Save kernel's r9, set module's GOT base, call function, restore r9
        uint32_t result;
        uint32_t funcAddr = reinterpret_cast<uint32_t>(initFunc);
        __asm__ volatile(
            "mov    r4, r9          \n"  // Save kernel's r9
            "mov    r9, %[got]      \n"  // Set module's GOT base
            "blx    %[func]         \n"  // Call module init function
            "mov    %[result], r0   \n"  // Save result
            "mov    r9, r4          \n"  // Restore kernel's r9
            : [result] "=r" (result)
            : [got] "r" (gotBase), [func] "r" (funcAddr)
            : "r0", "r1", "r2", "r3", "r4", "r12", "lr", "memory", "cc"
        );
        driver = reinterpret_cast<Drivers::DriverBase*>(result);
    }
    else
    {
        driver = reinterpret_cast<ModuleInitFunc>(initFunc)();
    }

    if (driver == nullptr)
    {
        if (got != nullptr) MEM_FREE(got);
        MEM_FREE(moduleBase);
        return ModuleResult::ERROR_INIT_FAILED;
    }

    // Set GOT base in the driver so kernel can set r9 before calling virtual methods
    if (got != nullptr)
    {
        driver->setGotBase(gotBase);
    }

    // Debug: print driver info
    PRINTF("ModuleLoader: Driver instance at %p\r\n", driver);
    uint32_t* vtablePtr = *reinterpret_cast<uint32_t**>(driver);
    PRINTF("ModuleLoader: vtable at 0x%p\r\n", vtablePtr);
    if (vtablePtr != nullptr) {
        PRINTF("ModuleLoader: vtable[0] = %08X\r\n", vtablePtr[0]);
        PRINTF("ModuleLoader: vtable[1] = %08X\r\n", vtablePtr[1]);
        PRINTF("ModuleLoader: vtable[9] = %08X (getInfo)\r\n", vtablePtr[9]);
    }
    
    // Test getInfo before registering - must use CALL_DRIVER_METHOD macro
    const Drivers::DriverInfo* info = CALL_DRIVER_METHOD(driver, getInfo);
    PRINTF("ModuleLoader: getInfo() returned %p\r\n", info);
    if (info != nullptr) {
        PRINTF("ModuleLoader: Driver name: %s\r\n", info->name);
    }

    // Register driver with DriverManager
    Drivers::DriverManager::getInstance().registerDriver(driver, true);

    // Store module info - copy name to module's own buffer
    strncpy(module->name, moduleName, sizeof(module->name) - 1);
    module->name[sizeof(module->name) - 1] = '\0';
    module->baseAddress = moduleBase;
    module->size = totalSize;
    module->initFunc = initFunc;
    module->exitFunc = exitFunc;
    module->driverInstance = driver;
    module->got = got;
    module->gotSize = gotEntryCount * sizeof(uint32_t);
    module->isLoaded = true;

    _moduleCount++;

    PRINTF("ModuleLoader: Loaded module '%s' at %p, size=%u, veneers=%u\r\n", 
           moduleName, moduleBase, (unsigned)totalSize, (unsigned)veneerCount);

    return ModuleResult::SUCCESS;
}

ModuleResult ModuleLoader::loadModuleFromFile(const char* filePath)
{
    if (!_initialized)
    {
        init();
    }

    // Open file
    FIL file;
    FRESULT fr = f_open(&file, filePath, FA_READ);
    if (fr != FR_OK)
    {
        PRINTF("ModuleLoader: Failed to open file '%s' (error: %d)\r\n", filePath, (int)fr);
        return ModuleResult::ERROR_FILE_READ;
    }

    // Get file size
    FSIZE_t fileSize = f_size(&file);
    if (fileSize == 0 || fileSize > 512 * 1024)  // Max 512KB for a module
    {
        f_close(&file);
        PRINTF("ModuleLoader: Invalid file size: %lu\r\n", (unsigned long)fileSize);
        return ModuleResult::ERROR_FILE_READ;
    }

    // Allocate buffer for ELF data from SDRAM (large memory pool)
    // Use 32-byte alignment for cache operations
    size_t alignedSize = (fileSize + 31) & ~31;  // Round up to 32-byte boundary
    uint8_t* elfData = static_cast<uint8_t*>(HeapAllocator::Allocate(alignedSize, Memory::AllocHints::Aligned(32)));
    if (elfData == nullptr)
    {
        // Fallback to regular large allocation
        elfData = static_cast<uint8_t*>(MEM_ALLOC_LARGE(alignedSize));
    }
    if (elfData == nullptr)
    {
        f_close(&file);
        PRINTF("ModuleLoader: Failed to allocate %lu bytes for ELF buffer\\r\\n", (unsigned long)fileSize);
        return ModuleResult::ERROR_NO_MEMORY;
    }

    // CRITICAL: Invalidate cache for buffer BEFORE reading from SD card
    // This is needed because SD card driver may use DMA which bypasses CPU cache
    SCB_InvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(elfData), static_cast<int32_t>(alignedSize));
    __DSB();

    // Read file contents
    UINT bytesRead = 0;
    fr = f_read(&file, elfData, static_cast<UINT>(fileSize), &bytesRead);
    f_close(&file);

    if (fr != FR_OK || bytesRead != fileSize)
    {
        MEM_FREE(elfData);
        PRINTF("ModuleLoader: Failed to read file (error: %d, read: %u/%lu)\\r\\n", 
               (int)fr, bytesRead, (unsigned long)fileSize);
        return ModuleResult::ERROR_FILE_READ;
    }

    // Ensure cache coherency for ELF buffer
    // Use CleanInvalidate to handle both CPU-written and DMA-written scenarios
    // SCB_CleanInvalidateDCache_by_Addr requires 32-byte aligned address and size
    SCB_CleanInvalidateDCache_by_Addr(reinterpret_cast<uint32_t*>(elfData), static_cast<int32_t>(alignedSize));
    __DSB();
    __ISB();

    // Extract module name from path (e.g., "/drivers/uart.ko" -> "uart")
    const char* baseName = filePath;
    const char* p = filePath;
    while (*p)
    {
        if (*p == '/' || *p == '\\')
        {
            baseName = p + 1;
        }
        p++;
    }

    // Remove .ko extension if present
    static char moduleName[64];
    strncpy(moduleName, baseName, sizeof(moduleName) - 1);
    moduleName[sizeof(moduleName) - 1] = '\0';
    
    size_t len = strlen(moduleName);
    if (len > 3 && strcmp(moduleName + len - 3, ".ko") == 0)
    {
        moduleName[len - 3] = '\0';
    }

    // Load module from memory
    ModuleResult result = loadModule(elfData, static_cast<size_t>(fileSize), moduleName);

    // Free ELF buffer (module code has been copied to allocated memory)
    MEM_FREE(elfData);

    return result;
}

ModuleResult ModuleLoader::unloadModule(const char* moduleName)
{
    LoadedModule* module = findModule(moduleName);
    if (module == nullptr)
    {
        return ModuleResult::ERROR_SYMBOL_NOT_FOUND;
    }

    // Unregister driver
    if (module->driverInstance != nullptr)
    {
        Drivers::DriverManager::getInstance().unregisterDriver(
            static_cast<Drivers::DriverBase*>(module->driverInstance)->getName()
        );
    }

    // Call exit function
    if (module->exitFunc != nullptr)
    {
        typedef void (*ModuleExitFunc)(Drivers::DriverBase*);
        reinterpret_cast<ModuleExitFunc>(module->exitFunc)(
            static_cast<Drivers::DriverBase*>(module->driverInstance)
        );
    }

    // Free GOT if allocated
    if (module->got != nullptr)
    {
        MEM_FREE(module->got);
    }

    // Free module memory
    if (module->baseAddress != nullptr)
    {
        MEM_FREE(module->baseAddress);
    }

    // Clear module entry
    memset(module, 0, sizeof(LoadedModule));
    _moduleCount--;

    PRINTF("ModuleLoader: Unloaded module '%s'\r\n", moduleName);

    return ModuleResult::SUCCESS;
}

LoadedModule* ModuleLoader::findModule(const char* moduleName)
{
    for (size_t i = 0; i < MAX_MODULES; i++)
    {
        if (_modules[i].isLoaded && strcmp(_modules[i].name, moduleName) == 0)
        {
            return &_modules[i];
        }
    }
    return nullptr;
}

void* ModuleLoader::getDriverInstance(const char* moduleName)
{
    LoadedModule* module = findModule(moduleName);
    if (module != nullptr)
    {
        return module->driverInstance;
    }
    return nullptr;
}

void ModuleLoader::listModules()
{
    PRINTF("=== Loaded Modules (%u) ===\r\n", (unsigned)_moduleCount);
    
    for (size_t i = 0; i < MAX_MODULES; i++)
    {
        if (_modules[i].isLoaded)
        {
            PRINTF("  [%u] %s @ %p (size: %u)\r\n",
                   (unsigned)i,
                   _modules[i].name,
                   _modules[i].baseAddress,
                   (unsigned)_modules[i].size);
        }
    }
}

size_t ModuleLoader::loadModulesFromDirectory(const char* directoryPath)
{
    if (!_initialized)
    {
        init();
    }

    PRINTF("ModuleLoader: Scanning directory '%s' for modules...\r\n", directoryPath);

    size_t loadedCount = 0;
    DIR dir;
    FILINFO fno;

    // Open directory
    FRESULT fr = f_opendir(&dir, directoryPath);
    if (fr != FR_OK)
    {
        PRINTF("ModuleLoader: Failed to open directory '%s' (error: %d)\r\n", directoryPath, (int)fr);
        return 0;
    }

    // Iterate through directory entries
    while (true)
    {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0)
        {
            break;  // End of directory or error
        }

        // Skip directories
        if (fno.fattrib & AM_DIR)
        {
            continue;
        }

        // Check for .ko extension
        size_t nameLen = strlen(fno.fname);
        if (nameLen > 3 && strcmp(fno.fname + nameLen - 3, ".ko") == 0)
        {
            // Build full path
            char fullPath[256];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", directoryPath, fno.fname);

            PRINTF("ModuleLoader: Found module '%s'\r\n", fno.fname);

            // Load module
            ModuleResult result = loadModuleFromFile(fullPath);
            if (result == ModuleResult::SUCCESS)
            {
                loadedCount++;
                PRINTF("ModuleLoader: Successfully loaded '%s'\r\n", fno.fname);
            }
            else
            {
                PRINTF("ModuleLoader: Failed to load '%s' (error: %d)\r\n", fno.fname, (int)result);
            }
        }
    }

    f_closedir(&dir);

    PRINTF("ModuleLoader: Loaded %u module(s) from '%s'\r\n", (unsigned)loadedCount, directoryPath);

    return loadedCount;
}

} // namespace CRTOS
