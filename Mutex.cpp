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
/*-----------------------------------------------------------------------*/
/* C Interface for FatFS ffsystem.c                                      */
/*-----------------------------------------------------------------------*/

// Simple mutex pool for FatFS (max FF_VOLUMES + 1)
#define FATFS_MAX_MUTEXES 4
static CRTOS::Mutex g_fatfsMutexPool[FATFS_MAX_MUTEXES];
static bool g_fatfsMutexUsed[FATFS_MAX_MUTEXES] = {false};

extern "C" {

void* CRTOS_Mutex_Create(void)
{
    for (int i = 0; i < FATFS_MAX_MUTEXES; i++)
    {
        if (!g_fatfsMutexUsed[i])
        {
            g_fatfsMutexUsed[i] = true;
            return &g_fatfsMutexPool[i];
        }
    }
    return nullptr;
}

void CRTOS_Mutex_Delete(void* mutex)
{
    for (int i = 0; i < FATFS_MAX_MUTEXES; i++)
    {
        if (&g_fatfsMutexPool[i] == mutex)
        {
            g_fatfsMutexUsed[i] = false;
            return;
        }
    }
}

int CRTOS_Mutex_Lock(void* mutex, unsigned int timeout_ms)
{
    (void)timeout_ms;  /* Simple spinlock doesn't support timeout */
    if (mutex)
    {
        static_cast<CRTOS::Mutex*>(mutex)->Lock();
        return 1;  /* Success */
    }
    return 0;  /* Failure */
}

void CRTOS_Mutex_Unlock(void* mutex)
{
    if (mutex)
    {
        static_cast<CRTOS::Mutex*>(mutex)->Unlock();
    }
}

} /* extern "C" */