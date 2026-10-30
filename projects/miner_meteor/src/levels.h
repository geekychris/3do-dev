/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#ifndef MINER_METEOR_LEVELS_H
#define MINER_METEOR_LEVELS_H

#include <exec/types.h>

/*
 * Cavern definition. Level tile data is a GRID_W * GRID_H ASCII string
 * (row-major, top row first) so it reads legibly in source. The
 * `guards` array is packed with (axis, x, y, dx, dy, min, max, colour)
 * quintuples, terminated by axis == 0xFF.
 *
 *   .    empty
 *   #    solid wall / floor
 *   K    key
 *   E    exit
 *   c    collapsing floor
 *   <    conveyor pushing left
 *   >    conveyor pushing right
 *   ^    hazard (spikes / lava)
 *   P    player spawn (rendered as empty)
 */

typedef struct GuardianSpec {
    UBYTE  axis;         /* 0 horizontal, 1 vertical, 0xFF = terminator */
    UBYTE  colour;
    BYTE   dx, dy;       /* px/frame */
    UWORD  x, y;         /* pixel spawn */
    UWORD  min_pos, max_pos;
} GuardianSpec;

typedef struct LevelSpec {
    const char *name;
    const char *tiles;               /* 20 * 15 = 300 chars, row-major */
    ULONG       palette_variant;     /* 0..3 - colour scheme selector (port: ULONG, no padding) */
    const GuardianSpec *guards;      /* terminated by axis==0xFF */
} LevelSpec;

/* Exposed to game.cpp / main.cpp. */
extern const LevelSpec LEVELS[];
extern const LONG      NUM_LEVELS;

#endif
