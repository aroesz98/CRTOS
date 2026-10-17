/*
 * voxel_render.h - 3D renderer of the world, in software or on the accelerator.
 *
 * Software: faces become two triangles each, clipped at the near plane and filled with a
 * z-buffer, perspective-correct texturing (exact every 8 pixels, linear between), the corner
 * brightness blended across (smooth lighting) and distance fog. Sections are culled against
 * the view and drawn near to far; water is blended over the rest afterwards, then the
 * ocean around the island.
 *
 * Accelerator (GPU2D_CAP_PRIMS): the same faces as textured quadrilaterals in perspective,
 * light and fog over each, the farthest first (no depth buffer: an order exact for blocks).
 */
#ifndef VOXEL_RENDER_H
#define VOXEL_RENDER_H

#include <stdint.h>

#include "voxel_mesh.h"
#include "voxel_world.h"

struct gfx_buffer;

struct Camera
{
    float x, y, z;    /* the eye */
    float yaw, pitch; /* radians; yaw 0 looks north (-z), pitch up is positive */
};

struct Renderer
{
    int w, h;
    uint16_t *color;  /* RGB565, w x h */
    bool own_color;   /* (allocated by render_init; else the caller's) */
    uint16_t *depth;  /* 1/z scaled, 0 = far */
    /* the last frame (drawn by the GPU: us_sky, us_solid and us_water are the time of putting
     * the faces in order, of making the primitives and of the GPU; triangles the primitives) */
    int sections, quads, triangles, rows, pixels;
    uint32_t us_sky, us_solid, us_water; /* time of the phases */
    bool underwater;
};

/* the frame size; @color: w x h pixels of the caller's to draw into (null: allocated here) */
bool render_init(Renderer *r, int w, int h, uint16_t *color = nullptr);
void render_free(Renderer *r);

/* Draws the view of @cam; @target: the block to outline (x, y, z) or null */
void render_frame(Renderer *r, const World *w, const Meshes *m, const Camera *cam, const int *target);

/* The accelerator can draw the view (its textures made, once; after render_init) */
bool render_gpu_init(void);
/* The view drawn by the accelerator into @view (r->w x r->h); false: not drawn */
bool render_frame_gpu(Renderer *r, gfx_buffer *view, const World *w, const Meshes *m, const Camera *cam,
                      const int *target);

/* The direction of the ray through pixel (px, py) of the render buffer (unit length) */
void render_ray(const Renderer *r, const Camera *cam, float px, float py, float dir[3]);

/* The view direction of the camera (unit length) */
void camera_forward(const Camera *cam, float dir[3]);

#endif
