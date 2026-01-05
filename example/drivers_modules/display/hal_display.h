/*
 * hal_display.h - Display HAL Interface for Driver Module
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * Description:
 * Local HAL interface header for Display driver module.
 * This allows the driver to be compiled separately while
 * still linking to the kernel's HAL implementation.
 * 
 * This is a pure HAL - no kernel dependencies.
 * Display is a singleton (one eLCDIF controller).
 */

#ifndef DRIVER_HAL_DISPLAY_H
#define DRIVER_HAL_DISPLAY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pixel format enumeration
 */
typedef enum {
    HAL_DISPLAY_FORMAT_RGB565 = 0,   // 16-bit RGB565
    HAL_DISPLAY_FORMAT_RGB888 = 1,   // 24-bit RGB888 packed
    HAL_DISPLAY_FORMAT_XRGB8888 = 2, // 32-bit XRGB8888
} hal_display_format_t;

/**
 * @brief Display configuration structure
 */
typedef struct {
    uint16_t width;          // Panel width in pixels
    uint16_t height;         // Panel height in pixels
    uint8_t hsw;             // Horizontal sync width
    uint8_t hfp;             // Horizontal front porch
    uint8_t hbp;             // Horizontal back porch
    uint8_t vsw;             // Vertical sync width
    uint8_t vfp;             // Vertical front porch
    uint8_t vbp;             // Vertical back porch
    uint32_t polarityFlags;  // Signal polarity flags
    hal_display_format_t pixelFormat; // Pixel format
    bool doubleBuffer;       // Enable double buffering
} hal_display_config_t;

/**
 * @brief Rectangle structure for drawing operations
 */
typedef struct {
    uint16_t x;      // X coordinate
    uint16_t y;      // Y coordinate
    uint16_t width;  // Width in pixels
    uint16_t height; // Height in pixels
} hal_display_rect_t;

/**
 * @brief Display status flags
 */
typedef enum {
    HAL_DISPLAY_STATUS_NOT_INITIALIZED = 0,
    HAL_DISPLAY_STATUS_INITIALIZED     = (1 << 0),
    HAL_DISPLAY_STATUS_ENABLED         = (1 << 1),
    HAL_DISPLAY_STATUS_DOUBLE_BUFFER   = (1 << 2),
    HAL_DISPLAY_STATUS_SWAP_PENDING    = (1 << 3),
} hal_display_status_t;

// ============================================================================
// Initialization API
// ============================================================================

/**
 * @brief Get default configuration (480x272 LCD, XRGB8888, double buffered)
 */
__attribute__((weak)) void hal_display_get_default_config(hal_display_config_t* config);

/**
 * @brief Initialize display with configuration
 * @return true on success
 */
__attribute__((weak)) bool hal_display_init(const hal_display_config_t* config);

/**
 * @brief Deinitialize display and release resources
 */
__attribute__((weak)) void hal_display_deinit(void);

/**
 * @brief Check if display is initialized
 */
__attribute__((weak)) bool hal_display_is_initialized(void);

/**
 * @brief Enable display output
 */
__attribute__((weak)) void hal_display_enable(void);

/**
 * @brief Disable display output
 */
__attribute__((weak)) void hal_display_disable(void);

// ============================================================================
// Frame Buffer API
// ============================================================================

/**
 * @brief Get front buffer address (currently displayed)
 */
__attribute__((weak)) uint32_t* hal_display_get_framebuffer(void);

/**
 * @brief Get back buffer address (for drawing in double-buffered mode)
 */
__attribute__((weak)) uint32_t* hal_display_get_backbuffer(void);

/**
 * @brief Swap front and back buffers (on VSYNC)
 * @return true on success
 */
__attribute__((weak)) bool hal_display_swap_buffers(void);

/**
 * @brief Flush D-cache for current drawing buffer
 */
__attribute__((weak)) void hal_display_flush_cache(void);

// ============================================================================
// Drawing API
// ============================================================================

/**
 * @brief Clear entire display with color
 */
__attribute__((weak)) void hal_display_clear(uint32_t color);

/**
 * @brief Fill rectangle with color
 */
__attribute__((weak)) void hal_display_fill_rect(const hal_display_rect_t* rect, uint32_t color);

/**
 * @brief Draw single pixel
 */
__attribute__((weak)) void hal_display_draw_pixel(uint16_t x, uint16_t y, uint32_t color);

/**
 * @brief Get pixel color at coordinates
 */
__attribute__((weak)) uint32_t hal_display_get_pixel(uint16_t x, uint16_t y);

// ============================================================================
// Synchronization API
// ============================================================================

/**
 * @brief Wait for vertical blank (VSYNC)
 * @param timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return true on VSYNC, false on timeout
 */
__attribute__((weak)) bool hal_display_wait_vsync(uint32_t timeout_ms);

/**
 * @brief Get IRQ number for LCDIF (for DPC registration)
 */
__attribute__((weak)) uint32_t hal_display_get_irq_number(void);

// ============================================================================
// Info API
// ============================================================================

/**
 * @brief Get display width
 */
__attribute__((weak)) uint16_t hal_display_get_width(void);

/**
 * @brief Get display height
 */
__attribute__((weak)) uint16_t hal_display_get_height(void);

/**
 * @brief Get pixel format
 */
__attribute__((weak)) hal_display_format_t hal_display_get_format(void);

/**
 * @brief Get bytes per pixel
 */
__attribute__((weak)) uint32_t hal_display_get_bytes_per_pixel(void);

/**
 * @brief Get current status flags
 */
__attribute__((weak)) uint32_t hal_display_get_status(void);

#ifdef __cplusplus
}
#endif

#endif // DRIVER_HAL_DISPLAY_H
