// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* tilt.h - Marble Tilt's game shell, shared by game.c and render.c */
#ifndef TILT_H
#define TILT_H

#include "a3d_draw.h"
#include "mt.h"

enum { TS_TITLE, TS_PLAY, TS_OVER };

typedef struct {
    int state, state_timer;
    int start_level, autoplay, paused, help;
    char flash[32];
    int flash_timer, flash_pen;
    int32_t best;
} Tilt;

extern Tilt tl;
extern MTGame game;
extern const uint16_t tilt_palette[32];

void render_game(Canvas *cv, const MTGame *g);
void render_title(Canvas *cv, const MTGame *g);
void render_help(Canvas *cv, int page);

#endif
