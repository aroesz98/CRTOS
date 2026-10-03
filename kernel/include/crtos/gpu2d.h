/*
 * crtos/gpu2d.h - 2D acceleration (fills, copies, alpha blending) for the compositor.
 * One accelerator (PXP) serves all users; requests are serialised.
 */
#ifndef CRTOS_GPU2D_H
#define CRTOS_GPU2D_H

#include <stdint.h>
#include <crtos/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GPU2D_FMT_RGB565    1u
#define GPU2D_FMT_XRGB8888  2u
#define GPU2D_FMT_ARGB8888  3u

#define GPU2D_BLEND         0x1u    /* blit: blend using the source's alpha */

struct gpu2d_surface {
    uintptr_t addr;
    uint16_t width, height;
    uint32_t stride;        /* bytes per line */
    uint32_t format;        /* GPU2D_FMT_* */
};

struct gpu2d_rect {
    int16_t x, y;
    uint16_t w, h;
};

struct gpu2d_ops {
    int (*fill)(void *ctx, const struct gpu2d_surface *dst, const struct gpu2d_rect *r, uint32_t argb);
    int (*blit)(void *ctx, const struct gpu2d_surface *dst, int16_t dx, int16_t dy,
                const struct gpu2d_surface *src, const struct gpu2d_rect *sr, uint32_t flags);
};

struct gpu2d_fill_req {
    struct gpu2d_surface dst;
    struct gpu2d_rect rect;
    uint32_t argb;
};

struct gpu2d_blit_req {
    struct gpu2d_surface dst;
    int16_t dx, dy;
    struct gpu2d_surface src;
    struct gpu2d_rect src_rect;
    uint32_t flags;
};

/* User space names a surface by a shared memory handle and an offset in it (the kernel
 * checks the whole surface lies inside); raw addresses are for kernel callers only. */
struct gpu2d_hsurface {
    int32_t handle;
    uint32_t offset;
    uint16_t width, height;
    uint32_t stride;
    uint32_t format;
};

struct gpu2d_hfill_req {
    struct gpu2d_hsurface dst;
    struct gpu2d_rect rect;
    uint32_t argb;
};

struct gpu2d_hblit_req {
    struct gpu2d_hsurface dst;
    int16_t dx, dy;
    struct gpu2d_hsurface src;
    struct gpu2d_rect src_rect;
    uint32_t flags;
};

#define GPU2D_OP_FILL 1u
#define GPU2D_OP_BLIT 2u

struct gpu2d_hop {
    uint32_t op;                        /* GPU2D_OP_* */
    union {
        struct gpu2d_hfill_req fill;
        struct gpu2d_hblit_req blit;
    };
};

struct gpu2d_hbatch {
    const struct gpu2d_hop *ops;
    uint32_t count;                     /* at most GPU2D_BATCH_MAX */
};
#define GPU2D_BATCH_MAX 64

/* ioctls on /dev/gpu2d */
#define GPU2D_IOC_FILL   _IOW('G', 1, struct gpu2d_fill_req)     /* kernel callers */
#define GPU2D_IOC_BLIT   _IOW('G', 2, struct gpu2d_blit_req)     /* kernel callers */
#define GPU2D_IOC_HFILL  _IOW('G', 3, struct gpu2d_hfill_req)
#define GPU2D_IOC_HBLIT  _IOW('G', 4, struct gpu2d_hblit_req)
#define GPU2D_IOC_HBATCH _IOW('G', 5, struct gpu2d_hbatch)       /* one call for a whole frame */

int gpu2d_register(const struct gpu2d_ops *ops, void *ctx);
void gpu2d_unregister(void);
int gpu2d_fill(const struct gpu2d_surface *dst, const struct gpu2d_rect *r, uint32_t argb);
int gpu2d_blit(const struct gpu2d_surface *dst, int16_t dx, int16_t dy, const struct gpu2d_surface *src,
               const struct gpu2d_rect *sr, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif
