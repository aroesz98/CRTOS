/*
 * PXP.cpp - Pixel Pipeline HAL Implementation for CRTOS
 * Author: Arkadiusz Szlanta
 * Date: 30 Dec 2024
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 */

#include "PXP.hpp"
#include "fsl_pxp.h"
#include "fsl_clock.h"
#include "MIMXRT1052.h"
#include "InterruptDPC.hpp"
#include "CRTOS.hpp"
#include <cstring>

namespace HAL
{

// Static member initialization
bool PXPDriver::s_initialized = false;
volatile bool PXPDriver::s_operationComplete = false;
PXPCompleteCallback PXPDriver::s_callback = nullptr;
void* PXPDriver::s_callbackContext = nullptr;
CRTOS::BinarySemaphore PXPDriver::s_completionSemaphore;

// Helper: Convert HAL PixelFormat to PXP Process Surface format
uint32_t PXPDriver::ToPxpPsFormat(PixelFormat fmt)
{
    switch (fmt)
    {
        case PixelFormat::XRGB8888:
        case PixelFormat::ARGB8888:
            return kPXP_PsPixelFormatARGB8888;  // 32-bit pixels
        case PixelFormat::RGB565:
            return kPXP_PsPixelFormatRGB565;
        case PixelFormat::RGB888:
            return kPXP_PsPixelFormatARGB8888;  // Use 32-bit unpacked
        default:
            return kPXP_PsPixelFormatARGB8888;
    }
}

// Helper: Convert HAL PixelFormat to PXP Alpha Surface format
uint32_t PXPDriver::ToPxpAsFormat(PixelFormat fmt)
{
    switch (fmt)
    {
        case PixelFormat::XRGB8888:
        case PixelFormat::ARGB8888:
            return kPXP_AsPixelFormatRGB888;
        case PixelFormat::RGB565:
            return kPXP_AsPixelFormatRGB565;
        case PixelFormat::RGB888:
            return kPXP_AsPixelFormatRGB888;
        default:
            return kPXP_AsPixelFormatRGB888;
    }
}

// Helper: Convert HAL PixelFormat to PXP Output format
uint32_t PXPDriver::ToPxpOutFormat(PixelFormat fmt)
{
    switch (fmt)
    {
        case PixelFormat::XRGB8888:
        case PixelFormat::ARGB8888:
            return kPXP_OutputPixelFormatRGB888;
        case PixelFormat::RGB565:
            return kPXP_OutputPixelFormatRGB565;
        case PixelFormat::RGB888:
            return kPXP_OutputPixelFormatRGB888P;
        default:
            return kPXP_OutputPixelFormatRGB888;
    }
}

// Helper: Get bytes per pixel for format
uint32_t PXPDriver::GetBytesPerPixel(PixelFormat fmt)
{
    switch (fmt)
    {
        case PixelFormat::XRGB8888:
        case PixelFormat::ARGB8888:
            return 4;
        case PixelFormat::RGB565:
            return 2;
        case PixelFormat::RGB888:
            return 3;
        default:
            return 4;
    }
}

bool PXPDriver::Init()
{
    if (s_initialized)
    {
        return true;
    }

    // Initialize PXP hardware
    PXP_Init(PXP);

    // Disable CSC1 (not doing color space conversion by default)
    PXP_EnableCsc1(PXP, false);
    
    // Clear PS_OFFSET register to ensure clean state
    // This register can retain garbage after reset and cause source sampling issues
    PXP->PS_OFFSET = 0;

    // Register PXP interrupt source with DPC dispatcher
    CRTOS::GlobalDPCDispatcher.RegisterInterruptSource(PXP_IRQn);
    
    // Register our DPC handler for PXP interrupt
    // The DPC worker will call DPCHandler which signals the semaphore
    CRTOS::GlobalDPCDispatcher.RegisterHandler(
        PXP_IRQn,
        &s_completionSemaphore,  // Semaphore to signal on completion
        DPCHandler,              // Our DPC callback
        nullptr                  // Context
    );

    // Enable PXP IRQ in NVIC
    // Priority 5 to allow it to preempt lower priority tasks
    NVIC_SetPriority(PXP_IRQn, 3);
    NVIC_EnableIRQ(PXP_IRQn);

    s_callback = nullptr;
    s_callbackContext = nullptr;
    s_operationComplete = true;  // Ready for first operation
    s_initialized = true;
    return true;
}

void PXPDriver::Deinit()
{
    if (!s_initialized)
    {
        return;
    }

    // Disable PXP IRQ
    NVIC_DisableIRQ(PXP_IRQn);
    PXP_DisableInterrupts(PXP, kPXP_CompleteInterruptEnable);

    // Unregister from DPC dispatcher
    CRTOS::GlobalDPCDispatcher.UnregisterHandler(PXP_IRQn, &s_completionSemaphore);
    CRTOS::GlobalDPCDispatcher.UnregisterInterruptSource(PXP_IRQn);

    PXP_Deinit(PXP);
    s_callback = nullptr;
    s_callbackContext = nullptr;
    s_initialized = false;
}

bool PXPDriver::IsBusy()
{
    // Check if PXP is running
    return (PXP->CTRL & PXP_CTRL_ENABLE_MASK) != 0;
}

bool PXPDriver::WaitComplete(uint32_t timeoutMs)
{
    bool completed = false;
    
    // Fast path: check if already complete (ISR sets this flag)
    if (s_operationComplete)
    {
        completed = true;
    }
    // Check if we're in exception/interrupt context (SVC, etc.)
    // In exception context, we must poll - cannot use semaphore wait
    // __get_IPSR() returns non-zero if in exception handler
    else if (__get_IPSR() != 0)
    {
        // Polling mode for exception context (e.g., syscall handler)
        // IMPORTANT: SVCall has higher priority than PXP_IRQ, so the ISR
        // cannot fire while we're in SVC. We must poll the hardware flag directly.
        // PXP operations are very fast (microseconds)
        uint32_t iterations = (timeoutMs == 0) ? 100000 : timeoutMs * 60000;
        
        while (!s_operationComplete)
        {
            // Check hardware complete flag directly since ISR can't run
            if (PXP_GetStatusFlags(PXP) & kPXP_CompleteFlag)
            {
                // Clear the flag ourselves since ISR won't do it
                PXP_ClearStatusFlags(PXP, kPXP_CompleteFlag);
                s_operationComplete = true;
                break;
            }
            
            if (--iterations == 0)
            {
                return false;
            }
            
            __NOP();
        }
        completed = true;
    }
    else
    {
        // Task context - can use semaphore wait (blocks task, allows others to run)
        // The DPC worker will signal this when PXP interrupt fires
        CRTOS::Result result = s_completionSemaphore.wait(timeoutMs);
        completed = (result == CRTOS::Result::RESULT_SUCCESS);
    }
    
    // NOTE: No cache invalidation needed - destination buffer is non-cached
    // (allocated with MEM_NON_CACHED flag)
    
    return completed;
}

bool PXPDriver::Scale(const ScaleParams& params)
{
    // Use ScaleAsync to start, then wait for completion
    if (!ScaleAsync(params))
    {
        return false;
    }

    // Wait for completion via polling (ScaleAsync already cleared flags)
    return WaitComplete(100);
}

bool PXPDriver::Copy(const ImageBuffer& src, const ImageBuffer& dst,
               uint16_t srcX, uint16_t srcY,
               uint16_t dstX, uint16_t dstY,
               uint16_t width, uint16_t height)
{
    // Copy is just a 1:1 scale
    ScaleParams params;
    memset(&params, 0, sizeof(params));
    params.src = src;
    params.dst = dst;
    params.srcX = srcX;
    params.srcY = srcY;
    params.srcW = width;
    params.srcH = height;
    params.dstX = dstX;
    params.dstY = dstY;
    params.dstW = width;
    params.dstH = height;
    params.rotation = Rotation::None;

    // Adjust source buffer address for the source region
    uint32_t bpp = GetBytesPerPixel(src.format);
    ImageBuffer adjustedSrc = src;
    adjustedSrc.address = src.address + srcY * src.pitchBytes + srcX * bpp;

    ScaleParams adjParams = params;
    adjParams.src = adjustedSrc;

    return Scale(adjParams);
}

bool PXPDriver::Fill(const ImageBuffer& dst,
               uint16_t x, uint16_t y,
               uint16_t width, uint16_t height,
               uint32_t color)
{
    if (!s_initialized)
    {
        return false;
    }

    // Use PS background color as fill
    // Disable PS by setting it outside the output area
    PXP_SetProcessSurfacePosition(PXP, 0xFFFF, 0xFFFF, 0, 0);
    PXP_SetProcessSurfaceBackGroundColor(PXP, color);

    // Disable Alpha Surface
    PXP_SetAlphaSurfacePosition(PXP, 0xFFFF, 0xFFFF, 0, 0);

    // Configure Output buffer
    pxp_output_buffer_config_t outConfig;
    memset(&outConfig, 0, sizeof(outConfig));
    outConfig.pixelFormat = (pxp_output_pixel_format_t)ToPxpOutFormat(dst.format);
    outConfig.interlacedMode = kPXP_OutputProgressive;
    outConfig.buffer0Addr = dst.address;
    outConfig.buffer1Addr = 0;
    outConfig.pitchBytes = dst.pitchBytes;
    outConfig.width = dst.width;
    outConfig.height = dst.height;

    PXP_SetOutputBufferConfig(PXP, &outConfig);

    // Set output clip to the fill region
    // Note: PXP doesn't have direct clip, we'd need to adjust buffer for partial fill
    // For now, this fills the entire output buffer with the color
    
    // Enable interrupt
    PXP_ClearStatusFlags(PXP, kPXP_CompleteFlag);
    s_operationComplete = false;
    PXP_EnableInterrupts(PXP, kPXP_CompleteInterruptEnable);
    
    // Start PXP
    PXP_Start(PXP);

    return WaitComplete(100);
}

bool PXPDriver::Blend(const BlendParams& params)
{
    if (!s_initialized)
    {
        return false;
    }

    // NOTE: No cache management needed - all PXP buffers are non-cached

    // Configure Process Surface (background)
    pxp_ps_buffer_config_t psConfig;
    memset(&psConfig, 0, sizeof(psConfig));
    psConfig.pixelFormat = (pxp_ps_pixel_format_t)ToPxpPsFormat(params.background.format);
    psConfig.swapByte = false;
    psConfig.bufferAddr = params.background.address;
    psConfig.bufferAddrU = 0;
    psConfig.bufferAddrV = 0;
    psConfig.pitchBytes = params.background.pitchBytes;

    PXP_SetProcessSurfaceBufferConfig(PXP, &psConfig);
    PXP_SetProcessSurfacePosition(PXP, 0, 0,
                                   params.background.width - 1,
                                   params.background.height - 1);

    // Configure Alpha Surface (foreground)
    pxp_as_buffer_config_t asConfig;
    memset(&asConfig, 0, sizeof(asConfig));
    asConfig.pixelFormat = (pxp_as_pixel_format_t)ToPxpAsFormat(params.foreground.format);
    asConfig.bufferAddr = params.foreground.address;
    asConfig.pitchBytes = params.foreground.pitchBytes;

    PXP_SetAlphaSurfaceBufferConfig(PXP, &asConfig);
    PXP_SetAlphaSurfacePosition(PXP, params.fgX, params.fgY,
                                 params.fgX + params.foreground.width - 1,
                                 params.fgY + params.foreground.height - 1);

    // Configure blending
    pxp_as_blend_config_t blendConfig;
    memset(&blendConfig, 0, sizeof(blendConfig));
    blendConfig.alpha = params.globalAlpha;
    blendConfig.invertAlpha = false;
    blendConfig.alphaMode = params.useGlobalAlpha ? kPXP_AlphaOverride : kPXP_AlphaEmbedded;
    blendConfig.ropMode = kPXP_RopMaskAs;

    PXP_SetAlphaSurfaceBlendConfig(PXP, &blendConfig);

    // Configure Output buffer
    pxp_output_buffer_config_t outConfig;
    memset(&outConfig, 0, sizeof(outConfig));
    outConfig.pixelFormat = (pxp_output_pixel_format_t)ToPxpOutFormat(params.output.format);
    outConfig.interlacedMode = kPXP_OutputProgressive;
    outConfig.buffer0Addr = params.output.address;
    outConfig.buffer1Addr = 0;
    outConfig.pitchBytes = params.output.pitchBytes;
    outConfig.width = params.output.width;
    outConfig.height = params.output.height;

    PXP_SetOutputBufferConfig(PXP, &outConfig);

    // Enable interrupt, clear flags, and start PXP
    PXP_ClearStatusFlags(PXP, kPXP_CompleteFlag);
    s_operationComplete = false;
    PXP_EnableInterrupts(PXP, kPXP_CompleteInterruptEnable);
    PXP_Start(PXP);

    return WaitComplete(100);
}

bool PXPDriver::Rotate(const ImageBuffer& src, const ImageBuffer& dst, Rotation rotation)
{
    ScaleParams params;
    memset(&params, 0, sizeof(params));
    params.src = src;
    params.dst = dst;
    params.srcX = 0;
    params.srcY = 0;
    params.srcW = src.width;
    params.srcH = src.height;
    params.dstX = 0;
    params.dstY = 0;

    // For 90/270 rotation, swap width/height
    if (rotation == Rotation::Deg90 || rotation == Rotation::Deg270)
    {
        params.dstW = src.height;
        params.dstH = src.width;
    }
    else
    {
        params.dstW = src.width;
        params.dstH = src.height;
    }

    params.rotation = rotation;

    return Scale(params);
}

void PXPDriver::SetCompletionCallback(PXPCompleteCallback callback, void* context)
{
    s_callback = callback;
    s_callbackContext = context;
}

void PXPDriver::DPCHandler(void* context)
{
    (void)context;
    
    // Set completion flag
    s_operationComplete = true;
    
    // Signal the completion semaphore to wake any waiting tasks
    // Note: DPC dispatcher may also signal it, but double signal is safe
    s_completionSemaphore.signal();
    
    // Call user callback if registered
    if (s_callback != nullptr)
    {
        s_callback(s_callbackContext, true);
    }
}

CRTOS::BinarySemaphore* PXPDriver::GetCompletionSemaphore()
{
    return &s_completionSemaphore;
}

bool PXPDriver::ConfigureScale(const ScaleParams& params)
{
    if (!s_initialized)
    {
        return false;
    }

    // Calculate adjusted source buffer address for srcX/srcY offset
    // PXP reads from this address, so we must point to the correct starting pixel
    uint32_t bpp = GetBytesPerPixel(params.src.format);
    uint32_t adjustedSrcAddr = params.src.address + 
                               params.srcY * params.src.pitchBytes + 
                               params.srcX * bpp;

    // Configure Process Surface (input)
    pxp_ps_buffer_config_t psConfig;
    memset(&psConfig, 0, sizeof(psConfig));
    psConfig.pixelFormat = (pxp_ps_pixel_format_t)ToPxpPsFormat(params.src.format);
    psConfig.swapByte = false;
    psConfig.bufferAddr = adjustedSrcAddr;  // Use adjusted address for sub-region
    psConfig.bufferAddrU = 0;
    psConfig.bufferAddrV = 0;
    psConfig.pitchBytes = params.src.pitchBytes;

    PXP_SetProcessSurfaceBufferConfig(PXP, &psConfig);

    // Clear PS_OFFSET register - ensures no leftover offset from previous operations
    // This register provides sub-pixel offset; we want 0 for clean alignment
    PXP->PS_OFFSET = 0;

    // Set background color (visible if PS doesn't cover output)
    PXP_SetProcessSurfaceBackGroundColor(PXP, 0);

    // Configure scaling: input is srcW x srcH, output is dstW x dstH
    PXP_SetProcessSurfaceScaler(PXP, params.srcW, params.srcH, params.dstW, params.dstH);

    // Set PS position in output buffer (where scaled image appears)
    // Coordinates are relative to output buffer origin
    PXP_SetProcessSurfacePosition(PXP, params.dstX, params.dstY,
                                   params.dstX + params.dstW - 1,
                                   params.dstY + params.dstH - 1);

    // Disable Alpha Surface (not blending)
    PXP_SetAlphaSurfacePosition(PXP, 0xFFFF, 0xFFFF, 0, 0);

    // Configure Output buffer
    // IMPORTANT: width/height define the output buffer's TOTAL dimensions (for pitch calc)
    // The PS position above defines WHERE in this buffer PXP writes
    // We use full destination buffer dimensions so PXP calculates row offsets correctly
    pxp_output_buffer_config_t outConfig;
    memset(&outConfig, 0, sizeof(outConfig));
    outConfig.pixelFormat = (pxp_output_pixel_format_t)ToPxpOutFormat(params.dst.format);
    outConfig.interlacedMode = kPXP_OutputProgressive;
    outConfig.buffer0Addr = params.dst.address;
    outConfig.buffer1Addr = 0;
    outConfig.pitchBytes = params.dst.pitchBytes;
    outConfig.width = params.dst.width;
    outConfig.height = params.dst.height;

    PXP_SetOutputBufferConfig(PXP, &outConfig);

    // Handle rotation if specified
    if (params.rotation != Rotation::None)
    {
        PXP_SetRotateConfig(PXP, kPXP_RotateProcessSurface, 
                            (pxp_rotate_degree_t)params.rotation, kPXP_FlipDisable);
    }
    else
    {
        PXP_SetRotateConfig(PXP, kPXP_RotateProcessSurface, 
                            kPXP_Rotate0, kPXP_FlipDisable);
    }

    return true;
}

bool PXPDriver::ScaleAsync(const ScaleParams& params)
{
    // NOTE: Cache management not needed here because all PXP buffers
    // (framebuffers and client content buffers) are allocated with
    // MEM_NON_CACHED flag, so they bypass the CPU cache entirely.
    
    // Configure PXP hardware
    if (!ConfigureScale(params))
    {
        return false;
    }

    // Clear any pending status flags before starting (CRITICAL!)
    PXP_ClearStatusFlags(PXP, kPXP_CompleteFlag);
    
    // Clear software completion flag
    s_operationComplete = false;
    
    // Enable interrupt before starting (as per NXP pxp_scale example)
    PXP_EnableInterrupts(PXP, kPXP_CompleteInterruptEnable);
    
    // Start PXP - interrupt will fire when complete
    PXP_Start(PXP);

    // Return immediately - interrupt will set s_operationComplete when done
    return true;
}

} // namespace HAL

// PXP Interrupt Service Routine - dispatches to DPC for deferred processing
extern "C" void PXP_IRQHandler(void)
{
    // Clear the complete interrupt flag
    PXP_ClearStatusFlags(PXP, kPXP_CompleteFlag);
    
    // Set completion flag immediately in ISR context
    // This allows polling code in SVC context to see completion
    // (DPC handler would also set this, but DPC can't run during SVC)
    HAL::PXPDriver::s_operationComplete = true;
    
    // Dispatch to DPC for deferred work (semaphore signal, callbacks)
    // The DPC worker will call DPCHandler to signal semaphore and run callbacks
    CRTOS::GlobalDPCDispatcher.DispatchInterrupt(PXP_IRQn);
}
