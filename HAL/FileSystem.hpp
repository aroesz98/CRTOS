/*
 * FileSystem.hpp - CRTOS Hardware Abstraction Layer - FAT File System
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
 * FAT file system layer using FatFS library.
 * Provides file operations for SD card storage.
 */

#ifndef CRTOS_HAL_FILESYSTEM_HPP
#define CRTOS_HAL_FILESYSTEM_HPP

#include <stdint.h>
#include "../CRTOS.hpp"

// Include FatFS header (uses stub if FatFS library not available)
extern "C" {
#include "ff.h"
}

namespace CRTOS
{
namespace HAL
{

/**
 * @brief File open modes
 */
enum class FileMode : uint8_t
{
    Read            = 0x01,     ///< Open for reading
    Write           = 0x02,     ///< Open for writing
    ReadWrite       = 0x03,     ///< Open for reading and writing
    CreateNew       = 0x04,     ///< Create new file (fail if exists)
    CreateAlways    = 0x08,     ///< Create new file (overwrite if exists)
    OpenAlways      = 0x10,     ///< Open existing or create new
    OpenAppend      = 0x30,     ///< Open and seek to end for append
};

/**
 * @brief File attributes
 */
enum class FileAttribute : uint8_t
{
    ReadOnly    = 0x01,
    Hidden      = 0x02,
    System      = 0x04,
    Directory   = 0x10,
    Archive     = 0x20,
};

/**
 * @brief File/Directory information
 */
struct FileInfo
{
    uint32_t size;              ///< File size in bytes
    uint16_t date;              ///< Modified date
    uint16_t time;              ///< Modified time
    uint8_t attributes;         ///< File attributes
    char name[256];             ///< File name (null-terminated)
    char shortName[13];         ///< 8.3 short name
};

/**
 * @brief File system statistics
 */
struct FileSystemStats
{
    uint64_t totalBytes;        ///< Total capacity
    uint64_t freeBytes;         ///< Free space
    uint32_t clusterSize;       ///< Cluster size in bytes
    uint32_t totalClusters;     ///< Total clusters
    uint32_t freeClusters;      ///< Free clusters
};

/**
 * @brief File handle for read/write operations
 */
class File
{
public:
    File();
    ~File();
    
    /**
     * @brief Check if file is open
     */
    bool IsOpen() const;
    
    /**
     * @brief Read data from file
     * 
     * @param buffer Destination buffer
     * @param size Number of bytes to read
     * @param bytesRead Actual bytes read (output)
     * @return Result::RESULT_SUCCESS on success
     */
    Result Read(void* buffer, uint32_t size, uint32_t* bytesRead);
    
    /**
     * @brief Write data to file
     * 
     * @param buffer Source buffer
     * @param size Number of bytes to write
     * @param bytesWritten Actual bytes written (output)
     * @return Result::RESULT_SUCCESS on success
     */
    Result Write(const void* buffer, uint32_t size, uint32_t* bytesWritten);
    
    /**
     * @brief Seek to position in file
     * 
     * @param offset Position from beginning of file
     * @return Result::RESULT_SUCCESS on success
     */
    Result Seek(uint32_t offset);
    
    /**
     * @brief Get current position in file
     * 
     * @return Current position
     */
    uint32_t Tell() const;
    
    /**
     * @brief Get file size
     * 
     * @return Size in bytes
     */
    uint32_t Size() const;
    
    /**
     * @brief Check if at end of file
     * 
     * @return true if at EOF
     */
    bool Eof() const;
    
    /**
     * @brief Flush pending writes to disk
     * 
     * @return Result::RESULT_SUCCESS on success
     */
    Result Sync();
    
    /**
     * @brief Truncate file at current position
     * 
     * @return Result::RESULT_SUCCESS on success
     */
    Result Truncate();
    
    /**
     * @brief Close the file
     * 
     * @return Result::RESULT_SUCCESS on success
     */
    Result Close();
    
    /**
     * @brief Read a line from text file
     * 
     * @param buffer Destination buffer
     * @param maxLen Maximum buffer size
     * @return Pointer to buffer, or nullptr on error/EOF
     */
    char* ReadLine(char* buffer, int maxLen);
    
    /**
     * @brief Write formatted string to file
     * 
     * @param format Printf-style format string
     * @return Number of characters written, or negative on error
     */
    int Printf(const char* format, ...);

private:
    friend class FileSystem;
    void* m_file;   // FIL* internally
    bool m_isOpen;
};

/**
 * @brief Directory handle for enumeration
 */
class Directory
{
public:
    Directory();
    ~Directory();
    
    /**
     * @brief Check if directory is open
     */
    bool IsOpen() const;
    
    /**
     * @brief Read next entry
     * 
     * @param info Output file info
     * @return Result::RESULT_SUCCESS on success, RESULT_ERROR at end
     */
    Result ReadNext(FileInfo& info);
    
    /**
     * @brief Rewind to first entry
     * 
     * @return Result::RESULT_SUCCESS on success
     */
    Result Rewind();
    
    /**
     * @brief Close the directory
     * 
     * @return Result::RESULT_SUCCESS on success
     */
    Result Close();

private:
    friend class FileSystem;
    void* m_dir;    // DIR* internally
    bool m_isOpen;
};

/**
 * @brief File system class
 * 
 * Provides FAT file system operations using FatFS library.
 */
class FileSystem
{
public:
    /**
     * @brief Constructor
     */
    FileSystem();
    
    /**
     * @brief Destructor
     */
    ~FileSystem();
    
    /**
     * @brief Mount file system
     * 
     * @param path Drive path (e.g., "0:" or "SD:")
     * @return Result::RESULT_SUCCESS on success
     */
    Result Mount(const char* path = "0:");
    
    /**
     * @brief Unmount file system
     * 
     * @param path Drive path
     * @return Result::RESULT_SUCCESS on success
     */
    Result Unmount(const char* path = "0:");
    
    /**
     * @brief Check if mounted
     * 
     * @return true if file system is mounted
     */
    bool IsMounted() const;
    
    /**
     * @brief Open a file
     * 
     * @param file File object to initialize
     * @param path File path
     * @param mode Open mode (see FileMode enum)
     * @return Result::RESULT_SUCCESS on success
     */
    Result Open(File& file, const char* path, uint8_t mode);
    
    /**
     * @brief Open a directory
     * 
     * @param dir Directory object to initialize
     * @param path Directory path
     * @return Result::RESULT_SUCCESS on success
     */
    Result OpenDir(Directory& dir, const char* path);
    
    /**
     * @brief Get file/directory information
     * 
     * @param path File or directory path
     * @param info Output file info
     * @return Result::RESULT_SUCCESS on success
     */
    Result Stat(const char* path, FileInfo& info);
    
    /**
     * @brief Check if file or directory exists
     * 
     * @param path Path to check
     * @return true if exists
     */
    bool Exists(const char* path);
    
    /**
     * @brief Create a directory
     * 
     * @param path Directory path
     * @return Result::RESULT_SUCCESS on success
     */
    Result MakeDir(const char* path);
    
    /**
     * @brief Delete a file or empty directory
     * 
     * @param path Path to delete
     * @return Result::RESULT_SUCCESS on success
     */
    Result Remove(const char* path);
    
    /**
     * @brief Rename or move a file/directory
     * 
     * @param oldPath Current path
     * @param newPath New path
     * @return Result::RESULT_SUCCESS on success
     */
    Result Rename(const char* oldPath, const char* newPath);
    
    /**
     * @brief Change file/directory attributes
     * 
     * @param path Path
     * @param attr Attributes to set
     * @param mask Attribute mask
     * @return Result::RESULT_SUCCESS on success
     */
    Result SetAttributes(const char* path, uint8_t attr, uint8_t mask);
    
    /**
     * @brief Get file system statistics
     * 
     * @param path Drive path
     * @param stats Output statistics
     * @return Result::RESULT_SUCCESS on success
     */
    Result GetStats(const char* path, FileSystemStats& stats);
    
    /**
     * @brief Get volume label
     * 
     * @param path Drive path
     * @param label Output buffer (min 12 chars)
     * @param serialNumber Output serial number (optional)
     * @return Result::RESULT_SUCCESS on success
     */
    Result GetLabel(const char* path, char* label, uint32_t* serialNumber = nullptr);
    
    /**
     * @brief Set volume label
     * 
     * @param label New label (max 11 chars)
     * @return Result::RESULT_SUCCESS on success
     */
    Result SetLabel(const char* label);
    
    /**
     * @brief Format the volume
     * 
     * @param path Drive path
     * @param clusterSize Cluster size in bytes (0 = auto)
     * @return Result::RESULT_SUCCESS on success
     */
    Result Format(const char* path, uint32_t clusterSize = 0);
    
    /**
     * @brief Get current directory
     * 
     * @param buffer Output buffer
     * @param size Buffer size
     * @return Result::RESULT_SUCCESS on success
     */
    Result GetCwd(char* buffer, uint32_t size);
    
    /**
     * @brief Change current directory
     * 
     * @param path New directory path
     * @return Result::RESULT_SUCCESS on success
     */
    Result Chdir(const char* path);

private:
    void* m_fs;     // FATFS* internally
    bool m_mounted;
};

/**
 * @brief Get global FileSystem instance
 * 
 * @return Reference to singleton FileSystem
 */
FileSystem& GetFileSystem();

// Convenience operators for FileMode
inline uint8_t operator|(FileMode a, FileMode b)
{
    return static_cast<uint8_t>(a) | static_cast<uint8_t>(b);
}

inline uint8_t operator|(uint8_t a, FileMode b)
{
    return a | static_cast<uint8_t>(b);
}

} // namespace HAL
} // namespace CRTOS

#endif // CRTOS_HAL_FILESYSTEM_HPP
