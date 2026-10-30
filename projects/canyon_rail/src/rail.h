// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* rail.h - Canyon Rail's game shell, shared by game.c and render.c */
#ifndef RAIL_H
#define RAIL_H

#include "a3d_draw.h"
#include "cr.h"

enum { RS_TITLE, RS_PLAY, RS_OVER };

#define MAX_BURSTS 12
typedef struct { int t, size, pen; int32_t x, y, z; } Burst;

typedef struct {
    int state, state_timer;
    int start_stage, autoplay, paused, help;
    char flash[32];
    int flash_timer, flash_pen;
    int hurt_timer, tracer_timer;
    int32_t best;
    Burst burst[MAX_BURSTS];
} Rail;

extern Rail rl;
extern CRGame game;
extern int32_t view_ahead;              /* how far down the canyon is drawn (bridge: view_ahead) */
extern const uint16_t rail_palette[32];

void render_init(void);
void render_game(Canvas *cv, const CRGame *g);
void render_title(Canvas *cv, const CRGame *g);
void render_help(Canvas *cv, int page);

#endif
