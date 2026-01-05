/*
 * PinMux.hpp - CRTOS Hardware Abstraction Layer - PinMux Driver
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
 * Hardware abstraction layer for IOMUXC pin multiplexing.
 * Provides runtime pin configuration similar to Linux Device Tree.
 */

#ifndef CRTOS_HAL_PINMUX_HPP
#define CRTOS_HAL_PINMUX_HPP

#include <stdint.h>
#include <stddef.h>
#include "../CRTOS.hpp"

namespace CRTOS
{
namespace HAL
{
    /**
     * @brief GPIO port enumeration
     */
    enum class GpioPort
    {
        GPIO1 = 0,
        GPIO2 = 1,
        GPIO3 = 2,
        GPIO4 = 3,
        GPIO5 = 4,
        COUNT
    };

    /**
     * @brief Pin drive strength
     */
    enum class DriveStrength : uint8_t
    {
        Disabled   = 0,
        R0_150Ohm  = 1,
        R0_2_75Ohm = 2,
        R0_3_50Ohm = 3,
        R0_4_37Ohm = 4,
        R0_5_30Ohm = 5,
        R0_6_25Ohm = 6,
        R0_7_22Ohm = 7,
    };

    /**
     * @brief Pin speed (slew rate)
     */
    enum class PinSpeed : uint8_t
    {
        Low_50MHz    = 0,
        Medium_100MHz= 1,
        Fast_150MHz  = 2,
        Max_200MHz   = 3,
    };

    /**
     * @brief Pull configuration
     */
    enum class PullConfig : uint8_t
    {
        Disabled    = 0,
        PullDown_100K = 1,
        PullUp_47K  = 2,
        PullUp_100K = 3,
        PullUp_22K  = 4,
    };

    /**
     * @brief Pin pad configuration structure
     */
    struct PinPadConfig
    {
        DriveStrength drive;
        PinSpeed speed;
        PullConfig pull;
        bool openDrain;
        bool hysteresis;
        bool pullKeeperEnable;
        bool pullKeeperSelect;
    };

    /**
     * @brief Pin mux entry (compact form matching fsl_iomuxc.h)
     */
    struct PinMuxEntry
    {
        uint32_t muxRegister;
        uint32_t muxMode;
        uint32_t inputRegister;
        uint32_t inputDaisy;
        uint32_t configRegister;
    };

    /**
     * @brief PinMux HAL class (singleton)
     */
    class PinMux
    {
    public:
        /**
         * @brief Get singleton instance
         */
        static PinMux& GetInstance();

        /**
         * @brief Initialize the PinMux HAL
         * @return Result code
         */
        Result Initialize();

        /**
         * @brief Check if initialized
         */
        bool IsInitialized() const { return m_initialized; }

        // ====================================================================
        // Pin Mux Configuration
        // ====================================================================

        /**
         * @brief Set pin mux using entry structure
         * @param entry Pin mux entry
         * @param sion Software Input On (loopback)
         */
        void SetMux(const PinMuxEntry& entry, bool sion = false);

        /**
         * @brief Set pin mux using raw values
         */
        void SetMuxRaw(uint32_t muxRegister, uint32_t muxMode,
                       uint32_t inputRegister, uint32_t inputDaisy,
                       bool sion = false);

        /**
         * @brief Get current mux mode
         */
        uint8_t GetMuxMode(uint32_t muxRegister);

        // ====================================================================
        // Pad Configuration
        // ====================================================================

        /**
         * @brief Set pad electrical configuration
         * @param configRegister SW_PAD_CTL register address
         * @param config Configuration structure
         */
        void SetPadConfig(uint32_t configRegister, const PinPadConfig& config);

        /**
         * @brief Set pad config with raw value
         */
        void SetPadConfigRaw(uint32_t configRegister, uint32_t value);

        /**
         * @brief Get current pad config value
         */
        uint32_t GetPadConfig(uint32_t configRegister);

        /**
         * @brief Build raw pad value from config structure
         */
        static uint32_t BuildPadValue(const PinPadConfig& config);

        /**
         * @brief Get default pad configuration
         */
        static PinPadConfig GetDefaultPadConfig();

        // ====================================================================
        // GPIO Configuration
        // ====================================================================

        /**
         * @brief Set pin direction when in GPIO mode
         */
        void SetGpioDirection(GpioPort port, uint8_t pin, bool output, bool initialValue = false);

        /**
         * @brief Write GPIO output
         */
        void GpioWrite(GpioPort port, uint8_t pin, bool value);

        /**
         * @brief Read GPIO input
         */
        bool GpioRead(GpioPort port, uint8_t pin);

        /**
         * @brief Toggle GPIO output
         */
        void GpioToggle(GpioPort port, uint8_t pin);

    private:
        PinMux() = default;
        ~PinMux() = default;
        PinMux(const PinMux&) = delete;
        PinMux& operator=(const PinMux&) = delete;

        bool m_initialized = false;

        /**
         * @brief Get GPIO base address
         */
        static void* GetGpioBase(GpioPort port);
    };

    /**
     * @brief Global accessor
     */
    inline PinMux& GetPinMux() { return PinMux::GetInstance(); }

} // namespace HAL
} // namespace CRTOS

#endif // CRTOS_HAL_PINMUX_HPP
