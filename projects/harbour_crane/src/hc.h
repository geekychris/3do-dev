// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * hc.h - Harbour Crane's game logic. A gantry crane runs from the quay out
 * over a ship. Its trolley moves along the gantry, its winch lets the hook
 * up and down, and whatever hangs on the hook swings as a pendulum (pushed
 * by the trolley's acceleration and by the wind). Containers wait on the
 * quay; the player lifts each one and lands it on the ship's deck, in six
 * slots, stacked up to four high. A container lands where it is let go:
 * dropped from high up it is dented, landed across uneven stacks or off
 * the end of the deck it is lost in the harbour.
 *
 * The ship rolls in the swell and lists towards its heavy side; the higher
 * the stacks, the more it rolls. Too far and it capsizes.
 *
 * Positions are 16.16 fixed point world units (y up); one tick is 40 ms.
 * No Amiga headers: tested on the host too.
 */
#ifndef HC_GAME_H
#define HC_GAME_H

#include <stdint.h>
#include "a3d_fix.h"

#define HC_SLOTS     6
#define HC_TIERS     4
#define HC_MAXBOX    16
#define HC_LEVELS    8
#define HC_GANTRY_Y  320                /* the trolley's rail */
#define HC_TX_MIN    (-220)
#define HC_TX_MAX    430
#define HC_L_MIN     20                 /* the cable */
#define HC_L_MAX     330
#define HC_QUAY_X1   40                 /* the quay: x < 40, its top at y = 0 */
#define HC_PICK_X    (-140)             /* where the next container waits */
#define HC_WATER_Y   (-70)
#define HC_SHIP_X0   100                /* the deck's slots: 6 x 50 from x = 100 */
#define HC_SLOT_W    50
#define HC_DECK_Y    (-40)
#define HC_PIVOT_X   250                /* the ship rolls about this point */
#define HC_PIVOT_Y   (-60)
#define HC_HULL_X0   70
#define HC_HULL_X1   430
#define HC_BOX_H     30                 /* a container: 48 or 98 long, 30 high */
#define HC_SNAP      14                 /* how far off a slot's edge still lands square */
#define HC_DENT_FALL 45                 /* dropped further than this: dented */
#define HC_CAPSIZE   FX(0.30)           /* radians of heel */
#define HC_MAX_LOST  3
#define HC_BETWEEN   75

/* input bits */
#define HC_IN_LEFT  1
#define HC_IN_RIGHT 2
#define HC_IN_UP    4                   /* winch in */
#define HC_IN_DOWN  8                   /* winch out */
#define HC_IN_FIRE  16                  /* grab / let go */

enum { HCE_NONE, HCE_GRAB, HCE_RELEASE, HCE_LAND, HCE_DENT, HCE_LOST, HCE_BUMP, HCE_SPLASH,
       HCE_CLEAR, HCE_LEVEL, HCE_CAPSIZE, HCE_TIMEOUT, HCE_OVER, HCE_WIN, HCE_GUST, HCE_COUNT };
typedef struct { int type, a, b; } HCEvent;
#define HC_MAX_EVENTS 16

typedef struct { int len, weight; } HCBox;          /* len 1 or 2 slots; weight 1-4 */
typedef struct { int box, slot, tier; } HCPlaced;

typedef struct {
    uint32_t seed, rng;
    int level, over, won, between, capsized;
    fix tx, tv;                         /* the trolley: x, speed (units a tick) */
    fix L, th, om;                      /* the cable's length, its angle from straight down, and how fast that turns */
    fix ldot;                           /* how fast the winch is moving */
    int carrying;                       /* the box on the hook, or -1 */
    HCBox box[HC_MAXBOX];
    int nbox, next;                     /* containers this level; the next on the quay */
    int8_t height[HC_SLOTS];            /* tiers in each slot */
    HCPlaced placed[HC_MAXBOX];
    int nplaced;
    int falling;                        /* a box let go, on its way down: its index, or -1 */
    fix fx, fy, fvx, fvy, fy0;          /* its left end and bottom, its speed, where it was let go */
    unsigned prev_in;                   /* last tick's input (fire acts on a press) */
    fix wind, gust;                     /* wind: units a tick^2 on the load */
    int gust_timer;
    fix swell, phase, list, heel, bob;  /* the ship */
    int loaded, lost, dents, bumps, total_loaded;
    int32_t score;
    int time_left, tick;
    HCEvent ev[HC_MAX_EVENTS];
    int nev;
} HCGame;

extern const char *const hc_event_names[HCE_COUNT];

uint32_t hc_rand(HCGame *g);
void hc_new_game(HCGame *g, uint32_t seed, int level);
void hc_load_level(HCGame *g, int level);
void hc_tick(HCGame *g, unsigned in);
int  hc_pop_event(HCGame *g, HCEvent *e);
unsigned hc_bot(const HCGame *g);       /* the autopilot's input */
uint32_t hc_state_hash(const HCGame *g);

/* where things are */
fix  hc_hook_x(const HCGame *g);
fix  hc_hook_y(const HCGame *g);        /* the hook, and a carried box's top middle */
int  hc_box_w(const HCGame *g, int b);  /* in world units */
fix  hc_support_y(const HCGame *g, fix x0, fix x1, int with_pick);   /* the highest thing under x0..x1 */
int  hc_slot_top(const HCGame *g, int slot);   /* the stack's top, ship's coordinates */
fix  hc_moment(const HCGame *g, int extra_box, int extra_slot);      /* the load's moment about the middle */
int  hc_weight(const HCGame *g);
fix  hc_list_for(const HCGame *g, fix moment, int weight);
int  hc_best_slot(const HCGame *g, int b);     /* the autopilot's choice, or -1 */
int  hc_fits(const HCGame *g, int b, int slot);

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
