/*
 * AppLoader.cpp - CRTOS Dynamic Application Loader Implementation
 * Author: Arkadiusz Szlanta
 * Date: 05 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "AppLoader.hpp"
#include "CRTOS.hpp"
#include "ModuleLoader.hpp"
#include "HeapAllocator.hpp"
#include "Task.hpp"
#include "Drivers/DriverManager.hpp"
#include "fsl_debug_console.h"
#include "fsl_cache.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>

// FatFS for file operations
extern "C" {
#include "ff.h"
}

// External kernel functions
extern uint32_t GetSystemTime(void);

// Memory allocation macros - use HeapAllocator from header
#define MEM_ALLOC(size)       HeapAllocator::Allocate(size)
#define MEM_FREE(ptr)         HeapAllocator::Free(ptr)

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
#define ELFDATA2LSB     1

#define ET_REL          1
#define ET_EXEC         2
#define ET_DYN          3
#define EM_ARM          40

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

// ARM relocation types
#define R_ARM_NONE            0
#define R_ARM_PC24            1
#define R_ARM_ABS32           2
#define R_ARM_REL32           3
#define R_ARM_THM_CALL        10
#define R_ARM_COPY            20
#define R_ARM_GLOB_DAT        21
#define R_ARM_JUMP_SLOT       22
#define R_ARM_RELATIVE        23
#define R_ARM_GOTOFF32        24
#define R_ARM_BASE_PREL       25
#define R_ARM_GOT_BREL        26
#define R_ARM_PLT32           27
#define R_ARM_CALL            28
#define R_ARM_JUMP24          29
#define R_ARM_THM_JUMP24      30
#define R_ARM_TARGET1         38
#define R_ARM_V4BX            40
#define R_ARM_PREL31          42
#define R_ARM_MOVW_ABS_NC     43
#define R_ARM_MOVT_ABS        44
#define R_ARM_THM_MOVW_ABS_NC 47
#define R_ARM_THM_MOVT_ABS    48
#define R_ARM_THM_MOVW_PREL_NC 49
#define R_ARM_THM_MOVT_PREL   50
#define R_ARM_GOT_ABS         95
#define R_ARM_GOT_PREL        96

// ELF structures
struct Elf32_Ehdr_App {
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

struct Elf32_Shdr_App {
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

struct Elf32_Sym_App {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
};

struct Elf32_Rel_App {
    uint32_t r_offset;
    uint32_t r_info;
};

#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((i) & 0xff)
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)

namespace CRTOS
{

// ============================================================================
// AppAPI Implementation - All functions use syscalls for USER mode safety
// ============================================================================

// Syscall numbers from SystemCall.hpp
#define SYS_EXIT            100
#define SYS_YIELD           102
#define SYS_SLEEP           103
#define SYS_MALLOC          111
#define SYS_FREE            112
#define SYS_WRITE           123
#define SYS_GET_TICK        150

#define STDOUT_FD           1

static void app_yield()
{
    __asm__ volatile(
        "svc %[svc_num]"
        :
        : [svc_num] "I" (SYS_YIELD)
        : "r0", "r1", "r2", "r3", "memory"
    );
}

static void app_delay(uint32_t ms)
{
    register uint32_t ticks __asm__("r0") = ms;
    
    __asm__ volatile(
        "svc %[svc_num]"
        :
        : "r" (ticks), [svc_num] "I" (SYS_SLEEP)
        : "r1", "r2", "r3", "memory"
    );
}

static uint32_t app_get_tick_count()
{
    register uint32_t result __asm__("r0");
    
    __asm__ volatile(
        "svc %[svc_num]"
        : "=r" (result)
        : [svc_num] "I" (SYS_GET_TICK)
        : "r1", "r2", "r3", "memory"
    );
    
    return result;
}

static void app_exit(int exitCode)
{
    register int32_t code __asm__("r0") = exitCode;
    
    __asm__ volatile(
        "svc %[svc_num]"
        :
        : "r" (code), [svc_num] "I" (SYS_EXIT)
        : "r1", "r2", "r3", "memory"
    );
    
    // Should not return, but just in case
    while(1) {}
}

// Helper to write a string via syscall
static int app_write_string(const char* str, size_t len)
{
    register int32_t result __asm__("r0");
    register int32_t fd __asm__("r0") = STDOUT_FD;
    register const char* buf __asm__("r1") = str;
    register uint32_t count __asm__("r2") = (uint32_t)len;
    
    __asm__ volatile(
        "svc %[svc_num]"
        : "=r" (result)
        : "r" (fd), "r" (buf), "r" (count), [svc_num] "I" (SYS_WRITE)
        : "r3", "memory"
    );
    
    return result;
}

// Simple number to string conversion (for printf implementation)
static char* app_utoa(uint32_t value, char* buf, int base, bool uppercase)
{
    static const char digits_lower[] = "0123456789abcdef";
    static const char digits_upper[] = "0123456789ABCDEF";
    const char* digits = uppercase ? digits_upper : digits_lower;
    
    char* p = buf;
    char* start = buf;
    
    if (value == 0) {
        *p++ = '0';
    } else {
        while (value > 0) {
            *p++ = digits[value % base];
            value /= base;
        }
    }
    *p = '\0';
    
    // Reverse the string
    char* end = p - 1;
    while (start < end) {
        char tmp = *start;
        *start++ = *end;
        *end-- = tmp;
    }
    
    return p;  // Return pointer to end
}

static char* app_itoa(int32_t value, char* buf, int base)
{
    char* p = buf;
    
    if (value < 0 && base == 10) {
        *p++ = '-';
        value = -value;
    }
    
    app_utoa((uint32_t)value, p, base, false);
    
    // Find end
    while (*p) p++;
    return p;
}

static int app_printf(const char* fmt, ...)
{
    char buffer[256];
    char* out = buffer;
    char* end = buffer + sizeof(buffer) - 1;
    
    va_list args;
    va_start(args, fmt);
    
    while (*fmt && out < end)
    {
        if (*fmt != '%')
        {
            *out++ = *fmt++;
            continue;
        }
        
        fmt++;  // Skip '%'
        
        // Handle %%
        if (*fmt == '%')
        {
            *out++ = '%';
            fmt++;
            continue;
        }
        
        // Parse flags
        bool zero_pad = false;
        bool left_align = false;
        
        while (*fmt == '0' || *fmt == '-' || *fmt == ' ' || *fmt == '+')
        {
            if (*fmt == '0') zero_pad = true;
            if (*fmt == '-') left_align = true;
            fmt++;
        }
        
        // Parse width
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
        {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        // Parse length modifier
        bool is_long = false;
        if (*fmt == 'l')
        {
            is_long = true;
            fmt++;
        }
        
        // Parse specifier
        char numBuf[24];
        const char* str = nullptr;
        int len = 0;
        
        switch (*fmt)
        {
            case 'd':
            case 'i':
            {
                int32_t val = is_long ? (int32_t)va_arg(args, long) : va_arg(args, int);
                app_itoa(val, numBuf, 10);
                str = numBuf;
                break;
            }
            
            case 'u':
            {
                uint32_t val = is_long ? (uint32_t)va_arg(args, unsigned long) : va_arg(args, unsigned int);
                app_utoa(val, numBuf, 10, false);
                str = numBuf;
                break;
            }
            
            case 'x':
            {
                uint32_t val = is_long ? (uint32_t)va_arg(args, unsigned long) : va_arg(args, unsigned int);
                app_utoa(val, numBuf, 16, false);
                str = numBuf;
                break;
            }
            
            case 'X':
            {
                uint32_t val = is_long ? (uint32_t)va_arg(args, unsigned long) : va_arg(args, unsigned int);
                app_utoa(val, numBuf, 16, true);
                str = numBuf;
                break;
            }
            
            case 'p':
            {
                void* ptr = va_arg(args, void*);
                numBuf[0] = '0';
                numBuf[1] = 'x';
                app_utoa((uint32_t)(uintptr_t)ptr, numBuf + 2, 16, false);
                str = numBuf;
                break;
            }
            
            case 's':
            {
                str = va_arg(args, const char*);
                if (str == nullptr) str = "(null)";
                break;
            }
            
            case 'c':
            {
                numBuf[0] = (char)va_arg(args, int);
                numBuf[1] = '\0';
                str = numBuf;
                break;
            }
            
            default:
                // Unknown specifier, just output it
                *out++ = '%';
                if (out < end) *out++ = *fmt;
                fmt++;
                continue;
        }
        
        fmt++;
        
        if (str)
        {
            // Calculate string length
            const char* s = str;
            while (*s) { len++; s++; }
            
            // Apply width padding
            int pad = (width > len) ? (width - len) : 0;
            char padChar = zero_pad ? '0' : ' ';
            
            // For hex with 0x prefix, handle specially
            if (zero_pad && str[0] == '0' && str[1] == 'x')
            {
                if (out < end) *out++ = '0';
                if (out < end) *out++ = 'x';
                str += 2;
                len -= 2;
                pad = (width > len + 2) ? (width - len - 2) : 0;
            }
            
            if (!left_align)
            {
                while (pad-- > 0 && out < end) *out++ = padChar;
            }
            
            while (*str && out < end) *out++ = *str++;
            
            if (left_align)
            {
                while (pad-- > 0 && out < end) *out++ = ' ';
            }
        }
    }
    
    va_end(args);
    
    *out = '\0';
    size_t totalLen = out - buffer;
    
    return app_write_string(buffer, totalLen);
}

static int app_getchar()
{
    // Not available in debug console lite
    return -1;
}

static int app_putchar(int c)
{
    // Use SYS_WRITE syscall to write single character to stdout
    char ch = (char)c;
    
    register int32_t result __asm__("r0");
    register int32_t fd __asm__("r0") = STDOUT_FD;
    register const char* buf __asm__("r1") = &ch;
    register uint32_t count __asm__("r2") = 1;
    
    __asm__ volatile(
        "svc %[svc_num]"
        : "=r" (result)
        : "r" (fd), "r" (buf), "r" (count), [svc_num] "I" (SYS_WRITE)
        : "r3", "memory"
    );
    
    return (result > 0) ? c : -1;
}

static void* app_malloc(size_t size)
{
    register void* result __asm__("r0");
    register size_t sz __asm__("r0") = size;
    
    __asm__ volatile(
        "svc %[svc_num]"
        : "=r" (result)
        : "r" (sz), [svc_num] "I" (SYS_MALLOC)
        : "r1", "r2", "r3", "memory"
    );
    
    return result;
}

static void app_free(void* ptr)
{
    register void* p __asm__("r0") = ptr;
    
    __asm__ volatile(
        "svc %[svc_num]"
        :
        : "r" (p), [svc_num] "I" (SYS_FREE)
        : "r1", "r2", "r3", "memory"
    );
}

static void* app_get_driver(const char* name)
{
    return Drivers::getDriver(name);
}

// Global AppAPI instance
static AppAPI s_appAPI;
static bool s_appAPIInitialized = false;

extern "C" const AppAPI* crtos_get_app_api(void)
{
    if (!s_appAPIInitialized)
    {
        return nullptr;
    }
    return &s_appAPI;
}

// ============================================================================
// AppLoader Implementation
// ============================================================================

AppLoader& AppLoader::getInstance()
{
    static AppLoader instance;
    return instance;
}

AppLoader::AppLoader()
    : _appCount(0)
    , _initialized(false)
{
    memset(&_appAPI, 0, sizeof(_appAPI));
    memset(_apps, 0, sizeof(_apps));
}

AppLoader::~AppLoader()
{
    // Unload all applications
    for (size_t i = 0; i < MAX_APPS; i++)
    {
        if (_apps[i].isLoaded)
        {
            unloadApp(_apps[i].name);
        }
    }
}

AppResult AppLoader::init()
{
    if (_initialized)
    {
        return AppResult::SUCCESS;
    }

    // Initialize AppAPI
    _appAPI.version         = 0x00010000; // v1.0.0
    _appAPI.yield           = app_yield;
    _appAPI.delay           = app_delay;
    _appAPI.get_tick_count  = app_get_tick_count;
    _appAPI.exit            = app_exit;
    _appAPI.printf          = app_printf;
    _appAPI.getchar         = app_getchar;
    _appAPI.putchar         = app_putchar;
    _appAPI.malloc          = app_malloc;
    _appAPI.free            = app_free;
    _appAPI.get_driver      = app_get_driver;

    // Copy to global
    memcpy(&s_appAPI, &_appAPI, sizeof(AppAPI));
    s_appAPIInitialized = true;

    // Register AppAPI accessor in ModuleLoader's symbol table
    ModuleLoader& ml = ModuleLoader::getInstance();
    ml.registerSymbol("crtos_get_app_api", (void*)&crtos_get_app_api);

    PRINTF("AppLoader: Initialized, API version 0x%08lX\r\n", _appAPI.version);

    _initialized = true;
    return AppResult::SUCCESS;
}

bool AppLoader::validateElf(const uint8_t* elfData, size_t elfSize)
{
    if (elfData == nullptr || elfSize < sizeof(Elf32_Ehdr_App))
    {
        return false;
    }

    const Elf32_Ehdr_App* ehdr = reinterpret_cast<const Elf32_Ehdr_App*>(elfData);

    // Check magic
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3)
    {
        PRINTF("AppLoader: Invalid ELF magic\r\n");
        return false;
    }

    // Check class (32-bit)
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32)
    {
        PRINTF("AppLoader: Not 32-bit ELF\r\n");
        return false;
    }

    // Check endianness (little endian)
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB)
    {
        PRINTF("AppLoader: Not little-endian\r\n");
        return false;
    }

    // Check type (relocatable, executable, or shared)
    // We accept ET_EXEC with --emit-relocs for PIC applications
    if (ehdr->e_type != ET_REL && ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN)
    {
        PRINTF("AppLoader: Invalid ELF type (type=%u)\r\n", ehdr->e_type);
        return false;
    }

    // Check machine (ARM)
    if (ehdr->e_machine != EM_ARM)
    {
        PRINTF("AppLoader: Not ARM (machine=%u)\r\n", ehdr->e_machine);
        return false;
    }

    return true;
}

AppResult AppLoader::loadApp(const uint8_t* elfData, size_t elfSize, const char* appName)
{
    if (!_initialized)
    {
        init();
    }

    if (elfData == nullptr || appName == nullptr)
    {
        return AppResult::ERROR_INVALID_ELF;
    }

    // Check if already loaded
    if (findApp(appName) != nullptr)
    {
        PRINTF("AppLoader: App '%s' already loaded\r\n", appName);
        return AppResult::ERROR_ALREADY_LOADED;
    }

    // Find free slot
    LoadedApp* app = nullptr;
    for (size_t i = 0; i < MAX_APPS; i++)
    {
        if (!_apps[i].isLoaded)
        {
            app = &_apps[i];
            break;
        }
    }

    if (app == nullptr)
    {
        PRINTF("AppLoader: No free app slots\r\n");
        return AppResult::ERROR_NO_MEMORY;
    }

    // Validate ELF
    if (!validateElf(elfData, elfSize))
    {
        return AppResult::ERROR_INVALID_ELF;
    }

    // Initialize app structure
    memset(app, 0, sizeof(LoadedApp));
    strncpy(app->name, appName, sizeof(app->name) - 1);

    // Load sections
    AppResult result = loadElfSections(app, elfData, elfSize);
    if (result != AppResult::SUCCESS)
    {
        return result;
    }

    // Process relocations
    result = processRelocations(app, elfData, elfSize);
    if (result != AppResult::SUCCESS)
    {
        MEM_FREE(app->baseAddress);
        if (app->got) MEM_FREE(app->got);
        return result;
    }

    app->isLoaded = true;
    _appCount++;

    PRINTF("AppLoader: Loaded '%s' at 0x%08lX, size=%u, GOT=0x%08lX\r\n",
           appName, (uint32_t)app->baseAddress, (unsigned)app->size, app->gotBase);

    return AppResult::SUCCESS;
}

AppResult AppLoader::loadElfSections(LoadedApp* app, const uint8_t* elfData, size_t elfSize)
{
    const Elf32_Ehdr_App* ehdr = reinterpret_cast<const Elf32_Ehdr_App*>(elfData);
    const bool isExecutable = (ehdr->e_type == ET_EXEC);

    // Get section headers
    const Elf32_Shdr_App* shdrs = reinterpret_cast<const Elf32_Shdr_App*>(elfData + ehdr->e_shoff);
    const uint16_t shnum = ehdr->e_shnum;
    const uint16_t shstrndx = ehdr->e_shstrndx;

    // Get section name string table
    const char* shstrtab = nullptr;
    if (shstrndx < shnum)
    {
        shstrtab = reinterpret_cast<const char*>(elfData + shdrs[shstrndx].sh_offset);
    }

    // For ET_EXEC: sections have absolute addresses starting from 0
    // We need to find the address range (min to max+size) for allocation
    uint32_t minAddr = UINT32_MAX;
    uint32_t maxAddr = 0;
    size_t gotEntryCount = 0;
    uint32_t gotSectionAddr = 0;
    uint32_t gotSectionSize = 0;

    // First pass: find address range and GOT info
    for (uint16_t i = 0; i < shnum; i++)
    {
        const Elf32_Shdr_App* shdr = &shdrs[i];

        if ((shdr->sh_flags & SHF_ALLOC) && shdr->sh_size > 0)
        {
            if (shdr->sh_addr < minAddr)
            {
                minAddr = shdr->sh_addr;
            }
            uint32_t endAddr = shdr->sh_addr + shdr->sh_size;
            if (endAddr > maxAddr)
            {
                maxAddr = endAddr;
            }

            // Check if this is .got section
            const char* secName = shstrtab ? (shstrtab + shdr->sh_name) : nullptr;
            if (secName && strcmp(secName, ".got") == 0)
            {
                gotSectionAddr = shdr->sh_addr;
                gotSectionSize = shdr->sh_size;
            }
        }

        // Count symbols for GOT estimation (from .symtab)
        if (shdr->sh_type == SHT_SYMTAB)
        {
            size_t symCount = shdr->sh_size / sizeof(Elf32_Sym_App);
            gotEntryCount = symCount;
        }
    }

    // For executable, minAddr should be 0 (from linker script)
    if (isExecutable)
    {
        PRINTF("AppLoader: ET_EXEC address range 0x%08lX - 0x%08lX\r\n", minAddr, maxAddr);
        if (minAddr > 0)
        {
            // If minAddr is not 0, subtract it from all addresses
            maxAddr = maxAddr - minAddr;
        }
    }

    size_t totalSize = maxAddr - (isExecutable ? 0 : minAddr);
    if (totalSize == 0)
    {
        PRINTF("AppLoader: No loadable sections\r\n");
        return AppResult::ERROR_INVALID_ELF;
    }

    // Allocate module memory
    uint8_t* moduleBase = static_cast<uint8_t*>(MEM_ALLOC(totalSize));
    if (moduleBase == nullptr)
    {
        PRINTF("AppLoader: Failed to allocate %u bytes\r\n", (unsigned)totalSize);
        return AppResult::ERROR_NO_MEMORY;
    }
    memset(moduleBase, 0, totalSize);

    app->baseAddress = moduleBase;
    app->size = totalSize;

    // For ET_EXEC with embedded GOT, use the embedded GOT
    if (isExecutable && gotSectionAddr > 0)
    {
        // GOT will be part of the loaded image at its offset
        app->gotBase = reinterpret_cast<uint32_t>(moduleBase + gotSectionAddr - minAddr);
        app->gotSize = gotSectionSize;
        app->got = nullptr; // GOT is embedded, no separate allocation
        PRINTF("AppLoader: Embedded GOT at offset 0x%08lX, size %lu\r\n", gotSectionAddr, gotSectionSize);
    }
    else if (gotEntryCount > 0)
    {
        // Allocate separate GOT for ET_REL
        app->got = MEM_ALLOC(gotEntryCount * sizeof(uint32_t));
        if (app->got == nullptr)
        {
            MEM_FREE(moduleBase);
            return AppResult::ERROR_NO_MEMORY;
        }
        memset(app->got, 0, gotEntryCount * sizeof(uint32_t));
        app->gotSize = gotEntryCount * sizeof(uint32_t);
        app->gotBase = reinterpret_cast<uint32_t>(app->got);
    }

    // Store section addresses for relocation processing
    app->sectionAddrs = static_cast<uint32_t*>(MEM_ALLOC(shnum * sizeof(uint32_t)));
    if (app->sectionAddrs == nullptr)
    {
        MEM_FREE(moduleBase);
        if (app->got) MEM_FREE(app->got);
        return AppResult::ERROR_NO_MEMORY;
    }
    app->sectionCount = shnum;
    memset(app->sectionAddrs, 0, shnum * sizeof(uint32_t));

    // Second pass: copy sections
    for (uint16_t i = 0; i < shnum; i++)
    {
        const Elf32_Shdr_App* shdr = &shdrs[i];

        if ((shdr->sh_flags & SHF_ALLOC) && shdr->sh_size > 0)
        {
            uint32_t offsetInImage = shdr->sh_addr - minAddr;
            uint8_t* destAddr = moduleBase + offsetInImage;
            app->sectionAddrs[i] = reinterpret_cast<uint32_t>(destAddr);

            if (shdr->sh_type != SHT_NOBITS)
            {
                // Copy section data
                memcpy(destAddr, elfData + shdr->sh_offset, shdr->sh_size);
                const char* secName = shstrtab ? (shstrtab + shdr->sh_name) : "";
                PRINTF("AppLoader: Loaded section '%s' at 0x%08lX (size %lu)\r\n",
                       secName, (uint32_t)destAddr, shdr->sh_size);
            }
            // SHT_NOBITS (.bss) is already zeroed
        }
    }

    // For ET_EXEC: entry point comes from ELF header
    if (isExecutable)
    {
        uint32_t entryAddr = reinterpret_cast<uint32_t>(moduleBase) + ehdr->e_entry - minAddr;
        app->entryPoint = reinterpret_cast<AppEntryFunc>(entryAddr | 1); // Thumb bit
        PRINTF("AppLoader: Entry point from ELF header at 0x%08lX\r\n", entryAddr);
    }
    else
    {
        // For ET_REL: find entry point in symbols
        for (uint16_t i = 0; i < shnum; i++)
        {
            const Elf32_Shdr_App* shdr = &shdrs[i];
            if (shdr->sh_type == SHT_SYMTAB)
            {
                const Elf32_Sym_App* syms = reinterpret_cast<const Elf32_Sym_App*>(elfData + shdr->sh_offset);
                size_t symCount = shdr->sh_size / sizeof(Elf32_Sym_App);

                const Elf32_Shdr_App* strtabHdr = &shdrs[shdr->sh_link];
                const char* strtab = reinterpret_cast<const char*>(elfData + strtabHdr->sh_offset);

                for (size_t j = 0; j < symCount; j++)
                {
                    const char* symName = strtab + syms[j].st_name;
                    
                    if (strcmp(symName, "app_main") == 0 || strcmp(symName, "_start") == 0)
                    {
                        uint16_t shndx = syms[j].st_shndx;
                        if (shndx != SHN_UNDEF && shndx < shnum && app->sectionAddrs[shndx] != 0)
                        {
                            uint32_t entryAddr = app->sectionAddrs[shndx] + syms[j].st_value;
                            app->entryPoint = reinterpret_cast<AppEntryFunc>(entryAddr | 1);
                            PRINTF("AppLoader: Entry point '%s' at 0x%08lX\r\n", symName, entryAddr);
                            break;
                        }
                    }
                }
            }
        }
    }

    if (app->entryPoint == nullptr)
    {
        PRINTF("AppLoader: No entry point found\r\n");
        MEM_FREE(app->sectionAddrs);
        app->sectionAddrs = nullptr;
        MEM_FREE(moduleBase);
        if (app->got) MEM_FREE(app->got);
        return AppResult::ERROR_NO_ENTRY;
    }

    return AppResult::SUCCESS;
}

void* AppLoader::resolveSymbol(const char* name)
{
    // Try to find common symbols
    if (strcmp(name, "crtos_get_app_api") == 0)
        return (void*)&crtos_get_app_api;
    
    // Try ModuleLoader's registered symbols
    ModuleLoader& ml = ModuleLoader::getInstance();
    void* sym = ml.lookupSymbol(name);
    if (sym != nullptr)
        return sym;
    
    return nullptr;
}

AppResult AppLoader::processRelocations(LoadedApp* app, const uint8_t* elfData, size_t elfSize)
{
    const Elf32_Ehdr_App* ehdr = reinterpret_cast<const Elf32_Ehdr_App*>(elfData);
    const Elf32_Shdr_App* shdrs = reinterpret_cast<const Elf32_Shdr_App*>(elfData + ehdr->e_shoff);
    const uint16_t shnum = ehdr->e_shnum;
    const bool isExecutable = (ehdr->e_type == ET_EXEC);

    uint8_t* moduleBase = static_cast<uint8_t*>(app->baseAddress);
    uint32_t gotBase = app->gotBase;
    uint32_t* sectionAddrs = app->sectionAddrs;

    // Find min address for ET_EXEC adjustment
    uint32_t minAddr = 0;
    if (isExecutable)
    {
        minAddr = UINT32_MAX;
        for (uint16_t i = 0; i < shnum; i++)
        {
            const Elf32_Shdr_App* shdr = &shdrs[i];
            if ((shdr->sh_flags & SHF_ALLOC) && shdr->sh_size > 0)
            {
                if (shdr->sh_addr < minAddr)
                    minAddr = shdr->sh_addr;
            }
        }
        if (minAddr == UINT32_MAX) minAddr = 0;
    }

    // Build symbol address table
    uint32_t* symAddrs = nullptr;
    const Elf32_Sym_App* symtab = nullptr;
    const char* strtab = nullptr;
    size_t symCount = 0;

    for (uint16_t i = 0; i < shnum; i++)
    {
        const Elf32_Shdr_App* shdr = &shdrs[i];
        if (shdr->sh_type == SHT_SYMTAB)
        {
            symtab = reinterpret_cast<const Elf32_Sym_App*>(elfData + shdr->sh_offset);
            symCount = shdr->sh_size / sizeof(Elf32_Sym_App);

            const Elf32_Shdr_App* strtabHdr = &shdrs[shdr->sh_link];
            strtab = reinterpret_cast<const char*>(elfData + strtabHdr->sh_offset);

            symAddrs = static_cast<uint32_t*>(MEM_ALLOC(symCount * sizeof(uint32_t)));
            if (symAddrs == nullptr)
            {
                return AppResult::ERROR_NO_MEMORY;
            }

            // Resolve all symbols
            for (size_t j = 0; j < symCount; j++)
            {
                const Elf32_Sym_App* sym = &symtab[j];
                uint16_t shndx = sym->st_shndx;

                if (shndx == SHN_UNDEF)
                {
                    // External symbol - resolve from kernel
                    const char* symName = strtab + sym->st_name;
                    if (symName[0] != '\0')
                    {
                        void* addr = resolveSymbol(symName);
                        if (addr != nullptr)
                        {
                            symAddrs[j] = reinterpret_cast<uint32_t>(addr);
                            PRINTF("AppLoader: Resolved '%s' -> 0x%08lX\r\n", symName, symAddrs[j]);
                        }
                        else
                        {
                            PRINTF("AppLoader: WARNING: Unresolved symbol '%s'\r\n", symName);
                            symAddrs[j] = 0;
                        }
                    }
                }
                else if (shndx == SHN_ABS)
                {
                    symAddrs[j] = sym->st_value;
                }
                else if (shndx < shnum && sectionAddrs != nullptr && sectionAddrs[shndx] != 0)
                {
                    // For ET_EXEC: symbol value is already absolute (from linker)
                    // We need to adjust it to the loaded address
                    if (isExecutable)
                    {
                        // sym->st_value is already the absolute address from linker
                        // Add the base offset (moduleBase - minAddr)
                        symAddrs[j] = reinterpret_cast<uint32_t>(moduleBase) + sym->st_value - minAddr;
                    }
                    else
                    {
                        // For ET_REL: symbol value is section-relative
                        symAddrs[j] = sectionAddrs[shndx] + sym->st_value;
                    }
                }
            }
            break;
        }
    }

    // Process relocation sections
    for (uint16_t i = 0; i < shnum; i++)
    {
        const Elf32_Shdr_App* shdr = &shdrs[i];
        if (shdr->sh_type != SHT_REL)
            continue;

        // For ET_EXEC with --emit-relocs, sh_info is the target section
        uint16_t targetSec = shdr->sh_info;
        
        // Calculate relocation target base
        uint32_t relocBase;
        if (isExecutable)
        {
            // For ET_EXEC: relocations are at absolute addresses
            // We apply them to moduleBase + offset
            relocBase = reinterpret_cast<uint32_t>(moduleBase) - minAddr;
        }
        else
        {
            if (targetSec >= shnum || sectionAddrs == nullptr || sectionAddrs[targetSec] == 0)
                continue;
            relocBase = sectionAddrs[targetSec];
        }

        const Elf32_Rel_App* rels = reinterpret_cast<const Elf32_Rel_App*>(elfData + shdr->sh_offset);
        size_t relCount = shdr->sh_size / sizeof(Elf32_Rel_App);

        PRINTF("AppLoader: Processing %u relocations from section %u\r\n", (unsigned)relCount, i);

        for (size_t j = 0; j < relCount; j++)
        {
            uint32_t offset = rels[j].r_offset;
            uint32_t info = rels[j].r_info;
            uint32_t symIdx = ELF32_R_SYM(info);
            uint32_t relType = ELF32_R_TYPE(info);

            uint32_t* target = reinterpret_cast<uint32_t*>(relocBase + offset);
            uint32_t symAddr = (symAddrs && symIdx < symCount) ? symAddrs[symIdx] : 0;
            uint32_t pAddr = reinterpret_cast<uint32_t>(target);

            switch (relType)
            {
                case R_ARM_NONE:
                    break;

                case R_ARM_ABS32:
                case R_ARM_TARGET1:
                    *target = symAddr + *target;
                    break;

                case R_ARM_REL32:
                case R_ARM_PREL31:
                    *target = symAddr + *target - pAddr;
                    break;

                case R_ARM_GOT_BREL:
                    // GOT-relative relocation
                    // For embedded GOT: the value is already the GOT entry offset
                    // We need to write the symbol address to GOT and update the instruction
                    if (gotBase != 0)
                    {
                        // For ET_EXEC: GOT entries need to be filled with resolved addresses
                        // The embedded GOT already has placeholder values
                        // We need to patch them with actual symbol addresses
                        uint32_t currentGotOffset = *target;
                        uint32_t* gotEntry = reinterpret_cast<uint32_t*>(gotBase + currentGotOffset);
                        *gotEntry = symAddr;  // Fill GOT entry with resolved symbol
                        // The instruction already has correct GOT offset, no change needed
                    }
                    break;

                case R_ARM_GOTOFF32:
                    *target = symAddr + *target - gotBase;
                    break;

                case R_ARM_THM_CALL:
                case R_ARM_THM_JUMP24:
                {
                    uint16_t* hw = reinterpret_cast<uint16_t*>(target);
                    uint16_t hi = hw[0];
                    uint16_t lo = hw[1];

                    int32_t s = (hi >> 10) & 1;
                    int32_t j1 = (lo >> 13) & 1;
                    int32_t j2 = (lo >> 11) & 1;
                    int32_t imm10 = hi & 0x3ff;
                    int32_t imm11 = lo & 0x7ff;

                    int32_t i1 = ~(j1 ^ s) & 1;
                    int32_t i2 = ~(j2 ^ s) & 1;

                    int32_t offset32 = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
                    if (s) offset32 |= 0xFE000000;

                    int32_t newOffset = (int32_t)(symAddr | 1) - (int32_t)(pAddr + 4) + offset32;
                    newOffset >>= 1;

                    s = (newOffset >> 23) & 1;
                    i1 = (newOffset >> 22) & 1;
                    i2 = (newOffset >> 21) & 1;
                    imm10 = (newOffset >> 11) & 0x3ff;
                    imm11 = newOffset & 0x7ff;

                    j1 = ~(i1 ^ s) & 1;
                    j2 = ~(i2 ^ s) & 1;

                    hw[0] = (hi & 0xF800) | (s << 10) | imm10;
                    hw[1] = (lo & 0xD000) | (j1 << 13) | (j2 << 11) | imm11;
                    break;
                }

                case R_ARM_CALL:
                case R_ARM_JUMP24:
                case R_ARM_PC24:
                case R_ARM_PLT32:
                {
                    int32_t instr = *reinterpret_cast<int32_t*>(target);
                    int32_t offset24 = instr & 0x00FFFFFF;
                    if (offset24 & 0x00800000) offset24 |= 0xFF000000;
                    offset24 <<= 2;

                    int32_t newOffset = (int32_t)symAddr - (int32_t)(pAddr + 8) + offset24;
                    newOffset >>= 2;

                    *target = (instr & 0xFF000000) | (newOffset & 0x00FFFFFF);
                    break;
                }

                case R_ARM_THM_MOVW_ABS_NC:
                case R_ARM_THM_MOVT_ABS:
                {
                    uint16_t* hw = reinterpret_cast<uint16_t*>(target);
                    uint16_t hi = hw[0];
                    uint16_t lo = hw[1];

                    uint32_t imm = ((hi & 0xF) << 12) | ((hi & 0x400) << 1) |
                                   ((lo & 0x7000) >> 4) | (lo & 0xFF);

                    if (relType == R_ARM_THM_MOVT_ABS)
                        imm = (symAddr >> 16) + (imm >> 16);
                    else
                        imm = (symAddr & 0xFFFF) + (imm & 0xFFFF);

                    hw[0] = (hi & 0xFBF0) | ((imm >> 12) & 0xF) | ((imm >> 1) & 0x400);
                    hw[1] = (lo & 0x8F00) | ((imm << 4) & 0x7000) | (imm & 0xFF);
                    break;
                }

                case R_ARM_V4BX:
                    // No action needed
                    break;

                default:
                    PRINTF("AppLoader: Unknown relocation type %lu\r\n", relType);
                    break;
            }
        }
    }

    if (symAddrs) MEM_FREE(symAddrs);
    // Note: sectionAddrs belongs to app struct, freed in unloadApp

    // Flush caches to ensure code is visible
    L1CACHE_CleanDCache();
    L1CACHE_InvalidateICache();

    return AppResult::SUCCESS;
}

// Task wrapper that sets up r9 and calls app entry
struct AppTaskContext
{
    LoadedApp* app;
    void* args;
    uint32_t gotBase;  // GOT base for PIC code - passed to Task::Create
};

// Helper functions for syscalls used in appTaskWrapper (runs in USER mode)
static void syscall_free(void* ptr)
{
    register void* p __asm__("r0") = ptr;
    
    __asm__ volatile(
        "svc %[svc_num]"
        :
        : "r" (p), [svc_num] "I" (SYS_FREE)
        : "r1", "r2", "r3", "memory"
    );
}

static void syscall_exit(int exitCode)
{
    register int32_t code __asm__("r0") = exitCode;
    
    __asm__ volatile(
        "svc %[svc_num]"
        :
        : "r" (code), [svc_num] "I" (SYS_EXIT)
        : "r1", "r2", "r3", "memory"
    );
    
    while(1) {}
}

static int syscall_write(const char* str, size_t len)
{
    register int32_t result __asm__("r0");
    register int32_t fd __asm__("r0") = STDOUT_FD;
    register const char* buf __asm__("r1") = str;
    register uint32_t count __asm__("r2") = (uint32_t)len;
    
    __asm__ volatile(
        "svc %[svc_num]"
        : "=r" (result)
        : "r" (fd), "r" (buf), "r" (count), [svc_num] "I" (SYS_WRITE)
        : "r3", "memory"
    );
    
    return result;
}

void AppLoader::appTaskWrapper(void* param)
{
    AppTaskContext* ctx = static_cast<AppTaskContext*>(param);
    LoadedApp* app = ctx->app;
    void* args = ctx->args;
    // Note: gotBase is already set in r9 by Task::Create with r9Value parameter

    // Free context struct using syscall (we're in USER mode)
    syscall_free(ctx);

    if (app == nullptr || app->entryPoint == nullptr)
    {
        const char msg[] = "AppLoader: Invalid app context\r\n";
        syscall_write(msg, sizeof(msg) - 1);
        syscall_exit(-1);
        return;
    }

    app->isRunning = true;

    // Call entry point directly - r9 is already set by initStackWithR9
    AppEntryFunc entry = app->entryPoint;
    int result = entry(args);

    // Use simple message without formatting (we can't do printf in USER mode easily)
    const char exitMsg[] = "AppLoader: App exited\r\n";
    syscall_write(exitMsg, sizeof(exitMsg) - 1);
    
    app->isRunning = false;

    // Task ends here - use syscall
    syscall_exit(result);
}

AppResult AppLoader::runApp(const char* appName, void* args, uint32_t priority, size_t stackSize)
{
    LoadedApp* app = findApp(appName);
    if (app == nullptr)
    {
        PRINTF("AppLoader: App '%s' not loaded\r\n", appName);
        return AppResult::ERROR_INVALID_ELF;
    }

    if (app->isRunning)
    {
        PRINTF("AppLoader: App '%s' already running\r\n", appName);
        return AppResult::ERROR_ALREADY_LOADED;
    }

    // Allocate task context
    AppTaskContext* ctx = static_cast<AppTaskContext*>(MEM_ALLOC(sizeof(AppTaskContext)));
    if (ctx == nullptr)
    {
        return AppResult::ERROR_NO_MEMORY;
    }
    ctx->app = app;
    ctx->args = args;
    ctx->gotBase = app->gotBase;

    // Create task in USER mode with r9 set to GOT base for PIC code
    // App API functions (printf, etc.) use syscalls (SVC) which run in
    // privileged context, so cache operations work correctly.
    Result res = Task::Create(
        appTaskWrapper,
        appName,
        stackSize / sizeof(uint32_t),
        ctx,
        priority,
        &app->taskHandle,
        Task::PrivilegeMode::USER,  // Run app in unprivileged USER mode
        app->gotBase  // Initial r9 value (GOT base for PIC applications)
    );

    if (res != Result::RESULT_SUCCESS)
    {
        MEM_FREE(ctx);
        PRINTF("AppLoader: Failed to create task for '%s'\r\n", appName);
        return AppResult::ERROR_TASK_CREATE;
    }

    PRINTF("AppLoader: Started app '%s' (task handle=%p, GOT=0x%08lX)\r\n", appName, app->taskHandle, app->gotBase);
    return AppResult::SUCCESS;
}

AppResult AppLoader::loadAppFromFile(const char* filePath)
{
    if (filePath == nullptr)
    {
        return AppResult::ERROR_FILE_READ;
    }

    // Extract app name from path
    const char* appName = filePath;
    const char* lastSlash = strrchr(filePath, '/');
    if (lastSlash != nullptr)
    {
        appName = lastSlash + 1;
    }

    // Remove extension
    char nameBuf[32];
    strncpy(nameBuf, appName, sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    char* dot = strrchr(nameBuf, '.');
    if (dot != nullptr)
    {
        *dot = '\0';
    }

    // Open file
    FIL file;
    FRESULT fres = f_open(&file, filePath, FA_READ);
    if (fres != FR_OK)
    {
        PRINTF("AppLoader: Cannot open '%s' (error=%d)\r\n", filePath, fres);
        return AppResult::ERROR_FILE_READ;
    }

    // Get file size
    size_t fileSize = f_size(&file);
    if (fileSize == 0 || fileSize > 512 * 1024) // Max 512KB
    {
        f_close(&file);
        PRINTF("AppLoader: Invalid file size %u\r\n", (unsigned)fileSize);
        return AppResult::ERROR_FILE_READ;
    }

    // Allocate buffer
    uint8_t* buffer = static_cast<uint8_t*>(MEM_ALLOC(fileSize));
    if (buffer == nullptr)
    {
        f_close(&file);
        return AppResult::ERROR_NO_MEMORY;
    }

    // Read file
    UINT bytesRead;
    fres = f_read(&file, buffer, fileSize, &bytesRead);
    f_close(&file);

    if (fres != FR_OK || bytesRead != fileSize)
    {
        MEM_FREE(buffer);
        return AppResult::ERROR_FILE_READ;
    }

    // Load app
    AppResult result = loadApp(buffer, fileSize, nameBuf);

    MEM_FREE(buffer);
    return result;
}

AppResult AppLoader::loadAndRunApp(const char* filePath, void* args, uint32_t priority, size_t stackSize)
{
    // Extract app name
    const char* appName = filePath;
    const char* lastSlash = strrchr(filePath, '/');
    if (lastSlash != nullptr)
    {
        appName = lastSlash + 1;
    }
    char nameBuf[32];
    strncpy(nameBuf, appName, sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    char* dot = strrchr(nameBuf, '.');
    if (dot != nullptr)
    {
        *dot = '\0';
    }

    AppResult result = loadAppFromFile(filePath);
    if (result != AppResult::SUCCESS)
    {
        return result;
    }

    return runApp(nameBuf, args, priority, stackSize);
}

AppResult AppLoader::stopApp(const char* appName)
{
    LoadedApp* app = findApp(appName);
    if (app == nullptr || !app->isLoaded)
    {
        return AppResult::ERROR_INVALID_ELF;
    }

    if (app->isRunning && app->taskHandle != nullptr)
    {
        Task::Delete(&app->taskHandle);
        app->isRunning = false;
        app->taskHandle = nullptr;
    }

    return AppResult::SUCCESS;
}

AppResult AppLoader::unloadApp(const char* appName)
{
    LoadedApp* app = findApp(appName);
    if (app == nullptr)
    {
        return AppResult::ERROR_INVALID_ELF;
    }

    // Stop first
    stopApp(appName);

    // Free memory
    if (app->baseAddress != nullptr)
    {
        MEM_FREE(app->baseAddress);
    }
    if (app->got != nullptr)
    {
        MEM_FREE(app->got);
    }
    if (app->sectionAddrs != nullptr)
    {
        MEM_FREE(app->sectionAddrs);
    }

    memset(app, 0, sizeof(LoadedApp));
    _appCount--;

    return AppResult::SUCCESS;
}

LoadedApp* AppLoader::findApp(const char* appName)
{
    for (size_t i = 0; i < MAX_APPS; i++)
    {
        if (_apps[i].isLoaded && strcmp(_apps[i].name, appName) == 0)
        {
            return &_apps[i];
        }
    }
    return nullptr;
}

void AppLoader::listApps()
{
    PRINTF("=== Loaded Applications ===\r\n");
    for (size_t i = 0; i < MAX_APPS; i++)
    {
        if (_apps[i].isLoaded)
        {
            PRINTF("  [%u] %s @ 0x%08lX, size=%u, %s\r\n",
                   (unsigned)i,
                   _apps[i].name,
                   (uint32_t)_apps[i].baseAddress,
                   (unsigned)_apps[i].size,
                   _apps[i].isRunning ? "RUNNING" : "LOADED");
        }
    }
    PRINTF("===========================\r\n");
}

} // namespace CRTOS
