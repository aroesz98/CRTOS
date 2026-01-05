/*
 * DisplayIoctl.hpp - Display Driver IOCTL Definitions
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * Description:
 * Shared IOCTL command definitions and structures for Display driver.
 * This header can be included by both the driver module and applications.
 */

#ifndef DISPLAY_IOCTL_HPP
#define DISPLAY_IOCTL_HPP

#include <stdint.h>
#include <stdbool.h>

// IOCTL base for display (matches DriverBase.hpp)
#ifndef IOCTL_BASE_DISPLAY
#define IOCTL_BASE_DISPLAY 0x0500
#endif

namespace CRTOS
{
namespace Drivers
{

// Display-specific IOCTL commands
enum class DisplayIoctl : uint32_t
{
    GET_WIDTH           = IOCTL_BASE_DISPLAY + 0,
    GET_HEIGHT          = IOCTL_BASE_DISPLAY + 1,
    GET_FORMAT          = IOCTL_BASE_DISPLAY + 2,
    GET_BYTES_PER_PIXEL = IOCTL_BASE_DISPLAY + 3,
    GET_FRAMEBUFFER     = IOCTL_BASE_DISPLAY + 4,
    GET_BACKBUFFER      = IOCTL_BASE_DISPLAY + 5,
    SWAP_BUFFERS        = IOCTL_BASE_DISPLAY + 6,
    FLUSH_CACHE         = IOCTL_BASE_DISPLAY + 7,
    WAIT_VSYNC          = IOCTL_BASE_DISPLAY + 8,
    CLEAR               = IOCTL_BASE_DISPLAY + 9,
    FILL_RECT           = IOCTL_BASE_DISPLAY + 10,
    ENABLE              = IOCTL_BASE_DISPLAY + 11,
    DISABLE             = IOCTL_BASE_DISPLAY + 12,
    GET_STATUS          = IOCTL_BASE_DISPLAY + 13,
};

/**
 * @brief Pixel format enumeration
 */
enum class DisplayPixelFormat : uint8_t
{
    RGB565 = 0,
    RGB888 = 1,
    XRGB8888 = 2,
};

/**
 * @brief Rectangle structure for drawing operations
 */
struct DisplayRect
{
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
};

/**
 * @brief Clear display argument
 */
struct DisplayClearArg
{
    uint32_t color;
};

/**
 * @brief Fill rectangle argument
 */
struct DisplayFillRectArg
{
    DisplayRect rect;
    uint32_t color;
};

/**
 * @brief VSYNC wait argument
 */
struct DisplayVSyncArg
{
    uint32_t timeout_ms;
    bool success;  // Output: true if VSYNC occurred
};

/**
 * @brief Display status flags
 */
enum DisplayStatusFlags : uint32_t
{
    DISPLAY_STATUS_NOT_INITIALIZED = 0,
    DISPLAY_STATUS_INITIALIZED     = (1 << 0),
    DISPLAY_STATUS_ENABLED         = (1 << 1),
    DISPLAY_STATUS_DOUBLE_BUFFER   = (1 << 2),
    DISPLAY_STATUS_SWAP_PENDING    = (1 << 3),
};

} // namespace Drivers
} // namespace CRTOS

#endif // DISPLAY_IOCTL_HPP
