/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#ifndef MINER_METEOR_GAME_H
#define MINER_METEOR_GAME_H

#include <exec/types.h>

/*
 * miner-meteor - single-screen tile platformer.
 *
 * Clean-room reimplementation of the mechanics popularised by 8-bit
 * platformers: a fixed room of solid tiles, keys to collect, a locked
 * exit that opens after the last key, moving guardians that kill on
 * touch, conveyors that push you horizontally, collapsing floors that
 * evaporate a few frames after you step on them, and an air/oxygen
 * timer that drains regardless of action.
 *
 * Grid: 20 wide x 15 tall tiles, each 16x16 px -> 320x240 play area.
 * Screen 320x256 leaves 16px at the bottom for HUD. All positions
 * are in tile units (integer) or pixels (LONG) where indicated.
 */

#define TILE_W          16
#define TILE_H          16
#define GRID_W          20
#define GRID_H          15
#define PLAY_W          (GRID_W * TILE_W)     /* 320 */
#define PLAY_H          (GRID_H * TILE_H)     /* 240 */
#define HUD_H           16
#define SCREEN_W        320
#define SCREEN_H        (PLAY_H + HUD_H)      /* 256 */

/* Tile kinds - small enough to pack into a UBYTE per cell. */
enum TileKind {
    T_EMPTY     = 0,
    T_SOLID     = 1,     /* wall / floor */
    T_KEY       = 2,     /* collectible; consumed on touch */
    T_EXIT      = 3,     /* locked until keys_remaining == 0 */
    T_COLLAPSE  = 4,     /* steppable once; disappears after COLLAPSE_FRAMES */
    T_CONV_L    = 5,     /* solid + pushes player left when stood on */
    T_CONV_R    = 6,     /* solid + pushes player right when stood on */
    T_HAZARD    = 7,     /* deadly (spikes / lava) - kills on touch */
    T_KIND_COUNT
};

/* Per-tile mutable state (only collapse timer for now). */
typedef struct TileState {
    UBYTE kind;
    UBYTE timer;         /* collapse countdown; 0 = intact / non-collapsing */
    UBYTE pad[2];        /* port: explicit padding (Norcroft pads structs to words) */
} TileState;

/* Physics constants (units: pixels, frames). */
#define GRAVITY             1        /* vy += GRAVITY each frame while airborne */
#define MAX_FALL_SPEED      6        /* clamp downward vy */
#ifndef JUMP_IMPULSE
/* Port fix (bug in the original): (-8) on the Amiga. That jump rises 36 px, but the keys sit
 * on platforms 64 px above where you can stand, so no cavern could be
 * completed. (-12) rises 66 px: every key and exit of caverns 1, 3, 4
 * and 5 can then be reached
 * (2 and 6 also needed level changes, see the levels file). Set (-8) for
 * the original jump. */
#define JUMP_IMPULSE        (-12)    /* upward vy on jump */
#endif
#define WALK_SPEED          2        /* horizontal step per frame */
#define CONVEYOR_PUSH       1        /* extra px/frame on conveyors */
#define COLLAPSE_FRAMES     18       /* tile persists this many frames after step */

/* Guardian (moving hazard). Patrols horizontally OR vertically between
 * min/max, in tile-unit coordinates but with pixel-precise motion. */
typedef struct Guardian {
    LONG  x, y;          /* pixel top-left */
    LONG  dx, dy;        /* velocity in px/frame */
    LONG  min_pos;       /* patrol bound: px, either x-min or y-min */
    LONG  max_pos;       /* patrol bound: px, either x-max or y-max */
    UBYTE axis;          /* 0 = horizontal patrol, 1 = vertical */
    UBYTE colour;        /* palette index */
    UBYTE alive;         /* 0 = disabled slot */
    UBYTE frame;         /* animation counter */
} Guardian;

#define MAX_GUARDIANS  6

/* Ship / player state. */
typedef struct Player {
    LONG   x, y;         /* pixel top-left */
    LONG   vx, vy;
    UBYTE  facing;       /* 0 = left, 1 = right */
    UBYTE  on_ground;
    UBYTE  frame;        /* 0..3 walk cycle counter, wraps every 8 ticks */
    UBYTE  jump_lock;    /* prevents auto-jump if jump held from spawn */
} Player;

/* Top-level modes. */
enum GameMode {
    GM_TITLE    = 0,
    GM_PLAYING  = 1,
    GM_WIN      = 2,     /* all caverns cleared */
    GM_LOSE     = 3,     /* out of lives */
    GM_LEVELWIN = 4      /* brief pause before next cavern */
};

/* Input flags (bitfield). */
#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_JUMP   0x04
#define INPUT_START  0x08     /* SPACE on title / after game over */

struct Level;

typedef struct GameState {
    /* Widened to LONG so ab_register_var(AB_TYPE_I32) reads cleanly -
     * same lesson we learned in canyon_rescue with UBYTE/UWORD aliasing. */
    LONG      mode;
    LONG      level_index;
    LONG      lives;
    LONG      keys_remaining;
    LONG      keys_total;
    LONG      score;
    LONG      air;                        /* 0..1000 */
    LONG      state_timer;                /* generic ticks-until-next */

    Player    player;
    TileState grid[GRID_H][GRID_W];
    Guardian  guards[MAX_GUARDIANS];
    UBYTE     num_guards;
    UBYTE     pad[3];      /* port: explicit padding */

    /* Cached spawn point (pixel coords) - respawn after death. */
    LONG      spawn_x;
    LONG      spawn_y;

    /* Frame counter for animation timing. */
    ULONG     tick;
} GameState;

/* Air drains 1 unit per this many frames. Game runs ~25 fps so
 * 1000 / (25 / AIR_DRAIN_FRAMES) seconds of oxygen. AIR_DRAIN_FRAMES=6
 * -> ~240s per cavern, matches the source game's oxygen pacing. */
#define AIR_DRAIN_FRAMES  6
#define AIR_MAX           1000
#define KEY_SCORE         100
#define LEVEL_SCORE       1000

/* 3DO port: the MinerGame class as C functions (game.c keeps the
 * GameState pointer the class held). */
void game_init(GameState *state);
void game_load_level(LONG index);
void game_restart_current(void);
void game_tick(UBYTE input_flags);

#endif
