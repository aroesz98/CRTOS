/*
 * Mutex.cpp - CRTOS Mutex Implementation
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

#include "Mutex.hpp"

// External functions from CRTOS.cpp
extern "C" uint32_t getInterruptMask(void);
extern "C" void setInterruptMask(uint32_t mask);

CRTOS::Mutex::Mutex(void) : flag(ATOMIC_FLAG_INIT)
{
}

CRTOS::Mutex::~Mutex(void)
{
}

void CRTOS::Mutex::Lock(void)
{
    irqMask = getInterruptMask();

    while (flag.test_and_set(std::memory_order_acquire))
        ;
}

void CRTOS::Mutex::Unlock(void)
{
    flag.clear(std::memory_order_release);

    setInterruptMask(irqMask);
}
