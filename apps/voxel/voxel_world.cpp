/*
 * voxel_world.cpp - block storage and the island generator.
 *
 * Generation, in this order:
 *   1. height map: continents, hills and ridged mountains of Perlin noise, pulled down to the
 *      sea near the edges (an island with a wobbly coast);
 *   2. columns: bedrock, stone, dirt / sand / sandstone and the surface (grass, sand, snow,
 *      bare rock on steep slopes), water up to the sea level;
 *   3. caves (worms that carve spheres) and ore veins;
 *   4. structures: a village around a well (houses with pitched roofs, a watch tower, gravel
 *      paths; its style follows the biome), lone towers on hills, a lighthouse on the coast
 *      and a pyramid in the desert - each on ground levelled for it;
 *   5. trees (oaks, spruces in the heights) and cacti;
 *   6. the sky height of every column.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "voxel_noise.h"
#include "voxel_world.h"

bool world_alloc(World *w)
{
    w->b = (uint8_t *)malloc((size_t)WORLD_X * WORLD_Y * WORLD_Z);
    w->top = (uint8_t *)malloc((size_t)WORLD_X * WORLD_Z);
    return w->b && w->top;
}

void world_free(World *w)
{
    free(w->b);
    free(w->top);
    w->b = w->top = nullptr;
}

void world_column_top(World *w, int x, int z)
{
    int y = WORLD_Y - 1;
    while (y >= 0 && !block_opaque(w->b[world_index(x, y, z)]))
        y--;
    w->top[z * WORLD_X + x] = (uint8_t)(y + 1);
}

bool world_set(World *w, int x, int y, int z, uint8_t block)
{
    if (!world_inside(x, y, z))
        return false;
    w->b[world_index(x, y, z)] = block;
    uint8_t old = w->top[z * WORLD_X + x];
    if (block_opaque(block))
    {
        if (y >= old)
            w->top[z * WORLD_X + x] = (uint8_t)(y + 1);
    }
    else if (y == old - 1)
    {
        world_column_top(w, x, z);
    }
    return w->top[z * WORLD_X + x] != old;
}

/* ---- generator state ------------------------------------------------------------------------ */

static Noise s_cont, s_hill, s_mount, s_temp, s_forest;
static int16_t *s_h;        /* surface height per column: first air / water cell */
static Rng s_rng;

struct Rect
{
    int x0, z0, x1, z1; /* inclusive-exclusive */
};
static Rect s_used[48];     /* ground taken by structures */
static int s_nused;

static inline void put(World *w, int x, int y, int z, uint8_t b)
{
    if (world_inside(x, y, z))
        w->b[world_index(x, y, z)] = b;
}

static inline uint8_t get(const World *w, int x, int y, int z)
{
    return world_get(w, x, y, z);
}

static inline int H(int x, int z)
{
    x = x < 0 ? 0 : x >= WORLD_X ? WORLD_X - 1 : x;
    z = z < 0 ? 0 : z >= WORLD_Z ? WORLD_Z - 1 : z;
    return s_h[z * WORLD_X + x];
}

static float smoothstep(float a, float b, float x)
{
    float t = (x - a) / (b - a);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3.0f - 2.0f * t);
}

enum Biome
{
    BIOME_PLAINS,
    BIOME_DESERT,
    BIOME_SNOW
};

static Biome biome(int x, int z)
{
    int h = H(x, z);
    if (h > 45)
        return BIOME_SNOW;
    float t = fbm2(&s_temp, (float)x / 70.0f, (float)z / 70.0f, 2);
    if (t > 0.2f && h < 40)
        return BIOME_DESERT;
    return BIOME_PLAINS;
}

/* ---- 1. height map --------------------------------------------------------------------------- */

static int height_at(int x, int z)
{
    float fx = (float)x, fz = (float)z;
    float dx = (fx - WORLD_X * 0.5f) / (WORLD_X * 0.5f), dz = (fz - WORLD_Z * 0.5f) / (WORLD_Z * 0.5f);
    float d = sqrtf(dx * dx + dz * dz) + fbm2(&s_cont, fx / 36.0f, fz / 36.0f, 2) * 0.22f;
    float cont = fbm2(&s_cont, fx / 80.0f + 50.0f, fz / 80.0f, 4);
    float hills = fbm2(&s_hill, fx / 26.0f, fz / 26.0f, 3);
    float mask = smoothstep(0.02f, 0.3f, fbm2(&s_mount, fx / 64.0f, fz / 64.0f, 3));
    float ridge = 1.0f - fabsf(noise2(&s_mount, fx / 20.0f + 7.0f, fz / 20.0f));
    ridge *= ridge;
    float h = (float)SEA_LEVEL + 4.0f + cont * 12.0f + hills * 5.0f + mask * (ridge * 20.0f + 7.0f);
    float fall = smoothstep(0.55f, 0.95f, d);
    h = h * (1.0f - fall) + (float)(SEA_LEVEL - 14) * fall;
    int ih = (int)h;
    return ih < 4 ? 4 : ih > WORLD_Y - 6 ? WORLD_Y - 6 : ih;
}

/* ---- 2. columns -------------------------------------------------------------------------------- */

static void fill_column(World *w, int x, int z)
{
    int h = H(x, z);
    int slope = 0;
    for (int k = 0; k < 4; k++)
    {
        int nh = H(x + (k == 0) - (k == 1), z + (k == 2) - (k == 3));
        int d = abs(nh - h);
        slope = d > slope ? d : slope;
    }
    Biome bi = biome(x, z);
    int dirt = 3 + (int)(rng_next(&s_rng) % 2);
    for (int y = 0; y < WORLD_Y; y++)
    {
        uint8_t b = B_AIR;
        if (y == 0 || (y == 1 && (rng_next(&s_rng) & 1)) || (y == 2 && !(rng_next(&s_rng) & 3)))
        {
            b = B_BEDROCK;
        }
        else if (y < h)
        {
            int depth = h - 1 - y; /* 0: the surface block */
            b = B_STONE;
            if (h <= SEA_LEVEL + 1)
            {
                /* beaches and the sea floor */
                if (depth < 3)
                    b = (h < SEA_LEVEL - 5 && fbm2(&s_hill, (float)x / 9.0f, (float)z / 9.0f, 1) > 0.15f) ? B_GRAVEL
                        : (h < SEA_LEVEL - 7 && depth == 0)                                             ? B_DIRT
                                                                                                        : B_SAND;
                else if (depth < 5)
                    b = B_SANDSTONE;
            }
            else if (bi == BIOME_DESERT)
            {
                if (depth < 4)
                    b = B_SAND;
                else if (depth < 7)
                    b = B_SANDSTONE;
            }
            else if (slope >= 3 && h > SEA_LEVEL + 8)
            {
                b = depth == 0 && bi == BIOME_SNOW && slope < 5 ? B_SNOW : B_STONE; /* cliffs */
            }
            else if (depth == 0)
            {
                b = bi == BIOME_SNOW ? B_SNOW : B_GRASS;
            }
            else if (depth <= dirt)
            {
                b = B_DIRT;
            }
        }
        else if (y < SEA_LEVEL)
        {
            b = B_WATER;
        }
        w->b[world_index(x, y, z)] = b;
    }
}

/* ---- 3. caves and ores --------------------------------------------------------------------------- */

static void carve(World *w, float cx, float cy, float cz, float r, bool open_top)
{
    int x0 = (int)floorf(cx - r), x1 = (int)ceilf(cx + r);
    int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
    int z0 = (int)floorf(cz - r), z1 = (int)ceilf(cz + r);
    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
            {
                if (!world_inside(x, y, z) || y < 1)
                    continue;
                float dx = (float)x + 0.5f - cx, dy = ((float)y + 0.5f - cy) * 1.3f, dz = (float)z + 0.5f - cz;
                if (dx * dx + dy * dy + dz * dz > r * r)
                    continue;
                uint8_t b = w->b[world_index(x, y, z)];
                if (b == B_WATER || b == B_BEDROCK || get(w, x, y + 1, z) == B_WATER)
                    continue;
                if (!open_top && y >= H(x, z) - 2)
                    continue;
                w->b[world_index(x, y, z)] = y < 6 ? B_WATER : B_AIR; /* the deepest caves are flooded */
            }
}

static void caves(World *w)
{
    for (int i = 0; i < 42; i++)
    {
        float x = (float)rng_range(&s_rng, 12, WORLD_X - 12), z = (float)rng_range(&s_rng, 12, WORLD_Z - 12);
        int h = H((int)x, (int)z);
        if (h < 12)
            continue;
        float y = (float)rng_range(&s_rng, 6, h - 4);
        bool entrance = i % 5 == 0;
        if (entrance)
            y = (float)h - 1.0f;
        float yaw = rng_float(&s_rng) * 6.2832f, pitch = entrance ? -0.5f : (rng_float(&s_rng) - 0.5f) * 0.5f;
        float r0 = 1.3f + rng_float(&s_rng) * 1.3f;
        int len = rng_range(&s_rng, 70, 150);
        for (int s = 0; s < len; s++)
        {
            float r = r0 * (0.7f + 0.5f * sinf((float)s * 3.1416f / (float)len)) + rng_float(&s_rng) * 0.3f;
            carve(w, x, y, z, r, entrance && s < 12);
            x += cosf(yaw) * cosf(pitch);
            z += sinf(yaw) * cosf(pitch);
            y += sinf(pitch);
            yaw += (rng_float(&s_rng) - 0.5f) * 0.4f;
            pitch = pitch * 0.9f + (rng_float(&s_rng) - 0.5f) * 0.18f;
            if (y < 4)
                pitch = 0.2f;
            if (x < 4 || z < 4 || x > WORLD_X - 4 || z > WORLD_Z - 4)
                break;
        }
    }
}

static void ores(World *w, uint8_t ore, int veins, int size_lo, int size_hi, int ymax)
{
    for (int i = 0; i < veins; i++)
    {
        int x = rng_range(&s_rng, 1, WORLD_X - 2), z = rng_range(&s_rng, 1, WORLD_Z - 2);
        int y = rng_range(&s_rng, 3, ymax);
        int n = rng_range(&s_rng, size_lo, size_hi);
        for (int k = 0; k < n; k++)
        {
            if (get(w, x, y, z) == B_STONE)
                put(w, x, y, z, ore);
            switch (rng_next(&s_rng) % 6)
            {
            case 0: x++; break;
            case 1: x--; break;
            case 2: y++; break;
            case 3: y--; break;
            case 4: z++; break;
            default: z--; break;
            }
        }
    }
}

/* ---- 4. structures ----------------------------------------------------------------------------- */

struct Style
{
    uint8_t wall, pillar, roof, floor, base, ground;
    bool flat_roof;
};

static const Style STYLE_PLAINS = {B_PLANKS, B_LOG, B_ROOF, B_PLANKS, B_COBBLE, B_GRASS, false};
static const Style STYLE_DESERT = {B_SANDSTONE, B_SANDSTONE, B_SANDSTONE, B_SAND, B_SANDSTONE, B_SAND, true};
static const Style STYLE_SNOW = {B_PLANKS, B_SPRUCE_LOG, B_ROOF, B_PLANKS, B_STONEBRICK, B_SNOW, false};

static const Style *style_of(Biome b)
{
    return b == BIOME_DESERT ? &STYLE_DESERT : b == BIOME_SNOW ? &STYLE_SNOW : &STYLE_PLAINS;
}

static bool area_free(int x0, int z0, int x1, int z1)
{
    if (x0 < 3 || z0 < 3 || x1 > WORLD_X - 3 || z1 > WORLD_Z - 3)
        return false;
    for (int i = 0; i < s_nused; i++)
    {
        const Rect *r = &s_used[i];
        if (x0 < r->x1 + 2 && x1 + 2 > r->x0 && z0 < r->z1 + 2 && z1 + 2 > r->z0)
            return false;
    }
    return true;
}

static void area_take(int x0, int z0, int x1, int z1)
{
    if (s_nused < (int)(sizeof(s_used) / sizeof(s_used[0])))
        s_used[s_nused++] = {x0, z0, x1, z1};
}

/* spread of the ground heights over a rectangle; its average in *mean */
static int roughness(int x0, int z0, int x1, int z1, int *mean)
{
    int lo = 1000, hi = -1000, sum = 0, n = 0;
    for (int z = z0; z < z1; z++)
        for (int x = x0; x < x1; x++)
        {
            int h = H(x, z);
            lo = h < lo ? h : lo;
            hi = h > hi ? h : hi;
            sum += h;
            n++;
        }
    *mean = n ? (sum + n / 2) / n : 0;
    return hi - lo;
}

/* Level the ground of a rectangle at height g (the surface block at g - 1) */
static void level(World *w, int x0, int z0, int x1, int z1, int g, uint8_t surface)
{
    for (int z = z0; z < z1; z++)
        for (int x = x0; x < x1; x++)
        {
            for (int y = g; y < WORLD_Y; y++)
                put(w, x, y, z, B_AIR);
            for (int y = H(x, z); y < g - 1; y++)
                if (y > 0)
                    put(w, x, y, z, B_DIRT);
            put(w, x, g - 1, z, surface);
            s_h[z * WORLD_X + x] = (int16_t)g;
        }
}

/* The surface under a column: first cell above the highest solid block */
static int ground(const World *w, int x, int z)
{
    int y = WORLD_Y - 1;
    while (y > 0 && !block_solid(get(w, x, y, z)))
        y--;
    return y + 1;
}

/* door side: 0 -x, 1 +x, 2 -z, 3 +z */
static void house(World *w, int x0, int z0, int wx, int wz, int g, int door, const Style *st, bool shelves)
{
    const int wall_h = 4;
    level(w, x0 - 1, z0 - 1, x0 + wx + 1, z0 + wz + 1, g, st->ground);
    for (int z = z0; z < z0 + wz; z++)
        for (int x = x0; x < x0 + wx; x++)
        {
            bool edge_x = x == x0 || x == x0 + wx - 1, edge_z = z == z0 || z == z0 + wz - 1;
            put(w, x, g - 1, z, edge_x || edge_z ? st->base : st->floor);
            for (int y = g - 2; y > 0 && !block_solid(get(w, x, y, z)); y--)
                put(w, x, y, z, st->base); /* foundation down to the ground */
            for (int y = g; y < g + wall_h; y++)
            {
                uint8_t b = B_AIR;
                if (edge_x && edge_z)
                    b = st->pillar;
                else if (edge_x || edge_z)
                    b = st->wall;
                put(w, x, y, z, b);
            }
        }
    /* windows in the middle of each wall, doors */
    for (int x = x0 + 2; x < x0 + wx - 2; x += 2)
    {
        put(w, x, g + 1, z0, B_GLASS);
        put(w, x, g + 1, z0 + wz - 1, B_GLASS);
    }
    for (int z = z0 + 2; z < z0 + wz - 2; z += 2)
    {
        put(w, x0, g + 1, z, B_GLASS);
        put(w, x0 + wx - 1, g + 1, z, B_GLASS);
    }
    int dx = door == 0 ? x0 : door == 1 ? x0 + wx - 1 : x0 + wx / 2;
    int dz = door == 2 ? z0 : door == 3 ? z0 + wz - 1 : z0 + wz / 2;
    put(w, dx, g, dz, B_AIR);
    put(w, dx, g + 1, dz, B_AIR);
    if (shelves)
        for (int x = x0 + 1; x < x0 + wx - 1; x++)
        {
            int z = (door == 2) ? z0 + wz - 2 : z0 + 1;
            if (x != dx)
                for (int y = g; y < g + 2; y++)
                    put(w, x, y, z, B_BOOKSHELF);
        }
    /* roof */
    int top = g + wall_h;
    if (st->flat_roof)
    {
        for (int z = z0; z < z0 + wz; z++)
            for (int x = x0; x < x0 + wx; x++)
            {
                put(w, x, top, z, st->roof);
                bool edge = x == x0 || z == z0 || x == x0 + wx - 1 || z == z0 + wz - 1;
                if (edge && ((x + z) & 1))
                    put(w, x, top + 1, z, st->roof);
            }
        return;
    }
    bool along_z = wz >= wx; /* the ridge runs along the longer side */
    int span = along_z ? wx : wz, len = along_z ? wz : wx;
    for (int k = 0;; k++)
    {
        int a = k - 1, b = span - k; /* the two slopes, relative to x0 / z0 */
        if (a > b)
            break;
        for (int l = -1; l <= len; l++)
        {
            for (int s = a; s <= b; s++)
            {
                int x = along_z ? x0 + s : x0 + l, z = along_z ? z0 + l : z0 + s;
                /* two blocks per step, so that the steps overlap and leave no gaps */
                bool slope = s <= a + 1 || s >= b - 1;
                bool gable = (l == 0 || l == len - 1) && !slope;
                if (slope)
                    put(w, x, top + k, z, st->roof);
                else if (gable)
                    put(w, x, top + k, z, st->wall);
            }
        }
    }
}

static void tower(World *w, int x0, int z0, int g, int height, int door, bool lantern)
{
    const int n = 5;
    level(w, x0 - 1, z0 - 1, x0 + n + 1, z0 + n + 1, g, get(w, x0 - 1, g - 1, z0 - 1) == B_SAND ? B_SAND : B_GRAVEL);
    for (int z = z0; z < z0 + n; z++)
        for (int x = x0; x < x0 + n; x++)
        {
            bool edge = x == x0 || z == z0 || x == x0 + n - 1 || z == z0 + n - 1;
            for (int y = g - 1; y > 0 && (y == g - 1 || !block_solid(get(w, x, y, z))); y--)
                put(w, x, y, z, B_COBBLE);
            for (int y = g; y < g + height; y++)
            {
                uint8_t b = edge ? B_STONEBRICK : B_AIR;
                if (edge && (y - g) % 4 == 2 && ((x == x0 + 2) || (z == z0 + 2)))
                    b = B_GLASS; /* window slits */
                put(w, x, y, z, b);
            }
            put(w, x, g + height, z, B_STONEBRICK);
            if (edge && ((x + z) & 1) == 0)
                put(w, x, g + height + 1, z, B_STONEBRICK); /* battlements */
        }
    int dx = door == 0 ? x0 : door == 1 ? x0 + n - 1 : x0 + 2;
    int dz = door == 2 ? z0 : door == 3 ? z0 + n - 1 : z0 + 2;
    put(w, dx, g, dz, B_AIR);
    put(w, dx, g + 1, dz, B_AIR);
    if (lantern)
    {
        int y = g + height + 1;
        for (int z = z0 + 1; z < z0 + 4; z++)
            for (int x = x0 + 1; x < x0 + 4; x++)
            {
                put(w, x, y, z, B_GLASS);
                put(w, x, y + 1, z, B_GLASS);
                put(w, x, y + 2, z, B_ROOF);
            }
        put(w, x0 + 2, y, z0 + 2, B_GOLD_ORE); /* the light */
        put(w, x0 + 2, y + 3, z0 + 2, B_ROOF);
    }
}

static void well(World *w, int x0, int z0, int g, const Style *st)
{
    level(w, x0 - 1, z0 - 1, x0 + 5, z0 + 5, g, B_GRAVEL);
    for (int z = z0; z < z0 + 4; z++)
        for (int x = x0; x < x0 + 4; x++)
        {
            bool inner = x > x0 && x < x0 + 3 && z > z0 && z < z0 + 3;
            for (int y = g - 4; y < g; y++)
                put(w, x, y, z, inner ? B_WATER : B_COBBLE);
            put(w, x, g, z, inner ? B_AIR : B_COBBLE);
            bool corner = (x == x0 || x == x0 + 3) && (z == z0 || z == z0 + 3);
            if (corner)
            {
                put(w, x, g + 1, z, st->pillar);
                put(w, x, g + 2, z, st->pillar);
            }
            put(w, x, g + 3, z, st->flat_roof ? st->roof : B_PLANKS);
        }
}

static void path(World *w, int xa, int za, int xb, int zb)
{
    int x = xa, z = za;
    while (x != xb || z != zb)
    {
        if (x != xb)
            x += x < xb ? 1 : -1;
        else
            z += z < zb ? 1 : -1;
        for (int k = 0; k < 2; k++) /* two blocks wide */
        {
            int px = x + (x == xb ? k : 0), pz = z + (x == xb ? 0 : k);
            int y = ground(w, px, pz) - 1;
            uint8_t b = get(w, px, y, pz);
            if (b == B_GRASS || b == B_DIRT || b == B_SNOW || b == B_SAND)
                put(w, px, y, pz, b == B_SAND ? B_SANDSTONE : B_GRAVEL);
        }
    }
}

static void pyramid(World *w, int x0, int z0, int g)
{
    const int n = 15;
    level(w, x0 - 1, z0 - 1, x0 + n + 1, z0 + n + 1, g, B_SAND);
    for (int k = 0; k <= n / 2; k++)
        for (int z = z0 + k; z < z0 + n - k; z++)
            for (int x = x0 + k; x < x0 + n - k; x++)
                put(w, x, g + k, z, B_SANDSTONE);
    int c = n / 2;
    for (int y = g; y < g + 3; y++) /* the chamber and the way in */
        for (int z = z0 + c - 1; z <= z0 + c + 1; z++)
            for (int x = x0 + c - 1; x <= x0 + c + 1; x++)
                put(w, x, y, z, B_AIR);
    for (int z = z0; z < z0 + c; z++)
    {
        put(w, x0 + c, g, z, B_AIR);
        put(w, x0 + c, g + 1, z, B_AIR);
    }
    put(w, x0 + c, g - 1, z0 + c, B_GOLD_ORE);
    put(w, x0 + c - 1, g - 1, z0 + c + 1, B_GOLD_ORE);
    put(w, x0 + c + 1, g - 1, z0 + c - 1, B_GOLD_ORE);
}

/* Finds a place of w x d columns on land (flat enough) near (cx, cz); true and its corner */
static bool find_site(int cx, int cz, int radius, int wx, int wz, int max_rough, int min_h, int max_h, int *ox, int *oz,
                      int *g)
{
    int best = 1000;
    for (int tries = 0; tries < 80; tries++)
    {
        int x = cx + rng_range(&s_rng, -radius, radius) - wx / 2, z = cz + rng_range(&s_rng, -radius, radius) - wz / 2;
        if (!area_free(x - 1, z - 1, x + wx + 1, z + wz + 1))
            continue;
        int mean, r = roughness(x - 1, z - 1, x + wx + 1, z + wz + 1, &mean);
        if (r > max_rough || mean < min_h || mean > max_h)
            continue;
        bool wet = false;
        for (int zz = z - 1; zz <= z + wz && !wet; zz++)
            for (int xx = x - 1; xx <= x + wx && !wet; xx++)
                wet = H(xx, zz) <= SEA_LEVEL;
        if (wet && min_h > SEA_LEVEL)
            continue;
        if (r < best)
        {
            best = r;
            *ox = x;
            *oz = z;
            *g = mean < SEA_LEVEL + 1 ? SEA_LEVEL + 1 : mean;
        }
        if (r <= 1)
            break;
    }
    return best < 1000;
}

static int door_towards(int x0, int z0, int wx, int wz, int tx, int tz)
{
    int dx = tx - (x0 + wx / 2), dz = tz - (z0 + wz / 2);
    if (abs(dx) > abs(dz))
        return dx < 0 ? 0 : 1;
    return dz < 0 ? 2 : 3;
}

static void door_cell(int x0, int z0, int wx, int wz, int door, int *x, int *z)
{
    *x = door == 0 ? x0 - 1 : door == 1 ? x0 + wx : x0 + wx / 2;
    *z = door == 2 ? z0 - 1 : door == 3 ? z0 + wz : z0 + wz / 2;
}

static void village(World *w)
{
    int ox = WORLD_X / 2, oz = WORLD_Z / 2, g = SEA_LEVEL + 4;
    if (!find_site(WORLD_X / 2, WORLD_Z / 2, 40, 4, 4, 2, SEA_LEVEL + 2, 44, &ox, &oz, &g))
        find_site(WORLD_X / 2, WORLD_Z / 2, 70, 4, 4, 5, SEA_LEVEL + 1, 50, &ox, &oz, &g);
    const Style *st = style_of(biome(ox, oz));
    area_take(ox - 1, oz - 1, ox + 5, oz + 5);
    well(w, ox, oz, g, st);
    int wcx = ox + 2, wcz = oz + 2;
    w->spawn_x = (float)wcx + 0.5f;
    w->spawn_z = (float)oz + 10.5f; /* south of the well, looking at it */
    w->spawn_y = (float)g + 1.0f;

    int built = 0;
    for (int i = 0; i < 14 && built < 7; i++)
    {
        bool is_tower = i == 3;
        int wx = is_tower ? 5 : rng_range(&s_rng, 5, 8), wz = is_tower ? 5 : rng_range(&s_rng, 5, 8);
        float a = (float)i * 2.4f + rng_float(&s_rng);
        int d = 10 + built * 2 + rng_range(&s_rng, 0, 4);
        int cx = wcx + (int)(cosf(a) * (float)d), cz = wcz + (int)(sinf(a) * (float)d);
        int x0 = 0, z0 = 0, hg = 0;
        if (!find_site(cx, cz, 3, wx, wz, 3, SEA_LEVEL + 1, 50, &x0, &z0, &hg))
            continue;
        int door = door_towards(x0, z0, wx, wz, wcx, wcz);
        area_take(x0 - 1, z0 - 1, x0 + wx + 1, z0 + wz + 1);
        if (is_tower)
            tower(w, x0, z0, hg, 10, door, false);
        else
            house(w, x0, z0, wx, wz, hg, door, st, i % 4 == 1);
        int px, pz;
        door_cell(x0, z0, wx, wz, door, &px, &pz);
        path(w, px, pz, wcx, wcz);
        built++;
    }
}

static void landmarks(World *w)
{
    int x0 = 0, z0 = 0, g = 0;
    /* towers on the hills */
    for (int i = 0; i < 3; i++)
        if (find_site(rng_range(&s_rng, 30, WORLD_X - 30), rng_range(&s_rng, 30, WORLD_Z - 30), 30, 5, 5, 4, 34, 48, &x0,
                      &z0, &g))
        {
            area_take(x0 - 1, z0 - 1, x0 + 6, z0 + 6);
            tower(w, x0, z0, g, rng_range(&s_rng, 8, 12), rng_range(&s_rng, 0, 3), false);
        }
    /* a lighthouse on the coast */
    for (int i = 0; i < 6; i++)
    {
        float a = rng_float(&s_rng) * 6.2832f;
        int cx = WORLD_X / 2 + (int)(cosf(a) * WORLD_X * 0.36f), cz = WORLD_Z / 2 + (int)(sinf(a) * WORLD_Z * 0.36f);
        if (find_site(cx, cz, 14, 5, 5, 3, SEA_LEVEL + 1, SEA_LEVEL + 5, &x0, &z0, &g))
        {
            area_take(x0 - 1, z0 - 1, x0 + 6, z0 + 6);
            tower(w, x0, z0, g, 13, door_towards(x0, z0, 5, 5, WORLD_X / 2, WORLD_Z / 2), true);
            break;
        }
    }
    /* a pyramid in the desert */
    for (int i = 0; i < 40; i++)
    {
        int cx = rng_range(&s_rng, 24, WORLD_X - 24), cz = rng_range(&s_rng, 24, WORLD_Z - 24);
        if (biome(cx, cz) != BIOME_DESERT)
            continue;
        if (find_site(cx, cz, 6, 15, 15, 5, SEA_LEVEL + 2, 40, &x0, &z0, &g))
        {
            area_take(x0 - 1, z0 - 1, x0 + 16, z0 + 16);
            pyramid(w, x0, z0, g);
            break;
        }
    }
}

/* ---- 5. plants ------------------------------------------------------------------------------------ */

static bool clear_box(const World *w, int x0, int y0, int z0, int x1, int y1, int z1)
{
    for (int y = y0; y <= y1; y++)
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++)
                if (!world_inside(x, y, z) || get(w, x, y, z) != B_AIR)
                    return false;
    return true;
}

static void leaf(World *w, int x, int y, int z, uint8_t b)
{
    if (world_inside(x, y, z) && get(w, x, y, z) == B_AIR)
        put(w, x, y, z, b);
}

static void oak(World *w, int x, int y, int z)
{
    int h = rng_range(&s_rng, 4, 6);
    if (!clear_box(w, x - 2, y, z - 2, x + 2, y + h + 1, z + 2))
        return;
    for (int k = 0; k < h; k++)
        put(w, x, y + k, z, B_LOG);
    int t = y + h;
    for (int dy = -2; dy <= 1; dy++)
    {
        int r = dy < 0 ? 2 : 1;
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++)
            {
                bool corner = abs(dx) == r && abs(dz) == r;
                if (corner && (dy == 1 || (rng_next(&s_rng) & 1)))
                    continue;
                leaf(w, x + dx, t + dy, z + dz, B_LEAVES);
            }
    }
}

static void spruce(World *w, int x, int y, int z)
{
    int h = rng_range(&s_rng, 6, 9);
    if (!clear_box(w, x - 2, y, z - 2, x + 2, y + h + 1, z + 2))
        return;
    for (int k = 0; k < h; k++)
        put(w, x, y + k, z, B_SPRUCE_LOG);
    int t = y + h;
    leaf(w, x, t, z, B_SPRUCE_LEAVES);
    leaf(w, x, t + 1, z, B_SPRUCE_LEAVES);
    for (int k = 0; k < h - 2; k++)
    {
        int r = (k % 2 == 0) ? 1 : (k < 3 ? 1 : 2);
        int yy = t - 1 - k;
        for (int dz = -r; dz <= r; dz++)
            for (int dx = -r; dx <= r; dx++)
                if (!(abs(dx) == r && abs(dz) == r && r > 1))
                    leaf(w, x + dx, yy, z + dz, B_SPRUCE_LEAVES);
    }
}

static void plants(World *w)
{
    for (int z = 3; z < WORLD_Z - 3; z++)
        for (int x = 3; x < WORLD_X - 3; x++)
        {
            int y = ground(w, x, z);
            uint8_t below = get(w, x, y - 1, z);
            if (y >= WORLD_Y - 12 || get(w, x, y, z) != B_AIR)
                continue;
            bool taken = false;
            for (int i = 0; i < s_nused && !taken; i++)
                taken = x >= s_used[i].x0 - 1 && x < s_used[i].x1 + 1 && z >= s_used[i].z0 - 1 && z < s_used[i].z1 + 1;
            if (taken)
                continue;
            float f = fbm2(&s_forest, (float)x / 45.0f, (float)z / 45.0f, 2);
            float r = rng_float(&s_rng);
            if (below == B_GRASS && r < 0.002f + 0.06f * smoothstep(0.0f, 0.35f, f))
                (y > 38 ? spruce : oak)(w, x, y, z);
            else if (below == B_SNOW && r < 0.004f + 0.03f * smoothstep(0.0f, 0.35f, f))
                spruce(w, x, y, z);
            else if (below == B_SAND && y > SEA_LEVEL + 2 && r < 0.004f && clear_box(w, x - 1, y, z - 1, x + 1, y + 3, z + 1))
                for (int k = rng_range(&s_rng, 1, 3); k > 0; k--)
                    put(w, x, y + k - 1, z, B_CACTUS);
        }
}

/* ---- all of it ------------------------------------------------------------------------------------ */

void world_generate(World *w, uint32_t seed, void (*progress)(int percent, const char *what))
{
    w->seed = seed;
    s_rng.s = seed | 1u;
    noise_init(&s_cont, seed);
    noise_init(&s_hill, seed * 31u + 7u);
    noise_init(&s_mount, seed * 131u + 3u);
    noise_init(&s_temp, seed * 17u + 11u);
    noise_init(&s_forest, seed * 7u + 5u);
    s_nused = 0;
    s_h = (int16_t *)malloc(sizeof(int16_t) * WORLD_X * WORLD_Z);
    if (!s_h)
        return;

    progress(5, "shaping the island");
    for (int z = 0; z < WORLD_Z; z++)
        for (int x = 0; x < WORLD_X; x++)
            s_h[z * WORLD_X + x] = (int16_t)height_at(x, z);
    progress(20, "laying rock and soil");
    for (int z = 0; z < WORLD_Z; z++)
        for (int x = 0; x < WORLD_X; x++)
            fill_column(w, x, z);
    progress(40, "digging caves");
    caves(w);
    ores(w, B_COAL_ORE, 170, 4, 9, 52);
    ores(w, B_IRON_ORE, 90, 3, 7, 36);
    ores(w, B_GOLD_ORE, 30, 3, 5, 20);
    progress(55, "building a village");
    village(w);
    landmarks(w);
    progress(70, "planting trees");
    plants(w);
    progress(85, "lighting");
    for (int z = 0; z < WORLD_Z; z++)
        for (int x = 0; x < WORLD_X; x++)
            world_column_top(w, x, z);
    /* stand on whatever is at the spawn point */
    int sx = (int)w->spawn_x, sz = (int)w->spawn_z;
    w->spawn_y = (float)ground(w, sx, sz);
    free(s_h);
    s_h = nullptr;
}
