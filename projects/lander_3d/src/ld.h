// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ld.h - Lander 3D's simulation, ported from the browser game
 * (geekychris.github.io/tools/lander-3d, lander-3d-core.js): seeded terrain
 * (fBm hills, craters, flattened pads with score multipliers), a lander
 * with a main engine and RCS pitch and roll, three worlds (the Moon; Mars
 * with wind; a small asteroid), the touchdown check, and the autopilot
 * that flies the title demo and proves every level can be landed.
 *
 * Fixed point (16.16) throughout, so it runs on a 68020 without an FPU:
 * metres, m/s and radians are `fix`. The step is 1/100 s (four a frame).
 * No Amiga headers: tested on the host too.
 */
#ifndef LD_H
#define LD_H

#include <stdint.h>

#include "a3d_fix.h"

#define LD_STEP_HZ  100
#define LD_DT       (FONE / LD_STEP_HZ)
#define LD_MAX_N    97

enum { W_MOON, W_MARS, W_ASTEROID, W_COUNT };
enum { OUT_NONE, OUT_LANDED, OUT_OFFPAD, OUT_CRASHED, OUT_LOST };

typedef struct {
    const char *name;
    fix g, amax;                        /* gravity, the engine's acceleration at full throttle */
    int size, n;                        /* metres across, grid points a side */
    fix hills, rough;
    int craters, crater_max;
    fix wind, drag, curve, dunes;
    fix lim_vs, lim_hs, lim_tilt;       /* landing limits: m/s, m/s, radians */
    fix fuel;                           /* seconds of full throttle */
} LdWorld;

extern const LdWorld ld_worlds[W_COUNT];
extern const char *const ld_outcome_names[5];

typedef struct { fix x, z, r, y; int mult; } Pad;

typedef struct {
    int world, n, size;
    fix cell, half, inv_cell;
    fix h[LD_MAX_N * LD_MAX_N];         /* heights, row-major z then x */
    Pad pads[3];
} Terrain;

typedef struct {
    fix fuel, start_alt, start_h, start_dist, pad_scale, wind;
} LevelParams;

typedef struct {
    fix p[3], v[3];
    fix pitch, roll, wp, wr;            /* attitude (rad), rates (rad/s) */
    fix throttle;
    int rcs[2];
} Craft;

typedef struct { fix throttle, pitch, roll; } LdInput;     /* 0..1, -1..1, -1..1 */

typedef struct {
    int vs_ok, hs_ok, tilt_ok, pad;
    fix vs, hs, tilt, slope;
    int32_t score, soft, fuel_bonus;
} LdResult;

/* events for sound and the log */
enum { LDE_NONE, LDE_LANDED, LDE_OFFPAD, LDE_CRASHED, LDE_LOST, LDE_FUEL_OUT, LDE_COUNT };

typedef struct {
    int world, level;
    uint32_t seed;
    LevelParams params;
    Terrain T;
    Craft c;
    fix fuel, fuel_max;
    int32_t t;                          /* steps */
    int outcome;
    LdResult result;
    fix wind_dir, wind_ph, wind[2];
    int assist;
    int ap_down;                        /* autopilot: descending over the pad */
    int ap_target;                      /* autopilot: the pad, -1 none */
    fix probe_used;                     /* fuel the autopilot's probe flight used */
    int probe_outcome;
    int events[8], nev;
} Level;

void ld_level_params(int world, int level, LevelParams *p);
uint32_t ld_rng(uint32_t *a);           /* mulberry32 */
void ld_make_terrain(Terrain *T, int world, uint32_t seed, const LevelParams *p);
fix  ld_height(const Terrain *T, fix x, fix z);
fix  ld_slope_cos(const Terrain *T, fix x, fix z);      /* cos of the slope (1 = level) */
void ld_new_level(Level *L, int world, int level, uint32_t seed, int probe);
/* the same check in the background, a slice at a time (the game: a whole
 * flight at once takes far too long on a 68k): begin, then run until it
 * returns 1; it tops up L's tank if the autopilot needed more */
void ld_probe_begin(const Level *L);
int  ld_probe_run(Level *L, int steps);
void ld_step(Level *L, const LdInput *in);
void ld_autopilot(Level *L, LdInput *out);
void ld_up(const Craft *c, fix u[3]);
void ld_feet(const Craft *c, fix out[5][3]);           /* the four feet and the engine bell */
fix  ld_tilt(const Craft *c);                           /* radians from vertical */
fix  ld_agl(const Level *L);                            /* height of the feet above the ground */
int  ld_nearest_pad(const Level *L, int easiest);
int  ld_pop_event(Level *L);

int run_tests(void (*check)(int cond, const char *name));

#endif
