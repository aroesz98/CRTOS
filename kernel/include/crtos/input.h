/*
 * crtos/input.h - input devices (keys, touch screens, mice, gamepads).
 *
 * Drivers report Linux-compatible events; each device appears as /dev/eventN. Every open
 * file has its own event queue, read() returns whole struct input_event records and blocks
 * until at least one is available (unless opened O_NONBLOCK).
 */
#ifndef CRTOS_INPUT_H
#define CRTOS_INPUT_H

#include <stdint.h>
#include <crtos/ioctl.h>
#include <crtos/keys.h>

#ifdef __cplusplus
extern "C" {
#endif

struct input_event {
    uint32_t sec;
    uint32_t usec;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

#define EV_SYN      0x00
#define EV_KEY      0x01
#define EV_REL      0x02
#define EV_ABS      0x03
#define SYN_REPORT  0

#define REL_X               0x00
#define REL_Y               0x01
#define REL_HWHEEL          0x06    /* horizontal wheel: right positive */
#define REL_WHEEL           0x08    /* wheel: away from the user (up) positive */

#define ABS_X               0x00    /* gamepad: left stick, right positive */
#define ABS_Y               0x01    /* gamepad: left stick, down positive */
#define ABS_Z               0x02    /* gamepad: left trigger */
#define ABS_RX              0x03    /* gamepad: right stick */
#define ABS_RY              0x04
#define ABS_RZ              0x05    /* gamepad: right trigger */
#define INPUT_ABS_AXES      6       /* ABS_X..ABS_RZ have a range (input_set_abs, INPUT_IOC_GET_ABS) */
#define ABS_MT_SLOT         0x2f
#define ABS_MT_POSITION_X   0x35
#define ABS_MT_POSITION_Y   0x36
#define ABS_MT_TRACKING_ID  0x39

struct input_absinfo {
    int32_t value, minimum, maximum;
};

/* the keys a device can report: one bit per key code (crtos/keys.h), like Linux's
 * EVIOCGBIT(EV_KEY) - a keyboard is a device with the letter keys */
#define INPUT_KEYBITS_SIZE   ((KEY_MAX + 1) / 8)
static inline int input_has_key(const uint8_t *keybits, unsigned code)
{
    return code <= KEY_MAX && (keybits[code / 8] >> (code % 8)) & 1;
}

/* ioctls on /dev/eventN */
#define INPUT_NAME_MAX       32
#define INPUT_IOC_GET_NAME   _IOC(_IOC_READ, 'E', 1, INPUT_NAME_MAX)    /* arg: char[32] */
#define INPUT_IOC_GET_ABS_X  _IOR('E', 2, struct input_absinfo)
#define INPUT_IOC_GET_ABS_Y  _IOR('E', 3, struct input_absinfo)
#define INPUT_IOC_GET_KEYBITS _IOC(_IOC_READ, 'E', 4, INPUT_KEYBITS_SIZE)   /* arg: uint8_t[96] */
/* the range and value of any axis below INPUT_ABS_AXES (as Linux's EVIOCGABS); maximum <=
 * minimum: the device has no such axis */
#define INPUT_IOC_GET_ABS(axis) _IOC(_IOC_READ, 'E', 0x40u + (unsigned)(axis), sizeof(struct input_absinfo))

struct input_dev;

struct input_dev *input_register(const char *name);
void input_set_abs(struct input_dev *d, unsigned axis, int32_t min, int32_t max);   /* axis < INPUT_ABS_AXES */
/* the device can report this key (a key it reports is marked as well, but only then) */
void input_set_key(struct input_dev *d, uint16_t code);
void input_unregister(struct input_dev *d);
void input_report(struct input_dev *d, uint16_t type, uint16_t code, int32_t value);
void input_sync(struct input_dev *d);
const char *input_devname(const struct input_dev *d);   /* "event0" */

#ifdef __cplusplus
}
#endif

#endif
