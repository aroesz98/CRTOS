/*
 * Syscall_Dispatcher.cpp - CRTOS System Call Dispatcher
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
 * Main dispatcher that routes system calls to appropriate kernel functions.
 */

#include "SystemCall.hpp"
#include "Syscall_Handlers.hpp"
#include "../HAL/Interrupt.hpp"
#include "stdio.h"

namespace CRTOS
{
namespace Syscall
{
    //
    // Main syscall dispatcher
    // Called from SVC_Handler with extracted parameters
    //
    int32_t SVC_Dispatch(uint32_t syscallNum, uint32_t arg0, uint32_t arg1, 
                         uint32_t arg2, uint32_t arg3)
    {
        
        // Validate syscall number
        if (syscallNum >= static_cast<uint32_t>(SyscallNumber::SYS_MAX_SYSCALL))
        {
            printf("[SVC_Dispatch] ERROR: syscallNum %lu >= SYS_MAX_SYSCALL\r\n", syscallNum);
            return static_cast<int32_t>(SyscallError::ERR_NOSYS); // Function not implemented
        }
        
        SyscallNumber syscall = static_cast<SyscallNumber>(syscallNum);
        
        // Dispatch to appropriate handler based on syscall number
        switch (syscall)
        {
            // ===== Process Management =====
            case SyscallNumber::SYS_EXIT:
                return CRTOS::sys_exit((int32_t)arg0);
                
            case SyscallNumber::SYS_GETPID:
                return CRTOS::sys_getpid();
                
            case SyscallNumber::SYS_YIELD:
                return CRTOS::sys_yield();
                
            case SyscallNumber::SYS_SLEEP:
                return CRTOS::sys_sleep(arg0);
                
            case SyscallNumber::SYS_DROP_PRIVILEGES:
                return CRTOS::sys_drop_privileges();
                
            case SyscallNumber::SYS_GET_PRIVILEGE:
                return CRTOS::sys_get_privilege();
                
            case SyscallNumber::SYS_ELEVATE_PRIVILEGES:
                return CRTOS::sys_elevate_privileges();
            
            // ===== Memory Management =====
            case SyscallNumber::SYS_SBRK:
                return (int32_t)CRTOS::sys_sbrk((int32_t)arg0);
                
            case SyscallNumber::SYS_MALLOC:
                return (int32_t)CRTOS::sys_malloc((size_t)arg0);
                
            case SyscallNumber::SYS_FREE:
                return CRTOS::sys_free((void*)arg0);
            
            // ===== I/O Operations =====
            case SyscallNumber::SYS_OPEN:
                return CRTOS::sys_open((const char*)arg0, arg1, arg2);
                
            case SyscallNumber::SYS_CLOSE:
                return CRTOS::sys_close((int32_t)arg0);
                
            case SyscallNumber::SYS_READ:
                return CRTOS::sys_read((int32_t)arg0, (void*)arg1, (size_t)arg2);
                
            case SyscallNumber::SYS_WRITE:
                return CRTOS::sys_write((int32_t)arg0, (const void*)arg1, (size_t)arg2);
                
            case SyscallNumber::SYS_IOCTL:
                return CRTOS::sys_ioctl((int32_t)arg0, arg1, (void*)arg2);
                
            case SyscallNumber::SYS_LSEEK:
                return CRTOS::sys_lseek((int32_t)arg0, (int32_t)arg1, (int32_t)arg2);
                
            case SyscallNumber::SYS_FSTAT:
                return CRTOS::sys_fstat((int32_t)arg0, (Syscall::DirEntry*)arg1);
                
            case SyscallNumber::SYS_FSYNC:
                return CRTOS::sys_fsync((int32_t)arg0);
            
            // ===== Interrupt/Event Handling =====
            case SyscallNumber::SYS_WAIT_IRQ:
                return CRTOS::sys_wait_irq(arg0, (HAL::IRQEvent*)arg1, arg2);
                
            case SyscallNumber::SYS_REGISTER_IRQ:
                return CRTOS::sys_register_irq(arg0, (uint8_t)arg1);
                
            case SyscallNumber::SYS_UNREGISTER_IRQ:
                return CRTOS::sys_unregister_irq(arg0);
                
            case SyscallNumber::SYS_POLL_IRQ:
                return CRTOS::sys_poll_irq(arg0, (HAL::IRQEvent*)arg1);
            
            // ===== IPC =====
            case SyscallNumber::SYS_IPC_SEND:
                return CRTOS::sys_ipc_send(arg0, (const void*)arg1, (size_t)arg2);
                
            case SyscallNumber::SYS_IPC_RECV:
                return CRTOS::sys_ipc_recv((void*)arg0, (size_t)arg1, arg2);
                
            case SyscallNumber::SYS_SHM_CREATE:
                return (int32_t)CRTOS::sys_shm_create((uint32_t)arg0, (uint32_t)arg1);
                
            case SyscallNumber::SYS_SHM_ATTACH:
                return (int32_t)CRTOS::sys_shm_attach(arg0, (void**)arg1);
                
            case SyscallNumber::SYS_SHM_DETACH:
                return CRTOS::sys_shm_detach((void*)arg0);
            
            // ===== Time =====
            case SyscallNumber::SYS_GET_TICK:
                return (int32_t)CRTOS::sys_get_tick();
                
            case SyscallNumber::SYS_GET_TIME:
                return (int32_t)CRTOS::sys_get_time((uint64_t*)arg0);
            
            // ===== Debug/Info =====
            case SyscallNumber::SYS_DEBUG_PRINT:
                return CRTOS::sys_debug_print((const char*)arg0);
                
            case SyscallNumber::SYS_GET_PROCESS_INFO:
                return CRTOS::sys_get_process_info((int32_t)arg0, (ProcessInfo*)arg1);
                
            case SyscallNumber::SYS_GET_SYSTEM_INFO:
                return CRTOS::sys_get_system_info((SystemInfo*)arg0);
            
            // ===== Synchronization =====
            case SyscallNumber::SYS_MUTEX_CREATE:
                return CRTOS::sys_mutex_create();
                
            case SyscallNumber::SYS_MUTEX_LOCK:
                return CRTOS::sys_mutex_lock((int32_t)arg0);
                
            case SyscallNumber::SYS_MUTEX_UNLOCK:
                return CRTOS::sys_mutex_unlock((int32_t)arg0);
                
            case SyscallNumber::SYS_MUTEX_DESTROY:
                return CRTOS::sys_mutex_destroy((int32_t)arg0);
                
            case SyscallNumber::SYS_SEM_CREATE:
                return CRTOS::sys_sem_create(arg0);
                
            case SyscallNumber::SYS_SEM_WAIT:
                return CRTOS::sys_sem_wait((int32_t)arg0);
                
            case SyscallNumber::SYS_SEM_SIGNAL:
                return CRTOS::sys_sem_post((int32_t)arg0);
                
            case SyscallNumber::SYS_SEM_DESTROY:
                return CRTOS::sys_sem_destroy((int32_t)arg0);
            
            // ===== Futex Operations =====
            case SyscallNumber::SYS_FUTEX_WAIT:
                return CRTOS::sys_futex_wait((uint32_t*)arg0, arg1, arg2);
                
            case SyscallNumber::SYS_FUTEX_WAKE:
                return CRTOS::sys_futex_wake((uint32_t*)arg0, arg1);
                
            case SyscallNumber::SYS_FUTEX_REQUEUE:
                return CRTOS::sys_futex_requeue((uint32_t*)arg0, (uint32_t*)arg1, arg2, arg3);
                
            case SyscallNumber::SYS_FUTEX_WAIT_BITSET:
                return CRTOS::sys_futex_wait_bitset((uint32_t*)arg0, arg1, arg2, arg3);
                
            case SyscallNumber::SYS_FUTEX_WAKE_BITSET:
                return CRTOS::sys_futex_wake_bitset((uint32_t*)arg0, arg1, arg2);
            
            // ===== Display Operations =====
            case SyscallNumber::SYS_DISPLAY_CLEAR:
                return CRTOS::sys_display_clear(arg0);
                
            case SyscallNumber::SYS_DISPLAY_FILL_RECT:
            {
                // arg0 = rect_t* (x, y, width, height)
                // arg1 = color
                struct { uint16_t x, y, width, height; }* rect = (decltype(rect))arg0;
                return CRTOS::sys_display_fill_rect(rect->x, rect->y, rect->width, rect->height, arg1);
            }
                
            case SyscallNumber::SYS_DISPLAY_DRAW_PIXEL:
                return CRTOS::sys_display_draw_pixel(arg0, arg1, arg2);
                
            case SyscallNumber::SYS_DISPLAY_GET_PIXEL:
                return CRTOS::sys_display_get_pixel(arg0, arg1);
                
            case SyscallNumber::SYS_DISPLAY_SWAP:
                return CRTOS::sys_display_swap();
                
            case SyscallNumber::SYS_DISPLAY_WAIT_VSYNC:
                return CRTOS::sys_display_wait_vsync(arg0);
                
            case SyscallNumber::SYS_DISPLAY_GET_FRAMEBUFFER:
                return CRTOS::sys_display_get_framebuffer();
            
            case SyscallNumber::SYS_DISPLAY_SET_CURSOR:
                return CRTOS::sys_display_set_cursor((int32_t)arg0, (int32_t)arg1);
                
            case SyscallNumber::SYS_DISPLAY_SET_TEXT_COLOR:
                return CRTOS::sys_display_set_text_color(arg0, arg1);
                
            case SyscallNumber::SYS_DISPLAY_SET_TEXT_SIZE:
                return CRTOS::sys_display_set_text_size(arg0);
                
            case SyscallNumber::SYS_DISPLAY_DRAW_STRING: {
                union { uint32_t u; float f; } scale_conv;
                scale_conv.u = arg3;
                return CRTOS::sys_display_draw_string((const char*)arg0, (int32_t)arg1, (int32_t)arg2, scale_conv.f);
            }
                
            case SyscallNumber::SYS_DISPLAY_DRAW_CHAR:
            {
                // arg0=x, arg1=y, arg2=char, arg3=color - need additional args for bg and size
                // For simplicity, pack color in arg2 upper 16 bits, char in lower 16 bits
                // Or use default bg=transparent (0xFFFFFFFF) and size=1
                return CRTOS::sys_display_draw_char((int32_t)arg0, (int32_t)arg1, arg2, arg3, 0xFFFFFFFF, 1);
            }
                
            case SyscallNumber::SYS_DISPLAY_DRAW_NUMBER: {
                union { uint32_t u; float f; } scale_conv;
                scale_conv.u = arg3;
                return CRTOS::sys_display_draw_number((int32_t)arg0, (int32_t)arg1, (int32_t)arg2, scale_conv.f);
            }
                
            case SyscallNumber::SYS_DISPLAY_GET_TEXT_WIDTH:
                return CRTOS::sys_display_get_text_width((const char*)arg0);
                
            case SyscallNumber::SYS_DISPLAY_GET_FONT_HEIGHT:
                return CRTOS::sys_display_get_font_height();
                
            case SyscallNumber::SYS_DISPLAY_DRAW_LINE:
                return CRTOS::sys_display_draw_line((int32_t)arg0, (int32_t)arg1, (int32_t)arg2, (int32_t)arg3, 0xFFFFFF);
                
            case SyscallNumber::SYS_DISPLAY_DRAW_RECT:
                return CRTOS::sys_display_draw_rect((int32_t)arg0, (int32_t)arg1, (int32_t)arg2, (int32_t)arg3, 0xFFFFFF);
                
            case SyscallNumber::SYS_DISPLAY_DRAW_CIRCLE:
                return CRTOS::sys_display_draw_circle((int32_t)arg0, (int32_t)arg1, (int32_t)arg2, arg3);
                
            case SyscallNumber::SYS_DISPLAY_FILL_CIRCLE:
                return CRTOS::sys_display_fill_circle((int32_t)arg0, (int32_t)arg1, (int32_t)arg2, arg3);
            
            case SyscallNumber::SYS_DISPLAY_SET_FONT:
                return CRTOS::sys_display_set_font(arg0);
            
            // ===== Filesystem Operations =====
            case SyscallNumber::SYS_FS_OPENDIR:
                return CRTOS::sys_fs_opendir((const char*)arg0);
                
            case SyscallNumber::SYS_FS_READDIR:
                return CRTOS::sys_fs_readdir((int32_t)arg0, (Syscall::DirEntry*)arg1);
                
            case SyscallNumber::SYS_FS_CLOSEDIR:
                return CRTOS::sys_fs_closedir((int32_t)arg0);
                
            case SyscallNumber::SYS_FS_STAT:
                return CRTOS::sys_fs_stat((const char*)arg0, (Syscall::DirEntry*)arg1);
                
            case SyscallNumber::SYS_FS_GETCWD:
                return CRTOS::sys_fs_getcwd((char*)arg0, arg1);
                
            case SyscallNumber::SYS_FS_CHDIR:
                return CRTOS::sys_fs_chdir((const char*)arg0);
            
            // ===== PXP Hardware Accelerator Operations =====
            case SyscallNumber::SYS_PXP_SCALE:
                return CRTOS::sys_pxp_scale(arg0);
                
            case SyscallNumber::SYS_PXP_COPY:
                return CRTOS::sys_pxp_copy(arg0, arg1, arg2, arg3, 0);
                
            case SyscallNumber::SYS_PXP_FILL:
                return CRTOS::sys_pxp_fill(arg0, arg1, arg2, arg3);
                
            case SyscallNumber::SYS_PXP_BLEND:
                return CRTOS::sys_pxp_blend(arg0);
                
            case SyscallNumber::SYS_PXP_IS_BUSY:
                return CRTOS::sys_pxp_is_busy();
                
            case SyscallNumber::SYS_PXP_WAIT:
                return CRTOS::sys_pxp_wait(arg0);
                
            case SyscallNumber::SYS_PXP_SCALE_ASYNC:
                return CRTOS::sys_pxp_scale_async(arg0);
            
            // ===== Display Extended Operations =====
            case SyscallNumber::SYS_DISPLAY_SET_TARGET:
                return CRTOS::sys_display_set_target(arg0, arg1, arg2);
                
            case SyscallNumber::SYS_DISPLAY_GET_TARGET:
                return CRTOS::sys_display_get_target();
            
            // ===== Unknown/Unimplemented =====
            default:
                return static_cast<int32_t>(SyscallError::ERR_NOSYS);
        }
    }

} // namespace Syscall
} // namespace CRTOS
