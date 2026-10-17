/*
 * voxel_config.h - sizes and constants of the voxel game.
 *
 * Coordinates: x east, y up, z south; one block = 1.0. The world is a fixed island of
 * WORLD_X x WORLD_Z columns, WORLD_Y blocks high, cut into sections of 16 x 16 x 16 blocks
 * (the unit of meshing and culling). Around it the renderer draws an endless ocean.
 */
#ifndef VOXEL_CONFIG_H
#define VOXEL_CONFIG_H

#define WORLD_X 192
#define WORLD_Y 64
#define WORLD_Z 192

#define SECTION 16
#define SECTIONS_X (WORLD_X / SECTION)
#define SECTIONS_Y (WORLD_Y / SECTION)
#define SECTIONS_Z (WORLD_Z / SECTION)

#define SEA_LEVEL 24 /* water fills y < SEA_LEVEL */

/* view */
#define FOV_DEG 75.0f     /* horizontal field of view */
#define Z_NEAR 0.06f
#define VIEW_DIST 44.0f   /* sections farther than this are not drawn */
#define FOG_START 26.0f
#define FOG_END 42.0f
#define OCEAN_RING 64     /* how far the endless ocean reaches beyond the island */

/* player */
#define PLAYER_HALF_W 0.3f
#define PLAYER_HEIGHT 1.8f
#define EYE_HEIGHT 1.62f
#define WALK_SPEED 4.3f   /* blocks per second */
#define FLY_SPEED 9.0f
#define GRAVITY 26.0f
#define JUMP_SPEED 8.6f
#define MAX_FALL 40.0f
#define REACH 7.0f        /* blocks the player can break or build at */

#endif
