// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * models.h - polygon models and the 32-colour palette layout.
 */
#ifndef MODELS_H
#define MODELS_H

#include <stdint.h>

/* Palette pens. Each route has its own colours for the same pens
 * (render.c route_palette); PEN_SKY is re-coloured line by line by the
 * copper for the sky gradient. */
#define PEN_BLACK     0
#define PEN_WHITE     1
#define PEN_PANEL     2
#define PEN_GREY      3
#define PEN_SKY       4
#define PEN_HAZE      5
#define PEN_WATER     6     /* 6, 7 */
#define PEN_ROAD      8     /* 8, 9 */
#define PEN_PAVE      10
#define PEN_GRASS     11    /* 11, 12 */
#define PEN_DIRT      13    /* 13, 14 */
#define PEN_ROCK      15
#define PEN_RED       16    /* 16, 17 */
#define PEN_YELLOW    18
#define PEN_ORANGE    19
#define PEN_AI        20    /* cyan: the autopilot's path and boxes */
#define PEN_AI2       21    /* green */
#define PEN_BLUE      22    /* 22, 23 */
#define PEN_CARW      24    /* 24, 25 */
#define PEN_KHAKI     26
#define PEN_TREE      27
#define PEN_GLASS     28
#define PEN_BROWN     29    /* 29 dark, 30 light */
#define PEN_SHADOW    31

/* Face colours >= COL_BODY are worked out when drawing: the object's own
 * body colour (lit / shaded), or the ambulance's flashing light. */
#define COL_BODY      200   /* 200 lit, 201 shaded */
#define COL_FLASH     210

enum { MAT_BODY, MAT_GLASS, MAT_DARK, MAT_WHITE, MAT_BROWN, MAT_TREE, MAT_ORANGE,
       MAT_CONCRETE, MAT_RED, MAT_FLASH, MAT_YELLOW, MAT_ROCK, MAT_SKIN, MAT_COUNT };

enum { MDL_CAR, MDL_TRUCK, MDL_TRACTOR, MDL_CYCLIST, MDL_DEER, MDL_CONE, MDL_TYRE,
       MDL_ROCK, MDL_AMBULANCE, MDL_GOAT, MDL_PED, MDL_TREE, MDL_PINE, MDL_POLE,
       MDL_HOUSE, MDL_BLOCK, MDL_OVERPASS, MDL_GANTRY, MDL_BOULDER, MDL_LAMP,
       MDL_COUNT };

#define MDL_MAXV 48
#define MDL_MAXF 32

typedef struct { uint8_t n, col; uint8_t idx[4]; } Face;
typedef struct {
    int nv, nf;
    int16_t v[MDL_MAXV][3];
    Face f[MDL_MAXF];
} Model;

extern Model models[MDL_COUNT];
void models_init(void);

#endif
