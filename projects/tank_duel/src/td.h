// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * td.h - Tank Duel's game logic: a tank fight in a walled arena, seen from
 * the turret. Shells bounce once off the arena's walls and off crates, so
 * a shot can be banked round cover. Three kinds of enemy:
 *
 *   scout    quick and light; circles you and snipes
 *   mortar   slow; lobs shells that land where you were (a red ring
 *            shows where), hurting everything near
 *   bulwark  a heavy tank with a shield across its front: shots there
 *            glance off, so hit it from the side or behind
 *
 * Crates break after two hits; pillars and pyramids stand. Eight rounds,
 * each with more enemies; between rounds the tank is patched up a little.
 *
 * x across, z ahead, positions 16.16 fixed point world units, angles
 * 16-bit (0 = +z, a quarter turn = +x), one tick 40 ms. No Amiga headers:
 * tested on the host too.
 */
#ifndef TD_GAME_H
#define TD_GAME_H

#include <stdint.h>
#include "a3d_fix.h"

#define TD_ARENA     1000               /* the walls: x and z from -1000 to 1000 */
#define TD_MAX_OBS   20
#define TD_MAX_TANKS 8                  /* tank 0 is the player */
#define TD_MAX_SHELLS 16
#define TD_MAX_MORTARS 6
#define TD_ROUNDS    8
#define TD_TANK_R    28
#define TD_SHELL_SPEED FX(32)
#define TD_SHELL_LIFE 50
#define TD_RELOAD    25                 /* ticks between the player's shots */
#define TD_HP        100
#define TD_SHELL_DMG 20
#define TD_SPLASH_R  90
#define TD_SPLASH_DMG 35
#define TD_BETWEEN   75

/* input bits */
#define TD_IN_LEFT  1
#define TD_IN_RIGHT 2
#define TD_IN_UP    4
#define TD_IN_DOWN  8
#define TD_IN_FIRE  16

enum { OB_PILLAR, OB_PYRAMID, OB_CRATE };
enum { TK_PLAYER, TK_SCOUT, TK_MORTAR, TK_BULWARK, TK_COUNT };

enum { TDE_NONE, TDE_FIRE, TDE_EFIRE, TDE_LOB, TDE_BOUNCE, TDE_GLANCE, TDE_HIT, TDE_KILL, TDE_HURT, TDE_SPLASH,
       TDE_CRATE_HIT, TDE_CRATE_BREAK, TDE_ROUND_CLEAR, TDE_ROUND, TDE_DEAD, TDE_WIN, TDE_COUNT };
typedef struct { int type, a, b; fix x, z; } TDEvent;
#define TD_MAX_EVENTS 24

typedef struct {
    int kind, hp;                       /* OB_*; hp: crates' hits left (0: gone) */
    fix x, z;
    int r;                              /* pillars, pyramids: radius; crates: half size */
} Obstacle;

typedef struct {
    int type, alive, hp;
    fix x, z, v;
    int yaw;
    int reload, ai_timer, orbit;        /* orbit: which way a scout circles */
    int held;                           /* a practice target: doesn't move or shoot */
} Tank;

typedef struct {
    int alive, owner, bounces, life;    /* owner: the tank that fired it */
    fix x, z, dx, dz;
} Shell;

typedef struct {
    int alive, owner, t, flight;        /* ticks in the air, and how long it will be */
    fix x0, z0, tx, tz;                 /* from, and where it will land */
} Mortar;

typedef struct {
    uint32_t seed, rng;
    int round, over, won, between;
    Tank tk[TD_MAX_TANKS];
    int ntanks;
    Obstacle ob[TD_MAX_OBS];
    int nobs;
    Shell sh[TD_MAX_SHELLS];
    Mortar mo[TD_MAX_MORTARS];
    int32_t score, tick;
    int kills, shots, hits, damage_taken;
    unsigned prev_in;
    TDEvent ev[TD_MAX_EVENTS];
    int nev;
} TDGame;

extern const char *const td_event_names[TDE_COUNT];
extern const char *const td_tank_names[TK_COUNT];

uint32_t td_rand(TDGame *g);
void td_new_game(TDGame *g, uint32_t seed, int round);
void td_load_round(TDGame *g, int round);
void td_tick(TDGame *g, unsigned in);
int  td_pop_event(TDGame *g, TDEvent *e);
unsigned td_bot(const TDGame *g);       /* the autopilot's input */
uint32_t td_state_hash(const TDGame *g);
int  td_enemies_left(const TDGame *g);
int  td_add_tank(TDGame *g, int type, fix x, fix z, int yaw);
int  td_angle_to(fix dx, fix dz);       /* the heading from a point towards (dx, dz) away */
int  td_blocked(const TDGame *g, fix x, fix z, int r);   /* inside an obstacle or a wall? */
int  td_fire(TDGame *g, int tank);      /* 1 if a shell went */

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
