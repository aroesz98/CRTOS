/*
 * PinMux.cpp - CRTOS Hardware Abstraction Layer - PinMux Driver Implementation
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

#include "PinMux.hpp"

// Include the device header (which includes PERI_GPIO.h and other peripherals)
// This header file structure was reorganized in SDK 2.x
extern "C" {
#include "MIMXRT1052.h"
}

#include "../../drivers/fsl_iomuxc.h"
#include "../../drivers/fsl_gpio.h"

namespace CRTOS
{
namespace HAL
{

// ============================================================================
// Singleton Instance
// ============================================================================

PinMux& PinMux::GetInstance()
{
    static PinMux instance;
    return instance;
}

// ============================================================================
// Initialization
// ============================================================================

Result PinMux::Initialize()
{
    if (m_initialized)
    {
        return Result::RESULT_SUCCESS;
    }

    // IOMUXC is always accessible, no special init needed
    // Clock gating for IOMUXC is typically always on
    
    m_initialized = true;
    return Result::RESULT_SUCCESS;
}

// ============================================================================
// Pin Mux Configuration
// ============================================================================

void PinMux::SetMux(const PinMuxEntry& entry, bool sion)
{
    IOMUXC_SetPinMux(
        entry.muxRegister,
        entry.muxMode,
        entry.inputRegister,
        entry.inputDaisy,
        entry.configRegister,
        sion ? 1 : 0
    );
}

void PinMux::SetMuxRaw(uint32_t muxRegister, uint32_t muxMode,
                        uint32_t inputRegister, uint32_t inputDaisy,
                        bool sion)
{
    IOMUXC_SetPinMux(
        muxRegister,
        muxMode,
        inputRegister,
        inputDaisy,
        0,  // configRegister not used here
        sion ? 1 : 0
    );
}

uint8_t PinMux::GetMuxMode(uint32_t muxRegister)
{
    volatile uint32_t* reg = (volatile uint32_t*)muxRegister;
    return (*reg & IOMUXC_SW_MUX_CTL_PAD_MUX_MODE_MASK) >> IOMUXC_SW_MUX_CTL_PAD_MUX_MODE_SHIFT;
}

// ============================================================================
// Pad Configuration
// ============================================================================

void PinMux::SetPadConfig(uint32_t configRegister, const PinPadConfig& config)
{
    uint32_t value = BuildPadValue(config);
    SetPadConfigRaw(configRegister, value);
}

void PinMux::SetPadConfigRaw(uint32_t configRegister, uint32_t value)
{
    if (configRegister != 0)
    {
        volatile uint32_t* reg = (volatile uint32_t*)configRegister;
        *reg = value;
    }
}

uint32_t PinMux::GetPadConfig(uint32_t configRegister)
{
    if (configRegister == 0)
    {
        return 0;
    }
    volatile uint32_t* reg = (volatile uint32_t*)configRegister;
    return *reg;
}

uint32_t PinMux::BuildPadValue(const PinPadConfig& config)
{
    // Build the SW_PAD_CTL value based on config
    // Bit fields for i.MX RT1052:
    // [0]     SRE - Slew Rate (0=slow, 1=fast)
    // [5:3]   DSE - Drive Strength
    // [7:6]   SPEED - Speed
    // [11]    ODE - Open Drain
    // [12]    PKE - Pull/Keeper Enable
    // [13]    PUE - Pull/Keeper Select (0=keeper, 1=pull)
    // [15:14] PUS - Pull Select
    // [16]    HYS - Hysteresis Enable
    
    uint32_t value = 0;
    
    // Drive strength (DSE bits [5:3])
    value |= ((uint32_t)config.drive << 3);
    
    // Speed (bits [7:6])
    value |= ((uint32_t)config.speed << 6);
    
    // Open drain (bit 11)
    if (config.openDrain)
    {
        value |= (1 << 11);
    }
    
    // Pull/Keeper enable (bit 12)
    if (config.pullKeeperEnable)
    {
        value |= (1 << 12);
    }
    
    // Pull/Keeper select (bit 13) - 1 = pull, 0 = keeper
    if (config.pullKeeperSelect)
    {
        value |= (1 << 13);
    }
    
    // Pull select (bits [15:14])
    switch (config.pull)
    {
        case PullConfig::PullDown_100K:
            value |= (0 << 14);
            break;
        case PullConfig::PullUp_47K:
            value |= (1 << 14);
            break;
        case PullConfig::PullUp_100K:
            value |= (2 << 14);
            break;
        case PullConfig::PullUp_22K:
            value |= (3 << 14);
            break;
        default:
            break;
    }
    
    // Hysteresis (bit 16)
    if (config.hysteresis)
    {
        value |= (1 << 16);
    }
    
    return value;
}

PinPadConfig PinMux::GetDefaultPadConfig()
{
    PinPadConfig config;
    config.drive = DriveStrength::R0_6_25Ohm;
    config.speed = PinSpeed::Medium_100MHz;
    config.pull = PullConfig::PullUp_100K;
    config.openDrain = false;
    config.hysteresis = false;
    config.pullKeeperEnable = true;
    config.pullKeeperSelect = true;  // Pull mode
    return config;
}

// ============================================================================
// GPIO Configuration
// Uses SDK functions for portability across different i.MX RT variants
// ============================================================================

void* PinMux::GetGpioBase(GpioPort port)
{
    // GPIO base addresses from device header
    static volatile uint32_t* const gpioBaseAddrs[] = {
        (volatile uint32_t*)0x401B8000U,  // GPIO1
        (volatile uint32_t*)0x401BC000U,  // GPIO2
        (volatile uint32_t*)0x401C0000U,  // GPIO3
        (volatile uint32_t*)0x401C4000U,  // GPIO4
        (volatile uint32_t*)0x400C0000U,  // GPIO5
    };
    
    if ((uint8_t)port >= (uint8_t)GpioPort::COUNT)
    {
        return nullptr;
    }
    
    return (void*)gpioBaseAddrs[(uint8_t)port];
}

void PinMux::SetGpioDirection(GpioPort port, uint8_t pin, bool output, bool initialValue)
{
    volatile uint32_t* base = (volatile uint32_t*)GetGpioBase(port);
    if (base == nullptr || pin > 31)
    {
        return;
    }
    
    // GPIO register offsets
    // DR (Data Register)         = offset 0x00
    // GDIR (Direction Register)  = offset 0x04
    // DR_SET                     = offset 0x84
    // DR_CLEAR                   = offset 0x88
    // DR_TOGGLE                  = offset 0x8C
    
    volatile uint32_t* dr = base;           // offset 0x00
    volatile uint32_t* gdir = base + 1;     // offset 0x04
    volatile uint32_t* dr_set = base + 33;  // offset 0x84 / 4
    volatile uint32_t* dr_clear = base + 34; // offset 0x88 / 4
    
    if (output)
    {
        // Set initial value first
        if (initialValue)
        {
            *dr_set = (1UL << pin);
        }
        else
        {
            *dr_clear = (1UL << pin);
        }
        // Then set direction to output
        *gdir |= (1UL << pin);
    }
    else
    {
        // Set direction to input
        *gdir &= ~(1UL << pin);
    }
}

void PinMux::GpioWrite(GpioPort port, uint8_t pin, bool value)
{
    volatile uint32_t* base = (volatile uint32_t*)GetGpioBase(port);
    if (base == nullptr || pin > 31)
    {
        return;
    }
    
    volatile uint32_t* dr_set = base + 33;   // offset 0x84 / 4
    volatile uint32_t* dr_clear = base + 34; // offset 0x88 / 4
    
    if (value)
    {
        *dr_set = (1UL << pin);
    }
    else
    {
        *dr_clear = (1UL << pin);
    }
}

bool PinMux::GpioRead(GpioPort port, uint8_t pin)
{
    volatile uint32_t* base = (volatile uint32_t*)GetGpioBase(port);
    if (base == nullptr || pin > 31)
    {
        return false;
    }
    
    volatile uint32_t* psr = base + 2;  // PSR (Pad Status Register) offset 0x08 / 4
    
    return (*psr & (1UL << pin)) != 0;
}

void PinMux::GpioToggle(GpioPort port, uint8_t pin)
{
    volatile uint32_t* base = (volatile uint32_t*)GetGpioBase(port);
    if (base == nullptr || pin > 31)
    {
        return;
    }
    
    volatile uint32_t* dr_toggle = base + 35;  // offset 0x8C / 4
    
    *dr_toggle = (1UL << pin);
}

} // namespace HAL
} // namespace CRTOS
