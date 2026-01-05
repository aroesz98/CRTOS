/*
 * HAL_Display.cpp - Hardware Abstraction Layer for Display (C Wrappers)
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
 * C-linkage wrapper functions for Display class.
 * These are exported to loadable modules via ModuleLoader.
 */

#include "HAL_Display.hpp"
#include "Display.hpp"
#include "app.h"
#include "fsl_elcdif.h"

// LCDIF IRQ number from device header
#ifndef LCDIF_IRQn
#define LCDIF_IRQn 42
#endif

namespace
{
    // Convert C format to C++ format
    inline CRTOS::HAL::PixelFormat ConvertFormat(hal_display_format_t format)
    {
        switch (format)
        {
            case HAL_DISPLAY_FORMAT_RGB565:
                return CRTOS::HAL::PixelFormat::RGB565;
            case HAL_DISPLAY_FORMAT_RGB888:
                return CRTOS::HAL::PixelFormat::RGB888;
            case HAL_DISPLAY_FORMAT_XRGB8888:
            default:
                return CRTOS::HAL::PixelFormat::XRGB8888;
        }
    }
    
    // Convert C++ format to C format
    inline hal_display_format_t ConvertFormatToC(CRTOS::HAL::PixelFormat format)
    {
        switch (format)
        {
            case CRTOS::HAL::PixelFormat::RGB565:
                return HAL_DISPLAY_FORMAT_RGB565;
            case CRTOS::HAL::PixelFormat::RGB888:
                return HAL_DISPLAY_FORMAT_RGB888;
            case CRTOS::HAL::PixelFormat::XRGB8888:
            default:
                return HAL_DISPLAY_FORMAT_XRGB8888;
        }
    }
}

extern "C" {

// ============================================================================
// Initialization API
// ============================================================================

void hal_display_get_default_config(hal_display_config_t* config)
{
    if (config == nullptr)
    {
        return;
    }
    
    // Default configuration for 480x272 LCD panel (XRGB8888 format for TFTLIB compatibility)
    // Using values from board/app.h
    config->width = APP_IMG_WIDTH;
    config->height = APP_IMG_HEIGHT;
    config->hsw = APP_HSW;
    config->hfp = APP_HFP;
    config->hbp = APP_HBP;
    config->vsw = APP_VSW;
    config->vfp = APP_VFP;
    config->vbp = APP_VBP;
    config->polarityFlags = APP_POL_FLAGS;
    config->pixelFormat = HAL_DISPLAY_FORMAT_XRGB8888;  // TFTLIB uses 32-bit pixels
    config->doubleBuffer = true;
}

bool hal_display_init(const hal_display_config_t* config)
{
    if (config == nullptr)
    {
        return false;
    }
    
    CRTOS::HAL::DisplayConfig cppConfig;
    cppConfig.width = config->width;
    cppConfig.height = config->height;
    cppConfig.hsw = config->hsw;
    cppConfig.hfp = config->hfp;
    cppConfig.hbp = config->hbp;
    cppConfig.vsw = config->vsw;
    cppConfig.vfp = config->vfp;
    cppConfig.vbp = config->vbp;
    cppConfig.polarityFlags = config->polarityFlags;
    cppConfig.pixelFormat = ConvertFormat(config->pixelFormat);
    cppConfig.doubleBuffer = config->doubleBuffer;
    
    CRTOS::Result result = CRTOS::HAL::GlobalDisplay.Initialize(cppConfig);
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

void hal_display_deinit(void)
{
    CRTOS::HAL::GlobalDisplay.Deinitialize();
}

bool hal_display_is_initialized(void)
{
    return CRTOS::HAL::GlobalDisplay.IsInitialized();
}

void hal_display_enable(void)
{
    CRTOS::HAL::GlobalDisplay.Enable();
}

void hal_display_disable(void)
{
    CRTOS::HAL::GlobalDisplay.Disable();
}

// ============================================================================
// Frame Buffer API
// ============================================================================

uint32_t* hal_display_get_framebuffer(void)
{
    // Return the current draw buffer (the one NOT being displayed)
    // This is what the render loop should use for drawing
    return CRTOS::HAL::GlobalDisplay.GetDrawBuffer();
}

uint32_t* hal_display_get_backbuffer(void)
{
    // Return buffer 1 (fixed address for reference)
    return CRTOS::HAL::GlobalDisplay.GetBackBuffer();
}

bool hal_display_swap_buffers(void)
{
    CRTOS::Result result = CRTOS::HAL::GlobalDisplay.SwapBuffers();
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

void hal_display_flush_cache(void)
{
    CRTOS::HAL::GlobalDisplay.FlushCache();
}

// ============================================================================
// Drawing API
// ============================================================================

void hal_display_clear(uint32_t color)
{
    // Direct buffer fill - no TFTLIB dependency
    uint32_t* buffer = CRTOS::HAL::GlobalDisplay.GetDrawBuffer();
    uint32_t width = CRTOS::HAL::GlobalDisplay.GetWidth();
    uint32_t height = CRTOS::HAL::GlobalDisplay.GetHeight();
    uint32_t count = width * height;
    for (uint32_t i = 0; i < count; i++)
    {
        buffer[i] = color;
    }
}

void hal_display_fill_rect(const hal_display_rect_t* rect, uint32_t color)
{
    if (rect == nullptr)
    {
        return;
    }
    
    // Direct buffer fill - no TFTLIB dependency
    uint32_t* buffer = CRTOS::HAL::GlobalDisplay.GetDrawBuffer();
    uint32_t bw = CRTOS::HAL::GlobalDisplay.GetWidth();
    uint32_t bh = CRTOS::HAL::GlobalDisplay.GetHeight();
    
    uint32_t x2 = rect->x + rect->width;
    uint32_t y2 = rect->y + rect->height;
    if (x2 > bw) x2 = bw;
    if (y2 > bh) y2 = bh;
    
    for (uint32_t py = rect->y; py < y2; py++)
    {
        for (uint32_t px = rect->x; px < x2; px++)
        {
            buffer[py * bw + px] = color;
        }
    }
}

void hal_display_draw_pixel(uint16_t x, uint16_t y, uint32_t color)
{
    // Direct buffer access - no TFTLIB dependency
    uint32_t* buffer = CRTOS::HAL::GlobalDisplay.GetDrawBuffer();
    uint32_t width = CRTOS::HAL::GlobalDisplay.GetWidth();
    uint32_t height = CRTOS::HAL::GlobalDisplay.GetHeight();
    if (x < width && y < height)
    {
        buffer[y * width + x] = color;
    }
}

uint32_t hal_display_get_pixel(uint16_t x, uint16_t y)
{
    // Direct buffer access - no TFTLIB dependency
    uint32_t* buffer = CRTOS::HAL::GlobalDisplay.GetDrawBuffer();
    uint32_t width = CRTOS::HAL::GlobalDisplay.GetWidth();
    uint32_t height = CRTOS::HAL::GlobalDisplay.GetHeight();
    if (x < width && y < height)
    {
        return buffer[y * width + x];
    }
    return 0;
}

// ============================================================================
// Synchronization API
// ============================================================================

bool hal_display_wait_vsync(uint32_t timeout_ms)
{
    CRTOS::Result result = CRTOS::HAL::GlobalDisplay.WaitForVSync(timeout_ms);
    return (result == CRTOS::Result::RESULT_SUCCESS);
}

uint32_t hal_display_get_irq_number(void)
{
    return LCDIF_IRQn;
}

// ============================================================================
// Info API
// ============================================================================

uint16_t hal_display_get_width(void)
{
    return CRTOS::HAL::GlobalDisplay.GetWidth();
}

uint16_t hal_display_get_height(void)
{
    return CRTOS::HAL::GlobalDisplay.GetHeight();
}

hal_display_format_t hal_display_get_format(void)
{
    return ConvertFormatToC(CRTOS::HAL::GlobalDisplay.GetPixelFormat());
}

uint32_t hal_display_get_bytes_per_pixel(void)
{
    hal_display_format_t format = hal_display_get_format();
    switch (format)
    {
        case HAL_DISPLAY_FORMAT_RGB565:
            return 2;
        case HAL_DISPLAY_FORMAT_RGB888:
            return 3;
        case HAL_DISPLAY_FORMAT_XRGB8888:
        default:
            return 4;
    }
}

uint32_t hal_display_get_status(void)
{
    uint32_t status = 0;
    
    if (CRTOS::HAL::GlobalDisplay.IsInitialized())
    {
        status |= HAL_DISPLAY_STATUS_INITIALIZED;
        status |= HAL_DISPLAY_STATUS_ENABLED;  // Assume enabled if initialized
        
        // Check for double buffering
        if (CRTOS::HAL::GlobalDisplay.GetBackBuffer() != nullptr &&
            CRTOS::HAL::GlobalDisplay.GetBackBuffer() != CRTOS::HAL::GlobalDisplay.GetFrameBuffer())
        {
            status |= HAL_DISPLAY_STATUS_DOUBLE_BUFFER;
        }
    }
    
    return status;
}

} // extern "C"
