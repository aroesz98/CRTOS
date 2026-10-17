/*
 * cxxrt.cpp - the little C++ runtime programs need (built without exceptions and RTTI):
 * operator new/delete on malloc, pure virtual calls, and the handle static destructors are
 * registered with (__cxa_atexit comes from newlib; exit() runs them).
 */
#include <stdio.h>
#include <stdlib.h>
#include <new>

extern "C" {
void *__dso_handle = &__dso_handle;

void __cxa_pure_virtual(void)
{
    fputs("pure virtual function called\n", stderr);
    abort();
}
}

/* no exceptions: allocation failure ends the program, as std::terminate would */
static void *alloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        fputs("out of memory (operator new)\n", stderr);
        abort();
    }
    return p;
}

void *operator new(size_t n)
{
    return alloc(n);
}

void *operator new[](size_t n)
{
    return alloc(n);
}

void *operator new(size_t n, const std::nothrow_t &) noexcept
{
    return malloc(n ? n : 1);
}

void *operator new[](size_t n, const std::nothrow_t &) noexcept
{
    return malloc(n ? n : 1);
}

void operator delete(void *p) noexcept
{
    free(p);
}

void operator delete[](void *p) noexcept
{
    free(p);
}

void operator delete(void *p, size_t) noexcept
{
    free(p);
}

void operator delete[](void *p, size_t) noexcept
{
    free(p);
}
