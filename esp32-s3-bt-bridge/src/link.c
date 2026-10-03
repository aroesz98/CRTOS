/*
 * Binary pad state link to the emulator board (i.MX RT1050).
 *
 * The emulator cannot use the console output, so the controller state also goes
 * out as a fixed 13 byte frame on UART2 (GPIO17 = TX, GPIO16 = RX on a
 * DevKitC). The receiving side is source/gamepad.c in the psxe_embedded
 * project, which documents the frame and the wiring.
 *
 * Frame: a5 5a  buttons[0..2]  lx ly rx ry  l2 r2  seq  xor(bytes 2..11)
 *
 * The button bitmap is passed on exactly as the DualSense reports it; mapping it
 * to a PlayStation controller is the emulator's business, not ours.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "link.h"

LOG_MODULE_REGISTER(link, LOG_LEVEL_INF);

#define LINK_UART_NODE DT_NODELABEL(uart2)

#define LINK_FRAME_LEN 13
#define LINK_SYNC0 0xa5
#define LINK_SYNC1 0x5a

/* Stick and trigger movement alone is not worth a frame more often than this */
#define LINK_ANALOG_INTERVAL_MS 8
/* Repeat an unchanged state at least this often, as a sign of life */
#define LINK_HEARTBEAT_MS 200

#define LINK_STICK_CENTER 0x80

static const struct device *link_uart;
static uint8_t last_payload[9]; /* bytes 2..10 of the last frame sent */
static uint32_t last_sent_ms;
static uint8_t sequence;
static bool have_sent;

void link_init(void)
{
	link_uart = DEVICE_DT_GET(LINK_UART_NODE);

	if (!device_is_ready(link_uart)) {
		LOG_ERR("Pad link UART not ready, the emulator board will see nothing");
		link_uart = NULL;
		return;
	}

	LOG_INF("Pad link up on " DT_NODE_FULL_NAME(LINK_UART_NODE));
}

static void link_write(const uint8_t *payload)
{
	uint8_t frame[LINK_FRAME_LEN];
	uint8_t check = 0;

	frame[0] = LINK_SYNC0;
	frame[1] = LINK_SYNC1;

	memcpy(&frame[2], payload, 9);

	frame[11] = sequence++;

	for (int i = 2; i < LINK_FRAME_LEN - 1; i++) {
		check ^= frame[i];
	}

	frame[LINK_FRAME_LEN - 1] = check;

	for (int i = 0; i < LINK_FRAME_LEN; i++) {
		uart_poll_out(link_uart, frame[i]);
	}

	memcpy(last_payload, payload, 9);
	last_sent_ms = k_uptime_get_32();
	have_sent = true;
}

void link_send_state(uint32_t buttons, const uint8_t *sticks, const uint8_t *triggers)
{
	if (!link_uart) {
		return;
	}

	uint8_t payload[9];

	payload[0] = (uint8_t)(buttons & 0xff);
	payload[1] = (uint8_t)((buttons >> 8) & 0xff);
	payload[2] = (uint8_t)((buttons >> 16) & 0xff);
	payload[3] = sticks[0];
	payload[4] = sticks[1];
	payload[5] = sticks[2];
	payload[6] = sticks[3];
	payload[7] = triggers[0];
	payload[8] = triggers[1];

	if (!have_sent) {
		link_write(payload);
		return;
	}

	const bool buttons_changed = memcmp(&payload[0], &last_payload[0], 3) != 0;
	const bool analog_changed = memcmp(&payload[3], &last_payload[3], 6) != 0;
	const uint32_t idle_ms = k_uptime_get_32() - last_sent_ms;

	/* A press or a release must never wait: it may be shorter than the
	 * analog interval and losing it would be felt in the game.
	 */
	if (buttons_changed) {
		link_write(payload);
		return;
	}

	if (analog_changed && (idle_ms >= LINK_ANALOG_INTERVAL_MS)) {
		link_write(payload);
		return;
	}

	if (idle_ms >= LINK_HEARTBEAT_MS) {
		link_write(payload);
	}
}

void link_send_idle(void)
{
	if (!link_uart) {
		return;
	}

	const uint8_t payload[9] = {
		0, 0, 0,
		LINK_STICK_CENTER, LINK_STICK_CENTER, LINK_STICK_CENTER, LINK_STICK_CENTER,
		0, 0,
	};

	link_write(payload);
}
