/*
 * HAL_PinMux.hpp - Hardware Abstraction Layer for PinMux (C Interface)
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 * Description:
 * C-linkage interface for PinMux HAL, used for symbol export to loadable modules.
 * These functions wrap the IOMUXC SDK functions with a simpler interface.
 */

#ifndef HAL_PINMUX_HPP
#define HAL_PINMUX_HPP

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPIO port enumeration
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
 * @brief Pin drive strength
 */
typedef enum {
    HAL_DRIVE_DISABLED = 0,
    HAL_DRIVE_R0_150OHM = 1,
    HAL_DRIVE_R0_2_75OHM = 2,
    HAL_DRIVE_R0_3_50OHM = 3,
    HAL_DRIVE_R0_4_37OHM = 4,
    HAL_DRIVE_R0_5_30OHM = 5,
    HAL_DRIVE_R0_6_25OHM = 6,
    HAL_DRIVE_R0_7_22OHM = 7,
} hal_drive_strength_t;

/**
 * @brief Pin speed/slew rate
 */
typedef enum {
    HAL_SPEED_LOW_50MHZ = 0,
    HAL_SPEED_MEDIUM_100MHZ = 1,
    HAL_SPEED_FAST_150MHZ = 2,
    HAL_SPEED_MAX_200MHZ = 3,
} hal_pin_speed_t;

/**
 * @brief Pull configuration
 */
typedef enum {
    HAL_PULL_DISABLED = 0,
    HAL_PULL_DOWN_100K = 1,
    HAL_PULL_UP_47K = 2,
    HAL_PULL_UP_100K = 3,
    HAL_PULL_UP_22K = 4,
} hal_pull_config_t;

/**
 * @brief Pin pad configuration
 */
typedef struct {
    hal_drive_strength_t drive;  ///< Drive strength
    hal_pin_speed_t speed;       ///< Speed/slew rate
    hal_pull_config_t pull;      ///< Pull configuration
    bool openDrain;              ///< Open drain mode
    bool hysteresis;             ///< Hysteresis enable
    bool pullKeeperEnable;       ///< Pull/keeper enable
    bool pullKeeperSelect;       ///< 0 = keeper, 1 = pull
} hal_pin_pad_config_t;

/**
 * @brief Pin mux entry (compact form)
 * Matches the 5-tuple format from fsl_iomuxc.h
 */
typedef struct {
    uint32_t muxRegister;    ///< SW_MUX_CTL register address
    uint32_t muxMode;        ///< Mux mode (ALT0-7)
    uint32_t inputRegister;  ///< Input select register (0 if none)
    uint32_t inputDaisy;     ///< Input daisy value
    uint32_t configRegister; ///< SW_PAD_CTL register address
} hal_pin_mux_entry_t;

// ============================================================================
// Initialization API
// ============================================================================

/**
 * @brief Initialize PinMux HAL
 * @return true on success
 */
bool hal_pinmux_init(void);

/**
 * @brief Check if HAL is initialized
 */
bool hal_pinmux_is_initialized(void);

// ============================================================================
// Low-level Pin Mux API
// ============================================================================

/**
 * @brief Set pin mux mode using entry structure
 * @param entry Pin mux entry with all register addresses
 * @param sion Software Input On (for loopback)
 */
void hal_pinmux_set_mux(const hal_pin_mux_entry_t* entry, bool sion);

/**
 * @brief Set pin mux using raw register addresses
 * @param muxRegister SW_MUX_CTL register address
 * @param muxMode ALT mode (0-7)
 * @param inputRegister Input select register (0 if none)
 * @param inputDaisy Input daisy chain value
 * @param sion Software Input On
 */
void hal_pinmux_set_mux_raw(uint32_t muxRegister, uint32_t muxMode,
                             uint32_t inputRegister, uint32_t inputDaisy,
                             bool sion);

/**
 * @brief Set pin pad configuration
 * @param configRegister SW_PAD_CTL register address
 * @param config Pad configuration
 */
void hal_pinmux_set_pad_config(uint32_t configRegister, const hal_pin_pad_config_t* config);

/**
 * @brief Set pin pad configuration using raw value
 * @param configRegister SW_PAD_CTL register address
 * @param configValue Raw configuration value
 */
void hal_pinmux_set_pad_raw(uint32_t configRegister, uint32_t configValue);

/**
 * @brief Get current mux mode for a pin
 * @param muxRegister SW_MUX_CTL register address
 * @return Current ALT mode (0-7)
 */
uint8_t hal_pinmux_get_mux_mode(uint32_t muxRegister);

/**
 * @brief Get current pad configuration
 * @param configRegister SW_PAD_CTL register address
 * @return Raw pad configuration value
 */
uint32_t hal_pinmux_get_pad_config(uint32_t configRegister);

// ============================================================================
// GPIO Configuration API
// ============================================================================

/**
 * @brief Configure pin as GPIO
 * @param port GPIO port
 * @param pin GPIO pin number (0-31)
 * @param output True for output, false for input
 * @param initialValue Initial output value
 */
void hal_pinmux_set_gpio_direction(hal_gpio_port_t port, uint8_t pin, bool output, bool initialValue);

/**
 * @brief Set GPIO output value
 * @param port GPIO port
 * @param pin GPIO pin number
 * @param value Output value
 */
void hal_pinmux_gpio_write(hal_gpio_port_t port, uint8_t pin, bool value);

/**
 * @brief Read GPIO input value
 * @param port GPIO port
 * @param pin GPIO pin number
 * @return Input value
 */
bool hal_pinmux_gpio_read(hal_gpio_port_t port, uint8_t pin);

/**
 * @brief Toggle GPIO output value
 * @param port GPIO port
 * @param pin GPIO pin number
 */
void hal_pinmux_gpio_toggle(hal_gpio_port_t port, uint8_t pin);

// ============================================================================
// Helper functions
// ============================================================================

/**
 * @brief Build pad config value from structure
 * @param config Pad configuration
 * @return Raw register value
 */
uint32_t hal_pinmux_build_pad_value(const hal_pin_pad_config_t* config);

/**
 * @brief Get default pad configuration
 * @param config Config structure to fill
 */
void hal_pinmux_get_default_pad_config(hal_pin_pad_config_t* config);

#ifdef __cplusplus
}
#endif

#endif // HAL_PINMUX_HPP
