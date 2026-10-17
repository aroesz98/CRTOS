/*
 * voxel_mesh.cpp - building the face lists of the sections.
 */
#include <stdlib.h>
#include <string.h>

#include "voxel_mesh.h"

const FaceDef g_faces[6] = {
    /* -x: seen from the west, right is +z */
    {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, -1, 0}},
    /* +x: seen from the east, right is -z */
    {{1, 0, 0}, {1, 1, 1}, {0, 0, -1}, {0, -1, 0}},
    /* -y */
    {{0, -1, 0}, {0, 0, 1}, {1, 0, 0}, {0, 0, -1}},
    /* +y */
    {{0, 1, 0}, {0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
    /* -z: seen from the north, right is -x */
    {{0, 0, -1}, {1, 1, 0}, {-1, 0, 0}, {0, -1, 0}},
    /* +z: seen from the south, right is +x */
    {{0, 0, 1}, {0, 1, 1}, {1, 0, 0}, {0, -1, 0}},
};

/* brightness of the sides, 0..1 */
static const float SIDE_LIGHT[6] = {0.62f, 0.62f, 0.5f, 1.0f, 0.8f, 0.8f};
/* by the number of free neighbours of a corner (0..3) */
static const float AO_LIGHT[4] = {0.46f, 0.64f, 0.82f, 1.0f};
#define SHADOW 0.5f

void mesh_init(Meshes *m)
{
    memset(m, 0, sizeof(*m));
}

void mesh_free(Meshes *m)
{
    for (int i = 0; i < SECTION_COUNT; i++)
    {
        for (int d = 0; d < 6; d++)
            free(m->s[i].solid[d]);
        free(m->s[i].water);
    }
    memset(m, 0, sizeof(*m));
}

void mesh_mark_all(Meshes *m)
{
    for (int i = 0; i < SECTION_COUNT; i++)
        m->s[i].dirty = true;
}

static void mark_cell(Meshes *m, int x, int y, int z)
{
    if (!world_inside(x, y, z))
        return;
    m->s[section_index(x / SECTION, y / SECTION, z / SECTION)].dirty = true;
}

void mesh_mark(Meshes *m, int x, int y, int z, bool column)
{
    for (int dz = -1; dz <= 1; dz++)
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                mark_cell(m, x + dx, y + dy, z + dz);
    if (column)
        for (int dz = -1; dz <= 1; dz++)
            for (int dx = -1; dx <= 1; dx++)
                for (int sy = 0; sy < SECTIONS_Y; sy++)
                    mark_cell(m, x + dx, sy * SECTION, z + dz);
}

static bool push(Quad **list, int *n, int *cap, const Quad *q)
{
    if (*n == *cap)
    {
        int nc = *cap ? *cap * 2 : 64;
        Quad *nl = (Quad *)realloc(*list, sizeof(Quad) * (size_t)nc);
        if (!nl)
            return false;
        *list = nl;
        *cap = nc;
    }
    (*list)[(*n)++] = *q;
    return true;
}

/* the section being built and a border of one block around it, copied into the cache: the
 * world is stored a layer after another, and walking it along x or z per slice would miss
 * the cache at nearly every step */
#define LS (SECTION + 2)
static uint8_t s_local[LS * LS * LS]; /* [y][z][x] */
static int s_bx, s_by, s_bz;          /* world coordinates of the section's first block */

static inline uint8_t cell(int x, int y, int z)
{
    return s_local[((y - s_by + 1) * LS + (z - s_bz + 1)) * LS + (x - s_bx + 1)];
}

/* returns false when the section itself is all air */
static bool load_local(const World *w, int sx, int sy, int sz)
{
    s_bx = sx * SECTION;
    s_by = sy * SECTION;
    s_bz = sz * SECTION;
    for (int y = -1; y <= SECTION; y++)
        for (int z = -1; z <= SECTION; z++)
        {
            uint8_t *row = &s_local[((y + 1) * LS + (z + 1)) * LS];
            int wy = s_by + y, wz = s_bz + z;
            if ((unsigned)wy < WORLD_Y && (unsigned)wz < WORLD_Z && s_bx > 0 && s_bx + SECTION < WORLD_X)
                memcpy(row, &w->b[world_index(s_bx - 1, wy, wz)], LS);
            else
                for (int x = -1; x <= SECTION; x++)
                    row[x + 1] = world_get(w, s_bx + x, wy, wz);
        }
    for (int y = 1; y <= SECTION; y++)
        for (int z = 1; z <= SECTION; z++)
        {
            const uint32_t *row = (const uint32_t *)&s_local[(y * LS + z) * LS + 1]; /* (unaligned is fine on the M7) */
            if (row[0] | row[1] | row[2] | row[3])
                return true;
        }
    return false;
}

static inline float sky_of(const World *w, int x, int y, int z)
{
    return world_sky(w, x, y, z) ? 1.0f : SHADOW;
}

/* brightness of the four corners of a face of block (x, y, z) */
static void corner_light(const World *w, int x, int y, int z, int face, bool ao, uint8_t out[4])
{
    const FaceDef *f = &g_faces[face];
    int cx = x + f->n[0], cy = y + f->n[1], cz = z + f->n[2]; /* the cell in front */
    float front_sky = sky_of(w, cx, cy, cz);
    static const int A[4] = {0, 1, 1, 0}, B[4] = {0, 0, 1, 1};
    for (int k = 0; k < 4; k++)
    {
        float light = SIDE_LIGHT[face];
        if (ao)
        {
            int ux = A[k] ? f->u[0] : -f->u[0], uy = A[k] ? f->u[1] : -f->u[1], uz = A[k] ? f->u[2] : -f->u[2];
            int vx = B[k] ? f->v[0] : -f->v[0], vy = B[k] ? f->v[1] : -f->v[1], vz = B[k] ? f->v[2] : -f->v[2];
            bool s1 = block_opaque(cell(cx + ux, cy + uy, cz + uz));
            bool s2 = block_opaque(cell(cx + vx, cy + vy, cz + vz));
            bool c = block_opaque(cell(cx + ux + vx, cy + uy + vy, cz + uz + vz));
            int free_n = (s1 && s2) ? 0 : 3 - (int)s1 - (int)s2 - (int)c;
            light *= AO_LIGHT[free_n];
            /* sky: the average over the open cells around the corner */
            float sky = front_sky;
            int n = 1;
            if (!s1)
            {
                sky += sky_of(w, cx + ux, cy + uy, cz + uz);
                n++;
            }
            if (!s2)
            {
                sky += sky_of(w, cx + vx, cy + vy, cz + vz);
                n++;
            }
            if (!c && !(s1 && s2))
            {
                sky += sky_of(w, cx + ux + vx, cy + uy + vy, cz + uz + vz);
                n++;
            }
            light *= sky / (float)n;
        }
        else
        {
            light *= front_sky;
        }
        int l = (int)(light * 255.0f + 0.5f);
        out[k] = (uint8_t)(l > 255 ? 255 : l);
    }
}

static int total(const Section *s)
{
    int n = s->nwater;
    for (int d = 0; d < 6; d++)
        n += s->nsolid[d];
    return n;
}

/* Which sides of the section see each other: flood fills of the cells one can see through
 * (not opaque); every fill that touches sides a and b connects them */
static uint16_t connectivity(const World *w, int sx, int sy, int sz)
{
    static uint8_t seen[SECTION * SECTION * SECTION];
    static uint16_t queue[SECTION * SECTION * SECTION];
    const int x0 = sx * SECTION, y0 = sy * SECTION, z0 = sz * SECTION;
    int open = 0;
    for (int i = 0; i < SECTION * SECTION * SECTION; i++)
    {
        int lx = i & 15, lz = (i >> 4) & 15, ly = i >> 8;
        seen[i] = block_opaque(cell(x0 + lx, y0 + ly, z0 + lz));
        open += !seen[i];
    }
    if (open == SECTION * SECTION * SECTION)
        return 0x7FFF; /* all air: everything connected */
    uint16_t conn = 0;
    for (int start = 0; start < SECTION * SECTION * SECTION && open; start++)
    {
        if (seen[start])
            continue;
        int head = 0, tail = 0, sides = 0;
        queue[tail++] = (uint16_t)start;
        seen[start] = 1;
        while (head < tail)
        {
            int i = queue[head++];
            open--;
            int lx = i & 15, lz = (i >> 4) & 15, ly = i >> 8;
            sides |= (lx == 0) << 0 | (lx == 15) << 1 | (ly == 0) << 2 | (ly == 15) << 3 | (lz == 0) << 4 | (lz == 15) << 5;
            const int nb[6] = {lx > 0 ? i - 1 : -1,    lx < 15 ? i + 1 : -1,     ly > 0 ? i - 256 : -1,
                               ly < 15 ? i + 256 : -1, lz > 0 ? i - 16 : -1,    lz < 15 ? i + 16 : -1};
            for (int k = 0; k < 6; k++)
                if (nb[k] >= 0 && !seen[nb[k]])
                {
                    seen[nb[k]] = 1;
                    queue[tail++] = (uint16_t)nb[k];
                }
        }
        for (int a = 0; a < 6; a++)
            for (int b = a + 1; b < 6; b++)
                if ((sides >> a & 1) && (sides >> b & 1))
                    conn |= (uint16_t)(1u << side_pair(a, b));
    }
    return conn;
}

static void build(Meshes *m, const World *w, int sx, int sy, int sz)
{
    Section *s = &m->s[section_index(sx, sy, sz)];
    m->faces -= total(s);
    for (int d = 0; d < 6; d++)
        s->nsolid[d] = 0;
    s->nwater = 0;
    s->dirty = false;
    if (!load_local(w, sx, sy, sz))
    {
        s->conn = 0x7FFF; /* only air: no faces, every side sees every other */
        m->faces += total(s);
        return;
    }
    s->conn = connectivity(w, sx, sy, sz);
    const int base[3] = {sx * SECTION, sy * SECTION, sz * SECTION};

    /*
     * Per direction and per slice of the section: which cells show a face (a 16 x 16 mask
     * in the face's u, v directions), then equal neighbours merged greedily into rectangles.
     * Faces with a light gradient (ambient occlusion) are never merged: the corners of a
     * rectangle must be what its faces had.
     */
    struct Cell
    {
        uint32_t key; /* 0: no face; equal keys can merge */
        Quad q;
    };
    static Cell mask[SECTION][SECTION]; /* [b][a] */
    for (int face = 0; face < 6; face++)
    {
        const FaceDef *f = &g_faces[face];
        int na = f->n[0] ? 0 : f->n[1] ? 1 : 2;  /* the axis of the normal */
        int ua = f->u[0] ? 0 : f->u[1] ? 1 : 2;  /* of u and v */
        int va = f->v[0] ? 0 : f->v[1] ? 1 : 2;
        int us = f->u[ua], vs = f->v[va];      /* their signs */
        /* steps in the local copy ([y][z][x], a border of one) along the three axes */
        const int step[3] = {1, LS * LS, LS};
        const int sn = step[na], su = step[ua], sv = step[va], nstep = f->n[na] * sn;
        for (int sl = 0; sl < SECTION; sl++)
        {
            int nfaces = 0;
            const int i0 = LS * LS + LS + 1 + sl * sn;
            for (int b = 0; b < SECTION; b++)
                for (int a = 0; a < SECTION; a++)
                {
                    Cell *mc = &mask[b][a];
                    mc->key = 0;
                    int i = i0 + a * su + b * sv;
                    uint8_t blk = s_local[i];
                    if (blk == B_AIR)
                        continue;
                    const BlockInfo *bi = &g_blocks[blk];
                    bool liquid = bi->flags & BF_LIQUID;
                    uint8_t nb = s_local[i + nstep];
                    if (liquid ? (nb != B_AIR && !(g_blocks[nb].flags & BF_CUTOUT)) : (block_opaque(nb) || nb == blk))
                        continue;
                    int c[3];
                    c[na] = base[na] + sl;
                    c[ua] = base[ua] + a;
                    c[va] = base[va] + b;
                    Quad *q = &mc->q;
                    q->face = (uint8_t)face;
                    q->tex = face == 3 ? bi->top : face == 2 ? bi->bottom : bi->side;
                    q->flags = (bi->flags & BF_CUTOUT) ? Q_CUTOUT : 0;
                    if (liquid && face == 3)
                        q->flags |= Q_LOW;
                    corner_light(w, c[0], c[1], c[2], face, !liquid, q->light);
                    bool even = q->light[0] == q->light[1] && q->light[1] == q->light[2] && q->light[2] == q->light[3];
                    mc->key = even ? 1u | (uint32_t)q->tex << 1 | (uint32_t)q->flags << 9 | (uint32_t)q->light[0] << 17 |
                                           (uint32_t)liquid << 25
                                     : 0x80000000u | (uint32_t)(b * SECTION + a);
                    nfaces++;
                }
            if (!nfaces)
                continue;
            for (int b = 0; b < SECTION; b++)
                for (int a = 0; a < SECTION; a++)
                {
                    uint32_t key = mask[b][a].key;
                    if (!key)
                        continue;
                    int wd = 1, ht = 1;
                    while (a + wd < SECTION && wd < MERGE_MAX && mask[b][a + wd].key == key)
                        wd++;
                    for (; b + ht < SECTION && ht < MERGE_MAX; ht++)
                    {
                        int k = 0;
                        while (k < wd && mask[b + ht][a + k].key == key)
                            k++;
                        if (k < wd)
                            break;
                    }
                    for (int j = 0; j < ht; j++)
                        for (int k = 0; k < wd; k++)
                            mask[b + j][a + k].key = 0;
                    /* the first corner belongs to the block at the start of u and v */
                    Quad q = mask[b][a].q;
                    int c[3];
                    c[na] = base[na] + sl;
                    c[ua] = base[ua] + (us > 0 ? a : a + wd - 1);
                    c[va] = base[va] + (vs > 0 ? b : b + ht - 1);
                    q.x = (uint8_t)c[0];
                    q.y = (uint8_t)c[1];
                    q.z = (uint8_t)c[2];
                    q.w = (uint8_t)wd;
                    q.h = (uint8_t)ht;
                    if (g_blocks[cell(c[0], c[1], c[2])].flags & BF_LIQUID)
                        push(&s->water, &s->nwater, &s->capwater, &q);
                    else
                        push(&s->solid[face], &s->nsolid[face], &s->capsolid[face], &q);
                }
        }
    }
    m->faces += total(s);
}

int mesh_update(Meshes *m, const World *w)
{
    int n = 0;
    for (int sy = 0; sy < SECTIONS_Y; sy++)
        for (int sz = 0; sz < SECTIONS_Z; sz++)
            for (int sx = 0; sx < SECTIONS_X; sx++)
                if (m->s[section_index(sx, sy, sz)].dirty)
                {
                    build(m, w, sx, sy, sz);
                    n++;
                }
    return n;
}
