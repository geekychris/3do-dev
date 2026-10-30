// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * rr.h - Ridge Runner's game logic: a hover-bike race down a winding
 * valley. The ground is a sum of waves (rolling hills, and shorter bumps
 * that throw the bike into the air over their crests); the course runs
 * along the valley floor, whose sides rise steeply away from it. Gates
 * every 800 units must be passed between their pylons; each adds time to
 * the clock. Three rivals race the same course; finish in the first three
 * to go on to the next track.
 *
 * x across, y up, z along the course; positions 16.16 fixed point world
 * units, one tick 40 ms. No Amiga headers: tested on the host too.
 */
#ifndef RR_GAME_H
#define RR_GAME_H

#include <stdint.h>
#include "a3d_fix.h"

#define RR_TRACKS    6
#define RR_RIVALS    3
#define RR_LEN       16000              /* the course's length */
#define RR_GATE_GAP  800
#define RR_GATES     (RR_LEN / RR_GATE_GAP)
#define RR_TRACK_HW  70                 /* the track's half width */
#define RR_GATE_HW   90                 /* between a gate's pylons */
#define RR_SHOULDER  150                /* rough flat ground out to here, then the valley's sides */
#define RR_HOVER     14
#define RR_VMAX      FX(13)             /* units a tick */
#define RR_VBOOST    FX(19)
#define RR_ENERGY    FX(100)
#define RR_BETWEEN   75

/* input bits */
#define RR_IN_LEFT  1
#define RR_IN_RIGHT 2
#define RR_IN_UP    4                   /* throttle */
#define RR_IN_DOWN  8                   /* brake */
#define RR_IN_FIRE  16                  /* boost */

enum { RRE_NONE, RRE_GATE, RRE_MISS, RRE_JUMP, RRE_LAND, RRE_BUMP, RRE_RIVAL_FINISH, RRE_FINISH,
       RRE_QUALIFY, RRE_FAIL, RRE_TRACK, RRE_TIMEOUT, RRE_OVER, RRE_WIN, RRE_COUNT };
typedef struct { int type, a, b; } RREvent;
#define RR_MAX_EVENTS 16

typedef struct {
    fix x, z, v;                        /* across, along, speed */
    fix pace;                           /* its usual speed */
    int weave;                          /* its line's phase */
    int finished;                       /* 0, or its place */
} Rival;

typedef struct {
    uint32_t seed, rng;
    int track, over, won, between, finished, place, qualified;
    fix x, y, z, v, vy;                 /* the player's bike */
    int yaw, steer;                     /* heading (16-bit angle; 0 = +z), the last steer for the lean */
    int air;                            /* ticks in the air */
    fix energy;                         /* boost left */
    int boosting, offtrack;
    int gate;                           /* the next gate */
    int gates_hit, missed, bumps, jumps;
    int32_t time_left, tick, race_ticks;
    int32_t score;
    int nfinished;                      /* bikes over the line */
    int bump_timer;                     /* ticks before another bump counts */
    Rival rv[RR_RIVALS];
    /* the track's shape */
    int32_t c1, c2, ph1, ph2, a1, a2, a3, hph;
    unsigned prev_in;
    RREvent ev[RR_MAX_EVENTS];
    int nev;
} RRGame;

extern const char *const rr_event_names[RRE_COUNT];
extern const char *const rr_track_names[RR_TRACKS];

uint32_t rr_rand(RRGame *g);
void rr_new_game(RRGame *g, uint32_t seed, int track);
void rr_load_track(RRGame *g, int track);
void rr_tick(RRGame *g, unsigned in);
int  rr_pop_event(RRGame *g, RREvent *e);
unsigned rr_bot(const RRGame *g);       /* the autopilot's input */
uint32_t rr_state_hash(const RRGame *g);

int32_t rr_centre(const RRGame *g, int32_t z);      /* the course's middle at z (world units) */
int32_t rr_height(const RRGame *g, int32_t x, int32_t z);
int  rr_place_now(const RRGame *g);     /* 1 = leading */
int  rr_on_track(const RRGame *g, int32_t x, int32_t z);

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
