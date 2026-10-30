/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#ifndef EMBERKEEP_LEVELS_H
#define EMBERKEEP_LEVELS_H

#include <exec/types.h>

/*
 * ASCII cavern grid - 20 * 15 chars, row-major:
 *
 *   .   floor
 *   #   wall
 *   R   rune (key)
 *   E   portal (exit)
 *   O   oil flask
 *   ^   hazard (deadly on touch)
 *   P   player spawn (rendered as floor)
 *
 * 3DO port: C version of levels.h (typedefs instead of C++ struct names).
 */

typedef struct EnemySpec {
    UBYTE axis;          /* 0 = horizontal, 1 = vertical, 0xFF = terminator */
    UBYTE colour;
    BYTE  dx, dy;
    UWORD x, y;
    UWORD min_pos, max_pos;
} EnemySpec;

typedef struct LevelSpec {
    const char       *name;
    const char       *tiles;
    const EnemySpec  *enemies;
} LevelSpec;

extern const LevelSpec LEVELS[];
extern const LONG      NUM_LEVELS;

#endif
