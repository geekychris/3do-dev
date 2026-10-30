/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#ifndef WARDEN_KEEP_GAME_H
#define WARDEN_KEEP_GAME_H

#include <exec/types.h>

/*
 * warden_keep - clean-room isometric room-adventure.
 *
 * A Filmation-style single-room 3D scene: an 8x8 tile stone floor,
 * perimeter walls with a doorway gap, a few loose barrels you can push
 * around, and a jewel to collect. Rendered isometrically (2:1 tiles)
 * using painter's algorithm - no real 3D engine, just sorted quads.
 *
 * Scope for this cut: one room, no enemies, no stairs. Multi-room /
 * enemies / lifts are follow-ups.
 */

/* World dimensions. */
#define WORLD_W       8
#define WORLD_H       8

/* Isometric projection. Tile screen footprint is 32x16 (2:1). */
#define ISO_TILE_W    32
#define ISO_TILE_H    16
#define ISO_CUBE_H    16       /* how much a solid cube "rises" per world Z */

/* Room / screen. */
#define SCREEN_W      320
#define SCREEN_H      256
#define PLAY_H        240
#define HUD_H         16

/* Origin for the iso-projected world (screen coords). Chosen so an
 * 8x8 grid at wz=0 (plus 3-cube-tall walls above) fits centred inside
 * the play area. Walls project upward from the floor, so ORG_Y needs
 * headroom for stack * ISO_CUBE_H = 3 * 16 = 48 px. */
#define ORG_X         160
#define ORG_Y         96

/* Tile kinds in the floor grid. */
enum FloorKind {
    F_STONE     = 0,     /* passable */
    F_DOOR      = 1,     /* passable + drawn distinct */
    F_HAZARD    = 2,     /* deadly */
    F_KIND_COUNT
};

/* Object kinds (things standing on the floor grid). One entry per
 * occupied cell. wx,wy in world tiles. */
enum ObjectKind {
    O_NONE      = 0,
    O_WALL      = 1,     /* immovable, blocks movement + sight */
    O_BARREL    = 2,     /* pushable one tile at a time */
    O_JEWEL     = 3,     /* pickup - increments score */
    O_KIND_COUNT
};

typedef struct Prop {
    UBYTE kind;
    UBYTE wx, wy;        /* world tile coordinate */
    UBYTE anim;
} Prop;

#define MAX_OBJECTS   64

typedef struct Player {
    LONG  wx, wy;        /* world tile - tile-locked movement */
    UBYTE facing;        /* 0 N, 1 E, 2 S, 3 W */
    UBYTE anim;
    UBYTE pad[2];        /* port: explicit padding */
} Player;

enum GameMode {
    GM_TITLE    = 0,
    GM_PLAYING  = 1,
    GM_WIN      = 2
};

#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_UP     0x04
#define INPUT_DOWN   0x08
#define INPUT_START  0x10

typedef struct GameState {
    /* LONG for AB_TYPE_I32-safe bridge reads. */
    LONG   mode;
    LONG   score;
    LONG   jewels_left;
    LONG   state_timer;

    Player player;

    UBYTE  floor[WORLD_H][WORLD_W];   /* FloorKind */
    Prop   objects[MAX_OBJECTS];
    LONG   num_objects;

    ULONG  tick;
} GameState;

/* Port: the WardenGame class as C functions (game.c keeps the
 * GameState pointer the class held). */
void game_init(GameState *state);
void game_load_room(void);
void game_tick(UBYTE input);

#endif
