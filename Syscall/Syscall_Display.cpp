/*
 * Syscall_Display.cpp
 * Display HAL syscall handlers
 * Author: Arkadiusz Szlanta
 * Date: 27 Dec 2024
 */

#include "Syscall_Handlers.hpp"
#include "../HAL/Display.hpp"
#include "../CRTOS_Internal.hpp"
#include "TFTLIB_8BIT.hpp"
#include <cstring>

using namespace CRTOS;
using namespace CRTOS::HAL;
using namespace CRTOS::Syscall;

// Local TFTLIB instance for syscall drawing operations
static TFTLIB_8BIT s_syscallTFT;

// External reference to current task
extern volatile TaskControlBlock *sCurrentTCB;

namespace CRTOS {

    // Per-task render target state
    // Maximum number of tasks that can have custom render targets
    #define MAX_RENDER_TARGETS 8
    
    struct RenderTargetEntry {
        const TaskControlBlock* tcb;  // Task that owns this render target
        uint32_t* buffer;             // Render target buffer (NULL = system FB)
        uint32_t width;
        uint32_t height;
    };
    
    static RenderTargetEntry s_renderTargets[MAX_RENDER_TARGETS] = {};
    
    // Find or create render target entry for current task
    static RenderTargetEntry* GetCurrentTaskRenderTarget() {
        const TaskControlBlock* current = const_cast<const TaskControlBlock*>(sCurrentTCB);
        
        // Find existing entry for this task
        for (int i = 0; i < MAX_RENDER_TARGETS; i++) {
            if (s_renderTargets[i].tcb == current) {
                return &s_renderTargets[i];
            }
        }
        
        // Not found - return NULL (use default framebuffer)
        return nullptr;
    }
    
    // Helper: Get current render target (custom or system FB)
    static inline uint32_t* GetRenderBuffer() {
        RenderTargetEntry* entry = GetCurrentTaskRenderTarget();
        if (entry && entry->buffer) {
            return entry->buffer;
        }
        return HAL::GlobalDisplay.GetFrameBuffer();
    }
    
    static inline uint32_t GetRenderWidth() {
        RenderTargetEntry* entry = GetCurrentTaskRenderTarget();
        if (entry && entry->buffer) {
            return entry->width;
        }
        return HAL::GlobalDisplay.GetWidth();
    }
    
    static inline uint32_t GetRenderHeight() {
        RenderTargetEntry* entry = GetCurrentTaskRenderTarget();
        if (entry && entry->buffer) {
            return entry->height;
        }
        return HAL::GlobalDisplay.GetHeight();
    }
    
    static inline bool HasCustomRenderTarget() {
        RenderTargetEntry* entry = GetCurrentTaskRenderTarget();
        return (entry && entry->buffer != nullptr);
    }

    // SYS_DISPLAY_CLEAR (200) - Clear display to color
    // arg0: color (XRGB8888)
    // returns: 0 on success, -errno on error
    int32_t sys_display_clear(uint32_t color)
    {
        // Clear current render target
        uint32_t* buf = GetRenderBuffer();
        uint32_t count = GetRenderWidth() * GetRenderHeight();
        for (uint32_t i = 0; i < count; i++) {
            buf[i] = color;
        }
        return 0;
    }

    // SYS_DISPLAY_FILL_RECT (201) - Fill rectangle with color
    // arg0: x
    // arg1: y
    // arg2: width
    // arg3: height
    // arg4: color (XRGB8888)
    // returns: 0 on success, -errno on error
    int32_t sys_display_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color)
    {
        // Fill rectangle in current render target
        uint32_t* buf = GetRenderBuffer();
        uint32_t bw = GetRenderWidth();
        uint32_t bh = GetRenderHeight();
        uint32_t x2 = x + width;
        uint32_t y2 = y + height;
        if (x2 > bw) x2 = bw;
        if (y2 > bh) y2 = bh;
        for (uint32_t py = y; py < y2; py++) {
            for (uint32_t px = x; px < x2; px++) {
                buf[py * bw + px] = color;
            }
        }
        return 0;
    }

    // SYS_DISPLAY_DRAW_PIXEL (202) - Draw single pixel
    // arg0: x
    // arg1: y
    // arg2: color (XRGB8888)
    // returns: 0 on success, -errno on error
    int32_t sys_display_draw_pixel(uint32_t x, uint32_t y, uint32_t color)
    {
        uint32_t* buf = GetRenderBuffer();
        uint32_t bw = GetRenderWidth();
        uint32_t bh = GetRenderHeight();
        if (x < bw && y < bh) {
            buf[y * bw + x] = color;
        }
        return 0;
    }

    // SYS_DISPLAY_GET_PIXEL (203) - Read pixel color
    // arg0: x
    // arg1: y
    // returns: color value (XRGB8888) or 0 on error
    int32_t sys_display_get_pixel(uint32_t x, uint32_t y)
    {
        uint32_t* buf = GetRenderBuffer();
        uint32_t bw = GetRenderWidth();
        uint32_t bh = GetRenderHeight();
        if (x < bw && y < bh) {
            return (int32_t)buf[y * bw + x];
        }
        return 0;
    }

    // SYS_DISPLAY_SWAP (204) - Swap front/back buffers (vsync)
    // returns: 0 on success, -errno on error
    // Note: when using custom render target, swap is a no-op
    int32_t sys_display_swap()
    {
        if (HasCustomRenderTarget()) {
            // No-op for custom render target - client signals via contentDirty
            return 0;
        }
        HAL::GlobalDisplay.SwapBuffers();
        return 0;
    }

    // SYS_DISPLAY_WAIT_VSYNC (205) - Wait for VSYNC completion
    // arg0: timeout in milliseconds (0 = wait forever)
    // returns: 0 on success, -errno on error
    int32_t sys_display_wait_vsync(uint32_t timeout_ms)
    {
        if (HasCustomRenderTarget()) {
            // No vsync needed for custom render target
            return 0;
        }
        Result result = HAL::GlobalDisplay.WaitForVSync(timeout_ms);
        if (result == Result::RESULT_SUCCESS)
        {
            return 0;
        }
        return -static_cast<int32_t>(SyscallError::ERR_TIMEDOUT);
    }

    // SYS_DISPLAY_GET_FRAMEBUFFER (206) - Get current render target pointer
    // returns: pointer to current render buffer (XRGB8888 format)
    int32_t sys_display_get_framebuffer()
    {
        return (int32_t)GetRenderBuffer();
    }

    // SYS_DISPLAY_SET_CURSOR (207) - Set text cursor position
    int32_t sys_display_set_cursor(int32_t x, int32_t y)
    {
        s_syscallTFT.setCursor(x, y);
        return 0;
    }

    // SYS_DISPLAY_SET_TEXT_COLOR (208) - Set text foreground/background color
    int32_t sys_display_set_text_color(uint32_t fg, uint32_t bg)
    {
        s_syscallTFT.setTextColor(fg, bg);
        return 0;
    }

    // SYS_DISPLAY_SET_TEXT_SIZE (209) - Set text size multiplier
    int32_t sys_display_set_text_size(uint32_t size)
    {
        s_syscallTFT.setTextSize(static_cast<uint8_t>(size));
        return 0;
    }

    // SYS_DISPLAY_DRAW_STRING (210) - Draw string at position with scale
    int32_t sys_display_draw_string(const char* str, int32_t x, int32_t y, float scale)
    {
        if (!str) return -static_cast<int32_t>(SyscallError::ERR_INVAL);
        // Set TFT to use current render target
        s_syscallTFT.setFramebuffer(GetRenderBuffer(), 
                                     GetRenderWidth(), 
                                     GetRenderHeight());
        return s_syscallTFT.drawString(str, x, y, scale);
    }

    // SYS_DISPLAY_DRAW_CHAR (211) - Draw character at position
    int32_t sys_display_draw_char(int32_t x, int32_t y, uint32_t c, uint32_t color, uint32_t bg, uint32_t size)
    {
        s_syscallTFT.setFramebuffer(GetRenderBuffer(), 
                                     GetRenderWidth(), 
                                     GetRenderHeight());
        s_syscallTFT.drawChar(x, y, static_cast<uint16_t>(c), color, bg, static_cast<uint8_t>(size));
        return 0;
    }

    // SYS_DISPLAY_DRAW_NUMBER (212) - Draw number at position with scale
    int32_t sys_display_draw_number(int32_t num, int32_t x, int32_t y, float scale)
    {
        s_syscallTFT.setFramebuffer(GetRenderBuffer(), 
                                     GetRenderWidth(), 
                                     GetRenderHeight());
        return s_syscallTFT.drawNumber(num, x, y, scale);
    }

    // SYS_DISPLAY_GET_TEXT_WIDTH (213) - Get text width in pixels
    int32_t sys_display_get_text_width(const char* str)
    {
        if (!str) return 0;
        return s_syscallTFT.textWidth(str);
    }

    // SYS_DISPLAY_GET_FONT_HEIGHT (214) - Get font height in pixels
    int32_t sys_display_get_font_height()
    {
        return s_syscallTFT.fontHeight();
    }

    // SYS_DISPLAY_DRAW_LINE (215) - Draw line
    int32_t sys_display_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color)
    {
        s_syscallTFT.setFramebuffer(GetRenderBuffer(), 
                                     GetRenderWidth(), 
                                     GetRenderHeight());
        s_syscallTFT.drawLine(x0, y0, x1, y1, color);
        return 0;
    }

    // SYS_DISPLAY_DRAW_RECT (216) - Draw rectangle outline
    int32_t sys_display_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color)
    {
        s_syscallTFT.setFramebuffer(GetRenderBuffer(), 
                                     GetRenderWidth(), 
                                     GetRenderHeight());
        s_syscallTFT.drawRect(x, y, w, h, color);
        return 0;
    }

    // SYS_DISPLAY_DRAW_CIRCLE (217) - Draw circle outline
    int32_t sys_display_draw_circle(int32_t x, int32_t y, int32_t r, uint32_t color)
    {
        s_syscallTFT.setFramebuffer(GetRenderBuffer(), 
                                     GetRenderWidth(), 
                                     GetRenderHeight());
        s_syscallTFT.drawCircle(x, y, r, color);
        return 0;
    }

    // SYS_DISPLAY_FILL_CIRCLE (218) - Draw filled circle
    int32_t sys_display_fill_circle(int32_t x, int32_t y, int32_t r, uint32_t color)
    {
        s_syscallTFT.setFramebuffer(GetRenderBuffer(), 
                                     GetRenderWidth(), 
                                     GetRenderHeight());
        s_syscallTFT.fillCircle(x, y, r, color);
        return 0;
    }

    // SYS_DISPLAY_SET_FONT (219) - Set font by ID
    int32_t sys_display_set_font(uint32_t fontId)
    {
        s_syscallTFT.setFont(static_cast<uint8_t>(fontId));
        return 0;
    }
    
    // SYS_DISPLAY_SET_TARGET (240) - Set render target buffer (per-task)
    // arg0: buffer pointer (NULL = use system framebuffer)
    // arg1: width
    // arg2: height
    // returns: 0 on success, -1 if no slots available
    int32_t sys_display_set_target(uint32_t buffer_ptr, uint32_t width, uint32_t height)
    {
        const TaskControlBlock* current = const_cast<const TaskControlBlock*>(sCurrentTCB);
        
        if (buffer_ptr == 0) {
            // Clear this task's render target
            for (int i = 0; i < MAX_RENDER_TARGETS; i++) {
                if (s_renderTargets[i].tcb == current) {
                    s_renderTargets[i].tcb = nullptr;
                    s_renderTargets[i].buffer = nullptr;
                    s_renderTargets[i].width = 0;
                    s_renderTargets[i].height = 0;
                    break;
                }
            }
            return 0;
        }
        
        // Find existing entry or empty slot
        int emptySlot = -1;
        for (int i = 0; i < MAX_RENDER_TARGETS; i++) {
            if (s_renderTargets[i].tcb == current) {
                // Update existing entry
                s_renderTargets[i].buffer = reinterpret_cast<uint32_t*>(buffer_ptr);
                s_renderTargets[i].width = width;
                s_renderTargets[i].height = height;
                return 0;
            }
            if (s_renderTargets[i].tcb == nullptr && emptySlot < 0) {
                emptySlot = i;
            }
        }
        
        // Create new entry
        if (emptySlot >= 0) {
            s_renderTargets[emptySlot].tcb = current;
            s_renderTargets[emptySlot].buffer = reinterpret_cast<uint32_t*>(buffer_ptr);
            s_renderTargets[emptySlot].width = width;
            s_renderTargets[emptySlot].height = height;
            return 0;
        }
        
        // No slots available
        return -1;
    }
    
    // SYS_DISPLAY_GET_TARGET (241) - Get current render target for this task
    // returns: pointer to current render buffer (NULL = system FB)
    int32_t sys_display_get_target()
    {
        RenderTargetEntry* entry = GetCurrentTaskRenderTarget();
        if (entry) {
            return reinterpret_cast<int32_t>(entry->buffer);
        }
        return 0;
    }

} // namespace CRTOS
