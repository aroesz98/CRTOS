/*
 * voxel_blocks.h - block types, their textures (16 x 16 texels, made at start-up by code)
 * and the icons of the hotbar.
 */
#ifndef VOXEL_BLOCKS_H
#define VOXEL_BLOCKS_H

#include <stdint.h>

enum Block : uint8_t
{
    B_AIR,
    B_GRASS,
    B_DIRT,
    B_STONE,
    B_COBBLE,
    B_PLANKS,
    B_LOG,
    B_LEAVES,
    B_SAND,
    B_SANDSTONE,
    B_WATER,
    B_GLASS,
    B_BRICK,
    B_STONEBRICK,
    B_GRAVEL,
    B_SNOW,
    B_BEDROCK,
    B_COAL_ORE,
    B_IRON_ORE,
    B_GOLD_ORE,
    B_CACTUS,
    B_BOOKSHELF,
    B_ROOF,
    B_SPRUCE_LOG,
    B_SPRUCE_LEAVES,
    B_COUNT
};

enum Texture : uint8_t
{
    T_GRASS_TOP,
    T_GRASS_SIDE,
    T_DIRT,
    T_STONE,
    T_COBBLE,
    T_PLANKS,
    T_LOG_SIDE,
    T_LOG_TOP,
    T_LEAVES,
    T_SAND,
    T_SANDSTONE_SIDE,
    T_SANDSTONE_TOP,
    T_WATER,
    T_GLASS,
    T_BRICK,
    T_STONEBRICK,
    T_GRAVEL,
    T_SNOW,
    T_SNOW_SIDE,
    T_BEDROCK,
    T_COAL_ORE,
    T_IRON_ORE,
    T_GOLD_ORE,
    T_CACTUS_SIDE,
    T_CACTUS_TOP,
    T_BOOKSHELF,
    T_ROOF,
    T_SPRUCE_LOG,
    T_SPRUCE_LEAVES,
    T_COUNT
};

/* block flags */
#define BF_SOLID 0x01       /* the player collides with it */
#define BF_OPAQUE 0x02      /* hides the faces next to it, darkens corners and casts shadow */
#define BF_CUTOUT 0x04      /* texels with alpha 0 are holes (glass) */
#define BF_LIQUID 0x08      /* drawn see-through after everything else (water) */
#define BF_UNBREAKABLE 0x10 /* bedrock */

struct BlockInfo
{
    const char *name;
    uint8_t top, side, bottom; /* textures */
    uint8_t flags;
};

extern const BlockInfo g_blocks[B_COUNT];

/* 16 x 16 texels XRGB8888, alpha in the top byte (0: hole) */
extern uint32_t g_tex[T_COUNT][256];
/* the same in RGB565 for the renderer; holes are TEX_HOLE (a colour no texture uses) */
extern uint16_t g_tex565[T_COUNT][256];
#define TEX_HOLE 0xF81Fu

/* blocks the player can build with, in hotbar order */
extern const uint8_t g_buildable[];
extern const int g_buildable_count;

void blocks_init(void);

static inline bool block_opaque(uint8_t b)
{
    return (g_blocks[b].flags & BF_OPAQUE) != 0;
}

static inline bool block_solid(uint8_t b)
{
    return (g_blocks[b].flags & BF_SOLID) != 0;
}

/* An isometric picture of a block, size x size pixels, into XRGB8888 pixels (stride in
 * pixels); pixels outside the cube are left as they are */
void block_icon(uint32_t *pix, int stride, int size, uint8_t block);

#endif
