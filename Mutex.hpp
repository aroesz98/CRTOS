/*
 * Mutex.hpp - CRTOS Mutex Class
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

#ifndef MUTEX_HPP
#define MUTEX_HPP

#include <cstdint>
#include <atomic>

namespace CRTOS
{
    class Mutex
    {
        public:
            Mutex(void);
            ~Mutex(void);
            void Lock(void);
            void Unlock(void);

        private:
            std::atomic_flag flag;
            uint32_t irqMask;
    };
}

#endif /* MUTEX_HPP */
