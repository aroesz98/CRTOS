/*
 * voxel_world.h - the blocks of the island and the generator that makes it.
 *
 * Blocks are one byte each, stored y-major (a horizontal layer after another). top[] keeps,
 * per column, the height of the first cell above the highest opaque block: cells at or above
 * it see the sky, the ones below are in shadow.
 */
#ifndef VOXEL_WORLD_H
#define VOXEL_WORLD_H

#include <stdint.h>

#include "voxel_blocks.h"
#include "voxel_config.h"

struct World
{
    uint8_t *b;   /* WORLD_Y * WORLD_Z * WORLD_X */
    uint8_t *top; /* WORLD_Z * WORLD_X */
    uint32_t seed;
    float spawn_x, spawn_y, spawn_z; /* feet of the player at the start */
};

static inline bool world_inside(int x, int y, int z)
{
    return (unsigned)x < WORLD_X && (unsigned)y < WORLD_Y && (unsigned)z < WORLD_Z;
}

static inline int world_index(int x, int y, int z)
{
    return (y * WORLD_Z + z) * WORLD_X + x;
}

/* Outside the island: bedrock below, the endless ocean around, air above */
static inline uint8_t world_get(const World *w, int x, int y, int z)
{
    if (y < 0)
        return B_BEDROCK;
    if (y >= WORLD_Y)
        return B_AIR;
    if ((unsigned)x >= WORLD_X || (unsigned)z >= WORLD_Z)
        return y < SEA_LEVEL ? B_WATER : B_AIR;
    return w->b[world_index(x, y, z)];
}

/* 1 when the cell sees the sky */
static inline bool world_sky(const World *w, int x, int y, int z)
{
    if ((unsigned)x >= WORLD_X || (unsigned)z >= WORLD_Z)
        return true;
    return y >= w->top[z * WORLD_X + x];
}

bool world_alloc(World *w);
void world_free(World *w);

/* Changes one block; returns true when the sky height of its column changed */
bool world_set(World *w, int x, int y, int z, uint8_t block);

/* Recomputes top[] of a column */
void world_column_top(World *w, int x, int z);

/* A new island from @seed; @progress (0..100) is called now and then */
void world_generate(World *w, uint32_t seed, void (*progress)(int percent, const char *what));

#endif
