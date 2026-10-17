/*
 * Minimal Bluetooth Classic HID host (HIDP over L2CAP) for a single device.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef HID_HOST_H_
#define HID_HOST_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/conn.h>

/* HIDP report types (Bluetooth HID Profile 1.1, section 3.1.2.3) */
#define HID_REPORT_TYPE_INPUT   0x01
#define HID_REPORT_TYPE_OUTPUT  0x02
#define HID_REPORT_TYPE_FEATURE 0x03

/* All callbacks are invoked from the Bluetooth RX context. */
struct hid_host_cb {
	/** Both the control and the interrupt channel are connected. */
	void (*ready)(void);

	/** Input report from the interrupt channel, report[0] is the report ID. */
	void (*input_report)(const uint8_t *report, size_t len);

	/** The HID session ended, called once per session. */
	void (*closed)(void);
};

/**
 * Register the HID control/interrupt L2CAP servers, so a bonded device can
 * open the channels itself when it reconnects.
 */
int hid_host_init(const struct hid_host_cb *cb);

/**
 * Open the HID channels from the host side. Triggers pairing and encryption
 * first when the link is not secured yet.
 */
int hid_host_connect(struct bt_conn *conn);

/** True when neither HID channel is in use. */
bool hid_host_is_idle(void);

/** Send GET_REPORT on the control channel. */
int hid_host_get_report(uint8_t type, uint8_t id);

#endif /* HID_HOST_H_ */
