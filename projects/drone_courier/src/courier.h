// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* courier.h - Drone Courier's game shell, shared by game.c and render.c */
#ifndef COURIER_H
#define COURIER_H

#include "a3d_draw.h"
#include "dc.h"

enum { CS_TITLE, CS_PLAY, CS_OVER };

typedef struct {
    int state, state_timer;
    int start_day, autoplay, paused, help;
    char flash[32];
    int flash_timer, flash_pen;
    int32_t best;
    int cam_yaw;                        /* the chase camera's heading, easing after the drone's */
} Courier;

extern Courier co;
extern DCGame game;
extern int32_t view_range;              /* how far the city is drawn (bridge: view_range) */
extern int32_t towers_drawn;
extern const uint16_t courier_palette[32];

void render_game(Canvas *cv, const DCGame *g);
void render_title(Canvas *cv, const DCGame *g);
void render_help(Canvas *cv, int page);

#endif
