// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * mt.h - Marble Tilt's game logic: tilt a floating maze board to roll a
 * marble to the exit. The board is a grid of 12 x 12 cells, written as
 * text, one character a cell:
 *
 *   .  floor          #  wall           (space)  a hole: the marble falls
 *   S  start          E  the exit       C  a checkpoint (you come back here)
 *   B  a bumper: it kicks the marble away
 *   G  a gate: open for three seconds, shut for three
 *   Z  sand: the marble drags through it
 *
 * The tilt eases towards where the stick points; the marble rolls downhill
 * with a little friction and bounces off walls. Falling costs three
 * seconds and puts you back at the last checkpoint. Each level has a
 * clock; reach the exit before it runs out.
 *
 * x across (column), z away (row 0 is the far edge... see mt_cell_x/z);
 * 16.16 fixed point world units, one tick 40 ms. No Amiga headers: tested
 * on the host too.
 */
#ifndef MT_GAME_H
#define MT_GAME_H

#include <stdint.h>
#include "a3d_fix.h"

#define MT_N        12                  /* cells each way */
#define MT_CELL     40
#define MT_R        9                   /* the marble's radius */
#define MT_LEVELS   8
#define MT_TILT_MAX FX(0.12)            /* radians */
#define MT_GATE_T   75                  /* ticks a gate stays open, then shut */
#define MT_FALL_T   25                  /* ticks spent falling */
#define MT_BETWEEN  60

/* input bits */
#define MT_IN_LEFT  1
#define MT_IN_RIGHT 2
#define MT_IN_UP    4                   /* tilt away: the marble rolls away from you */
#define MT_IN_DOWN  8

enum { MTE_NONE, MTE_WALL, MTE_BUMP, MTE_FALL, MTE_RESPAWN, MTE_CHECKPOINT, MTE_GATE, MTE_EXIT, MTE_LEVEL,
       MTE_TIMEOUT, MTE_OVER, MTE_WIN, MTE_COUNT };
typedef struct { int type, a, b; } MTEvent;
#define MT_MAX_EVENTS 16

typedef struct {
    uint32_t seed;
    int level, over, won, between;
    char cell[MT_N][MT_N + 1];          /* [row][col] */
    fix x, z, vx, vz;                   /* the marble: board coordinates from its middle */
    fix roll, pitch;                    /* the tilt: roll lowers the right side, pitch the far side */
    int falling;                        /* ticks left of a fall, 0 = rolling */
    int cp_row, cp_col;                 /* where a fall puts you back */
    int gates_open;
    int falls, bumps;
    int32_t time_left, tick, level_ticks;
    int32_t score;
    MTEvent ev[MT_MAX_EVENTS];
    int nev;
} MTGame;

extern const char *const mt_event_names[MTE_COUNT];
extern const char *const mt_level_names[MT_LEVELS];
extern const char *const mt_levels[MT_LEVELS][MT_N];

void mt_new_game(MTGame *g, uint32_t seed, int level);
void mt_load_level(MTGame *g, int level);
void mt_tick(MTGame *g, unsigned in);
int  mt_pop_event(MTGame *g, MTEvent *e);
unsigned mt_bot(const MTGame *g);       /* the autopilot's input */
uint32_t mt_state_hash(const MTGame *g);

fix  mt_cell_x(int col);                /* a cell's middle */
fix  mt_cell_z(int row);
int  mt_col_of(fix x);
int  mt_row_of(fix z);
char mt_at(const MTGame *g, int row, int col);   /* '#' outside the board */
int  mt_solid(const MTGame *g, int row, int col);
int  mt_path(const MTGame *g, int row, int col, int *steps);   /* the autopilot's route: the next cell, -1 none */

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
