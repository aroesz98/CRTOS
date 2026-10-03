/*
 * voxel_noise.h - random numbers and gradient (Perlin) noise for the world generator.
 */
#ifndef VOXEL_NOISE_H
#define VOXEL_NOISE_H

#include <stdint.h>

struct Rng
{
    uint32_t s;
};

static inline uint32_t rng_next(Rng *r)
{
    uint32_t x = r->s ? r->s : 0x9E3779B9u; /* xorshift32 */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

/* lo..hi inclusive */
static inline int rng_range(Rng *r, int lo, int hi)
{
    return lo + (int)(rng_next(r) % (uint32_t)(hi - lo + 1));
}

/* 0..1 */
static inline float rng_float(Rng *r)
{
    return (float)(rng_next(r) >> 8) * (1.0f / 16777216.0f);
}

struct Noise
{
    uint8_t p[512];
};

void noise_init(Noise *n, uint32_t seed);
float noise2(const Noise *n, float x, float y);            /* about -1..1 */
float noise3(const Noise *n, float x, float y, float z);   /* about -1..1 */
float fbm2(const Noise *n, float x, float y, int octaves); /* about -1..1 */

#endif
