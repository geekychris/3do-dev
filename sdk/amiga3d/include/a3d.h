// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d.h - the shared 3D engine for the classic Amiga (68020+) and
 * AmigaOS 4 (PowerPC): maths and the camera. No Amiga headers, so a game's
 * simulation built on it is tested on the host too.
 *
 * Angles are 16-bit: 0..65535 is a full turn. A camera yaw of 0 looks
 * along +z, increasing towards +x; pitch > 0 looks down. Fixed-point
 * trig is * 65536.
 */
#ifndef A3D_H
#define A3D_H

#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 256

/* AmigaOS 4 draws at twice the resolution (A3_SUB pixels to each of the
 * games' 320 x 256), in full colour: the games keep their coordinates,
 * polygons and the 3D are drawn at the finer resolution */
#ifdef __PPC__
#define A3_SUB 2
#else
#define A3_SUB 1
#endif
#define PHYS_W (SCREEN_W * A3_SUB)
#define PHYS_H (SCREEN_H * A3_SUB)

extern const int32_t a3_sin_tab[256];
int32_t a3_sin(int a);                  /* * 65536, interpolated */
int32_t a3_cos(int a);
int a3_atan_ratio(int32_t num, int32_t den);    /* angle of (num, den), den > 0, |num| <= den */
int a3_atan2(int32_t x, int32_t y);     /* angle of (x, y) from +y towards +x, 0..65535 */
uint32_t a3_isqrt(uint32_t v);
int32_t a3_hypot(int32_t x, int32_t y);
uint32_t a3_rand(uint32_t *state);      /* 15-bit LCG output */
uint32_t a3_hash(uint32_t x);           /* stateless scramble */

typedef struct { int32_t x, y, z; } V3;

typedef struct {
    int32_t x, y, z;                    /* world units */
    int yaw, pitch;
    int32_t cy, sy, cp, sp;             /* cached trig, 16.16 (cam_setup) */
    int cx, cyc, fov;                   /* projection centre and scale (cam_view) */
    int vw, vh;                         /* the 3D area: 0..vw-1, 0..vh-1 */
    int32_t qx, qy, qz;                 /* cam_place: sixteenths of a unit past x, y, z */
    int precise;                        /* cam_place: camera space in sixteenths (see below) */
} Cam;

#define NEAR_Z  12

/* A camera that follows something moving smoothly must not snap to whole
 * units, or the scene steps unevenly (judder). cam_place() puts it at a
 * 16.16 position and makes cam_xform() work in sixteenths of a unit: its
 * output (camera space) is then 16 times larger. Projection doesn't care;
 * the engine scales its near plane and margins. Game code that adds sizes
 * or depth offsets in camera space multiplies them by CAM_U(c), and tests
 * against CAM_NEAR(c) instead of NEAR_Z. Cameras set up by assigning x, y,
 * z are unchanged (whole units, CAM_U 1). */
#define CAM_U(c)     ((c)->precise ? 16 : 1)
#define CAM_NEAR(c)  (NEAR_Z * CAM_U(c))
void cam_place(Cam *c, int32_t x, int32_t y, int32_t z);   /* 16.16 world position */
/* a copy of c that draws an object at a 16.16 position: pass the integer
 * parts (x >> 16 ...) to the drawing calls with the copy */
void cam_for(const Cam *c, int32_t x, int32_t y, int32_t z, Cam *out);

void cam_view(Cam *c, int vw, int vh, int cx, int cy, int fov);
void cam_setup(Cam *c);
void cam_xform(const Cam *c, int32_t wx, int32_t wy, int32_t wz, V3 *out);
void cam_project(const Cam *c, const V3 *p, int32_t *sx, int32_t *sy);   /* p->z >= NEAR_Z */
int  cam_horizon(const Cam *c);         /* screen row of the horizon */

#define MAX_PV 10
int clip_near(V3 *in, int n, V3 *out);
int clip_near_at(V3 *in, int n, V3 *out, int32_t near_z);
int clip_rect(int32_t *xs, int32_t *ys, int n, int32_t x0, int32_t y0,
              int32_t x1, int32_t y1);
#define SPAN_MAX 1024                   /* rows: AmigaOS 4 draws at twice the resolution */
int poly_spans(const int16_t *xs, const int16_t *ys, int n,
               int16_t *xl, int16_t *xr, int *ymin);

#endif
