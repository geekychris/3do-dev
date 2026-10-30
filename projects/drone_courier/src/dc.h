// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * dc.h - Drone Courier's game logic. A city of towers on a grid of
 * blocks; a delivery drone. Each job: fly to the depot (a blue pad on a
 * rooftop) and land to collect a parcel, then land it on the customer's
 * rooftop pad (green), against the clock. Landing means touching down on
 * the pad gently: under 2 a tick down and 1.5 across. Clip a tower and the
 * drone bounces off - with a fragile parcel aboard, hard enough breaks it.
 * The battery drains as you fly, faster climbing, and fills at the depot.
 * The wind blows, in gusts from day 2.
 *
 * A day is a run of jobs; three failed jobs (a broken parcel or the clock
 * running out) or a flat battery end the game.
 *
 * x east, y up, z north; 16.16 fixed point world units; angles 16-bit
 * (0 = +z, a quarter turn = +x); one tick 40 ms. No Amiga headers: tested
 * on the host too.
 */
#ifndef DC_GAME_H
#define DC_GAME_H

#include <stdint.h>
#include "a3d_fix.h"

#define DC_N         12                 /* blocks each way */
#define DC_PITCH     160                /* a block and its street */
#define DC_FOOT      55                 /* a tower's half width */
#define DC_HALF      (DC_N * DC_PITCH / 2)
#define DC_DAYS      5
#define DC_MAX_FAILS 3
#define DC_CEILING   600
#define DC_VMAX      FX(8)
#define DC_LAND_DOWN FX(2)
#define DC_LAND_SIDE FX(1.5)
#define DC_BETWEEN   75

/* input bits */
#define DC_IN_LEFT   1                  /* turn */
#define DC_IN_RIGHT  2
#define DC_IN_UP     4                  /* forward */
#define DC_IN_DOWN   8                  /* back */
#define DC_IN_CLIMB  16

enum { DCE_NONE, DCE_PICKUP, DCE_DELIVER, DCE_BUMP, DCE_BROKEN, DCE_HARD, DCE_LATE, DCE_GUST, DCE_FLAT,
       DCE_DAY, DCE_OVER, DCE_WIN, DCE_COUNT };
typedef struct { int type, a, b; } DCEvent;
#define DC_MAX_EVENTS 16

typedef struct {
    uint32_t seed, rng;
    int day, over, won, between;
    int16_t height[DC_N][DC_N];         /* the towers; 0 = a little square */
    fix x, y, z, vx, vy, vz;
    int yaw;
    int carrying, fragile;              /* a parcel aboard; it breaks if knocked */
    int depot, target;                  /* blocks (row * DC_N + col) */
    int jobs, jobs_done, fails;
    int pad_r;                          /* the pads' radius today */
    fix battery;                        /* 0..100 */
    fix wind_x, wind_z, gust_x, gust_z;
    int gust_timer;
    int32_t job_time, tick, score;
    int landed;                         /* sitting on a roof or the ground */
    int bumps, breakages;
    DCEvent ev[DC_MAX_EVENTS];
    int nev;
} DCGame;

extern const char *const dc_event_names[DCE_COUNT];

void dc_new_game(DCGame *g, uint32_t seed, int day);
void dc_load_day(DCGame *g, int day);
void dc_tick(DCGame *g, unsigned in);
int  dc_pop_event(DCGame *g, DCEvent *e);
unsigned dc_bot(const DCGame *g);       /* the autopilot's input */
uint32_t dc_state_hash(const DCGame *g);

fix  dc_block_x(int col);               /* a block's middle */
fix  dc_block_z(int row);
int  dc_block_at(fix x, fix z);         /* the block whose tower (x, z) is over, or -1 (a street) */
int  dc_roof(const DCGame *g, int b);   /* a tower's roof height */
fix  dc_ground(const DCGame *g, fix x, fix z);   /* what is under (x, z) */
int  dc_goal(const DCGame *g);          /* where to now: the depot or the customer */

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
