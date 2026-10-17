/*
 * DualSense / DualSense Edge input report parser.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DUALSENSE_H_
#define DUALSENSE_H_

#include <stddef.h>
#include <stdint.h>

/** Forget the controller state, call when a controller (dis)connects. */
void dualsense_reset(void);

/**
 * Ask the controller for its full input report. Until then it only sends the
 * basic report, which lacks the mute button and the Edge Fn/back buttons.
 * To be called once the HID channels are up.
 */
int dualsense_enable_full_report(void);

/**
 * Parse an input report (report[0] is the report ID), print button changes
 * and stick/trigger movement.
 */
void dualsense_handle_input_report(const uint8_t *report, size_t len);

#endif /* DUALSENSE_H_ */
