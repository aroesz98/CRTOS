/*
 * gpuprims - checks the accelerator's primitives (GPU2D_IOC_PRIMS) against the processor:
 * textures through an affine and a perspective matrix, a triangle, the blend modes, blending
 * over what the same call drew just before, and how fast thousands of small ones go.
 *
 *     gpuprims [-s FILE]      (-s: the perspective test's picture as a PPM file)
 *
 * One line per test, PASS or FAIL with what differed; the exit code is the number of failures.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <crtos.h>
#include "gfx.h"

#define W 128
#define H 128

static struct gfx_buffer *s_dst;
static struct gfx_shmem *s_mem;
#define PRIMS 12000
/* in s_mem: 8 x 8 RGB565 and 2 x 2 ARGB8888 textures, 4 layers each */
#define TEX_AT 0
#define ATEX_AT (TEX_AT + 8 * 8 * 2 * 4)
static uint8_t *s_tex, *s_atex;
static struct gfx_texarray s_arr[2];
static int s_failed;

enum { T565, TARGB };
enum { L_CHECK, L_RED, L_BLUE, L_BLACK };          /* RGB565 layers */
enum { A_GREEN50, A_WHITE50, A_GREY_PREMUL, A_RED50 }; /* ARGB8888 layers */

static uint16_t rgb565(int r, int g, int b)
{
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

/* the checker's texel (i, j): a colour of its own */
static uint16_t check(int i, int j)
{
    return rgb565(i * 32, j * 32, ((i ^ j) & 7) * 32);
}

static uint16_t *row(int y)
{
    return (uint16_t *)((uint8_t *)s_dst->s.pix + y * s_dst->s.stride);
}

static void clear(uint16_t c)
{
    for (int y = 0; y < s_dst->s.h; y++)
        for (int x = 0; x < s_dst->s.w; x++)
            row(y)[x] = c;
}

static struct gpu2d_prim s_prims[PRIMS] __attribute__((aligned(32)));

static struct gpu2d_prim *prim(int i)
{
    return &s_prims[i];
}

static void set_points(struct gpu2d_prim *p, int n, const float *xy)
{
    for (int k = 0; k < n; k++) {
        p->x[k] = (int16_t)lrintf(xy[2 * k] * 16.0f);
        p->y[k] = (int16_t)lrintf(xy[2 * k + 1] * 16.0f);
    }
    p->flags = (uint8_t)((p->flags & ~GPU2D_PRIM_QUAD) | (n == 4 ? GPU2D_PRIM_QUAD : 0));
}

static void rect_prim(struct gpu2d_prim *p, int tex, int layer, int flags, int blend, float x0, float y0, float x1,
                      float y1, const float *m)
{
    memset(p, 0, sizeof(*p));
    p->flags = (uint8_t)flags;
    p->blend = (uint8_t)blend;
    p->tex = (uint8_t)tex;
    p->layer = (uint16_t)layer;
    const float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    set_points(p, 4, xy);
    memcpy(p->m, m, sizeof(p->m));
}

static int draw(unsigned n)
{
    int r = gfx_gpu_prims(s_dst, 0, 0, s_dst->s.w, s_dst->s.h, 0, s_prims, NULL, n, s_arr, 2);
    if (r)
        printf("gpuprims: the call failed\n");
    return r;
}

static void result(const char *name, long bad, long total, const char *what)
{
    bool ok = bad * 1000 <= total; /* (a pixel in a thousand: rounding at texel edges) */
    printf("%s %-28s %ld of %ld pixels differ%s%s\n", ok ? "PASS" : "FAIL", name, bad, total, what ? ": " : "",
           what ? what : "");
    if (!ok)
        s_failed++;
}

/* the texel the matrix gives at pixel (x, y), sampled at the pixel's centre (+0.5) or corner */
static uint16_t expect_checker(const float *m, int x, int y, float off)
{
    float fx = (float)x + off, fy = (float)y + off;
    float q = m[6] * fx + m[7] * fy + m[8];
    float u = (m[0] * fx + m[1] * fy + m[2]) / q, v = (m[3] * fx + m[4] * fy + m[5]) / q;
    int i = (int)floorf(u) & 7, j = (int)floorf(v) & 7;
    return check(i, j);
}

static void test_matrix(const char *name, const float *m, const char *save)
{
    clear(0);
    rect_prim(prim(0), T565, L_CHECK, GPU2D_PRIM_REPEAT, GPU2D_PRIM_COPY, 0, 0, W, H, m);
    if (draw(1))
        return;
    long bad_c = 0, bad_k = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint16_t c = row(y)[x];
            bad_c += c != expect_checker(m, x, y, 0.5f);
            bad_k += c != expect_checker(m, x, y, 0.0f);
        }
    char what[64];
    snprintf(what, sizeof(what), "sampled at the corner: %ld differ", bad_k);
    result(name, bad_c < bad_k ? bad_c : bad_k, (long)W * H, bad_c <= bad_k ? "at the pixel centre" : what);
    if (save) {
        FILE *f = fopen(save, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", W, H);
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    uint16_t c = row(y)[x];
                    uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3)};
                    fwrite(rgb, 1, 3, f);
                }
            fclose(f);
            printf("     (the picture: %s)\n", save);
        }
    }
}

static bool inside_tri(const float *t, float px, float py)
{
    float d0 = (t[2] - t[0]) * (py - t[1]) - (t[3] - t[1]) * (px - t[0]);
    float d1 = (t[4] - t[2]) * (py - t[3]) - (t[5] - t[3]) * (px - t[2]);
    float d2 = (t[0] - t[4]) * (py - t[5]) - (t[1] - t[5]) * (px - t[4]);
    return (d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0);
}

static void test_triangle(void)
{
    static const float m[9] = {0, 0, 0, 0, 0, 0, 0, 0, 1};
    static const float t[6] = {10.5f, 8.25f, 117.0f, 30.0f, 33.75f, 120.5f};
    clear(0);
    struct gpu2d_prim *p = prim(0);
    rect_prim(p, T565, L_RED, 0, GPU2D_PRIM_COPY, 0, 0, 1, 1, m);
    set_points(p, 3, t);
    if (draw(1))
        return;
    long bad = 0, edge = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            bool want = inside_tri(t, (float)x + 0.5f, (float)y + 0.5f);
            bool got = row(y)[x] == rgb565(255, 0, 0);
            if (want != got) {
                /* a pixel on the edge either way */
                bool near = inside_tri(t, (float)x + 0.4f, (float)y + 0.4f) != inside_tri(t, (float)x + 0.6f, (float)y + 0.6f) ||
                            inside_tri(t, (float)x + 0.6f, (float)y + 0.4f) != inside_tri(t, (float)x + 0.4f, (float)y + 0.6f);
                if (near)
                    edge++;
                else
                    bad++;
            }
        }
    char what[48];
    snprintf(what, sizeof(what), "%ld more on the edges", edge);
    result("triangle", bad, (long)W * H, what);
}

static int chan(uint16_t c, int k)
{
    return k == 0 ? (c >> 11) << 3 : k == 1 ? ((c >> 5) & 63) << 2 : (c & 31) << 3;
}

/* the pixel in the middle against r, g, b, give or take @tol */
static void test_colour(const char *name, int r, int g, int b, int tol)
{
    uint16_t c = row(H / 2)[W / 2];
    int want[3] = {r, g, b};
    bool ok = true;
    for (int k = 0; k < 3; k++)
        ok = ok && abs(chan(c, k) - want[k]) <= tol;
    printf("%s %-28s got %d %d %d, want %d %d %d\n", ok ? "PASS" : "FAIL", name, chan(c, 0), chan(c, 1), chan(c, 2), r,
           g, b);
    if (!ok)
        s_failed++;
}

static void test_blends(void)
{
    static const float m[9] = {0, 0, 0.5f, 0, 0, 0.5f, 0, 0, 1}; /* (the middle of texel 0) */
    clear(0);
    rect_prim(prim(0), TARGB, A_RED50, 0, GPU2D_PRIM_ALPHA, 32, 32, 96, 96, m);
    if (!draw(1))
        test_colour("alpha blend over black", 128, 0, 0, 12);
    clear(0xffff);
    rect_prim(prim(0), TARGB, A_GREY_PREMUL, 0, GPU2D_PRIM_PREMUL, 32, 32, 96, 96, m);
    if (!draw(1))
        test_colour("premultiplied over white", 160, 160, 160, 12);
    /* blending over what the call drew before: red, green over it, then blue, white over that */
    clear(0);
    rect_prim(prim(0), T565, L_RED, 0, GPU2D_PRIM_COPY, 0, 0, W, H, m);
    rect_prim(prim(1), TARGB, A_GREEN50, 0, GPU2D_PRIM_ALPHA, 0, 0, W, H, m);
    rect_prim(prim(2), T565, L_BLUE, 0, GPU2D_PRIM_COPY, 16, 16, W - 16, H - 16, m);
    rect_prim(prim(3), TARGB, A_WHITE50, 0, GPU2D_PRIM_ALPHA, 0, 0, W, H, m);
    if (!draw(4))
        test_colour("blend over the call's own", 128, 128, 255, 12);
    /* the same many times over: each blend reads what the one before wrote */
    clear(0);
    int n = 0;
    for (int k = 0; k < 64; k++) {
        rect_prim(prim(n++), T565, (k & 1) ? L_BLUE : L_RED, 0, GPU2D_PRIM_COPY, 40, 40, 88, 88, m);
        rect_prim(prim(n++), TARGB, A_GREEN50, 0, GPU2D_PRIM_ALPHA, 40, 40, 88, 88, m);
    }
    if (!draw((unsigned)n))
        test_colour("64 copies and blends", 0, 128, 128, 12); /* the last: blue, green over it */
}

/* thousands of small quads: how long one takes by its size and kind (as the faces of a 3D
 * scene: textured in perspective, then a light texture premultiplied over) */
static void speed_in(const char *name, int count, int size, int kind, int dw, int dh)
{
    srand(7);
    for (int i = 0; i < count; i++) {
        float x = (float)(rand() % (dw - size)), y = (float)(rand() % (dh - size)), s = (float)size;
        float m[9] = {1.0f, 0, -x, 0, 1.0f, -y, 0.002f, 0, 1.0f};
        float ml[9] = {1.0f / s, 0, 0.5f - x / s, 0, 1.0f / s, 0.5f - y / s, 0, 0, 1.0f};
        if (kind == 0)
            rect_prim(prim(i), T565, L_CHECK, GPU2D_PRIM_REPEAT, GPU2D_PRIM_COPY, x, y, x + s, y + s, m);
        else if (kind == 1)
            rect_prim(prim(i), TARGB, A_GREY_PREMUL, GPU2D_PRIM_SMOOTH, GPU2D_PRIM_PREMUL, x, y, x + s, y + s, ml);
        else if (kind == 2) /* affine copy */
            rect_prim(prim(i), T565, L_CHECK, GPU2D_PRIM_REPEAT, GPU2D_PRIM_COPY, x, y, x + s, y + s, ml);
        else if (kind == 4) { /* a face: perspective copy, then a light texture over it (AGAIN) */
            if (i & 1) {
                rect_prim(prim(i), TARGB, A_GREY_PREMUL, GPU2D_PRIM_SMOOTH | GPU2D_PRIM_AGAIN, GPU2D_PRIM_PREMUL, 0, 0,
                          0, 0, ml);
                prim(i)->again[0] = 1.0f / s;
                prim(i)->again[1] = 0.5f;
                prim(i)->again[2] = 1.0f / s;
                prim(i)->again[3] = 0.5f;
            } else {
                rect_prim(prim(i), T565, L_CHECK, GPU2D_PRIM_REPEAT, GPU2D_PRIM_COPY, x, y, x + s, y + s, m);
            }
        } else { /* the same quad every time */
            rect_prim(prim(i), T565, L_CHECK, GPU2D_PRIM_REPEAT, GPU2D_PRIM_COPY, 10, 10, 10 + s, 10 + s, m);
        }
    }
    uint64_t t0 = crtos_time_us();
    int r = draw((unsigned)count);
    uint64_t us = crtos_time_us() - t0;
    if (!r)
        printf("TIME %-34s %5d of %2d x %2d: %6lu us, %5lu ns each\n", name, count, size, size, (unsigned long)us,
               (unsigned long)(us * 1000u / (unsigned)count));
}

static void speed(const char *name, int count, int size, int kind)
{
    speed_in(name, count, size, kind, 400, 217);
}

static void test_speed(void)
{
    struct gfx_buffer *big = gfx_buffer_create(400, 217, GPU2D_FMT_RGB565);
    if (!big) {
        printf("gpuprims: no memory for the speed test\n");
        return;
    }
    struct gfx_buffer *keep = s_dst;
    s_dst = big;
    speed("perspective copy", 4000, 1, 0);
    speed("perspective copy", 4000, 4, 0);
    speed("perspective copy", 4000, 16, 0);
    speed("perspective copy", 1000, 64, 0);
    speed("affine copy", 4000, 16, 2);
    speed("premultiplied, bilinear", 4000, 16, 1);
    speed("premultiplied, bilinear", 1000, 64, 1);
    speed("same place, same values", 4000, 16, 3);
    speed("faces: copy + light again", 10000, 6, 4);
    speed("faces: copy + light again", 10000, 12, 4);
    gfx_buffer_free(big);
    s_dst = keep;
}

int main(int argc, char **argv)
{
    const char *save = argc > 2 && !strcmp(argv[1], "-s") ? argv[2] : NULL;
    if (!(gfx_gpu_caps() & GPU2D_CAP_PRIMS)) {
        printf("gpuprims: the accelerator draws no primitives\n");
        return 1;
    }
    s_dst = gfx_buffer_create(W, H, GPU2D_FMT_RGB565);
    s_mem = gfx_shmem_create(ATEX_AT + 2 * 2 * 4 * 4);
    if (!s_dst || !s_mem) {
        printf("gpuprims: no memory\n");
        return 1;
    }
    s_tex = (uint8_t *)s_mem->p + TEX_AT;
    s_atex = (uint8_t *)s_mem->p + ATEX_AT;
    for (int l = 0; l < 4; l++)
        for (int j = 0; j < 8; j++)
            for (int i = 0; i < 8; i++) {
                uint16_t c = l == L_CHECK ? check(i, j) : l == L_RED ? rgb565(255, 0, 0) : l == L_BLUE ? rgb565(0, 0, 255) : 0;
                ((uint16_t *)(s_tex + (l * 8 + j) * 16))[i] = c;
            }
    static const uint32_t ARGB[4] = {0x8000ff00u, 0x80ffffffu, 0x80202020u, 0x80ff0000u};
    for (int l = 0; l < 4; l++)
        for (int j = 0; j < 2; j++)
            for (int i = 0; i < 2; i++)
                ((uint32_t *)(s_atex + (l * 2 + j) * 8))[i] = ARGB[l];
    s_arr[T565] = (struct gfx_texarray){s_mem->shm, TEX_AT, 8, 8, 16, GPU2D_FMT_RGB565, 4};
    s_arr[TARGB] = (struct gfx_texarray){s_mem->shm, ATEX_AT, 2, 2, 8, GPU2D_FMT_ARGB8888, 4};

    static const float scale[9] = {1.0f / 16, 0, 0, 0, 1.0f / 16, 0, 0, 0, 1};
    static const float pixel[9] = {0.5f, 0, 0, 0, 0.5f, 0, 0, 0, 1};
    static const float persp[9] = {0.25f, 0, 0, 0, 0.25f, 0, 0.004f, 0.002f, 1};
    test_matrix("affine: texel = pixel / 16", scale, NULL);
    test_matrix("affine: texel = pixel / 2", pixel, NULL);
    test_matrix("perspective", persp, save);
    test_triangle();
    test_blends();
    test_speed();
    printf("gpuprims: %d failed\n", s_failed);
    return s_failed;
}
