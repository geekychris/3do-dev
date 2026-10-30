// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * cr.h - Canyon Rail's game logic: a rail shooter. The camera rides a
 * fixed winding path down a canyon at a steady speed; you move a sight
 * about the view and fire. Turrets on the canyon walls and drones
 * swooping across fire bolts at you; shoot the bolts down too, or your
 * shield takes them. Fuel pods top the shield up when shot. Hits in a row
 * build a multiplier (up to x8); a miss or a hit taken resets it. At the
 * end of each stage the ride stops before a guardian: four nodes orbiting
 * its core, each to be shot out. Three stages.
 *
 * Where the sight points is found with the engine's own camera (a3d.h has
 * no Amiga headers), so hits are decided the same way they are drawn.
 *
 * x across, y up, z along the canyon; world units are 16.16 fixed point
 * where they move, whole otherwise; one tick 40 ms. No Amiga headers:
 * tested on the host too.
 */
#ifndef CR_GAME_H
#define CR_GAME_H

#include <stdint.h>
#include "a3d.h"
#include "a3d_fix.h"

#define CR_STAGES     3
#define CR_LEN        7000              /* a stage's ride */
#define CR_SPEED      9                 /* units a tick */
#define CR_MAX_TARGETS 40
#define CR_MAX_BOLTS  12
#define CR_SHIELD     100
#define CR_BOLT_DMG   12
#define CR_FIRE_GAP   5                 /* ticks between shots */
#define CR_SIGHT_SPEED 7                /* pixels a tick */
#define CR_VIEW_W     320
#define CR_VIEW_H     256
#define CR_BETWEEN    75
#define CR_NODES      4

/* input bits */
#define CR_IN_LEFT  1
#define CR_IN_RIGHT 2
#define CR_IN_UP    4
#define CR_IN_DOWN  8
#define CR_IN_FIRE  16

enum { TG_TURRET, TG_DRONE, TG_POD, TG_COUNT };
enum { CRE_NONE, CRE_SHOT, CRE_HIT, CRE_KILL, CRE_BOLT_DOWN, CRE_MISS, CRE_HURT, CRE_POD, CRE_FIRE, CRE_BOSS,
       CRE_NODE, CRE_BOSS_DOWN, CRE_STAGE, CRE_OVER, CRE_WIN, CRE_COUNT };
typedef struct { int type, a, b; } CREvent;
#define CR_MAX_EVENTS 16

typedef struct {
    int type, alive, hp, cool;
    int32_t s;                          /* where along the canyon */
    int32_t side, h;                    /* turrets: -1 left wall, 1 right; height */
    int32_t amp, phase;                 /* drones: how far they swoop, and when */
} Target;

typedef struct {
    int alive, t, flight;
    fix x0, y0, z0, x1, y1, z1;         /* from, and where the camera will be */
} Bolt;

typedef struct {
    uint32_t seed, rng;
    int stage, over, won, between;
    int32_t cam_s;                      /* the camera's place along the ride */
    int boss;                           /* the guardian is up */
    int node_hp[CR_NODES];
    int32_t boss_angle, boss_cool;
    Target tg[CR_MAX_TARGETS];
    int ntg;
    Bolt bolt[CR_MAX_BOLTS];
    int sx, sy;                         /* the sight, in the view's pixels */
    int fire_cool, combo, best_combo;
    int shield;
    int32_t score, tick;
    int shots, hits, kills, hurt;
    int last_hit_x, last_hit_y, last_shot_hit;  /* for the tracer */
    unsigned prev_in;
    CREvent ev[CR_MAX_EVENTS];
    int nev;
} CRGame;

extern const char *const cr_event_names[CRE_COUNT];

void cr_new_game(CRGame *g, uint32_t seed, int stage);
void cr_load_stage(CRGame *g, int stage);
void cr_tick(CRGame *g, unsigned in);
int  cr_pop_event(CRGame *g, CREvent *e);
unsigned cr_bot(const CRGame *g);       /* the autopilot's input */
uint32_t cr_state_hash(const CRGame *g);

int32_t cr_path_x(int32_t s);           /* the ride's line: across */
int32_t cr_path_y(int32_t s);           /* and up */
int32_t cr_half_width(int32_t s);       /* the canyon's half width there */
void cr_camera(const CRGame *g, Cam *c);
void cr_target_pos(const CRGame *g, int k, int32_t *x, int32_t *y, int32_t *z);
void cr_node_pos(const CRGame *g, int k, int32_t *x, int32_t *y, int32_t *z);
void cr_bolt_pos(const CRGame *g, int k, int32_t *x, int32_t *y, int32_t *z);
int  cr_project(const CRGame *g, int32_t x, int32_t y, int32_t z, int *px, int *py, int32_t *depth);   /* 0 if behind */
int  cr_size(int type);                 /* a target's radius */

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
