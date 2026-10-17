/*
 * kernel/rtos/cxx.cpp - C++ runtime support for kernel code.
 */
#include <crtos/mm.h>
#include <crtos/printk.h>
#include <stddef.h>

void *operator new(size_t n)
{
    void *p = kmalloc(n, KM_ANY);
    if (!p)
        panic("out of memory (operator new, %u bytes)", (unsigned)n);
    return p;
}

void *operator new[](size_t n)
{
    return operator new(n);
}

void operator delete(void *p) noexcept
{
    kfree(p);
}

void operator delete[](void *p) noexcept
{
    kfree(p);
}

void operator delete(void *p, size_t) noexcept
{
    kfree(p);
}

void operator delete[](void *p, size_t) noexcept
{
    kfree(p);
}

extern "C" void __cxa_pure_virtual(void)
{
    panic("pure virtual function called");
}

/* Static objects are never destroyed */
extern "C" int __aeabi_atexit(void *, void (*)(void *), void *)
{
    return 0;
}

namespace __gnu_cxx {
void __verbose_terminate_handler()
{
    panic("C++ terminate");
}
} // namespace __gnu_cxx
