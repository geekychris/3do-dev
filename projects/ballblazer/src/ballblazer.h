// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Ballblazer — Amiga port of Lucasfilm's 1985 futuristic sport.
 *
 * Two rotofoils on a checkered grid pitch, split-screen (top pane
 * = P2/CPU view, bottom pane = P1/human view). Each rotofoil auto-
 * faces the ball; players control forward/back speed and lateral
 * strafing. Ball attaches when close, releases when pushed. Goals
 * are hoverbeams at each end of the pitch.
 *
 * Coordinate system:
 *   +X = down-pitch toward P2 goal
 *   -X = down-pitch toward P1 goal
 *   +Z = P1's right when facing +X (grid width axis)
 *   Y  = up (rotofoils and ball hover at Y = HOVER_H above ground)
 *
 * All coordinates are 16.16 fixed-point unless suffixed _i (raw int).
 */

#ifndef BALLBLAZER_H
#define BALLBLAZER_H

#include <exec/types.h>

#define FP    16
#define ONE   (1L << FP)

/* ---- display geometry --------------------------------------------- */
#define SCR_W       320
#define SCR_H       256
#define SCR_DEPTH   5           /* 32 colours */
#define PANE_H      (SCR_H / 2) /* 128 rows each */
#define PANE_P1_Y0  PANE_H      /* human pane at bottom */
#define PANE_P2_Y0  0           /* CPU pane at top */
#define HORIZON_Y   (PANE_H / 3)/* horizon 1/3 down each pane, so ground */
                                /* fills the lower 2/3 */

/* ---- pitch dimensions --------------------------------------------- */
#define PITCH_LENGTH   (100L * ONE)   /* +/- along X: goal at +/-100 */
#define PITCH_WIDTH    ( 40L * ONE)   /* +/- along Z: sidelines */
#define GRID_STEP      (  5L * ONE)   /* grid square = 5 world units */
#define HOVER_H        (  2L * ONE)   /* rotofoils & ball hover this high */
#define BALL_RADIUS    ( ONE)         /* world-space radius */

/* ---- palette pens (5 planes = 32 pens) ---------------------------- */
#define PEN_BLACK      0     /* backdrop / horizon separator */
#define PEN_SKY_TOP    1     /* upper sky (P2/CPU pane) */
#define PEN_SKY_BOT    2     /* upper sky (P1/human pane) */
#define PEN_GRID_A     3     /* checker A */
#define PEN_GRID_B     4     /* checker B */
#define PEN_GRID_LINE  5     /* grid line colour */
#define PEN_GOAL_P1    6     /* P1's goal beam */
#define PEN_GOAL_P2    7     /* P2's goal beam */
#define PEN_BALL_DARK  8     /* ball shadow edge */
#define PEN_BALL       9     /* mid red */
#define PEN_BALL_HI   10     /* highlight */
#define PEN_ROTO_P1   11     /* player 1 rotofoil body */
#define PEN_ROTO_P2   12     /* player 2 rotofoil body */
#define PEN_ROTO_EDGE 13     /* rotofoil dark outline */
#define PEN_HUD_BG    14
#define PEN_HUD_FG    15
#define PEN_WHITE     16

/* ---- rotofoil state ----------------------------------------------- */
typedef struct {
    LONG x, z;        /* world position (fixed-point) */
    LONG vx, vz;      /* world velocity (fixed-point) */
    LONG angle;       /* facing angle degrees (0..359), auto-set to ball */
    int  has_ball;    /* nonzero when carrying the ball */
} Rotofoil;

/* ---- ball state --------------------------------------------------- */
typedef struct {
    LONG x, z;
    LONG vx, vz;
    int  carrier;     /* -1 = free, 0 = P1, 1 = P2 */
} Ball;

/* ---- pitch renderer ----------------------------------------------- */
/*
 * Render the checkered pitch + ball into one pane, from the given
 * camera. `pane_y0` is the pane's top scanline; the renderer only
 * touches rows [pane_y0 .. pane_y0 + PANE_H - 1].
 */
struct RastPort;
void pitch_render(struct RastPort *rp, int pane_y0,
                  LONG cam_x, LONG cam_z, LONG cam_angle,
                  const Ball *ball,
                  LONG other_x, LONG other_z, UBYTE other_pen);

/* Integer sine / cosine from a 360-entry lookup — same shape as the
 * one in ham_raster. */
void  math_init(void);
LONG  math_sin(LONG deg);
LONG  math_cos(LONG deg);

#endif
