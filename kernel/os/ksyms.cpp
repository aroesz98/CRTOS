/*
 * kernel/os/ksyms.cpp - symbols the kernel exports to loadable modules.
 *
 * This list is the module API: everything a driver may call. Keep it in sync with the
 * public headers in include/crtos/.
 */
#include "kernel.h"
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/fb.h>
#include <crtos/gpio.h>
#include <crtos/gpu2d.h>
#include <crtos/i2c.h>
#include <crtos/input.h>
#include <crtos/irq.h>
#include <crtos/pinctrl.h>
#include <crtos/queue.h>
#include <crtos/rtc.h>
#include <crtos/net.h>
#include <crtos/module.h>
#include <crtos/of.h>
#include <crtos/spi.h>
#include <crtos/timer.h>
#include <crtos/uaccess.h>
#include <crtos/vfs.h>
#include <stdlib.h>
#include <string.h>
#include "fsl_device_registers.h"
#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_cache.h"
#include "../lib/kformat.h"
#include "../lib/crc32.h"

#define KSYM(s) { #s, (const void *)&s },

static const struct ksym s_ksyms[] = {
    /* log */
    KSYM(printk) KSYM(vprintk) KSYM(cprintf) KSYM(ksnprintf) KSYM(kvsnprintf) KSYM(log_write) KSYM(panic)
    /* memory */
    KSYM(kmalloc) KSYM(kmalloc_aligned) KSYM(kmalloc_bounded) KSYM(kzalloc) KSYM(kfree) KSYM(ksize)
    /* tasks and time */
    KSYM(kthread_create) KSYM(kthread_stop) KSYM(task_should_stop) KSYM(task_exit) KSYM(task_current) KSYM(task_name) KSYM(task_id) KSYM(task_priority)
    KSYM(task_set_priority) KSYM(task_kill) KSYM(task_yield) KSYM(task_sleep_ticks) KSYM(task_sleep_ms)
    KSYM(tick_get) KSYM(tick_get64) KSYM(time_us) KSYM(sched_lock) KSYM(sched_unlock)
    KSYM(rtc_register) KSYM(rtc_unregister) KSYM(wall_time_us) KSYM(fat_time_now) KSYM(task_errno_ptr)
    /* synchronisation */
    KSYM(wq_init) KSYM(wq_wake_one) KSYM(wq_wake_all) KSYM(mutex_init) KSYM(mutex_lock) KSYM(mutex_trylock)
    KSYM(mutex_unlock) KSYM(sem_init) KSYM(sem_take) KSYM(sem_give) KSYM(event_init) KSYM(event_set)
    KSYM(event_clear) KSYM(event_wait)
    KSYM(queue_init) KSYM(queue_create) KSYM(queue_delete) KSYM(queue_send) KSYM(queue_recv) KSYM(queue_count)
    KSYM(timer_init) KSYM(timer_start) KSYM(timer_stop) KSYM(timer_active)
    /* interrupts */
    KSYM(irq_request) KSYM(irq_free) KSYM(irq_enable) KSYM(irq_disable) KSYM(irq_set_priority) KSYM(irq_pend)
    KSYM(irq_info) KSYM(irq_domain_add) KSYM(irq_domain_remove)
    /* device tree */
    KSYM(of_root) KSYM(of_find_node_by_path) KSYM(of_find_node_by_phandle) KSYM(of_find_compatible_node)
    KSYM(of_get_child_by_name) KSYM(of_next_node) KSYM(of_get_full_name) KSYM(of_get_property)
    KSYM(of_property_read_bool) KSYM(of_property_read_u32) KSYM(of_property_read_u32_index)
    KSYM(of_property_read_u32_array) KSYM(of_property_count_u32) KSYM(of_property_read_string)
    KSYM(of_property_read_string_index) KSYM(of_property_count_strings) KSYM(of_property_match_string)
    KSYM(of_device_is_compatible) KSYM(of_device_is_available) KSYM(of_n_addr_cells) KSYM(of_n_size_cells)
    KSYM(of_get_reg) KSYM(of_parse_phandle) KSYM(of_parse_phandle_with_args) KSYM(of_irq_parent)
    KSYM(of_irq_parse) KSYM(of_irq_count) KSYM(of_alias_get_id) KSYM(of_stdout_node)
    /* driver model */
    KSYM(driver_register) KSYM(driver_unregister) KSYM(device_create_of) KSYM(device_destroy)
    KSYM(device_get_match_data) KSYM(device_get_reg) KSYM(device_map) KSYM(device_get_irq) KSYM(devm_kzalloc)
    KSYM(devm_add_action) KSYM(device_foreach) KSYM(driver_foreach)
    /* files */
    KSYM(vfs_open) KSYM(vfs_opendir) KSYM(vfs_file_get) KSYM(vfs_close) KSYM(vfs_read) KSYM(vfs_write)
    KSYM(vfs_lseek) KSYM(vfs_ioctl) KSYM(vfs_readdir) KSYM(vfs_fstat) KSYM(vfs_sync) KSYM(vfs_stat)
    KSYM(vfs_mkdir) KSYM(vfs_unlink) KSYM(vfs_rename) KSYM(vfs_statfs) KSYM(vfs_load_file) KSYM(vfs_mount) KSYM(vfs_xip)
    KSYM(vfs_umount) KSYM(devfs_register) KSYM(devfs_unregister) KSYM(poll_head_init) KSYM(poll_add) KSYM(poll_notify)
    KSYM(uaccess_ok) KSYM(copy_from_user) KSYM(copy_to_user) KSYM(capable)
    /* modules */
    KSYM(ksym_lookup) KSYM(module_find) KSYM(module_name) KSYM(module_pin)
    /* subsystems */
    KSYM(pinctrl_register) KSYM(pinctrl_unregister) KSYM(pinctrl_select_state)
    KSYM(clk_provider_register) KSYM(clk_provider_unregister) KSYM(devm_clk_get) KSYM(devm_clk_get_enabled)
    KSYM(clk_enable) KSYM(clk_disable) KSYM(clk_get_rate) KSYM(clk_set_rate)
    KSYM(gpiochip_register) KSYM(gpiochip_unregister) KSYM(gpiod_get) KSYM(gpiod_get_index) KSYM(gpiod_get_value)
    KSYM(gpiod_set_value) KSYM(gpiod_direction_input) KSYM(gpiod_direction_output) KSYM(gpiod_to_irq)
    KSYM(irq_alloc_descs) KSYM(irq_free_descs) KSYM(irq_set_type) KSYM(irq_handle_nested)
    KSYM(i2c_adapter_register) KSYM(i2c_adapter_unregister) KSYM(i2c_adapter_get) KSYM(i2c_client_get)
    KSYM(i2c_transfer) KSYM(i2c_write) KSYM(i2c_read) KSYM(i2c_write_read)
    KSYM(spi_controller_register) KSYM(spi_controller_unregister) KSYM(spi_controller_bus) KSYM(spi_device_get)
    KSYM(spi_sync) KSYM(spi_write) KSYM(spi_read) KSYM(spi_write_then_read)
    KSYM(input_register) KSYM(input_set_abs) KSYM(input_set_key) KSYM(input_unregister) KSYM(input_report) KSYM(input_sync)
    KSYM(input_devname)
    KSYM(fb_register) KSYM(fb_unregister) KSYM(fb_get_info) KSYM(fb_show) KSYM(fb_wait_vsync) KSYM(fb_blank)
    KSYM(fb_vsync) KSYM(fb_suggest_size)
    KSYM(netdev_register) KSYM(netdev_unregister) KSYM(netdev_rx) KSYM(netdev_set_link) KSYM(netdev_count)
    KSYM(netdev_get) KSYM(netdev_by_name) KSYM(net_lock) KSYM(net_unlock) KSYM(net_stack_register)
    KSYM(net_stack_unregister) KSYM(net_sock_event)
    KSYM(gpu2d_register) KSYM(gpu2d_unregister) KSYM(gpu2d_fill) KSYM(gpu2d_blit)
    /* C library */
    KSYM(memcpy) KSYM(memmove) KSYM(memset) KSYM(memcmp) KSYM(memchr) KSYM(strlen) KSYM(strnlen) KSYM(strcmp)
    KSYM(strncmp) KSYM(strcpy) KSYM(strncpy) KSYM(strcat) KSYM(strncat) KSYM(strchr) KSYM(strrchr) KSYM(strstr)
    KSYM(strspn) KSYM(strcspn) KSYM(strtoul) KSYM(strtol) KSYM(atoi) KSYM(qsort) KSYM(crc32)
    /* SDK services implemented in the kernel image */
    KSYM(CLOCK_GetFreq) KSYM(CLOCK_GetPllFreq) KSYM(CLOCK_GetSysPfdFreq) KSYM(CLOCK_GetUsb1PfdFreq)
    KSYM(CLOCK_InitVideoPll) KSYM(CLOCK_DeinitVideoPll) KSYM(CLOCK_InitSysPfd) KSYM(CLOCK_InitUsb1Pfd)
    KSYM(CLOCK_InitEnetPll) KSYM(SDK_DelayAtLeastUs) KSYM(SystemCoreClock)
    KSYM(DCACHE_CleanByRange) KSYM(DCACHE_InvalidateByRange) KSYM(DCACHE_CleanInvalidateByRange)
};

const void *ksym_kernel_lookup(const char *name)
{
    for (size_t i = 0; i < ARRAY_SIZE(s_ksyms); i++)
        if (!strcmp(s_ksyms[i].name, name))
            return s_ksyms[i].addr;
    return nullptr;
}

unsigned ksym_kernel_count(void)
{
    return ARRAY_SIZE(s_ksyms);
}
