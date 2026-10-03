/*
 * DualSense / DualSense Edge input report parser.
 *
 * Report layouts follow the Linux hid-playstation driver, the DualSense Edge
 * Fn/back button bits follow SDL's PS5 HIDAPI driver.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "dualsense.h"
#include "hid_host.h"
#include "link.h"

LOG_MODULE_REGISTER(dualsense, LOG_LEVEL_INF);

/*
 * Basic report, the only one sent over Bluetooth after connecting:
 *   ID, LX, LY, RX, RY, buttons[3], L2, R2
 */
#define DS_REPORT_BASIC          0x01
#define DS_BASIC_STICKS_OFFSET   1
#define DS_BASIC_BUTTONS_OFFSET  5
#define DS_BASIC_TRIGGERS_OFFSET 8
#define DS_BASIC_MIN_LEN         10

/*
 * Full report over Bluetooth, sent once the calibration feature report has
 * been read:
 *   ID, sequence tag, LX, LY, RX, RY, L2, R2, counter, buttons[4], ...
 */
#define DS_REPORT_FULL_BT          0x31
#define DS_FULL_BT_STICKS_OFFSET   2
#define DS_FULL_BT_TRIGGERS_OFFSET 6
#define DS_FULL_BT_BUTTONS_OFFSET  9
#define DS_FULL_BT_MIN_LEN         12

#define DS_FEATURE_REPORT_CALIBRATION 0x05

#define DS_BUTTONS0_HAT_MASK 0x0f
/* buttons[2] of the basic report carries a counter in its upper bits */
#define DS_BASIC_BUTTONS2_MASK 0x03

/* Stick axes rest at mid-scale, ignore the jitter of a stick left alone */
#define DS_STICK_CENTER   0x80
#define DS_STICK_DEADZONE 10
/* Smallest analog change worth printing, keeps sensor noise off the console */
#define DS_ANALOG_MIN_DELTA 4
/* Per control; the console cannot keep up with every single report */
#define DS_ANALOG_PRINT_INTERVAL_MS 50

/*
 * Bit positions mirror buttons[0..2] of the input report, except for the low
 * nibble of buttons[0], where the hat switch is expanded into four d-pad bits.
 */
enum ds_button {
	DS_BTN_DPAD_UP,
	DS_BTN_DPAD_RIGHT,
	DS_BTN_DPAD_DOWN,
	DS_BTN_DPAD_LEFT,
	DS_BTN_SQUARE,
	DS_BTN_CROSS,
	DS_BTN_CIRCLE,
	DS_BTN_TRIANGLE,

	DS_BTN_L1,
	DS_BTN_R1,
	DS_BTN_L2,
	DS_BTN_R2,
	DS_BTN_CREATE,
	DS_BTN_OPTIONS,
	DS_BTN_L3,
	DS_BTN_R3,

	DS_BTN_PS,
	DS_BTN_TOUCHPAD,
	DS_BTN_MUTE,
	DS_BTN_RESERVED,
	/* DualSense Edge only */
	DS_BTN_FN_LEFT,
	DS_BTN_FN_RIGHT,
	DS_BTN_BACK_LEFT,
	DS_BTN_BACK_RIGHT,

	DS_BTN_COUNT,
};

static const char *const button_names[DS_BTN_COUNT] = {
	[DS_BTN_DPAD_UP] = "D-PAD UP",
	[DS_BTN_DPAD_RIGHT] = "D-PAD RIGHT",
	[DS_BTN_DPAD_DOWN] = "D-PAD DOWN",
	[DS_BTN_DPAD_LEFT] = "D-PAD LEFT",
	[DS_BTN_SQUARE] = "SQUARE",
	[DS_BTN_CROSS] = "CROSS",
	[DS_BTN_CIRCLE] = "CIRCLE",
	[DS_BTN_TRIANGLE] = "TRIANGLE",
	[DS_BTN_L1] = "L1",
	[DS_BTN_R1] = "R1",
	[DS_BTN_L2] = "L2",
	[DS_BTN_R2] = "R2",
	[DS_BTN_CREATE] = "CREATE",
	[DS_BTN_OPTIONS] = "OPTIONS",
	[DS_BTN_L3] = "L3",
	[DS_BTN_R3] = "R3",
	[DS_BTN_PS] = "PS",
	[DS_BTN_TOUCHPAD] = "TOUCHPAD",
	[DS_BTN_MUTE] = "MUTE",
	[DS_BTN_FN_LEFT] = "FN LEFT",
	[DS_BTN_FN_RIGHT] = "FN RIGHT",
	[DS_BTN_BACK_LEFT] = "BACK LEFT",
	[DS_BTN_BACK_RIGHT] = "BACK RIGHT",
};

/* Hat switch: 0 = up, then clockwise in 45 degree steps, 8 = released */
static const uint8_t hat_to_dpad[] = {
	BIT(DS_BTN_DPAD_UP),
	BIT(DS_BTN_DPAD_UP) | BIT(DS_BTN_DPAD_RIGHT),
	BIT(DS_BTN_DPAD_RIGHT),
	BIT(DS_BTN_DPAD_RIGHT) | BIT(DS_BTN_DPAD_DOWN),
	BIT(DS_BTN_DPAD_DOWN),
	BIT(DS_BTN_DPAD_DOWN) | BIT(DS_BTN_DPAD_LEFT),
	BIT(DS_BTN_DPAD_LEFT),
	BIT(DS_BTN_DPAD_LEFT) | BIT(DS_BTN_DPAD_UP),
};

/* Last printed state of a stick */
struct ds_stick {
	int x;
	int y;
	uint32_t printed_at;
};

/* Last printed state of a trigger */
struct ds_trigger {
	int value;
	uint32_t printed_at;
};

static uint32_t prev_buttons;
static struct ds_stick left_stick;
static struct ds_stick right_stick;
static struct ds_trigger l2_trigger;
static struct ds_trigger r2_trigger;
static bool full_report_seen;

static uint32_t decode_buttons(const uint8_t *raw, uint8_t buttons2_mask)
{
	uint8_t hat = raw[0] & DS_BUTTONS0_HAT_MASK;
	uint32_t buttons;

	buttons = (raw[0] & ~DS_BUTTONS0_HAT_MASK) | (raw[1] << 8) |
		  ((uint32_t)(raw[2] & buttons2_mask) << 16);
	buttons &= ~BIT(DS_BTN_RESERVED);

	if (hat < ARRAY_SIZE(hat_to_dpad)) {
		buttons |= hat_to_dpad[hat];
	}

	return buttons;
}

static void print_button_changes(uint32_t buttons)
{
	uint32_t changed = buttons ^ prev_buttons;

	for (int i = 0; i < DS_BTN_COUNT; i++) {
		if (changed & BIT(i)) {
			printk("[DualSense] %s %s\n", button_names[i],
			       (buttons & BIT(i)) ? "pressed" : "released");
		}
	}

	prev_buttons = buttons;
}

static bool analog_changed(int value, int printed)
{
	if (value == printed) {
		return false;
	}

	/* Never leave a stale value on the console once the control is at rest */
	if (value == 0) {
		return true;
	}

	return abs(value - printed) >= DS_ANALOG_MIN_DELTA;
}

static int stick_axis(uint8_t raw)
{
	int value = raw - DS_STICK_CENTER;

	return (abs(value) < DS_STICK_DEADZONE) ? 0 : value;
}

/*
 * Prints each axis as an offset from the center: 0 = centered, about +/-127 at
 * the end stops, right and up are positive.
 */
static void print_stick_change(const char *name, struct ds_stick *stick, const uint8_t *raw,
			       uint32_t now)
{
	int x = stick_axis(raw[0]);
	/* The report has the Y axis pointing down */
	int y = -stick_axis(raw[1]);

	if (now - stick->printed_at < DS_ANALOG_PRINT_INTERVAL_MS) {
		return;
	}

	if (!analog_changed(x, stick->x) && !analog_changed(y, stick->y)) {
		return;
	}

	printk("[DualSense] %s x = %d, y = %d\n", name, x, y);

	stick->x = x;
	stick->y = y;
	stick->printed_at = now;
}

/* Prints 0 (released) .. 255 (fully pressed) */
static void print_trigger_change(const char *name, struct ds_trigger *trigger, uint8_t value,
				 uint32_t now)
{
	if (now - trigger->printed_at < DS_ANALOG_PRINT_INTERVAL_MS) {
		return;
	}

	if (!analog_changed(value, trigger->value) &&
	    !(value == UINT8_MAX && trigger->value != UINT8_MAX)) {
		return;
	}

	printk("[DualSense] %s = %d\n", name, value);

	trigger->value = value;
	trigger->printed_at = now;
}

static void handle_state(const uint8_t *sticks, const uint8_t *triggers, uint32_t buttons)
{
	uint32_t now = k_uptime_get_32();

	/* The emulator board gets every state, unfiltered; the console filtering
	   below is only there to keep the log readable. */
	link_send_state(buttons, sticks, triggers);

	print_button_changes(buttons);
	print_stick_change("LEFT STICK", &left_stick, &sticks[0], now);
	print_stick_change("RIGHT STICK", &right_stick, &sticks[2], now);
	print_trigger_change("L2", &l2_trigger, triggers[0], now);
	print_trigger_change("R2", &r2_trigger, triggers[1], now);
}

void dualsense_reset(void)
{
	prev_buttons = 0;
	left_stick = (struct ds_stick){0};
	right_stick = (struct ds_stick){0};
	l2_trigger = (struct ds_trigger){0};
	r2_trigger = (struct ds_trigger){0};
	full_report_seen = false;

	/* Whatever the controller was holding when it went away must not stay
	   pressed in the emulator. */
	link_send_idle();
}

int dualsense_enable_full_report(void)
{
	/* Reading the calibration data is what switches the report format */
	return hid_host_get_report(HID_REPORT_TYPE_FEATURE, DS_FEATURE_REPORT_CALIBRATION);
}

void dualsense_handle_input_report(const uint8_t *report, size_t len)
{
	switch (report[0]) {
	case DS_REPORT_BASIC:
		if (len < DS_BASIC_MIN_LEN) {
			return;
		}

		handle_state(&report[DS_BASIC_STICKS_OFFSET], &report[DS_BASIC_TRIGGERS_OFFSET],
			     decode_buttons(&report[DS_BASIC_BUTTONS_OFFSET],
					    DS_BASIC_BUTTONS2_MASK));
		break;
	case DS_REPORT_FULL_BT:
		if (len < DS_FULL_BT_MIN_LEN) {
			return;
		}

		if (!full_report_seen) {
			full_report_seen = true;
			LOG_INF("Full input report active");
		}

		handle_state(&report[DS_FULL_BT_STICKS_OFFSET],
			     &report[DS_FULL_BT_TRIGGERS_OFFSET],
			     decode_buttons(&report[DS_FULL_BT_BUTTONS_OFFSET], 0xff));
		break;
	default:
		LOG_DBG("Unhandled input report 0x%02x (%u bytes)", report[0], (unsigned int)len);
		break;
	}
}
