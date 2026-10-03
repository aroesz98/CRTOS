/*
 * voxel_blocks.cpp - block table and the textures, painted by code (no image files): value
 * noise, cells and patterns in the style of the well-known block game, 16 x 16 texels that
 * tile without seams.
 */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "voxel_blocks.h"

#define S (BF_SOLID)
#define O (BF_OPAQUE)

const BlockInfo g_blocks[B_COUNT] = {
    {"air", 0, 0, 0, 0},
    {"grass", T_GRASS_TOP, T_GRASS_SIDE, T_DIRT, S | O},
    {"dirt", T_DIRT, T_DIRT, T_DIRT, S | O},
    {"stone", T_STONE, T_STONE, T_STONE, S | O},
    {"cobblestone", T_COBBLE, T_COBBLE, T_COBBLE, S | O},
    {"planks", T_PLANKS, T_PLANKS, T_PLANKS, S | O},
    {"log", T_LOG_TOP, T_LOG_SIDE, T_LOG_TOP, S | O},
    {"leaves", T_LEAVES, T_LEAVES, T_LEAVES, S | O},
    {"sand", T_SAND, T_SAND, T_SAND, S | O},
    {"sandstone", T_SANDSTONE_TOP, T_SANDSTONE_SIDE, T_SANDSTONE_TOP, S | O},
    {"water", T_WATER, T_WATER, T_WATER, BF_LIQUID},
    {"glass", T_GLASS, T_GLASS, T_GLASS, S | BF_CUTOUT},
    {"bricks", T_BRICK, T_BRICK, T_BRICK, S | O},
    {"stone bricks", T_STONEBRICK, T_STONEBRICK, T_STONEBRICK, S | O},
    {"gravel", T_GRAVEL, T_GRAVEL, T_GRAVEL, S | O},
    {"snowy grass", T_SNOW, T_SNOW_SIDE, T_DIRT, S | O},
    {"bedrock", T_BEDROCK, T_BEDROCK, T_BEDROCK, S | O | BF_UNBREAKABLE},
    {"coal ore", T_COAL_ORE, T_COAL_ORE, T_COAL_ORE, S | O},
    {"iron ore", T_IRON_ORE, T_IRON_ORE, T_IRON_ORE, S | O},
    {"gold ore", T_GOLD_ORE, T_GOLD_ORE, T_GOLD_ORE, S | O},
    {"cactus", T_CACTUS_TOP, T_CACTUS_SIDE, T_CACTUS_TOP, S | O},
    {"bookshelf", T_PLANKS, T_BOOKSHELF, T_PLANKS, S | O},
    {"roof tiles", T_ROOF, T_ROOF, T_ROOF, S | O},
    {"spruce log", T_LOG_TOP, T_SPRUCE_LOG, T_LOG_TOP, S | O},
    {"spruce leaves", T_SPRUCE_LEAVES, T_SPRUCE_LEAVES, T_SPRUCE_LEAVES, S | O},
};

#undef S
#undef O

const uint8_t g_buildable[] = {
    B_GRASS, B_DIRT,       B_STONE,  B_COBBLE, B_PLANKS,    B_LOG,       B_LEAVES,      B_GLASS,
    B_BRICK, B_STONEBRICK, B_ROOF,   B_SAND,   B_SANDSTONE, B_GRAVEL,    B_BOOKSHELF,   B_SNOW,
    B_WATER, B_CACTUS,     B_COAL_ORE, B_IRON_ORE, B_GOLD_ORE, B_SPRUCE_LOG, B_SPRUCE_LEAVES,
};
const int g_buildable_count = (int)(sizeof(g_buildable) / sizeof(g_buildable[0]));

uint32_t g_tex[T_COUNT][256];
uint16_t g_tex565[T_COUNT][256];

/* ---- painting helpers ------------------------------------------------------------------------ */

static uint32_t hash3(int x, int y, int s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* 0..1 per texel */
static float rnd(int x, int y, int s)
{
    return (float)(hash3(x, y, s) & 0xFFFFu) / 65535.0f;
}

static float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

/* value noise that tiles every 16 texels; c: cell size (divides 16) */
static float vnoise(int x, int y, int c, int s)
{
    int n = 16 / c;
    int x0 = x / c, y0 = y / c;
    float fx = smooth((float)(x % c) / (float)c), fy = smooth((float)(y % c) / (float)c);
    float a = rnd(x0 % n, y0 % n, s), b = rnd((x0 + 1) % n, y0 % n, s);
    float d = rnd(x0 % n, (y0 + 1) % n, s), e = rnd((x0 + 1) % n, (y0 + 1) % n, s);
    float top = a + (b - a) * fx, bottom = d + (e - d) * fx;
    return top + (bottom - top) * fy;
}

static uint32_t rgb(float r, float g, float b)
{
    int ir = (int)r, ig = (int)g, ib = (int)b;
    ir = ir < 0 ? 0 : ir > 255 ? 255 : ir;
    ig = ig < 0 ? 0 : ig > 255 ? 255 : ig;
    ib = ib < 0 ? 0 : ib > 255 ? 255 : ib;
    return 0xFF000000u | ((uint32_t)ir << 16) | ((uint32_t)ig << 8) | (uint32_t)ib;
}

/* base colour times a brightness factor */
static uint32_t tone(int r, int g, int b, float k)
{
    return rgb((float)r * k, (float)g * k, (float)b * k);
}

static void put(int t, int x, int y, uint32_t c)
{
    g_tex[t][(y & 15) * 16 + (x & 15)] = c;
}

/* ---- the textures ------------------------------------------------------------------------------ */

static void paint_dirt(int t, int salt)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float k = 0.78f + 0.3f * rnd(x, y, salt) + 0.12f * vnoise(x, y, 4, salt + 1);
            float s = rnd(x, y, salt + 2);
            if (s < 0.07f)
                k *= 0.7f;
            else if (s > 0.95f)
                k *= 1.18f;
            put(t, x, y, tone(126, 90, 62, k));
        }
}

static uint32_t grass_colour(int x, int y, int salt)
{
    float k = 0.8f + 0.32f * rnd(x, y, salt) + 0.12f * vnoise(x, y, 4, salt + 3);
    return tone(96, 158, 56, k);
}

static void paint_stone(int t, int salt)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float k = 0.74f + 0.24f * vnoise(x, y, 4, salt) + 0.14f * rnd(x, y, salt + 1);
            put(t, x, y, tone(128, 128, 128, k));
        }
}

/* cells around nine points (distances wrap around the tile) */
static void paint_cells(int t, int r, int g, int b, int salt, float border, float border_k)
{
    int px[9], py[9];
    float pk[9];
    for (int i = 0; i < 9; i++)
    {
        px[i] = (i % 3) * 5 + 2 + (int)(rnd(i, 0, salt) * 3.0f);
        py[i] = (i / 3) * 5 + 2 + (int)(rnd(i, 1, salt) * 3.0f);
        pk[i] = 0.72f + 0.34f * rnd(i, 2, salt);
    }
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float d1 = 1e9f, d2 = 1e9f;
            int best = 0;
            for (int i = 0; i < 9; i++)
            {
                int dx = abs(x - px[i]), dy = abs(y - py[i]);
                dx = dx > 8 ? 16 - dx : dx;
                dy = dy > 8 ? 16 - dy : dy;
                float d = sqrtf((float)(dx * dx + dy * dy));
                if (d < d1)
                {
                    d2 = d1;
                    d1 = d;
                    best = i;
                }
                else if (d < d2)
                {
                    d2 = d;
                }
            }
            float k = pk[best] * (0.9f + 0.18f * rnd(x, y, salt + 5));
            if (d2 - d1 < border)
                k *= border_k;
            put(t, x, y, tone(r, g, b, k));
        }
}

static void paint_planks(int t, int r, int g, int b, int salt)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            int band = y / 4;
            float k = 0.86f + 0.14f * rnd(band, 0, salt);
            k *= 0.92f + 0.12f * vnoise(x, y, 4, salt + band) + 0.06f * rnd(x, y, salt + 1);
            if (y % 4 == 3)
                k *= 0.62f; /* gap between the boards */
            if (x == (band * 5 + 3) % 16)
                k *= 0.7f;  /* board end */
            put(t, x, y, tone(r, g, b, k));
        }
}

static void paint_bark(int t, int r, int g, int b, int salt)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float k = 0.7f + 0.35f * vnoise(x, y / 4 * 4, 2, salt) + 0.1f * rnd(x, y, salt + 1);
            if (rnd(x, y / 3, salt + 2) < 0.18f)
                k *= 0.7f;
            put(t, x, y, tone(r, g, b, k));
        }
}

static void paint_leaves(int t, int r, int g, int b, int salt)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float k = 0.62f + 0.5f * rnd(x, y, salt) + 0.1f * vnoise(x, y, 4, salt + 1);
            if (rnd(x, y, salt + 2) < 0.14f)
                k *= 0.45f;
            put(t, x, y, tone(r, g, b, k));
        }
}

static void paint_ore(int t, int r, int g, int b, int salt)
{
    paint_stone(t, 11);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            if (vnoise(x, y, 2, salt) > 0.7f && rnd(x, y, salt + 1) > 0.2f)
                put(t, x, y, tone(r, g, b, 0.8f + 0.35f * rnd(x, y, salt + 2)));
}

static void paint_sand(int t, int r, int g, int b, int salt)
{
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            put(t, x, y, tone(r, g, b, 0.9f + 0.1f * rnd(x, y, salt) + 0.05f * vnoise(x, y, 4, salt + 1)));
}

void blocks_init(void)
{
    /* grass, dirt, snow */
    paint_dirt(T_DIRT, 100);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            put(T_GRASS_TOP, x, y, grass_colour(x, y, 200));
    memcpy(g_tex[T_GRASS_SIDE], g_tex[T_DIRT], sizeof(g_tex[0]));
    memcpy(g_tex[T_SNOW_SIDE], g_tex[T_DIRT], sizeof(g_tex[0]));
    for (int x = 0; x < 16; x++)
    {
        int d = 3 + (rnd(x, 0, 210) > 0.5f) + (rnd(x, 1, 210) > 0.8f ? 1 : 0);
        for (int y = 0; y < d; y++)
            put(T_GRASS_SIDE, x, y, grass_colour(x, y, 200));
        int s = 3 + (rnd(x, 2, 211) > 0.6f);
        for (int y = 0; y < s; y++)
            put(T_SNOW_SIDE, x, y, tone(242, 246, 250, 0.93f + 0.07f * rnd(x, y, 212)));
    }
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            put(T_SNOW, x, y, tone(242, 246, 250, 0.92f + 0.08f * rnd(x, y, 213)));

    /* stone and friends */
    paint_stone(T_STONE, 11);
    paint_cells(T_COBBLE, 122, 122, 122, 300, 1.1f, 0.55f);
    paint_cells(T_GRAVEL, 128, 118, 112, 310, 0.7f, 0.7f);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
            put(T_BEDROCK, x, y, tone(90, 90, 90, 0.35f + 0.9f * vnoise(x, y, 2, 320) * rnd(x, y, 321)));
    paint_ore(T_COAL_ORE, 34, 34, 34, 330);
    paint_ore(T_IRON_ORE, 214, 172, 142, 340);
    paint_ore(T_GOLD_ORE, 250, 232, 80, 350);

    /* stone bricks: two rows, the joint of the lower one in the middle */
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            int row = y / 8, bx = row ? (x + 8) % 16 : x, by = y % 8;
            float k = (0.8f + 0.2f * rnd(row, 0, 360)) * (0.9f + 0.14f * rnd(x, y, 361));
            if (by == 7 || bx == 15)
                k = 0.52f;          /* mortar */
            else if (by == 0 || bx == 0)
                k *= 1.12f;         /* light edge */
            else if (by == 6 || bx == 14)
                k *= 0.85f;         /* shadow edge */
            put(T_STONEBRICK, x, y, tone(126, 126, 126, k));
        }

    /* bricks */
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            int row = y / 4;
            int bx = (row & 1) ? (x + 4) % 16 : x;
            uint32_t c;
            if (y % 4 == 3 || bx % 8 == 7)
                c = tone(190, 180, 170, 0.9f + 0.1f * rnd(x, y, 370));
            else
                c = tone(152, 72, 56, (0.82f + 0.2f * rnd(row * 2 + bx / 8, 0, 371)) * (0.92f + 0.12f * rnd(x, y, 372)));
            put(T_BRICK, x, y, c);
        }

    /* roof tiles: rows of overlapping tiles, lighter at their top */
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            int row = y / 4, by = y % 4;
            int bx = (row & 1) ? (x + 2) % 16 : x;
            float k = 1.1f - 0.13f * (float)by;
            if (bx % 4 == 3)
                k *= 0.7f;
            k *= 0.9f + 0.14f * rnd(x, y, 380);
            put(T_ROOF, x, y, tone(142, 64, 48, k));
        }

    /* wood */
    paint_planks(T_PLANKS, 164, 132, 80, 400);
    paint_bark(T_LOG_SIDE, 104, 82, 52, 410);
    paint_bark(T_SPRUCE_LOG, 70, 52, 34, 415);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float dx = (float)x - 7.5f, dy = (float)y - 7.5f, d = sqrtf(dx * dx + dy * dy);
            uint32_t c;
            if (d > 6.6f)
                c = tone(104, 82, 52, 0.8f + 0.2f * rnd(x, y, 420));
            else
                c = tone(176, 142, 90, (fmodf(d, 2.4f) < 0.9f ? 0.78f : 1.0f) * (0.94f + 0.08f * rnd(x, y, 421)));
            put(T_LOG_TOP, x, y, c);
        }

    /* bookshelf: planks at the top, the middle and the bottom, books between */
    static const int book[][3] = {{150, 40, 36}, {48, 70, 150}, {50, 120, 60}, {120, 90, 50}, {190, 170, 70}, {110, 50, 120}};
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            uint32_t c;
            if (y < 2 || y > 13 || y == 7 || y == 8 || x == 0 || x == 15)
            {
                c = tone(164, 132, 80, 0.85f + 0.15f * rnd(x, y, 430));
            }
            else
            {
                int shelf = y < 7 ? 0 : 1;
                int b = (int)(rnd(x / 2, shelf, 431) * 6.0f) % 6;
                int top = (shelf ? 9 : 2) + (int)(rnd(x / 2, shelf, 432) * 2.0f);
                if (y < top)
                    c = tone(60, 44, 30, 1.0f);
                else
                    c = tone(book[b][0], book[b][1], book[b][2], (x % 2 ? 0.8f : 1.0f) * (0.9f + 0.1f * rnd(x, y, 433)));
            }
            put(T_BOOKSHELF, x, y, c);
        }

    /* plants */
    paint_leaves(T_LEAVES, 60, 124, 40, 500);
    paint_leaves(T_SPRUCE_LEAVES, 44, 92, 58, 510);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float k = (x % 4 == 0 ? 0.72f : 1.0f) * (0.9f + 0.12f * rnd(x, y, 520));
            uint32_t c = tone(84, 138, 46, k);
            if (x % 4 == 2 && rnd(x, y, 521) < 0.12f)
                c = tone(226, 222, 180, 1.0f); /* spines */
            put(T_CACTUS_SIDE, x, y, c);
            float dx = (float)x - 7.5f, dy = (float)y - 7.5f;
            float d = sqrtf(dx * dx + dy * dy);
            put(T_CACTUS_TOP, x, y, tone(96, 154, 54, (d > 6.0f ? 0.75f : 1.0f) * (0.9f + 0.12f * rnd(x, y, 522))));
        }

    /* sand */
    paint_sand(T_SAND, 220, 207, 160, 600);
    paint_sand(T_SANDSTONE_TOP, 216, 203, 156, 610);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float k = 0.92f + 0.08f * rnd(x, y, 620);
            if (y < 3)
                k *= 1.06f;
            if (y == 3 || y == 11)
                k *= 0.82f;
            put(T_SANDSTONE_SIDE, x, y, tone(214, 200, 152, k));
        }

    /* water: blue with brighter wave crests */
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            float k = 0.86f + 0.2f * vnoise(x, y, 4, 700);
            if ((x + y * 3) % 11 == 0 && rnd(x, y, 701) < 0.6f)
                k *= 1.25f;
            put(T_WATER, x, y, tone(50, 96, 206, k));
        }

    /* glass: a frame, a few highlights, the rest is a hole */
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++)
        {
            uint32_t c = 0;
            if (x == 0 || y == 0 || x == 15 || y == 15)
                c = tone(214, 232, 238, 0.9f + 0.1f * rnd(x, y, 710));
            else if ((x + y == 7 || x + y == 8) && x > 1 && x < 7)
                c = tone(236, 246, 250, 1.0f);
            else if (x + y == 21 && x > 9)
                c = tone(236, 246, 250, 1.0f);
            put(T_GLASS, x, y, c);
        }

    for (int t = 0; t < T_COUNT; t++)
        for (int i = 0; i < 256; i++)
        {
            uint32_t c = g_tex[t][i];
            uint16_t p = (uint16_t)(((c >> 8) & 0xF800u) | ((c >> 5) & 0x07E0u) | ((c >> 3) & 0x001Fu));
            if (p == TEX_HOLE)
                p ^= 0x0020u; /* keep the key free */
            g_tex565[t][i] = (c >> 24) ? p : (uint16_t)TEX_HOLE;
        }
}

/* ---- icons --------------------------------------------------------------------------------------- */

static uint32_t shade(uint32_t c, int k) /* k: 0..256 */
{
    uint32_t rb = ((c & 0xFF00FFu) * (uint32_t)k >> 8) & 0xFF00FFu;
    uint32_t g = ((c & 0x00FF00u) * (uint32_t)k >> 8) & 0x00FF00u;
    return 0xFF000000u | rb | g;
}

void block_icon(uint32_t *pix, int stride, int size, uint8_t block)
{
    const BlockInfo *bi = &g_blocks[block];
    float s = (float)size;
    for (int py = 0; py < size; py++)
        for (int px = 0; px < size; px++)
        {
            float x = (float)px + 0.5f, y = (float)py + 0.5f;
            int tex = -1, k = 256;
            float u = 0, v = 0;
            /* top face: from the back corner (s/2, 0) along (s/2, s/4) and (-s/2, s/4) */
            float ax = s * 0.5f, ay = s * 0.25f, bx = -s * 0.5f, by = s * 0.25f;
            float det = ax * by - ay * bx;
            float rx = x - s * 0.5f, ry = y;
            u = (rx * by - ry * bx) / det;
            v = (ax * ry - ay * rx) / det;
            if (u >= 0 && u < 1 && v >= 0 && v < 1)
            {
                tex = bi->top;
            }
            else
            {
                /* left face: from (0, s/4) along (s/2, s/4) and down */
                rx = x;
                ry = y - s * 0.25f;
                u = rx / (s * 0.5f);
                v = (ry - u * s * 0.25f) / (s * 0.5f);
                if (u >= 0 && u < 1 && v >= 0 && v < 1)
                {
                    tex = bi->side;
                    k = 204;
                }
                else
                {
                    /* right face: from (s/2, s/2) along (s/2, -s/4) and down */
                    rx = x - s * 0.5f;
                    ry = y - s * 0.5f;
                    u = rx / (s * 0.5f);
                    v = (ry + u * s * 0.25f) / (s * 0.5f);
                    if (u >= 0 && u < 1 && v >= 0 && v < 1)
                    {
                        tex = bi->side;
                        k = 160;
                    }
                }
            }
            if (tex < 0)
                continue;
            uint32_t c = g_tex[tex][((int)(v * 16.0f) & 15) * 16 + ((int)(u * 16.0f) & 15)];
            if (!(c >> 24))
                continue;
            pix[py * stride + px] = shade(c, k);
        }
}
