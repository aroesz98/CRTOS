/*
 * Syscall_Handlers.hpp - System call handler declarations
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

#ifndef CRTOS_SYSCALL_HANDLERS_HPP
#define CRTOS_SYSCALL_HANDLERS_HPP

#include <cstdint>
#include "SystemCall.hpp"

namespace CRTOS
{
    // Forward declarations
    namespace HAL
    {
        struct IRQEvent;
    }
    
    // Import types from Syscall namespace
    using Syscall::ProcessInfo;
    using Syscall::SystemInfo;
    using Syscall::SyscallError;
    
    // Process management
    int32_t sys_exit(int32_t status);
    int32_t sys_getpid();
    int32_t sys_yield();
    int32_t sys_sleep(uint32_t ms);
    int32_t sys_drop_privileges();   // Drop to user mode (can escalate back with elevate)
    int32_t sys_get_privilege();     // Check if running in privileged mode (1=priv, 0=user)
    int32_t sys_elevate_privileges(); // Temporarily elevate to privileged mode
    
    // Memory management
    void* sys_sbrk(int32_t increment);
    void* sys_malloc(uint32_t size);
    int32_t sys_free(void* ptr);
    
    // I/O operations
    int32_t sys_open(const char* path, int32_t flags, int32_t mode);
    int32_t sys_close(int32_t fd);
    int32_t sys_read(int32_t fd, void* buffer, uint32_t count);
    int32_t sys_write(int32_t fd, const void* buffer, uint32_t count);
    int32_t sys_ioctl(int32_t fd, uint32_t request, void* arg);
    int32_t sys_lseek(int32_t fd, int32_t offset, int32_t whence);
    int32_t sys_fstat(int32_t fd, Syscall::DirEntry* stat);
    int32_t sys_fsync(int32_t fd);
    
    // IRQ operations
    int32_t sys_wait_irq(uint32_t irq_number, HAL::IRQEvent* event_buffer, uint32_t timeout);
    int32_t sys_register_irq(uint32_t irq_number, uint8_t priority);
    int32_t sys_unregister_irq(uint32_t irq_number);
    int32_t sys_poll_irq(uint32_t irq_number, HAL::IRQEvent* event_buffer);
    
    // IPC operations
    int32_t sys_ipc_send(int32_t pid, const void* data, uint32_t size);
    int32_t sys_ipc_recv(void* buffer, uint32_t size, uint32_t timeout);
    int32_t sys_shm_create(uint32_t key, uint32_t size);
    int32_t sys_shm_attach(uint32_t shmid, void** addr);
    int32_t sys_shm_detach(void* addr);
    
    // Time operations
    uint32_t sys_get_tick();
    int32_t sys_get_time(uint64_t* time_us);
    
    // Debug operations
    int32_t sys_debug_print(const char* message);
    int32_t sys_get_process_info(int32_t pid, ProcessInfo* info);
    int32_t sys_get_system_info(SystemInfo* info);
    
    // Synchronization primitives
    int32_t sys_mutex_create();
    int32_t sys_mutex_lock(int32_t mutex_id);
    int32_t sys_mutex_unlock(int32_t mutex_id);
    int32_t sys_mutex_destroy(int32_t mutex_id);
    
    int32_t sys_sem_create(int32_t initial_value);
    int32_t sys_sem_wait(int32_t sem_id);
    int32_t sys_sem_post(int32_t sem_id);
    int32_t sys_sem_destroy(int32_t sem_id);
    
    // Futex operations (fast userspace mutex)
    int32_t sys_futex_wait(uint32_t* uaddr, uint32_t val, uint32_t timeout);
    int32_t sys_futex_wake(uint32_t* uaddr, uint32_t numWake);
    int32_t sys_futex_requeue(uint32_t* uaddr, uint32_t* uaddr2, uint32_t numWake, uint32_t numRequeue);
    int32_t sys_futex_wait_bitset(uint32_t* uaddr, uint32_t val, uint32_t timeout, uint32_t bitset);
    int32_t sys_futex_wake_bitset(uint32_t* uaddr, uint32_t numWake, uint32_t bitset);
    
    // Display operations
    int32_t sys_display_clear(uint32_t color);
    int32_t sys_display_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t color);
    int32_t sys_display_draw_pixel(uint32_t x, uint32_t y, uint32_t color);
    int32_t sys_display_get_pixel(uint32_t x, uint32_t y);
    int32_t sys_display_swap();
    int32_t sys_display_wait_vsync(uint32_t timeout_ms);
    int32_t sys_display_get_framebuffer();
    
    // TFTLIB text and graphics operations
    int32_t sys_display_set_cursor(int32_t x, int32_t y);
    int32_t sys_display_set_text_color(uint32_t fg, uint32_t bg);
    int32_t sys_display_set_text_size(uint32_t size);
    int32_t sys_display_draw_string(const char* str, int32_t x, int32_t y, float scale);
    int32_t sys_display_draw_char(int32_t x, int32_t y, uint32_t c, uint32_t color, uint32_t bg, uint32_t size);
    int32_t sys_display_draw_number(int32_t num, int32_t x, int32_t y, float scale);
    int32_t sys_display_get_text_width(const char* str);
    int32_t sys_display_get_font_height();
    int32_t sys_display_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
    int32_t sys_display_draw_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t color);
    int32_t sys_display_draw_circle(int32_t x, int32_t y, int32_t r, uint32_t color);
    int32_t sys_display_fill_circle(int32_t x, int32_t y, int32_t r, uint32_t color);
    int32_t sys_display_set_font(uint32_t fontId);
    
    // Filesystem operations
    int32_t sys_fs_opendir(const char* path);
    int32_t sys_fs_readdir(int32_t dir_handle, Syscall::DirEntry* entry);
    int32_t sys_fs_closedir(int32_t dir_handle);
    int32_t sys_fs_stat(const char* path, Syscall::DirEntry* entry);
    int32_t sys_fs_getcwd(char* buffer, uint32_t size);
    int32_t sys_fs_chdir(const char* path);
    
    // PXP Hardware Accelerator operations
    int32_t sys_pxp_scale(uint32_t params_ptr);
    int32_t sys_pxp_copy(uint32_t src_ptr, uint32_t dst_ptr, 
                         uint32_t src_xy, uint32_t dst_xy, uint32_t size);
    int32_t sys_pxp_fill(uint32_t dst_ptr, uint32_t xy, uint32_t size, uint32_t color);
    int32_t sys_pxp_blend(uint32_t params_ptr);
    int32_t sys_pxp_is_busy();
    int32_t sys_pxp_wait(uint32_t timeout_ms);
    int32_t sys_pxp_scale_async(uint32_t params_ptr);
    
    // Display extended operations (render target)
    int32_t sys_display_set_target(uint32_t buffer_ptr, uint32_t width, uint32_t height);
    int32_t sys_display_get_target();
    
} // namespace CRTOS

#endif // CRTOS_SYSCALL_HANDLERS_HPP
