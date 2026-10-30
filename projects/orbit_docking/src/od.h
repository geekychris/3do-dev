// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * od.h - Orbit Docking's game logic. A station spins about its long axis;
 * its docking port is a slot at the end of its hub, on that axis, turning
 * with it. You fly a small craft that always faces the station: thrusters
 * move it across (x, y) and along (z) the axis, and roll it. There is no
 * friction in space - a push lasts until you push back. The thrusters
 * across are the craft's own: right is right in the (rolled) view.
 *
 * To dock: come in along the axis, rolled to match the slot and turning
 * as fast as it turns, slowly. At contact the craft must be within 14 of
 * the axis, closing at under 1.6 a tick, drifting across at under 0.8,
 * rolled within 8 degrees of the slot and turning within 7 degrees a
 * second of it. Anything else is a crash. Fuel is limited; from level 4
 * debris drifts across the approach.
 *
 * The station at the origin, its axis along z, the port facing +z;
 * positions 16.16 fixed point world units; angles 16-bit (65536 a turn)
 * with 8 more bits of fraction (a8); one tick 40 ms. No Amiga headers:
 * tested on the host too.
 */
#ifndef OD_GAME_H
#define OD_GAME_H

#include <stdint.h>
#include "a3d_fix.h"

#define OD_LEVELS    6
#define OD_LIVES     3
#define OD_PORT_Z    60                 /* the hub's face */
#define OD_HUB_R     60
#define OD_NOSE      20                 /* the craft's nose ahead of its middle */
#define OD_START_Z   900
#define OD_MAX_DEBRIS 3
#define OD_ACC       FX(0.04)           /* a thruster: units a tick^2 */
#define OD_ROLL_ACC  (6 << 8)           /* a8 units a tick^2 */
#define OD_TOL_AXIS  14                 /* the docking limits */
#define OD_TOL_CLOSE FX(1.6)
#define OD_TOL_DRIFT FX(0.8)
#define OD_TOL_ROLL  1456               /* 8 degrees */
#define OD_TOL_SPIN  (20 << 8)          /* 7 degrees a second, a8 a tick */
#define OD_BETWEEN   75

/* input bits */
#define OD_IN_LEFT   1
#define OD_IN_RIGHT  2
#define OD_IN_UP     4
#define OD_IN_DOWN   8
#define OD_IN_FWD    16                 /* towards the station */
#define OD_IN_BACK   32
#define OD_IN_ROLL_L 64
#define OD_IN_ROLL_R 128

enum { ODE_NONE, ODE_DOCK, ODE_CRASH, ODE_DEBRIS, ODE_FUEL_OUT, ODE_LEVEL, ODE_OVER, ODE_WIN, ODE_LOST, ODE_COUNT };
typedef struct { int type, a, b; } ODEvent;
#define OD_MAX_EVENTS 12

typedef struct { int32_t z, y, amp, period, phase; } Debris;   /* across x: amp * sin(2 pi t / period + phase) */

typedef struct {
    uint32_t seed, rng;
    int level, lives, over, won, between;
    fix x, y, z, vx, vy, vz;            /* the craft */
    int32_t roll, roll_rate;            /* a8 */
    int32_t st_angle, st_spin;          /* the station's turn and spin, a8 */
    fix fuel, fuel_max;
    Debris debris[OD_MAX_DEBRIS];
    int ndebris;
    int crashes, docks;
    int32_t tick, level_ticks;
    int32_t score;
    int last_fail;                      /* why the last crash: 1 off axis, 2 too fast, 3 drifting, 4 rolled wrong, 5 spin, 6 the hub's side, 7 debris */
    ODEvent ev[OD_MAX_EVENTS];
    int nev;
} ODGame;

extern const char *const od_event_names[ODE_COUNT];
extern const char *const od_fail_names[8];

void od_new_game(ODGame *g, uint32_t seed, int level);
void od_load_level(ODGame *g, int level);
void od_tick(ODGame *g, unsigned in);
int  od_pop_event(ODGame *g, ODEvent *e);
unsigned od_bot(const ODGame *g);       /* the autopilot's input */
uint32_t od_state_hash(const ODGame *g);
int  od_roll_error(const ODGame *g);    /* the craft's roll against the slot, -16384..16383 (the slot looks the same turned half round) */
fix  od_debris_x(const ODGame *g, int k);

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
