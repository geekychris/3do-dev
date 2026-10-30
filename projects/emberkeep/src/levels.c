/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#include "levels.h"

/* Four starter chambers exercise the mechanics gradually:
 *
 *   L0  Guiding Hall    - plain floor, corridors, one rune -> portal
 *   L1  Oil Cellar      - need to top up lantern from oil flasks
 *   L2  Watchful Halls  - first patrolling enemies + hazards
 *   L3  Full Depths     - all elements, longer path
 *
 * Data lives in ASCII strings for hand-legible authoring. Engine
 * scales to 20+ chambers; more content is a follow-up.
 */

/* Port fix (bug in the original): row 5 was "#..#............####", which
 * sealed the rune at column 6, row 6 inside a walled box with no way in, so
 * this chamber could not be completed. FIX_SEALED_RUNE opens columns 17-18
 * of row 5 to the corridor above; set it to 0 for the original layout. */
#ifndef FIX_SEALED_RUNE
#define FIX_SEALED_RUNE 1
#endif

static const char LVL0[] =
    "####################"
    "#..................#"
    "#..P..........R....#"
    "#..................#"
    "#..#############...#"
#if FIX_SEALED_RUNE
    "#..#............#..#"
#else
    "#..#............####"
#endif
    "#..#..R............."
    "#..#............####"
    "#..#############...#"
    "#..................#"
    "#....R.........O...#"
    "#..................#"
    "#..............E...#"
    "#..................#"
    "####################";

static const EnemySpec LVL0_ENEMIES[] = {
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

static const char LVL1[] =
    "####################"
    "#..................#"
    "#..P...O...........#"
    "#..................#"
    "####.###############"
    "#..R...............#"
    "#..................#"
    "#..###########.....#"
    "#..O.........#.....#"
    "#............#.O...#"
    "#..###########.....#"
    "#..................#"
    "#..R...........E...#"
    "#..................#"
    "####################";

static const EnemySpec LVL1_ENEMIES[] = {
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

static const char LVL2[] =
    "####################"
    "#..................#"
    "#..P............R..#"
    "#..................#"
    "#..###.......###...#"
    "#..............^...#"
    "#..O...............#"
    "#..............^^..#"
    "#..###.......###...#"
    "#..................#"
    "#..R....^^.....O...#"
    "#..................#"
    "#..............E...#"
    "#..................#"
    "####################";

static const EnemySpec LVL2_ENEMIES[] = {
    { 0, 4, +2, 0, 48, 96, 32, 288 },
    { 0, 4, -2, 0, 240, 176, 32, 288 },
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

static const char LVL3[] =
    "####################"
    "#..R..O............#"
    "#..................#"
    "###.###..###.###.###"
    "#..................#"
    "#..R....^^.^^..R...#"
    "#..................#"
    "###.###..###.###.###"
    "#..O...............#"
    "#............^^....#"
    "#..R.....O......R..#"
    "#..................#"
    "#..P...........E...#"
    "#..................#"
    "####################";

static const EnemySpec LVL3_ENEMIES[] = {
    { 0, 5, +2, 0, 48, 48, 32, 288 },
    { 0, 4, +3, 0, 240, 144, 32, 288 },
    { 0, 6, -2, 0, 240, 176, 32, 288 },
    { 1, 5, 0, +2, 128, 64, 64, 192 },
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

const LevelSpec LEVELS[] = {
    { "GUIDING HALL",    LVL0, LVL0_ENEMIES },
    { "OIL CELLAR",      LVL1, LVL1_ENEMIES },
    { "WATCHFUL HALLS",  LVL2, LVL2_ENEMIES },
    { "FULL DEPTHS",     LVL3, LVL3_ENEMIES },
};

const LONG NUM_LEVELS = (LONG)(sizeof(LEVELS) / sizeof(LEVELS[0]));
