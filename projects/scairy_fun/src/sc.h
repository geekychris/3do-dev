// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sc.h - ScAIry Fun portable core: maths, routes, the car, the autopilot,
 * traffic, hazards and scoring.
 *
 * Nothing in here includes an Amiga header, so the same code is unit tested
 * on the host (make host-test), on the Amiga (scairy_fun_test) and inside
 * the game (the "selftest" hook). Amiga-only code (screen, copper, audio,
 * bridge) lives in main.c, render.c, sound.c and game.c.
 *
 * Units: 16 world units = 1 metre. The road runs along +Z; a route is a
 * list of 8 m segments, each with the lateral position of the road's centre
 * line (cx), its height and what lies beside it. Moving things store z and
 * their offset d from the centre line in "fine" units (world << FBITS).
 * The car is always aligned with the road (as in OutRun): steering moves it
 * across, and bends push it outwards.
 */
#ifndef SC_H
#define SC_H

#include <stdint.h>

/* ---------------- maths ---------------- */
/* the shared engine's (../amiga3d/a3d.h): trig with 16-bit angles (0..65535
 * a full turn; yaw 0 faces +Z, increasing to the right), the camera, clipping
 * and the span converter. ScAIry Fun's own names for them: */
#include "a3d.h"

#define FBITS          4
#define UNITS_M        16                /* world units per metre */

#define sc_sin_tab     a3_sin_tab
#define sc_sin         a3_sin
#define sc_cos         a3_cos
#define sc_atan_ratio  a3_atan_ratio
#define sc_rand        a3_rand
#define sc_hash        a3_hash

#define VIEW_W      320
#define VIEW_H      212                  /* 3D area; dashboard / HUD below */
#define VIEW_CX     160
#define VIEW_CY     100
#define FOV         180
#define FAR_Z       4200

/* the fixed view's projection and horizon (z >= NEAR_Z) */
static inline void sc_project(const V3 *p, int32_t *sx, int32_t *sy)
{
    *sx = VIEW_CX + (p->x * FOV) / p->z;
    *sy = VIEW_CY - (p->y * FOV) / p->z;
}
static inline int sc_horizon(const Cam *c)
{
    return VIEW_CY - (int)((FOV * (c->sp >> 4)) / (c->cp >> 4));
}

/* ---------------- routes ---------------- */
#define SEG_BITS    7
#define SEG_LEN     (1 << SEG_BITS)      /* 128 units = 8 m */
#define MAX_SEGS    560
#define LANE_W      56                   /* 3.5 m */
#define SHOULDER    24
#define LEVEE_H     96

/* what lies beside the road, per side */
enum { SIDE_GRASS, SIDE_LEVEE, SIDE_DROP, SIDE_WALL, SIDE_CLIFF, SIDE_MEDIAN,
       SIDE_TOWN, SIDE_CANAL, SIDE_COUNT };

/* segment flags */
#define SF_FLOOD     0x01               /* water over the road */
#define SF_GHOST     0x02               /* old lane lines, seg.ghost = their offset */
#define SF_OVERPASS  0x04
#define SF_RAIL_L    0x08               /* guardrail */
#define SF_RAIL_R    0x10
#define SF_GLARE     0x20               /* low sun straight ahead */
#define SF_FINISH    0x40
#define SF_CONES     0x80               /* seg.closed lanes are coned off */

typedef struct {
    int32_t cx;                          /* centre line x at the segment start */
    int16_t y;                           /* road height */
    int16_t dir;                         /* cx change to the next segment */
    uint8_t side[2];                     /* SIDE_*, left and right */
    uint8_t flags;
    uint8_t closed;                      /* bit n: our lane n coned off */
    int16_t ghost;                       /* SF_GHOST: old line offset from the right road edge */
} Seg;

/* hazards and bait */
enum { HZ_NONE,
       /* the autopilot gets these wrong */
       HZ_FLOOD, HZ_GHOST, HZ_PHANTOM, HZ_CONES, HZ_GLARE, HZ_SIREN, HZ_DEBRIS,
       /* and these right: taking over for them is needless */
       HZ_DEER, HZ_CYCLIST, HZ_TRACTOR, HZ_MERGE, HZ_GOATS, HZ_PEDS,
       HZ_COUNT };
#define HZ_FAILS(t) ((t) >= HZ_FLOOD && (t) <= HZ_DEBRIS)

typedef struct {
    uint8_t type;
    int16_t seg, len;                    /* where, how many segments */
    int16_t param;                       /* lane, side or strength */
} Hazard;

typedef struct {
    const char *name, *dest, *ai_name;
    int nl, no;                          /* lanes our way, oncoming lanes */
    int limit_kmh;
    int traffic;                         /* cars per 100 segments */
    int sky;                             /* sky palette index */
} RouteInfo;

#define NUM_ROUTES 4
#define MAX_HAZ    24
extern const RouteInfo route_info[NUM_ROUTES];

/* ---------------- objects ---------------- */
enum { OBJ_NONE, OBJ_CAR, OBJ_TRUCK, OBJ_TRACTOR, OBJ_CYCLIST, OBJ_DEER,
       OBJ_CONE, OBJ_DEBRIS, OBJ_AMBULANCE, OBJ_GOAT, OBJ_PED, OBJ_COUNT };

#define OF_ONCOMING  0x01
#define OF_KNOCKED   0x02               /* a cone hit by the car */
#define OF_TAILGATE  0x04               /* the truck behind a phantom brake */
#define OF_PASSING   0x08               /* the ambulance has given up waiting */

typedef struct {
    uint8_t type, flags, colour;
    int hz;                              /* the hazard it belongs to, -1 traffic */
    int lane;                            /* the lane it wants, oncoming lanes < 0 */
    int32_t z, d;                        /* fine */
    int32_t v, vd;                       /* fine / frame, along and across */
    int32_t vt;                          /* the speed it wants */
    int timer, cool;
} Obj;

#define MAX_OBJS 40

/* ---------------- the car and its autopilot ---------------- */
#define MAX_KMH    140
#define KMH(v)     ((int)(((v) * 90L) >> 8))     /* fine/frame -> km/h */
#define FROM_KMH(k) ((int32_t)(((k) * 256L) / 90))

enum { CAM_CHASE, CAM_COCKPIT };
enum { GS_TITLE, GS_INTRO, GS_PLAY, GS_ARRIVED, GS_UPDATE, GS_GAMEOVER, GS_WIN };
enum { CS_DRIVING, CS_AQUAPLANE, CS_SPLASH, CS_ROLL };

/* Input bits */
#define IN_LEFT    0x0001
#define IN_RIGHT   0x0002
#define IN_UP      0x0004      /* accelerate */
#define IN_DOWN    0x0008      /* brake */
#define IN_FIRE    0x0010      /* autopilot on / off */
#define IN_VIEW    0x0020
#define IN_ESC     0x0080
#define IN_START   0x0100
#define IN_PAUSE   0x0200
#define IN_HELP    0x0400      /* the HELP key (or H): the rules */

/* What the AI is showing / saying. Each has a line of text and a voice clip. */
enum { MSG_NONE, MSG_AP_ON, MSG_AP_OFF, MSG_HAD_THAT, MSG_GOOD_CATCH,
       MSG_RECALC, MSG_ROAD_PROBABLY, MSG_OBSTACLE, MSG_HANDS, MSG_OOPS,
       MSG_UPDATED, MSG_UNAVAILABLE, MSG_WELCOME, MSG_WATER_ANYWAY,
       MSG_ARRIVED, MSG_BLINDED, MSG_PATIENT, MSG_TICKET, MSG_TOW,
       MSG_CANCELLED, MSG_COUNT };
extern const char *const msg_text[MSG_COUNT];

/* Events for the platform layer (sound, voice, log) */
enum { EV_NONE, EV_AP_ON, EV_AP_OFF, EV_UNAVAILABLE, EV_SAVE, EV_NEEDLESS,
       EV_SCRAPE, EV_CONE, EV_DEBRIS, EV_REAREND, EV_CRASH, EV_SPLASH,
       EV_ROLL, EV_TICKET, EV_RESPAWN, EV_PHANTOM, EV_BLINDED, EV_AQUAPLANE,
       EV_ARRIVED, EV_GAME_OVER, EV_MULT_UP, EV_SIREN, EV_ROUTE_START,
       EV_HIT_ANIMAL, EV_SAY, EV_WIN, EV_COUNT };
extern const char *const ev_name[EV_COUNT];
typedef struct { int type, a, b; } Event;
#define MAX_EVENTS 16

/* the AI's picture of the world: what it has detected, for the display */
#define MAX_SEEN 12
typedef struct {
    int32_t z, d;                        /* fine, absolute */
    uint8_t type, phantom;
} Seen;

typedef struct {
    int on;                              /* autopilot engaged */
    int lane;                            /* the lane it is keeping */
    int32_t plan[16];                    /* planned offset d (fine) at each segment ahead */
    int plan_from;                       /* segment plan[0] belongs to */
    int phantom_timer;                   /* hard braking for something that isn't there */
    int phantom_seg;                     /* the overpass it already braked for */
    int blinded;                         /* the low sun has blinded the camera */
    int32_t blind_x, blind_z, blind_dir; /* the straight line it then follows */
    int32_t target_v;
    int lane_hold;                       /* frames before it changes lane again */
    Seen seen[MAX_SEEN];
    int nseen;
} Ai;

typedef struct {
    int state;                           /* CS_* */
    int32_t z, d;                        /* fine */
    int32_t v;                           /* fine / frame */
    int32_t s;                           /* steering -256..256 */
    int32_t lat;                         /* last lateral move, fine / frame (for the car's yaw) */
    int timer;                           /* splash / roll: frames until the tow truck */
    int cool;                            /* frames until another scrape counts */
    int32_t hist[32];                    /* speed history, for the tailgating truck */
    int hist_pos;
} Car;

/* Tunables, exposed over the bridge */
typedef struct {
    int32_t god_mode;                    /* 1 = no damage */
    int32_t ai_perfect;                  /* 1 = the autopilot never fails */
    int32_t traffic_pct;                 /* traffic density, percent */
    int32_t speed_pct;                   /* speed limit scale, percent */
    int32_t mult_secs;                   /* autopilot seconds per multiplier step */
    int32_t aqua_kmh;                    /* aquaplaning above this in flood water */
} Tune;

typedef struct {
    /* the route */
    int route;
    Seg seg[MAX_SEGS];
    int nseg;
    Hazard haz[MAX_HAZ];
    int nhaz;
    uint8_t haz_state[MAX_HAZ];          /* 0 ahead, 1 active, 2 passed */
    uint8_t haz_said[MAX_HAZ];           /* the AI has commented on it */
    int finish;                          /* the destination's segment */

    int state, frame, state_timer, view;
    Car car;
    Ai ai;
    Obj obj[MAX_OBJS];
    uint32_t rng;

    /* scoring */
    int32_t score, cover, mult;
    int32_t ap_frames;                   /* frames of unbroken autopilot */
    int32_t dist_ap, dist_all;           /* this route, fine */
    int32_t saves, needless, takeovers;
    int32_t ap_rem;                      /* score remainder, fine metres */
    uint8_t knocked[MAX_SEGS / 8 + 1];   /* segments whose cones were hit */
    int pending_save;                    /* hazard index a takeover is being judged on, -1 */
    int incident;                        /* something went wrong since the takeover */
    int32_t route_bonus;
    int32_t total_saves, total_needless;

    int msg, msg_timer;                  /* MSG_* on screen */
    int glare;                           /* 0..16 */
    int siren;                           /* an ambulance is behind */
    Event ev[MAX_EVENTS];
    int nev;
    unsigned last_in;
    Tune tune;
} World;

/* sc_math.c is above; track.c */
void route_build(World *w, int route);
int32_t road_cx(const World *w, int32_t z);          /* fine z -> world units */
int32_t road_y(const World *w, int32_t z);
int road_dir(const World *w, int32_t z);             /* yaw of the road, 16-bit angle */
int32_t road_curve(const World *w, int32_t z);       /* dir change per segment */
int seg_of(int32_t z);                               /* fine z -> segment index */
int32_t road_left(const World *w);                   /* road edge offsets from the centre line, units */
int32_t road_right(const World *w);
int32_t lane_centre(const World *w, int lane);       /* units; lane 0 = our leftmost */
int lane_of(const World *w, int32_t d);              /* fine d -> our lane, -1 oncoming */
int32_t ghost_at(const World *w, int32_t z);         /* units */

/* world.c */
void world_defaults(Tune *t);
void world_init(World *w);                           /* title state */
void world_start_game(World *w);
void world_start_route(World *w, int route);
void world_update(World *w, unsigned input);
int  world_pop_event(World *w, Event *e);
void world_say(World *w, int msg);
int  world_scene(World *w, const char *name);        /* jump to just before a hazard */
int  world_spawn(World *w, int type, int32_t z, int32_t d, int32_t v, int flags);
int  world_hazard_ahead(const World *w, int segs);   /* failing hazard index within segs, -1 */
void world_set_ap(World *w, int on);
int  world_ap_allowed(const World *w);
int32_t world_ap_pct(const World *w);
int  aqua_kmh(const World *w);                       /* aquaplaning speed in this route's floods */
const char *hz_name(int type);
int  cone_at(const World *w, int seg, int32_t *d);  /* a cone in the closed lane: its d, units */
const char *obj_name(int type);
int obj_len(int type);                               /* half length, units */
int obj_wid(int type);                               /* half width, units */
void ai_think(World *w);                             /* plan, perceive, choose speed */
int32_t ai_steer(const World *w);

/* tests/test_cases.c */
typedef void (*sc_assert_fn)(int cond, const char *name);
int sc_run_tests(sc_assert_fn check);
int run_tests(sc_assert_fn check);          /* the engine's test runners call this */

#endif
