/*
 * hal_pinmux.h - PinMux HAL Interface for Driver Module
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * Description:
 * Local HAL interface header for PinMux driver module.
 * This allows the driver to be compiled separately while
 * still linking to the kernel's HAL implementation.
 * 
 * Provides Linux Device Tree-like pin configuration through JSON files.
 */

#ifndef DRIVER_HAL_PINMUX_H
#define DRIVER_HAL_PINMUX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO port enumeration (i.MX RT1052 has GPIO1-5)
 */
typedef enum {
    HAL_GPIO_PORT_1 = 0,
    HAL_GPIO_PORT_2 = 1,
    HAL_GPIO_PORT_3 = 2,
    HAL_GPIO_PORT_4 = 3,
    HAL_GPIO_PORT_5 = 4,
    HAL_GPIO_PORT_COUNT
} hal_gpio_port_t;

/**
 * @brief Drive strength enumeration
 * Controls the output driver strength (DSE bits)
 */
typedef enum {
    HAL_DRIVE_DISABLED        = 0,  ///< Output driver disabled
    HAL_DRIVE_R0_150OHM       = 1,  ///< R0 (150 Ohm @ 3.3V)
    HAL_DRIVE_R0_2_75OHM      = 2,  ///< R0/2 (75 Ohm @ 3.3V)
    HAL_DRIVE_R0_3_50OHM      = 3,  ///< R0/3 (50 Ohm @ 3.3V)
    HAL_DRIVE_R0_4_37OHM      = 4,  ///< R0/4 (37 Ohm @ 3.3V)
    HAL_DRIVE_R0_5_30OHM      = 5,  ///< R0/5 (30 Ohm @ 3.3V)
    HAL_DRIVE_R0_6_25OHM      = 6,  ///< R0/6 (25 Ohm @ 3.3V)
    HAL_DRIVE_R0_7_22OHM      = 7,  ///< R0/7 (22 Ohm @ 3.3V)
} hal_drive_strength_t;

/**
 * @brief Pin speed enumeration (SPEED bits)
 */
typedef enum {
    HAL_SPEED_50MHZ   = 0,  ///< 50 MHz
    HAL_SPEED_100MHZ  = 1,  ///< 100 MHz
    HAL_SPEED_150MHZ  = 2,  ///< 150 MHz
    HAL_SPEED_200MHZ  = 3,  ///< 200 MHz
} hal_pin_speed_t;

/**
 * @brief Pull resistor configuration (PUS bits)
 */
typedef enum {
    HAL_PULL_DOWN_100K  = 0,  ///< 100K Ohm Pull Down
    HAL_PULL_UP_47K     = 1,  ///< 47K Ohm Pull Up
    HAL_PULL_UP_100K    = 2,  ///< 100K Ohm Pull Up
    HAL_PULL_UP_22K     = 3,  ///< 22K Ohm Pull Up
} hal_pull_config_t;

/**
 * @brief Pin pad configuration structure
 */
typedef struct {
    hal_drive_strength_t drive;     ///< Output driver strength
    hal_pin_speed_t speed;          ///< Pin speed
    hal_pull_config_t pull;         ///< Pull resistor config
    bool openDrain;                 ///< Open drain enable
    bool hysteresis;                ///< Schmitt trigger enable
    bool pullKeeperEnable;          ///< Pull/Keeper circuit enable
    bool pullKeeperSelect;          ///< true=Pull, false=Keeper
} hal_pin_pad_config_t;

/**
 * @brief Pin mux entry structure (corresponds to SDK 5-tuple)
 */
typedef struct {
    uint32_t muxRegister;    ///< SW_MUX_CTL register address
    uint32_t muxMode;        ///< ALT function (0-7)
    uint32_t inputRegister;  ///< Select input register address (0 if unused)
    uint32_t inputDaisy;     ///< Input daisy chain value
    uint32_t configRegister; ///< SW_PAD_CTL register address
} hal_pin_mux_entry_t;

/* ============================================================================
 * Initialization Functions
 * ============================================================================ */

/**
 * @brief Initialize the PinMux HAL
 * @return true on success
 */
bool hal_pinmux_init(void);

/**
 * @brief Check if PinMux is initialized
 * @return true if initialized
 */
bool hal_pinmux_is_initialized(void);

/* ============================================================================
 * Pin Mux Configuration Functions
 * ============================================================================ */

/**
 * @brief Set pin mux configuration
 * @param entry Pin mux entry (5-tuple)
 * @param sion Software input-on field (enables loopback)
 */
void hal_pinmux_set_mux(const hal_pin_mux_entry_t* entry, bool sion);

/**
 * @brief Set pin mux using raw register addresses
 * @param muxRegister SW_MUX_CTL register address
 * @param muxMode ALT function (0-7)
 * @param inputRegister Select input register (0 if unused)
 * @param inputDaisy Daisy chain value
 * @param sion Software input-on
 */
void hal_pinmux_set_mux_raw(uint32_t muxRegister, uint32_t muxMode,
                             uint32_t inputRegister, uint32_t inputDaisy,
                             bool sion);

/**
 * @brief Get current mux mode for a pin
 * @param muxRegister SW_MUX_CTL register address
 * @return Current ALT function (0-7)
 */
uint8_t hal_pinmux_get_mux_mode(uint32_t muxRegister);

/* ============================================================================
 * Pad Configuration Functions
 * ============================================================================ */

/**
 * @brief Set pad configuration
 * @param configRegister SW_PAD_CTL register address
 * @param config Pad configuration structure
 */
void hal_pinmux_set_pad_config(uint32_t configRegister, const hal_pin_pad_config_t* config);

/**
 * @brief Set pad configuration with raw value
 * @param configRegister SW_PAD_CTL register address
 * @param value Raw register value
 */
void hal_pinmux_set_pad_raw(uint32_t configRegister, uint32_t value);

/**
 * @brief Get pad configuration
 * @param configRegister SW_PAD_CTL register address
 * @return Raw register value
 */
uint32_t hal_pinmux_get_pad_config(uint32_t configRegister);

/**
 * @brief Build pad value from configuration
 * @param config Pad configuration
 * @return Raw register value
 */
uint32_t hal_pinmux_build_pad_value(const hal_pin_pad_config_t* config);

/**
 * @brief Get default pad configuration
 * @param config Output: default configuration
 */
void hal_pinmux_get_default_pad_config(hal_pin_pad_config_t* config);

/* ============================================================================
 * GPIO Functions
 * ============================================================================ */

/**
 * @brief Configure GPIO pin direction
 * @param port GPIO port (1-5)
 * @param pin Pin number (0-31)
 * @param output true for output, false for input
 * @param initialValue Initial output value (if output)
 */
void hal_pinmux_set_gpio_direction(hal_gpio_port_t port, uint8_t pin, bool output, bool initialValue);

/**
 * @brief Write GPIO output
 * @param port GPIO port (1-5)
 * @param pin Pin number (0-31)
 * @param value Output value
 */
void hal_pinmux_gpio_write(hal_gpio_port_t port, uint8_t pin, bool value);

/**
 * @brief Read GPIO input
 * @param port GPIO port (1-5)
 * @param pin Pin number (0-31)
 * @return Pin value
 */
bool hal_pinmux_gpio_read(hal_gpio_port_t port, uint8_t pin);

/**
 * @brief Toggle GPIO output
 * @param port GPIO port (1-5)
 * @param pin Pin number (0-31)
 */
void hal_pinmux_gpio_toggle(hal_gpio_port_t port, uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_HAL_PINMUX_H */
