/*
 * PXP.hpp - Pixel Pipeline HAL for CRTOS
 * Author: Arkadiusz Szlanta
 * Date: 30 Dec 2024
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 *
 * Description:
 * Hardware abstraction layer for the i.MX RT1052 Pixel Pipeline (PXP).
 * The PXP provides hardware-accelerated image processing including:
 * - Scaling (upscale/downscale)
 * - Rotation (0, 90, 180, 270 degrees)
 * - Color space conversion (YUV<->RGB)
 * - Alpha blending
 * - Color keying
 *
 * Uses DPC (Deferred Procedure Call) for interrupt handling to minimize
 * ISR latency and allow callbacks to run in a safer context.
 */

#ifndef CRTOS_HAL_PXP_HPP_
#define CRTOS_HAL_PXP_HPP_

#include <cstdint>
#include "BinarySemaphore.hpp"

namespace HAL
{

/**
 * @brief Pixel format enumeration
 */
enum class PixelFormat : uint8_t
{
    XRGB8888,       ///< 32-bit XRGB (no alpha, used by eLCDIF)
    ARGB8888,       ///< 32-bit ARGB with alpha
    RGB565,         ///< 16-bit RGB565
    RGB888,         ///< 24-bit RGB packed
};

/**
 * @brief Rotation angle enumeration
 */
enum class Rotation : uint8_t
{
    None   = 0,     ///< No rotation
    Deg90  = 1,     ///< 90 degrees clockwise
    Deg180 = 2,     ///< 180 degrees
    Deg270 = 3,     ///< 270 degrees clockwise
};

/**
 * @brief Image buffer descriptor
 */
struct ImageBuffer
{
    uint32_t address;       ///< Buffer address (must be aligned to 8 bytes)
    uint16_t width;         ///< Image width in pixels
    uint16_t height;        ///< Image height in pixels
    uint16_t pitchBytes;    ///< Bytes per row (stride)
    PixelFormat format;     ///< Pixel format
};

/**
 * @brief Scale operation parameters
 */
struct ScaleParams
{
    ImageBuffer src;        ///< Source buffer
    ImageBuffer dst;        ///< Destination buffer
    uint16_t srcX;          ///< Source region X start
    uint16_t srcY;          ///< Source region Y start
    uint16_t srcW;          ///< Source region width
    uint16_t srcH;          ///< Source region height
    uint16_t dstX;          ///< Destination X position
    uint16_t dstY;          ///< Destination Y position
    uint16_t dstW;          ///< Destination width (scaled)
    uint16_t dstH;          ///< Destination height (scaled)
    Rotation rotation;      ///< Rotation to apply
};

/**
 * @brief Alpha blend parameters
 */
struct BlendParams
{
    ImageBuffer background; ///< Background (process surface)
    ImageBuffer foreground; ///< Foreground with alpha (alpha surface)
    ImageBuffer output;     ///< Output buffer
    uint16_t fgX;           ///< Foreground X position on background
    uint16_t fgY;           ///< Foreground Y position on background
    uint8_t globalAlpha;    ///< Global alpha value (0-255)
    bool useGlobalAlpha;    ///< If true, use globalAlpha instead of per-pixel
};

/**
 * @brief PXP completion callback type
 * 
 * This callback is invoked via DPC when a PXP operation completes.
 * It runs outside of ISR context, so it's safe to do more work here.
 * 
 * @param context User-provided context pointer
 * @param success True if operation completed successfully
 */
typedef void (*PXPCompleteCallback)(void* context, bool success);

/**
 * @brief PXP Hardware Accelerator HAL
 */
class PXPDriver
{
public:
    /**
     * @brief Initialize the PXP hardware
     * @return true on success
     */
    static bool Init();

    /**
     * @brief Deinitialize the PXP hardware
     */
    static void Deinit();

    /**
     * @brief Check if PXP is currently busy
     * @return true if busy
     */
    static bool IsBusy();

    /**
     * @brief Wait for PXP to complete current operation
     * @param timeoutMs Timeout in milliseconds (0 = infinite)
     * @return true if completed, false if timeout
     */
    static bool WaitComplete(uint32_t timeoutMs = 0);

    /**
     * @brief Scale an image region to a different size
     * 
     * Uses PXP hardware to efficiently scale images. Supports both
     * upscaling and downscaling with bilinear filtering.
     * 
     * @param params Scale parameters
     * @return true on success
     */
    static bool Scale(const ScaleParams& params);

    /**
     * @brief Copy a region from source to destination (1:1 scale)
     * 
     * Optimized copy using PXP DMA. Faster than CPU memcpy for large buffers.
     * 
     * @param src Source buffer
     * @param dst Destination buffer  
     * @param srcX Source X start
     * @param srcY Source Y start
     * @param dstX Destination X start
     * @param dstY Destination Y start
     * @param width Width to copy
     * @param height Height to copy
     * @return true on success
     */
    static bool Copy(const ImageBuffer& src, const ImageBuffer& dst,
                     uint16_t srcX, uint16_t srcY,
                     uint16_t dstX, uint16_t dstY,
                     uint16_t width, uint16_t height);

    /**
     * @brief Fill a region with a solid color
     * 
     * @param dst Destination buffer
     * @param x X start
     * @param y Y start
     * @param width Width
     * @param height Height
     * @param color Fill color (format depends on dst.format)
     * @return true on success
     */
    static bool Fill(const ImageBuffer& dst,
                     uint16_t x, uint16_t y,
                     uint16_t width, uint16_t height,
                     uint32_t color);

    /**
     * @brief Alpha blend foreground onto background
     * 
     * @param params Blend parameters
     * @return true on success
     */
    static bool Blend(const BlendParams& params);

    /**
     * @brief Rotate an image
     * 
     * @param src Source buffer
     * @param dst Destination buffer
     * @param rotation Rotation angle
     * @return true on success
     */
    static bool Rotate(const ImageBuffer& src, const ImageBuffer& dst,
                       Rotation rotation);

    /**
     * @brief Set a completion callback for async operations
     * 
     * The callback is invoked via DPC when PXP completes.
     * 
     * @param callback Callback function (nullptr to disable)
     * @param context User context passed to callback
     */
    static void SetCompletionCallback(PXPCompleteCallback callback, void* context);

    /**
     * @brief Start async scale operation
     * 
     * Starts the operation and returns immediately.
     * Use WaitComplete() or a callback to know when done.
     * 
     * @param params Scale parameters
     * @return true if operation started
     */
    static bool ScaleAsync(const ScaleParams& params);

    /**
     * @brief DPC callback handler - called from ISR via DPC dispatcher
     * 
     * This is registered with the InterruptDPC system and called
     * when the PXP complete interrupt fires.
     * 
     * @param context PXPDriver context (unused)
     */
    static void DPCHandler(void* context);

    // Allow ISR access to completion flag
    static volatile bool s_operationComplete;
    
    /**
     * @brief Get the PXP completion semaphore
     * 
     * Returns the semaphore that is signaled when PXP completes an operation.
     * Userspace can wait on this semaphore after starting an async operation.
     * 
     * @return Pointer to the completion semaphore
     */
    static CRTOS::BinarySemaphore* GetCompletionSemaphore();

private:
    static bool s_initialized;
    static PXPCompleteCallback s_callback;
    static void* s_callbackContext;
    static CRTOS::BinarySemaphore s_completionSemaphore;

    // Internal helper to configure and start PXP
    static bool ConfigureScale(const ScaleParams& params);

    // Convert HAL pixel format to PXP format enum
    static uint32_t ToPxpPsFormat(PixelFormat fmt);
    static uint32_t ToPxpAsFormat(PixelFormat fmt);
    static uint32_t ToPxpOutFormat(PixelFormat fmt);
    static uint32_t GetBytesPerPixel(PixelFormat fmt);
};

} // namespace HAL

#endif // CRTOS_HAL_PXP_HPP_
