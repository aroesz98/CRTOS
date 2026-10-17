/*
 * voxel_mesh.h - the visible faces of the world, kept per section (16 x 16 x 16 blocks).
 *
 * A face is emitted only where a block touches one that does not hide it (air, water, glass).
 * Each corner gets its brightness once, when the section is built: the side of the block
 * (top bright, bottom dark), ambient occlusion from the blocks around the corner, and
 * shadow when the cell in front of the face does not see the sky.
 */
#ifndef VOXEL_MESH_H
#define VOXEL_MESH_H

#include <stdint.h>

#include "voxel_world.h"

/* faces: 0 -x, 1 +x, 2 -y, 3 +y, 4 -z, 5 +z */
struct FaceDef
{
    int8_t n[3];      /* normal */
    int8_t o[3];      /* first corner, relative to the block */
    int8_t u[3], v[3]; /* the corners are o, o+u, o+u+v, o+v (texture: u right, v down) */
};

extern const FaceDef g_faces[6];

#define Q_CUTOUT 0x01 /* texture with holes */
#define Q_LOW 0x02    /* water surface, a little below the top of its block */

/* A face, or a rectangle of equal faces merged into one (same texture, the same light at
 * every corner): w x h blocks along the face's u and v directions, starting at the block
 * whose corner is the face's first corner */
struct Quad
{
    uint8_t x, y, z;   /* that block */
    uint8_t face;
    uint8_t tex;
    uint8_t flags;
    uint8_t w, h;
    uint8_t light[4];  /* 0..255 per corner */
};

#define MERGE_MAX 8    /* blocks: bigger faces would show the fog's linear interpolation */

struct Section
{
    Quad *solid[6];    /* opaque and cut-out faces, by the direction they face */
    int nsolid[6], capsolid[6];
    Quad *water;       /* see-through faces */
    int nwater, capwater;
    /* which sides of the section are connected through cells one can see through (a bit per
     * pair of sides, see section_connected()): the renderer does not look through solid
     * rock into closed caves */
    uint16_t conn;
    bool dirty;
};

#define SECTION_COUNT (SECTIONS_X * SECTIONS_Y * SECTIONS_Z)

struct Meshes
{
    Section s[SECTION_COUNT];
    int faces;         /* all faces, for the statistics */
};

static inline int section_index(int sx, int sy, int sz)
{
    return (sy * SECTIONS_Z + sz) * SECTIONS_X + sx;
}

/* bit of a pair of sides a != b (0..5) in Section.conn */
static inline int side_pair(int a, int b)
{
    static const int8_t bit[6][6] = {{-1, 0, 1, 2, 3, 4},  {0, -1, 5, 6, 7, 8},    {1, 5, -1, 9, 10, 11},
                                     {2, 6, 9, -1, 12, 13}, {3, 7, 10, 12, -1, 14}, {4, 8, 11, 13, 14, -1}};
    return bit[a][b];
}

static inline bool section_connected(const Section *s, int a, int b)
{
    return (s->conn >> side_pair(a, b)) & 1u;
}

void mesh_init(Meshes *m);
void mesh_free(Meshes *m);
void mesh_mark_all(Meshes *m);
/* after a block changed: its section and the neighbouring ones; with @column also every
 * section around the column (its sky height changed) */
void mesh_mark(Meshes *m, int x, int y, int z, bool column);
/* rebuilds the dirty sections; returns how many */
int mesh_update(Meshes *m, const World *w);

#endif
