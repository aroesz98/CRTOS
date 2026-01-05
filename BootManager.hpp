/*
 * BootManager.hpp - CRTOS Boot Manager
 * Author: Arkadiusz Szlanta
 * Date: 28 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Boot Manager for loading binary modules from SD card on startup.
 * Scans configured directories for .bin files and loads them as tasks.
 */

#ifndef CRTOS_BOOT_MANAGER_HPP
#define CRTOS_BOOT_MANAGER_HPP

#include <stdint.h>
#include "CRTOS.hpp"
#include "Task.hpp"

namespace CRTOS
{

/**
 * @brief Boot configuration
 */
struct BootConfig
{
    const char* bootDirectory;      ///< Directory to scan for modules (e.g., "/boot")
    const char* configFile;         ///< Optional config file (e.g., "/boot/boot.cfg")
    const char* bootLogoFile;       ///< Optional boot logo file (BMP format, e.g., "/boot/logo.bmp")
    uint32_t bootLogoDisplayMs;     ///< Time to display boot logo in milliseconds (0 = skip)
    uint32_t defaultPriority;       ///< Default task priority for loaded modules
    uint32_t sdCardTimeoutMs;       ///< Timeout waiting for SD card insertion
    bool waitForCard;               ///< If true, wait for card; if false, skip if not present
    bool verboseOutput;             ///< Print detailed loading information
    bool skipDiagnostics;           ///< Skip SD card info and filesystem stats for faster boot
    uint32_t maxModules;            ///< Maximum number of modules to load
};

/**
 * @brief Loaded module entry
 */
struct BootedModule
{
    char filename[64];              ///< Module filename
    Task::TaskHandle taskHandle;    ///< Task handle
    uint32_t loadAddress;           ///< Memory address where module is loaded
    uint32_t size;                  ///< Module size in bytes
    uint32_t priority;              ///< Task priority
    bool loaded;                    ///< Successfully loaded flag
};

/**
 * @brief Boot Manager statistics
 */
struct BootStats
{
    uint32_t modulesFound;          ///< Number of .bin files found
    uint32_t modulesLoaded;         ///< Number successfully loaded
    uint32_t modulesFailed;         ///< Number that failed to load
    uint32_t totalBytesLoaded;      ///< Total bytes loaded
    uint32_t bootTimeMs;            ///< Total boot time in milliseconds
};

/**
 * @brief Boot Manager class
 * 
 * Handles automatic loading of binary modules from SD card during boot.
 */
class BootManager
{
public:
    /// Maximum modules that can be loaded
    static constexpr uint32_t MAX_BOOT_MODULES = 16;
    
    /// Default boot configuration
    static constexpr BootConfig DefaultConfig = {
        .bootDirectory = "0:/boot",
        .configFile = "0:/boot/boot.cfg",
        .bootLogoFile = "0:/boot/logo.bmp",
        .bootLogoDisplayMs = 2000,
        .defaultPriority = 5,
        .sdCardTimeoutMs = 5000,
        .waitForCard = true,
        .verboseOutput = true,
        .skipDiagnostics = false,
        .maxModules = MAX_BOOT_MODULES
    };

    /**
     * @brief Constructor
     */
    BootManager();
    
    /**
     * @brief Destructor
     */
    ~BootManager();
    
    /**
     * @brief Initialize boot manager and load modules
     * 
     * This is the main entry point. It will:
     * 1. Wait for SD card (if configured)
     * 2. Mount the filesystem
     * 3. Parse boot configuration (if exists)
     * 4. Load all modules from boot directory
     * 
     * @param config Boot configuration
     * @return Result::RESULT_SUCCESS if at least one module loaded
     */
    Result Boot(const BootConfig& config = DefaultConfig);
    
    /**
     * @brief Display boot logo from BMP file
     *
     * Loads a BMP image from SD card and displays it centered on screen.
     * Supports 24-bit and 32-bit BMP formats.
     *
     * @param logoPath Path to BMP file
     * @param displayMs Time to show logo in milliseconds
     * @return Result::RESULT_SUCCESS on success
     */
    Result DisplayBootLogo(const char* logoPath, uint32_t displayMs);

    /**
     * @brief Get number of loaded modules
     * 
     * @return Number of successfully loaded modules
     */
    uint32_t GetLoadedModuleCount() const;
    
    /**
     * @brief Get loaded module info by index
     * 
     * @param index Module index (0 to GetLoadedModuleCount()-1)
     * @param module Output module info
     * @return Result::RESULT_SUCCESS on success
     */
    Result GetLoadedModule(uint32_t index, BootedModule& module) const;
    
    /**
     * @brief Get boot statistics
     * 
     * @return Boot statistics structure
     */
    const BootStats& GetStats() const;
    
    /**
     * @brief Unload a specific module
     * 
     * @param index Module index
     * @return Result::RESULT_SUCCESS on success
     */
    Result UnloadModule(uint32_t index);
    
    /**
     * @brief Unload all modules
     * 
     * @return Result::RESULT_SUCCESS on success
     */
    Result UnloadAll();
    
    /**
     * @brief Reload a module from file
     * 
     * Loads a .bin module from the specified path and adds it to the
     * loaded modules list. Use this to reload a module after deletion.
     * 
     * @param filepath Full path to .bin file (e.g., "0:/boot/display_test.bin")
     * @param priority Task priority
     * @param outHandle Output task handle (optional, can be nullptr)
     * @return Result::RESULT_SUCCESS on success
     */
    Result ReloadModule(const char* filepath, uint32_t priority, Task::TaskHandle* outHandle = nullptr);
    
    /**
     * @brief Check if boot manager is initialized
     * 
     * @return true if Boot() has been called successfully
     */
    bool IsInitialized() const;

private:
    /**
     * @brief Initialize SD card and mount filesystem
     * 
     * @param config Boot configuration
     * @return Result::RESULT_SUCCESS on success
     */
    Result InitializeStorage(const BootConfig& config);
    
    /**
     * @brief Parse boot configuration file
     * 
     * @param configPath Path to config file
     * @return Result::RESULT_SUCCESS on success
     */
    Result ParseConfigFile(const char* configPath);
    
    /**
     * @brief Scan directory for .bin files
     * 
     * @param directory Directory path
     * @param config Boot configuration
     * @return Number of modules found
     */
    uint32_t ScanDirectory(const char* directory, const BootConfig& config);
    
    /**
     * @brief Load modules directly from config file entries
     * 
     * @param config Boot configuration
     * @return Number of modules loaded
     */
    uint32_t LoadModulesFromConfig(const BootConfig& config);
    
    /**
     * @brief Load a single module from file
     * 
     * @param filepath Full path to .bin file
     * @param priority Task priority
     * @param module Output module info
     * @return Result::RESULT_SUCCESS on success
     */
    Result LoadModule(const char* filepath, uint32_t priority, BootedModule& module);
    
    /**
     * @brief Parse priority from config line
     * 
     * @param filename Module filename
     * @return Priority (or default if not specified)
     */
    uint32_t GetModulePriority(const char* filename) const;
    
    /**
     * @brief Check if file has .bin extension
     * 
     * @param filename Filename to check
     * @return true if ends with .bin
     */
    static bool IsBinFile(const char* filename);
    
    /**
     * @brief Print boot message
     * 
     * @param format Printf format string
     */
    void Log(const char* format, ...) const;

    // State
    bool m_initialized;
    bool m_verbose;
    
    // Loaded modules
    BootedModule m_modules[MAX_BOOT_MODULES];
    uint32_t m_moduleCount;
    
    // Per-module priority overrides from config file
    struct PriorityOverride
    {
        char filename[32];
        uint32_t priority;
    };
    PriorityOverride m_priorityOverrides[MAX_BOOT_MODULES];
    uint32_t m_priorityOverrideCount;
    
    // Default priority
    uint32_t m_defaultPriority;
    
    // Statistics
    BootStats m_stats;
};

/**
 * @brief Get global boot manager instance
 * 
 * @return Reference to singleton BootManager
 */
BootManager& GetBootManager();

} // namespace CRTOS

#endif // CRTOS_BOOT_MANAGER_HPP
