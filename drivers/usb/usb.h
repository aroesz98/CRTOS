/*
 * usb.h - the parts of the USB driver (drivers/usb): the controllers (usb-imxrt.c) start the
 * two sides, each on its own controller. A side runs its TinyUSB work in its controller's USB
 * thread.
 */
#ifndef DRIVERS_USB_H
#define DRIVERS_USB_H

#include <stdbool.h>
#include <stdint.h>

/* usb-hid.c - host: keyboards and mice as input devices */
/* held keys repeat, mice go over to the report protocol; returns how long the thread may
 * wait (ms) */
uint32_t usb_hid_repeat(void);
void usb_hid_stop(void);        /* the controller stops: their input devices go */

/* usb-acm.c - device: a USB serial port for the computer, /dev/ttyACM0 */
int usb_acm_start(void);        /* registers /dev/ttyACM0 */
void usb_acm_ready(void);       /* TinyUSB runs: the file side may ask the thread for work */
void usb_acm_poll(void);        /* the USB thread, at least every 100 ms */
void usb_acm_stop(void);

#endif
