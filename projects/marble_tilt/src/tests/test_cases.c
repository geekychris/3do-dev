// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Marble Tilt's tests: the levels themselves (shape, a
 * start, an exit, a route between), the tilt, rolling, walls, holes and
 * falling back to a checkpoint, bumpers, gates, sand, the exit and the
 * level flow, the clock, the autopilot rolling every level, and a whole
 * autopilot game whose final state hash (tests/vectors.h, from `make
 * vectors` on the host) the Amiga must reproduce exactly. On the host
 * (make host-test), on the Amiga (marble_tilt_test) and inside the game
 * (the selftest hook).
 */
#include <string.h>
#include "../mt.h"
#define MT_TRACE_TICKS 6000             /* four minutes: the whole game */
#include "vectors.h"

const char test_client[] = "marble_tilt_test";
static MTGame tg;                       /* off the Amiga's stack */

static int count_events(MTGame *g, int type)
{
    MTEvent e;
    int n = 0;
    while (mt_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

static fix fabs_(fix a) { return a < 0 ? -a : a; }

/* an open board: floor everywhere inside the walls, the marble in the middle */
static void open_board(MTGame *g)
{
    int r, c;
    mt_new_game(g, 5, 1);
    for (r = 1; r < MT_N - 1; r++) for (c = 1; c < MT_N - 1; c++) g->cell[r][c] = '.';
    g->cell[10][10] = 'E';
    g->x = 0;
    g->z = 0;
    g->cp_row = 6;
    g->cp_col = 6;
    g->nev = 0;
}

static int inside_wall(const MTGame *g)
{
    int r = mt_row_of(g->z), c = mt_col_of(g->x);
    return mt_solid(g, r, c);
}

static int solve(MTGame *g, int level, int max, int *falls)
{
    int t;
    MTEvent e;
    mt_new_game(g, 1, level);
    for (t = 0; t < max && !g->over; t++) {
        mt_tick(g, mt_bot(g));
        if (inside_wall(g)) return 0;
        while (mt_pop_event(g, &e)) if (e.type == MTE_EXIT) { *falls = g->falls; return 1; }
    }
    *falls = g->falls;
    return 0;
}

int run_tests(void (*check)(int cond, const char *name))
{
    MTGame *g = &tg;
    int k, ok, r, c, falls;
    fix v0;
    uint32_t h1;

    /* the levels */
    for (ok = 1, k = 0; k < MT_LEVELS; k++) {
        int starts = 0, exits = 0, s = 0;
        for (r = 0; r < MT_N; r++) {
            if (strlen(mt_levels[k][r]) != MT_N) ok = 0;
            for (c = 0; c < MT_N; c++) {
                char ch = mt_levels[k][r][c];
                if (ch == 'S') starts++;
                if (ch == 'E') exits++;
                if ((r == 0 || c == 0 || r == MT_N - 1 || c == MT_N - 1) && ch != '#') ok = 0;
                if (!strchr(".# SECBGZ", ch)) ok = 0;
            }
        }
        mt_new_game(g, 1, k + 1);
        if (starts != 1 || exits != 1 || mt_path(g, g->cp_row, g->cp_col, &s) < 0 || s < 8) ok = 0;
    }
    check(ok, "every level: 12 x 12, walled round, one start, one exit, a route between");
    check(mt_row_of(mt_cell_z(3)) == 3 && mt_col_of(mt_cell_x(9)) == 9, "cells and coordinates agree");

    /* the tilt */
    open_board(g);
    for (k = 0; k < 3; k++) mt_tick(g, MT_IN_RIGHT);
    check(g->roll == 3 * (MT_TILT_MAX / 5) && g->pitch == 0, "the tilt eases over");
    for (k = 0; k < 5; k++) mt_tick(g, MT_IN_RIGHT);
    check(g->roll == MT_TILT_MAX, "as far as it goes");
    check(g->vx > 0 && fabs_(g->vz) < FX(0.01), "tilted right, the marble rolls right");
    for (k = 0; k < 8; k++) mt_tick(g, 0);
    check(g->roll == 0, "let go, the board levels");
    open_board(g);
    for (k = 0; k < 10; k++) mt_tick(g, MT_IN_UP);
    check(g->vz > 0 && g->z > 0, "UP: it rolls away from you");
    for (k = 0; k < 6; k++) mt_tick(g, 0);           /* (the board levels over 5 ticks) */
    v0 = g->vz;
    for (k = 0; k < 10; k++) mt_tick(g, 0);
    check(g->vz < v0 && g->vz > v0 * 4 / 5, "level, it rolls on, slowing a little");

    /* walls */
    open_board(g);
    g->vx = FX(4);
    for (k = 0; k < 120 && g->vx > 0; k++) { mt_tick(g, 0); if (inside_wall(g)) break; }
    check(!inside_wall(g) && g->vx < 0 && g->vx > -FX(2.5), "it bounces off a wall, slower");
    open_board(g);
    for (ok = 1, k = 0; k < 300; k++) { mt_tick(g, (k / 30) & 1 ? MT_IN_RIGHT | MT_IN_UP : MT_IN_LEFT | MT_IN_DOWN); if (inside_wall(g)) ok = 0; }
    check(ok, "rattled about, it never ends up in a wall");

    /* holes */
    open_board(g);
    g->cell[6][8] = ' ';
    g->time_left = 1000;
    for (k = 0; k < 60 && !g->falling; k++) mt_tick(g, MT_IN_RIGHT);
    check(g->falling && g->falls == 1 && g->time_left < 1000 - 70, "into a hole: it falls, and the clock loses 3 s");
    for (k = 0; k < MT_FALL_T; k++) mt_tick(g, 0);
    check(!g->falling && mt_row_of(g->z) == 6 && mt_col_of(g->x) == 6 && g->vx == 0, "then back at the checkpoint");
    open_board(g);
    g->cell[6][8] = 'C';
    for (k = 0; k < 80 && g->cp_col != 8; k++) mt_tick(g, MT_IN_RIGHT);
    check(g->cp_row == 6 && g->cp_col == 8, "rolling over a checkpoint moves it");

    /* bumpers */
    open_board(g);
    g->cell[6][8] = 'B';
    g->z = mt_cell_z(6);                /* square on to it */
    g->vx = FX(2);
    g->nev = 0;
    for (k = 0; k < 80 && !g->bumps; k++) mt_tick(g, 0);
    check(g->bumps == 1 && g->vx < -FX(2), "a bumper kicks it back harder");

    /* gates */
    open_board(g);
    g->cell[6][8] = 'G';
    check(g->gates_open, "gates start open");
    for (k = 0; k < MT_GATE_T + 1; k++) mt_tick(g, 0);
    check(!g->gates_open && mt_solid(g, 6, 8), "and shut after three seconds");
    g->vx = FX(3);
    for (k = 0; k < 60 && g->vx > 0; k++) mt_tick(g, 0);
    check(mt_col_of(g->x) < 8 && g->vx < 0, "shut, a gate is a wall");
    for (k = 0; k < MT_GATE_T; k++) mt_tick(g, 0);
    check(g->gates_open, "then open again");
    open_board(g);
    g->cell[6][6] = 'G';
    for (k = 0; k < MT_GATE_T + 10; k++) mt_tick(g, 0);
    check(g->gates_open, "a gate doesn't shut on the marble");

    /* sand */
    open_board(g);
    for (k = 0; k < 15; k++) mt_tick(g, MT_IN_UP);
    v0 = g->vz;
    open_board(g);
    for (r = 1; r < MT_N - 1; r++) for (c = 1; c < MT_N - 1; c++) g->cell[r][c] = 'Z';
    for (k = 0; k < 15; k++) mt_tick(g, MT_IN_UP);
    check(g->vz > 0 && g->vz < v0 / 2, "sand drags");

    /* the exit, the levels, the clock */
    open_board(g);
    g->x = mt_cell_x(10) - FROMI(30);
    g->z = mt_cell_z(10);
    g->time_left = 40 * 25;
    for (k = 0; k < 30 && !g->between; k++) mt_tick(g, MT_IN_RIGHT);
    check(g->between == MT_BETWEEN && g->score >= 100 + 390, "the exit: level done, the time left scores");
    for (k = 0; k < MT_BETWEEN; k++) mt_tick(g, 0);
    check(g->level == 2 && g->cell[5][5] == 'E', "then level 2");
    mt_new_game(g, 1, MT_LEVELS);
    g->between = 1;
    mt_tick(g, 0);
    check(g->won, "the last level done wins");
    open_board(g);
    g->time_left = 2;
    for (k = 0; k < 4; k++) mt_tick(g, 0);
    check(g->over && count_events(g, MTE_TIMEOUT) == 1, "out of time: over");

    /* the autopilot */
    for (ok = 1, k = 1; k <= MT_LEVELS; k++) {
        if (!solve(g, k, 3000, &falls)) ok = 0;
        else if (falls) ok = 0;
    }
    check(ok, "autopilot: rolls every level to the exit, never falling");

    /* determinism: the host and the Amiga must agree */
    mt_new_game(g, 1, 1);
    for (k = 0; k < MT_TRACE_TICKS && !g->over && !g->won; k++) mt_tick(g, mt_bot(g));
    h1 = mt_state_hash(g);
    check(g->won, "autopilot: the whole game");
    check(h1 == MT_TRACE_HASH, "the whole autopilot game matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef MT_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    int k;
    mt_new_game(&tg, 1, 1);
    for (k = 0; k < MT_TRACE_TICKS && !tg.over && !tg.won; k++) mt_tick(&tg, mt_bot(&tg));
    printf("/* make vectors: the autopilot from level 1: %s at level %d after %d ticks, score %ld, falls %d */\n",
           tg.won ? "won" : "stopped", tg.level, k, (long)tg.score, tg.falls);
    printf("#define MT_TRACE_HASH 0x%08lXUL\n", (unsigned long)mt_state_hash(&tg));
    return 0;
}
#endif
