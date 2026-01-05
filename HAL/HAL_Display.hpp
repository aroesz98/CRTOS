/*
 * HAL_Display.hpp - Hardware Abstraction Layer for Display (C Interface)
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * C-linkage interface for Display HAL, used for symbol export to loadable modules.
 * These functions wrap the C++ Display class (GlobalDisplay singleton).
 */

#ifndef HAL_DISPLAY_HPP
#define HAL_DISPLAY_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Pixel format enumeration (matches Display::PixelFormat)
 */
typedef enum {
    HAL_DISPLAY_FORMAT_RGB565 = 0,
    HAL_DISPLAY_FORMAT_RGB888 = 1,
    HAL_DISPLAY_FORMAT_XRGB8888 = 2,
} hal_display_format_t;

/**
 * @brief Display configuration structure
 */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint8_t hsw;
    uint8_t hfp;
    uint8_t hbp;
    uint8_t vsw;
    uint8_t vfp;
    uint8_t vbp;
    uint32_t polarityFlags;
    hal_display_format_t pixelFormat;
    bool doubleBuffer;
} hal_display_config_t;

/**
 * @brief Rectangle structure for drawing operations
 */
typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
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

void hal_display_get_default_config(hal_display_config_t* config);
bool hal_display_init(const hal_display_config_t* config);
void hal_display_deinit(void);
bool hal_display_is_initialized(void);
void hal_display_enable(void);
void hal_display_disable(void);

// ============================================================================
// Frame Buffer API
// ============================================================================

uint32_t* hal_display_get_framebuffer(void);
uint32_t* hal_display_get_backbuffer(void);
bool hal_display_swap_buffers(void);
void hal_display_flush_cache(void);

// ============================================================================
// Drawing API
// ============================================================================

void hal_display_clear(uint32_t color);
void hal_display_fill_rect(const hal_display_rect_t* rect, uint32_t color);
void hal_display_draw_pixel(uint16_t x, uint16_t y, uint32_t color);
uint32_t hal_display_get_pixel(uint16_t x, uint16_t y);

// ============================================================================
// Synchronization API
// ============================================================================

bool hal_display_wait_vsync(uint32_t timeout_ms);
uint32_t hal_display_get_irq_number(void);

// ============================================================================
// Info API
// ============================================================================

uint16_t hal_display_get_width(void);
uint16_t hal_display_get_height(void);
hal_display_format_t hal_display_get_format(void);
uint32_t hal_display_get_bytes_per_pixel(void);
uint32_t hal_display_get_status(void);

#ifdef __cplusplus
}
#endif

#endif // HAL_DISPLAY_HPP
