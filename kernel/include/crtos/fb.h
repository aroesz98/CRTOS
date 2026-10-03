/*
 * crtos/fb.h - frame buffers (displays).
 *
 * A display driver registers its buffers; they appear as /dev/fbN. With two buffers the
 * client draws into the hidden one and makes it visible with fb_show() (applied at the next
 * vertical blank, no tearing). Buffers live in non-cacheable memory.
 *
 * A display whose panel can be one of several (its device tree lists their modes) starts in
 * a default mode; a driver that learns which panel is fitted - its touch controller knows the
 * size it was configured for - tells with fb_suggest_size(), and the display changes mode as
 * long as nobody has opened it yet (at boot, before the graphics server).
 */
#ifndef CRTOS_FB_H
#define CRTOS_FB_H

#include <stdint.h>
#include <crtos/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FB_FMT_RGB565    1u
#define FB_FMT_XRGB8888  2u

#define FB_MAX_BUFFERS 2

struct fb_info {
    uint16_t width, height;
    uint32_t stride;                    /* bytes per line */
    uint32_t format;                    /* FB_FMT_* */
    uint32_t bpp;
    uint32_t nbuffers;
    uint32_t buffer_size;
    uintptr_t buffer[FB_MAX_BUFFERS];
    uint32_t refresh_mhz;               /* refresh rate in mHz */
};

struct fb_ops {
    int (*show)(void *ctx, unsigned index);           /* switch at the next vblank */
    int (*wait_vsync)(void *ctx, uint32_t timeout);   /* 0 or -ETIMEDOUT */
    int (*blank)(void *ctx, int blank);               /* backlight / output off */
    /* optional: change to the panel mode of this size, new buffers described in @info; 0,
     * -ENOENT (no such mode) or -ENOMEM (the old mode stays). Called only while nobody uses
     * the frame buffer. */
    int (*set_size)(void *ctx, unsigned width, unsigned height, struct fb_info *info);
};

struct fb_shm_req {
    uint32_t index;                     /* in: buffer */
    int32_t handle;                     /* out: shared memory handle of it */
};

/* ioctls on /dev/fbN. poll() reports POLLIN when a frame started since the last
 * FB_IOC_GET_FRAME. */
#define FB_IOC_GET_INFO    _IOR('F', 1, struct fb_info)
#define FB_IOC_SHOW        _IO('F', 2)         /* arg: buffer index, shown from the next frame */
#define FB_IOC_WAIT_VSYNC  _IO('F', 3)
#define FB_IOC_BLANK       _IO('F', 4)         /* arg: 0/1 */
#define FB_IOC_GET_FRAME   _IOR('F', 5, uint32_t)          /* frames started so far */
#define FB_IOC_GET_SHOWN   _IOR('F', 6, uint32_t)          /* buffer on the screen */
#define FB_IOC_GET_SHM     _IOWR('F', 7, struct fb_shm_req) /* buffer for the 2D accelerator */

int fb_register(const struct fb_info *info, const struct fb_ops *ops, void *ctx);   /* index or -errno */
void fb_unregister(int index);
int fb_get_info(int index, struct fb_info *info);
int fb_show(int index, unsigned buffer);
int fb_wait_vsync(int index, uint32_t timeout);
int fb_blank(int index, int blank);
/* Driver, from its interrupt: a frame started, showing buffer @shown */
void fb_vsync(int index, unsigned shown);
/* The fitted panel is @width x @height (said by a driver that knows it, e.g. touch): /dev/fb0
 * changes to that mode. 0 (changed, or already that size), -ENOENT (its panel has no such
 * mode), -EBUSY (opened already: the graphics server runs), -ENODEV (no display). */
int fb_suggest_size(unsigned width, unsigned height);

#ifdef __cplusplus
}
#endif

#endif
