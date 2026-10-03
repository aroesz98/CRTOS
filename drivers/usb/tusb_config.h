/*
 * tusb_config.h - TinyUSB in CRTOS (drivers/usb): the ChipIdea/EHCI controllers of the i.MX RT
 * as a device (a CDC ACM serial port) and as a host (hubs, HID keyboards and mice); the device
 * tree gives each controller its side. The stack's data lives in the module's memory, which is
 * cached: TinyUSB cleans and invalidates the cache lines the controller works on.
 */
#ifndef TUSB_CONFIG_H_
#define TUSB_CONFIG_H_

#define CFG_TUSB_MCU                  OPT_MCU_MIMXRT1XXX
#define CFG_TUSB_OS                   OPT_OS_CUSTOM
#define CFG_TUSB_DEBUG                0
/* a failed assertion: counted by the driver - TinyUSB's own would stop the core with BKPT
 * whenever a debugger has ever attached (the SWD console) */
#define CFG_TUSB_DEBUG_BREAKPOINT     tusb_breakpoint

/* ---- device ---- */
#define CFG_TUD_ENABLED               1
#define CFG_TUD_MAX_SPEED             OPT_MODE_HIGH_SPEED
#define CFG_TUD_MEM_SECTION
#define CFG_TUD_MEM_ALIGN             __attribute__((aligned(32)))
#define CFG_TUD_MEM_DCACHE_ENABLE     1
#define CFG_TUD_MEM_DCACHE_LINE_SIZE  32
#define CFG_TUD_ENDPOINT0_SIZE        64
#define CFG_TUD_TASK_QUEUE_SZ         32

#define CFG_TUD_CDC                   1
#define CFG_TUD_CDC_RX_BUFSIZE        1024
#define CFG_TUD_CDC_TX_BUFSIZE        1024

/* ---- host ---- */
#define CFG_TUH_ENABLED               1
#define CFG_TUH_MAX_SPEED             OPT_MODE_HIGH_SPEED
#define CFG_TUH_MEM_SECTION
#define CFG_TUH_MEM_ALIGN             __attribute__((aligned(32)))
#define CFG_TUH_MEM_DCACHE_ENABLE     1
#define CFG_TUH_MEM_DCACHE_LINE_SIZE  32

/* also holds a HID report descriptor, which gives a mouse's wheel (gaming mice: ~300 bytes) */
#define CFG_TUH_ENUMERATION_BUFSIZE   512
#define CFG_TUH_TASK_QUEUE_SZ         32
#define CFG_TUH_HUB                   1                           /* hubs */
#define CFG_TUH_DEVICE_MAX            (3 * CFG_TUH_HUB + 1)       /* devices, hubs included */
#define CFG_TUH_HID                   (3 * CFG_TUH_DEVICE_MAX)    /* HID interfaces */
#define CFG_TUH_HID_EPIN_BUFSIZE      64
#define CFG_TUH_HID_EPOUT_BUFSIZE     64
#define CFG_TUH_MSC                   0
#define CFG_TUH_CDC                   0
#define CFG_TUH_VENDOR                0

#endif
