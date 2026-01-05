/*
 * FileSystem.cpp - CRTOS Hardware Abstraction Layer - FAT File System Implementation
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
 * FAT file system implementation using FatFS library.
 */

#include "FileSystem.hpp"
#include "SDCard.hpp"
#include "diskio.h"
#include <cstring>
#include <cstdarg>
#include <cstdio>

namespace CRTOS
{
namespace HAL
{

// Internal FatFS objects - use global scope qualifier to avoid namespace conflicts
static ::FATFS s_fatfs;
static ::FIL s_filePool[4];       // Pool of file objects
static ::DIR s_dirPool[2];        // Pool of directory objects
static bool s_fileInUse[4] = {false};
static bool s_dirInUse[2] = {false};

// Convert FatFS result to CRTOS result
static Result FatFSResultToResult(FRESULT fres)
{
    switch (fres)
    {
        case FR_OK:
            return Result::RESULT_SUCCESS;
        case FR_NO_FILE:
        case FR_NO_PATH:
        case FR_INVALID_PARAMETER:
            return Result::RESULT_BAD_PARAMETER;
        case FR_DISK_ERR:
        case FR_INT_ERR:
        case FR_NOT_READY:
        case FR_NO_FILESYSTEM:
        case FR_INVALID_DRIVE:
            return Result::RESULT_BAD_PARAMETER;
        case FR_DENIED:
        case FR_EXIST:
        case FR_WRITE_PROTECTED:
        case FR_LOCKED:
            return Result::RESULT_SEMAPHORE_BUSY;
        case FR_TIMEOUT:
            return Result::RESULT_SEMAPHORE_TIMEOUT;
        case FR_NOT_ENOUGH_CORE:
            return Result::RESULT_NO_MEMORY;
        default:
            return Result::RESULT_BAD_PARAMETER;
    }
}

// Allocate a file object from pool
static ::FIL* AllocateFile()
{
    for (int i = 0; i < 4; i++)
    {
        if (!s_fileInUse[i])
        {
            s_fileInUse[i] = true;
            return &s_filePool[i];
        }
    }
    return nullptr;
}

// Free a file object back to pool
static void FreeFile(::FIL* fil)
{
    for (int i = 0; i < 4; i++)
    {
        if (&s_filePool[i] == fil)
        {
            s_fileInUse[i] = false;
            return;
        }
    }
}

// Allocate a directory object from pool
static ::DIR* AllocateDir()
{
    for (int i = 0; i < 2; i++)
    {
        if (!s_dirInUse[i])
        {
            s_dirInUse[i] = true;
            return &s_dirPool[i];
        }
    }
    return nullptr;
}

// Free a directory object back to pool
static void FreeDir(::DIR* dir)
{
    for (int i = 0; i < 2; i++)
    {
        if (&s_dirPool[i] == dir)
        {
            s_dirInUse[i] = false;
            return;
        }
    }
}

// ============================================================================
// File class implementation
// ============================================================================

File::File()
    : m_file(nullptr)
    , m_isOpen(false)
{
}

File::~File()
{
    if (m_isOpen)
    {
        Close();
    }
}

bool File::IsOpen() const
{
    return m_isOpen;
}

Result File::Read(void* buffer, uint32_t size, uint32_t* bytesRead)
{
    if (!m_isOpen || !buffer)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    UINT br = 0;
    FRESULT fres = f_read(static_cast<::FIL*>(m_file), buffer, size, &br);
    
    if (bytesRead)
    {
        *bytesRead = br;
    }
    
    return FatFSResultToResult(fres);
}

Result File::Write(const void* buffer, uint32_t size, uint32_t* bytesWritten)
{
    if (!m_isOpen || !buffer)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    UINT bw = 0;
    FRESULT fres = f_write(static_cast<::FIL*>(m_file), buffer, size, &bw);
    
    if (bytesWritten)
    {
        *bytesWritten = bw;
    }
    
    return FatFSResultToResult(fres);
}

Result File::Seek(uint32_t offset)
{
    if (!m_isOpen)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_lseek(static_cast<::FIL*>(m_file), offset);
    return FatFSResultToResult(fres);
}

uint32_t File::Tell() const
{
    if (!m_isOpen)
    {
        return 0;
    }
    
    return f_tell(static_cast<::FIL*>(m_file));
}

uint32_t File::Size() const
{
    if (!m_isOpen)
    {
        return 0;
    }
    
    return f_size(static_cast<::FIL*>(m_file));
}

bool File::Eof() const
{
    if (!m_isOpen)
    {
        return true;
    }
    
    return f_eof(static_cast<::FIL*>(m_file)) != 0;
}

Result File::Sync()
{
    if (!m_isOpen)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_sync(static_cast<::FIL*>(m_file));
    return FatFSResultToResult(fres);
}

Result File::Truncate()
{
    if (!m_isOpen)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_truncate(static_cast<::FIL*>(m_file));
    return FatFSResultToResult(fres);
}

Result File::Close()
{
    if (!m_isOpen)
    {
        return Result::RESULT_SUCCESS;
    }
    
    FRESULT fres = f_close(static_cast<::FIL*>(m_file));
    FreeFile(static_cast<::FIL*>(m_file));
    m_file = nullptr;
    m_isOpen = false;
    
    return FatFSResultToResult(fres);
}

char* File::ReadLine(char* buffer, int maxLen)
{
    if (!m_isOpen || !buffer || maxLen <= 0)
    {
        return nullptr;
    }
    
    return f_gets(buffer, maxLen, static_cast<::FIL*>(m_file));
}

int File::Printf(const char* format, ...)
{
    if (!m_isOpen || !format)
    {
        return -1;
    }
    
    // Format string into buffer first
    char buffer[256];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0)
    {
        UINT bw = 0;
        FRESULT fres = f_write(static_cast<::FIL*>(m_file), buffer, len, &bw);
        if (fres != FR_OK)
        {
            return -1;
        }
        return bw;
    }
    
    return len;
}

// ============================================================================
// Directory class implementation
// ============================================================================

Directory::Directory()
    : m_dir(nullptr)
    , m_isOpen(false)
{
}

Directory::~Directory()
{
    if (m_isOpen)
    {
        Close();
    }
}

bool Directory::IsOpen() const
{
    return m_isOpen;
}

Result Directory::ReadNext(FileInfo& info)
{
    if (!m_isOpen)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FILINFO fno;
    FRESULT fres = f_readdir(static_cast<::DIR*>(m_dir), &fno);
    
    if (fres != FR_OK)
    {
        return FatFSResultToResult(fres);
    }
    
    // End of directory
    if (fno.fname[0] == 0)
    {
        return Result::RESULT_QUEUE_EMPTY;  // No more entries
    }
    
    // Copy info
    info.size = fno.fsize;
    info.date = fno.fdate;
    info.time = fno.ftime;
    info.attributes = fno.fattrib;
    strncpy(info.name, fno.fname, sizeof(info.name) - 1);
    info.name[sizeof(info.name) - 1] = '\0';
    
#if FF_USE_LFN && FF_LFN_BUF >= 13
    strncpy(info.shortName, fno.altname, sizeof(info.shortName) - 1);
    info.shortName[sizeof(info.shortName) - 1] = '\0';
#else
    strncpy(info.shortName, fno.fname, sizeof(info.shortName) - 1);
    info.shortName[sizeof(info.shortName) - 1] = '\0';
#endif
    
    return Result::RESULT_SUCCESS;
}

Result Directory::Rewind()
{
    if (!m_isOpen)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_rewinddir(static_cast<::DIR*>(m_dir));
    return FatFSResultToResult(fres);
}

Result Directory::Close()
{
    if (!m_isOpen)
    {
        return Result::RESULT_SUCCESS;
    }
    
    FRESULT fres = f_closedir(static_cast<::DIR*>(m_dir));
    FreeDir(static_cast<::DIR*>(m_dir));
    m_dir = nullptr;
    m_isOpen = false;
    
    return FatFSResultToResult(fres);
}

// ============================================================================
// FileSystem class implementation
// ============================================================================

FileSystem::FileSystem()
    : m_fs(nullptr)
    , m_mounted(false)
{
}

FileSystem::~FileSystem()
{
    if (m_mounted)
    {
        Unmount();
    }
}

Result FileSystem::Mount(const char* path)
{
    if (m_mounted)
    {
        return Result::RESULT_SEMAPHORE_BUSY;
    }
    
    // SD card should already be initialized by application before mounting
    SDCard& sd = GetSDCard();
    if (sd.GetState() != SDCardState::Ready)
    {
        return Result::RESULT_BAD_PARAMETER;  // SD card not ready
    }
    
    // Mount file system
    FRESULT fres = f_mount(&s_fatfs, path, 1);  // 1 = mount now
    
    if (fres == FR_OK)
    {
        m_fs = &s_fatfs;
        m_mounted = true;
    }
    
    return FatFSResultToResult(fres);
}

Result FileSystem::Unmount(const char* path)
{
    if (!m_mounted)
    {
        return Result::RESULT_SUCCESS;
    }
    
    FRESULT fres = f_mount(nullptr, path, 0);
    m_fs = nullptr;
    m_mounted = false;
    
    return FatFSResultToResult(fres);
}

bool FileSystem::IsMounted() const
{
    return m_mounted;
}

Result FileSystem::Open(File& file, const char* path, uint8_t mode)
{
    if (!m_mounted)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (file.IsOpen())
    {
        file.Close();
    }
    
    ::FIL* fil = AllocateFile();
    if (!fil)
    {
        return Result::RESULT_NO_MEMORY;
    }
    
    FRESULT fres = f_open(fil, path, mode);
    
    if (fres == FR_OK)
    {
        file.m_file = fil;
        file.m_isOpen = true;
    }
    else
    {
        FreeFile(fil);
    }
    
    return FatFSResultToResult(fres);
}

Result FileSystem::OpenDir(Directory& dir, const char* path)
{
    if (!m_mounted)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    if (dir.IsOpen())
    {
        dir.Close();
    }
    
    ::DIR* d = AllocateDir();
    if (!d)
    {
        return Result::RESULT_NO_MEMORY;
    }
    
    FRESULT fres = f_opendir(d, path);
    
    if (fres == FR_OK)
    {
        dir.m_dir = d;
        dir.m_isOpen = true;
    }
    else
    {
        FreeDir(d);
    }
    
    return FatFSResultToResult(fres);
}

Result FileSystem::Stat(const char* path, FileInfo& info)
{
    if (!m_mounted)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    ::FILINFO fno;
    FRESULT fres = f_stat(path, &fno);
    
    if (fres == FR_OK)
    {
        info.size = fno.fsize;
        info.date = fno.fdate;
        info.time = fno.ftime;
        info.attributes = fno.fattrib;
        strncpy(info.name, fno.fname, sizeof(info.name) - 1);
        info.name[sizeof(info.name) - 1] = '\0';
        
#if FF_USE_LFN && FF_LFN_BUF >= 13
        strncpy(info.shortName, fno.altname, sizeof(info.shortName) - 1);
#else
        strncpy(info.shortName, fno.fname, sizeof(info.shortName) - 1);
#endif
        info.shortName[sizeof(info.shortName) - 1] = '\0';
    }
    
    return FatFSResultToResult(fres);
}

bool FileSystem::Exists(const char* path)
{
    if (!m_mounted)
    {
        return false;
    }
    
    ::FILINFO fno;
    return f_stat(path, &fno) == FR_OK;
}

Result FileSystem::MakeDir(const char* path)
{
    if (!m_mounted)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_mkdir(path);
    return FatFSResultToResult(fres);
}

Result FileSystem::Remove(const char* path)
{
    if (!m_mounted)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_unlink(path);
    return FatFSResultToResult(fres);
}

Result FileSystem::Rename(const char* oldPath, const char* newPath)
{
    if (!m_mounted)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_rename(oldPath, newPath);
    return FatFSResultToResult(fres);
}

Result FileSystem::SetAttributes(const char* path, uint8_t attr, uint8_t mask)
{
    if (!m_mounted)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_chmod(path, attr, mask);
    return FatFSResultToResult(fres);
}

Result FileSystem::GetStats(const char* path, FileSystemStats& stats)
{
    if (!m_mounted)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    ::FATFS* fs = nullptr;
    DWORD fre_clust = 0;
    FRESULT fres = f_getfree(path, &fre_clust, &fs);
    
    if (fres == FR_OK && fs)
    {
        // Calculate sizes
        uint32_t sectPerClust = fs->csize;
        uint32_t bytesPerSect = 512;  // Standard sector size
        
#if FF_MAX_SS != FF_MIN_SS
        bytesPerSect = fs->ssize;
#endif
        
        stats.clusterSize = sectPerClust * bytesPerSect;
        stats.freeClusters = fre_clust;
        stats.totalClusters = fs->n_fatent - 2;
        stats.totalBytes = (uint64_t)stats.totalClusters * stats.clusterSize;
        stats.freeBytes = (uint64_t)stats.freeClusters * stats.clusterSize;
    }
    
    return FatFSResultToResult(fres);
}

Result FileSystem::GetLabel(const char* path, char* label, uint32_t* serialNumber)
{
    if (!m_mounted || !label)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    DWORD sn = 0;
    FRESULT fres = f_getlabel(path, label, &sn);
    
    if (fres == FR_OK && serialNumber)
    {
        *serialNumber = sn;
    }
    
    return FatFSResultToResult(fres);
}

Result FileSystem::SetLabel(const char* label)
{
    if (!m_mounted || !label)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
    FRESULT fres = f_setlabel(label);
    return FatFSResultToResult(fres);
}

Result FileSystem::Format(const char* path, uint32_t clusterSize)
{
    // Note: Formatting destroys all data!
    BYTE work[FF_MAX_SS];
    MKFS_PARM opt = {0};
    
    opt.fmt = FM_ANY;           // Auto-select format
    opt.au_size = clusterSize;  // Cluster size (0 = auto)
    
    FRESULT fres = f_mkfs(path, &opt, work, sizeof(work));
    
    if (fres == FR_OK && m_mounted)
    {
        // Remount after format
        f_mount(nullptr, path, 0);
        m_mounted = false;
        
        fres = f_mount(&s_fatfs, path, 1);
        if (fres == FR_OK)
        {
            m_fs = &s_fatfs;
            m_mounted = true;
        }
    }
    
    return FatFSResultToResult(fres);
}

Result FileSystem::GetCwd(char* buffer, uint32_t size)
{
    if (!m_mounted || !buffer)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
#if FF_FS_RPATH >= 1
    FRESULT fres = f_getcwd(buffer, size);
    return FatFSResultToResult(fres);
#else
    strncpy(buffer, "0:/", size);
    return Result::RESULT_SUCCESS;
#endif
}

Result FileSystem::Chdir(const char* path)
{
    if (!m_mounted || !path)
    {
        return Result::RESULT_BAD_PARAMETER;
    }
    
#if FF_FS_RPATH >= 1
    FRESULT fres = f_chdir(path);
    return FatFSResultToResult(fres);
#else
    return Result::RESULT_BAD_PARAMETER;
#endif
}

// ============================================================================
// Singleton instance
// ============================================================================

static FileSystem s_fileSystem;

FileSystem& GetFileSystem()
{
    return s_fileSystem;
}

} // namespace HAL
} // namespace CRTOS
