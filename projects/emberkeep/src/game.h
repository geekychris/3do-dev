/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#ifndef EMBERKEEP_GAME_H
#define EMBERKEEP_GAME_H

#include <exec/types.h>

/*
 * emberkeep - clean-room top-down cavern explorer.
 *
 * The player carries a lantern that lights only a small radius around
 * them. Everything outside the light stays pitch-black - enemies,
 * pickups and terrain features are all invisible until they enter your
 * cone. The lantern burns oil; run dry and the light collapses.
 *
 * Level geometry follows the miner_meteor tile alphabet with different
 * kinds: walls, oil flasks (refill lantern), runes (keys), portal
 * (unlocks after all runes), and open floor.
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

/* Tile kinds - packed into a UBYTE per cell. */
enum TileKind {
    T_FLOOR    = 0,     /* passable + drawn as stone floor */
    T_WALL     = 1,     /* impassable */
    T_RUNE     = 2,     /* pickup - increments runes; unlocks portal */
    T_PORTAL   = 3,     /* exit - locked until runes_remaining == 0 */
    T_OIL      = 4,     /* pickup - refills lantern fuel */
    T_HAZARD   = 5,     /* deadly on touch */
    T_KIND_COUNT
};

/* Physics constants. Top-down: no gravity, 4-directional walk. */
#define WALK_SPEED          2        /* px per frame */
#define LIGHT_RADIUS_PX     40       /* half-side of the light square */
#define LANTERN_MAX         1000     /* fuel scale */
#define LANTERN_DRAIN_FRAMES 4       /* drain 1 unit per this many frames */
#define OIL_REFILL           400     /* per flask */

/* Enemy (patrols horizontal or vertical). Kills on contact regardless
 * of visibility - you can walk into one you can't see. */
typedef struct Enemy {
    LONG  x, y;          /* pixel top-left */
    LONG  dx, dy;
    LONG  min_pos, max_pos;
    UBYTE axis;          /* 0 = horizontal, 1 = vertical */
    UBYTE colour;        /* palette hint */
    UBYTE alive;
    UBYTE frame;
} Enemy;

#define MAX_ENEMIES  6

typedef struct Player {
    LONG  x, y;
    UBYTE facing;        /* 0 up, 1 right, 2 down, 3 left */
    UBYTE frame;
    UBYTE pad[2];        /* port: explicit padding */
} Player;

enum GameMode {
    GM_TITLE     = 0,
    GM_PLAYING   = 1,
    GM_WIN       = 2,
    GM_LOSE      = 3,
    GM_LEVELWIN  = 4
};

/* Input bitfield. */
#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_UP     0x04
#define INPUT_DOWN   0x08
#define INPUT_START  0x10     /* SPACE on title / end screens */

typedef struct GameState {
    /* Widened to LONG for AB_TYPE_I32-safe bridge registration -
     * same lesson learned in canyon_rescue / miner_meteor. */
    LONG      mode;
    LONG      level_index;
    LONG      lives;
    LONG      runes_remaining;
    LONG      runes_total;
    LONG      score;
    LONG      lantern;                /* 0..LANTERN_MAX */
    LONG      state_timer;

    Player    player;
    UBYTE     grid[GRID_H][GRID_W];
    Enemy     enemies[MAX_ENEMIES];
    UBYTE     num_enemies;
    UBYTE     pad[3];      /* port: explicit padding */

    LONG      spawn_x, spawn_y;
    ULONG     tick;
} GameState;

#define RUNE_SCORE       100
#define OIL_SCORE        25
#define LEVEL_SCORE      500

/* 3DO port: the EmberGame class as C functions (game.c keeps the
 * GameState pointer the class held). */
void game_init(GameState *state);
void game_load_level(LONG index);
void game_restart_current(void);
void game_tick(UBYTE input_flags);

#endif
