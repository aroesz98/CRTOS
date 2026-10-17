/*
 * pxp-imxrt.ko - i.MX RT PXP as the 2D accelerator (gpu2d): rectangle fills, copies with
 * format conversion, and alpha blending of an ARGB8888 surface over another surface.
 *
 * Fill: the process surface is moved out of the output area, so the PXP writes its
 * background colour. Copy: the source is the process surface. Blend: the destination is
 * read as the process surface and the source is the alpha surface (in-place).
 *
 * A destination in cached memory is cleaned and invalidated before the operation (the
 * processor's writes reach memory first) and invalidated again after it: lines a program read
 * meanwhile (the remote desktop's screen copy, read while the next frame is copied into it)
 * would otherwise keep the old pixels.
 */
#include <crtos/clk.h>
#include <crtos/device.h>
#include <crtos/errno.h>
#include <crtos/gpu2d.h>
#include <crtos/irq.h>
#include <crtos/module.h>
#include <crtos/sync.h>
#include "fsl_cache.h"
#include "fsl_pxp.h"

struct pxp
{
    struct device *dev;
    PXP_Type *base;
    int irq;
    struct semaphore done;
    uint32_t jobs;
};

static void pxp_irq(int irq, void *ctx)
{
    (void)irq;
    struct pxp *p = ctx;
    if (PXP_GetStatusFlags(p->base) & kPXP_CompleteFlag)
    {
        PXP_ClearStatusFlags(p->base, kPXP_CompleteFlag);
        sem_give(&p->done);
    }
}

static uint32_t bpp(uint32_t fmt)
{
    return fmt == GPU2D_FMT_RGB565 ? 2u : 4u;
}

static int out_format(uint32_t fmt, pxp_output_pixel_format_t *f)
{
    switch (fmt)
    {
        case GPU2D_FMT_RGB565:
            *f = kPXP_OutputPixelFormatRGB565;
            return 0;
        case GPU2D_FMT_XRGB8888:
            *f = kPXP_OutputPixelFormatRGB888;
            return 0;
        case GPU2D_FMT_ARGB8888:
            *f = kPXP_OutputPixelFormatARGB8888;
            return 0;
        default:
            return -EINVAL;
    }
}

static int ps_format(uint32_t fmt, pxp_ps_pixel_format_t *f)
{
    switch (fmt)
    {
        case GPU2D_FMT_RGB565:
            *f = kPXP_PsPixelFormatRGB565;
            return 0;
        case GPU2D_FMT_XRGB8888:
        case GPU2D_FMT_ARGB8888:
            *f = (pxp_ps_pixel_format_t)0x4;
            return 0; /* 32-bit unpacked RGB */
        default:
            return -EINVAL;
    }
}

static int as_format(uint32_t fmt, pxp_as_pixel_format_t *f)
{
    switch (fmt)
    {
        case GPU2D_FMT_RGB565:
            *f = kPXP_AsPixelFormatRGB565;
            return 0;
        case GPU2D_FMT_XRGB8888:
            *f = kPXP_AsPixelFormatRGB888;
            return 0;
        case GPU2D_FMT_ARGB8888:
            *f = kPXP_AsPixelFormatARGB8888;
            return 0;
        default:
            return -EINVAL;
    }
}

static int cached(uintptr_t a)
{
    return (a >= 0x20200000u && a < 0x20240000u) || (a >= 0x80000000u && a < 0x81E00000u);
}

/* Clip a w*h rectangle at (x, y) to the surface; 0 if nothing is left */
static int clip(const struct gpu2d_surface *s, int *x, int *y, int *w, int *h)
{
    if (*x < 0)
    {
        *w += *x;
        *x = 0;
    }
    if (*y < 0)
    {
        *h += *y;
        *y = 0;
    }
    if (*x + *w > s->width)
        *w = s->width - *x;
    if (*y + *h > s->height)
        *h = s->height - *y;
    return *w > 0 && *h > 0;
}

static uintptr_t at(const struct gpu2d_surface *s, int x, int y)
{
    return s->addr + (uint32_t)y * s->stride + (uint32_t)x * bpp(s->format);
}

static void set_output(struct pxp *p, const struct gpu2d_surface *d, int x, int y, int w, int h, pxp_output_pixel_format_t f)
{
    pxp_output_buffer_config_t o = {0};
    o.pixelFormat = f;
    o.interlacedMode = kPXP_OutputProgressive;
    o.buffer0Addr = (uint32_t)at(d, x, y);
    o.pitchBytes = (uint16_t)d->stride;
    o.width = (uint16_t)w;
    o.height = (uint16_t)h;
    PXP_SetOutputBufferConfig(p->base, &o);
}

static void invalidate_out(const struct gpu2d_surface *d, int y, int h)
{
    uintptr_t a = at(d, 0, y);
    if (cached(a))
        DCACHE_CleanInvalidateByRange((uint32_t)a, (uint32_t)h * d->stride);
}

static int run(struct pxp *p)
{
    PXP_Start(p->base);
    if (sem_take(&p->done, 200))
    {
        PXP_Reset(p->base);
        return -ETIMEDOUT;
    }
    p->jobs++;
    return 0;
}

static int pxp_fill(void *ctx, const struct gpu2d_surface *dst, const struct gpu2d_rect *r, uint32_t argb)
{
    struct pxp *p = ctx;
    int x = r->x, y = r->y, w = r->w, h = r->h;
    pxp_output_pixel_format_t of;
    if (out_format(dst->format, &of))
        return -EINVAL;
    if (!clip(dst, &x, &y, &w, &h))
        return 0;
    invalidate_out(dst, y, h);
    set_output(p, dst, x, y, w, h, of);
    PXP_SetProcessSurfaceBackGroundColor(p->base, argb & 0xFFFFFFu);
    PXP_SetProcessSurfacePosition(p->base, 0xFFFFu, 0xFFFFu, 0, 0); /* no PS: background only */
    PXP_SetAlphaSurfacePosition(p->base, 0xFFFFu, 0xFFFFu, 0, 0);   /* no AS */
    int ret = run(p);
    invalidate_out(dst, y, h);
    return ret;
}

static int pxp_blit(void *ctx, const struct gpu2d_surface *dst, int16_t dx, int16_t dy, const struct gpu2d_surface *src,
                    const struct gpu2d_rect *sr, uint32_t flags)
{
    struct pxp *p = ctx;
    int sx = sr->x, sy = sr->y, w = sr->w, h = sr->h, x = dx, y = dy;
    /* clip to the source, then to the destination, moving both origins together */
    if (sx < 0)
    {
        x -= sx;
        w += sx;
        sx = 0;
    }
    if (sy < 0)
    {
        y -= sy;
        h += sy;
        sy = 0;
    }
    if (sx + w > src->width)
        w = src->width - sx;
    if (sy + h > src->height)
        h = src->height - sy;
    if (x < 0)
    {
        sx -= x;
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        sy -= y;
        h += y;
        y = 0;
    }
    if (x + w > dst->width)
        w = dst->width - x;
    if (y + h > dst->height)
        h = dst->height - y;
    if (w <= 0 || h <= 0)
        return 0;
    pxp_output_pixel_format_t of;
    if (out_format(dst->format, &of))
        return -EINVAL;
    uintptr_t sa = at(src, sx, sy);
    if (cached(sa))
        DCACHE_CleanByRange((uint32_t)sa, (uint32_t)h * src->stride);
    invalidate_out(dst, y, h);
    set_output(p, dst, x, y, w, h, of);

    pxp_ps_buffer_config_t ps = {0};
    if (flags & GPU2D_BLEND)
    {
        /* PS = destination (in place), AS = source with its own alpha */
        if (ps_format(dst->format, &ps.pixelFormat))
            return -EINVAL;
        ps.bufferAddr = (uint32_t)at(dst, x, y);
        ps.pitchBytes = (uint16_t)dst->stride;
        pxp_as_buffer_config_t as = {0};
        if (as_format(src->format, &as.pixelFormat))
            return -EINVAL;
        as.bufferAddr = (uint32_t)sa;
        as.pitchBytes = (uint16_t)src->stride;
        pxp_as_blend_config_t blend = {0, false, kPXP_AlphaEmbedded, kPXP_RopMergeAs};
        PXP_SetAlphaSurfaceBufferConfig(p->base, &as);
        PXP_SetAlphaSurfaceBlendConfig(p->base, &blend);
        PXP_SetAlphaSurfacePosition(p->base, 0, 0, (uint16_t)(w - 1), (uint16_t)(h - 1));
    }
    else
    {
        if (ps_format(src->format, &ps.pixelFormat))
            return -EINVAL;
        ps.bufferAddr = (uint32_t)sa;
        ps.pitchBytes = (uint16_t)src->stride;
        PXP_SetAlphaSurfacePosition(p->base, 0xFFFFu, 0xFFFFu, 0, 0);
    }
    PXP_SetProcessSurfaceBufferConfig(p->base, &ps);
    PXP_SetProcessSurfaceScaler(p->base, (uint16_t)w, (uint16_t)h, (uint16_t)w, (uint16_t)h);
    PXP_SetProcessSurfacePosition(p->base, 0, 0, (uint16_t)(w - 1), (uint16_t)(h - 1));
    int ret = run(p);
    invalidate_out(dst, y, h);
    return ret;
}

static const struct gpu2d_ops pxp_ops = {pxp_fill, pxp_blit};

static int pxp_probe(struct device *dev)
{
    struct pxp *p = devm_kzalloc(dev, sizeof(*p), 0);
    if (!p)
        return -ENOMEM;
    p->dev = dev;
    p->base = device_map(dev, 0);
    struct clk *clk;
    int r = devm_clk_get_enabled(dev, NULL, &clk);
    if (r)
        return r;
    p->irq = device_get_irq(dev, 0);
    if (p->irq < 0)
        return p->irq;
    sem_init(&p->done, 0, 1);
    PXP_Init(p->base);
    PXP_SetProcessSurfaceBackGroundColor(p->base, 0);
    PXP_SetRotateConfig(p->base, kPXP_RotateOutputBuffer, kPXP_Rotate0, kPXP_FlipDisable);
    PXP_EnableCsc1(p->base, false);
    r = irq_request(p->irq, pxp_irq, p, 7, dev->name);
    if (r)
        return r;
    PXP_EnableInterrupts(p->base, kPXP_CompleteInterruptEnable);
    r = gpu2d_register(&pxp_ops, p);
    if (r)
    {
        irq_free(p->irq);
        return r;
    }
    dev_set_drvdata(dev, p);
    dev_info(dev, "PXP 2D accelerator for /dev/gpu2d\n");
    return 0;
}

static void pxp_remove(struct device *dev)
{
    struct pxp *p = dev_get_drvdata(dev);
    gpu2d_unregister();
    PXP_DisableInterrupts(p->base, kPXP_CompleteInterruptEnable);
    irq_free(p->irq);
    PXP_Deinit(p->base);
}

static const struct of_device_id pxp_ids[] = {
    {"fsl,imxrt1050-pxp", NULL},
    {NULL, NULL},
};

static struct driver pxp_driver = {
    .name = "pxp-imxrt",
    .of_match_table = pxp_ids,
    .probe = pxp_probe,
    .remove = pxp_remove,
};

static int init(void)
{
    return driver_register(&pxp_driver);
}

static void fini(void)
{
    driver_unregister(&pxp_driver);
}

MODULE("pxp-imxrt", "i.MX RT PXP 2D accelerator", init, fini);
