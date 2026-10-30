// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * pb.h - Boing Pinball's simulation, ported from the browser game
 * (geekychris.github.io/tools/boing-pinball, boing-pinball-core.js): the
 * tables, the fixed-step physics and the rules. No Amiga headers, so it is
 * tested on the host too.
 *
 * Units are millimetres and seconds in the plane of the playfield, 16.16
 * fixed point (`fix`): x across (0 = the left wall), y up the table (0 =
 * just below the flippers). Each 1/120 s step is split into substeps
 * short enough that neither a ball nor a moving flipper's tip goes more
 * than a quarter of the ball's radius in one, so nothing tunnels; the
 * flipper hit uses the velocity of the flipper's surface at the contact
 * (w x r), which is what throws the ball up the table.
 *
 * The browser has one table, Comet Tour; here it is one of three. They
 * share the cabinet, the flippers, slingshots, inlanes and outlanes, the
 * top lanes and the plunger; the upper playfields differ (bumpers, ramps,
 * drop-target banks, spinners, the saucer, an upper flipper), and so do
 * the missions' names and the art. The rules are the browser's.
 */
#ifndef PB_H
#define PB_H

#include <stdint.h>

#include "a3d_fix.h"

#define PB_HZ        120
#define PB_STEP      (FONE / PB_HZ)     /* 1/120 s */
#define PB_R         FX(13.5)           /* the ball's radius (a 27 mm ball) */
#define PB_GRAVITY   1500               /* mm/s^2 along the slope */
#define PB_MAX_SPEED FROMI(7000)
#define PB_MID       239                /* the playfield's centre line (the plunger lane is on the right) */
#define PB_WIDTH     520
#define PB_TOP       1040

#define PB_SECS(s)   ((int32_t)((s) * PB_HZ))      /* seconds -> steps */

#define PB_MAX_SEG   200
#define PB_MAX_CIRC  16
#define PB_MAX_SENS  24
#define PB_MAX_FLIP  3
#define PB_MAX_RAMP  2
#define PB_MAX_RPTS  16
#define PB_MAX_SPIN  2
#define PB_MAX_BANK  2
#define PB_MAX_BALLS 4
#define PB_MAX_EV    64
#define PB_MAX_MSG   6
#define PB_MAX_TIMER 8
#define PB_MAX_DECO  48

/* the grid the ball looks things up in: 64 mm cells */
#define PB_CELL_SH   6
#define PB_GX        9                  /* x 0..575 */
#define PB_GY        19                 /* y -64..1151 */
#define PB_GMAX      40                 /* entries per cell */

enum { SK_WALL, SK_OUTER, SK_SLING, SK_GATE, SK_FLOOR, SK_DROP, SK_GUIDE };

typedef struct {
    fix ax, ay, bx, by;
    fix ux, uy, len;                    /* the unit direction a -> b, the length */
    fix r, e;                           /* capsule radius, restitution */
    int8_t kind, id;                    /* sling: 0 left, 1 right; drop: its index */
    int8_t ow_x, ow_y;                  /* a one-way gate lets the ball through towards (ow_x, ow_y) */
    int8_t up;                          /* a drop target is standing */
    int8_t bank;                        /* a drop target's bank */
    int16_t h;                          /* height drawn, mm */
    int16_t bb[4];                      /* x0 y0 x1 y1 (mm), grown by r + the ball */
} Seg;

typedef struct { fix cx, cy, r, e; int8_t bumper, id; int16_t h; } Circ;   /* posts and pop bumpers */

enum { SN_LANE, SN_IN, SN_OUT, SN_SPIN, SN_RAMPIN, SN_LANEOUT };
typedef struct { fix ax, ay, bx, by; int8_t kind, idx; int16_t bb[4]; } Sensor;

typedef struct { int side; fix px, py, len, r1, r2, rest, up; } FlipDef;   /* side: 0 left button, 1 right */

typedef struct {
    int n;
    int16_t pts[PB_MAX_RPTS][3];        /* x, y, height (mm) */
    fix prof[PB_MAX_RPTS];              /* arc length to each point */
    fix exit_x, exit_y, exit_vy;
} Ramp;

typedef struct { fix x, y; } SpinDef;   /* the spinner's centre (on its sensor) */

/* the lamps (inserts) a table can have; r == 0: not on this table */
enum {
    L_LANE0, L_LANE1, L_LANE2, L_LOCK0, L_LOCK1, L_LOCK2, L_JACKPOT,
    L_M0, L_M1, L_M2, L_M3, L_M4, L_MISSION, L_X2, L_X3, L_X4, L_X5,
    L_SAVE, L_SPIN0, L_SPIN1, L_SUPER, L_DROP0,
    L_COUNT = L_DROP0 + 8
};
enum { LC_BLUE, LC_YELLOW, LC_RED, LC_GREEN, LC_MAGENTA, LC_ORANGE };
typedef struct { int16_t x, y, r; int8_t col; } Lamp;

/* playfield art: flat shapes drawn under everything (render.c) */
enum { DK_DISC, DK_RING, DK_QUAD, DK_STAR, DK_TEXT };
typedef struct { int8_t kind, col; int16_t x, y, a, b; const char *text; int16_t px[4], py[4]; } Deco;

typedef struct {
    const char *name;                   /* "COMET TOUR" */
    const char *blurb;                  /* one line for the title */
    const char *mission[5];             /* the missions' names, in the browser's order */
    int theme;                          /* render.c's palette */

    Seg seg[PB_MAX_SEG]; int nseg;      /* walls, gates, slings, drop targets */
    Circ circ[PB_MAX_CIRC]; int ncirc;
    Sensor sens[PB_MAX_SENS]; int nsens;
    FlipDef flip[PB_MAX_FLIP]; int nflip;
    Ramp ramp[PB_MAX_RAMP]; int nramp;
    SpinDef spin[PB_MAX_SPIN]; int nspin;
    int ndrop, nbank;
    int drop_seg[16];                   /* drop target i -> its index in seg[] */
    fix saucer_x, saucer_y, saucer_r;
    fix plunger_x, plunger_rest;
    Lamp lamp[L_COUNT];
    Deco deco[PB_MAX_DECO]; int ndeco;

    uint16_t grid[PB_GY][PB_GX][PB_GMAX];   /* (type << 12) | index */
    uint8_t ngrid[PB_GY][PB_GX];
} Table;

enum { TB_COMET, TB_DEEP, TB_FOUNDRY, TB_COUNT };
void pb_build_table(Table *t, int which);
extern const char *const pb_table_names[TB_COUNT];

/* ---------- the world: physics only ---------- */
enum { BS_PLAY, BS_LANE, BS_RAMP, BS_SAUCER, BS_DRAINED };

typedef struct {
    int id, state, ramp;
    fix x, y, vx, vy, z;
    fix rs, rv;                         /* on a ramp: the distance along it, the speed */
    int hold;                           /* in the saucer: steps left */
    fix nocatch;                        /* seconds the saucer won't catch it */
    int autol;                          /* steps to an automatic launch, -1 none */
} Ball;

typedef struct { FlipDef d; fix ang, w; int held; } Flipper;
typedef struct { fix ang, w; } Spinner;

enum {
    EV_WALL, EV_SLING, EV_DROP, EV_BUMPER, EV_FLIPHIT, EV_FLIPSTOP, EV_SPINHIT, EV_SPIN,
    EV_SENSOR, EV_RAMPENTER, EV_RAMP, EV_RAMPFAIL, EV_SAUCER, EV_KICK, EV_LANEREST, EV_DRAIN, EV_PLUNGE,
    /* the rules' */
    EV_SCORE, EV_SKILL, EV_LANESDONE, EV_BANK, EV_BANKRESET, EV_LOCK, EV_MULTIBALL, EV_JACKPOT,
    EV_MISSIONSTART, EV_MISSIONDONE, EV_MISSIONFAIL, EV_SUPERNOVA, EV_SAVE, EV_ENDBALL, EV_GAMEOVER,
    EV_TILTWARN, EV_TILT, EV_NUDGE, EV_OUTLANE, EV_COUNT
};
extern const char *const pb_event_names[EV_COUNT];

typedef struct {
    int8_t type, id, kind, up;          /* sensor: kind, idx in id; up: crossing up the table */
    int16_t x, y;                       /* mm */
    int16_t ball;
    int32_t val;                        /* speed (mm/s), points, pull (0..1000) */
} Event;

typedef struct {
    Table *t;
    Ball balls[PB_MAX_BALLS]; int nballs;
    Flipper flip[PB_MAX_FLIP]; int nflip;
    Spinner spin[PB_MAX_SPIN];
    Event ev[PB_MAX_EV]; int nev;
    int next_id;
    int flipper_power, active_targets;
    int substeps;                       /* the last step's */
    int32_t tests;                      /* collision tests (for timing the 68k) */
} World;

void pb_world_init(World *w, Table *t);
Ball *pb_add_ball(World *w, fix x, fix y, fix vx, fix vy, int state);
void pb_world_step(World *w, fix h);
void pb_plunge(World *w, Ball *b, fix pull);
void pb_kick_saucer(World *w, Ball *b, fix jitter);   /* jitter: added to the kick's speed across */
void pb_nudge(World *w, fix dx, fix dy);
void pb_flipper_tip(const Flipper *f, fix *x, fix *y);
void pb_remove_drained(World *w);

/* exposed for the tests: -1 no contact, else the approach speed (0: touching) */
fix pb_hit_segment(Ball *b, const Seg *s);
fix pb_hit_circle(Ball *b, const Circ *c, fix *nx, fix *ny);
fix pb_hit_flipper(Ball *b, const Flipper *f);
int pb_hit_balls(Ball *a, Ball *b);
int pb_crossed(fix x0, fix y0, fix x1, fix y1, const Sensor *s);
void pb_seg_init(Seg *s, fix ax, fix ay, fix bx, fix by, fix r, fix e, int kind);
void pb_ramp_at(const Ramp *r, fix s, fix *x, fix *y, fix *z, fix *slope, fix *dx, fix *dy);

/* ---------- the rules ---------- */
enum { GS_ATTRACT, GS_PLAY, GS_BONUS, GS_OVER };
enum { MI_BUMPER, MI_BANK, MI_SPIN, MI_RAMP, MI_LANES };

typedef struct { const char *goal; int need, ev; int32_t award; } MissionDef;
extern const MissionDef pb_missions[5];

typedef struct { int32_t until, at; char big[24], small[36]; } Msg;
typedef struct { int32_t at; int kind, arg; } Timer;
enum { TM_BANK_RESET, TM_SERVE };

typedef struct {
    int left, right, plunger;           /* held */
    int pull_to;                        /* 0..1000 sets the plunger directly; -1 none */
    int nudge_x, nudge_y;               /* -1/0/1 */
} PbInput;

typedef struct {
    World w;
    Table *t;
    int state, balls_per_game, extra_balls;
    int32_t score, time;                /* time in steps */
    int ball_no;
    int lanes[3], skill_lane, skill_armed;
    int bonus_x, banks, ramps, locks;
    int missions_done;                  /* bit per mission */
    int mission, mission_n; int32_t mission_until;    /* mission: -1 none */
    int mission_lit;
    int multiball, jackpot;
    int32_t supernova, save_until, plunge_at, jackpot_lit;
    int bonus_ramps, bonus_targets, bonus_lanes, bonus_spins;
    int32_t bonus_pts, bonus_t;
    fix tilt_meter; int tilt_warnings, tilted;
    fix pull; int pull_held;
    Msg msg[PB_MAX_MSG]; int nmsg;
    Timer timers[PB_MAX_TIMER]; int ntimers;
    Event ev[PB_MAX_EV]; int nev;       /* what happened, for the sound and the lamps */
    uint32_t rng;
    int32_t acc_us;                     /* real time not yet stepped */
} Game;

void pb_game_init(Game *g, Table *t, uint32_t seed);
void pb_game_start(Game *g);
void pb_game_update(Game *g, int32_t us, const PbInput *in);   /* real time elapsed */
void pb_game_tick(Game *g, const PbInput *in);                 /* one step */
void pb_game_handle(Game *g, const Event *e);                  /* the rules' reaction to a world event */
void pb_say(Game *g, const char *big, const char *small, int32_t steps);
void pb_award(Game *g, int32_t pts, int x, int y);
void pb_mission_hit(Game *g, int ev);
void pb_do_tilt(Game *g);
int  pb_live_balls(const Game *g);

typedef struct { const char *big, *small; int flash; } Dmd;
void pb_dmd(Game *g, Dmd *d);
void pb_lamps(const Game *g, int blink, int chase, uint8_t on[L_COUNT]);
void pb_fmt(char *out, int32_t n);       /* 1234560 -> "1,234,560" */

/* the attract mode's (and the tests') player: flips when a ball comes down onto a flipper */
typedef struct { uint32_t rng; int hold[2], launch; } Bot;
void pb_bot(const Game *g, PbInput *in, Bot *bot);

#endif
