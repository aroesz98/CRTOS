/*
 * Syscall_IO.cpp - CRTOS System Call I/O Implementations
 * Author: Arkadiusz Szlanta
 * Date: 27 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * Implementation of I/O related system calls (open, close, read, write, ioctl).
 * Routes file operations to FatFS via HAL::FileSystem.
 */

#include "SystemCall.hpp"
#include "../CRTOS.hpp"
#include "../HAL/FileSystem.hpp"
#include "fsl_debug_console.h"
#include <cstring>
#include <cstdio>

namespace CRTOS
{
    using namespace Syscall; // For SyscallError and other types
    
    // Maximum number of file descriptors per process
    #define MAX_FILE_DESCRIPTORS 16
    
    // Special file descriptors
    #define STDIN_FD  0
    #define STDOUT_FD 1
    #define STDERR_FD 2
    
    // File descriptor type
    enum class FdType : uint8_t
    {
        None = 0,
        Console,    // stdin/stdout/stderr
        Device,     // Device driver
        File        // FatFS file
    };
    
    // File descriptor structure with FatFS support
    struct FileDescriptor
    {
        bool inUse;
        FdType type;
        char name[64];
        uint32_t flags;
        HAL::File* file;      // FatFS file handle (allocated dynamically)
        void* devicePrivate;  // Device-specific data
    };
    
    // Per-process file descriptor table (simplified - using global for now)
    // TODO: Move to ProcessControlBlock when implemented
    static FileDescriptor g_fdTable[MAX_FILE_DESCRIPTORS] = {
        {true, FdType::Console, "stdin", O_RDONLY, nullptr, nullptr},   // FD 0: stdin
        {true, FdType::Console, "stdout", O_WRONLY, nullptr, nullptr},  // FD 1: stdout
        {true, FdType::Console, "stderr", O_WRONLY, nullptr, nullptr},  // FD 2: stderr
    };
    
    //
    // Find a free file descriptor
    //
    static int32_t allocate_fd(void)
    {
        for (int32_t fd = 3; fd < MAX_FILE_DESCRIPTORS; fd++)
        {
            if (!g_fdTable[fd].inUse)
            {
                return fd;
            }
        }
        return -1; // No free FDs
    }
    
    //
    // Validate file descriptor
    //
    static bool is_valid_fd(int32_t fd)
    {
        return (fd >= 0 && fd < MAX_FILE_DESCRIPTORS && g_fdTable[fd].inUse);
    }
    
    //
    // Check if path is a file path (not a device)
    // File paths: "0:/...", "/...", or paths without "dev:" prefix
    //
    static bool is_file_path(const char* path)
    {
        if (!path || path[0] == '\0')
            return false;
        
        // FatFS drive path "0:/" or "1:/"
        if (path[0] >= '0' && path[0] <= '9' && path[1] == ':')
            return true;
        
        // Absolute path starting with /
        if (path[0] == '/')
            return true;
        
        // Check for device prefix "dev:"
        if (path[0] == 'd' && path[1] == 'e' && path[2] == 'v' && path[3] == ':')
            return false;
        
        // Default: treat as relative file path
        return true;
    }
    
    //
    // Convert syscall flags to HAL::FileMode
    //
    static uint8_t flags_to_filemode(int32_t flags)
    {
        uint8_t mode = 0;
        
        // Access mode
        if ((flags & O_RDWR) == O_RDWR)
            mode = static_cast<uint8_t>(HAL::FileMode::ReadWrite);
        else if (flags & O_WRONLY)
            mode = static_cast<uint8_t>(HAL::FileMode::Write);
        else
            mode = static_cast<uint8_t>(HAL::FileMode::Read);
        
        // Create flags
        if (flags & O_CREAT)
        {
            if (flags & O_TRUNC)
                mode |= static_cast<uint8_t>(HAL::FileMode::CreateAlways);
            else
                mode |= static_cast<uint8_t>(HAL::FileMode::OpenAlways);
        }
        
        // Append mode
        if (flags & O_APPEND)
            mode = static_cast<uint8_t>(HAL::FileMode::OpenAppend);
        
        return mode;
    }
    
    //
    // SYS_OPEN: Open a device/file
    //
    int32_t sys_open(const char* path, int32_t flags, int32_t mode)
    {
        // Validate path pointer
        if (path == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_FAULT);
        }
        
        // TODO: Validate path is in user-accessible memory (MPU check)
        (void)mode;  // Unix mode bits not used for FatFS
        
        // Allocate a file descriptor
        int32_t fd = allocate_fd();
        if (fd < 0)
        {
            return static_cast<int32_t>(SyscallError::ERR_MFILE); // Too many open files
        }
        
        FileDescriptor& fdesc = g_fdTable[fd];
        fdesc.inUse = true;
        fdesc.flags = static_cast<uint32_t>(flags);
        fdesc.file = nullptr;
        fdesc.devicePrivate = nullptr;
        strncpy(fdesc.name, path, sizeof(fdesc.name) - 1);
        fdesc.name[sizeof(fdesc.name) - 1] = '\0';
        
        // Check if this is a file path or device path
        if (is_file_path(path))
        {
            // File path - use FatFS via HAL
            HAL::FileSystem& fs = HAL::GetFileSystem();
            
            if (!fs.IsMounted())
            {
                fdesc.inUse = false;
                return static_cast<int32_t>(SyscallError::ERR_NOENT);
            }
            
            // Build full path if relative
            char fullPath[256];
            if (path[0] >= '0' && path[0] <= '9' && path[1] == ':')
            {
                // Absolute FatFS path
                strncpy(fullPath, path, sizeof(fullPath) - 1);
                fullPath[sizeof(fullPath) - 1] = '\0';
            }
            else if (path[0] == '/')
            {
                // Root-relative path - prepend drive
                snprintf(fullPath, sizeof(fullPath), "0:%s", path);
            }
            else
            {
                // Relative path - prepend drive and /
                snprintf(fullPath, sizeof(fullPath), "0:/%s", path);
            }
            
            // Allocate HAL::File object
            fdesc.file = new HAL::File();
            if (!fdesc.file)
            {
                fdesc.inUse = false;
                return static_cast<int32_t>(SyscallError::ERR_NOMEM);
            }
            
            // Convert flags to FatFS mode
            uint8_t fileMode = flags_to_filemode(flags);
            
            // Open the file
            Result res = fs.Open(*fdesc.file, fullPath, fileMode);
            if (res != Result::RESULT_SUCCESS)
            {
                delete fdesc.file;
                fdesc.file = nullptr;
                fdesc.inUse = false;
                
                // Map result to errno
                if (res == Result::RESULT_NOT_FOUND)
                    return static_cast<int32_t>(SyscallError::ERR_NOENT);
                else if (res == Result::RESULT_DENIED)
                    return static_cast<int32_t>(SyscallError::ERR_ACCES);
                else
                    return static_cast<int32_t>(SyscallError::ERR_IO);
            }
            
            fdesc.type = FdType::File;
        }
        else
        {
            // Device path - use device driver framework
            fdesc.type = FdType::Device;
            // TODO: Look up device in DeviceManager and call device->open()
        }
        
        return fd;
    }
    
    //
    // SYS_CLOSE: Close a file descriptor
    //
    int32_t sys_close(int32_t fd)
    {
        // Validate FD
        if (!is_valid_fd(fd))
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        // Don't allow closing stdin/stdout/stderr
        if (fd <= 2)
        {
            return static_cast<int32_t>(SyscallError::ERR_PERM);
        }
        
        FileDescriptor& fdesc = g_fdTable[fd];
        
        // Close based on type
        if (fdesc.type == FdType::File && fdesc.file != nullptr)
        {
            // Close FatFS file
            fdesc.file->Close();
            delete fdesc.file;
            fdesc.file = nullptr;
        }
        else if (fdesc.type == FdType::Device)
        {
            // TODO: Call device->close() when device framework exists
        }
        
        // Mark as free
        fdesc.inUse = false;
        fdesc.type = FdType::None;
        fdesc.devicePrivate = nullptr;
        
        return 0; // Success
    }
    
    //
    // SYS_READ: Read from file descriptor
    //
    int32_t sys_read(int32_t fd, void* buffer, uint32_t count)
    {
        // Validate parameters
        if (!is_valid_fd(fd))
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        if (buffer == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_FAULT);
        }
        
        if (count == 0)
        {
            return 0;
        }
        
        // TODO: Validate buffer is in user-accessible memory (MPU check)
        
        FileDescriptor& fdesc = g_fdTable[fd];
        
        // Check if FD is readable (not write-only)
        if ((fdesc.flags & O_WRONLY) != 0 && (fdesc.flags & O_RDWR) == 0)
        {
            return static_cast<int32_t>(SyscallError::ERR_ACCES);
        }
        
        if (fdesc.type == FdType::File && fdesc.file != nullptr)
        {
            // Read from FatFS file
            uint32_t bytesRead = 0;
            Result res = fdesc.file->Read(buffer, count, &bytesRead);
            
            if (res != Result::RESULT_SUCCESS)
            {
                return static_cast<int32_t>(SyscallError::ERR_IO);
            }
            
            return static_cast<int32_t>(bytesRead);
        }
        else if (fdesc.type == FdType::Console)
        {
            // TODO: Implement stdin reading if needed
            return static_cast<int32_t>(SyscallError::ERR_NOSYS);
        }
        else if (fdesc.type == FdType::Device)
        {
            // TODO: Call device->read() when device framework exists
            return static_cast<int32_t>(SyscallError::ERR_NOSYS);
        }
        
        return static_cast<int32_t>(SyscallError::ERR_BADF);
    }
    
    //
    // SYS_WRITE: Write to file descriptor
    //
    int32_t sys_write(int32_t fd, const void* buffer, uint32_t count)
    {
        // Validate parameters
        if (!is_valid_fd(fd))
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        if (buffer == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_FAULT);
        }
        
        if (count == 0)
        {
            return 0; // Nothing to write
        }
        
        // TODO: Validate buffer is in user-accessible memory (MPU check)
        
        FileDescriptor& fdesc = g_fdTable[fd];
        
        // Check if FD is writable
        if ((fdesc.flags & (O_WRONLY | O_RDWR)) == 0)
        {
            return static_cast<int32_t>(SyscallError::ERR_ACCES);
        }
        
        if (fdesc.type == FdType::File && fdesc.file != nullptr)
        {
            // Write to FatFS file
            uint32_t bytesWritten = 0;
            Result res = fdesc.file->Write(buffer, count, &bytesWritten);
            
            if (res != Result::RESULT_SUCCESS)
            {
                return static_cast<int32_t>(SyscallError::ERR_IO);
            }
            
            return static_cast<int32_t>(bytesWritten);
        }
        else if (fdesc.type == FdType::Console)
        {
            // Write to debug console (stdout/stderr)
            // Using PUTCHAR which runs in privileged context (SVC handler)
            if (fd == STDOUT_FD || fd == STDERR_FD)
            {
                const char* str = static_cast<const char*>(buffer);
                
                // Print character by character using PUTCHAR
                // This is called from SVC handler which is privileged
                for (size_t i = 0; i < count; i++)
                {
                    PUTCHAR(str[i]);
                }
                
                return static_cast<int32_t>(count);
            }
            return static_cast<int32_t>(SyscallError::ERR_NOSYS);
        }
        else if (fdesc.type == FdType::Device)
        {
            // TODO: Call device->write() when device framework exists
            return static_cast<int32_t>(SyscallError::ERR_NOSYS);
        }
        
        return static_cast<int32_t>(SyscallError::ERR_BADF);
    }
    
    //
    // SYS_IOCTL: Device-specific control operations
    //
    int32_t sys_ioctl(int32_t fd, uint32_t cmd, void* arg)
    {
        // Validate FD
        if (!is_valid_fd(fd))
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        // TODO: Validate arg pointer is in user-accessible memory (MPU check)
        
        // TODO: Call device->ioctl() when device framework exists
        // For now, return "not implemented"
        return static_cast<int32_t>(SyscallError::ERR_NOSYS);
    }
    
    //
    // SYS_LSEEK: Seek in file
    //
    int32_t sys_lseek(int32_t fd, int32_t offset, int32_t whence)
    {
        // Validate FD
        if (!is_valid_fd(fd))
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        FileDescriptor& fdesc = g_fdTable[fd];
        
        // Only files support seeking
        if (fdesc.type != FdType::File || fdesc.file == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_SPIPE);  // Illegal seek
        }
        
        uint32_t newPos = 0;
        uint32_t curPos = fdesc.file->Tell();
        uint32_t fileSize = fdesc.file->Size();
        
        switch (whence)
        {
            case SeekWhence::WHENCE_SET:
                if (offset < 0)
                    return static_cast<int32_t>(SyscallError::ERR_INVAL);
                newPos = static_cast<uint32_t>(offset);
                break;
                
            case SeekWhence::WHENCE_CUR:
                if (offset < 0 && static_cast<uint32_t>(-offset) > curPos)
                    return static_cast<int32_t>(SyscallError::ERR_INVAL);
                newPos = curPos + offset;
                break;
                
            case SeekWhence::WHENCE_END:
                if (offset < 0 && static_cast<uint32_t>(-offset) > fileSize)
                    return static_cast<int32_t>(SyscallError::ERR_INVAL);
                newPos = fileSize + offset;
                break;
                
            default:
                return static_cast<int32_t>(SyscallError::ERR_INVAL);
        }
        
        Result res = fdesc.file->Seek(newPos);
        if (res != Result::RESULT_SUCCESS)
        {
            return static_cast<int32_t>(SyscallError::ERR_IO);
        }
        
        return static_cast<int32_t>(newPos);
    }
    
    //
    // SYS_FSTAT: Get file status
    //
    int32_t sys_fstat(int32_t fd, Syscall::DirEntry* stat)
    {
        // Validate FD
        if (!is_valid_fd(fd))
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        if (stat == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_FAULT);
        }
        
        FileDescriptor& fdesc = g_fdTable[fd];
        
        // Only files support fstat
        if (fdesc.type != FdType::File || fdesc.file == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        // Fill in stat info
        strncpy(stat->name, fdesc.name, sizeof(stat->name) - 1);
        stat->name[sizeof(stat->name) - 1] = '\0';
        stat->size = fdesc.file->Size();
        stat->isDirectory = 0;  // It's an open file, not a directory
        
        return 0;
    }
    
    //
    // SYS_FSYNC: Sync file to disk
    //
    int32_t sys_fsync(int32_t fd)
    {
        // Validate FD
        if (!is_valid_fd(fd))
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        FileDescriptor& fdesc = g_fdTable[fd];
        
        // Only files support fsync
        if (fdesc.type != FdType::File || fdesc.file == nullptr)
        {
            return static_cast<int32_t>(SyscallError::ERR_BADF);
        }
        
        Result res = fdesc.file->Sync();
        if (res != Result::RESULT_SUCCESS)
        {
            return static_cast<int32_t>(SyscallError::ERR_IO);
        }
        
        return 0;
    }

} // namespace CRTOS
