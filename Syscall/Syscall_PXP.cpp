/*
 * Syscall_PXP.cpp - PXP Hardware Accelerator System Call Implementations
 * Author: Arkadiusz Szlanta
 * Date: 30 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 */

#include "Syscall_Handlers.hpp"
#include "SystemCall.hpp"
#include "HAL/PXP.hpp"
#include "Task.hpp"

namespace CRTOS
{

// PXP Scale parameters structure (passed from user space)
// Must match the structure in kernel.h for modules
struct PXPScaleParamsUser
{
    uint32_t srcAddr;
    uint16_t srcWidth;
    uint16_t srcHeight;
    uint16_t srcPitch;
    uint8_t srcFormat;
    uint8_t reserved1;
    
    uint32_t dstAddr;
    uint16_t dstWidth;
    uint16_t dstHeight;
    uint16_t dstPitch;
    uint8_t dstFormat;
    uint8_t reserved2;
    
    uint16_t srcX;
    uint16_t srcY;
    uint16_t srcW;
    uint16_t srcH;
    uint16_t dstX;
    uint16_t dstY;
    uint16_t dstW;
    uint16_t dstH;
    uint8_t rotation;
    uint8_t reserved3[3];
};

// PXP Blend parameters structure
struct PXPBlendParamsUser
{
    uint32_t bgAddr;
    uint16_t bgWidth;
    uint16_t bgHeight;
    uint16_t bgPitch;
    uint8_t bgFormat;
    uint8_t reserved1;
    
    uint32_t fgAddr;
    uint16_t fgWidth;
    uint16_t fgHeight;
    uint16_t fgPitch;
    uint8_t fgFormat;
    uint8_t reserved2;
    
    uint32_t outAddr;
    uint16_t outWidth;
    uint16_t outHeight;
    uint16_t outPitch;
    uint8_t outFormat;
    uint8_t reserved3;
    
    uint16_t fgX;
    uint16_t fgY;
    uint8_t globalAlpha;
    uint8_t useGlobalAlpha;
    uint8_t reserved4[2];
};

// Helper to convert user format to HAL format
static ::HAL::PixelFormat ToPixelFormat(uint8_t fmt)
{
    switch (fmt)
    {
        case 0: return ::HAL::PixelFormat::XRGB8888;
        case 1: return ::HAL::PixelFormat::ARGB8888;
        case 2: return ::HAL::PixelFormat::RGB565;
        case 3: return ::HAL::PixelFormat::RGB888;
        default: return ::HAL::PixelFormat::XRGB8888;
    }
}

// SYS_PXP_SCALE (230) - Scale image using PXP hardware (synchronous)
int32_t sys_pxp_scale(uint32_t params_ptr)
{
    if (params_ptr == 0)
    {
        return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
    }
    
    // Initialize PXP if not already done
    if (!::HAL::PXPDriver::Init())
    {
        return static_cast<int32_t>(Syscall::SyscallError::ERR_IO);
    }
    
    const PXPScaleParamsUser* userParams = reinterpret_cast<const PXPScaleParamsUser*>(params_ptr);
    
    // Build HAL parameters
    ::HAL::ScaleParams params;
    
    params.src.address = userParams->srcAddr;
    params.src.width = userParams->srcWidth;
    params.src.height = userParams->srcHeight;
    params.src.pitchBytes = userParams->srcPitch;
    params.src.format = ToPixelFormat(userParams->srcFormat);
    
    params.dst.address = userParams->dstAddr;
    params.dst.width = userParams->dstWidth;
    params.dst.height = userParams->dstHeight;
    params.dst.pitchBytes = userParams->dstPitch;
    params.dst.format = ToPixelFormat(userParams->dstFormat);
    
    params.srcX = userParams->srcX;
    params.srcY = userParams->srcY;
    params.srcW = userParams->srcW;
    params.srcH = userParams->srcH;
    params.dstX = userParams->dstX;
    params.dstY = userParams->dstY;
    params.dstW = userParams->dstW;
    params.dstH = userParams->dstH;
    params.rotation = static_cast<::HAL::Rotation>(userParams->rotation);
    
    // Start async operation - returns immediately, does NOT wait
    // Caller should use pxp_wait() or pxp_is_busy() to check completion
    bool result = ::HAL::PXPDriver::ScaleAsync(params);
    
    return result ? 0 : static_cast<int32_t>(Syscall::SyscallError::ERR_IO);
}

// SYS_PXP_COPY (231) - Copy image region using PXP
int32_t sys_pxp_copy(uint32_t src_ptr, uint32_t dst_ptr, 
                     uint32_t src_xy, uint32_t dst_xy, uint32_t size)
{
    // Initialize PXP if not already done
    if (!::HAL::PXPDriver::Init())
    {
        return static_cast<int32_t>(Syscall::SyscallError::ERR_IO);
    }
    
    // Simple image buffer copy - extract packed parameters
    uint16_t srcX = (src_xy >> 16) & 0xFFFF;
    uint16_t srcY = src_xy & 0xFFFF;
    uint16_t dstX = (dst_xy >> 16) & 0xFFFF;
    uint16_t dstY = dst_xy & 0xFFFF;
    uint16_t width = (size >> 16) & 0xFFFF;
    uint16_t height = size & 0xFFFF;
    
    // For simple copy, we need full buffer info - this is a simplified version
    // Real implementation would need buffer descriptors
    (void)src_ptr;
    (void)dst_ptr;
    (void)srcX;
    (void)srcY;
    (void)dstX;
    (void)dstY;
    (void)width;
    (void)height;
    
    // TODO: Implement when needed
    return static_cast<int32_t>(Syscall::SyscallError::ERR_NOSYS);
}

// SYS_PXP_FILL (232) - Fill rectangle using PXP
int32_t sys_pxp_fill(uint32_t dst_ptr, uint32_t xy, uint32_t size, uint32_t color)
{
    // Initialize PXP if not already done
    if (!::HAL::PXPDriver::Init())
    {
        return static_cast<int32_t>(Syscall::SyscallError::ERR_IO);
    }
    
    (void)dst_ptr;
    (void)xy;
    (void)size;
    (void)color;
    
    // TODO: Implement when needed
    return static_cast<int32_t>(Syscall::SyscallError::ERR_NOSYS);
}

// SYS_PXP_BLEND (233) - Alpha blend using PXP
int32_t sys_pxp_blend(uint32_t params_ptr)
{
    if (params_ptr == 0)
    {
        return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
    }
    
    // Initialize PXP if not already done
    if (!::HAL::PXPDriver::Init())
    {
        return static_cast<int32_t>(Syscall::SyscallError::ERR_IO);
    }
    
    const PXPBlendParamsUser* userParams = reinterpret_cast<const PXPBlendParamsUser*>(params_ptr);
    
    // Build HAL parameters
    ::HAL::BlendParams params;
    
    params.background.address = userParams->bgAddr;
    params.background.width = userParams->bgWidth;
    params.background.height = userParams->bgHeight;
    params.background.pitchBytes = userParams->bgPitch;
    params.background.format = ToPixelFormat(userParams->bgFormat);
    
    params.foreground.address = userParams->fgAddr;
    params.foreground.width = userParams->fgWidth;
    params.foreground.height = userParams->fgHeight;
    params.foreground.pitchBytes = userParams->fgPitch;
    params.foreground.format = ToPixelFormat(userParams->fgFormat);
    
    params.output.address = userParams->outAddr;
    params.output.width = userParams->outWidth;
    params.output.height = userParams->outHeight;
    params.output.pitchBytes = userParams->outPitch;
    params.output.format = ToPixelFormat(userParams->outFormat);
    
    params.fgX = userParams->fgX;
    params.fgY = userParams->fgY;
    params.globalAlpha = userParams->globalAlpha;
    params.useGlobalAlpha = userParams->useGlobalAlpha != 0;
    
    bool result = ::HAL::PXPDriver::Blend(params);
    
    return result ? 0 : static_cast<int32_t>(Syscall::SyscallError::ERR_IO);
}

// SYS_PXP_IS_BUSY (234) - Check if PXP is busy
int32_t sys_pxp_is_busy()
{
    return ::HAL::PXPDriver::IsBusy() ? 1 : 0;
}

// SYS_PXP_WAIT (235) - Wait for PXP completion
// For typical PXP operations (microseconds), we poll.
// The interrupt-based signaling is available for async patterns.
int32_t sys_pxp_wait(uint32_t timeout_ms)
{
    // Check if already complete (fast path - very common)
    if (::HAL::PXPDriver::s_operationComplete)
    {
        return 0;
    }
    
    // PXP operations are typically very fast (10-100 microseconds)
    // For short timeouts, poll directly (efficient for fast ops)
    bool complete = ::HAL::PXPDriver::WaitComplete(timeout_ms);
    
    return complete ? 0 : static_cast<int32_t>(Syscall::SyscallError::ERR_TIMEDOUT);
}

// SYS_PXP_SCALE_ASYNC (236) - Start async scale operation (non-blocking)
int32_t sys_pxp_scale_async(uint32_t params_ptr)
{
    if (params_ptr == 0)
    {
        return static_cast<int32_t>(Syscall::SyscallError::ERR_INVAL);
    }
    
    // Initialize PXP if not already done
    if (!::HAL::PXPDriver::Init())
    {
        return static_cast<int32_t>(Syscall::SyscallError::ERR_IO);
    }
    
    const PXPScaleParamsUser* userParams = reinterpret_cast<const PXPScaleParamsUser*>(params_ptr);
    
    // Build HAL parameters
    ::HAL::ScaleParams params;
    
    params.src.address = userParams->srcAddr;
    params.src.width = userParams->srcWidth;
    params.src.height = userParams->srcHeight;
    params.src.pitchBytes = userParams->srcPitch;
    params.src.format = ToPixelFormat(userParams->srcFormat);
    
    params.dst.address = userParams->dstAddr;
    params.dst.width = userParams->dstWidth;
    params.dst.height = userParams->dstHeight;
    params.dst.pitchBytes = userParams->dstPitch;
    params.dst.format = ToPixelFormat(userParams->dstFormat);
    
    params.srcX = userParams->srcX;
    params.srcY = userParams->srcY;
    params.srcW = userParams->srcW;
    params.srcH = userParams->srcH;
    params.dstX = userParams->dstX;
    params.dstY = userParams->dstY;
    params.dstW = userParams->dstW;
    params.dstH = userParams->dstH;
    params.rotation = static_cast<::HAL::Rotation>(userParams->rotation);
    
    // Start async operation - returns immediately after PXP starts
    bool result = ::HAL::PXPDriver::ScaleAsync(params);
    
    return result ? 0 : static_cast<int32_t>(Syscall::SyscallError::ERR_IO);
}

} // namespace CRTOS
