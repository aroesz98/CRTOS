/*
 * Binary pad state link to the emulator board (i.MX RT1050).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LINK_H_
#define LINK_H_

#include <stdint.h>

/** Brings up the UART the emulator board listens on. */
void link_init(void);

/**
 * Forwards one controller state. Button changes go out immediately, stick and
 * trigger movement is rate limited, and an unchanged state is repeated now and
 * then so the receiver can tell the link is alive.
 *
 * @param buttons  button bitmap, the layout of enum ds_button
 * @param sticks   LX, LY, RX, RY as they come off the report (0x80 centred)
 * @param triggers L2, R2 as they come off the report (0 .. 255)
 */
void link_send_state(uint32_t buttons, const uint8_t *sticks, const uint8_t *triggers);

/** Sends a released, centred state, for when the controller goes away. */
void link_send_idle(void);

#endif /* LINK_H_ */
