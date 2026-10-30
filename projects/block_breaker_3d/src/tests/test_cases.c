// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Block Breaker 3D's tests: the rules one by one, the
 * autopilot clearing levels, and a long autopilot game whose final state
 * hash (tests/vectors.h, from `make vectors` on the host) the Amiga must
 * reproduce exactly. On the host (make host-test), on the Amiga
 * (block_breaker_3d_test) and inside the game (the selftest hook).
 */
#include <string.h>
#include "../bb.h"
#define BB_TRACE_TICKS 20000            /* 13 minutes of play */
#include "vectors.h"

const char test_client[] = "block_breaker_3d_test";
static BBGame tg;                       /* off the Amiga's stack */

static int count_events(BBGame *g, int type)
{
    BBEvent e;
    int n = 0;
    while (bb_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

/* an empty tunnel (one steel brick, so the level never clears) */
static void empty(BBGame *g)
{
    bb_new_game(g, 5, 1);
    memset(g->cell, 0, sizeof(g->cell));
    memset(g->hits, 0, sizeof(g->hits));
    g->cell[bb_index(0, 0, 2)] = BK_STEEL;
    g->left = 1;
    g->nev = 0;
}

/* the ball in flight at x y z (world units) with speed vx vy vz (1/256) */
static void fly(BBGame *g, int x, int y, int z, int32_t vx, int32_t vy, int32_t vz)
{
    g->stuck = 0;
    g->bx = x * BB_FX; g->by = y * BB_FX; g->bz = z * BB_FX;
    g->vx = vx; g->vy = vy; g->vz = vz;
}

static int inside_brick(const BBGame *g)
{
    int i;
    for (i = 0; i < BB_CELLS; i++) {
        int32_t x0, y0, z0;
        if (!g->cell[i]) continue;
        bb_cell_box(i, &x0, &y0, &z0);
        if (g->bx > (x0 - BB_R) * BB_FX && g->bx < (x0 + BB_CW + BB_R) * BB_FX &&
            g->by > (y0 - BB_R) * BB_FX && g->by < (y0 + BB_CH + BB_R) * BB_FX &&
            g->bz > (z0 - BB_R) * BB_FX && g->bz < (z0 + BB_CD + BB_R) * BB_FX) return 1;
    }
    return 0;
}

/* the autopilot plays seed until the game ends or max ticks; returns
 * whether every tick kept the ball inside the tunnel and out of bricks */
static int autoplay(BBGame *g, uint32_t seed, int level, int max, int stop_after_clear)
{
    int t, ok = 1;
    bb_new_game(g, seed, level);
    for (t = 0; t < max && !g->over && !g->won; t++) {
        BBEvent e;
        bb_tick(g, bb_bot(g));
        while (bb_pop_event(g, &e)) if (e.type == BBE_CLEAR && stop_after_clear) return ok;
        if (!g->stuck && !g->between) {
            if (g->bx < -(BB_W - BB_R) * BB_FX || g->bx > (BB_W - BB_R) * BB_FX) ok = 0;
            if (g->by < -(BB_H - BB_R) * BB_FX || g->by > (BB_H - BB_R) * BB_FX) ok = 0;
            if (g->bz > (BB_D - BB_R) * BB_FX) ok = 0;
            if (g->vz == 0) ok = 0;
            if (inside_brick(g)) ok = 0;
        }
    }
    return ok;
}

int run_tests(void (*check)(int cond, const char *name))
{
    BBGame *g = &tg;
    int k, i, n;
    uint32_t h1;

    /* levels */
    for (k = 1; k <= BB_LEVELS; k++) {
        bb_new_game(g, 1, k);
        check(g->left > 0 && g->level == k, "every level has bricks to break");
    }
    bb_new_game(g, 1, 1);
    check(g->left == 32, "level 1: 32 bricks");
    check(g->cell[bb_index(0, 0, 2)] == BK_GREEN && g->cell[bb_index(0, 3, 2)] == BK_RED, "rows: the top line of the text is the top row");
    check(g->lives == BB_LIVES && g->stuck, "a new game: 3 lives, the ball on the paddle");
    bb_new_game(g, 1, 6);
    check(g->hits[bb_index(0, 3, 1)] == 3, "tough bricks take 3 hits from level 5");
    bb_new_game(g, 1, 2);
    check(g->hits[bb_index(0, 3, 2)] == 2, "and 2 before");

    /* serving */
    empty(g);
    bb_tick(g, 0);
    check(g->stuck && g->bx == g->px && g->bz > 0, "the ball rides the paddle until fire");
    bb_tick(g, BB_IN_RIGHT);
    check(g->px == BB_PAD_SPEED * BB_FX && g->bx == g->px, "the paddle moves, the ball with it");
    bb_tick(g, BB_IN_FIRE);
    check(!g->stuck && g->vz > 0 && count_events(g, BBE_SERVE) == 1, "fire serves the ball down the tunnel");
    for (k = 0; k < 100; k++) bb_tick(g, BB_IN_RIGHT);
    check(g->px == (BB_W - BB_PAD_W) * BB_FX, "the paddle stops at the wall");

    /* walls */
    empty(g);
    fly(g, BB_W - BB_R - 2, 0, 300, 8 * BB_FX, 0, 12 * BB_FX);
    bb_tick(g, 0);
    check(g->vx < 0 && g->bx <= (BB_W - BB_R) * BB_FX, "the side wall turns the ball");
    empty(g);
    fly(g, 0, -(BB_H - BB_R - 2), 300, 0, -8 * BB_FX, 12 * BB_FX);
    bb_tick(g, 0);
    check(g->vy > 0, "and the floor");
    empty(g);
    fly(g, 100, 0, BB_D - BB_R - 4, 0, 0, 16 * BB_FX);
    bb_tick(g, 0);
    check(g->vz < 0 && g->bz <= (BB_D - BB_R) * BB_FX, "the back wall sends it back");

    /* the paddle */
    empty(g);
    fly(g, 0, 0, 20, 0, 0, -16 * BB_FX);
    bb_tick(g, 0);
    check(g->vz > 0 && g->paddle_hits == 1 && g->lives == BB_LIVES, "the paddle returns the ball");
    empty(g);
    fly(g, 30, -20, 20, 0, 0, -16 * BB_FX);
    bb_tick(g, 0);
    check(g->vz > 0 && g->vx > 0 && g->vy < 0, "off-centre hits steer it that way");
    empty(g);
    fly(g, 120, 80, 20, 0, 0, -16 * BB_FX);
    for (k = 0; k < 10 && !g->stuck; k++) bb_tick(g, 0);
    check(g->stuck && g->lives == BB_LIVES - 1 && count_events(g, BBE_LOST) == 1, "a miss costs a life");
    g->lives = 1;
    fly(g, 120, 80, 20, 0, 0, -16 * BB_FX);
    for (k = 0; k < 10 && !g->over; k++) bb_tick(g, 0);
    check(g->over && count_events(g, BBE_OVER) == 1, "the last miss ends the game");

    /* bricks */
    empty(g);
    g->cell[bb_index(3, 2, 0)] = BK_RED; g->hits[bb_index(3, 2, 0)] = 1; g->left = 2;
    fly(g, 28, 30, BB_Z0 - BB_R - 10, 0, 0, 16 * BB_FX);
    for (k = 0; k < 4; k++) bb_tick(g, 0);
    check(g->cell[bb_index(3, 2, 0)] == BK_EMPTY && g->vz < 0 && g->score == 10, "a brick breaks and the ball bounces back");
    empty(g);
    g->cell[bb_index(3, 2, 0)] = BK_TOUGH; g->hits[bb_index(3, 2, 0)] = 2; g->left = 2;
    fly(g, 28, 30, BB_Z0 - BB_R - 10, 0, 0, 16 * BB_FX);
    for (k = 0; k < 4; k++) bb_tick(g, 0);
    check(g->cell[bb_index(3, 2, 0)] == BK_TOUGH && g->hits[bb_index(3, 2, 0)] == 1, "a tough brick survives one hit");
    empty(g);
    fly(g, -BB_W + 28, -BB_H + 30, BB_Z0 + 2 * BB_CD - BB_R - 10, 0, 0, 16 * BB_FX);
    for (k = 0; k < 4; k++) bb_tick(g, 0);
    check(g->cell[bb_index(0, 0, 2)] == BK_STEEL && g->vz < 0 && count_events(g, BBE_STEEL) == 1, "steel never breaks");
    empty(g);
    for (i = 0; i < 3; i++) { g->cell[bb_index(3, 2, i)] = BK_RED; g->hits[bb_index(3, 2, i)] = 1; }
    g->left = 4;
    g->plough_t = 100;
    fly(g, 28, 30, BB_Z0 - BB_R - 10, 0, 0, 16 * BB_FX);
    for (k = 0; k < 16; k++) bb_tick(g, 0);
    check(g->left == 1 && g->broken == 3, "the plough goes through a row of bricks");

    /* capsules */
    empty(g);
    g->cell[bb_index(2, 1, 0)] = BK_BONUS; g->hits[bb_index(2, 1, 0)] = 1; g->left = 2;
    fly(g, -BB_W + 2 * BB_CW + 28, -BB_H + BB_CH + 30, BB_Z0 - BB_R - 10, 0, 0, 16 * BB_FX);
    for (k = 0; k < 4; k++) bb_tick(g, 0);
    check(g->cap[0].active && g->caps_dropped == 1, "a bonus brick drops a capsule");
    g->stuck = 1;
    g->px = g->cap[0].x;
    g->py = g->cap[0].y;
    g->nev = 0;
    for (k = 0; k < 120 && g->cap[0].active; k++) bb_tick(g, 0);
    n = count_events(g, BBE_CAPSULE);
    check(!g->cap[0].active && n == 1 && g->score >= 150, "the paddle catches it");

    /* level flow */
    bb_new_game(g, 3, 1);
    bb_clear_all(g);
    g->nev = 0;
    bb_tick(g, 0);
    check(g->between == BB_BETWEEN && count_events(g, BBE_CLEAR) == 1, "the last brick clears the level");
    for (k = 0; k < BB_BETWEEN; k++) bb_tick(g, 0);
    check(g->level == 2 && g->left > 0 && g->stuck, "then the next level, the ball on the paddle");
    bb_new_game(g, 3, BB_LEVELS);
    bb_clear_all(g);
    for (k = 0; k <= BB_BETWEEN + 1; k++) bb_tick(g, 0);
    check(g->won, "clearing the last level wins");

    /* the autopilot */
    for (k = 1; k <= 3; k++) {
        int ok = autoplay(g, (uint32_t)k, 1, 25000, 1);
        check(ok, "autopilot: the ball stays in the tunnel and out of the bricks");
        check(g->left == 0, "autopilot: clears level 1");
    }
    check(autoplay(g, 11, 3, 30000, 1) && g->left == 0, "autopilot: clears level 3 (steel window)");

    /* determinism: the host and the Amiga must agree */
    autoplay(g, 1234, 1, BB_TRACE_TICKS, 0);
    h1 = bb_state_hash(g);
    autoplay(g, 1234, 1, BB_TRACE_TICKS, 0);
    check(bb_state_hash(g) == h1, "the same seed plays the same game");
    check(h1 == BB_TRACE_HASH, "the long autopilot game matches the host's (tests/vectors.h)");
    return 0;
}

/* `make vectors`: the long game's hash and how it went */
#ifdef BB_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    autoplay(&tg, 1234, 1, BB_TRACE_TICKS, 0);
    printf("/* make vectors: seed 1234 from level 1, %d ticks: level %d, score %ld, lives %d, broken %d */\n",
           BB_TRACE_TICKS, tg.level, (long)tg.score, tg.lives, tg.broken);
    printf("#define BB_TRACE_HASH 0x%08lXUL\n", (unsigned long)bb_state_hash(&tg));
    return 0;
}
#endif
