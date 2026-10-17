/*
 * voxel_player.h - the player: walking with gravity, jumping, swimming and flying,
 * collisions with the blocks, and the ray that finds the block being aimed at.
 */
#ifndef VOXEL_PLAYER_H
#define VOXEL_PLAYER_H

#include "voxel_world.h"

struct Player
{
    float x, y, z;    /* the middle of the feet */
    float vy;         /* vertical speed */
    float yaw, pitch; /* radians */
    bool on_ground;
    bool flying;
    bool in_water;
    float step;       /* walking distance, for the view bobbing */
};

/* what the player wants in this frame */
struct Controls
{
    float forward;    /* -1..1 */
    float strafe;     /* -1..1, right positive */
    bool jump;        /* also: up when flying */
    bool down;        /* flying: down */
};

struct Hit
{
    bool hit;
    int x, y, z;      /* the block */
    int nx, ny, nz;   /* the side that was hit (a new block goes there) */
    float dist;
};

void player_spawn(Player *p, const World *w);
void player_update(Player *p, const World *w, const Controls *c, float dt);
float player_eye(const Player *p); /* height of the eyes */

/* true when a block at (x, y, z) would be inside the player */
bool player_overlaps(const Player *p, int x, int y, int z);

/* The first block that is not air or water along a ray, up to @max blocks */
Hit world_raycast(const World *w, const float origin[3], const float dir[3], float max);

#endif
