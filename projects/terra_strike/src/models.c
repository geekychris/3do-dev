// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * models.c - flat-shaded polygon models built at start-up from boxes,
 * pyramids and prisms. Each face is turned to point away from the
 * interior point of its part, so the renderer can back-face cull with the
 * sign of the projected area, and gets a shade from a fixed sun.
 */
#include "models.h"

Model models[MDL_COUNT];

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
    int32_t light;

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
    {
        int32_t mag = (nx < 0 ? -nx : nx) + (ny < 0 ? -ny : ny) + (nz < 0 ? -nz : nz);
        int shade = 1;
        if (mag && light * 4 > mag * 3) shade = 0;     /* lit */
        f->col = (uint8_t)(mat_pen[mat] + (shade && mat_shades[mat] > 1 ? 1 : 0));
    }
}

static void part(int x, int y, int z) { part_x = x; part_y = y; part_z = z; }

static void box(int mat, int x0, int x1, int y0, int y1, int z0, int z1)
{
    int b = cur->nv;
    part((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
    vtx(x0, y0, z0); vtx(x1, y0, z0); vtx(x1, y1, z0); vtx(x0, y1, z0);
    vtx(x0, y0, z1); vtx(x1, y0, z1); vtx(x1, y1, z1); vtx(x0, y1, z1);
    face(mat, 4, b + 0, b + 3, b + 2, b + 1);   /* front */
    face(mat, 4, b + 5, b + 6, b + 7, b + 4);   /* back */
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

static void begin(int id) { cur = &models[id]; cur->nv = 0; cur->nf = 0; }

void models_init(void)
{
    int b;

    /* player craft: an arrowhead with a raised tail, Zarch style */
    begin(MDL_CRAFT);
    part(0, 6, -6);
    b = cur->nv;
    vtx(0, 6, 50);      /* 0 nose */
    vtx(-38, 0, -26);   /* 1 left wing */
    vtx(38, 0, -26);    /* 2 right wing */
    vtx(0, 20, -20);    /* 3 tail top */
    vtx(0, -6, -18);    /* 4 belly */
    face(MAT_CRAFT, 3, b + 0, b + 1, b + 3, 0);
    face(MAT_CRAFT, 3, b + 0, b + 3, b + 2, 0);
    face(MAT_CRAFT, 3, b + 1, b + 2, b + 3, 0);
    face(MAT_CRAFT_DARK, 3, b + 0, b + 4, b + 1, 0);
    face(MAT_CRAFT_DARK, 3, b + 0, b + 2, b + 4, 0);
    face(MAT_CRAFT_DARK, 3, b + 1, b + 4, b + 2, 0);

    /* craft shadow: the wing triangle, flattened by the renderer */
    begin(MDL_SHADOW);
    part(0, -10, 0);
    vtx(0, 0, 50); vtx(-38, 0, -26); vtx(38, 0, -26);
    face(MAT_SHADOW, 3, 0, 1, 2, 0);

    begin(MDL_TANK);
    box(MAT_TANK, -32, 32, 0, 18, -42, 42);
    box(MAT_TANK, -17, 17, 18, 34, -18, 14);
    box(MAT_TANK, -3, 3, 23, 29, 14, 56);

    begin(MDL_BUILDING);
    box(MAT_BUILDING, -55, 55, 0, 100, -55, 55);
    pyramid(MAT_ROOF, -55, 55, 100, -55, 55, 150);

    begin(MDL_TOWER);   /* radar tower variant of a building */
    box(MAT_BUILDING, -24, 24, 0, 150, -24, 24);
    box(MAT_ROOF, -48, 48, 150, 168, -10, 10);

    begin(MDL_SHIP);
    part(0, 10, -10);
    b = cur->nv;
    vtx(0, 0, 100); vtx(-32, 0, 40); vtx(-32, 0, -90); vtx(32, 0, -90); vtx(32, 0, 40);
    vtx(0, 22, 110); vtx(-34, 22, 40); vtx(-34, 22, -92); vtx(34, 22, -92); vtx(34, 22, 40);
    face(MAT_SHIP, 4, b + 0, b + 1, b + 6, b + 5);
    face(MAT_SHIP, 4, b + 1, b + 2, b + 7, b + 6);
    face(MAT_SHIP, 4, b + 2, b + 3, b + 8, b + 7);
    face(MAT_SHIP, 4, b + 3, b + 4, b + 9, b + 8);
    face(MAT_SHIP, 4, b + 4, b + 0, b + 5, b + 9);
    face(MAT_SHIP, 3, b + 5, b + 6, b + 9, 0);           /* deck: bow */
    face(MAT_SHIP, 4, b + 6, b + 7, b + 8, b + 9);       /* deck: rear */
    box(MAT_SHIP_DECK, -20, 20, 22, 56, -60, 0);
    box(MAT_ROOF, -7, 7, 56, 80, -40, -26);

    begin(MDL_TREE);
    pyramid(MAT_TREE, -26, 26, 10, -26, 26, 100);
}
