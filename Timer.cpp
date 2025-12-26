/*
 * Timer.cpp - CRTOS Software Timer Implementation
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

#include "Timer.hpp"
#include "CRTOS.hpp"
#include "CRTOS_Internal.hpp"

// External timer list from CRTOS.cpp
extern Node<CRTOS::Timer::SoftwareTimer> *sTimerList;

CRTOS::Result CRTOS::Timer::Init(SoftwareTimer *timer, uint32_t timeoutTicks, void (*callback)(void *), void *callbackArgs, bool autoReload)
{
    if (timer == nullptr || callback == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }

    timer->timeoutTicks = timeoutTicks;
    timer->elapsedTicks = 0u;
    timer->isActive = false;
    timer->callback = callback;
    timer->callbackArgs = callbackArgs;
    timer->autoReload = autoReload;

    ListInsertAtBeginning(sTimerList, timer);

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Timer::Start(SoftwareTimer *timer)
{
    if (timer == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }
    if (timer->isActive == true)
    {
        return CRTOS::Result::RESULT_TIMER_ALREADY_ACTIVE;
    }

    timer->elapsedTicks = 0u;
    timer->isActive = true;

    return CRTOS::Result::RESULT_SUCCESS;
}

CRTOS::Result CRTOS::Timer::Stop(SoftwareTimer *timer)
{
    if (timer == nullptr)
    {
        return CRTOS::Result::RESULT_BAD_PARAMETER;
    }
    if (timer->isActive == true)
    {
        return CRTOS::Result::RESULT_TIMER_ALREADY_STOPPED;
    }

    timer->isActive = false;
    timer->elapsedTicks = 0u;

    return CRTOS::Result::RESULT_SUCCESS;
}
