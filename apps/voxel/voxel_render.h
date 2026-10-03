/*
 * voxel_render.h - software 3D renderer of the world.
 *
 * Faces become two triangles each, clipped at the near plane and filled with a z-buffer,
 * perspective-correct texturing (exact every 8 pixels, linear between), the corner
 * brightness blended across (smooth lighting) and distance fog. Sections are culled against
 * the view and drawn near to far; water is blended over the rest afterwards, then the
 * ocean around the island.
 */
#ifndef VOXEL_RENDER_H
#define VOXEL_RENDER_H

#include <stdint.h>

#include "voxel_mesh.h"
#include "voxel_world.h"

struct Camera
{
    float x, y, z;    /* the eye */
    float yaw, pitch; /* radians; yaw 0 looks north (-z), pitch up is positive */
};

struct Renderer
{
    int w, h;
    uint16_t *color;  /* RGB565, w x h */
    uint16_t *depth;  /* 1/z scaled, 0 = far */
    /* the last frame */
    int sections, quads, triangles, rows, pixels;
    uint32_t us_sky, us_solid, us_water; /* time of the phases */
    bool underwater;
};

bool render_init(Renderer *r, int w, int h);
void render_free(Renderer *r);

/* Draws the view of @cam; @target: the block to outline (x, y, z) or null */
void render_frame(Renderer *r, const World *w, const Meshes *m, const Camera *cam, const int *target);

/* The direction of the ray through pixel (px, py) of the render buffer (unit length) */
void render_ray(const Renderer *r, const Camera *cam, float px, float py, float dir[3]);

/* The view direction of the camera (unit length) */
void camera_forward(const Camera *cam, float dir[3]);

#endif
