// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * models.h - polygon models and the 32-colour palette layout.
 */
#ifndef MODELS_H
#define MODELS_H

#include <stdint.h>

/* Palette pens */
#define PEN_BLACK     0
#define PEN_WHITE     1
#define PEN_PANEL     2
#define PEN_GREY      3
#define PEN_TERRAIN   4     /* 4..15: 6 bands x 2 shades (sea, sand, grass, forest, rock, snow) */
#define PEN_RED       16
#define PEN_DKRED     17
#define PEN_YELLOW    18
#define PEN_ORANGE    19
#define PEN_CRAFT     20    /* 20, 21 */
#define PEN_CRAFT_DK  22
#define PEN_TANK      23    /* 23, 24 */
#define PEN_BUILD     25    /* 25, 26 */
#define PEN_ROOF      27
#define PEN_SHIP      28    /* 28, 29 */
#define PEN_TREE      30
#define PEN_SHADOW    31

enum { MAT_CRAFT, MAT_CRAFT_DARK, MAT_SHADOW, MAT_TANK, MAT_BUILDING, MAT_ROOF,
       MAT_SHIP, MAT_SHIP_DECK, MAT_TREE, MAT_COUNT };

static const uint8_t mat_pen[MAT_COUNT] = {
    PEN_CRAFT, PEN_CRAFT_DK, PEN_SHADOW, PEN_TANK, PEN_BUILD, PEN_ROOF,
    PEN_SHIP, PEN_BUILD, PEN_TREE
};
static const uint8_t mat_shades[MAT_COUNT] = { 2, 1, 1, 2, 2, 1, 2, 2, 1 };

enum { MDL_CRAFT, MDL_SHADOW, MDL_TANK, MDL_BUILDING, MDL_TOWER, MDL_SHIP,
       MDL_TREE, MDL_COUNT };

#define MDL_MAXV 32
#define MDL_MAXF 24

typedef struct { uint8_t n, col; uint8_t idx[4]; } Face;
typedef struct {
    int nv, nf;
    int16_t v[MDL_MAXV][3];
    Face f[MDL_MAXF];
} Model;

extern Model models[MDL_COUNT];
void models_init(void);

#endif
