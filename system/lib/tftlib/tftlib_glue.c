/*
 * tftlib_glue.c - what TFTLIB expected from the old firmware, on CRTOS: delays, the tick
 * and its memory helpers.
 *
 * TFTLIB draws into a 32-bit XRGB8888 frame buffer whose line is as wide as the image; a
 * window created with GFX_WIN_XRGB is exactly that (see tftdemo).
 */
#include <stdint.h>
#include <string.h>
#include <crtos.h>

void delay(uint32_t ms)
{
    crtos_sleep_ms(ms);
}

uint32_t GetTick(void)
{
    return (uint32_t)(crtos_time_us() / 1000u);
}

void *memset_optimized(void *dest, int value, size_t count)
{
    return memset(dest, value, count);
}

void *memcpy_optimized(void *dest, const void *src, size_t count)
{
    return memcpy(dest, src, count);
}
