/*
 * BootManager.cpp - CRTOS Boot Manager Implementation
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
 * Boot Manager implementation for loading binary modules from SD card.
 */

#include "BootManager.hpp"
#include "HAL/SDCard.hpp"
#include "HAL/FileSystem.hpp"
#include "HAL/Display.hpp"
#include "HeapAllocator.hpp"
#include "Task.hpp"
#include <cstring>
#include <cstdio>
#include <cstdarg>

// External tick counter (defined in CRTOS.cpp)
extern volatile uint32_t tickCount;

namespace CRTOS
{

// Singleton instance
static BootManager s_bootManager;

BootManager& GetBootManager()
{
    return s_bootManager;
}

BootManager::BootManager()
    : m_initialized(false)
    , m_verbose(true)
    , m_moduleCount(0)
    , m_priorityOverrideCount(0)
    , m_defaultPriority(5)
{
    memset(&m_stats, 0, sizeof(m_stats));
    memset(m_modules, 0, sizeof(m_modules));
    memset(m_priorityOverrides, 0, sizeof(m_priorityOverrides));
}

BootManager::~BootManager()
{
    // Don't unload modules in destructor - they may still be running
}

Result BootManager::Boot(const BootConfig& config)
{
    m_verbose = config.verboseOutput;
    m_defaultPriority = config.defaultPriority;
    
    // Start timing from the beginning (includes SD init, excludes logo display)
    uint32_t startTime = ::tickCount;
    uint32_t logoTime = 0;

    Log("\r\n");
    Log("╔══════════════════════════════════════════════════════════════╗\r\n");
    Log("║              CRTOS Boot Manager v1.0                         ║\r\n");
    Log("╠══════════════════════════════════════════════════════════════╣\r\n");
    Log("║  Boot Directory: %-42s ║\r\n", config.bootDirectory);
    Log("╚══════════════════════════════════════════════════════════════╝\r\n");
    Log("\r\n");
    
    // Step 1: Initialize storage (SD card + filesystem)
    Result res = InitializeStorage(config);
    if (res != Result::RESULT_SUCCESS)
    {
        Log("[BOOT] ✗ Storage initialization failed\r\n");
        if (!config.waitForCard)
        {
            Log("[BOOT] Continuing without SD card modules...\r\n");
            return Result::RESULT_SUCCESS;  // Not fatal if card is optional
        }
        return res;
    }
    
    Log("[BOOT] ✓ Storage initialized\r\n");
    
    // Step 1.5: Display boot logo if configured (measure time separately)
    if (config.bootLogoFile && config.bootLogoFile[0] != '\0')
    {
        uint32_t logoStart = ::tickCount;
        DisplayBootLogo(config.bootLogoFile, config.bootLogoDisplayMs);
        logoTime = ::tickCount - logoStart;
    }
    
    // Step 2: Parse config file and load modules
    HAL::FileSystem& fs = HAL::GetFileSystem();
    if (config.configFile && fs.Exists(config.configFile))
    {
        Log("[BOOT] Found config file: %s\r\n", config.configFile);
        ParseConfigFile(config.configFile);
        
        // Load modules from config file (in order specified)
        if (m_priorityOverrideCount > 0)
        {
            Log("[BOOT] Loading %u modules from config...\r\n", m_priorityOverrideCount);
            LoadModulesFromConfig(config);
        }
    }
    else
    {
        // Fallback: scan directory if no config file
        Log("[BOOT] No config file, scanning directory...\r\n");
        
        if (!fs.Exists(config.bootDirectory))
        {
            Log("[BOOT] Boot directory not found: %s\r\n", config.bootDirectory);
            Log("[BOOT] Creating boot directory...\r\n");
            fs.MakeDir(config.bootDirectory);
            return Result::RESULT_BAD_PARAMETER;
        }
        
        (void)ScanDirectory(config.bootDirectory, config);
    }
    
    // Calculate boot time (excluding logo display time)
    m_stats.bootTimeMs = (::tickCount - startTime) - logoTime;
    
    // Print summary
    Log("\r\n");
    Log("╔══════════════════════════════════════════════════════════════╗\r\n");
    Log("║                    Boot Summary                              ║\r\n");
    Log("╠══════════════════════════════════════════════════════════════╣\r\n");
    Log("║  Modules found:    %-4u                                      ║\r\n", m_stats.modulesFound);
    Log("║  Modules loaded:   %-4u                                      ║\r\n", m_stats.modulesLoaded);
    Log("║  Modules failed:   %-4u                                      ║\r\n", m_stats.modulesFailed);
    Log("║  Total bytes:      %-8u                                  ║\r\n", m_stats.totalBytesLoaded);
    Log("║  Boot time:        %-4u ms                                   ║\r\n", m_stats.bootTimeMs);
    Log("╚══════════════════════════════════════════════════════════════╝\r\n");
    Log("\r\n");
    
    // Print loaded modules table
    if (m_moduleCount > 0)
    {
        Log("┌────────────────────────────────────────────────────────────────┐\r\n");
        Log("│ #  │ Module Name                      │ Size    │ Pri │ Handle │\r\n");
        Log("├────────────────────────────────────────────────────────────────┤\r\n");
        
        for (uint32_t i = 0; i < m_moduleCount; i++)
        {
            if (m_modules[i].loaded)
            {
                Log("│ %-2u │ %-32s │ %-7u │ %-3u │ %06X │\r\n",
                    i,
                    m_modules[i].filename,
                    m_modules[i].size,
                    m_modules[i].priority,
                    (uint32_t)m_modules[i].taskHandle & 0xFFFFFF);
            }
        }
        
        Log("└────────────────────────────────────────────────────────────────┘\r\n");
    }
    
    m_initialized = true;
    
    return (m_stats.modulesLoaded > 0) ? Result::RESULT_SUCCESS : Result::RESULT_BAD_PARAMETER;
}

Result BootManager::InitializeStorage(const BootConfig& config)
{
    HAL::SDCard& sd = HAL::GetSDCard();
    HAL::FileSystem& fs = HAL::GetFileSystem();
    
    // Check if already mounted
    if (fs.IsMounted())
    {
        Log("[BOOT] Filesystem already mounted\r\n");
        return Result::RESULT_SUCCESS;
    }
    
    // Initialize SD card hardware
    Log("[BOOT] Initializing SD Card...\r\n");
    
    HAL::SDCardConfig sdConfig = HAL::SDCard::DefaultConfig;
    sdConfig.maxFrequencyHz = 50000000;  // 50 MHz high-speed
    sdConfig.use4BitBus = true;
    sdConfig.enableHighSpeed = true;
    sdConfig.irqPriority = 5;
    
    Result sdResult = sd.Initialize(sdConfig);
    if (sdResult != Result::RESULT_SUCCESS)
    {
        Log("[BOOT]  SD Card initialization failed\r\n");
        return sdResult;
    }
    
    if (!config.skipDiagnostics)
    {
        Log("[BOOT] ✓ SD Card controller initialized\r\n");
    }
    
    if (!sd.IsCardInserted())
    {
        Log("[BOOT] ✗ No SD card inserted\r\n");
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (!config.skipDiagnostics)
    {
        Log("[BOOT] ✓ SD Card detected\r\n");
        
        // Get and display card info (skip if fast boot)
        HAL::SDCardInfo cardInfo;
        if (sd.GetCardInfo(cardInfo) == Result::RESULT_SUCCESS)
        {
            const char* typeStr = "Unknown";
            switch (cardInfo.type)
            {
                case HAL::SDCardType::SDSC: typeStr = "SDSC"; break;
                case HAL::SDCardType::SDHC: typeStr = "SDHC"; break;
                case HAL::SDCardType::SDXC: typeStr = "SDXC"; break;
                default: break;
            }
            Log("[BOOT]   Type: %s\r\n", typeStr);
            Log("[BOOT]   Capacity: %u MB\r\n", (uint32_t)(cardInfo.capacityBytes / (1024 * 1024)));
            Log("[BOOT]   Block Size: %u bytes\r\n", cardInfo.blockSize);
            Log("[BOOT]   Block Count: %u\r\n", cardInfo.blockCount);
        }
    }
    
    // Mount filesystem
    if (!config.skipDiagnostics)
    {
        Log("[BOOT] Mounting filesystem...\r\n");
    }
    
    Result res = fs.Mount("0:");
    if (res != Result::RESULT_SUCCESS)
    {
        Log("[BOOT] ✗ Failed to mount filesystem\r\n");
        return res;
    }
    
    if (!config.skipDiagnostics)
    {
        Log("[BOOT] ✓ Filesystem mounted\r\n");
        
        // Get filesystem stats (skip if fast boot)
        HAL::FileSystemStats stats;
        if (fs.GetStats("0:", stats) == Result::RESULT_SUCCESS)
        {
            uint32_t totalMB = (uint32_t)(stats.totalBytes / (1024 * 1024));
            uint32_t freeMB = (uint32_t)(stats.freeBytes / (1024 * 1024));
            Log("[BOOT]   Total: %u MB, Free: %u MB\r\n", totalMB, freeMB);
        }
    }
    
    return Result::RESULT_SUCCESS;
}

Result BootManager::ParseConfigFile(const char* configPath)
{
    HAL::FileSystem& fs = HAL::GetFileSystem();
    HAL::File file;
    
    Result res = fs.Open(file, configPath, 0x01);  // FA_READ
    if (res != Result::RESULT_SUCCESS)
    {
        return res;
    }
    
    Log("[BOOT] Parsing config file...\r\n");
    
    char line[128];
    while (file.ReadLine(line, sizeof(line)) != nullptr)
    {
        // Skip comments and empty lines
        if (line[0] == '#' || line[0] == '\r' || line[0] == '\n' || line[0] == '\0')
        {
            continue;
        }
        
        // Remove trailing newline
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n'))
        {
            line[--len] = '\0';
        }
        
        // Parse "module.bin=priority" format
        char* equals = strchr(line, '=');
        if (equals && m_priorityOverrideCount < MAX_BOOT_MODULES)
        {
            *equals = '\0';
            const char* filename = line;
            const char* priorityStr = equals + 1;
            
            // Simple atoi implementation (avoid cstdlib dependency)
            uint32_t priority = 0;
            while (*priorityStr >= '0' && *priorityStr <= '9')
            {
                priority = priority * 10 + (*priorityStr - '0');
                priorityStr++;
            }
            if (priority > 0 && priority <= 10)
            {
                strncpy(m_priorityOverrides[m_priorityOverrideCount].filename, 
                        filename, 
                        sizeof(m_priorityOverrides[0].filename) - 1);
                m_priorityOverrides[m_priorityOverrideCount].priority = priority;
                m_priorityOverrideCount++;
                
                Log("[BOOT]   Config: %s -> priority %u\r\n", filename, priority);
            }
        }
    }
    
    file.Close();
    return Result::RESULT_SUCCESS;
}

uint32_t BootManager::LoadModulesFromConfig(const BootConfig& config)
{
    HAL::FileSystem& fs = HAL::GetFileSystem();
    uint32_t loadedCount = 0;
    
    for (uint32_t i = 0; i < m_priorityOverrideCount; i++)
    {
        // Check max modules limit
        if (m_moduleCount >= config.maxModules)
        {
            Log("[BOOT] ⚠ Max modules (%u) reached\r\n", config.maxModules);
            break;
        }
        
        const char* filename = m_priorityOverrides[i].filename;
        uint32_t priority = m_priorityOverrides[i].priority;
        
        // Build full path
        char fullPath[128];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", config.bootDirectory, filename);
        
        // Check if file exists
        if (!fs.Exists(fullPath))
        {
            Log("[BOOT] ✗ Module not found: %s\r\n", filename);
            m_stats.modulesFailed++;
            continue;
        }
        
        m_stats.modulesFound++;
        
        // Get file size for logging
        HAL::FileInfo info;
        uint32_t fileSize = 0;
        if (fs.Stat(fullPath, info) == Result::RESULT_SUCCESS)
        {
            fileSize = info.size;
        }
        
        Log("[BOOT] Loading: %-32s (%u bytes, pri %u)\r\n", 
            filename, fileSize, priority);
        
        // Load the module
        BootedModule& module = m_modules[m_moduleCount];
        Result res = LoadModule(fullPath, priority, module);
        
        if (res == Result::RESULT_SUCCESS)
        {
            Log("[BOOT]   ✓ Loaded at 0x%08X, handle 0x%08X\r\n",
                module.loadAddress, (uint32_t)module.taskHandle);
            m_stats.modulesLoaded++;
            m_stats.totalBytesLoaded += module.size;
            m_moduleCount++;
            loadedCount++;
        }
        else
        {
            Log("[BOOT]   ✗ Failed to load (error %d)\r\n", (int)res);
            m_stats.modulesFailed++;
        }
    }
    
    return loadedCount;
}

uint32_t BootManager::ScanDirectory(const char* directory, const BootConfig& config)
{
    HAL::FileSystem& fs = HAL::GetFileSystem();
    HAL::Directory dir;
    
    Result res = fs.OpenDir(dir, directory);
    if (res != Result::RESULT_SUCCESS)
    {
        Log("[BOOT] ✗ Failed to open directory: %s\r\n", directory);
        return 0;
    }
    
    HAL::FileInfo info;
    uint32_t foundCount = 0;
    
    while (dir.ReadNext(info) == Result::RESULT_SUCCESS)
    {
        // Skip directories
        if (info.attributes & 0x10)  // AM_DIR
        {
            continue;
        }
        
        // Skip non-.bin files
        if (!IsBinFile(info.name))
        {
            continue;
        }
        
        m_stats.modulesFound++;
        foundCount++;
        
        // Check max modules limit
        if (m_moduleCount >= config.maxModules)
        {
            Log("[BOOT] ⚠ Max modules (%u) reached, skipping: %s\r\n", 
                config.maxModules, info.name);
            continue;
        }
        
        // Build full path
        char fullPath[128];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", directory, info.name);
        
        // Get priority for this module
        uint32_t priority = GetModulePriority(info.name);
        
        Log("[BOOT] Loading: %-32s (%u bytes, pri %u)\r\n", 
            info.name, info.size, priority);
        
        // Load the module
        BootedModule& module = m_modules[m_moduleCount];
        res = LoadModule(fullPath, priority, module);
        
        if (res == Result::RESULT_SUCCESS)
        {
            Log("[BOOT]   ✓ Loaded at 0x%08X, handle 0x%08X\r\n",
                module.loadAddress, (uint32_t)module.taskHandle);
            m_stats.modulesLoaded++;
            m_stats.totalBytesLoaded += module.size;
            m_moduleCount++;
        }
        else
        {
            Log("[BOOT]   ✗ Failed to load (error %d)\r\n", (int)res);
            m_stats.modulesFailed++;
        }
    }
    
    dir.Close();
    return foundCount;
}

Result BootManager::LoadModule(const char* filepath, uint32_t priority, BootedModule& module)
{
    HAL::FileSystem& fs = HAL::GetFileSystem();
    HAL::File file;
    
    // Open the file
    Result res = fs.Open(file, filepath, 0x01);  // FA_READ
    if (res != Result::RESULT_SUCCESS)
    {
        return res;
    }
    
    // Get file size
    uint32_t fileSize = file.Size();
    if (fileSize == 0 || fileSize > 1024 * 1024)  // Max 1MB module
    {
        file.Close();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Allocate buffer for the module binary
    uint8_t* buffer = (uint8_t*)HeapAllocator::Allocate(fileSize);
    if (buffer == nullptr)
    {
        file.Close();
        return Result::RESULT_NO_MEMORY;
    }
    
    // Read the entire file
    uint32_t bytesRead = 0;
    res = file.Read(buffer, fileSize, &bytesRead);
    file.Close();
    
    if (res != Result::RESULT_SUCCESS || bytesRead != fileSize)
    {
        HeapAllocator::Free(buffer);
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Extract filename from path
    const char* filename = filepath;
    const char* lastSlash = strrchr(filepath, '/');
    if (lastSlash)
    {
        filename = lastSlash + 1;
    }
    
    // Create task for the module
    Task::TaskHandle handle = nullptr;
    res = Task::LPC55S69_Features::RunExecutable(buffer, nullptr, priority, &handle);
    
    if (res != Result::RESULT_SUCCESS)
    {
        HeapAllocator::Free(buffer);
        return res;
    }

    HeapAllocator::Free(buffer);
    
    // Fill in module info
    strncpy(module.filename, filename, sizeof(module.filename) - 1);
    module.filename[sizeof(module.filename) - 1] = '\0';
    module.taskHandle = handle;
    module.loadAddress = (uint32_t)buffer;
    module.size = fileSize;
    module.priority = priority;
    module.loaded = true;
    
    return Result::RESULT_SUCCESS;
}

uint32_t BootManager::GetModulePriority(const char* filename) const
{
    // Check for override in config
    for (uint32_t i = 0; i < m_priorityOverrideCount; i++)
    {
        if (strcmp(m_priorityOverrides[i].filename, filename) == 0)
        {
            return m_priorityOverrides[i].priority;
        }
    }
    
    return m_defaultPriority;
}

bool BootManager::IsBinFile(const char* filename)
{
    if (!filename)
    {
        return false;
    }
    
    size_t len = strlen(filename);
    if (len < 5)  // At least "x.bin"
    {
        return false;
    }
    
    // Check for .bin extension (case insensitive)
    const char* ext = filename + len - 4;
    return (ext[0] == '.' && 
            (ext[1] == 'b' || ext[1] == 'B') &&
            (ext[2] == 'i' || ext[2] == 'I') &&
            (ext[3] == 'n' || ext[3] == 'N'));
}

void BootManager::Log(const char* format, ...) const
{
    if (!m_verbose)
    {
        return;
    }
    
    va_list args;
    va_start(args, format);
    
    // Use a buffer for formatted output
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);
    printf("%s", buffer);
    
    va_end(args);
}

uint32_t BootManager::GetLoadedModuleCount() const
{
    return m_moduleCount;
}

Result BootManager::GetLoadedModule(uint32_t index, BootedModule& module) const
{
    if (index >= m_moduleCount)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    module = m_modules[index];
    return Result::RESULT_SUCCESS;
}

const BootStats& BootManager::GetStats() const
{
    return m_stats;
}

Result BootManager::UnloadModule(uint32_t index)
{
    if (index >= m_moduleCount)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (!m_modules[index].loaded)
    {
        return Result::RESULT_SUCCESS;
    }
    
    // Delete the task
    Result res = Task::Delete(&m_modules[index].taskHandle);
    if (res != Result::RESULT_SUCCESS)
    {
        return res;
    }
    
    // Free the module memory
    HeapAllocator::Free((void*)m_modules[index].loadAddress);
    
    m_modules[index].loaded = false;
    m_modules[index].taskHandle = nullptr;
    
    return Result::RESULT_SUCCESS;
}

Result BootManager::UnloadAll()
{
    for (uint32_t i = 0; i < m_moduleCount; i++)
    {
        UnloadModule(i);
    }
    
    m_moduleCount = 0;
    m_initialized = false;
    
    return Result::RESULT_SUCCESS;
}

Result BootManager::ReloadModule(const char* filepath, uint32_t priority, Task::TaskHandle* outHandle)
{
    if (!m_initialized)
    {
        return Result::RESULT_NOT_FOUND;
    }
    
    if (filepath == nullptr)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Check if we have space for a new module
    if (m_moduleCount >= MAX_BOOT_MODULES)
    {
        return Result::RESULT_NO_MEMORY;
    }
    
    // Load the module
    BootedModule& module = m_modules[m_moduleCount];
    Result res = LoadModule(filepath, priority, module);
    
    if (res == Result::RESULT_SUCCESS)
    {
        Log("[BOOT] Reloaded: %s at 0x%08X, handle 0x%08X\r\n",
            module.filename, module.loadAddress, (uint32_t)module.taskHandle);
        
        m_stats.modulesLoaded++;
        m_stats.totalBytesLoaded += module.size;
        m_moduleCount++;
        
        if (outHandle != nullptr)
        {
            *outHandle = module.taskHandle;
        }
    }
    else
    {
        Log("[BOOT] Reload failed: %s (error %d)\r\n", filepath, (int)res);
        m_stats.modulesFailed++;
    }
    
    return res;
}

bool BootManager::IsInitialized() const
{
    return m_initialized;
}

Result BootManager::DisplayBootLogo(const char* logoPath, uint32_t displayMs)
{
    if (!logoPath || logoPath[0] == '\0')
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    HAL::FileSystem& fs = HAL::GetFileSystem();
    HAL::Display& display = HAL::GlobalDisplay;
    
    if (!fs.Exists(logoPath))
    {
        Log("[BOOT] Boot logo not found: %s\r\n", logoPath);
        return Result::RESULT_BAD_PARAMETER;
    }
    
    Log("[BOOT] Loading boot logo: %s\r\n", logoPath);
    
    // Open BMP file
    HAL::File file;
    if (fs.Open(file, logoPath, static_cast<uint8_t>(HAL::FileMode::Read)) != Result::RESULT_SUCCESS)
    {
        Log("[BOOT] Failed to open boot logo file\r\n");
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // BMP header structures
    #pragma pack(push, 1)
    struct BMPFileHeader {
        uint16_t signature;    // 'BM'
        uint32_t fileSize;
        uint16_t reserved1;
        uint16_t reserved2;
        uint32_t dataOffset;   // Offset to pixel data
    };
    
    struct BMPInfoHeader {
        uint32_t headerSize;
        int32_t  width;
        int32_t  height;       // Negative = top-down
        uint16_t planes;
        uint16_t bitsPerPixel;
        uint32_t compression;
        uint32_t imageSize;
        int32_t  xPixelsPerMeter;
        int32_t  yPixelsPerMeter;
        uint32_t colorsUsed;
        uint32_t colorsImportant;
    };
    #pragma pack(pop)
    
    // Read BMP file header
    BMPFileHeader fileHeader;
    uint32_t bytesRead;
    if (file.Read(&fileHeader, sizeof(fileHeader), &bytesRead) != Result::RESULT_SUCCESS ||
        bytesRead != sizeof(fileHeader))
    {
        Log("[BOOT] Failed to read BMP file header\r\n");
        file.Close();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Verify BMP signature ('BM')
    if (fileHeader.signature != 0x4D42)  // 'BM' in little-endian
    {
        Log("[BOOT] Invalid BMP signature: 0x%04X\r\n", fileHeader.signature);
        file.Close();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Read BMP info header
    BMPInfoHeader infoHeader;
    if (file.Read(&infoHeader, sizeof(infoHeader), &bytesRead) != Result::RESULT_SUCCESS ||
        bytesRead != sizeof(infoHeader))
    {
        Log("[BOOT] Failed to read BMP info header\r\n");
        file.Close();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Validate BMP format
    int32_t imgWidth = infoHeader.width;
    int32_t imgHeight = infoHeader.height;
    bool topDown = (imgHeight < 0);
    if (topDown)
    {
        imgHeight = -imgHeight;
    }
    
    uint16_t bpp = infoHeader.bitsPerPixel;
    if (bpp != 24 && bpp != 32)
    {
        Log("[BOOT] Unsupported BMP format: %d bpp (need 24 or 32)\r\n", bpp);
        file.Close();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (infoHeader.compression != 0)  // BI_RGB = 0
    {
        Log("[BOOT] Compressed BMP not supported\r\n");
        file.Close();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    Log("[BOOT] Logo: %dx%d, %d bpp\r\n", imgWidth, imgHeight, bpp);
    
    // Get display dimensions
    uint16_t screenW = display.GetWidth();
    uint16_t screenH = display.GetHeight();
    
    // Calculate centered position
    int32_t offsetX = (screenW - imgWidth) / 2;
    int32_t offsetY = (screenH - imgHeight) / 2;
    
    // Clamp to screen bounds
    int32_t srcStartX = 0, srcStartY = 0;
    int32_t dstStartX = offsetX, dstStartY = offsetY;
    int32_t drawWidth = imgWidth, drawHeight = imgHeight;
    
    if (offsetX < 0)
    {
        srcStartX = -offsetX;
        dstStartX = 0;
        drawWidth = imgWidth + offsetX;
    }
    if (offsetY < 0)
    {
        srcStartY = -offsetY;
        dstStartY = 0;
        drawHeight = imgHeight + offsetY;
    }
    if (dstStartX + drawWidth > screenW)
    {
        drawWidth = screenW - dstStartX;
    }
    if (dstStartY + drawHeight > screenH)
    {
        drawHeight = screenH - dstStartY;
    }
    
    if (drawWidth <= 0 || drawHeight <= 0)
    {
        Log("[BOOT] Logo dimensions invalid after clamping\r\n");
        file.Close();
        return Result::RESULT_BAD_PARAMETER;
    }
    
    // Calculate row size (padded to 4-byte boundary)
    uint32_t bytesPerPixel = bpp / 8;
    uint32_t rowSize = ((imgWidth * bytesPerPixel + 3) / 4) * 4;
    
    // Allocate row buffer
    uint8_t* rowBuffer = (uint8_t*)HeapAllocator::Allocate(rowSize);
    if (!rowBuffer)
    {
        Log("[BOOT] Failed to allocate row buffer (%u bytes)\r\n", rowSize);
        file.Close();
        return Result::RESULT_NO_MEMORY;
    }
    
    // Clear display to black first
    uint32_t* fb = display.GetBackBuffer();
    for (uint32_t i = 0; i < (uint32_t)(screenW * screenH); i++)
    {
        fb[i] = 0xFF000000;  // Black with full alpha
    }
    
    // Seek to pixel data
    file.Seek(fileHeader.dataOffset);
    
    // Read and draw each row
    for (int32_t row = 0; row < imgHeight; row++)
    {
        // For bottom-up BMP, read rows in reverse
        int32_t srcRow = topDown ? row : (imgHeight - 1 - row);
        int32_t dstRow;
        
        if (topDown)
        {
            dstRow = row;
        }
        else
        {
            dstRow = row;  // We're reading in reverse, so dstRow = row
        }
        
        // Skip rows outside visible area
        if (dstRow < srcStartY || dstRow >= srcStartY + drawHeight)
        {
            // Still need to read to advance file position for bottom-up BMPs
            if (!topDown)
            {
                continue;  // Seek is complex, just continue
            }
            file.Read(rowBuffer, rowSize, &bytesRead);
            continue;
        }
        
        // Seek to correct row position
        uint32_t rowOffset = fileHeader.dataOffset + srcRow * rowSize;
        file.Seek(rowOffset);
        
        // Read row
        if (file.Read(rowBuffer, rowSize, &bytesRead) != Result::RESULT_SUCCESS)
        {
            continue;
        }
        
        // Calculate destination row on screen
        int32_t screenRow = dstStartY + (dstRow - srcStartY);
        
        // Convert and copy pixels
        for (int32_t col = srcStartX; col < srcStartX + drawWidth; col++)
        {
            uint32_t pixelOffset = col * bytesPerPixel;
            uint8_t b = rowBuffer[pixelOffset + 0];
            uint8_t g = rowBuffer[pixelOffset + 1];
            uint8_t r = rowBuffer[pixelOffset + 2];
            uint8_t a = (bpp == 32) ? rowBuffer[pixelOffset + 3] : 0xFF;
            
            uint32_t color = (a << 24) | (r << 16) | (g << 8) | b;
            
            int32_t screenCol = dstStartX + (col - srcStartX);
            fb[screenRow * screenW + screenCol] = color;
        }
    }
    
    HeapAllocator::Free(rowBuffer);
    file.Close();
    
    // Flush cache and swap buffers to display the logo
    display.FlushCache();
    display.SwapBuffers();
    
    Log("[BOOT] Boot logo displayed\r\n");
    
    // Wait for the configured display time
    if (displayMs > 0)
    {
        Task::Delay(displayMs);
    }
    
    return Result::RESULT_SUCCESS;
}

} // namespace CRTOS
