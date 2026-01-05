/*
 * HAL_PinMux.cpp - CRTOS Hardware Abstraction Layer - PinMux C Wrapper
 * Author: Arkadiusz Szlanta
 * Date: 04 Jan 2026
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 */

#include "HAL_PinMux.hpp"
#include "PinMux.hpp"

using namespace CRTOS;
using namespace CRTOS::HAL;

// ============================================================================
// Initialization
// ============================================================================

bool hal_pinmux_init(void)
{
    return PinMux::GetInstance().Initialize() == Result::RESULT_SUCCESS;
}

bool hal_pinmux_is_initialized(void)
{
    return PinMux::GetInstance().IsInitialized();
}

// ============================================================================
// Pin Mux Configuration
// ============================================================================

void hal_pinmux_set_mux(const hal_pin_mux_entry_t* entry, bool sion)
{
    if (entry == nullptr)
    {
        return;
    }
    
    PinMuxEntry cppEntry;
    cppEntry.muxRegister = entry->muxRegister;
    cppEntry.muxMode = entry->muxMode;
    cppEntry.inputRegister = entry->inputRegister;
    cppEntry.inputDaisy = entry->inputDaisy;
    cppEntry.configRegister = entry->configRegister;
    
    PinMux::GetInstance().SetMux(cppEntry, sion);
}

void hal_pinmux_set_mux_raw(uint32_t muxRegister, uint32_t muxMode,
                             uint32_t inputRegister, uint32_t inputDaisy,
                             bool sion)
{
    PinMux::GetInstance().SetMuxRaw(muxRegister, muxMode, inputRegister, inputDaisy, sion);
}

uint8_t hal_pinmux_get_mux_mode(uint32_t muxRegister)
{
    return PinMux::GetInstance().GetMuxMode(muxRegister);
}

// ============================================================================
// Pad Configuration
// ============================================================================

void hal_pinmux_set_pad_config(uint32_t configRegister, const hal_pin_pad_config_t* config)
{
    if (config == nullptr)
    {
        return;
    }
    
    PinPadConfig cppConfig;
    cppConfig.drive = (DriveStrength)config->drive;
    cppConfig.speed = (PinSpeed)config->speed;
    cppConfig.pull = (PullConfig)config->pull;
    cppConfig.openDrain = config->openDrain;
    cppConfig.hysteresis = config->hysteresis;
    cppConfig.pullKeeperEnable = config->pullKeeperEnable;
    cppConfig.pullKeeperSelect = config->pullKeeperSelect;
    
    PinMux::GetInstance().SetPadConfig(configRegister, cppConfig);
}

void hal_pinmux_set_pad_raw(uint32_t configRegister, uint32_t value)
{
    PinMux::GetInstance().SetPadConfigRaw(configRegister, value);
}

uint32_t hal_pinmux_get_pad_config(uint32_t configRegister)
{
    return PinMux::GetInstance().GetPadConfig(configRegister);
}

uint32_t hal_pinmux_build_pad_value(const hal_pin_pad_config_t* config)
{
    if (config == nullptr)
    {
        return 0;
    }
    
    PinPadConfig cppConfig;
    cppConfig.drive = (DriveStrength)config->drive;
    cppConfig.speed = (PinSpeed)config->speed;
    cppConfig.pull = (PullConfig)config->pull;
    cppConfig.openDrain = config->openDrain;
    cppConfig.hysteresis = config->hysteresis;
    cppConfig.pullKeeperEnable = config->pullKeeperEnable;
    cppConfig.pullKeeperSelect = config->pullKeeperSelect;
    
    return PinMux::GetInstance().BuildPadValue(cppConfig);
}

void hal_pinmux_get_default_pad_config(hal_pin_pad_config_t* config)
{
    if (config == nullptr)
    {
        return;
    }
    
    PinPadConfig cppConfig = PinMux::GetInstance().GetDefaultPadConfig();
    
    config->drive = (hal_drive_strength_t)cppConfig.drive;
    config->speed = (hal_pin_speed_t)cppConfig.speed;
    config->pull = (hal_pull_config_t)cppConfig.pull;
    config->openDrain = cppConfig.openDrain;
    config->hysteresis = cppConfig.hysteresis;
    config->pullKeeperEnable = cppConfig.pullKeeperEnable;
    config->pullKeeperSelect = cppConfig.pullKeeperSelect;
}

// ============================================================================
// GPIO Configuration
// ============================================================================

void hal_pinmux_set_gpio_direction(hal_gpio_port_t port, uint8_t pin, bool output, bool initialValue)
{
    PinMux::GetInstance().SetGpioDirection((GpioPort)port, pin, output, initialValue);
}

void hal_pinmux_gpio_write(hal_gpio_port_t port, uint8_t pin, bool value)
{
    PinMux::GetInstance().GpioWrite((GpioPort)port, pin, value);
}

bool hal_pinmux_gpio_read(hal_gpio_port_t port, uint8_t pin)
{
    return PinMux::GetInstance().GpioRead((GpioPort)port, pin);
}

void hal_pinmux_gpio_toggle(hal_gpio_port_t port, uint8_t pin)
{
    PinMux::GetInstance().GpioToggle((GpioPort)port, pin);
}
