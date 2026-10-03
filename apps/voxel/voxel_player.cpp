/*
 * voxel_player.cpp - movement and collisions of the player, ray casting.
 *
 * The player is a box of 0.6 x 1.8 x 0.6 blocks. Each frame it moves along x, then z, then
 * y, in steps of at most 0.4 blocks; a step that would end inside a solid block stops at
 * its face. Walking into a single block step while on the ground jumps over it by itself.
 */
#include <math.h>

#include "voxel_config.h"
#include "voxel_player.h"

static bool solid_at(const World *w, int x, int y, int z)
{
    if (y < 0)
        return true;
    if ((unsigned)x >= WORLD_X || (unsigned)z >= WORLD_Z)
        return true; /* the edge of the island is a wall */
    return block_solid(world_get(w, x, y, z));
}

/* does the box with its feet at (x, y, z) touch a solid block? */
static bool collides(const World *w, float x, float y, float z)
{
    const float e = 0.001f;
    int x0 = (int)floorf(x - PLAYER_HALF_W + e), x1 = (int)floorf(x + PLAYER_HALF_W - e);
    int y0 = (int)floorf(y + e), y1 = (int)floorf(y + PLAYER_HEIGHT - e);
    int z0 = (int)floorf(z - PLAYER_HALF_W + e), z1 = (int)floorf(z + PLAYER_HALF_W - e);
    for (int yy = y0; yy <= y1; yy++)
        for (int zz = z0; zz <= z1; zz++)
            for (int xx = x0; xx <= x1; xx++)
                if (solid_at(w, xx, yy, zz))
                    return true;
    return false;
}

bool player_overlaps(const Player *p, int x, int y, int z)
{
    return (float)x < p->x + PLAYER_HALF_W && (float)(x + 1) > p->x - PLAYER_HALF_W && (float)y < p->y + PLAYER_HEIGHT &&
           (float)(y + 1) > p->y && (float)z < p->z + PLAYER_HALF_W && (float)(z + 1) > p->z - PLAYER_HALF_W;
}

float player_eye(const Player *p)
{
    return p->y + EYE_HEIGHT;
}

void player_spawn(Player *p, const World *w)
{
    p->x = w->spawn_x;
    p->z = w->spawn_z;
    p->y = w->spawn_y;
    while (p->y < WORLD_Y - 2 && collides(w, p->x, p->y, p->z))
        p->y += 1.0f;
    p->vy = 0;
    p->yaw = 0;
    p->pitch = -0.15f;
    p->on_ground = false;
    p->flying = false;
    p->in_water = false;
    p->step = 0;
}

/* moves along one axis; returns false when it hit something */
static bool move_axis(Player *p, const World *w, int axis, float d)
{
    while (d != 0.0f)
    {
        float s = d > 0.4f ? 0.4f : d < -0.4f ? -0.4f : d;
        float x = p->x + (axis == 0 ? s : 0), y = p->y + (axis == 1 ? s : 0), z = p->z + (axis == 2 ? s : 0);
        if (collides(w, x, y, z))
        {
            /* up to the face of the block */
            if (axis == 0)
                p->x = s > 0 ? floorf(x + PLAYER_HALF_W) - PLAYER_HALF_W - 0.001f : floorf(x - PLAYER_HALF_W) + 1.0f + PLAYER_HALF_W + 0.001f;
            else if (axis == 2)
                p->z = s > 0 ? floorf(z + PLAYER_HALF_W) - PLAYER_HALF_W - 0.001f : floorf(z - PLAYER_HALF_W) + 1.0f + PLAYER_HALF_W + 0.001f;
            else
                p->y = s > 0 ? floorf(y + PLAYER_HEIGHT) - PLAYER_HEIGHT - 0.001f : floorf(y) + 1.0f;
            if (collides(w, p->x, p->y, p->z)) /* (should not happen) back to where it was */
            {
                if (axis == 0)
                    p->x = x - s;
                else if (axis == 2)
                    p->z = z - s;
                else
                    p->y = y - s;
            }
            return false;
        }
        p->x = x;
        p->y = y;
        p->z = z;
        d -= s;
    }
    return true;
}

void player_update(Player *p, const World *w, const Controls *c, float dt)
{
    if (dt > 0.1f)
        dt = 0.1f;
    int fx = (int)floorf(p->x), fz = (int)floorf(p->z);
    p->in_water = world_get(w, fx, (int)floorf(p->y + 0.4f), fz) == B_WATER;
    bool head_water = world_get(w, fx, (int)floorf(player_eye(p)), fz) == B_WATER;

    /* horizontal movement in the direction of the view */
    float speed = p->flying ? FLY_SPEED : p->in_water ? WALK_SPEED * 0.55f : WALK_SPEED;
    float f = c->forward, s = c->strafe;
    float len = sqrtf(f * f + s * s);
    if (len > 1.0f)
    {
        f /= len;
        s /= len;
    }
    float sy = sinf(p->yaw), cy = cosf(p->yaw);
    float dx = (sy * f + cy * s) * speed * dt, dz = (-cy * f + sy * s) * speed * dt;
    bool free_x = move_axis(p, w, 0, dx);
    bool free_z = move_axis(p, w, 2, dz);
    p->step += sqrtf(dx * dx + dz * dz);

    /* one-block steps: jump by themselves when walking against them */
    if ((!free_x || !free_z) && p->on_ground && !p->flying && len > 0.1f)
    {
        float tx = p->x + (free_x ? 0 : (dx > 0 ? 0.35f : -0.35f)), tz = p->z + (free_z ? 0 : (dz > 0 ? 0.35f : -0.35f));
        if (!collides(w, tx, p->y + 1.05f, tz) && !collides(w, p->x, p->y + 1.05f, p->z))
            p->vy = JUMP_SPEED * 0.92f;
    }

    /* vertical */
    if (p->flying)
    {
        p->vy = (c->jump ? FLY_SPEED : 0.0f) - (c->down ? FLY_SPEED : 0.0f);
    }
    else if (p->in_water)
    {
        p->vy -= GRAVITY * 0.25f * dt;
        if (c->jump)
            p->vy = head_water ? 3.2f : 5.5f; /* swim up, and out at the surface */
        p->vy = p->vy < -3.0f ? -3.0f : p->vy;
    }
    else
    {
        if (c->jump && p->on_ground)
            p->vy = JUMP_SPEED;
        p->vy -= GRAVITY * dt;
        p->vy = p->vy < -MAX_FALL ? -MAX_FALL : p->vy;
    }
    bool was_falling = p->vy < 0;
    bool free_y = move_axis(p, w, 1, p->vy * dt);
    p->on_ground = !free_y && was_falling;
    if (!free_y)
        p->vy = 0;
    if (p->on_ground && p->flying && c->down)
        p->flying = false; /* landed */

    /* never leave the island, never fall out of the world */
    const float m = 0.5f;
    p->x = p->x < m ? m : p->x > WORLD_X - m ? WORLD_X - m : p->x;
    p->z = p->z < m ? m : p->z > WORLD_Z - m ? WORLD_Z - m : p->z;
    if (p->y < -8.0f)
        player_spawn(p, w);
    if (p->y > WORLD_Y + 16)
        p->y = WORLD_Y + 16;
}

Hit world_raycast(const World *w, const float o[3], const float d[3], float max)
{
    Hit h = {};
    int x = (int)floorf(o[0]), y = (int)floorf(o[1]), z = (int)floorf(o[2]);
    int sx = d[0] > 0 ? 1 : -1, sy = d[1] > 0 ? 1 : -1, sz = d[2] > 0 ? 1 : -1;
    float tdx = d[0] != 0 ? fabsf(1.0f / d[0]) : 1e30f;
    float tdy = d[1] != 0 ? fabsf(1.0f / d[1]) : 1e30f;
    float tdz = d[2] != 0 ? fabsf(1.0f / d[2]) : 1e30f;
    float tx = d[0] != 0 ? ((sx > 0 ? (float)(x + 1) - o[0] : o[0] - (float)x) * tdx) : 1e30f;
    float ty = d[1] != 0 ? ((sy > 0 ? (float)(y + 1) - o[1] : o[1] - (float)y) * tdy) : 1e30f;
    float tz = d[2] != 0 ? ((sz > 0 ? (float)(z + 1) - o[2] : o[2] - (float)z) * tdz) : 1e30f;
    float t = 0;
    int nx = 0, ny = 0, nz = 0;
    while (t <= max)
    {
        uint8_t b = world_inside(x, y, z) ? world_get(w, x, y, z) : B_AIR;
        if (b != B_AIR && b != B_WATER)
        {
            h.hit = true;
            h.x = x;
            h.y = y;
            h.z = z;
            h.nx = nx;
            h.ny = ny;
            h.nz = nz;
            h.dist = t;
            return h;
        }
        if (tx < ty && tx < tz)
        {
            t = tx;
            tx += tdx;
            x += sx;
            nx = -sx;
            ny = nz = 0;
        }
        else if (ty < tz)
        {
            t = ty;
            ty += tdy;
            y += sy;
            ny = -sy;
            nx = nz = 0;
        }
        else
        {
            t = tz;
            tz += tdz;
            z += sz;
            nz = -sz;
            nx = ny = 0;
        }
    }
    return h;
}
