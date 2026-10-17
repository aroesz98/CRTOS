/*
 * crtos/gpu2d.h - 2D acceleration (fills, copies, alpha blending) for the compositor.
 * One accelerator (PXP, NeoChrom) serves all users; requests are serialised. A batch
 * (GPU2D_IOC_HBATCH) goes to a driver with a batch operation as a whole - one submission for
 * a frame - and to the others operation by operation. A driver that can also draw textured
 * triangles and quadrilaterals in perspective for 3D programs (GPU2D_IOC_PRIMS).
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

#define GPU2D_BLEND         0x1u    /* blit, scale: blend using the source's alpha */
#define GPU2D_FILTER        0x2u    /* scale: bilinear filtering (else the nearest pixel) */

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

/* one operation of a batch, as a driver gets it (surfaces checked, addresses resolved) */
struct gpu2d_op {
    uint32_t op;                /* GPU2D_OP_FILL, GPU2D_OP_BLIT or GPU2D_OP_SCALE */
    struct gpu2d_surface dst;
    struct gpu2d_rect rect;     /* fill: the rectangle; blit, scale: the source rectangle */
    int16_t dx, dy;             /* blit: where the source rectangle goes */
    struct gpu2d_surface src;   /* blit, scale */
    uint32_t argb;              /* fill */
    uint32_t flags;             /* blit, scale: GPU2D_BLEND, GPU2D_FILTER */
    struct gpu2d_rect drect;    /* scale: the destination rectangle the source fills */
};

/* what the accelerator did since it started (GPU2D_IOC_STATS) */
struct gpu2d_stats {
    char name[16];              /* "NeoChrom", "PXP"; "CPU" without a driver */
    uint32_t batches;           /* batches taken whole (a driver with a batch operation) */
    uint32_t ops;               /* operations done */
    uint32_t lists;             /* jobs given to the hardware (a command list, a PXP run) */
    uint32_t busy_us;           /* time from giving them to their end */
    uint32_t cache_hits;        /* the accelerator's read cache, if it has one */
    uint32_t cache_misses;
    uint32_t caps;              /* GPU2D_CAP_*: what the hardware does itself */
};
#define GPU2D_CAP_SCALE 0x1u    /* stretching (else the kernel does it with the processor: the nearest pixel) */
#define GPU2D_CAP_PRIMS 0x2u    /* primitives (GPU2D_IOC_PRIMS) */

/*
 * Primitives for 3D: triangles and convex quadrilaterals drawn in their order over a
 * destination, each filled with a texture through a projective matrix - at pixel (x, y) the
 * texel (u, v) = ((m0 x + m1 y + m2) / q, (m3 x + m4 y + m5) / q), q = m6 x + m7 y + m8 - so a
 * face of a 3D scene is textured in perspective by itself; a pixel is filled when its centre is
 * inside. There is no depth buffer: a program draws its faces the farthest first. A texture is
 * a layer of a texture array: pictures of one size one after another in shared memory (the
 * GPU reads them); the primitives are an array of the caller's (the kernel reads them).
 */
#define GPU2D_PRIM_QUAD    0x01u    /* four points (else a triangle of three) */
#define GPU2D_PRIM_SMOOTH  0x02u    /* bilinear filtering (else the nearest texel) */
#define GPU2D_PRIM_REPEAT  0x04u    /* the texture repeats (else its edge texels go on) */
#define GPU2D_PRIM_AGAIN   0x08u    /* the previous primitive's points again, its matrix scaled:
                                     * u' = again[0] u + again[1], v' = again[2] v + again[3] - a
                                     * second texture over the same face (the rest is not read) */

#define GPU2D_PRIM_COPY    0u       /* the destination becomes the texel */
#define GPU2D_PRIM_ALPHA   1u       /* d = s a + d (1 - a) (an ARGB8888 texture) */
#define GPU2D_PRIM_PREMUL  2u       /* d = s + d (1 - a): colours multiplied by their alpha */

/* 64 bytes: an array of them aligned to 32 bytes has a primitive's head (to the points) in
 * one cache line - all that is read of an AGAIN one */
struct gpu2d_prim {
    uint8_t flags;                  /* GPU2D_PRIM_QUAD, _SMOOTH, _REPEAT, _AGAIN */
    uint8_t blend;                  /* GPU2D_PRIM_COPY, _ALPHA, _PREMUL */
    uint8_t tex;                    /* the texture array */
    uint8_t reserved;
    uint16_t layer;                 /* the picture in it */
    uint16_t reserved2;
    union {
        struct {
            int16_t x[4], y[4];     /* the points, pixels x 16 (12.4 fixed point) */
        };
        float again[4];             /* GPU2D_PRIM_AGAIN */
    };
    float m[9];                     /* the texture matrix */
    uint32_t reserved3[1];
};

/* a texture array as a driver gets it: the first picture, how many there are */
struct gpu2d_texarray {
    struct gpu2d_surface s;
    uint32_t layers;
};
#define GPU2D_PRIM_TEXARRAYS 8

struct gpu2d_ops {
    int (*fill)(void *ctx, const struct gpu2d_surface *dst, const struct gpu2d_rect *r, uint32_t argb);
    int (*blit)(void *ctx, const struct gpu2d_surface *dst, int16_t dx, int16_t dy,
                const struct gpu2d_surface *src, const struct gpu2d_rect *sr, uint32_t flags);
    /* optional: @n operations in their order, as one job (NULL: fill / blit each) */
    int (*batch)(void *ctx, const struct gpu2d_op *ops, unsigned n);
    /* optional: its statistics (the name at least) */
    void (*stats)(void *ctx, struct gpu2d_stats *st);
    /* optional: @n primitives over @dst inside @clip (the arrays and every layer checked,
     * the primitives and @rows not: they lie in the caller's memory and may change meanwhile) */
    int (*prims)(void *ctx, const struct gpu2d_surface *dst, const struct gpu2d_rect *clip, uint32_t flags,
                 const struct gpu2d_prim *p, const int16_t *rows, unsigned n, const struct gpu2d_texarray *tex,
                 unsigned ntex);
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
 * checks the whole surface lies inside); raw addresses are for kernel callers only. So any
 * program may use /dev/gpu2d: it can only make the accelerator touch memory it was given. */
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

/* src_rect of src stretched over dst_rect of dst (GPU2D_FILTER: smoothly) */
struct gpu2d_hscale_req {
    struct gpu2d_hsurface dst;
    struct gpu2d_rect dst_rect;
    struct gpu2d_hsurface src;
    struct gpu2d_rect src_rect;
    uint32_t flags;
};

#define GPU2D_OP_FILL 1u
#define GPU2D_OP_BLIT 2u
#define GPU2D_OP_SCALE 3u

struct gpu2d_hop {
    uint32_t op;                        /* GPU2D_OP_* */
    union {
        struct gpu2d_hfill_req fill;
        struct gpu2d_hblit_req blit;
        struct gpu2d_hscale_req scale;
    };
};

struct gpu2d_hbatch {
    const struct gpu2d_hop *ops;
    uint32_t count;                     /* at most GPU2D_BATCH_MAX */
};
#define GPU2D_BATCH_MAX 64

/* a texture array in shared memory: hs is its first picture, the others follow it (each
 * height x stride bytes) */
struct gpu2d_htexarray {
    struct gpu2d_hsurface s;
    uint32_t layers;
};

/* @count primitives at @prims (the caller's own memory) drawn over @dst, inside @clip */
#define GPU2D_PRIMS_DISCARD 0x1u    /* what @dst holds inside @clip is not needed: the primitives cover it */
struct gpu2d_prims_req {
    struct gpu2d_hsurface dst;
    struct gpu2d_rect clip;
    uint32_t flags;                     /* GPU2D_PRIMS_* */
    const struct gpu2d_prim *prims;
    const int16_t *rows;                /* or NULL: the rows each one reaches, first and last + 1
                                         * (an AGAIN one: those of the one before) - the driver need
                                         * not read the primitives themselves to see which part of
                                         * the destination each is in */
    uint32_t count;                     /* at most GPU2D_PRIMS_MAX */
    uint32_t ntex;
    struct gpu2d_htexarray tex[GPU2D_PRIM_TEXARRAYS];
};
#define GPU2D_PRIMS_MAX 65535u

/* ioctls on /dev/gpu2d */
#define GPU2D_IOC_FILL   _IOW('G', 1, struct gpu2d_fill_req)     /* kernel callers */
#define GPU2D_IOC_BLIT   _IOW('G', 2, struct gpu2d_blit_req)     /* kernel callers */
#define GPU2D_IOC_HFILL  _IOW('G', 3, struct gpu2d_hfill_req)
#define GPU2D_IOC_HBLIT  _IOW('G', 4, struct gpu2d_hblit_req)
#define GPU2D_IOC_HBATCH _IOW('G', 5, struct gpu2d_hbatch)       /* one call for a whole frame */
#define GPU2D_IOC_STATS  _IOR('G', 6, struct gpu2d_stats)
#define GPU2D_IOC_PRIMS  _IOW('G', 7, struct gpu2d_prims_req)   /* -ENOSYS without GPU2D_CAP_PRIMS */

int gpu2d_register(const struct gpu2d_ops *ops, void *ctx);
void gpu2d_unregister(void);
int gpu2d_fill(const struct gpu2d_surface *dst, const struct gpu2d_rect *r, uint32_t argb);
int gpu2d_blit(const struct gpu2d_surface *dst, int16_t dx, int16_t dy, const struct gpu2d_surface *src,
               const struct gpu2d_rect *sr, uint32_t flags);

#ifdef __cplusplus
}
#endif

#endif
