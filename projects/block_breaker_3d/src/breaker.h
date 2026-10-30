// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* breaker.h - Block Breaker 3D's game shell, shared by game.c and render.c */
#ifndef BREAKER_H
#define BREAKER_H

#include "a3d_draw.h"
#include "bb.h"

enum { BS_TITLE, BS_PLAY, BS_OVER };

#define MAX_PARTS 48
typedef struct { int32_t x, y, z, vx, vy, vz; int life, col; } Part;   /* world units * 16 */

typedef struct {
    int state, state_timer;
    int start_level, autoplay, paused, help;
    char flash[32];
    int flash_timer;
    int32_t best;
    Part part[MAX_PARTS];
} Breaker;

extern Breaker br;
extern BBGame game;
extern const uint16_t breaker_palette[32];

void render_game(Canvas *cv, const BBGame *g);
void render_title(Canvas *cv, const BBGame *g);
void render_help(Canvas *cv, int page);
int  brick_pen(int type, int hits);    /* the lit pen; +1 shaded */

#endif
