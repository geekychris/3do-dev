// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* duel.h - Tank Duel's game shell, shared by game.c and render.c */
#ifndef DUEL_H
#define DUEL_H

#include "a3d_draw.h"
#include "td.h"

enum { DS_TITLE, DS_PLAY, DS_OVER };

#define MAX_BLASTS 12
typedef struct { int t, size, pen; int32_t x, y, z; } Blast;   /* an explosion: world units */

typedef struct {
    int state, state_timer;
    int start_round, autoplay, paused, help;
    char flash[32];
    int flash_timer, flash_pen;
    int hurt_timer;                     /* the screen's red edge after a hit */
    int32_t best;
    Blast blast[MAX_BLASTS];
} Duel;

extern Duel du;
extern TDGame game;

void render_init(void);
void render_game(Canvas *cv, const TDGame *g);
void render_title(Canvas *cv, const TDGame *g);
void render_help(Canvas *cv, int page);
extern const uint16_t duel_palette[32];

#endif
