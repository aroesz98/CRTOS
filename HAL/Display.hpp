/*
 * Display.hpp - CRTOS Hardware Abstraction Layer - Display Driver
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
 * Hardware abstraction layer for eLCDIF RGB display controller.
 * Supports RGB565, RGB888, and XRGB8888 pixel formats with hardware-accelerated
 * double buffering for smooth animations.
 * 
 * NOTE: This is a low-level HAL driver. For drawing operations, use an external
 * graphics library (e.g., TFTLIB) with the framebuffer obtained from GetDrawBuffer().
 */

#ifndef CRTOS_HAL_DISPLAY_HPP
#define CRTOS_HAL_DISPLAY_HPP

#include <stdint.h>
#include "../CRTOS.hpp"
#include "../BinarySemaphore.hpp"

namespace CRTOS
{
namespace HAL
{
    /**
     * @brief Pixel format enumeration
     */
    enum class PixelFormat
    {
        RGB565,      ///< 16-bit RGB565 (5 bits R, 6 bits G, 5 bits B)
        RGB888,      ///< 24-bit RGB888 packed
        XRGB8888,    ///< 32-bit XRGB8888 (8 bits unused, 8 bits R, G, B)
    };
    
    /**
     * @brief Display configuration structure
     */
    struct DisplayConfig
    {
        uint16_t width;          ///< Panel width in pixels
        uint16_t height;         ///< Panel height in pixels
        uint8_t hsw;             ///< Horizontal sync width
        uint8_t hfp;             ///< Horizontal front porch
        uint8_t hbp;             ///< Horizontal back porch
        uint8_t vsw;             ///< Vertical sync width
        uint8_t vfp;             ///< Vertical front porch
        uint8_t vbp;             ///< Vertical back porch
        uint32_t polarityFlags;  ///< Signal polarity flags
        PixelFormat pixelFormat; ///< Pixel format
        bool doubleBuffer;       ///< Enable double buffering
    };
    
    /**
     * @brief Rectangle structure for drawing operations
     */
    struct Rect
    {
        uint16_t x;      ///< X coordinate
        uint16_t y;      ///< Y coordinate
        uint16_t width;  ///< Width in pixels
        uint16_t height; ///< Height in pixels
    };
    
    /**
     * @brief Display driver class
     * 
     * Provides hardware-accelerated display operations using eLCDIF controller.
     * Supports RGB modes with configurable timing and double buffering.
     */
    class Display
    {
    public:
        /**
         * @brief Constructor
         */
        Display();
        
        /**
         * @brief Destructor
         */
        ~Display();
        
        /**
         * @brief Initialize display with configuration
         * 
         * @param config Display configuration
         * @return Result::RESULT_SUCCESS on success
         */
        Result Initialize(const DisplayConfig& config);
        
        /**
         * @brief Deinitialize display and release resources
         */
        void Deinitialize();
        
        /**
         * @brief Enable display output
         */
        void Enable();
        
        /**
         * @brief Disable display output
         */
        void Disable();
        
        /**
         * @brief Get buffer 0 address (first allocated)
         * 
         * @return Pointer to buffer 0
         */
        uint32_t* GetFrameBuffer();
        
        /**
         * @brief Get buffer 1 address (second allocated, for double buffering)
         * 
         * @return Pointer to buffer 1, or nullptr if single buffered
         */
        uint32_t* GetBackBuffer();
        
        /**
         * @brief Get current draw buffer (the one NOT being displayed)
         * 
         * @return Pointer to draw buffer
         */
        uint32_t* GetDrawBuffer();
        
        /**
         * @brief Get current display buffer (the one being shown on screen)
         * 
         * @return Pointer to display buffer
         */
        uint32_t* GetDisplayBuffer();
        
        /**
         * @brief Swap buffers (flip)
         * 
         * For double buffered mode, swaps front and back buffers.
         * Waits for VSYNC to avoid tearing.
         * 
         * @return Result::RESULT_SUCCESS on success
         */
        Result SwapBuffers();
        
        /**
         * @brief Wait for vertical blank (VSYNC)
         * 
         * Blocks until VSYNC interrupt occurs.
         */
        void WaitVSync();
        
        /**
         * @brief Get display width
         * 
         * @return Width in pixels
         */
        uint16_t GetWidth() const { return m_width; }
        
        /**
         * @brief Get display height
         * 
         * @return Height in pixels
         */
        uint16_t GetHeight() const { return m_height; }
        
        /**
         * @brief Get pixel format
         * 
         * @return Current pixel format
         */
        PixelFormat GetPixelFormat() const { return m_pixelFormat; }
        
        /**
         * @brief Check if display is initialized
         * 
         * @return true if initialized
         */
        bool IsInitialized() const { return m_initialized; }
        
        /**
         * @brief Frame done callback (called from IRQ handler)
         * 
         * Internal use only - called by interrupt handler
         */
        void OnFrameDone();
        
        /**
         * @brief Wait for VSYNC (frame completion)
         * 
         * Blocks until the next VSYNC interrupt occurs.
         * Useful for synchronizing frame updates.
         * 
         * @param timeout_ms Timeout in milliseconds (0 = wait forever)
         * @return Result::RESULT_SUCCESS on VSYNC, Result::RESULT_TIMEOUT on timeout
         */
        Result WaitForVSync(uint32_t timeout_ms = 0);
        
        /**
         * @brief Flush D-cache for current drawing buffer
         * 
         * Call this after completing a series of drawing operations to ensure
         * all pixels are visible to the display controller. This is automatically
         * called by SwapBuffers() for double-buffered mode.
         * 
         * For single-buffered mode or when drawing without SwapBuffers(),
         * call this manually to avoid horizontal line artifacts.
         */
        void FlushCache();
        
    private:
        bool m_initialized;
        bool m_doubleBuffer;
        uint8_t m_currentBuffer;
        volatile bool m_frameDone;
        volatile bool m_swapPending;
        
        uint16_t m_width;
        uint16_t m_height;
        PixelFormat m_pixelFormat;
        uint32_t m_bytesPerPixel;
        
        uint32_t* m_frameBuffer[2];
        size_t m_frameBufferSize;
        
        // VSYNC synchronization via DPC
        BinarySemaphore* m_vsyncSemaphore;
        
        /**
         * @brief Get bytes per pixel for format
         */
        uint32_t GetBytesPerPixel(PixelFormat format);
        
        /**
         * @brief Configure eLCDIF hardware
         */
        void ConfigureHardware(const DisplayConfig& config);
        
        /**
         * @brief Allocate frame buffers
         */
        Result AllocateFrameBuffers();
        
        /**
         * @brief Free frame buffers
         */
        void FreeFrameBuffers();
    };
    
    // Global display instance
    extern Display GlobalDisplay;
    
} // namespace HAL
} // namespace CRTOS

#endif // CRTOS_HAL_DISPLAY_HPP
