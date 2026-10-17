/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Pins of the STM32 for CRTOS device trees, as Linux and Zephyr write them:
 *
 *     pinmux = <STM32_PINMUX(D, 1, AF8)>, <STM32_PINMUX(D, 0, AF8)>;
 *     bias-pull-up;                 (bias-pull-down, bias-disable)
 *     drive-open-drain;             (drive-push-pull)
 *     slew-rate = <3>;              0 low ... 3 very high speed
 *
 * One cell per pin: the port (A = 0 ... P = 15), the pin and its function: an alternate
 * function AF0-AF15, ANALOG or GPIO (input; a driver that uses the pin as a GPIO sets the
 * direction itself). The pinctrl-stm32 module reads them.
 */
#ifndef DT_BINDINGS_PINCTRL_STM32_H
#define DT_BINDINGS_PINCTRL_STM32_H

#define STM32_PORT_A 0
#define STM32_PORT_B 1
#define STM32_PORT_C 2
#define STM32_PORT_D 3
#define STM32_PORT_E 4
#define STM32_PORT_F 5
#define STM32_PORT_G 6
#define STM32_PORT_H 7
#define STM32_PORT_M 12
#define STM32_PORT_N 13
#define STM32_PORT_O 14
#define STM32_PORT_P 15

#define AF0  0
#define AF1  1
#define AF2  2
#define AF3  3
#define AF4  4
#define AF5  5
#define AF6  6
#define AF7  7
#define AF8  8
#define AF9  9
#define AF10 10
#define AF11 11
#define AF12 12
#define AF13 13
#define AF14 14
#define AF15 15
#define ANALOG 16
#define GPIO 17

/* (port << 12) | (pin << 8) | function */
#define STM32_PINMUX(port, pin, func) (((STM32_PORT_##port) << 12) | ((pin) << 8) | (func))

#define STM32_SLEW_LOW       0
#define STM32_SLEW_MEDIUM    1
#define STM32_SLEW_HIGH      2
#define STM32_SLEW_VERY_HIGH 3

#endif
