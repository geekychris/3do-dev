// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "models.h"

#include <string.h>

/* Ship models. Coordinates in "ship units" — 1000 = 1 metre-ish.
 * Front of the ship is +Z (into the screen), so a positive-Z
 * vertex is the nose. */

Model model_cobra;
Model model_krait;
Model model_station;

static void set_verts(Model *m, const LONG src[][3], int n)
{
    int i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 3; j++)
            m->vbase[i][j] = src[i][j];
    m->num_verts = n;
}
static void set_faces(Model *m, const int src[][3], int n)
{
    int i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 3; j++)
            m->face[i][j] = src[i][j];
    m->num_faces = n;
}

/* --- Cobra Mk III (player) -------------------------------------
 * Classic Elite dart / arrowhead shape.
 * Nose forward, wide wings, tapered rear. */
static const LONG cobra_v[][3] = {
    {     0,     0,  1400 },   /* 0: nose */
    {  -800,  -100,  -500 },   /* 1: left wingtip */
    {   800,  -100,  -500 },   /* 2: right wingtip */
    {  -300,   200,  -400 },   /* 3: top-left canopy */
    {   300,   200,  -400 },   /* 4: top-right canopy */
    {     0,   200,   200 },   /* 5: canopy front */
    {  -300,  -200,  -700 },   /* 6: rear-left underside */
    {   300,  -200,  -700 },   /* 7: rear-right underside */
    {     0,  -200,   200 },   /* 8: belly front */
};
static const int cobra_f[][3] = {
    /* Top surface */
    { 0, 5, 4 }, { 0, 3, 5 },
    { 3, 4, 5 },
    { 0, 4, 2 }, { 0, 1, 3 },
    { 3, 1, 6 }, { 4, 7, 2 },
    { 3, 6, 4 }, { 6, 7, 4 },
    /* Belly */
    { 0, 2, 8 }, { 0, 8, 1 },
    { 1, 8, 6 }, { 2, 7, 8 },
    { 8, 7, 6 },
    /* Rear */
    { 1, 6, 3 }, { 2, 4, 7 },
    { 6, 7, 3 }, { 3, 7, 4 },
};

/* --- Krait (enemy) ---------------------------------------------
 * Triangular pyramid / diamond shape — Elite's iconic pirate. */
static const LONG krait_v[][3] = {
    {     0,     0,  1000 },   /* 0: nose */
    {  -700,     0,  -500 },   /* 1: left wingtip */
    {   700,     0,  -500 },   /* 2: right wingtip */
    {     0,   500,  -300 },   /* 3: top spine */
    {     0,  -500,  -300 },   /* 4: bottom spine */
    {     0,     0,  -700 },   /* 5: tail */
};
static const int krait_f[][3] = {
    /* Front (nose to each edge) */
    { 0, 3, 1 }, { 0, 2, 3 },
    { 0, 1, 4 }, { 0, 4, 2 },
    /* Rear (tail to each edge) */
    { 5, 1, 3 }, { 5, 3, 2 },
    { 5, 4, 1 }, { 5, 2, 4 },
};

/* --- Coriolis station -----------------------------------------
 * Rotating cube with the docking slot on the +Z face. Solid faces
 * only — the slot is faked by the docking-mode logic, not modelled. */
static const LONG stn_v[][3] = {
    { -1500, -1500, -1500 }, /* 0 */
    {  1500, -1500, -1500 }, /* 1 */
    {  1500,  1500, -1500 }, /* 2 */
    { -1500,  1500, -1500 }, /* 3 */
    { -1500, -1500,  1500 }, /* 4 */
    {  1500, -1500,  1500 }, /* 5 */
    {  1500,  1500,  1500 }, /* 6 */
    { -1500,  1500,  1500 }, /* 7 */
};
static const int stn_f[][3] = {
    { 0, 1, 2 }, { 0, 2, 3 },   /* back  -Z */
    { 5, 4, 7 }, { 5, 7, 6 },   /* front +Z */
    { 4, 0, 3 }, { 4, 3, 7 },   /* left  -X */
    { 1, 5, 6 }, { 1, 6, 2 },   /* right +X */
    { 3, 2, 6 }, { 3, 6, 7 },   /* top   +Y */
    { 4, 5, 1 }, { 4, 1, 0 },   /* bottom-Y */
};

void vt_build_models(void)
{
    memset(&model_cobra,   0, sizeof(model_cobra));
    memset(&model_krait,   0, sizeof(model_krait));
    memset(&model_station, 0, sizeof(model_station));

    set_verts(&model_cobra, cobra_v, sizeof(cobra_v) / sizeof(cobra_v[0]));
    set_faces(&model_cobra, cobra_f, sizeof(cobra_f) / sizeof(cobra_f[0]));
    e3d_compute_normals(&model_cobra);

    set_verts(&model_krait, krait_v, sizeof(krait_v) / sizeof(krait_v[0]));
    set_faces(&model_krait, krait_f, sizeof(krait_f) / sizeof(krait_f[0]));
    e3d_compute_normals(&model_krait);

    set_verts(&model_station, stn_v, sizeof(stn_v) / sizeof(stn_v[0]));
    set_faces(&model_station, stn_f, sizeof(stn_f) / sizeof(stn_f[0]));
    e3d_compute_normals(&model_station);
}
