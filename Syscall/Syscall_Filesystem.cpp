/*
 * Syscall_Filesystem.cpp - Filesystem System Call Implementations
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
 * System call implementations for filesystem operations (directory listing).
 */

#include "Syscall_Handlers.hpp"
#include "SystemCall.hpp"
#include "../HAL/FileSystem.hpp"
#include <cstring>
#include <cstdio>

namespace CRTOS
{

// Maximum open directories per process
#define MAX_OPEN_DIRS 8

// Directory handle structure
struct DirHandle
{
    bool inUse;
    HAL::Directory dir;
    char path[256];
};

// Global directory handle table (shared for simplicity)
static DirHandle s_dirHandles[MAX_OPEN_DIRS];
static bool s_initialized = false;

// Current working directory
static char s_cwd[256] = "0:/";

static void InitDirHandles()
{
    if (!s_initialized)
    {
        for (int i = 0; i < MAX_OPEN_DIRS; i++)
        {
            s_dirHandles[i].inUse = false;
        }
        s_initialized = true;
    }
}

static int AllocDirHandle()
{
    InitDirHandles();
    for (int i = 0; i < MAX_OPEN_DIRS; i++)
    {
        if (!s_dirHandles[i].inUse)
        {
            s_dirHandles[i].inUse = true;
            return i;
        }
    }
    return -1;
}

static DirHandle* GetDirHandle(int32_t handle)
{
    if (handle < 0 || handle >= MAX_OPEN_DIRS)
        return nullptr;
    if (!s_dirHandles[handle].inUse)
        return nullptr;
    return &s_dirHandles[handle];
}

// Open a directory for reading
int32_t sys_fs_opendir(const char* path)
{
    if (!path)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
    
    HAL::FileSystem& fs = HAL::GetFileSystem();
    
    // Check if filesystem is mounted
    if (!fs.IsMounted())
        return static_cast<int32_t>(Syscall::SyscallError::ERR_NOENT);
    
    // Allocate a directory handle
    int handle = AllocDirHandle();
    if (handle < 0)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_MFILE);
    
    DirHandle* dh = &s_dirHandles[handle];
    
    // Build full path if relative
    char fullPath[256];
    if (path[0] == '0' && path[1] == ':')
    {
        // Absolute path
        strncpy(fullPath, path, sizeof(fullPath) - 1);
        fullPath[sizeof(fullPath) - 1] = '\0';
    }
    else if (path[0] == '/')
    {
        // Root-relative path
        snprintf(fullPath, sizeof(fullPath), "0:%s", path);
    }
    else
    {
        // Relative to CWD
        snprintf(fullPath, sizeof(fullPath), "%s/%s", s_cwd, path);
    }
    
    // Open the directory
    Result res = fs.OpenDir(dh->dir, fullPath);
    if (res != Result::RESULT_SUCCESS)
    {
        dh->inUse = false;
        return static_cast<int32_t>(Syscall::SyscallError::ERR_NOENT);
    }
    
    strncpy(dh->path, fullPath, sizeof(dh->path) - 1);
    dh->path[sizeof(dh->path) - 1] = '\0';
    
    return handle;
}

// Read next directory entry
int32_t sys_fs_readdir(int32_t dir_handle, Syscall::DirEntry* entry)
{
    if (!entry)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
    
    DirHandle* dh = GetDirHandle(dir_handle);
    if (!dh)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_BADF);
    
    HAL::FileInfo fileInfo;
    Result res = dh->dir.ReadNext(fileInfo);
    
    if (res != Result::RESULT_SUCCESS)
    {
        // End of directory or error
        return static_cast<int32_t>(Syscall::SyscallError::ERR_NOENT);
    }
    
    // Fill in the entry
    strncpy(entry->name, fileInfo.name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->size = fileInfo.size;
    entry->isDirectory = (fileInfo.attributes & static_cast<uint8_t>(HAL::FileAttribute::Directory)) ? 1 : 0;
    
    return 0;
}

// Close a directory
int32_t sys_fs_closedir(int32_t dir_handle)
{
    DirHandle* dh = GetDirHandle(dir_handle);
    if (!dh)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_BADF);
    
    dh->dir.Close();
    dh->inUse = false;
    
    return 0;
}

// Get file/directory info
int32_t sys_fs_stat(const char* path, Syscall::DirEntry* entry)
{
    if (!path || !entry)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
    
    HAL::FileSystem& fs = HAL::GetFileSystem();
    
    if (!fs.IsMounted())
        return static_cast<int32_t>(Syscall::SyscallError::ERR_NOENT);
    
    // Build full path if relative
    char fullPath[256];
    if (path[0] == '0' && path[1] == ':')
    {
        strncpy(fullPath, path, sizeof(fullPath) - 1);
        fullPath[sizeof(fullPath) - 1] = '\0';
    }
    else if (path[0] == '/')
    {
        snprintf(fullPath, sizeof(fullPath), "0:%s", path);
    }
    else
    {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", s_cwd, path);
    }
    
    HAL::FileInfo fileInfo;
    Result res = fs.Stat(fullPath, fileInfo);
    
    if (res != Result::RESULT_SUCCESS)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_NOENT);
    
    strncpy(entry->name, fileInfo.name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->size = fileInfo.size;
    entry->isDirectory = (fileInfo.attributes & static_cast<uint8_t>(HAL::FileAttribute::Directory)) ? 1 : 0;
    
    return 0;
}

// Get current working directory
int32_t sys_fs_getcwd(char* buffer, uint32_t size)
{
    if (!buffer || size == 0)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
    
    strncpy(buffer, s_cwd, size - 1);
    buffer[size - 1] = '\0';
    
    return 0;
}

// Change current working directory
int32_t sys_fs_chdir(const char* path)
{
    if (!path)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
    
    HAL::FileSystem& fs = HAL::GetFileSystem();
    
    if (!fs.IsMounted())
        return static_cast<int32_t>(Syscall::SyscallError::ERR_NOENT);
    
    // Build full path
    char fullPath[256];
    if (path[0] == '0' && path[1] == ':')
    {
        strncpy(fullPath, path, sizeof(fullPath) - 1);
        fullPath[sizeof(fullPath) - 1] = '\0';
    }
    else if (path[0] == '/')
    {
        snprintf(fullPath, sizeof(fullPath), "0:%s", path);
    }
    else if (strcmp(path, "..") == 0)
    {
        // Go up one directory
        strncpy(fullPath, s_cwd, sizeof(fullPath) - 1);
        fullPath[sizeof(fullPath) - 1] = '\0';
        
        // Find last slash and truncate
        char* lastSlash = strrchr(fullPath, '/');
        if (lastSlash && lastSlash != fullPath && lastSlash != fullPath + 2)
        {
            *lastSlash = '\0';
        }
        else
        {
            // Already at root
            strcpy(fullPath, "0:/");
        }
    }
    else
    {
        snprintf(fullPath, sizeof(fullPath), "%s/%s", s_cwd, path);
    }
    
    // Verify directory exists by trying to open it
    HAL::Directory testDir;
    Result res = fs.OpenDir(testDir, fullPath);
    if (res != Result::RESULT_SUCCESS)
        return static_cast<int32_t>(Syscall::SyscallError::ERR_NOENT);
    
    testDir.Close();
    
    // Update CWD
    strncpy(s_cwd, fullPath, sizeof(s_cwd) - 1);
    s_cwd[sizeof(s_cwd) - 1] = '\0';
    
    return 0;
}

} // namespace CRTOS
