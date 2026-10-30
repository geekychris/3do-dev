// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* dock.h - Orbit Docking's game shell, shared by game.c and render.c */
#ifndef DOCK_H
#define DOCK_H

#include "a3d_draw.h"
#include "od.h"

enum { DS_TITLE, DS_PLAY, DS_OVER };

typedef struct {
    int state, state_timer;
    int start_level, autoplay, paused, help;
    char flash[32];
    int flash_timer, flash_pen;
    int32_t best;
} Dock;

extern Dock dk;
extern ODGame game;
extern const uint16_t dock_palette[32];

void render_game(Canvas *cv, const ODGame *g);
void render_title(Canvas *cv, const ODGame *g);
void render_help(Canvas *cv, int page);

#endif
