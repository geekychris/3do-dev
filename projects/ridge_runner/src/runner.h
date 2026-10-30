// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* runner.h - Ridge Runner's game shell, shared by game.c and render.c */
#ifndef RUNNER_H
#define RUNNER_H

#include "a3d_draw.h"
#include "rr.h"

enum { RS_TITLE, RS_PLAY, RS_OVER };

typedef struct {
    int state, state_timer;
    int start_track, autoplay, paused, help;
    char flash[32];
    int flash_timer, flash_pen;
    int32_t best;
    int cam_yaw;                        /* the chase camera's heading, easing after the bike's */
    int countdown;                      /* ticks of 3-2-1 before the start */
} Runner;

extern Runner rn;
extern RRGame game;
extern int32_t ground_win;              /* terrain cells each side drawn (bridge: ground_win) */

void render_init(void);
void render_theme(int track);
void render_game(Canvas *cv, const RRGame *g);
void render_title(Canvas *cv, const RRGame *g);
void render_help(Canvas *cv, int page);

#endif
