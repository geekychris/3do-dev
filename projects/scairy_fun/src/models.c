// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * models.c - flat-shaded polygon models built at start-up from boxes and
 * pyramids (the builder comes from Terra Strike). Each face is turned to
 * point away from the interior point of its part, so the renderer can
 * back-face cull, and gets a shade from a fixed sun.
 *
 * Units: 16 = 1 metre; x right, y up, z forward (the front of a vehicle).
 */
#include "models.h"

Model models[MDL_COUNT];

/* lit and shaded pen for each material */
static const uint8_t mat_pen[MAT_COUNT][2] = {
    { COL_BODY, COL_BODY + 1 },         /* MAT_BODY: the object's own colour */
    { PEN_GLASS, PEN_GLASS },
    { PEN_SHADOW, PEN_SHADOW },
    { PEN_CARW, PEN_CARW + 1 },
    { PEN_BROWN + 1, PEN_BROWN },
    { PEN_TREE, PEN_TREE },
    { PEN_ORANGE, PEN_ORANGE },
    { PEN_GREY, PEN_PANEL },
    { PEN_RED, PEN_RED + 1 },
    { COL_FLASH, COL_FLASH },
    { PEN_YELLOW, PEN_ORANGE },
    { PEN_ROCK, PEN_DIRT },
    { PEN_BROWN + 1, PEN_BROWN + 1 },
};

static Model *cur;
static int part_x, part_y, part_z;

static int vtx(int x, int y, int z)
{
    int i = cur->nv++;
    cur->v[i][0] = (int16_t)x;
    cur->v[i][1] = (int16_t)y;
    cur->v[i][2] = (int16_t)z;
    return i;
}

/* Add a face; flip it if it faces the part's interior; pick a shade. */
static void face(int mat, int n, int a, int b, int c, int d)
{
    Face *f = &cur->f[cur->nf++];
    int16_t *A = cur->v[a], *B = cur->v[b], *C = cur->v[c];
    int32_t ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2];
    int32_t vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
    int32_t nx = uy * vz - uz * vy;
    int32_t ny = uz * vx - ux * vz;
    int32_t nz = ux * vy - uy * vx;
    int32_t out = nx * (A[0] - part_x) + ny * (A[1] - part_y) + nz * (A[2] - part_z);
    int32_t light, mag;

    f->n = (uint8_t)n;
    if (out >= 0) {
        f->idx[0] = a; f->idx[1] = b; f->idx[2] = c; f->idx[3] = d;
    } else {                        /* reverse the winding */
        if (n == 4) { f->idx[0] = d; f->idx[1] = c; f->idx[2] = b; f->idx[3] = a; }
        else        { f->idx[0] = c; f->idx[1] = b; f->idx[2] = a; f->idx[3] = 0; }
        nx = -nx; ny = -ny; nz = -nz;
    }
    /* sun high in the north-west: (-1, 2, 1) */
    light = -nx + 2 * ny + nz;
    mag = (nx < 0 ? -nx : nx) + (ny < 0 ? -ny : ny) + (nz < 0 ? -nz : nz);
    f->col = mat_pen[mat][(mag && light * 4 > mag * 3) ? 0 : 1];
}

static void part(int x, int y, int z) { part_x = x; part_y = y; part_z = z; }

static void box(int mat, int x0, int x1, int y0, int y1, int z0, int z1)
{
    int b = cur->nv;
    part((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    vtx(x0, y0, z0); vtx(x1, y0, z0); vtx(x1, y1, z0); vtx(x0, y1, z0);
    vtx(x0, y0, z1); vtx(x1, y0, z1); vtx(x1, y1, z1); vtx(x0, y1, z1);
    face(mat, 4, b + 0, b + 3, b + 2, b + 1);   /* back (-z) */
    face(mat, 4, b + 5, b + 6, b + 7, b + 4);   /* front (+z) */
    face(mat, 4, b + 4, b + 7, b + 3, b + 0);   /* left */
    face(mat, 4, b + 1, b + 2, b + 6, b + 5);   /* right */
    face(mat, 4, b + 3, b + 7, b + 6, b + 2);   /* top */
}

/* square pyramid on the rectangle (x0..x1, z0..z1) at height y0 */
static void pyramid(int mat, int x0, int x1, int y0, int z0, int z1, int apex)
{
    int b = cur->nv, t;
    part((x0 + x1) / 2, y0 + (apex - y0) / 4, (z0 + z1) / 2);
    vtx(x0, y0, z0); vtx(x1, y0, z0); vtx(x1, y0, z1); vtx(x0, y0, z1);
    t = vtx((x0 + x1) / 2, apex, (z0 + z1) / 2);
    face(mat, 3, b + 0, b + 1, t, 0);
    face(mat, 3, b + 1, b + 2, t, 0);
    face(mat, 3, b + 2, b + 3, t, 0);
    face(mat, 3, b + 3, b + 0, t, 0);
}

/* a flat dark rectangle on the ground under a vehicle */
static void shadow(int x0, int x1, int z0, int z1)
{
    int b = cur->nv;
    part(0, -10, 0);
    vtx(x0, 1, z0); vtx(x1, 1, z0); vtx(x1, 1, z1); vtx(x0, 1, z1);
    face(MAT_DARK, 4, b + 0, b + 1, b + 2, b + 3);
}

static void begin(int id) { cur = &models[id]; cur->nv = 0; cur->nf = 0; }

void models_init(void)
{
    begin(MDL_CAR);
    shadow(-16, 16, -38, 38);
    box(MAT_BODY, -14, 14, 3, 11, -36, 36);
    box(MAT_GLASS, -12, 12, 11, 19, -15, 12);
    box(MAT_BODY, -12, 12, 19, 21, -13, 10);

    begin(MDL_TRUCK);
    shadow(-21, 21, -92, 92);
    box(MAT_WHITE, -20, 20, 5, 58, -90, 50);
    box(MAT_BODY, -19, 19, 3, 44, 54, 90);
    box(MAT_GLASS, -17, 17, 30, 42, 88, 91);

    begin(MDL_TRACTOR);
    box(MAT_RED, -9, 9, 8, 22, -4, 30);
    box(MAT_GLASS, -10, 10, 22, 42, -26, -4);
    box(MAT_DARK, -17, -10, 0, 26, -28, -6);
    box(MAT_DARK, 10, 17, 0, 26, -28, -6);

    begin(MDL_CYCLIST);
    box(MAT_DARK, -1, 1, 1, 13, -14, 14);
    box(MAT_YELLOW, -4, 4, 13, 26, -5, 4);
    box(MAT_SKIN, -3, 3, 26, 32, -3, 3);

    begin(MDL_DEER);
    box(MAT_BROWN, -3, 3, 0, 14, -11, 11);
    box(MAT_BROWN, -5, 5, 14, 24, -14, 14);
    box(MAT_BROWN, -3, 3, 22, 36, 10, 18);

    begin(MDL_GOAT);
    box(MAT_WHITE, -2, 2, 0, 9, -7, 7);
    box(MAT_WHITE, -4, 4, 9, 17, -9, 9);
    box(MAT_WHITE, -2, 2, 15, 23, 7, 12);

    begin(MDL_PED);
    box(MAT_DARK, -3, 3, 0, 14, -2, 2);
    box(MAT_BODY, -4, 4, 14, 26, -3, 3);
    box(MAT_SKIN, -2, 2, 26, 31, -2, 2);

    begin(MDL_CONE);
    pyramid(MAT_ORANGE, -5, 5, 0, -5, 5, 13);

    begin(MDL_TYRE);
    box(MAT_DARK, -10, 10, 0, 7, -9, 9);

    begin(MDL_ROCK);
    pyramid(MAT_ROCK, -11, 11, 0, -9, 9, 13);

    begin(MDL_AMBULANCE);
    shadow(-18, 18, -52, 52);
    box(MAT_WHITE, -17, 17, 3, 44, -50, 28);
    box(MAT_WHITE, -16, 16, 3, 28, 28, 50);
    box(MAT_GLASS, -14, 14, 18, 27, 49, 51);
    box(MAT_RED, -18, 18, 20, 26, -51, 28);
    box(MAT_FLASH, -10, 10, 44, 49, 20, 27);

    begin(MDL_TREE);
    box(MAT_BROWN, -3, 3, 0, 30, -3, 3);
    pyramid(MAT_TREE, -28, 28, 28, -28, 28, 110);

    begin(MDL_PINE);
    box(MAT_BROWN, -3, 3, 0, 16, -3, 3);
    pyramid(MAT_TREE, -20, 20, 14, -20, 20, 140);

    begin(MDL_POLE);
    box(MAT_BROWN, -2, 2, 0, 150, -2, 2);
    box(MAT_BROWN, -22, 22, 136, 140, -2, 2);

    begin(MDL_HOUSE);
    box(MAT_WHITE, -50, 50, 0, 80, -50, 50);
    pyramid(MAT_RED, -56, 56, 80, -56, 56, 136);

    begin(MDL_BLOCK);
    box(MAT_CONCRETE, -56, 56, 0, 180, -56, 56);
    box(MAT_GLASS, -50, 50, 180, 230, -50, 50);         /* a glass top storey */

    begin(MDL_OVERPASS);
    box(MAT_CONCRETE, -150, -128, 0, 104, -10, 10);
    box(MAT_CONCRETE, 128, 150, 0, 104, -10, 10);
    box(MAT_CONCRETE, -170, 170, 104, 128, -26, 26);

    begin(MDL_GANTRY);
    box(MAT_YELLOW, -150, -140, 0, 130, -4, 4);
    box(MAT_YELLOW, 140, 150, 0, 130, -4, 4);
    box(MAT_RED, -150, 150, 104, 134, -3, 3);

    begin(MDL_BOULDER);
    pyramid(MAT_ROCK, -44, 44, 0, -40, 40, 66);

    begin(MDL_LAMP);
    box(MAT_CONCRETE, -2, 2, 0, 150, -2, 2);
    box(MAT_CONCRETE, -34, 2, 146, 150, -2, 2);
}
