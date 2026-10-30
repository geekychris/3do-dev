// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * bb.h - Block Breaker 3D's game logic: breakout down a tunnel. The
 * paddle is a frame on the tunnel's near opening (z = 0) that moves up,
 * down, left and right; the ball flies down the tunnel to a block of
 * bricks at the far end (6 across, 4 high, 3 deep) and back. Where the
 * ball meets the paddle steers it.
 *
 * World units, y up, z into the tunnel; positions and speeds are kept
 * in 1/256 units. One tick is 40 ms. No Amiga headers: tested on the
 * host too.
 */
#ifndef BB_GAME_H
#define BB_GAME_H

#include <stdint.h>

#define BB_COLS    6
#define BB_ROWS    4
#define BB_LAYERS  3
#define BB_CELLS   (BB_COLS * BB_ROWS * BB_LAYERS)
#define BB_W       168                  /* the tunnel's half width */
#define BB_H       120                  /* and half height */
#define BB_D       800                  /* the back wall's z */
#define BB_CW      (2 * BB_W / BB_COLS) /* a brick: 56 x 60 x 60 */
#define BB_CH      (2 * BB_H / BB_ROWS)
#define BB_CD      60
#define BB_Z0      (BB_D - BB_LAYERS * BB_CD)   /* the bricks' front */
#define BB_R       10                   /* the ball's radius */
#define BB_PAD_W   40                   /* the paddle's half size */
#define BB_PAD_H   28
#define BB_WIDE_W  62
#define BB_WIDE_H  42
#define BB_PAD_SPEED 13                 /* units a tick */
#define BB_LOST_Z  (-60)                /* past the paddle this far: lost */
#define BB_LIVES   3
#define BB_LEVELS  8
#define BB_MAX_CAPS 4
#define BB_POWER_TICKS 375              /* 15 s */
#define BB_BETWEEN 50                   /* ticks between levels */
#define BB_FX      256                  /* 1/256 units */

/* bricks */
enum { BK_EMPTY, BK_RED, BK_ORANGE, BK_YELLOW, BK_GREEN, BK_CYAN, BK_BLUE, BK_TOUGH, BK_STEEL, BK_BONUS, BK_COUNT };
/* capsules: the paddle wider, the ball slower, a life, and the ball
 * ploughing through bricks without bouncing */
enum { CAP_WIDE, CAP_SLOW, CAP_LIFE, CAP_PLOUGH, CAP_COUNT };

/* input bits */
#define BB_IN_LEFT  1
#define BB_IN_RIGHT 2
#define BB_IN_UP    4
#define BB_IN_DOWN  8
#define BB_IN_FIRE  16

/* events, for sound, particles and the log */
enum { BBE_NONE, BBE_WALL, BBE_PADDLE, BBE_HIT, BBE_BREAK, BBE_STEEL, BBE_LOST, BBE_SERVE,
       BBE_CAPSULE, BBE_CAP_MISS, BBE_CLEAR, BBE_LEVEL, BBE_OVER, BBE_WIN, BBE_COUNT };
typedef struct { int type, a, b; } BBEvent;
#define BB_MAX_EVENTS 24

typedef struct { int32_t x, y, z; int type, active; } Capsule;

typedef struct {
    uint32_t seed, rng;
    uint8_t cell[BB_CELLS];             /* BK_*: [layer][row][col], layer 0 nearest, row 0 lowest */
    uint8_t hits[BB_CELLS];             /* hits left */
    int level, lives, over, won, left;  /* left: breakable bricks */
    int32_t score;
    int32_t px, py;                     /* the paddle's centre, 1/256 units */
    int32_t bx, by, bz, vx, vy, vz;     /* the ball */
    int32_t speed;                      /* the ball's speed, 1/256 units a tick */
    int stuck;                          /* on the paddle, waiting for fire */
    int between;                        /* ticks to the next level */
    int wide_t, slow_t, plough_t;       /* capsules' time left */
    int paddle_hits, broken, tick, caps_dropped;
    Capsule cap[BB_MAX_CAPS];
    BBEvent ev[BB_MAX_EVENTS];
    int nev;
} BBGame;

extern const char *const bb_event_names[BBE_COUNT];
extern const char *const bb_cap_names[CAP_COUNT];
extern const char *const bb_level_names[BB_LEVELS];

static inline int bb_index(int c, int r, int l) { return (l * BB_ROWS + r) * BB_COLS + c; }
int  bb_cell_box(int i, int32_t *x0, int32_t *y0, int32_t *z0);   /* a cell's low corner, world units */
int  bb_breakable(int type);
uint32_t bb_rand(BBGame *g);

void bb_new_game(BBGame *g, uint32_t seed, int level);
void bb_load_level(BBGame *g, int level);
void bb_tick(BBGame *g, unsigned in);
int  bb_pad_w(const BBGame *g);
int  bb_pad_h(const BBGame *g);
int  bb_pop_event(BBGame *g, BBEvent *e);
unsigned bb_bot(const BBGame *g);       /* the autopilot's input */
uint32_t bb_state_hash(const BBGame *g);
void bb_clear_all(BBGame *g);           /* tests and the `clear` hook: break every breakable brick */

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
