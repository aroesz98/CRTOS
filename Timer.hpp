/*
 * Timer.hpp - CRTOS Software Timer
 * Author: Arkadiusz Szlanta
 * Date: 25 Dec 2025
 *
 * License:
 * This source code is provided for hobbyist and private use only.
 * Any commercial or industrial use, including distribution, reproduction, or
 * incorporation in commercial or industrial products or services is prohibited.
 * Use at your own risk. The author(s) hold no responsibility for any damages
 * or losses resulting from the use of this software.
 *
 */

#ifndef TIMER_HPP
#define TIMER_HPP

#include <cstdint>

namespace CRTOS
{
    enum class Result : uint8_t;

    namespace Timer
    {
        typedef struct
        {
            uint32_t timeoutTicks;
            uint32_t elapsedTicks;
            bool isActive;
            void (*callback)(void*);
            void *callbackArgs;
            bool autoReload;
        } SoftwareTimer;

        Result Init(SoftwareTimer *timer, uint32_t timeoutTicks, void (*callback)(void*), void *callbackArgs, bool autoReload);
        Result Start(SoftwareTimer *timer);
        Result Stop(SoftwareTimer *timer);
    }
}

#endif /* TIMER_HPP */
