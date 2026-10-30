// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Ridge Runner's tests: the ground and the course, the
 * throttle, brake and boost, steering, rough ground, hovering and flying
 * off crests, gates (made and missed), bumping a rival, the clock, the
 * finish and places, qualifying and the track flow, the autopilot racing,
 * and a long autopilot race whose final state hash (tests/vectors.h, from
 * `make vectors` on the host) the Amiga must reproduce exactly. On the
 * host (make host-test), on the Amiga (ridge_runner_test) and inside the
 * game (the selftest hook).
 */
#include <string.h>
#include "../rr.h"
#define RR_TRACE_TICKS 9000             /* six minutes of racing */
#include "vectors.h"

const char test_client[] = "ridge_runner_test";
static RRGame tg;                       /* off the Amiga's stack */

static int count_events(RRGame *g, int type)
{
    RREvent e;
    int n = 0;
    while (rr_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

/* flat calm: no hills, a straight course, the rivals parked far behind */
static void flat(RRGame *g)
{
    int k;
    rr_new_game(g, 5, 1);
    g->a1 = g->a2 = g->a3 = 0;
    g->c1 = g->c2 = 0;
    g->x = 0;
    g->y = FROMI(RR_HOVER);
    for (k = 0; k < RR_RIVALS; k++) { g->rv[k].z = -FROMI(5000); g->rv[k].pace = 0; }
    g->nev = 0;
}

static int race(RRGame *g, uint32_t seed, int track, int max)
{
    int t;
    rr_new_game(g, seed, track);
    for (t = 0; t < max && !g->finished && !g->over; t++) rr_tick(g, rr_bot(g));
    return g->finished;
}

int run_tests(void (*check)(int cond, const char *name))
{
    RRGame *g = &tg;
    int k, ok;
    int32_t c;
    fix v0;
    uint32_t h1;

    /* the ground and the course */
    for (ok = 1, k = 1; k <= RR_TRACKS; k++) {
        rr_new_game(g, 3, k);
        if (!rr_on_track(g, g->x >> 16, 0) || g->y != (rr_height(g, g->x >> 16, 0) + RR_HOVER) * FONE) ok = 0;
    }
    check(ok, "every track: the bike starts on the course, hovering");
    rr_new_game(g, 3, 1);
    c = rr_centre(g, 4000);
    check(rr_height(g, c + 300, 4000) > rr_height(g, c, 4000) + 100, "the valley's sides rise steeply");
    check(rr_on_track(g, c + 60, 4000) && !rr_on_track(g, c + 80, 4000), "the track is 140 wide");
    {
        int32_t lo = 0, hi = 0;
        for (k = 0; k < RR_LEN; k += 100) { c = rr_centre(g, k); if (c < lo) lo = c; if (c > hi) hi = c; }
        check(hi - lo > 300, "the course winds");
    }

    /* throttle, brake, boost */
    flat(g);
    for (k = 0; k < 10; k++) rr_tick(g, RR_IN_UP);
    check(g->v == 10 * FX(0.3), "the throttle builds speed");
    for (k = 0; k < 60; k++) rr_tick(g, RR_IN_UP);
    check(g->v == RR_VMAX, "up to top speed");
    check(g->z > FROMI(500) && g->x == 0, "straight down the course");
    for (k = 0; k < 4; k++) rr_tick(g, RR_IN_UP | RR_IN_FIRE);
    check(g->v > RR_VMAX && g->boosting && g->energy < RR_ENERGY, "the boost goes faster and uses energy");
    for (k = 0; k < 200 && g->boosting; k++) rr_tick(g, RR_IN_UP | RR_IN_FIRE);
    check(g->energy < FX(0.5) && !g->boosting && k > 70, "until the energy runs out (about 3 s)");
    for (k = 0; k < 40; k++) rr_tick(g, RR_IN_UP | RR_IN_FIRE);
    check(!g->boosting && g->energy >= FX(10) && g->energy < RR_ENERGY / 5, "held, it stays off while it charges (no flicker)");
    for (k = 0; k < 60 && !g->boosting; k++) rr_tick(g, RR_IN_UP | RR_IN_FIRE);
    check(g->boosting && g->energy < RR_ENERGY / 5 + FX(1), "and comes back with a fifth of a charge");
    for (k = 0; k < 120; k++) rr_tick(g, RR_IN_UP);
    v0 = g->v;
    for (k = 0; k < 5; k++) rr_tick(g, RR_IN_DOWN);
    check(g->v == v0 - 5 * FX(0.8), "the brake");
    for (k = 0; k < 100; k++) rr_tick(g, 0);
    check(g->v < v0 / 2, "with nothing pressed it coasts to a stop");

    /* steering and rough ground */
    flat(g);
    for (k = 0; k < 30; k++) rr_tick(g, RR_IN_UP);
    for (k = 0; k < 10; k++) rr_tick(g, RR_IN_UP | RR_IN_RIGHT);
    check(g->yaw > 2000 && g->yaw < 8000, "RIGHT turns right");
    for (k = 0; k < 60; k++) rr_tick(g, RR_IN_UP);
    check(g->x > FROMI(RR_TRACK_HW) && g->offtrack, "and off the track");
    check(g->v <= RR_VMAX * 3 / 5 + FX(0.5), "where the rough ground holds the bike to 60%");

    /* hovering and flying */
    flat(g);
    check(g->y == FROMI(RR_HOVER), "on flat ground it hovers 14 up");
    ok = race(g, 7, 2, 9000);
    check(ok && g->jumps > 3, "over the crests it flies (a race has jumps)");

    /* gates */
    flat(g);
    g->time_left = 20 * 25;
    for (k = 0; k < 200 && g->gate == 0; k++) rr_tick(g, RR_IN_UP);
    check(g->gates_hit == 1 && g->score == 100, "between the pylons: a gate");
    check(g->time_left > 20 * 25, "which adds time");
    flat(g);
    g->x = FROMI(RR_GATE_HW + 20);
    g->time_left = 20 * 25;
    for (k = 0; k < 400 && g->gate == 0; k++) rr_tick(g, RR_IN_UP);
    check(g->missed == 1 && g->gates_hit == 0 && g->time_left < 20 * 25, "outside them: missed, and time lost");

    /* a rival */
    flat(g);
    for (k = 0; k < 30; k++) rr_tick(g, RR_IN_UP);
    g->rv[0].z = g->z + FROMI(20);
    g->rv[0].x = g->x + FROMI(8);
    g->rv[0].v = 0;
    g->nev = 0;
    v0 = g->v;
    rr_tick(g, RR_IN_UP);
    check(count_events(g, RRE_BUMP) == 1 && g->v < v0 && g->x < 0, "bumping a rival slows you and pushes you aside");

    /* the clock, the finish, places */
    flat(g);
    g->time_left = 3;
    for (k = 0; k < 5; k++) rr_tick(g, 0);
    check(g->over && count_events(g, RRE_TIMEOUT) == 1, "out of time: over");
    flat(g);
    g->z = FROMI(RR_LEN - 30);
    g->v = RR_VMAX;
    rr_tick(g, RR_IN_UP);
    rr_tick(g, RR_IN_UP);
    rr_tick(g, RR_IN_UP);
    check(g->finished && g->place == 1 && g->qualified, "over the line first: 1st, qualified");
    for (k = 0; k < RR_BETWEEN; k++) rr_tick(g, 0);
    check(g->track == 2 && !g->finished && g->z == 0, "then the next track");
    flat(g);
    for (k = 0; k < RR_RIVALS; k++) g->rv[k].finished = ++g->nfinished;
    g->z = FROMI(RR_LEN - 5);
    g->v = RR_VMAX;
    rr_tick(g, RR_IN_UP);
    check(g->finished && g->place == 4 && !g->qualified, "fourth doesn't qualify");
    for (k = 0; k < RR_BETWEEN; k++) rr_tick(g, 0);
    check(g->over && !g->won, "and the game is over");
    rr_new_game(g, 3, RR_TRACKS);
    g->z = FROMI(RR_LEN - 5);
    g->v = RR_VMAX;
    rr_tick(g, RR_IN_UP);
    for (k = 0; k < RR_BETWEEN; k++) rr_tick(g, 0);
    check(g->won, "qualifying on the last track wins");
    rr_new_game(g, 3, 1);
    for (k = 0; k < 400; k++) rr_tick(g, 0);
    check(rr_place_now(g) == 4, "standing still, everyone passes you");

    /* the autopilot */
    for (k = 1; k <= 3; k++) {
        ok = race(g, (uint32_t)k, 1, 9000);
        check(ok && g->place == 1 && g->missed == 0, "autopilot: wins track 1, every gate made");
    }
    ok = race(g, 4, 4, 9000);
    check(ok && g->qualified, "autopilot: qualifies on track 4");

    /* determinism: the host and the Amiga must agree */
    race(g, 1234, 3, RR_TRACE_TICKS);
    h1 = rr_state_hash(g);
    race(g, 1234, 3, RR_TRACE_TICKS);
    check(rr_state_hash(g) == h1, "the same seed races the same race");
    check(h1 == RR_TRACE_HASH, "the long autopilot race matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef RR_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    race(&tg, 1234, 3, RR_TRACE_TICKS);
    printf("/* make vectors: seed 1234 on track 3: place %d, score %ld, gates %d, jumps %d, bumps %d, %ld ticks */\n",
           tg.place, (long)tg.score, tg.gates_hit, tg.jumps, tg.bumps, (long)tg.race_ticks);
    printf("#define RR_TRACE_HASH 0x%08lXUL\n", (unsigned long)rr_state_hash(&tg));
    return 0;
}
#endif
