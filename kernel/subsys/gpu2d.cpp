/*
 * subsys/gpu2d.cpp - 2D accelerator registry and /dev/gpu2d.
 *
 * One accelerator driver (the PXP) serves every user; requests are serialised. Without a
 * driver the same operations run on the CPU, so the graphics server works either way.
 *
 * User space names surfaces by shared memory handle + offset; the kernel checks that each
 * surface lies inside its object before the accelerator (a bus master) touches it, so a
 * process can only make it read or write memory it was given.
 */
#include "kernel.h"
#include <crtos/gpu2d.h>
#include <crtos/vfs.h>
#include <string.h>

static const struct gpu2d_ops *s_ops;
static void *s_ctx;
static struct mutex s_lock;
static bool s_ready;

static void gpu_init_once(void)
{
    if (s_ready)
        return;
    uint32_t key = irq_lock();
    if (!s_ready) {
        mutex_init(&s_lock);
        s_ready = true;
    }
    irq_unlock(key);
}

/* ---- CPU fallback ------------------------------------------------------------------------------ */

static uint32_t bpp(uint32_t fmt)
{
    return fmt == GPU2D_FMT_RGB565 ? 2u : 4u;
}

static inline uint16_t to565(uint32_t c)
{
    return (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
}

static inline uint32_t from565(uint16_t c)
{
    uint32_t r = (c >> 11) & 0x1Fu, g = (c >> 5) & 0x3Fu, b = c & 0x1Fu;
    return 0xFF000000u | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

static inline uint32_t get_px(const struct gpu2d_surface *s, int x, int y)
{
    const uint8_t *row = (const uint8_t *)s->addr + (uint32_t)y * s->stride;
    if (s->format == GPU2D_FMT_RGB565)
        return from565(((const uint16_t *)row)[x]);
    uint32_t c = ((const uint32_t *)row)[x];
    return s->format == GPU2D_FMT_XRGB8888 ? c | 0xFF000000u : c;
}

static inline void put_px(const struct gpu2d_surface *s, int x, int y, uint32_t c)
{
    uint8_t *row = (uint8_t *)s->addr + (uint32_t)y * s->stride;
    if (s->format == GPU2D_FMT_RGB565)
        ((uint16_t *)row)[x] = to565(c);
    else
        ((uint32_t *)row)[x] = c;
}

static inline uint32_t blend(uint32_t src, uint32_t dst)
{
    uint32_t a = src >> 24, na = 255u - a;
    uint32_t r = (((src >> 16) & 0xFFu) * a + ((dst >> 16) & 0xFFu) * na) / 255u;
    uint32_t g = (((src >> 8) & 0xFFu) * a + ((dst >> 8) & 0xFFu) * na) / 255u;
    uint32_t b = ((src & 0xFFu) * a + (dst & 0xFFu) * na) / 255u;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

static int soft_fill(const struct gpu2d_surface *d, const struct gpu2d_rect *r, uint32_t argb)
{
    int x0 = r->x < 0 ? 0 : r->x, y0 = r->y < 0 ? 0 : r->y;
    int x1 = r->x + r->w > d->width ? d->width : r->x + r->w;
    int y1 = r->y + r->h > d->height ? d->height : r->y + r->h;
    for (int y = y0; y < y1; y++) {
        if (d->format == GPU2D_FMT_RGB565) {
            uint16_t *row = (uint16_t *)((uint8_t *)d->addr + (uint32_t)y * d->stride);
            uint16_t c = to565(argb);
            for (int x = x0; x < x1; x++)
                row[x] = c;
        } else {
            uint32_t *row = (uint32_t *)((uint8_t *)d->addr + (uint32_t)y * d->stride);
            for (int x = x0; x < x1; x++)
                row[x] = argb | (d->format == GPU2D_FMT_XRGB8888 ? 0xFF000000u : 0u);
        }
    }
    return 0;
}

static int soft_blit(const struct gpu2d_surface *d, int16_t dx, int16_t dy, const struct gpu2d_surface *s,
                     const struct gpu2d_rect *sr, uint32_t flags)
{
    int sx = sr->x, sy = sr->y, w = sr->w, h = sr->h, x = dx, y = dy;
    if (sx < 0) { x -= sx; w += sx; sx = 0; }
    if (sy < 0) { y -= sy; h += sy; sy = 0; }
    if (sx + w > s->width) w = s->width - sx;
    if (sy + h > s->height) h = s->height - sy;
    if (x < 0) { sx -= x; w += x; x = 0; }
    if (y < 0) { sy -= y; h += y; y = 0; }
    if (x + w > d->width) w = d->width - x;
    if (y + h > d->height) h = d->height - y;
    bool alpha = (flags & GPU2D_BLEND) && s->format == GPU2D_FMT_ARGB8888;
    for (int j = 0; j < h; j++) {
        if (!alpha && s->format == d->format) {
            memmove((uint8_t *)d->addr + (uint32_t)(y + j) * d->stride + (uint32_t)x * bpp(d->format),
                    (const uint8_t *)s->addr + (uint32_t)(sy + j) * s->stride + (uint32_t)sx * bpp(s->format),
                    (size_t)w * bpp(d->format));
            continue;
        }
        for (int i = 0; i < w; i++) {
            uint32_t c = get_px(s, sx + i, sy + j);
            if (alpha)
                c = blend(c, get_px(d, x + i, y + j));
            put_px(d, x + i, y + j, c);
        }
    }
    return 0;
}

/* @sr of @s stretched over @dr of @d, the nearest pixel (GPU2D_FILTER is a hint the CPU skips) */
static int soft_scale(const struct gpu2d_surface *d, const struct gpu2d_rect *dr, const struct gpu2d_surface *s,
                      const struct gpu2d_rect *sr, uint32_t flags)
{
    int sx = sr->x < 0 ? 0 : sr->x, sy = sr->y < 0 ? 0 : sr->y;
    int sw = sr->x + sr->w > s->width ? s->width - sx : sr->x + sr->w - sx;
    int sh = sr->y + sr->h > s->height ? s->height - sy : sr->y + sr->h - sy;
    if (sw <= 0 || sh <= 0 || !dr->w || !dr->h)
        return 0;
    int x0 = dr->x < 0 ? 0 : dr->x, y0 = dr->y < 0 ? 0 : dr->y;
    int x1 = dr->x + dr->w > d->width ? d->width : dr->x + dr->w;
    int y1 = dr->y + dr->h > d->height ? d->height : dr->y + dr->h;
    bool alpha = (flags & GPU2D_BLEND) && s->format == GPU2D_FMT_ARGB8888;
    for (int y = y0; y < y1; y++) {
        int v = sy + (int)((uint32_t)(y - dr->y) * (uint32_t)sh / dr->h);
        for (int x = x0; x < x1; x++) {
            int u = sx + (int)((uint32_t)(x - dr->x) * (uint32_t)sw / dr->w);
            uint32_t c = get_px(s, u, v);
            if (alpha)
                c = blend(c, get_px(d, x, y));
            put_px(d, x, y, c);
        }
    }
    return 0;
}

/* ---- kernel API --------------------------------------------------------------------------------- */

int gpu2d_fill(const struct gpu2d_surface *dst, const struct gpu2d_rect *r, uint32_t argb)
{
    gpu_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    int ret = s_ops ? s_ops->fill(s_ctx, dst, r, argb) : soft_fill(dst, r, argb);
    mutex_unlock(&s_lock);
    return ret;
}

int gpu2d_blit(const struct gpu2d_surface *dst, int16_t dx, int16_t dy, const struct gpu2d_surface *src,
               const struct gpu2d_rect *sr, uint32_t flags)
{
    gpu_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    int ret = s_ops ? s_ops->blit(s_ctx, dst, dx, dy, src, sr, flags) : soft_blit(dst, dx, dy, src, sr, flags);
    mutex_unlock(&s_lock);
    return ret;
}

/* ---- /dev/gpu2d ----------------------------------------------------------------------------------- */

/* @size bytes at @offset of the calling process's shared memory @handle (a reference held) */
static int shm_range(int32_t handle, uint64_t offset, uint64_t size, uint8_t **p, struct shm **ref)
{
    uint8_t *base;
    uint32_t total;
    struct shm *sh = shm_lookup(handle, &base, &total, nullptr);
    if (!sh)
        return -EBADF;
    if (offset + size > total) {
        shm_put(sh);
        return -EINVAL;
    }
    *p = base + offset;
    *ref = sh;
    return 0;
}

/* Surface @hs of the calling process as an address, checked against its shared memory */
static int resolve(const struct gpu2d_hsurface *hs, struct gpu2d_surface *s, struct shm **ref)
{
    if (hs->format < GPU2D_FMT_RGB565 || hs->format > GPU2D_FMT_ARGB8888 || !hs->width || !hs->height)
        return -EINVAL;
    uint64_t row = (uint64_t)hs->width * bpp(hs->format);
    if (hs->stride < row || ((hs->offset | hs->stride) & 1u))
        return -EINVAL;
    uint8_t *p;
    int r = shm_range(hs->handle, hs->offset, (uint64_t)hs->stride * (hs->height - 1u) + row, &p, ref);
    if (r)
        return r;
    s->addr = (uintptr_t)p;
    s->width = hs->width;
    s->height = hs->height;
    s->stride = hs->stride;
    s->format = hs->format;
    return 0;
}

static int hfill(const struct gpu2d_hfill_req *q)
{
    struct gpu2d_surface d;
    struct shm *ref;
    int r = resolve(&q->dst, &d, &ref);
    if (r)
        return r;
    r = gpu2d_fill(&d, &q->rect, q->argb);
    shm_put(ref);
    return r;
}

static int hblit(const struct gpu2d_hblit_req *q)
{
    struct gpu2d_surface d, s;
    struct shm *dref, *sref;
    int r = resolve(&q->dst, &d, &dref);
    if (r)
        return r;
    r = resolve(&q->src, &s, &sref);
    if (r) {
        shm_put(dref);
        return r;
    }
    r = gpu2d_blit(&d, q->dx, q->dy, &s, &q->src_rect, q->flags);
    shm_put(sref);
    shm_put(dref);
    return r;
}

static int hscale(const struct gpu2d_hscale_req *q)
{
    struct gpu2d_surface d, s;
    struct shm *dref, *sref;
    int r = resolve(&q->dst, &d, &dref);
    if (r)
        return r;
    r = resolve(&q->src, &s, &sref);
    if (r) {
        shm_put(dref);
        return r;
    }
    gpu_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    r = soft_scale(&d, &q->dst_rect, &s, &q->src_rect, q->flags);
    mutex_unlock(&s_lock);
    shm_put(sref);
    shm_put(dref);
    return r;
}

/* A batch for a driver that takes it whole: every surface checked first, then one call */
static struct gpu2d_op s_bops[GPU2D_BATCH_MAX];     /* (under s_lock) */
static struct shm *s_brefs[2 * GPU2D_BATCH_MAX];

static int hbatch_whole(const struct gpu2d_hbatch *b)
{
    mutex_lock(&s_lock, WAIT_FOREVER);
    uint32_t nrefs = 0;
    int r = 0;
    for (uint32_t i = 0; i < b->count && !r; i++) {
        const struct gpu2d_hop *h = &b->ops[i];
        struct gpu2d_op *o = &s_bops[i];
        o->op = h->op;
        if (h->op == GPU2D_OP_FILL) {
            r = resolve(&h->fill.dst, &o->dst, &s_brefs[nrefs]);
            if (!r)
                nrefs++;
            o->rect = h->fill.rect;
            o->argb = h->fill.argb;
        } else if (h->op == GPU2D_OP_SCALE) {
            r = resolve(&h->scale.dst, &o->dst, &s_brefs[nrefs]);
            if (!r) {
                nrefs++;
                r = resolve(&h->scale.src, &o->src, &s_brefs[nrefs]);
                if (!r)
                    nrefs++;
            }
            o->rect = h->scale.src_rect;
            o->drect = h->scale.dst_rect;
            o->flags = h->scale.flags;
        } else if (h->op == GPU2D_OP_BLIT) {
            r = resolve(&h->blit.dst, &o->dst, &s_brefs[nrefs]);
            if (!r) {
                nrefs++;
                r = resolve(&h->blit.src, &o->src, &s_brefs[nrefs]);
                if (!r)
                    nrefs++;
            }
            o->rect = h->blit.src_rect;
            o->dx = h->blit.dx;
            o->dy = h->blit.dy;
            o->flags = h->blit.flags;
        } else {
            r = -EINVAL;
        }
    }
    if (!r)
        r = s_ops && s_ops->batch ? s_ops->batch(s_ctx, s_bops, b->count) : -ENODEV;
    while (nrefs)
        shm_put(s_brefs[--nrefs]);
    mutex_unlock(&s_lock);
    return r;
}

/* Primitives: the request copied (the caller may change it meanwhile), the destination and
 * each texture array with all its layers checked against the caller's shared memory, the
 * primitives against its memory; the driver checks each primitive as it reads it */
static int hprims(const struct gpu2d_prims_req *uq)
{
    static struct gpu2d_prims_req q;    /* (under s_lock) */
    static struct gpu2d_texarray tex[GPU2D_PRIM_TEXARRAYS];
    static struct shm *refs[GPU2D_PRIM_TEXARRAYS + 2];
    gpu_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    if (!s_ops || !s_ops->prims) {
        mutex_unlock(&s_lock);
        return -ENOSYS;
    }
    memcpy(&q, uq, sizeof(q));
    unsigned nrefs = 0;
    int r = q.count > GPU2D_PRIMS_MAX || q.ntex > GPU2D_PRIM_TEXARRAYS || ((uintptr_t)q.prims & 3u) ? -EINVAL : 0;
    if (!r && (!uaccess_ok(q.prims, q.count * sizeof(struct gpu2d_prim), 0) ||
               (q.rows && !uaccess_ok(q.rows, q.count * 4u, 0))))
        r = -EFAULT;
    struct gpu2d_surface dst;
    if (!r && !(r = resolve(&q.dst, &dst, &refs[nrefs])))
        nrefs++;
    for (unsigned i = 0; i < q.ntex && !r; i++) {
        const struct gpu2d_htexarray *h = &q.tex[i];
        if (!h->layers || h->layers > 65536u || !h->s.height || h->s.format < GPU2D_FMT_RGB565 ||
            h->s.format > GPU2D_FMT_ARGB8888) {
            r = -EINVAL;
            break;
        }
        /* all the layers: one surface as tall as all of them */
        uint64_t row = (uint64_t)h->s.width * bpp(h->s.format);
        uint64_t size = (uint64_t)h->s.stride * ((uint64_t)h->s.height * h->layers - 1u) + row;
        uint8_t *p;
        if (!h->s.width || h->s.stride < row || ((h->s.offset | h->s.stride) & 1u)) {
            r = -EINVAL;
            break;
        }
        if (!(r = shm_range(h->s.handle, h->s.offset, size, &p, &refs[nrefs]))) {
            nrefs++;
            tex[i].s = (struct gpu2d_surface){ (uintptr_t)p, h->s.width, h->s.height, h->s.stride, h->s.format };
            tex[i].layers = h->layers;
        }
    }
    if (!r)
        r = s_ops->prims(s_ctx, &dst, &q.clip, q.flags, q.prims, q.rows, q.count, tex, q.ntex);
    while (nrefs)
        shm_put(refs[--nrefs]);
    mutex_unlock(&s_lock);
    return r;
}

static int hbatch(const struct gpu2d_hbatch *b)
{
    if (b->count > GPU2D_BATCH_MAX)
        return -EINVAL;
    if (!uaccess_ok(b->ops, b->count * sizeof(struct gpu2d_hop), 0))
        return -EFAULT;
    gpu_init_once();
    if (s_ops && s_ops->batch)
        return hbatch_whole(b);
    for (uint32_t i = 0; i < b->count; i++) {
        const struct gpu2d_hop *op = &b->ops[i];
        int r = op->op == GPU2D_OP_FILL    ? hfill(&op->fill)
                : op->op == GPU2D_OP_BLIT  ? hblit(&op->blit)
                : op->op == GPU2D_OP_SCALE ? hscale(&op->scale)
                                           : -EINVAL;
        if (r)
            return r;
    }
    return 0;
}

static int gpu_ioctl(struct file *, unsigned cmd, void *arg)
{
    bool kernel = g_current->proc == nullptr;
    switch (cmd) {
    case GPU2D_IOC_FILL: {
        if (!kernel)
            return -EPERM;
        const struct gpu2d_fill_req *q = (const struct gpu2d_fill_req *)arg;
        return gpu2d_fill(&q->dst, &q->rect, q->argb);
    }
    case GPU2D_IOC_BLIT: {
        if (!kernel)
            return -EPERM;
        const struct gpu2d_blit_req *q = (const struct gpu2d_blit_req *)arg;
        return gpu2d_blit(&q->dst, q->dx, q->dy, &q->src, &q->src_rect, q->flags);
    }
    case GPU2D_IOC_HFILL:
        return hfill((const struct gpu2d_hfill_req *)arg);
    case GPU2D_IOC_HBLIT:
        return hblit((const struct gpu2d_hblit_req *)arg);
    case GPU2D_IOC_HBATCH:
        return hbatch((const struct gpu2d_hbatch *)arg);
    case GPU2D_IOC_PRIMS:
        return hprims((const struct gpu2d_prims_req *)arg);
    case GPU2D_IOC_STATS: {
        struct gpu2d_stats *st = (struct gpu2d_stats *)arg;
        memset(st, 0, sizeof(*st));
        gpu_init_once();
        mutex_lock(&s_lock, WAIT_FOREVER);
        if (s_ops && s_ops->stats)
            s_ops->stats(s_ctx, st);
        else
            strcpy(st->name, s_ops ? "accelerator" : "CPU");
        mutex_unlock(&s_lock);
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static const struct file_ops gpu_file_ops = {
    nullptr, nullptr, nullptr, nullptr, gpu_ioctl, nullptr, nullptr, nullptr, nullptr,
};

/* /dev/gpu2d exists from the start (CPU fallback); a driver makes it fast */
void gpu2d_init(void)
{
    gpu_init_once();
    devfs_register("gpu2d", &gpu_file_ops, nullptr);
}

int gpu2d_register(const struct gpu2d_ops *ops, void *ctx)
{
    gpu_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    if (s_ops) {
        mutex_unlock(&s_lock);
        return -EBUSY;
    }
    s_ops = ops;
    s_ctx = ctx;
    mutex_unlock(&s_lock);
    return 0;
}

void gpu2d_unregister(void)
{
    gpu_init_once();
    mutex_lock(&s_lock, WAIT_FOREVER);
    s_ops = nullptr;
    s_ctx = nullptr;
    mutex_unlock(&s_lock);
}
