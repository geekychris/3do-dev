// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* pinball.h - Boing Pinball's shell, shared by game.c and render.c */
#ifndef PINBALL_H
#define PINBALL_H

#include "a3d_draw.h"
#include "pb.h"

enum { PS_TITLE, PS_PLAY, PS_OVER };

/* the pens (render.c's palettes) */
enum {
    P_BLACK, P_FIELD1, P_FIELD2, P_ART1, P_ART2, P_ART3, P_METAL_D, P_METAL_L,
    P_CAB_D, P_CAB_L, P_RUBBER, P_RUBBER_D, P_RED, P_RED_D, P_FLASH, P_SHADOW,
    P_BLUE, P_BLUE_D, P_YEL, P_YEL_D, P_LRED, P_LRED_D, P_GREEN, P_MAG,
    P_ORANGE, P_DIM, P_DMD_BG, P_DMD_ON, P_DMD_MID, P_GLASS, P_WOOD, P_TEXT
};

/* the view: the table drawn once into a tall picture (an oblique view
 * from the player's end, heights up the screen), shown 224 rows at a time
 * under the dot-matrix display */
#define PIC_H    512
#define VIEW_Y   32                     /* the screen row the table starts at */
#define VIEW_H   (SCREEN_H - VIEW_Y)
#define CAM_MAX  (PIC_H - VIEW_H)

typedef struct {
    int state, state_timer;
    int table;                          /* which table is built */
    int paused, help;
    int32_t best[TB_COUNT];
    int cam;                            /* the picture row at the top of the view */
    int bumper_flash[PB_MAX_CIRC], sling_flash[2];
    int frame;
    int ball_phase[8];                  /* the Boing ball's spin, by ball id */
    fix ball_px[8], ball_py[8];
    char toast[40]; int toast_timer;    /* a line over the table */
    int demo;                           /* the bot is playing (the title) */
} Pinball;

extern Pinball pin;
extern Game game;
extern Table table;

int  render_build(int which);           /* build the table and draw it into the picture; 0: no memory */
void render_palette(int theme);
void render_game(Canvas *cv);
void render_title(Canvas *cv);
void render_title_start(void);         /* the title's attract cycle starts again */
void render_help(Canvas *cv, int page);
void render_follow(int snap);           /* move the view towards the ball */

#endif
