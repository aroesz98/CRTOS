/*
 * ModuleLoader.hpp - CRTOS Dynamic Module Loader
 * Author: Arkadiusz Szlanta
 * Date: 03 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Dynamic module loader for kernel modules (drivers, etc.).
 * Loads relocatable ELF objects (.o files) and resolves symbols
 * against the kernel symbol table.
 */

#ifndef MODULE_LOADER_HPP
#define MODULE_LOADER_HPP

#include <cstdint>
#include <cstddef>

namespace CRTOS
{

// Module load result
enum class ModuleResult : int32_t
{
    SUCCESS             = 0,
    ERROR_INVALID_ELF   = -1,
    ERROR_NOT_RELOCATABLE = -2,
    ERROR_WRONG_ARCH    = -3,
    ERROR_NO_MEMORY     = -4,
    ERROR_SYMBOL_NOT_FOUND = -5,
    ERROR_RELOCATION_FAILED = -6,
    ERROR_NO_INIT_FUNC  = -7,
    ERROR_INIT_FAILED   = -8,
    ERROR_FILE_READ     = -9,
    ERROR_ALREADY_LOADED = -10
};

// Maximum number of loaded modules
constexpr size_t MAX_MODULES = 8;

// Maximum exported symbols from kernel
constexpr size_t MAX_KERNEL_SYMBOLS = 256;

/**
 * @brief Kernel symbol entry
 *
 * Used to export kernel functions that modules can call.
 */
struct KernelSymbol
{
    const char* name;       // Symbol name
    void*       address;    // Symbol address
};

/**
 * @brief Loaded module information
 */
struct LoadedModule
{
    char        name[64];       // Module name (own buffer)
    void*       baseAddress;    // Base address of loaded module
    size_t      size;           // Total allocated size
    void*       initFunc;       // Module init function
    void*       exitFunc;       // Module exit function
    void*       driverInstance; // Driver instance (returned by init)
    void*       got;            // Global Offset Table (if allocated)
    size_t      gotSize;        // GOT size in bytes
    bool        isLoaded;       // Whether module is loaded
};

/**
 * @brief Dynamic Module Loader
 *
 * Loads relocatable ELF objects and links them against kernel symbols.
 * Used to dynamically load drivers at runtime.
 */
class ModuleLoader
{
public:
    /**
     * @brief Get singleton instance
     */
    static ModuleLoader& getInstance();

    /**
     * @brief Initialize the module loader
     *
     * Registers default kernel symbols.
     *
     * @return ModuleResult::SUCCESS on success
     */
    ModuleResult init();

    /**
     * @brief Register a kernel symbol
     *
     * Modules can reference these symbols during relocation.
     *
     * @param name      Symbol name
     * @param address   Symbol address
     * @return ModuleResult::SUCCESS on success
     */
    ModuleResult registerSymbol(const char* name, void* address);

    /**
     * @brief Load a module from memory buffer
     *
     * @param elfData   Pointer to ELF data in memory
     * @param elfSize   Size of ELF data
     * @param moduleName    Name for the module
     * @return ModuleResult::SUCCESS on success
     */
    ModuleResult loadModule(const uint8_t* elfData, size_t elfSize, const char* moduleName);

    /**
     * @brief Load a module from file path
     *
     * @param filePath  Path to module file (e.g., "/drivers/uart.ko")
     * @return ModuleResult::SUCCESS on success
     */
    ModuleResult loadModuleFromFile(const char* filePath);

    /**
     * @brief Unload a module by name
     *
     * Calls module exit function and frees memory.
     *
     * @param moduleName    Module name
     * @return ModuleResult::SUCCESS on success
     */
    ModuleResult unloadModule(const char* moduleName);

    /**
     * @brief Find a loaded module by name
     *
     * @param moduleName    Module name
     * @return Pointer to LoadedModule, or nullptr if not found
     */
    LoadedModule* findModule(const char* moduleName);

    /**
     * @brief Get the driver instance from a loaded module
     *
     * @param moduleName    Module name
     * @return Driver instance pointer, or nullptr if not found
     */
    void* getDriverInstance(const char* moduleName);

    /**
     * @brief List all loaded modules (debug)
     */
    void listModules();

    /**
     * @brief Load all modules from a directory
     *
     * Scans a directory for .ko files and loads them.
     *
     * @param directoryPath Path to directory (e.g., "/drivers")
     * @return Number of successfully loaded modules
     */
    size_t loadModulesFromDirectory(const char* directoryPath);

    /**
     * @brief Get number of loaded modules
     */
    size_t getModuleCount() const { return _moduleCount; }

    /**
     * @brief Lookup a registered kernel symbol by name
     *
     * @param name      Symbol name to look up
     * @return Symbol address, or nullptr if not found
     */
    void* lookupSymbol(const char* name);

private:
    ModuleLoader();
    ~ModuleLoader();

    // Prevent copying
    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    // ELF parsing helpers
    bool validateElf(const uint8_t* elfData, size_t elfSize);
    ModuleResult processRelocations(LoadedModule* module, const uint8_t* elfData);
    void* resolveSymbol(const char* name);

    // Kernel symbol table
    KernelSymbol _kernelSymbols[MAX_KERNEL_SYMBOLS];
    size_t _symbolCount;

    // Loaded modules
    LoadedModule _modules[MAX_MODULES];
    size_t _moduleCount;

    bool _initialized;
};

/**
 * @brief Macro to export a kernel symbol for use by modules
 *
 * Place this in kernel code to make a function available to modules:
 *   EXPORT_SYMBOL(myFunction);
 */
#define EXPORT_SYMBOL(sym) \
    static void __attribute__((constructor)) __export_##sym(void) { \
        CRTOS::ModuleLoader::getInstance().registerSymbol(#sym, (void*)&sym); \
    }

/**
 * @brief Macro for module metadata
 *
 * Place this in a module to define entry/exit points:
 *   MODULE_INFO("uart", driver_module_init, driver_module_exit);
 */
#define MODULE_INFO(name, init, exit) \
    extern "C" { \
        const char __module_name[] __attribute__((section(".modinfo"))) = name; \
        void* __module_init __attribute__((section(".modinfo"))) = (void*)init; \
        void* __module_exit __attribute__((section(".modinfo"))) = (void*)exit; \
    }

/**
 * @brief Register kernel symbols for use by dynamically loaded modules
 *
 * This function registers C wrapper functions for CRTOS API.
 * Must be called before loading any modules.
 * Defined in KernelExports.cpp
 */
void RegisterKernelSymbols();

} // namespace CRTOS

#endif // MODULE_LOADER_HPP
