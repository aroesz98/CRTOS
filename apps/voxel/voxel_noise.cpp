/*
 * voxel_noise.cpp - improved Perlin noise (2D and 3D) over a permutation made from a seed,
 * and fractal sums of it.
 */
#include <math.h>

#include "voxel_noise.h"

void noise_init(Noise *n, uint32_t seed)
{
    Rng r = {seed ^ 0x5bd1e995u};
    for (int i = 0; i < 256; i++)
        n->p[i] = (uint8_t)i;
    for (int i = 255; i > 0; i--)
    {
        int j = (int)(rng_next(&r) % (uint32_t)(i + 1));
        uint8_t t = n->p[i];
        n->p[i] = n->p[j];
        n->p[j] = t;
    }
    for (int i = 0; i < 256; i++)
        n->p[256 + i] = n->p[i];
}

static inline float fade(float t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

static inline float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline float grad2(int h, float x, float y)
{
    switch (h & 7)
    {
    case 0: return x + y;
    case 1: return x - y;
    case 2: return -x + y;
    case 3: return -x - y;
    case 4: return x;
    case 5: return -x;
    case 6: return y;
    default: return -y;
    }
}

static inline float grad3(int h, float x, float y, float z)
{
    int k = h & 15;
    float u = k < 8 ? x : y;
    float v = k < 4 ? y : (k == 12 || k == 14) ? x : z;
    return ((k & 1) ? -u : u) + ((k & 2) ? -v : v);
}

float noise2(const Noise *n, float x, float y)
{
    float fx = floorf(x), fy = floorf(y);
    int X = (int)fx & 255, Y = (int)fy & 255;
    x -= fx;
    y -= fy;
    float u = fade(x), v = fade(y);
    const uint8_t *p = n->p;
    int a = p[X] + Y, b = p[X + 1] + Y;
    float r = lerp(lerp(grad2(p[a], x, y), grad2(p[b], x - 1, y), u),
                   lerp(grad2(p[a + 1], x, y - 1), grad2(p[b + 1], x - 1, y - 1), u), v);
    return r * 0.7f;
}

float noise3(const Noise *n, float x, float y, float z)
{
    float fx = floorf(x), fy = floorf(y), fz = floorf(z);
    int X = (int)fx & 255, Y = (int)fy & 255, Z = (int)fz & 255;
    x -= fx;
    y -= fy;
    z -= fz;
    float u = fade(x), v = fade(y), w = fade(z);
    const uint8_t *p = n->p;
    int A = p[X] + Y, AA = p[A] + Z, AB = p[A + 1] + Z;
    int B = p[X + 1] + Y, BA = p[B] + Z, BB = p[B + 1] + Z;
    return lerp(lerp(lerp(grad3(p[AA], x, y, z), grad3(p[BA], x - 1, y, z), u),
                     lerp(grad3(p[AB], x, y - 1, z), grad3(p[BB], x - 1, y - 1, z), u), v),
                lerp(lerp(grad3(p[AA + 1], x, y, z - 1), grad3(p[BA + 1], x - 1, y, z - 1), u),
                     lerp(grad3(p[AB + 1], x, y - 1, z - 1), grad3(p[BB + 1], x - 1, y - 1, z - 1), u), v),
                w);
}

float fbm2(const Noise *n, float x, float y, int octaves)
{
    float sum = 0, amp = 1, norm = 0;
    for (int i = 0; i < octaves; i++)
    {
        sum += noise2(n, x, y) * amp;
        norm += amp;
        amp *= 0.5f;
        x = x * 2.03f + 17.1f;
        y = y * 2.03f + 9.7f;
    }
    return sum / norm;
}
