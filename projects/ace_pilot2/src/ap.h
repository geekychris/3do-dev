// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ap.h - Ace Pilot 2's game logic: a biplane war over the river Sellen.
 * You fly a Swallow scout of Larkspur Squadron against the Grauland air
 * service: eight missions (a patrol, balloons, an escort, a train, the
 * bombers, the supply depot, the enemy aerodrome and the Grey Falcon).
 *
 * Every aeroplane flies the same model: throttle, pitch, a bank that
 * turns it, speed bought and sold for height, a stall below flying speed.
 * The computer's pilots (and the autopilot) fly it through the same
 * controls the player has, from ap_steer.
 *
 * x across (east), y up, z ahead (north: the enemy's side), positions
 * 16.16 fixed point world units (about a metre), angles 16-bit (yaw 0 =
 * +z, a quarter turn = +x; pitch > 0 nose up; roll > 0 right wing down),
 * one tick 40 ms. No Amiga headers: tested on the host too.
 */
#ifndef AP_GAME_H
#define AP_GAME_H

#include <stdint.h>
#include "a3d_fix.h"

#define AP_WORLD      7000              /* the map: x and z from -7000 to 7000 */
#define AP_MAX_PLANES 10                /* plane 0 is the player */
#define AP_MAX_BULLETS 48
#define AP_MAX_FLAK   10
#define AP_MAX_GROUND 24
#define AP_MAX_WP     4
#define AP_MISSIONS   8
#define AP_LIVES      3                 /* aeroplanes the squadron gives you */
#define AP_HP         30                /* the player's: a bullet takes 1, a close shell burst 4 */
#define AP_AMMO       600               /* rounds: the twin guns fire two at a time */
#define AP_JAM_HEAT   100
#define AP_JAM_TICKS  75
#define AP_THROTTLE   4                 /* throttle 0..4 */
#define AP_BANK_MAX   12743             /* 70 degrees */
#define AP_PITCH_MAX  10923             /* 60 degrees */
#define AP_V_STALL    FX(1.7)           /* units a tick: 42 a second */
#define AP_V_CRUISE   FX(3.6)
#define AP_V_MAX      FX(6.5)
#define AP_BULLET_V   FX(14)            /* added to the plane's speed */
#define AP_BULLET_LIFE 28
#define AP_CEILING    FX(1600)
#define AP_END_TICKS  75                /* the world goes on after a mission ends */
#define AP_HOME_R     700               /* back over the aerodrome: home */

/* the map's places */
#define AP_HOME_X     (-600)            /* Larkspur Field, its runway along z */
#define AP_HOME_Z     (-2400)
#define AP_RUNWAY_W   60
#define AP_RUNWAY_L   420
#define AP_ENEMY_X    1800              /* the Grauland aerodrome at Kessin */
#define AP_ENEMY_Z    3800
#define AP_DEPOT_X    (-2500)           /* the supply depot at Orme */
#define AP_DEPOT_Z    2000
#define AP_RAIL_Z     2700              /* the railway, east-west */

/* input bits (the shell maps keys to them) */
#define AP_IN_LEFT   1                  /* roll left */
#define AP_IN_RIGHT  2
#define AP_IN_NOSEDN 4                  /* stick forward */
#define AP_IN_NOSEUP 8                  /* stick back */
#define AP_IN_FIRE   16
#define AP_IN_THR_UP 32
#define AP_IN_THR_DN 64

enum { SIDE_HOME, SIDE_ENEMY };
/* aeroplanes: yours and the reconnaissance two-seater you escort; theirs:
 * the light scout, the fighter, the bomber (a rear gunner) and the ace */
enum { PT_SWALLOW, PT_HERON, PT_WASP, PT_HORNET, PT_OX, PT_FALCON, PT_COUNT };
enum { PS_FLY, PS_FALL, PS_DEAD };      /* shot down: falls burning, then gone */
enum { AI_NONE, AI_PATROL, AI_PURSUE, AI_BREAK, AI_CLIMB, AI_FLEE, AI_ROUTE, AI_HELD };
/* ground things: sheds and tents at the aerodromes, supply dumps, the
 * anti-aircraft guns, observation balloons (tethered aloft), the train,
 * lorries on the roads */
enum { GT_HANGAR, GT_DEPOT, GT_AA, GT_BALLOON, GT_LOCO, GT_WAGON, GT_TRUCK, GT_COUNT };
enum { PH_OUT, PH_HOME, PH_DONE, PH_FAILED };

enum { APE_NONE, APE_FIRE, APE_HIT, APE_KILL, APE_GKILL, APE_HURT, APE_JAM, APE_UNJAM, APE_STALL, APE_FLAK,
       APE_WAYPOINT, APE_OBJECTIVE, APE_HOMEWARD, APE_DONE, APE_FAILED, APE_DOWN, APE_CRASH, APE_LANDED,
       APE_REARMED, APE_ARRIVE, APE_BOMBS, APE_ALLY_LOST, APE_EDGE, APE_COUNT };
typedef struct { int type, a, b; fix x, y, z; } APEvent;
#define AP_MAX_EVENTS 32

typedef struct {
    int type, side, state, hp, maxhp;
    fix x, y, z, v;                     /* position; airspeed, units a tick */
    int yaw, pitch, roll;
    int throttle, stalled, on_ground, ground_t;
    int heat, jam, ammo, gun_cd, gun_side, burst, rest;
    int ai, ai_t, target, wp, held, objective, home;   /* home: reached its route's end */
    int rear_cd;                        /* the bomber's rear gunner */
    fix tx, tz;                         /* where it patrols, or runs to */
    int talt;
    int hit_t;                          /* ticks since it was last hit */
    unsigned in;                        /* the controls it flew with this tick */
} Plane;

typedef struct {
    int alive, owner, life;
    fix x, y, z, dx, dy, dz;
} Bullet;

typedef struct {
    int alive, t, fuse;                 /* a shell in flight (fuse ticks), then the burst's puff */
    fix x, y, z;
} Flak;

typedef struct {
    int type, alive, hp, objective;
    fix x, y, z;                        /* y: a balloon's height */
    int yaw, cd;                        /* cd: an AA gun's reload */
    int line;                           /* 0 still, 1 the railway, 2 the road */
    fix s, ds;                          /* the train and lorries: along their line */
} Ground;

typedef struct { fix x, z; int alt; } Waypoint;

typedef struct {
    uint32_t seed, rng;
    int mission, phase, end_t, over, won, lives;
    Plane p[AP_MAX_PLANES];
    int np;
    Bullet b[AP_MAX_BULLETS];
    Flak fk[AP_MAX_FLAK];
    Ground gr[AP_MAX_GROUND];
    int ngr;
    Waypoint wp[AP_MAX_WP];
    int nwp, wp_next;                   /* the player's patrol line */
    Waypoint route[AP_MAX_WP];          /* the escorted or the bombers' route */
    int nroute, loiter;
    int need, done;                     /* objectives: how many must go, how many have */
    int lost, lost_max;                 /* escorts lost / bombers through: the mission fails at lost_max */
    int wave_t, wave_n;                 /* reinforcements: when, how many left */
    int32_t score, tick, mtick;
    int kills, gkills, shots, hits, missions_flown, landed;
    int bot_mode, bot_t;                /* the autopilot's memory: an attack run, the climb away */
    APEvent ev[AP_MAX_EVENTS];
    int nev;
} APGame;

typedef struct {
    const char *name, *place;
    const char *brief[4];
} APMission;

extern const APMission ap_missions[AP_MISSIONS];
extern const char *const ap_event_names[APE_COUNT];
extern const char *const ap_plane_names[PT_COUNT];
extern const char *const ap_ground_names[GT_COUNT];
extern const char *const ap_ranks[5];

/* hills: cones on flat country */
#define AP_HILLS 16
typedef struct { int16_t x, z, r, h; } Hill;
extern const Hill ap_hills[AP_HILLS];

uint32_t ap_rand(APGame *g);
void ap_new_game(APGame *g, uint32_t seed, int mission);
void ap_load_mission(APGame *g, int mission);
void ap_tick(APGame *g, unsigned in);
int  ap_next(APGame *g);                /* after a mission ends: the next (or again); 0: the campaign is over */
int  ap_pop_event(APGame *g, APEvent *e);
unsigned ap_bot(APGame *g);             /* the autopilot's controls for the player */
uint32_t ap_state_hash(const APGame *g);
int  ap_add_plane(APGame *g, int type, int side, fix x, fix y, fix z, int yaw);
int  ap_add_ground(APGame *g, int type, fix x, fix y, fix z, int yaw, int objective);
void ap_fly(APGame *g, Plane *p, unsigned in);
unsigned ap_steer(const APGame *g, const Plane *p, fix tx, fix ty, fix tz, int hard);
int  ap_angle_to(fix dx, fix dz);
fix  ap_ground_y(fix x, fix z);         /* the ground's height */
int  ap_on_runway(fix x, fix z);
int32_t ap_river_z(int32_t x);          /* the Sellen's middle, whole units */
int32_t ap_trench_z(int32_t x);         /* the front line's trenches */
int32_t ap_rail_z(int32_t x);           /* the railway */
void ap_road_pos(fix s, fix *x, fix *z);   /* the road from the depot to the front, s 0..AP_ROAD_LEN */
#define AP_ROAD_LEN FX(2300)
int  ap_enemies_left(const APGame *g);  /* hostile aeroplanes flying */
int  ap_rank(int32_t score);
void ap_objective_text(const APGame *g, char *buf);   /* 40 bytes */
int  ap_objective_pos(const APGame *g, fix *x, fix *z);   /* where to go next; 0 none */

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
