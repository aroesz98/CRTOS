/*
 * Display.cpp - CRTOS Hardware Abstraction Layer - Display Driver Implementation
 * Author: Arkadiusz Szlanta
 * Date: 27 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "Display.hpp"
#include "../HeapAllocator.hpp"
#include "../InterruptDPC.hpp"
#include "../../device/MIMXRT1052.h"
#include "../../drivers/fsl_elcdif.h"
#include "../../drivers/fsl_gpio.h"
#include "fsl_cache.h"
#include <string.h>
#include "stdio.h"

// External functions for interrupt mask control
extern "C" void setInterruptMask(uint32_t mask);
extern "C" uint32_t getInterruptMask(void);

// External tick count from CRTOS.cpp (global namespace)
extern volatile uint32_t tickCount;

// Default display configuration for EVKB-IMXRT1050 480x272 LCD
#define DEFAULT_WIDTH       480
#define DEFAULT_HEIGHT      272
#define DEFAULT_HSW         40
#define DEFAULT_HFP         16
#define DEFAULT_HBP         56
#define DEFAULT_VSW         10
#define DEFAULT_VFP         3
#define DEFAULT_VBP         8

// GPIO pins for display control
#define LCD_DISP_GPIO       GPIO1
#define LCD_DISP_GPIO_PIN   2
#define LCD_BL_GPIO         GPIO2
#define LCD_BL_GPIO_PIN     31

// Frame buffer alignment (64 bytes for better DMA performance)
#define FRAME_BUFFER_ALIGN  64

namespace CRTOS
{
namespace HAL
{
    // Global display instance
    Display GlobalDisplay;
    
    // Static volatile flag for frame done interrupt
    static volatile bool s_frameDoneFlag = false;
    
    // Forward declaration for ISR callback
    static void LCDIF_DPC_Callback(void* context);
    
    Display::Display()
        : m_initialized(false)
        , m_doubleBuffer(false)
        , m_currentBuffer(0)
        , m_frameDone(false)
        , m_swapPending(false)
        , m_width(0)
        , m_height(0)
        , m_pixelFormat(PixelFormat::XRGB8888)
        , m_bytesPerPixel(4)
        , m_frameBufferSize(0)
        , m_vsyncSemaphore(nullptr)
    {
        m_frameBuffer[0] = nullptr;
        m_frameBuffer[1] = nullptr;
    }
    
    Display::~Display()
    {
        Deinitialize();
    }
    
    uint32_t Display::GetBytesPerPixel(PixelFormat format)
    {
        switch (format)
        {
            case PixelFormat::RGB565:
                return 2;
            case PixelFormat::RGB888:
                return 3;
            case PixelFormat::XRGB8888:
                return 4;
            default:
                return 4;
        }
    }
    
    Result Display::AllocateFrameBuffers()
    {
        m_frameBufferSize = m_width * m_height * m_bytesPerPixel;
        
        // Hints for framebuffer allocation: DMA-capable, non-cached memory required
        Memory::AllocHints hints;
        hints.requiredFlags = Memory::MEM_DMA_CAPABLE | Memory::MEM_NON_CACHED;
        hints.preferredFlags = Memory::MEM_LARGE;
        hints.alignment = FRAME_BUFFER_ALIGN;
        
        // Allocate first buffer - allocator will pick best region (SDRAM preferred for large buffers)
        m_frameBuffer[0] = (uint32_t*)HeapAllocator::Allocate(m_frameBufferSize, hints);
        if (!m_frameBuffer[0])
        {
            printf("[Display] ERROR: Could not allocate buffer 0!\r\n");
            return Result::RESULT_NO_MEMORY;
        }
        
        // Clear first buffer
        memset(m_frameBuffer[0], 0, m_frameBufferSize);
        
        // Allocate second buffer if double buffering enabled
        if (m_doubleBuffer)
        {
            m_frameBuffer[1] = (uint32_t*)HeapAllocator::Allocate(m_frameBufferSize, hints);
            if (!m_frameBuffer[1])
            {
                printf("[Display] ERROR: Could not allocate buffer 1!\r\n");
                HeapAllocator::Free(m_frameBuffer[0]);
                m_frameBuffer[0] = nullptr;
                return Result::RESULT_NO_MEMORY;
            }
            
            // Clear second buffer
            memset(m_frameBuffer[1], 0, m_frameBufferSize);
        }
        
        return Result::RESULT_SUCCESS;
    }
    
    void Display::FreeFrameBuffers()
    {
        if (m_frameBuffer[0])
        {
            HeapAllocator::Free(m_frameBuffer[0]);
            m_frameBuffer[0] = nullptr;
        }
        
        if (m_frameBuffer[1])
        {
            HeapAllocator::Free(m_frameBuffer[1]);
            m_frameBuffer[1] = nullptr;
        }
    }
    
    void Display::ConfigureHardware(const DisplayConfig& config)
    {
        // Map pixel format to eLCDIF format
        elcdif_pixel_format_t lcdifFormat;
        switch (config.pixelFormat)
        {
            case PixelFormat::RGB565:
                lcdifFormat = kELCDIF_PixelFormatRGB565;
                break;
            case PixelFormat::RGB888:
                lcdifFormat = kELCDIF_PixelFormatRGB888;
                break;
            case PixelFormat::XRGB8888:
            default:
                lcdifFormat = kELCDIF_PixelFormatXRGB8888;
                break;
        }
        
        // Configure eLCDIF in RGB mode
        elcdif_rgb_mode_config_t lcdifConfig = {0};
        lcdifConfig.panelWidth = config.width;
        lcdifConfig.panelHeight = config.height;
        lcdifConfig.hsw = config.hsw;
        lcdifConfig.hfp = config.hfp;
        lcdifConfig.hbp = config.hbp;
        lcdifConfig.vsw = config.vsw;
        lcdifConfig.vfp = config.vfp;
        lcdifConfig.vbp = config.vbp;
        lcdifConfig.polarityFlags = config.polarityFlags;
        lcdifConfig.bufferAddr = (uint32_t)m_frameBuffer[0];
        lcdifConfig.pixelFormat = lcdifFormat;
        
        // Data bus is always 16-bit - this is the physical LCD panel connection
        // The pixel format determines how data is packed, but bus width is hardware-fixed
        lcdifConfig.dataBus = kELCDIF_DataBus16Bit;
        
        printf("[Display] ConfigureHardware: buffer @ 0x%08lX, format=%d, dataBus=%d, %ux%u\r\n",
               lcdifConfig.bufferAddr, lcdifFormat, lcdifConfig.dataBus, config.width, config.height);
        
        // Initialize eLCDIF controller
        ELCDIF_RgbModeInit(LCDIF, &lcdifConfig);
        
        // Enable frame done interrupt
        ELCDIF_EnableInterrupts(LCDIF, kELCDIF_CurFrameDoneInterruptEnable);
        
        // Enable LCDIF interrupt in NVIC
        NVIC_SetPriority(LCDIF_IRQn, 5);
        NVIC_EnableIRQ(LCDIF_IRQn);
        
        // Configure GPIO for display enable
        gpio_pin_config_t gpioConfig = {kGPIO_DigitalOutput, 1, kGPIO_NoIntmode};
        GPIO_PinInit(LCD_DISP_GPIO, LCD_DISP_GPIO_PIN, &gpioConfig);
        
        // Configure GPIO for backlight
        GPIO_PinInit(LCD_BL_GPIO, LCD_BL_GPIO_PIN, &gpioConfig);
        
        // Enable display and backlight
        GPIO_PinWrite(LCD_DISP_GPIO, LCD_DISP_GPIO_PIN, 1);
        GPIO_PinWrite(LCD_BL_GPIO, LCD_BL_GPIO_PIN, 1);
    }
    
    Result Display::Initialize(const DisplayConfig& config)
    {
        if (m_initialized)
            return Result::RESULT_CRC_ALREADY_INITIALIZED;
        
        m_width = config.width;
        m_height = config.height;
        m_pixelFormat = config.pixelFormat;
        m_bytesPerPixel = GetBytesPerPixel(config.pixelFormat);
        m_doubleBuffer = config.doubleBuffer;
        m_currentBuffer = 0;
        
        // Allocate frame buffers
        Result result = AllocateFrameBuffers();
        if (result != Result::RESULT_SUCCESS)
            return result;
        
        // Configure hardware
        ConfigureHardware(config);
        
        // Create VSYNC semaphore (starts at 0 - unsignaled)
        m_vsyncSemaphore = new BinarySemaphore();
        if (!m_vsyncSemaphore)
        {
            printf("[Display] ERROR: Failed to create VSYNC semaphore\r\n");
            FreeFrameBuffers();
            return Result::RESULT_NO_MEMORY;
        }
        
        // Register with DPC system for LCDIF interrupt
        printf("[Display] Registering IRQ %d with DPC dispatcher...\r\n", LCDIF_IRQn);
        Result dpcResult = GlobalDPCDispatcher.RegisterInterruptSource(LCDIF_IRQn);
        if (dpcResult != Result::RESULT_SUCCESS)
        {
            printf("[Display] ERROR: Failed to register interrupt source: %d\r\n", (int)dpcResult);
        }
        
        // Register handler with ISR callback for hardware flag clearing
        dpcResult = GlobalDPCDispatcher.RegisterHandler(LCDIF_IRQn, m_vsyncSemaphore, 
                                                        LCDIF_DPC_Callback, this);
        if (dpcResult != Result::RESULT_SUCCESS)
        {
            printf("[Display] ERROR: Failed to register DPC handler: %d\r\n", (int)dpcResult);
        }
        else
        {
            printf("[Display] DPC handler registered for LCDIF IRQ\r\n");
        }
        
        m_initialized = true;
        m_frameDone = false;
        m_currentBuffer = 0;
        
        return Result::RESULT_SUCCESS;
    }
    
    void Display::Deinitialize()
    {
        if (!m_initialized)
            return;
        
        // Disable display output
        Disable();
        
        // Disable interrupts
        ELCDIF_DisableInterrupts(LCDIF, kELCDIF_CurFrameDoneInterruptEnable);
        NVIC_DisableIRQ(LCDIF_IRQn);
        
        // Unregister from DPC system
        if (m_vsyncSemaphore)
        {
            GlobalDPCDispatcher.UnregisterHandler(LCDIF_IRQn, m_vsyncSemaphore);
            delete m_vsyncSemaphore;
            m_vsyncSemaphore = nullptr;
        }
        
        // Disable display and backlight
        GPIO_PinWrite(LCD_DISP_GPIO, LCD_DISP_GPIO_PIN, 0);
        GPIO_PinWrite(LCD_BL_GPIO, LCD_BL_GPIO_PIN, 0);
        
        // Free frame buffers
        FreeFrameBuffers();
        
        m_initialized = false;
    }
    
    void Display::Enable()
    {
        if (!m_initialized)
        {
            printf("[Display] ERROR: Enable() called but not initialized!\r\n");
            return;
        }
        
        printf("[Display] Starting eLCDIF controller...\r\n");
        ELCDIF_RgbModeStart(LCDIF);
        printf("[Display] eLCDIF controller started\r\n");
    }
    
    void Display::Disable()
    {
        if (!m_initialized)
            return;
        
        ELCDIF_RgbModeStop(LCDIF);
    }
    
    uint32_t* Display::GetFrameBuffer()
    {
        // Return buffer 0 (first allocated buffer)
        return m_frameBuffer[0];
    }
    
    uint32_t* Display::GetBackBuffer()
    {
        // Return buffer 1 (second allocated buffer)
        if (!m_doubleBuffer)
            return nullptr;
        
        return m_frameBuffer[1];
    }
    
    uint32_t* Display::GetDrawBuffer()
    {
        // Return the buffer we should draw to (not currently being displayed)
        if (m_doubleBuffer)
            return m_frameBuffer[m_currentBuffer ^ 1];
        else
            return m_frameBuffer[0];
    }
    
    uint32_t* Display::GetDisplayBuffer()
    {
        // Return the buffer currently being displayed
        return m_frameBuffer[m_currentBuffer];
    }
    
    Result Display::SwapBuffers()
    {
        if (!m_initialized || !m_doubleBuffer)
            return Result::RESULT_BAD_PARAMETER;
        
        // CRITICAL: Clean D-cache for the back buffer (one we just drew to) BEFORE swapping
        // This ensures all CPU writes are visible to the eLCDIF DMA controller
        // Without this, horizontal line artifacts appear due to stale cache data
        // Back buffer (draw buffer) is at m_frameBuffer[m_currentBuffer ^ 1]
        uint32_t* drawBuffer = m_frameBuffer[m_currentBuffer ^ 1];
        L1CACHE_CleanDCacheByRange((uint32_t)drawBuffer, m_frameBufferSize);
        
        // Data synchronization barrier to ensure cache clean completes before DMA starts
        __DSB();
        
        // Flip buffer index - m_currentBuffer now points to the buffer we just finished drawing
        m_currentBuffer ^= 1;
        
        // Set next buffer address - hardware will display this buffer on next VSYNC
        ELCDIF_SetNextBufferAddr(LCDIF, (uint32_t)m_frameBuffer[m_currentBuffer]);
        
        // Mark that a swap is pending - interrupt handler will signal when complete
        m_swapPending = true;
        
        // Note: Non-blocking - hardware will complete the swap asynchronously
        // The interrupt handler will signal the semaphore when swap completes
        
        return Result::RESULT_SUCCESS;
    }
    
    Result Display::WaitForVSync(uint32_t timeout_ms)
    {
        if (!m_initialized)
            return Result::RESULT_BAD_PARAMETER;
        
        // If no swap is pending, return immediately (nothing to wait for)
        if (!m_swapPending)
            return Result::RESULT_SUCCESS;
        
        // Check if we're in handler mode (interrupt/exception context)
        uint32_t ipsr;
        __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
        
        if (ipsr != 0)
        {
            // We're in handler mode (syscall/interrupt) - can't block here
            // Return immediately and let the swap complete asynchronously
            (void)timeout_ms;
            return Result::RESULT_SUCCESS;
        }
        
        // Thread mode - we can busy-wait safely
        uint32_t timeout_ticks = (timeout_ms == 0) ? 0xFFFFFFFF : timeout_ms;
        uint32_t start_time = ::tickCount;
        
        // Spin until swap completes or timeout
        while (m_swapPending)
        {
            // Check timeout
            if ((::tickCount - start_time) >= timeout_ticks)
            {
                return Result::RESULT_SEMAPHORE_TIMEOUT;
            }
        }
        
        return Result::RESULT_SUCCESS;
    }
    
    void Display::OnFrameDone()
    {
        m_frameDone = true;
        s_frameDoneFlag = true;
        
        // Note: Semaphore signaling is now done by DPC dispatcher
        m_swapPending = false;
    }
    
    void Display::FlushCache()
    {
        if (!m_initialized)
            return;
        
        // Clean D-cache for the current drawing buffer
        // This ensures CPU writes are visible to DMA/display controller
        uint32_t* buffer = GetDrawBuffer();
        if (buffer)
        {
            L1CACHE_CleanDCacheByRange((uint32_t)buffer, m_frameBufferSize);
            __DSB();
        }
    }
    
    // Static DPC callback for LCDIF interrupt - runs in ISR context via IntDefaultHandler
    static void LCDIF_DPC_Callback(void* context)
    {
        // Clear LCDIF interrupt status
        uint32_t intStatus = ELCDIF_GetInterruptStatus(LCDIF);
        ELCDIF_ClearInterruptStatus(LCDIF, intStatus);
        
        if (intStatus & kELCDIF_CurFrameDone)
        {
            // Update internal state
            Display* display = static_cast<Display*>(context);
            if (display)
            {
                display->OnFrameDone();
            }
        }
    }

} // namespace HAL
} // namespace CRTOS
