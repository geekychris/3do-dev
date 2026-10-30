// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Orbit Docking's tests: the levels, thrusters and roll in
 * free space, fuel, the slot's roll, a clean dock, every way of crashing
 * (off the axis, too fast, drifting, rolled wrong, not turning with it,
 * the hub's side, debris, drifting away), lives, the level flow, the
 * autopilot docking, and a long autopilot game whose final state hash
 * (tests/vectors.h, from `make vectors` on the host) the Amiga must
 * reproduce exactly. On the host (make host-test), on the Amiga
 * (orbit_docking_test) and inside the game (the selftest hook).
 */
#include <string.h>
#include "../od.h"
#define OD_TRACE_TICKS 6000             /* four minutes of flying */
#include "vectors.h"

const char test_client[] = "orbit_docking_test";
static ODGame tg;                       /* off the Amiga's stack */

static int count_events(ODGame *g, int type)
{
    ODEvent e;
    int n = 0;
    while (od_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

/* lined up just short of the port, closing gently, rolled and turning
 * with the slot */
static void lined_up(ODGame *g, int level)
{
    od_new_game(g, 3, level);
    g->x = g->y = 0;
    g->vx = g->vy = 0;
    g->z = FROMI(OD_PORT_Z + OD_NOSE + 1);
    g->vz = -FX(1);
    g->roll = g->st_angle;
    g->roll_rate = g->st_spin;
    g->nev = 0;
}

/* the last crash's reason after one tick */
static int crash_why(ODGame *g)
{
    ODEvent e;
    int why = 0;
    od_tick(g, 0);
    while (od_pop_event(g, &e)) if (e.type == ODE_CRASH || e.type == ODE_DEBRIS) why = e.a;
    return why;
}

static int fly(ODGame *g, uint32_t seed, int level, int max)
{
    int t;
    ODEvent e;
    od_new_game(g, seed, level);
    for (t = 0; t < max && !g->over; t++) {
        od_tick(g, od_bot(g));
        while (od_pop_event(g, &e)) if (e.type == ODE_DOCK) return 1;
    }
    return 0;
}

int run_tests(void (*check)(int cond, const char *name))
{
    ODGame *g = &tg;
    int k, ok;
    fix f0;
    int32_t s0;
    uint32_t h1;

    /* the levels */
    for (ok = 1, k = 1; k <= OD_LEVELS; k++) {
        od_new_game(g, 3, k);
        if (g->z != FROMI(OD_START_Z) || g->fuel <= 0 || g->st_spin == 0 || g->lives != OD_LIVES) ok = 0;
    }
    check(ok, "every level: start 900 out, fuelled, the station spinning");
    od_new_game(g, 3, 1);
    check(g->x == 0 && g->y == 0 && g->ndebris == 0, "level 1: on the axis, no debris");
    od_new_game(g, 3, 6);
    check(g->st_spin < 0 && g->ndebris == 3 && fhypot(g->x, g->y) > FROMI(150), "level 6: spinning the other way, debris, well off the axis");

    /* thrusters, in free space */
    od_new_game(g, 3, 1);
    g->roll = 0;
    f0 = g->fuel;
    for (k = 0; k < 10; k++) od_tick(g, OD_IN_RIGHT);
    check(g->vx == -10 * OD_ACC && g->vy == 0 && g->fuel == f0 - FROMI(10), "a thruster builds speed and burns fuel (right, looking back at the station, is -x)");
    for (k = 0; k < 20; k++) od_tick(g, 0);
    check(g->vx == -10 * OD_ACC && g->x < -FROMI(9), "let go, it carries on: no friction in space");
    for (k = 0; k < 10; k++) od_tick(g, OD_IN_LEFT);
    check(g->vx == 0, "only the opposite thruster stops it");
    for (k = 0; k < 5; k++) od_tick(g, OD_IN_FWD | OD_IN_UP);
    check(g->vz < 0 && g->vy > 0, "forward closes on the station; up goes up");
    od_new_game(g, 3, 1);
    g->roll = 0x4000 << 8;              /* rolled a quarter turn */
    for (k = 0; k < 10; k++) od_tick(g, OD_IN_RIGHT);
    check(g->vy < -9 * OD_ACC && g->vx > -OD_ACC && g->vx < OD_ACC, "rolled a quarter turn, the craft's right is the station's -y");
    s0 = g->roll_rate;
    f0 = g->fuel;
    for (k = 0; k < 4; k++) od_tick(g, OD_IN_ROLL_R);
    check(g->roll_rate == s0 + 4 * OD_ROLL_ACC && g->fuel == f0 - 2 * FONE, "rolling: the turn builds; half the fuel");
    od_new_game(g, 3, 1);
    g->fuel = FONE;
    g->roll = 0;
    g->nev = 0;
    for (k = 0; k < 5; k++) od_tick(g, OD_IN_RIGHT);
    check(g->vx == -OD_ACC && g->fuel == 0 && count_events(g, ODE_FUEL_OUT) == 1, "out of fuel, the thrusters stop");

    /* the slot */
    od_new_game(g, 3, 1);
    g->roll = g->st_angle;
    check(od_roll_error(g) == 0, "rolled with the slot: no error");
    g->roll = (g->st_angle + (0x8000 << 8)) & 0xFFFFFF;
    check(od_roll_error(g) == 0, "a half turn round is the same slot");
    g->roll = (g->st_angle + (0x2000 << 8)) & 0xFFFFFF;
    check(od_roll_error(g) == 0x2000, "an eighth of a turn off");

    /* docking */
    lined_up(g, 1);
    od_tick(g, 0);
    check(g->docks == 1 && g->between == OD_BETWEEN && g->score >= 1000 && count_events(g, ODE_DOCK) == 1, "lined up and gentle: docked");
    for (k = 0; k < OD_BETWEEN; k++) od_tick(g, 0);
    check(g->level == 2 && g->z == FROMI(OD_START_Z), "then the next level");
    lined_up(g, OD_LEVELS);
    od_tick(g, 0);
    for (k = 0; k < OD_BETWEEN; k++) od_tick(g, 0);
    check(g->won, "docking at the last station wins");

    /* crashing */
    lined_up(g, 1); g->x = FROMI(30);
    check(crash_why(g) == 1, "off the axis: crash");
    check(g->lives == OD_LIVES - 1 && g->z == FROMI(OD_START_Z), "a life lost, back to the start");
    lined_up(g, 1); g->vz = -FX(2.5);
    check(crash_why(g) == 2, "too fast: crash");
    lined_up(g, 1); g->vx = FX(1.2);
    check(crash_why(g) == 3, "drifting across: crash");
    lined_up(g, 1); g->roll = (g->st_angle + (4000 << 8)) & 0xFFFFFF;
    check(crash_why(g) == 4, "rolled 22 degrees off the slot: crash");
    lined_up(g, 1); g->roll_rate = g->st_spin + (40 << 8);
    check(crash_why(g) == 5, "not turning with it: crash");
    lined_up(g, 1); g->x = FROMI(OD_HUB_R + 5);
    od_tick(g, 0);
    check(crash_why(g) == 6, "past the face, into the hub's side: crash");
    od_new_game(g, 3, 4);
    g->z = FROMI(g->debris[0].z);
    g->y = FROMI(g->debris[0].y);
    g->x = od_debris_x(g, 0);
    check(crash_why(g) == 7, "into debris: crash");
    od_new_game(g, 3, 1);
    g->vz = FX(10);
    for (k = 0; k < 80 && g->lives == OD_LIVES; k++) od_tick(g, 0);
    check(g->lives == OD_LIVES - 1, "drifting away into space: lost");
    od_new_game(g, 3, 1);
    for (k = 0; k < OD_LIVES; k++) { lined_up(g, 1); g->lives = OD_LIVES - k; g->x = FROMI(30); od_tick(g, 0); }
    check(g->over && count_events(g, ODE_OVER) == 1, "the last life lost: over");

    /* the autopilot */
    for (k = 1; k <= 3; k++) check(fly(g, (uint32_t)k, 1, 3000) && g->crashes == 0, "autopilot: docks at station 1");
    check(fly(g, 1, 3, 3000) && g->crashes == 0, "autopilot: docks at station 3 (from well off the axis)");
    check(fly(g, 1, 4, 4000) && g->crashes == 0, "autopilot: docks at station 4 (through debris)");

    /* determinism: the host and the Amiga must agree */
    od_new_game(g, 1234, 1);
    for (k = 0; k < OD_TRACE_TICKS && !g->over && !g->won; k++) od_tick(g, od_bot(g));
    h1 = od_state_hash(g);
    check(g->docks >= 3, "autopilot: a long game docks again and again");
    check(h1 == OD_TRACE_HASH, "the long autopilot game matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef OD_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    int k;
    od_new_game(&tg, 1234, 1);
    for (k = 0; k < OD_TRACE_TICKS && !tg.over && !tg.won; k++) od_tick(&tg, od_bot(&tg));
    printf("/* make vectors: seed 1234 from level 1, %d ticks: level %d, docks %d, crashes %d, score %ld%s */\n",
           k, tg.level, tg.docks, tg.crashes, (long)tg.score, tg.won ? ", won" : tg.over ? ", over" : "");
    printf("#define OD_TRACE_HASH 0x%08lXUL\n", (unsigned long)od_state_hash(&tg));
    return 0;
}
#endif
