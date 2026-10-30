// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* crane.h - Harbour Crane's game shell, shared by game.c and render.c */
#ifndef CRANE_H
#define CRANE_H

#include "a3d_draw.h"
#include "hc.h"

enum { CS_TITLE, CS_PLAY, CS_OVER };

typedef struct {
    int state, state_timer;
    int start_level, autoplay, paused, help;
    char flash[32];
    int flash_timer, flash_pen;
    int32_t best;
    int splash_timer;
    int32_t splash_x;
} Crane;

extern Crane cr;
extern HCGame game;
extern const uint16_t crane_palette[32];

void render_game(Canvas *cv, const HCGame *g);
void render_title(Canvas *cv, const HCGame *g);
void render_help(Canvas *cv, int page);

#endif
