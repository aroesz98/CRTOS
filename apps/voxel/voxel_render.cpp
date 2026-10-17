/*
 * voxel_render.cpp - the software renderer (see voxel_render.h).
 *
 * Camera space: x right, y up, z forward. A corner of the block grid is transformed as the
 * sum of three per-frame table entries (one per axis), so neighbouring faces get exactly the
 * same corner coordinates and never show cracks between them.
 *
 * Pixels are RGB565 and the depth is 16 bits (1/z scaled): the frame is limited by the
 * bandwidth of the SDRAM, so every byte less per pixel counts. Light and fog are applied
 * with the colour spread out in one 32-bit word (0000 0GGG GGG0 0000 RRRR R000 00BB BBBB,
 * fields far enough apart to be multiplied by 0..32 at once).
 */
#include <algorithm>
#include <errno.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"
#include "voxel_render.h"

extern "C" uint64_t crtos_time_us(void);

struct V3
{
    float x, y, z;
};

/* a vertex in camera space */
struct CV
{
    float x, y, z;
    float u, v;   /* texels */
    float l, f;   /* light 0..255, fog 0..255 */
};

/* a vertex on the screen */
struct SV
{
    float x, y;
    float iz, uz, vz; /* 1/z, u/z, v/z */
    float u, v;
    float l, f;
};

enum Mode
{
    M_SOLID,
    M_CUTOUT,
    M_WATER
};

#define AXN (WORLD_X + 2 * OCEAN_RING + 1)
#define AZN (WORLD_Z + 2 * OCEAN_RING + 1)
#define DEPTH_K (60000.0f * Z_NEAR) /* 1/z -> depth buffer units */
#define SPREAD 0x07E0F81Fu

static V3 s_ax[AXN], s_ay[WORLD_Y + 1], s_az[AZN];
static V3 s_right, s_up, s_fwd;
static float s_ex, s_ey, s_ez;
static float s_fx, s_fy, s_cx, s_cy, s_kx, s_ky;
static int s_w, s_h;
static uint16_t *s_color;
static uint16_t *s_depth;
static const uint16_t *s_tex;
static Mode s_mode;
static uint32_t s_fog;     /* spread */
static float s_fog_start, s_fog_scale;
static uint16_t s_ocean[256];
static int s_tris, s_quads, s_rows, s_pixels;

#define WATER_ALPHA 21u /* of 32 */
#define TEX_EDGE 0.02f  /* texture coordinates stay this far inside the texture */

static inline uint32_t spread(uint32_t c)
{
    return (c | (c << 16)) & SPREAD;
}

static inline uint16_t pack(uint32_t c)
{
    return (uint16_t)(c | (c >> 16));
}

static inline uint16_t rgb565(uint32_t r, uint32_t g, uint32_t b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* light l and fog f: 0..32 */
static inline uint16_t shade(uint32_t t, uint32_t l, uint32_t f)
{
    uint32_t c = ((spread(t) * l) >> 5) & SPREAD;
    if (f)
        c = ((c * (32u - f) + s_fog * f) >> 5) & SPREAD;
    return pack(c);
}

static inline uint16_t blend(uint32_t d, uint32_t s, uint32_t a)
{
    return pack(((spread(d) * (32u - a) + spread(s) * a) >> 5) & SPREAD);
}

bool render_init(Renderer *r, int w, int h, uint16_t *color)
{
    if (color)
    {
        if (r->own_color)
            free(r->color);
        r->color = color;
        r->own_color = false;
    }
    else
    {
        uint16_t *c = (uint16_t *)realloc(r->own_color ? r->color : nullptr, sizeof(uint16_t) * (size_t)w * (size_t)h);
        if (!c)
            return false;
        r->color = c;
        r->own_color = true;
    }
    uint16_t *d = (uint16_t *)realloc(r->depth, sizeof(uint16_t) * (size_t)w * (size_t)h);
    if (!d)
        return false;
    r->depth = d;
    r->w = w;
    r->h = h;
    /* the ocean beyond the island: water over a dark sea floor, as the water inside is drawn */
    for (int i = 0; i < 256; i++)
        s_ocean[i] = blend(rgb565(90, 78, 60), g_tex565[T_WATER][i], WATER_ALPHA);
    return true;
}

void render_free(Renderer *r)
{
    if (r->own_color)
        free(r->color);
    free(r->depth);
    r->color = nullptr;
    r->depth = nullptr;
}

void camera_forward(const Camera *cam, float dir[3])
{
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    dir[0] = sinf(cam->yaw) * cp;
    dir[1] = sp;
    dir[2] = -cosf(cam->yaw) * cp;
}

static void setup(const Renderer *r, const Camera *cam)
{
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    s_fwd = {sy * cp, sp, -cy * cp};
    s_right = {cy, 0.0f, sy};
    s_up = {-sy * sp, cp, cy * sp};
    s_ex = cam->x;
    s_ey = cam->y;
    s_ez = cam->z;
    s_w = r->w;
    s_h = r->h;
    s_color = r->color;
    s_depth = r->depth;
    s_fx = (float)s_w * 0.5f / tanf(FOV_DEG * 3.14159265f / 360.0f);
    s_fy = s_fx;
    s_cx = (float)s_w * 0.5f;
    s_cy = (float)s_h * 0.5f;
    s_kx = s_cx / s_fx;
    s_ky = s_cy / s_fy;
    for (int i = 0; i < AXN; i++)
    {
        float d = (float)(i - OCEAN_RING) - s_ex;
        s_ax[i] = {d * s_right.x, d * s_up.x, d * s_fwd.x};
    }
    for (int i = 0; i <= WORLD_Y; i++)
    {
        float d = (float)i - s_ey;
        s_ay[i] = {d * s_right.y, d * s_up.y, d * s_fwd.y};
    }
    for (int i = 0; i < AZN; i++)
    {
        float d = (float)(i - OCEAN_RING) - s_ez;
        s_az[i] = {d * s_right.z, d * s_up.z, d * s_fwd.z};
    }
}

static inline void to_camera(float wx, float wy, float wz, float *o)
{
    float dx = wx - s_ex, dy = wy - s_ey, dz = wz - s_ez;
    o[0] = dx * s_right.x + dy * s_right.y + dz * s_right.z;
    o[1] = dx * s_up.x + dy * s_up.y + dz * s_up.z;
    o[2] = dx * s_fwd.x + dy * s_fwd.y + dz * s_fwd.z;
}

/* ---- spans --------------------------------------------------------------------------------------- */

struct Grad
{
    float iz, uz, vz, u, v, l, f; /* per pixel (u, v: for the affine spans) */
};

/*
 * One row of a triangle. PERSPECTIVE: exact texture coordinates every 8 pixels, linear
 * between; otherwise (small or distant faces, where the difference cannot be seen) linear
 * over the whole row, without any division.
 */
template <Mode MODE, bool PERSPECTIVE>
static void span(int y, int x0, int x1, float iz, float a, float b, float l, float f, const Grad *g)
{
    uint16_t *cp = s_color + y * s_w;
    uint16_t *zp = s_depth + y * s_w;
    const uint16_t *tex = s_tex;
    int32_t li = (int32_t)(l * 65536.0f), dli = (int32_t)(g->l * 65536.0f);
    int32_t fi = (int32_t)(f * 65536.0f), dfi = (int32_t)(g->f * 65536.0f);
    int32_t zi = (int32_t)(iz * (DEPTH_K * 256.0f)), dzi = (int32_t)(g->iz * (DEPTH_K * 256.0f));
    float u, v;
    int32_t dui = 0, dvi = 0;
    if (PERSPECTIVE)
    {
        float z = 1.0f / iz;
        u = a * z; /* a, b: u/z and v/z */
        v = b * z;
    }
    else
    {
        u = a; /* a, b: u and v */
        v = b;
        dui = (int32_t)(g->u * 65536.0f);
        dvi = (int32_t)(g->v * 65536.0f);
    }
    int32_t ui = (int32_t)(u * 65536.0f), vi = (int32_t)(v * 65536.0f);
    int x = x0;
    while (x < x1)
    {
        int n = x1 - x;
        if (PERSPECTIVE)
        {
            if (n > 8)
                n = 8;
            float fn = (float)n;
            iz += g->iz * fn;
            a += g->uz * fn;
            b += g->vz * fn;
            float z2 = 1.0f / iz;
            float u2 = a * z2, v2 = b * z2;
            float inv = 65536.0f / fn;
            ui = (int32_t)(u * 65536.0f);
            vi = (int32_t)(v * 65536.0f);
            dui = (int32_t)((u2 - u) * inv);
            dvi = (int32_t)((v2 - v) * inv);
            u = u2;
            v = v2;
        }
        for (int k = 0; k < n; k++, x++)
        {
            uint32_t depth = (uint32_t)zi >> 8;
            if (depth > zp[x])
            {
                uint32_t t = tex[((vi >> 12) & 0xF0) | ((ui >> 16) & 15)];
                uint32_t l32 = (uint32_t)((li >> 16) + 4) >> 3, f32 = (uint32_t)((fi >> 16) + 4) >> 3;
                if (MODE == M_WATER)
                {
                    cp[x] = blend(cp[x], shade(t, l32, f32), WATER_ALPHA);
                }
                else if (MODE == M_SOLID || t != TEX_HOLE)
                {
                    zp[x] = (uint16_t)depth;
                    cp[x] = shade(t, l32, f32);
                }
            }
            zi += dzi;
            ui += dui;
            vi += dvi;
            li += dli;
            fi += dfi;
        }
    }
}

/* ---- triangles ----------------------------------------------------------------------------------- */

static inline void project(const CV *c, SV *s)
{
    float iz = 1.0f / c->z;
    s->x = s_cx + s_fx * c->x * iz;
    s->y = s_cy - s_fy * c->y * iz;
    s->iz = iz;
    s->uz = c->u * iz;
    s->vz = c->v * iz;
    s->u = c->u;
    s->v = c->v;
    s->l = c->l;
    s->f = c->f;
}

/* the gradients of the face being drawn that are the same for both its triangles */
static bool s_have_plane;
static Grad s_px, s_py;
static bool s_perspective;

static void raster(const SV *v0, const SV *v1, const SV *v2)
{
    const SV *v[3] = {v0, v1, v2};
    float minx = fminf(v0->x, fminf(v1->x, v2->x)), maxx = fmaxf(v0->x, fmaxf(v1->x, v2->x));
    float miny = fminf(v0->y, fminf(v1->y, v2->y)), maxy = fmaxf(v0->y, fmaxf(v1->y, v2->y));
    if (maxx < 0 || minx > (float)s_w || maxy < 0 || miny > (float)s_h)
        return;
    float e1x = v1->x - v0->x, e1y = v1->y - v0->y, e2x = v2->x - v0->x, e2y = v2->y - v0->y;
    float area = e1x * e2y - e2x * e1y;
    if (fabsf(area) < 0.01f)
        return;
    float ia = 1.0f / area;
    Grad gx, gy;
#define GRAD(A)                                                                                                        \
    {                                                                                                                  \
        float d1 = v1->A - v0->A, d2 = v2->A - v0->A;                                                                  \
        gx.A = (d1 * e2y - d2 * e1y) * ia;                                                                             \
        gy.A = (d2 * e1x - d1 * e2x) * ia;                                                                             \
    }
    if (s_have_plane)
    {
        gx = s_px;
        gy = s_py;
    }
    else
    {
        GRAD(iz)
        GRAD(uz)
        GRAD(vz)
        GRAD(u)
        GRAD(v)
    }
    GRAD(l)
    GRAD(f)
#undef GRAD
    /* sort by y */
    const SV *t = v[0], *m = v[1], *btm = v[2];
    if (m->y < t->y)
    {
        const SV *x = t;
        t = m;
        m = x;
    }
    if (btm->y < m->y)
    {
        const SV *x = m;
        m = btm;
        btm = x;
    }
    if (m->y < t->y)
    {
        const SV *x = t;
        t = m;
        m = x;
    }
    int y0 = (int)ceilf(t->y - 0.5f), y1 = (int)ceilf(btm->y - 0.5f);
    y0 = y0 < 0 ? 0 : y0;
    y1 = y1 > s_h ? s_h : y1;
    if (y0 >= y1)
        return;
    float dl = btm->y - t->y, du = m->y - t->y, dd = btm->y - m->y;
    float slope_long = dl > 0 ? (btm->x - t->x) / dl : 0;
    float slope_top = du > 0 ? (m->x - t->x) / du : 0;
    float slope_bottom = dd > 0 ? (btm->x - m->x) / dd : 0;
    s_tris++;
    bool persp = s_perspective;
    for (int y = y0; y < y1; y++)
    {
        float yc = (float)y + 0.5f;
        float xa = t->x + (yc - t->y) * slope_long;
        float xb = yc < m->y ? t->x + (yc - t->y) * slope_top : m->x + (yc - m->y) * slope_bottom;
        float xl = xa < xb ? xa : xb, xr = xa < xb ? xb : xa;
        int x0 = (int)ceilf(xl - 0.5f), x1 = (int)ceilf(xr - 0.5f);
        x0 = x0 < 0 ? 0 : x0;
        x1 = x1 > s_w ? s_w : x1;
        if (x0 >= x1)
            continue;
        float px = (float)x0 + 0.5f - v0->x, py = yc - v0->y;
        float iz = v0->iz + gx.iz * px + gy.iz * py;
        float l = v0->l + gx.l * px + gy.l * py;
        float f = v0->f + gx.f * px + gy.f * py;
        if (iz <= 0)
            continue;
        l = l < 0 ? 0 : l > 255 ? 255 : l;
        f = f < 0 ? 0 : f > 255 ? 255 : f;
        s_rows++;
        s_pixels += x1 - x0;
        if (persp)
        {
            float a = v0->uz + gx.uz * px + gy.uz * py, b = v0->vz + gx.vz * px + gy.vz * py;
            switch (s_mode)
            {
            case M_SOLID: span<M_SOLID, true>(y, x0, x1, iz, a, b, l, f, &gx); break;
            case M_CUTOUT: span<M_CUTOUT, true>(y, x0, x1, iz, a, b, l, f, &gx); break;
            default: span<M_WATER, true>(y, x0, x1, iz, a, b, l, f, &gx); break;
            }
        }
        else
        {
            float a = v0->u + gx.u * px + gy.u * py, b = v0->v + gx.v * px + gy.v * py;
            switch (s_mode)
            {
            case M_SOLID: span<M_SOLID, false>(y, x0, x1, iz, a, b, l, f, &gx); break;
            case M_CUTOUT: span<M_CUTOUT, false>(y, x0, x1, iz, a, b, l, f, &gx); break;
            default: span<M_WATER, false>(y, x0, x1, iz, a, b, l, f, &gx); break;
            }
        }
    }
}

static inline void lerp_cv(const CV *a, const CV *b, float t, CV *o)
{
    o->x = a->x + (b->x - a->x) * t;
    o->y = a->y + (b->y - a->y) * t;
    o->z = a->z + (b->z - a->z) * t;
    o->u = a->u + (b->u - a->u) * t;
    o->v = a->v + (b->v - a->v) * t;
    o->l = a->l + (b->l - a->l) * t;
    o->f = a->f + (b->f - a->f) * t;
}

/* a triangle that crosses the near plane: clipped, then drawn as one or two */
static void clipped(const CV *a, const CV *b, const CV *c)
{
    const CV *in[3] = {a, b, c};
    CV out[4];
    int n = 0;
    for (int i = 0; i < 3; i++)
    {
        const CV *p = in[i], *q = in[(i + 1) % 3];
        bool pin = p->z >= Z_NEAR, qin = q->z >= Z_NEAR;
        if (pin)
            out[n++] = *p;
        if (pin != qin)
            lerp_cv(p, q, (Z_NEAR - p->z) / (q->z - p->z), &out[n++]);
    }
    SV s[4];
    for (int i = 0; i < n; i++)
        project(&out[i], &s[i]);
    if (n >= 3)
        raster(&s[0], &s[1], &s[2]);
    if (n == 4)
        raster(&s[0], &s[2], &s[3]);
}

/* ---- faces ----------------------------------------------------------------------------------------- */

/* fog by the distance along the ground (as the well-known game does): looking down from high
 * up still shows the ground below */
static inline float fog_of(const CV *v)
{
    float dx = v->x * s_right.x + v->y * s_up.x + v->z * s_fwd.x;
    float dz = v->x * s_right.z + v->y * s_up.z + v->z * s_fwd.z;
    float d2 = dx * dx + dz * dz;
    if (d2 <= s_fog_start * s_fog_start)
        return 0;
    float f = (sqrtf(d2) - s_fog_start) * s_fog_scale;
    return f > 255.0f ? 255.0f : f;
}

/* true when all four corners are outside one side of the view */
static bool outside(const CV *v)
{
    int right = 0, left = 0, top = 0, bottom = 0, behind = 0;
    for (int k = 0; k < 4; k++)
    {
        right += v[k].x > v[k].z * s_kx;
        left += v[k].x < -v[k].z * s_kx;
        top += v[k].y > v[k].z * s_ky;
        bottom += v[k].y < -v[k].z * s_ky;
        behind += v[k].z < Z_NEAR;
    }
    return right == 4 || left == 4 || top == 4 || bottom == 4 || behind == 4;
}

/* the gradients of the attributes that are planar over the whole face (1/z, u/z, v/z, u, v) */
static bool plane(const SV *p)
{
    float e1x = p[1].x - p[0].x, e1y = p[1].y - p[0].y, e2x = p[2].x - p[0].x, e2y = p[2].y - p[0].y;
    float area = e1x * e2y - e2x * e1y;
    if (fabsf(area) < 0.01f)
        return false;
    float ia = 1.0f / area;
#define GRAD(A)                                                                                                        \
    {                                                                                                                  \
        float d1 = p[1].A - p[0].A, d2 = p[2].A - p[0].A;                                                              \
        s_px.A = (d1 * e2y - d2 * e1y) * ia;                                                                           \
        s_py.A = (d2 * e1x - d1 * e2x) * ia;                                                                           \
    }
    GRAD(iz)
    GRAD(uz)
    GRAD(vz)
    GRAD(u)
    GRAD(v)
#undef GRAD
    return true;
}

static void face(CV *v)
{
    if (outside(v))
        return;
    float zmin = v[0].z, zmax = v[0].z;
    for (int k = 0; k < 4; k++)
    {
        v[k].f = fog_of(&v[k]);
        zmin = fminf(zmin, v[k].z);
        zmax = fmaxf(zmax, v[k].z);
    }
    s_quads++;
    /* the diagonal that keeps the ambient occlusion symmetric */
    static const int A[2][6] = {{0, 1, 2, 0, 2, 3}, {1, 2, 3, 1, 3, 0}};
    const int *o = A[v[0].l + v[2].l >= v[1].l + v[3].l ? 0 : 1];
    /* texture coordinates can be interpolated linearly where the depth hardly changes */
    s_perspective = zmax > zmin * 1.12f + 0.3f;
    if (zmin >= Z_NEAR)
    {
        SV p[4];
        for (int k = 0; k < 4; k++)
            project(&v[k], &p[k]);
        s_have_plane = plane(p);
        raster(&p[o[0]], &p[o[1]], &p[o[2]]);
        raster(&p[o[3]], &p[o[4]], &p[o[5]]);
        s_have_plane = false;
        return;
    }
    s_perspective = true;
    for (int i = 0; i < 6; i += 3)
    {
        const CV *a = &v[o[i]], *b = &v[o[i + 1]], *c = &v[o[i + 2]];
        if (a->z < Z_NEAR || b->z < Z_NEAR || c->z < Z_NEAR)
        {
            clipped(a, b, c);
        }
        else
        {
            SV s[3];
            project(a, &s[0]);
            project(b, &s[1]);
            project(c, &s[2]);
            raster(&s[0], &s[1], &s[2]);
        }
    }
}

static const int CA[4] = {0, 1, 1, 0}, CB[4] = {0, 0, 1, 1};

static void quad(const Quad *q)
{
    const FaceDef *fd = &g_faces[q->face];
    bool low = q->flags & Q_LOW;
    switch (q->face)
    {
    case 0: if (s_ex >= (float)q->x) return; break;
    case 1: if (s_ex <= (float)(q->x + 1)) return; break;
    case 2: if (s_ey >= (float)q->y) return; break;
    case 3: if (s_ey <= (float)q->y + (low ? 0.875f : 1.0f)) return; break;
    case 4: if (s_ez >= (float)q->z) return; break;
    default: if (s_ez <= (float)(q->z + 1)) return; break;
    }
    CV v[4];
    int ox = q->x + fd->o[0], oy = q->y + fd->o[1], oz = q->z + fd->o[2];
    int qw = q->w, qh = q->h;
    for (int k = 0; k < 4; k++)
    {
        int du = CA[k] * qw, dv = CB[k] * qh;
        int x = ox + du * fd->u[0] + dv * fd->v[0];
        int y = oy + du * fd->u[1] + dv * fd->v[1];
        int z = oz + du * fd->u[2] + dv * fd->v[2];
        const V3 &a = s_ax[x + OCEAN_RING], &b = s_ay[y], &c = s_az[z + OCEAN_RING];
        v[k].x = a.x + b.x + c.x;
        v[k].y = a.y + b.y + c.y;
        v[k].z = a.z + b.z + c.z;
        if (low)
        {
            v[k].y -= 0.125f * s_up.y;
            v[k].z -= 0.125f * s_fwd.y;
        }
        v[k].u = CA[k] ? 16.0f * (float)qw - TEX_EDGE : TEX_EDGE;
        v[k].v = CB[k] ? 16.0f * (float)qh - TEX_EDGE : TEX_EDGE;
        v[k].l = (float)q->light[k];
    }
    s_tex = g_tex565[q->tex];
    face(v);
}

/* the endless ocean: squares of 8 x 8 blocks around the island (small enough for the fog) */
static void ocean(void)
{
    if (s_ey <= (float)SEA_LEVEL - 0.125f)
        return;
    s_tex = s_ocean;
    s_mode = M_SOLID;
    for (int cz = -OCEAN_RING; cz < WORLD_Z + OCEAN_RING; cz += 8)
        for (int cx = -OCEAN_RING; cx < WORLD_X + OCEAN_RING; cx += 8)
        {
            if (cx >= 0 && cx < WORLD_X && cz >= 0 && cz < WORLD_Z)
                continue;
            float dx = (float)cx + 4.0f - s_ex, dz = (float)cz + 4.0f - s_ez;
            if (dx * dx + dz * dz > (VIEW_DIST + 6.0f) * (VIEW_DIST + 6.0f))
                continue;
            CV v[4];
            static const int X[4] = {0, 8, 8, 0}, Z[4] = {0, 0, 8, 8};
            for (int k = 0; k < 4; k++)
            {
                const V3 &a = s_ax[cx + X[k] + OCEAN_RING], &b = s_ay[SEA_LEVEL], &c = s_az[cz + Z[k] + OCEAN_RING];
                v[k].x = a.x + b.x + c.x;
                v[k].y = a.y + b.y + c.y - 0.125f * s_up.y;
                v[k].z = a.z + b.z + c.z - 0.125f * s_fwd.y;
                v[k].u = (float)X[k] * 16.0f;
                v[k].v = (float)Z[k] * 16.0f;
                v[k].l = 250.0f;
            }
            face(v);
        }
}

/* ---- sky ------------------------------------------------------------------------------------------- */

static void fill_row(uint16_t *p, int n, uint16_t c)
{
    if (((uintptr_t)p & 2) && n > 0)
    {
        *p++ = c;
        n--;
    }
    uint32_t cc = (uint32_t)c | ((uint32_t)c << 16);
    uint32_t *q = (uint32_t *)p;
    for (int i = 0; i < n / 2; i++)
        q[i] = cc;
    if (n & 1)
        p[n - 1] = c;
}

static void sky(bool underwater)
{
    static const float SUN[3] = {0.42f, 0.72f, -0.55f};
    int horizon[3] = {192, 216, 242}, zenith[3] = {98, 150, 228};
    for (int y = 0; y < s_h; y++)
    {
        uint16_t c;
        if (underwater)
        {
            c = rgb565(22, 52, 110);
        }
        else
        {
            float d = (s_cy - ((float)y + 0.5f)) / s_fy;
            float e = (s_up.y * d + s_fwd.y) / sqrtf(1.0f + d * d); /* sine of the elevation */
            float t = e <= 0 ? 0 : sqrtf(e);
            t = t > 1 ? 1 : t;
            c = rgb565((uint32_t)(horizon[0] + (zenith[0] - horizon[0]) * t), (uint32_t)(horizon[1] + (zenith[1] - horizon[1]) * t),
                       (uint32_t)(horizon[2] + (zenith[2] - horizon[2]) * t));
        }
        fill_row(s_color + y * s_w, s_w, c);
        fill_row(s_depth + y * s_w, s_w, 0);
    }
    if (underwater)
        return;
    float sx = SUN[0] * s_right.x + SUN[1] * s_right.y + SUN[2] * s_right.z;
    float sy = SUN[0] * s_up.x + SUN[1] * s_up.y + SUN[2] * s_up.z;
    float sz = SUN[0] * s_fwd.x + SUN[1] * s_fwd.y + SUN[2] * s_fwd.z;
    if (sz < 0.2f)
        return;
    int px = (int)(s_cx + s_fx * sx / sz), py = (int)(s_cy - s_fy * sy / sz);
    int r = (int)(s_fx * 0.06f) + 2;
    uint16_t core = rgb565(255, 248, 216), glow = rgb565(255, 244, 200);
    for (int y = py - r - 2; y <= py + r + 2; y++)
        for (int x = px - r - 2; x <= px + r + 2; x++)
        {
            if ((unsigned)x >= (unsigned)s_w || (unsigned)y >= (unsigned)s_h)
                continue;
            uint16_t *p = &s_color[y * s_w + x];
            *p = (abs(x - px) <= r && abs(y - py) <= r) ? core : blend(*p, glow, 14);
        }
}

/* ---- the outline of the aimed-at block --------------------------------------------------------- */

static void line(const float *a, const float *b)
{
    float pa[3] = {a[0], a[1], a[2]}, pb[3] = {b[0], b[1], b[2]};
    if (pa[2] < Z_NEAR && pb[2] < Z_NEAR)
        return;
    if (pa[2] < Z_NEAR || pb[2] < Z_NEAR)
    {
        float *p = pa[2] < Z_NEAR ? pa : pb;
        const float *q = pa[2] < Z_NEAR ? pb : pa;
        float t = (Z_NEAR - p[2]) / (q[2] - p[2]);
        for (int i = 0; i < 3; i++)
            p[i] += (q[i] - p[i]) * t;
    }
    float iza = 1.0f / pa[2], izb = 1.0f / pb[2];
    float xa = s_cx + s_fx * pa[0] * iza, ya = s_cy - s_fy * pa[1] * iza;
    float xb = s_cx + s_fx * pb[0] * izb, yb = s_cy - s_fy * pb[1] * izb;
    float dx = xb - xa, dy = yb - ya;
    int n = (int)fmaxf(fabsf(dx), fabsf(dy)) + 1;
    if (n > 4000)
        return;
    for (int i = 0; i <= n; i++)
    {
        float t = (float)i / (float)n;
        int x = (int)(xa + dx * t), y = (int)(ya + dy * t);
        if ((unsigned)x >= (unsigned)s_w || (unsigned)y >= (unsigned)s_h)
            continue;
        float iz = iza + (izb - iza) * t;
        int k = y * s_w + x;
        if (iz * DEPTH_K * 1.03f >= (float)s_depth[k])
            s_color[k] = rgb565(16, 16, 16);
    }
}

static void outline(const int *t)
{
    const float e = 0.004f;
    float c[8][3];
    for (int i = 0; i < 8; i++)
        to_camera((float)t[0] + ((i & 1) ? 1.0f + e : -e), (float)t[1] + ((i & 2) ? 1.0f + e : -e),
                  (float)t[2] + ((i & 4) ? 1.0f + e : -e), c[i]);
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int i = 0; i < 12; i++)
        line(c[E[i][0]], c[E[i][1]]);
}

/* ---- which sections are drawn ----------------------------------------------------------------------- */

static const float SECTION_RADIUS = 13.9f; /* of a section's bounding sphere */

static bool section_in_view(int sx, int sy, int sz)
{
    float c[3];
    to_camera((float)(sx * SECTION + 8), (float)(sy * SECTION + 8), (float)(sz * SECTION + 8), c);
    float r = SECTION_RADIUS;
    float hx = (float)(sx * SECTION + 8) - s_ex, hz = (float)(sz * SECTION + 8) - s_ez;
    if (hx * hx + hz * hz > (VIEW_DIST + r) * (VIEW_DIST + r) || c[2] < -r)
        return false;
    float nx = sqrtf(1.0f + s_kx * s_kx), ny = sqrtf(1.0f + s_ky * s_ky);
    if (c[0] - c[2] * s_kx > r * nx || -c[0] - c[2] * s_kx > r * nx)
        return false;
    if (c[1] - c[2] * s_ky > r * ny || -c[1] - c[2] * s_ky > r * ny)
        return false;
    return true;
}

static int section_faces(const Section *s)
{
    int n = s->nwater;
    for (int d = 0; d < 6; d++)
        n += s->nsolid[d];
    return n;
}

/*
 * The sections to draw, near ones first: a breadth-first walk from the camera's section,
 * into a neighbour only through the side the walk can leave the section by (connected, in
 * the section, with the side it came in through by cells one sees through), only away from
 * the camera and only while the neighbour is in view. Closed caves under the ground and
 * rock behind hills are never visited.
 */
static int collect(const Meshes *m, int16_t *out)
{
    static const int8_t DX[6] = {-1, 1, 0, 0, 0, 0}, DY[6] = {0, 0, -1, 1, 0, 0}, DZ[6] = {0, 0, 0, 0, -1, 1};
    static uint8_t visited[SECTION_COUNT];
    struct Node
    {
        int16_t index;
        int8_t from;  /* side it was entered through, -1: the start */
        uint8_t dirs; /* the directions walked so far */
    };
    static Node queue[SECTION_COUNT];
    int n = 0;
    int cx = (int)floorf(s_ex / SECTION), cy = (int)floorf(s_ey / SECTION), cz = (int)floorf(s_ez / SECTION);
    cy = cy >= SECTIONS_Y ? SECTIONS_Y - 1 : cy; /* above the world: start at the top */
    if (cx < 0 || cx >= SECTIONS_X || cy < 0 || cz < 0 || cz >= SECTIONS_Z)
    {
        /* outside the island: everything in view */
        for (int i = 0; i < SECTION_COUNT; i++)
            if (section_faces(&m->s[i]) &&
                section_in_view(i % SECTIONS_X, i / (SECTIONS_X * SECTIONS_Z), (i / SECTIONS_X) % SECTIONS_Z))
                out[n++] = (int16_t)i;
        return n;
    }
    memset(visited, 0, sizeof(visited));
    int head = 0, tail = 0;
    int start = section_index(cx, cy, cz);
    queue[tail++] = {(int16_t)start, -1, 0};
    visited[start] = 1;
    while (head < tail)
    {
        Node nd = queue[head++];
        const Section *s = &m->s[nd.index];
        if (section_faces(s))
            out[n++] = nd.index;
        int sx = nd.index % SECTIONS_X, sz = (nd.index / SECTIONS_X) % SECTIONS_Z, sy = nd.index / (SECTIONS_X * SECTIONS_Z);
        for (int d = 0; d < 6; d++)
        {
            if (nd.dirs & (1u << (d ^ 1)))
                continue; /* never back towards the camera */
            int nx = sx + DX[d], ny = sy + DY[d], nz = sz + DZ[d];
            if (nx < 0 || nx >= SECTIONS_X || ny < 0 || ny >= SECTIONS_Y || nz < 0 || nz >= SECTIONS_Z)
                continue;
            int ni = section_index(nx, ny, nz);
            if (visited[ni])
                continue;
            if (nd.from >= 0 && !section_connected(s, nd.from, d))
                continue;
            if (!section_in_view(nx, ny, nz))
                continue;
            visited[ni] = 1;
            queue[tail++] = {(int16_t)ni, (int8_t)(d ^ 1), (uint8_t)(nd.dirs | (1u << d))};
        }
    }
    return n;
}

/* ---- the frame ---------------------------------------------------------------------------------- */

/* under water or in the air: the fog */
static void atmosphere(Renderer *r, const World *w, const Camera *cam)
{
    r->underwater = world_get(w, (int)floorf(cam->x), (int)floorf(cam->y), (int)floorf(cam->z)) == B_WATER;
    if (r->underwater)
    {
        s_fog = spread(rgb565(22, 52, 110));
        s_fog_start = 1.5f;
        s_fog_scale = 255.0f / (14.0f - 1.5f);
    }
    else
    {
        s_fog = spread(rgb565(192, 216, 242));
        s_fog_start = FOG_START;
        s_fog_scale = 255.0f / (FOG_END - FOG_START);
    }
}

void render_frame(Renderer *r, const World *w, const Meshes *m, const Camera *cam, const int *target)
{
    setup(r, cam);
    atmosphere(r, w, cam);
    s_tris = s_quads = s_rows = s_pixels = 0;
    uint64_t t0 = crtos_time_us();
    sky(r->underwater);
    uint64_t t1 = crtos_time_us();

    static int16_t vis[SECTION_COUNT];
    int nvis = collect(m, vis);
    r->sections = nvis;

    for (int i = 0; i < nvis; i++)
    {
        int idx = vis[i];
        const Section *s = &m->s[idx];
        float x0 = (float)(idx % SECTIONS_X * SECTION), z0 = (float)((idx / SECTIONS_X) % SECTIONS_Z * SECTION);
        float y0 = (float)(idx / (SECTIONS_X * SECTIONS_Z) * SECTION);
        /* a whole direction faces away when the eye is behind all its faces */
        const bool away[6] = {s_ex >= x0 + 15.0f, s_ex <= x0 + 1.0f, s_ey >= y0 + 15.0f,
                              s_ey <= y0 + 1.0f,  s_ez >= z0 + 15.0f, s_ez <= z0 + 1.0f};
        for (int d = 0; d < 6; d++)
        {
            if (away[d])
                continue;
            const Quad *q = s->solid[d];
            for (int k = s->nsolid[d]; k > 0; k--, q++)
            {
                s_mode = (q->flags & Q_CUTOUT) ? M_CUTOUT : M_SOLID;
                quad(q);
            }
        }
    }
    ocean();
    uint64_t t2 = crtos_time_us();
    s_mode = M_WATER;
    for (int i = nvis - 1; i >= 0; i--)
    {
        const Section *s = &m->s[vis[i]];
        for (int k = 0; k < s->nwater; k++)
            quad(&s->water[k]);
    }
    if (target)
        outline(target);
    r->us_sky = (uint32_t)(t1 - t0);
    r->us_solid = (uint32_t)(t2 - t1);
    r->us_water = (uint32_t)(crtos_time_us() - t2);
    r->quads = s_quads;
    r->rows = s_rows;
    r->pixels = s_pixels;
    r->triangles = s_tris;
}

void render_ray(const Renderer *r, const Camera *cam, float px, float py, float dir[3])
{
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw), cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    float fx = (float)r->w * 0.5f / tanf(FOV_DEG * 3.14159265f / 360.0f);
    float a = (px - (float)r->w * 0.5f) / fx, b = ((float)r->h * 0.5f - py) / fx;
    float f[3] = {sy * cp, sp, -cy * cp}, rt[3] = {cy, 0.0f, sy}, up[3] = {-sy * sp, cp, cy * sp};
    float len = 0;
    for (int i = 0; i < 3; i++)
    {
        dir[i] = f[i] + rt[i] * a + up[i] * b;
        len += dir[i] * dir[i];
    }
    len = 1.0f / sqrtf(len);
    for (int i = 0; i < 3; i++)
        dir[i] *= len;
}

/* ---- the accelerator ---------------------------------------------------------------------------- */

/*
 * The same faces drawn by the accelerator (GPU2D_CAP_PRIMS, gfx_gpu_prims): each one a
 * quadrilateral textured in perspective by the GPU itself (the matrix from pixel to texel comes
 * from the face's plane), and over it its light and fog - a 2 x 2 texture stretched smoothly
 * across the face, premultiplied: the fog's colour added, the face darkened. Cut-out faces
 * (leaves, glass) take a texture lit as a whole instead (the light would darken what shows
 * through their holes), water a see-through one and its light over it.
 *
 * There is no depth buffer: the faces go the farthest first, in an order that is exact for a
 * world of blocks. A face is seen through the cell of air in front of it, and a ray of sight
 * passes cells farther and farther along each axis; so the cells' distances in layers of y
 * order everything, and in a layer the horizontal faces (which can only be seen from above or
 * below the layer) come before the upright ones, ordered by their distance in x and z. That
 * needs upright faces of one block each: the merged ones are drawn block by block (horizontal
 * ones stay merged). Water drawn in the same order is blended right.
 */

#define G_FACES 12000                 /* drawn at most */
#define G_PRIMS (2 * G_FACES + 64)
#define G_LEVELS 8                    /* light levels of the lit cut-out textures */
#define G_SKY_MAX 1024                /* rows of the sky texture */
#define G_GUARD 1.0f                  /* points stay within a screen beyond each edge */

enum { GT_BLOCK, GT_ALPHA, GT_LIGHT, GT_SKY, GT_MISC, GT_COUNT };
enum { GM_OUTLINE, GM_SUN };
enum { K_SOLID, K_CUTOUT, K_WATER, K_OCEAN };

/* a face to draw: a copy of its quad (read in order from the sections; through a pointer in
 * the faces' order it missed the data cache) */
struct FaceRef
{
    Quad q;          /* (the ocean: q.x, q.z the square's corner / 8) */
    uint8_t i, j;    /* the block of a merged upright face; 255: the whole face */
    uint8_t kind;
};

static gfx_shmem *s_gmem;
static gfx_texarray s_garr[GT_COUNT];
static uint32_t *s_glight;           /* 2 x 2 ARGB8888 a face */
static uint16_t *s_gsky;
static gpu2d_prim *s_gp;
static int16_t *s_grows;             /* the rows each primitive reaches (for the driver) */
static int s_gn, s_gnlight;
static FaceRef *s_gf;
static uint32_t *s_gorder;           /* key << 14 | index of the face, the farthest first */

static uint32_t argb_scale(uint32_t c, uint32_t k) /* k: 0..256 */
{
    uint32_t rb = ((c & 0xFF00FFu) * k >> 8) & 0xFF00FFu, g = ((c & 0x00FF00u) * k >> 8) & 0x00FF00u;
    return (c & 0xFF000000u) | rb | g;
}

bool render_gpu_init(void)
{
    if (s_gmem)
        return true;
    if (!(gfx_gpu_caps() & GPU2D_CAP_PRIMS))
        return false;
    /* the textures in one piece of shared memory */
    size_t at = 0;
    size_t o_block = at;
    at += (size_t)(T_COUNT + 1) * 512u;          /* RGB565 16 x 16, and the ocean */
    size_t o_alpha = at;
    at += (size_t)T_COUNT * G_LEVELS * 1024u;    /* ARGB8888 16 x 16 at each light level */
    size_t o_light = at;
    at += (size_t)G_FACES * 16u;                 /* ARGB8888 2 x 2 */
    size_t o_sky = at;
    at += G_SKY_MAX * 2u;                        /* RGB565 1 x rows */
    size_t o_misc = at;
    at += 2u * 1024u;                            /* ARGB8888 16 x 16 */
    s_gmem = gfx_shmem_create(at);
    s_gp = (gpu2d_prim *)memalign(32, sizeof(gpu2d_prim) * G_PRIMS); /* (a head in one cache line) */
    s_grows = (int16_t *)malloc(sizeof(int16_t) * 2u * G_PRIMS);
    s_gf = (FaceRef *)malloc(sizeof(FaceRef) * G_FACES);
    s_gorder = (uint32_t *)malloc(sizeof(uint32_t) * G_FACES);
    if (!s_gmem || !s_gp || !s_grows || !s_gf || !s_gorder)
    {
        gfx_shmem_free(s_gmem);
        free(s_gp);
        free(s_grows);
        free(s_gf);
        free(s_gorder);
        s_gmem = nullptr;
        return false;
    }
    uint8_t *base = (uint8_t *)s_gmem->p;
    uint16_t *block = (uint16_t *)(base + o_block);
    for (int t = 0; t < T_COUNT; t++)
        memcpy(block + t * 256, g_tex565[t], 512);
    memcpy(block + T_COUNT * 256, s_ocean, 512);
    uint32_t *alpha = (uint32_t *)(base + o_alpha);
    for (int t = 0; t < T_COUNT; t++)
        for (int l = 0; l < G_LEVELS; l++)
            for (int i = 0; i < 256; i++)
            {
                uint32_t c = g_tex[t][i];
                uint32_t a = !(c >> 24) ? 0u : t == T_WATER ? WATER_ALPHA * 255u / 32u : 255u;
                alpha[(t * G_LEVELS + l) * 256 + i] = argb_scale((c & 0xFFFFFFu) | a << 24, 256u * (uint32_t)(l + 1) / G_LEVELS);
            }
    s_glight = (uint32_t *)(base + o_light);
    s_gsky = (uint16_t *)(base + o_sky);
    uint32_t *misc = (uint32_t *)(base + o_misc);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            misc[GM_OUTLINE * 256 + y * 16 + x] = 0xFF101010u;
            bool core = x >= 3 && x <= 12 && y >= 3 && y <= 12;
            misc[GM_SUN * 256 + y * 16 + x] = core ? 0xFFFFF8D8u : (14u * 255u / 32u) << 24 | 0xFFF4C8u;
        }
    int sh = s_gmem->shm;
    s_garr[GT_BLOCK] = {sh, o_block, 16, 16, 32, GPU2D_FMT_RGB565, T_COUNT + 1};
    s_garr[GT_ALPHA] = {sh, o_alpha, 16, 16, 64, GPU2D_FMT_ARGB8888, T_COUNT * G_LEVELS};
    s_garr[GT_LIGHT] = {sh, o_light, 2, 2, 8, GPU2D_FMT_ARGB8888, G_FACES};
    s_garr[GT_SKY] = {sh, o_sky, 1, G_SKY_MAX, 2, GPU2D_FMT_RGB565, 1};
    s_garr[GT_MISC] = {sh, o_misc, 16, 16, 64, GPU2D_FMT_ARGB8888, 2};
    return true;
}

/* a new primitive (null: no room) */
static gpu2d_prim *gprim(int tex, int layer, int flags, int blend)
{
    if (s_gn >= G_PRIMS)
        return nullptr;
    gpu2d_prim *p = &s_gp[s_gn++];
    p->flags = (uint8_t)flags;
    p->blend = (uint8_t)blend;
    p->tex = (uint8_t)tex;
    p->reserved = 0;
    p->layer = (uint16_t)layer;
    p->reserved2 = 0;
    return p;
}

/* rounded to the nearest (a call of the C library's lrintf took as long as the rest) */
static inline int iround(float v)
{
    return (int)(v < 0 ? v - 0.5f : v + 0.5f);
}

static inline int16_t fx4(float v)
{
    return (int16_t)iround(v * 16.0f);
}

/* a rectangle of the screen (x0, y0)-(x1, y1) with texel (u0, v0) at its first corner and
 * (du, dv) texels a pixel */
static void grect(int tex, int layer, int blend, float x0, float y0, float x1, float y1, float u0, float v0, float du,
                  float dv)
{
    gpu2d_prim *p = gprim(tex, layer, GPU2D_PRIM_QUAD, blend);
    if (!p)
        return;
    s_grows[2 * (s_gn - 1)] = (int16_t)floorf(y0) - 1;
    s_grows[2 * (s_gn - 1) + 1] = (int16_t)ceilf(y1) + 1;
    const float xs[4] = {x0, x1, x1, x0}, ys[4] = {y0, y0, y1, y1};
    for (int k = 0; k < 4; k++)
    {
        p->x[k] = fx4(xs[k]);
        p->y[k] = fx4(ys[k]);
    }
    const float m[9] = {du, 0, u0 - x0 * du, 0, dv, v0 - y0 * dv, 0, 0, 1};
    memcpy(p->m, m, sizeof(m));
}

static void gsky(bool underwater)
{
    int h = s_h < G_SKY_MAX ? s_h : G_SKY_MAX;
    static const float SUN[3] = {0.42f, 0.72f, -0.55f};
    int horizon[3] = {192, 216, 242}, zenith[3] = {98, 150, 228};
    for (int y = 0; y < h; y++)
    {
        if (underwater)
        {
            s_gsky[y] = rgb565(22, 52, 110);
            continue;
        }
        float d = (s_cy - ((float)y + 0.5f)) / s_fy;
        float e = (s_up.y * d + s_fwd.y) / sqrtf(1.0f + d * d);
        float t = e <= 0 ? 0 : sqrtf(e);
        t = t > 1 ? 1 : t;
        s_gsky[y] = rgb565((uint32_t)(horizon[0] + (zenith[0] - horizon[0]) * t),
                           (uint32_t)(horizon[1] + (zenith[1] - horizon[1]) * t),
                           (uint32_t)(horizon[2] + (zenith[2] - horizon[2]) * t));
    }
    grect(GT_SKY, 0, GPU2D_PRIM_COPY, 0, 0, (float)s_w, (float)s_h, 0.5f, 0, 0, 1.0f);
    if (underwater)
        return;
    float sx = SUN[0] * s_right.x + SUN[1] * s_right.y + SUN[2] * s_right.z;
    float sy = SUN[0] * s_up.x + SUN[1] * s_up.y + SUN[2] * s_up.z;
    float sz = SUN[0] * s_fwd.x + SUN[1] * s_fwd.y + SUN[2] * s_fwd.z;
    if (sz < 0.2f)
        return;
    float px = s_cx + s_fx * sx / sz, py = s_cy - s_fy * sy / sz, r = s_fx * 0.06f + 2.0f, size = 2.0f * r + 5.0f;
    if (px + size < 0 || px - size > (float)s_w || py + size < 0 || py - size > (float)s_h)
        return;
    grect(GT_MISC, GM_SUN, GPU2D_PRIM_ALPHA, px - size / 2, py - size / 2, px + size / 2, py + size / 2, 0, 0,
          16.0f / size, 16.0f / size);
}

/* ---- the order ---- */

static int s_gcount;

/* the corners of face @f in camera space; *@tw, *@th: texels across */
static void gcorners(const FaceRef &f, float c[4][3], float *tw, float *th)
{
    const Quad *q = &f.q;
    if (f.kind == K_OCEAN)
    {
        static const int X[4] = {0, 8, 8, 0}, Z[4] = {0, 0, 8, 8};
        int x0 = q->x * 8, z0 = q->z * 8; /* (+ OCEAN_RING) */
        for (int k = 0; k < 4; k++)
        {
            const V3 &a = s_ax[x0 + X[k]], &b = s_ay[SEA_LEVEL], &cc = s_az[z0 + Z[k]];
            c[k][0] = a.x + b.x + cc.x;
            c[k][1] = a.y + b.y + cc.y - 0.125f * s_up.y;
            c[k][2] = a.z + b.z + cc.z - 0.125f * s_fwd.y;
        }
        *tw = *th = 8 * 16.0f;
        return;
    }
    const FaceDef *fd = &g_faces[q->face];
    bool low = q->flags & Q_LOW;
    int ox = q->x + fd->o[0], oy = q->y + fd->o[1], oz = q->z + fd->o[2];
    int u0 = 0, v0 = 0, u1 = q->w, v1 = q->h;
    if (f.i != 255)
    {
        u0 = f.i;
        v0 = f.j;
        u1 = u0 + 1;
        v1 = v0 + 1;
    }
    for (int k = 0; k < 4; k++)
    {
        int du = CA[k] ? u1 : u0, dv = CB[k] ? v1 : v0;
        int x = ox + du * fd->u[0] + dv * fd->v[0];
        int y = oy + du * fd->u[1] + dv * fd->v[1];
        int z = oz + du * fd->u[2] + dv * fd->v[2];
        const V3 &a = s_ax[x + OCEAN_RING], &b = s_ay[y], &cc = s_az[z + OCEAN_RING];
        c[k][0] = a.x + b.x + cc.x;
        c[k][1] = a.y + b.y + cc.y;
        c[k][2] = a.z + b.z + cc.z;
        if (low)
        {
            c[k][1] -= 0.125f * s_up.y;
            c[k][2] -= 0.125f * s_fwd.y;
        }
    }
    *tw = 16.0f * (float)(u1 - u0);
    *th = 16.0f * (float)(v1 - v0);
}

/* a face in view to the list, with its key */
static void gadd(const FaceRef &f, int ex, int ey, int ez)
{
    if (s_gcount >= G_FACES)
        return;
    float c[4][3], tw, th;
    gcorners(f, c, &tw, &th);
    CV cv[4];
    for (int k = 0; k < 4; k++)
    {
        cv[k].x = c[k][0];
        cv[k].y = c[k][1];
        cv[k].z = c[k][2];
    }
    if (outside(cv))
        return;
    uint32_t key;
    const Quad *q = &f.q;
    if (f.kind == K_OCEAN)
    {
        key = (uint32_t)abs(SEA_LEVEL - ey) << 10 | 1023u;
    }
    else
    {
        const FaceDef *fd = &g_faces[q->face];
        int ay = q->y + fd->n[1];
        if (f.i == 255)
        {
            key = (uint32_t)abs(ay - ey) << 10 | 1023u; /* horizontal: first in its layer */
        }
        else
        {
            int ax = q->x + f.i * fd->u[0] + f.j * fd->v[0] + fd->n[0];
            int az = q->z + f.i * fd->u[2] + f.j * fd->v[2] + fd->n[2];
            ay += f.i * fd->u[1] + f.j * fd->v[1];
            int d = abs(ax - ex) + abs(az - ez);
            key = (uint32_t)abs(ay - ey) << 10 | (uint32_t)(d > 1022 ? 1022 : d);
        }
    }
    s_gorder[s_gcount] = key << 14 | (uint32_t)s_gcount; /* (G_FACES < 2^14, the key < 2^17) */
    s_gf[s_gcount++] = f;
}

static void gadd_quad(const Quad *q, uint8_t kind, int ex, int ey, int ez)
{
    if (q->face == 2 || q->face == 3 || (q->w == 1 && q->h == 1))
    {
        gadd({*q, (uint8_t)(q->face == 2 || q->face == 3 ? 255 : 0), 0, kind}, ex, ey, ez);
        return;
    }
    for (int j = 0; j < q->h; j++)
        for (int i = 0; i < q->w; i++)
            gadd({*q, (uint8_t)i, (uint8_t)j, kind}, ex, ey, ez);
}

/* the farthest first: by layer, then by the rest of the key - indices with their keys, which
 * the data cache holds (moving the faces themselves missed it at every step) */
static void gsort(void)
{
    std::sort(s_gorder, s_gorder + s_gcount, [](uint32_t a, uint32_t b) { return a > b; });
}

/* ---- a face ---- */

/* the polygon of camera-space points @in (n) clipped by the plane a.p >= b (a 4th coordinate:
 * the plane's value at the point is a[0] x + a[1] y + a[2] z) */
static int gclip(const float (*in)[3], int n, float (*out)[3], float ax, float ay, float az, float b)
{
    int m = 0;
    for (int i = 0; i < n; i++)
    {
        const float *p = in[i], *q = in[(i + 1) % n];
        float dp = ax * p[0] + ay * p[1] + az * p[2] - b, dq = ax * q[0] + ay * q[1] + az * q[2] - b;
        if (dp >= 0)
        {
            memcpy(out[m++], p, sizeof(float) * 3);
        }
        if ((dp >= 0) != (dq >= 0))
        {
            float t = dp / (dp - dq);
            for (int a = 0; a < 3; a++)
                out[m][a] = p[a] + (q[a] - p[a]) * t;
            m++;
        }
    }
    return m;
}

/* the matrix from pixel to texel of the plane through @o with @u, @v per texel (camera
 * space): the inverse (adjugate) of pixel ~ K (o + s u + t v) */
static void gmatrix(const float *o, const float *u, const float *v, float *m)
{
    /* K A, A = [u v o] by columns, K = [fx 0 cx; 0 -fy cy; 0 0 1] */
    float a[3][3];
    const float *col[3] = {u, v, o};
    for (int c = 0; c < 3; c++)
    {
        a[0][c] = s_fx * col[c][0] + s_cx * col[c][2];
        a[1][c] = -s_fy * col[c][1] + s_cy * col[c][2];
        a[2][c] = col[c][2];
    }
    m[0] = a[1][1] * a[2][2] - a[1][2] * a[2][1];
    m[1] = a[0][2] * a[2][1] - a[0][1] * a[2][2];
    m[2] = a[0][1] * a[1][2] - a[0][2] * a[1][1];
    m[3] = a[1][2] * a[2][0] - a[1][0] * a[2][2];
    m[4] = a[0][0] * a[2][2] - a[0][2] * a[2][0];
    m[5] = a[0][2] * a[1][0] - a[0][0] * a[1][2];
    m[6] = a[1][0] * a[2][1] - a[1][1] * a[2][0];
    m[7] = a[0][1] * a[2][0] - a[0][0] * a[2][1];
    m[8] = a[0][0] * a[1][1] - a[0][1] * a[1][0];
    float det = a[0][0] * m[0] + a[0][1] * m[3] + a[0][2] * m[6];
    float k = det < 0 ? -1.0f : 1.0f; /* (the divisor positive in front) */
    float big = 0;
    for (int i = 0; i < 9; i++)
        big = fmaxf(big, fabsf(m[i]));
    k /= big > 0 ? big : 1.0f; /* (numbers of a size the GPU's floats like) */
    for (int i = 0; i < 9; i++)
        m[i] *= k;
}

static void gface(const FaceRef &f)
{
    float c[4][3];          /* the corners, camera space */
    float tw, th;           /* texels across */
    uint8_t light[4] = {250, 250, 250, 250};
    int tex, layer, blend;
    bool lit = true;        /* the light texture over it */
    gcorners(f, c, &tw, &th);
    const Quad *q = &f.q;
    if (f.kind == K_OCEAN)
    {
        tex = GT_BLOCK;
        layer = T_COUNT;
        blend = GPU2D_PRIM_COPY;
    }
    else
    {
        memcpy(light, q->light, 4);
        if (f.kind == K_SOLID)
        {
            tex = GT_BLOCK;
            layer = q->tex;
            blend = GPU2D_PRIM_COPY;
        }
        else
        {
            int l = f.kind == K_WATER ? 255 : (light[0] + light[1] + light[2] + light[3]) / 4;
            tex = GT_ALPHA;
            layer = q->tex * G_LEVELS + (l * G_LEVELS / 256 < G_LEVELS ? l * G_LEVELS / 256 : G_LEVELS - 1);
            blend = GPU2D_PRIM_ALPHA;
            lit = f.kind == K_WATER;
        }
    }
    CV cv[4]; /* (for the fog) */
    for (int k = 0; k < 4; k++)
    {
        cv[k].x = c[k][0];
        cv[k].y = c[k][1];
        cv[k].z = c[k][2];
    }
    s_quads++; /* (in view: gadd() saw to it) */
    /* the texture's plane: texel (s, t) at o + s u + t v, a little inside the edges (the
     * texture repeats: an edge pixel must not take the texel across) */
    float e = TEX_EDGE;
    float u[3], v[3], o[3];
    for (int a = 0; a < 3; a++)
    {
        u[a] = (c[1][a] - c[0][a]) / (tw - 2 * e);
        v[a] = (c[3][a] - c[0][a]) / (th - 2 * e);
        o[a] = c[0][a] - e * (u[a] + v[a]);
    }
    float m[9];
    gmatrix(o, u, v, m);
    /* clipped at the near plane and the guard band (the points stay within 12.4 fixed point) */
    float pa[10][3], pb[10][3];
    memcpy(pa, c, sizeof(c));
    int n = 4;
    float gx = (1.0f + 2.0f * G_GUARD) * s_kx, gy = (1.0f + 2.0f * G_GUARD) * s_ky;
    bool far_in = true;
    for (int k = 0; k < 4; k++)
        far_in = far_in && c[k][2] >= Z_NEAR && fabsf(c[k][0]) <= gx * c[k][2] && fabsf(c[k][1]) <= gy * c[k][2];
    if (!far_in)
    {
        n = gclip(pa, n, pb, 0, 0, 1, Z_NEAR);
        n = gclip(pb, n, pa, -1, 0, gx, 0);
        n = gclip(pa, n, pb, 1, 0, gx, 0);
        n = gclip(pb, n, pa, 0, -1, gy, 0);
        n = gclip(pa, n, pb, 0, 1, gy, 0);
        memcpy(pa, pb, sizeof(pa));
        if (n < 3)
            return;
    }
    int16_t sx[10], sy[10];
    for (int k = 0; k < n; k++)
    {
        float iz = 1.0f / pa[k][2];
        sx[k] = fx4(s_cx + s_fx * pa[k][0] * iz);
        sy[k] = fx4(s_cy - s_fy * pa[k][1] * iz);
    }
    /* light and fog: premultiplied, the fog's colour added and the face darkened */
    int lightl = -1;
    if (lit)
    {
        float fg[4];
        bool any = false;
        for (int k = 0; k < 4; k++)
        {
            fg[k] = fog_of(&cv[k]);
            any = any || fg[k] > 0 || light[k] < 250;
        }
        if (any && s_gnlight < G_FACES)
        {
            lightl = s_gnlight++;
            uint32_t *t = s_glight + lightl * 4;
            uint32_t fr = (s_fog >> 11) & 0x1Fu, fgc = (s_fog >> 21) & 0x3Fu, fb = s_fog & 0x1Fu; /* (spread) */
            fr = fr << 3 | fr >> 2;
            fgc = fgc << 2 | fgc >> 4;
            fb = fb << 3 | fb >> 2;
            static const int AT[4] = {0, 1, 3, 2};
            for (int k = 0; k < 4; k++)
            {
                float f = fg[k] / 255.0f, l = (float)light[k] / 255.0f * (1.0f - f);
                uint32_t a = (uint32_t)iround((1.0f - l) * 255.0f);
                uint32_t r = (uint32_t)iround((float)fr * f), g = (uint32_t)iround((float)fgc * f),
                         b = (uint32_t)iround((float)fb * f);
                t[AT[k]] = a << 24 | r << 16 | g << 8 | b;
            }
        }
    }
    /* a fan of quadrilaterals (and a triangle) */
    for (int k0 = 1; k0 + 1 < n; k0 += 2)
    {
        bool quad = k0 + 2 < n;
        gpu2d_prim *p = gprim(tex, layer, (quad ? GPU2D_PRIM_QUAD : 0) | GPU2D_PRIM_REPEAT, blend);
        if (!p)
            return;
        const int idx[4] = {0, k0, k0 + 1, quad ? k0 + 2 : 0};
        int lo = sy[0], hi = sy[0];
        for (int k = 0; k < 4; k++)
        {
            p->x[k] = sx[idx[k]];
            p->y[k] = sy[idx[k]];
            lo = sy[idx[k]] < lo ? sy[idx[k]] : lo;
            hi = sy[idx[k]] > hi ? sy[idx[k]] : hi;
        }
        s_grows[2 * (s_gn - 1)] = (int16_t)((lo >> 4) - 1);
        s_grows[2 * (s_gn - 1) + 1] = (int16_t)((hi >> 4) + 2);
        memcpy(p->m, m, sizeof(m));
        s_tris++;
        if (lightl >= 0)
        {
            /* the light texture's texel centres at the corners: u' = (s - e) / (tw - 2 e) + 0.5 */
            gpu2d_prim *l = gprim(GT_LIGHT, lightl, GPU2D_PRIM_AGAIN | GPU2D_PRIM_SMOOTH, GPU2D_PRIM_PREMUL);
            if (!l)
                return;
            s_grows[2 * (s_gn - 1)] = s_grows[2 * (s_gn - 2)]; /* (the rows of the one before) */
            s_grows[2 * (s_gn - 1) + 1] = s_grows[2 * (s_gn - 2) + 1];
            l->again[0] = 1.0f / (tw - 2 * e);
            l->again[1] = 0.5f - e * l->again[0];
            l->again[2] = 1.0f / (th - 2 * e);
            l->again[3] = 0.5f - e * l->again[2];
            s_tris++;
        }
        if (!quad)
            break;
    }
}

/* ---- the outline of the aimed-at block ---- */

static void gline(const float *a, const float *b)
{
    float pa[3] = {a[0], a[1], a[2]}, pb[3] = {b[0], b[1], b[2]};
    if (pa[2] < Z_NEAR && pb[2] < Z_NEAR)
        return;
    if (pa[2] < Z_NEAR || pb[2] < Z_NEAR)
    {
        float *p = pa[2] < Z_NEAR ? pa : pb;
        const float *q = pa[2] < Z_NEAR ? pb : pa;
        float t = (Z_NEAR - p[2]) / (q[2] - p[2]);
        for (int i = 0; i < 3; i++)
            p[i] += (q[i] - p[i]) * t;
    }
    float xa = s_cx + s_fx * pa[0] / pa[2], ya = s_cy - s_fy * pa[1] / pa[2];
    float xb = s_cx + s_fx * pb[0] / pb[2], yb = s_cy - s_fy * pb[1] / pb[2];
    float dx = xb - xa, dy = yb - ya, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.5f || fmaxf(fmaxf(fabsf(xa), fabsf(xb)), fmaxf(fabsf(ya), fabsf(yb))) > 2000.0f)
        return;
    float nx = -dy / len * 0.6f, ny = dx / len * 0.6f;
    gpu2d_prim *p = gprim(GT_MISC, GM_OUTLINE, GPU2D_PRIM_QUAD, GPU2D_PRIM_COPY);
    if (!p)
        return;
    s_grows[2 * (s_gn - 1)] = (int16_t)(fminf(ya, yb) - 2.0f);
    s_grows[2 * (s_gn - 1) + 1] = (int16_t)(fmaxf(ya, yb) + 2.0f);
    const float xs[4] = {xa + nx, xb + nx, xb - nx, xa - nx}, ys[4] = {ya + ny, yb + ny, yb - ny, ya - ny};
    for (int k = 0; k < 4; k++)
    {
        p->x[k] = fx4(xs[k]);
        p->y[k] = fx4(ys[k]);
    }
    const float m[9] = {0, 0, 8.0f, 0, 0, 8.0f, 0, 0, 1.0f};
    memcpy(p->m, m, sizeof(m));
}

/* the edges of the block's faces that face the eye (there is no depth test for the others) */
static void goutline(const int *t)
{
    const float e = 0.004f;
    const float eye[3] = {s_ex, s_ey, s_ez};
    float c[8][3];
    for (int i = 0; i < 8; i++)
        to_camera((float)t[0] + ((i & 1) ? 1.0f + e : -e), (float)t[1] + ((i & 2) ? 1.0f + e : -e),
                  (float)t[2] + ((i & 4) ? 1.0f + e : -e), c[i]);
    static const int E[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (int i = 0; i < 12; i++)
    {
        int a = E[i][0], b = E[i][1], along = (a ^ b) == 1 ? 0 : (a ^ b) == 2 ? 1 : 2;
        bool seen = false;
        for (int k = 0; k < 3; k++)
        {
            if (k == along)
                continue;
            int side = (a >> k) & 1;
            seen = seen || (side ? eye[k] > (float)t[k] + 1.0f : eye[k] < (float)t[k]);
        }
        if (seen)
            gline(c[a], c[b]);
    }
}

/* ---- the frame ---- */

static bool facing(const Quad *q)
{
    bool low = q->flags & Q_LOW;
    switch (q->face)
    {
    case 0: return s_ex < (float)q->x;
    case 1: return s_ex > (float)(q->x + 1);
    case 2: return s_ey < (float)q->y;
    case 3: return s_ey > (float)q->y + (low ? 0.875f : 1.0f);
    case 4: return s_ez < (float)q->z;
    default: return s_ez > (float)(q->z + 1);
    }
}

bool render_frame_gpu(Renderer *r, gfx_buffer *view, const World *w, const Meshes *m, const Camera *cam,
                      const int *target)
{
    if (!view || !render_gpu_init())
        return false;
    setup(r, cam);
    atmosphere(r, w, cam);
    s_tris = s_quads = s_rows = s_pixels = 0;
    s_gn = 0;
    s_gnlight = 0;
    s_gcount = 0;
    uint64_t t0 = crtos_time_us();
    gsky(r->underwater);

    static int16_t vis[SECTION_COUNT];
    int nvis = collect(m, vis);
    r->sections = nvis;
    int ex = (int)floorf(s_ex), ey = (int)floorf(s_ey), ez = (int)floorf(s_ez);
    for (int i = 0; i < nvis; i++)
    {
        int idx = vis[i];
        const Section *s = &m->s[idx];
        float x0 = (float)(idx % SECTIONS_X * SECTION), z0 = (float)((idx / SECTIONS_X) % SECTIONS_Z * SECTION);
        float y0 = (float)(idx / (SECTIONS_X * SECTIONS_Z) * SECTION);
        const bool away[6] = {s_ex >= x0 + 15.0f, s_ex <= x0 + 1.0f, s_ey >= y0 + 15.0f,
                              s_ey <= y0 + 1.0f,  s_ez >= z0 + 15.0f, s_ez <= z0 + 1.0f};
        for (int d = 0; d < 6; d++)
        {
            if (away[d])
                continue;
            const Quad *q = s->solid[d];
            for (int k = s->nsolid[d]; k > 0; k--, q++)
                if (facing(q))
                    gadd_quad(q, (q->flags & Q_CUTOUT) ? K_CUTOUT : K_SOLID, ex, ey, ez);
        }
        for (int k = 0; k < s->nwater; k++)
            if (facing(&s->water[k]))
                gadd_quad(&s->water[k], K_WATER, ex, ey, ez);
    }
    if (s_ey > (float)SEA_LEVEL - 0.125f)
        for (int cz = -OCEAN_RING; cz < WORLD_Z + OCEAN_RING; cz += 8)
            for (int cx = -OCEAN_RING; cx < WORLD_X + OCEAN_RING; cx += 8)
            {
                if (cx >= 0 && cx < WORLD_X && cz >= 0 && cz < WORLD_Z)
                    continue;
                float dx = (float)cx + 4.0f - s_ex, dz = (float)cz + 4.0f - s_ez;
                if (dx * dx + dz * dz > (VIEW_DIST + 6.0f) * (VIEW_DIST + 6.0f))
                    continue;
                Quad oq;
                memset(&oq, 0, sizeof(oq));
                oq.x = (uint8_t)((cx + OCEAN_RING) / 8);
                oq.z = (uint8_t)((cz + OCEAN_RING) / 8);
                gadd({oq, 255, 0, K_OCEAN}, ex, ey, ez);
            }
    gsort();
    uint64_t t1 = crtos_time_us();
    for (int i = 0; i < s_gcount; i++)
        gface(s_gf[s_gorder[i] & 0x3FFFu]);
    if (target)
        goutline(target);
    uint64_t t2 = crtos_time_us();
    int rc = gfx_gpu_prims(view, 0, 0, s_w, s_h, GPU2D_PRIMS_DISCARD, s_gp, s_grows, (unsigned)s_gn, s_garr, GT_COUNT);
    static int s_failed;
    if (rc && s_failed++ < 3)
        printf("voxel: the GPU did not draw (%s)\n", strerror(errno));
    r->us_sky = (uint32_t)(t1 - t0);
    r->us_solid = (uint32_t)(t2 - t1);
    r->us_water = (uint32_t)(crtos_time_us() - t2);
    r->quads = s_quads;
    r->triangles = s_gn;
    r->rows = 0;
    r->pixels = 0;
    return rc == 0;
}
