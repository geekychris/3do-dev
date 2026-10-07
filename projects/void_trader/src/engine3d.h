// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef ENGINE3D_H
#define ENGINE3D_H

/*
 * Multi-object solid-poly 3D engine, adapted from aga3d.
 *
 * Key changes vs aga3d:
 *   1. Multiple objects tracked in a world — each with its own
 *      (x,y,z) position and (pitch,yaw,roll) orientation.
 *   2. Camera has its own 6DoF pose — the view transform inverts
 *      the camera's rotation and translation before projecting.
 *   3. Rendering is a public API called each frame with a list of
 *      Entitys; the caller owns the world state.
 *
 * Fixed-point convention: rotation angles are FP-scaled (1 << FP =
 * one unit). sin/cos table returns 12-bit FP (1 << 12 = 1.0).
 * World coords are plain LONG world-units.
 */

#include <exec/types.h>

#define FP        12
#define ONE       (1L << FP)

/* Vertex + face count caps per model. Enough for a Cobra Mk III
 * or a Coriolis station. */
#define MAX_VERTS 64
#define MAX_FACES 48

/* Static model definition — vertex list + triangle face list.
 * Face normals are computed at load time so we can backface cull
 * and flat-shade against a directional light. */
typedef struct {
    LONG vbase[MAX_VERTS][3];       /* vertex coords in local space */
    int  face[MAX_FACES][3];        /* triangle indices */
    LONG fnorm[MAX_FACES][3];       /* face normals, FP-scaled */
    int  num_verts;
    int  num_faces;
} Model;

/* World-space instance of a model. Multiple instances may share
 * the same Model pointer. */
typedef struct {
    const Model *model;
    LONG   x, y, z;                 /* world position */
    LONG   pitch, yaw, roll;        /* orientation, 360-scale */
    UBYTE  active;                  /* 0 = slot free */
    UBYTE  team;                    /* 0 player, 1 enemy, 2 station */
    UBYTE  pen_base;                /* palette pen for shading ramp */
    UBYTE  hp;
} Entity;

/* Camera pose — world-space position + orientation. Rendering
 * applies the inverse transform. */
typedef struct {
    LONG x, y, z;
    LONG pitch, yaw, roll;
} Camera;

/* Screen setup — the caller supplies the RastPort we should draw
 * into and the viewport dimensions. Called once at boot. */
void e3d_init(int screen_w, int screen_h);

/* Build a model's face normals from its vertex + face lists.
 * Call once per model after populating vbase[], face[], num_verts,
 * num_faces. */
void e3d_compute_normals(Model *m);

/* Sine/cosine lookup, FP-scaled result. Angle in 360-unit range;
 * wraps automatically. */
LONG e3d_sin(LONG angle);
LONG e3d_cos(LONG angle);

/* Full frame render: clears sky, transforms + rasterises each
 * active object into rp, projects a starfield backdrop. Painter's
 * algorithm depth-sorts objects by distance so nearer ones
 * overwrite farther ones. */
struct RastPort;
void e3d_render_frame(struct RastPort *rp,
                      const Camera *cam,
                      const Entity *objs, int num_objs);

/* Project a single world-space point to screen. Returns 1 if in
 * front of camera and inside the viewport, 0 otherwise. Used for
 * HUD widgets (targeting reticle, scanner dots). */
int e3d_project(const Camera *cam, LONG wx, LONG wy, LONG wz,
                int *sx, int *sy, LONG *depth);

#endif
