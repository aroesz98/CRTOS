/*
 * SystemCall.hpp - CRTOS System Call Interface
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
 * System call interface providing the boundary between user space and kernel space.
 * All user applications must use these system calls to interact with the kernel.
 */

#ifndef CRTOS_SYSTEMCALL_HPP
#define CRTOS_SYSTEMCALL_HPP

#include <cstdint>
#include <cstddef>

namespace CRTOS
{
namespace Syscall
{
    // System call numbers (start at 100 to avoid conflict with CRTOS commands 0-23)
    enum class SyscallNumber : uint32_t
    {
        // Process management (100-109)
        SYS_EXIT = 100,         // Exit current process
        SYS_GETPID = 101,       // Get process ID
        SYS_YIELD = 102,        // Yield CPU to other processes
        SYS_SLEEP = 103,        // Sleep for specified ticks
        SYS_DROP_PRIVILEGES = 104, // Drop to user mode (can escalate back with elevate)
        SYS_GET_PRIVILEGE = 105,   // Check if running in privileged mode
        SYS_ELEVATE_PRIVILEGES = 106, // Temporarily elevate to privileged mode
        
        // Memory management (110-119)
        SYS_SBRK = 110,         // Allocate heap memory
        SYS_MALLOC = 111,       // Allocate memory (wrapper)
        SYS_FREE = 112,         // Free memory
        
        // I/O operations (120-129)
        SYS_OPEN = 120,         // Open device/file
        SYS_CLOSE = 121,        // Close file descriptor
        SYS_READ = 122,         // Read from file descriptor
        SYS_WRITE = 123,        // Write to file descriptor
        SYS_IOCTL = 124,        // Device-specific control
        SYS_LSEEK = 125,        // Seek in file
        SYS_FSTAT = 126,        // Get file status
        SYS_FSYNC = 127,        // Sync file to disk
        
        // Interrupt/Event handling (130-139)
        SYS_WAIT_IRQ = 130,     // Wait for interrupt event
        SYS_REGISTER_IRQ = 131, // Register for IRQ notifications
        SYS_UNREGISTER_IRQ = 132,// Unregister from IRQ
        SYS_POLL_IRQ = 133,     // Poll for pending IRQ events
        
        // IPC (Inter-Process Communication) (140-149)
        SYS_IPC_SEND = 140,     // Send message to another process
        SYS_IPC_RECV = 141,     // Receive message
        SYS_SHM_CREATE = 142,   // Create shared memory region
        SYS_SHM_ATTACH = 143,   // Attach to shared memory
        SYS_SHM_DETACH = 144,   // Detach from shared memory
        
        // Time (150-159)
        SYS_GET_TICK = 150,     // Get system tick count
        SYS_GET_TIME = 151,     // Get current time
        
        // Debug/Info (160-169)
        SYS_DEBUG_PRINT = 160,  // Debug output (only in debug builds)
        SYS_GET_PROCESS_INFO = 161, // Get process information
        SYS_GET_SYSTEM_INFO = 162,  // Get system information
        
        // Synchronization (170-179)
        SYS_MUTEX_CREATE = 170, // Create mutex
        SYS_MUTEX_LOCK = 171,   // Lock mutex
        SYS_MUTEX_UNLOCK = 172, // Unlock mutex
        SYS_MUTEX_DESTROY = 173,// Destroy mutex
        SYS_SEM_CREATE = 174,   // Create semaphore
        SYS_SEM_WAIT = 175,     // Wait on semaphore
        SYS_SEM_SIGNAL = 176,   // Signal semaphore
        SYS_SEM_DESTROY = 177,  // Destroy semaphore
        
        // Futex operations (180-189)
        SYS_FUTEX_WAIT = 180,       // Wait on futex if *uaddr == val
        SYS_FUTEX_WAKE = 181,       // Wake up to N waiters on futex
        SYS_FUTEX_REQUEUE = 182,    // Requeue waiters from one futex to another
        SYS_FUTEX_WAIT_BITSET = 183, // Wait with bitmask (advanced)
        SYS_FUTEX_WAKE_BITSET = 184, // Wake with bitmask (advanced)
        
        // Display operations (200-209)
        SYS_DISPLAY_CLEAR = 200,        // Clear display to color
        SYS_DISPLAY_FILL_RECT = 201,    // Fill rectangle
        SYS_DISPLAY_DRAW_PIXEL = 202,   // Draw single pixel
        SYS_DISPLAY_GET_PIXEL = 203,    // Read pixel color
        SYS_DISPLAY_SWAP = 204,         // Swap buffers
        SYS_DISPLAY_WAIT_VSYNC = 205,   // Wait for VSYNC
        SYS_DISPLAY_GET_FRAMEBUFFER = 206, // Get back buffer pointer
        SYS_DISPLAY_SET_CURSOR = 207,       // Set text cursor position
        SYS_DISPLAY_SET_TEXT_COLOR = 208,   // Set text foreground/background color
        SYS_DISPLAY_SET_TEXT_SIZE = 209,    // Set text size multiplier
        SYS_DISPLAY_DRAW_STRING = 210,      // Draw string at position
        SYS_DISPLAY_DRAW_CHAR = 211,        // Draw character at position
        SYS_DISPLAY_DRAW_NUMBER = 212,      // Draw number at position
        SYS_DISPLAY_GET_TEXT_WIDTH = 213,   // Get text width in pixels
        SYS_DISPLAY_GET_FONT_HEIGHT = 214,  // Get font height in pixels
        SYS_DISPLAY_DRAW_LINE = 215,        // Draw line
        SYS_DISPLAY_DRAW_RECT = 216,        // Draw rectangle outline
        SYS_DISPLAY_DRAW_CIRCLE = 217,      // Draw circle outline
        SYS_DISPLAY_FILL_CIRCLE = 218,      // Draw filled circle
        SYS_DISPLAY_SET_FONT = 219,         // Set font by ID
        
        // Filesystem operations (220-229)
        SYS_FS_OPENDIR = 220,           // Open directory
        SYS_FS_READDIR = 221,           // Read directory entry
        SYS_FS_CLOSEDIR = 222,          // Close directory
        SYS_FS_STAT = 223,              // Get file/directory info
        SYS_FS_GETCWD = 224,            // Get current working directory
        SYS_FS_CHDIR = 225,             // Change current directory
        
        // PXP Hardware Accelerator (230-239)
        SYS_PXP_SCALE = 230,            // Scale image using PXP hardware (sync)
        SYS_PXP_COPY = 231,             // Copy image region using PXP
        SYS_PXP_FILL = 232,             // Fill rectangle using PXP
        SYS_PXP_BLEND = 233,            // Alpha blend using PXP
        SYS_PXP_IS_BUSY = 234,          // Check if PXP is busy
        SYS_PXP_WAIT = 235,             // Wait for PXP completion (blocking)
        SYS_PXP_SCALE_ASYNC = 236,      // Start async scale operation (non-blocking)
        
        // Display extended operations (240-249)
        SYS_DISPLAY_SET_TARGET = 240,   // Set render target buffer (NULL = system FB)
        SYS_DISPLAY_GET_TARGET = 241,   // Get current render target
        
        SYS_MAX_SYSCALL = 250   // Maximum syscall number
    };

    // System call return codes (compatible with POSIX errno values)
    // Using ERR_ prefix to avoid conflicts with system errno macros
    enum class SyscallError : int32_t
    {
        ERR_SUCCESS = 0,            // No error
        ERR_PERM = -1,              // Operation not permitted
        ERR_NOENT = -2,             // No such file or directory
        ERR_SRCH = -3,              // No such process
        ERR_INTR = -4,              // Interrupted system call
        ERR_IO = -5,                // I/O error
        ERR_NXIO = -6,              // No such device or address
        ERR_2BIG = -7,              // Argument list too long
        ERR_BADF = -9,              // Bad file descriptor
        ERR_CHILD = -10,            // No child processes
        ERR_AGAIN = -11,            // Try again
        ERR_NOMEM = -12,            // Out of memory
        ERR_ACCES = -13,            // Permission denied
        ERR_FAULT = -14,            // Bad address
        ERR_BUSY = -16,             // Device or resource busy
        ERR_EXIST = -17,            // File exists
        ERR_NODEV = -19,            // No such device
        ERR_INVAL = -22,            // Invalid argument
        ERR_MFILE = -24,            // Too many open files
        ERR_SPIPE = -29,            // Illegal seek
        ERR_NOSYS = -38,            // Function not implemented
        ERR_TIMEDOUT = -110,        // Connection timed out
        ERR_WOULDBLOCK = -11,       // Operation would block (same as EAGAIN)
    };

    // IRQ Event structure passed to user space
    struct IRQEvent
    {
        uint32_t irqNumber;     // Which IRQ triggered
        uint32_t timestamp;     // System tick when IRQ occurred
        uint32_t data;          // Additional data (device-specific)
        void* context;          // Context pointer
    };

    // Process information structure
    struct ProcessInfo
    {
        uint32_t pid;           // Process ID
        char name[32];          // Process name
        uint32_t heapUsed;      // Heap memory used
        uint32_t heapSize;      // Total heap size
        uint32_t stackUsed;     // Stack used by main thread
        uint32_t stackSize;     // Total stack size
        uint32_t cpuTime;       // CPU time used (in ticks)
        uint32_t state;         // Process state
    };

    // System information structure
    struct SystemInfo
    {
        uint32_t totalMemory;   // Total system memory
        uint32_t freeMemory;    // Free memory
        uint32_t processCount;  // Number of processes
        uint32_t uptime;        // System uptime in ticks
        uint32_t cpuFreq;       // CPU frequency in Hz
        uint32_t tickRate;      // Tick rate in Hz
    };

    // Directory entry structure (for readdir)
    struct DirEntry
    {
        char name[256];         // File/directory name
        uint32_t size;          // File size in bytes
        uint8_t isDirectory;    // 1 if directory, 0 if file
        uint8_t reserved[3];    // Padding
    };

    // File flags (for sys_open)
    enum OpenFlags : uint32_t
    {
        O_RDONLY = 0x0001,      // Read only
        O_WRONLY = 0x0002,      // Write only
        O_RDWR = 0x0003,        // Read and write
        O_NONBLOCK = 0x0004,    // Non-blocking mode
        O_APPEND = 0x0008,      // Append mode
        O_CREAT = 0x0010,       // Create if not exists
        O_TRUNC = 0x0020,       // Truncate to zero length
    };

    // Seek whence values (for sys_lseek)
    enum SeekWhence : int32_t
    {
        WHENCE_SET = 0,         // Seek from beginning of file
        WHENCE_CUR = 1,         // Seek from current position
        WHENCE_END = 2,         // Seek from end of file
    };

    // IOCTL commands (device-specific)
    enum IOCTLCommand : uint32_t
    {
        IOCTL_GET_STATUS = 0x1000,
        IOCTL_SET_CONFIG = 0x1001,
        IOCTL_RESET = 0x1002,
        IOCTL_GET_INFO = 0x1003,
        
        // UART specific
        IOCTL_UART_SET_BAUDRATE = 0x2000,
        IOCTL_UART_GET_BAUDRATE = 0x2001,
        IOCTL_UART_SET_PARITY = 0x2002,
        
        // GPIO specific
        IOCTL_GPIO_SET_DIR = 0x3000,
        IOCTL_GPIO_GET_DIR = 0x3001,
        IOCTL_GPIO_SET_VALUE = 0x3002,
        IOCTL_GPIO_GET_VALUE = 0x3003,
    };

    //
    // Note: System call implementations are declared in Syscall_Handlers.hpp  
    // in namespace CRTOS (not CRTOS::Syscall)
    //

    //
    // SVC Handler - main entry point from SVC exception
    //
    // This function is called with parameters already extracted from registers
    // Returns result to be placed in r0
    //
    int32_t SVC_Dispatch(uint32_t syscallNum, uint32_t arg0, uint32_t arg1, 
                         uint32_t arg2, uint32_t arg3);

} // namespace Syscall
} // namespace CRTOS

#endif /* CRTOS_SYSTEMCALL_HPP */
