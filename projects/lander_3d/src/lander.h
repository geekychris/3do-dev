// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* lander.h - Lander 3D's game shell, shared by game.c and render.c */
#ifndef LANDER_H
#define LANDER_H

#include "a3d_draw.h"
#include "ld.h"

enum { LS_TITLE, LS_PLAY, LS_RESULT, LS_OVER };
#define MODE_TOUR W_COUNT               /* the menu's worlds, then the tour */

typedef struct {
    int state, state_timer;
    int menu;                           /* 0..2 a world, 3 the tour */
    int ships, level;
    int32_t score, last_score, best;
    int paused, help, demo;             /* demo: the autopilot is flying */
    fix steady;                         /* the steady throttle (Q/E) */
    int thrust;                         /* thrust key held */
    char msg[48], msg2[48];
    int frame;
} Lander;

extern Lander lander;
extern Level level;

extern int32_t ground_win;
void render_init_models(void);
void render_palette(int world);
void render_game(Canvas *cv, const Level *L);
void render_title(Canvas *cv, const Level *L);
void render_title_start(void);         /* the title's painting, then the demo, from the start */
void render_help(Canvas *cv, int page);

#endif
