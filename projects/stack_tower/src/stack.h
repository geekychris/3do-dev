// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* stack.h - Stack Tower's game shell, shared by game.c and render.c */
#ifndef STACK_H
#define STACK_H

#include "a3d_draw.h"
#include "st.h"

enum { SS_TITLE, SS_PLAY, SS_OVER };

typedef struct {
    int state, state_timer;
    int mode, start_level;
    int view;                           /* the column facing you, 8.8, turning towards the piece */
    int ghost, paused, help;
    char flash[24];
    int flash_timer;
    int32_t best[MODE_COUNT];
} Stack;

extern Stack stack;
extern StGame game;
extern const uint16_t stack_palette[32];

void render_game(Canvas *cv, const StGame *g);
void render_title(Canvas *cv, const StGame *g);
void render_title_start(void);         /* the title's painting, then the demo, from the start */
void render_help(Canvas *cv, int page);

#endif
