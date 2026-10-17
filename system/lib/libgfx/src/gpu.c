/*
 * gpu.c - the 2D accelerator (/dev/gpu2d) for a program's own pictures: buffers in shared
 * memory (the accelerator reads surfaces only by a shared memory handle of the caller) and
 * stretched or plain copies into a window, fills; textured primitives for 3D where the
 * accelerator can (GPU2D_CAP_PRIMS). Each call waits for the accelerator, so the program may
 * draw on the result at once. Without an accelerator the kernel does the same on the
 * processor (not the primitives); only a missing /dev/gpu2d makes the calls fail (-1), and
 * then the caller draws itself.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <crtos.h>
#include "gfx.h"

static int s_gpu = -2; /* -2: not opened yet, -1: there is none */

static int gpu(void)
{
    if (s_gpu == -2)
        s_gpu = open("/dev/gpu2d", O_RDWR);
    return s_gpu;
}

struct gfx_buffer *gfx_buffer_create(int width, int height, uint32_t format)
{
    if (width <= 0 || height <= 0 || width > 4096 || height > 65535) {
        errno = EINVAL;
        return NULL;
    }
    struct gfx_buffer *b = (struct gfx_buffer *)calloc(1, sizeof(*b));
    if (!b)
        return NULL;
    int bpp = format == GPU2D_FMT_RGB565 ? 2 : 4;
    b->s.w = width;
    b->s.h = height;
    b->s.stride = (width * bpp + 3) & ~3;
    b->s.format = format;
    b->shm = crtos_shm_create((size_t)b->s.stride * (size_t)height, 0);
    b->s.pix = b->shm >= 0 ? crtos_shm_map(b->shm) : NULL;
    if (!b->s.pix) {
        if (b->shm >= 0)
            close(b->shm);
        free(b);
        return NULL;
    }
    return b;
}

void gfx_buffer_free(struct gfx_buffer *b)
{
    if (!b)
        return;
    crtos_shm_unmap(b->s.pix);
    close(b->shm);
    free(b);
}

unsigned gfx_gpu_caps(void)
{
    struct gpu2d_stats st;
    return gpu() >= 0 && ioctl(gpu(), GPU2D_IOC_STATS, &st) == 0 ? st.caps : 0u;
}

struct gfx_shmem *gfx_shmem_create(size_t size)
{
    struct gfx_shmem *m = (struct gfx_shmem *)calloc(1, sizeof(*m));
    if (!m)
        return NULL;
    m->size = size;
    m->shm = crtos_shm_create(size, 0);
    m->p = m->shm >= 0 ? crtos_shm_map(m->shm) : NULL;
    if (!m->p) {
        if (m->shm >= 0)
            close(m->shm);
        free(m);
        return NULL;
    }
    return m;
}

void gfx_shmem_free(struct gfx_shmem *m)
{
    if (!m)
        return;
    crtos_shm_unmap(m->p);
    close(m->shm);
    free(m);
}

static struct gpu2d_hsurface win_surface(const struct gfx_win *w)
{
    struct gpu2d_hsurface s = { w->shm, w->offset, (uint16_t)w->s.w, (uint16_t)w->s.h, (uint32_t)w->s.stride,
                                w->s.format };
    return s;
}

static struct gpu2d_hsurface buf_surface(const struct gfx_buffer *b)
{
    struct gpu2d_hsurface s = { b->shm, 0, (uint16_t)b->s.w, (uint16_t)b->s.h, (uint32_t)b->s.stride, b->s.format };
    return s;
}

static int run(struct gpu2d_hop *op)
{
    int fd = gpu();
    if (fd < 0)
        return -1;
    struct gpu2d_hbatch b = { op, 1 };
    return ioctl(fd, GPU2D_IOC_HBATCH, &b) < 0 ? -1 : 0;
}

int gfx_gpu_scale(struct gfx_win *dst, int dx, int dy, int dw, int dh, const struct gfx_buffer *src, int sx, int sy,
                  int sw, int sh, unsigned flags)
{
    struct gpu2d_hop op;
    memset(&op, 0, sizeof(op));
    op.op = GPU2D_OP_SCALE;
    op.scale.dst = win_surface(dst);
    op.scale.dst_rect = (struct gpu2d_rect){ (int16_t)dx, (int16_t)dy, (uint16_t)dw, (uint16_t)dh };
    op.scale.src = buf_surface(src);
    op.scale.src_rect = (struct gpu2d_rect){ (int16_t)sx, (int16_t)sy, (uint16_t)sw, (uint16_t)sh };
    op.scale.flags = ((flags & GFX_GPU_SMOOTH) ? GPU2D_FILTER : 0u) | ((flags & GFX_GPU_BLEND) ? GPU2D_BLEND : 0u);
    return run(&op);
}

int gfx_gpu_copy(struct gfx_win *dst, int dx, int dy, const struct gfx_buffer *src, int sx, int sy, int w, int h,
                 unsigned flags)
{
    struct gpu2d_hop op;
    memset(&op, 0, sizeof(op));
    op.op = GPU2D_OP_BLIT;
    op.blit.dst = win_surface(dst);
    op.blit.dx = (int16_t)dx;
    op.blit.dy = (int16_t)dy;
    op.blit.src = buf_surface(src);
    op.blit.src_rect = (struct gpu2d_rect){ (int16_t)sx, (int16_t)sy, (uint16_t)w, (uint16_t)h };
    op.blit.flags = (flags & GFX_GPU_BLEND) ? GPU2D_BLEND : 0u;
    return run(&op);
}

int gfx_gpu_prims(struct gfx_buffer *dst, int cx, int cy, int cw, int ch, unsigned flags, const struct gpu2d_prim *prims,
                  const int16_t *rows, unsigned count, const struct gfx_texarray *tex, unsigned ntex)
{
    int fd = gpu();
    if (fd < 0 || ntex > GPU2D_PRIM_TEXARRAYS) {
        errno = fd < 0 ? ENODEV : EINVAL;
        return -1;
    }
    struct gpu2d_prims_req q;
    memset(&q, 0, sizeof(q));
    q.dst = buf_surface(dst);
    q.clip = (struct gpu2d_rect){ (int16_t)cx, (int16_t)cy, (uint16_t)cw, (uint16_t)ch };
    q.flags = flags;
    q.prims = prims;
    q.rows = rows;
    q.count = count;
    q.ntex = ntex;
    for (unsigned i = 0; i < ntex; i++) {
        const struct gfx_texarray *t = &tex[i];
        q.tex[i].s = (struct gpu2d_hsurface){ t->shm, (uint32_t)t->offset, (uint16_t)t->width, (uint16_t)t->height,
                                              (uint32_t)t->stride, t->format };
        q.tex[i].layers = (uint32_t)t->layers;
    }
    return ioctl(fd, GPU2D_IOC_PRIMS, &q) < 0 ? -1 : 0;
}

int gfx_gpu_fill(struct gfx_win *dst, int x, int y, int w, int h, uint32_t argb)
{
    struct gpu2d_hop op;
    memset(&op, 0, sizeof(op));
    op.op = GPU2D_OP_FILL;
    op.fill.dst = win_surface(dst);
    op.fill.rect = (struct gpu2d_rect){ (int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h };
    op.fill.argb = argb;
    return run(&op);
}
