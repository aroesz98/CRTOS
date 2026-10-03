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
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

bool render_init(Renderer *r, int w, int h)
{
    uint16_t *c = (uint16_t *)realloc(r->color, sizeof(uint16_t) * (size_t)w * (size_t)h);
    if (!c)
        return false;
    r->color = c;
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

void render_frame(Renderer *r, const World *w, const Meshes *m, const Camera *cam, const int *target)
{
    setup(r, cam);
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
