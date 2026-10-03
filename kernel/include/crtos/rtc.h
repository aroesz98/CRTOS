/*
 * crtos/rtc.h - real-time clocks.
 *
 * The wall clock (SYS_TIME_GET) is the time since boot plus an offset. A driver of a clock
 * that keeps running through resets registers here: the kernel takes the date from it
 * when it registers, and SYS_TIME_SET (settimeofday) writes the new date to it, so the
 * date survives reboots (and power loss, with a battery).
 */
#ifndef CRTOS_RTC_H
#define CRTOS_RTC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rtc_ops {
    int (*read)(void *ctx, int64_t *us);    /* microseconds since 1970; <0: not set */
    int (*set)(void *ctx, int64_t us);
};

int rtc_register(const struct rtc_ops *ops, void *ctx, const char *name);  /* -EBUSY: one clock */
void rtc_unregister(void *ctx);

/* The wall clock in microseconds since 1970 (UTC) - small numbers while nobody has set it */
int64_t wall_time_us(void);
/* The wall clock as a FAT timestamp (date << 16 | time, 2 s steps), the format of the mtime of
 * struct vfs_stat; 2021-01-01 00:00 while the date is unknown */
uint32_t fat_time_now(void);

#ifdef __cplusplus
}
#endif

#endif
