/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#include "levels.h"

/*
 * Six starter caverns. Each demonstrates one or two mechanics
 * introduced by the tile alphabet in levels.h:
 *
 *   L0  First Shaft         - plain floors + keys (jump / walk baseline)
 *   L1  Hazard Bay          - stationary hazards + air pacing
 *   L2  The Escalators      - conveyor belts
 *   L3  Guardian Alley      - patrolling horizontal guardians
 *   L4  Collapsing Attic    - collapsing floors that eat the ground
 *   L5  Full Ensemble       - every mechanic in one room
 *
 * Level authoring room for 14 more caverns before hitting the ticket's
 * "20+ caverns" bar; that pack is a follow-up task. The engine is
 * already sized for it (NUM_LEVELS is derived at build time).
 */

/* --- Level 0 : First Shaft ----------------------------------------- */

static const char LVL0_TILES[] =
    /* 0         1         2  <- col tens */
    /* 01234567890123456789 */
    "####################"   /* 0 */
    "#..................#"   /* 1 */
    "#..................#"   /* 2 */
    "#....K........K....#"   /* 3 */
    "#....##.......##...#"   /* 4 */
    "#..................#"   /* 5 */
    "#..................#"   /* 6 */
    "#........####......#"   /* 7 */
    "#..K...............#"   /* 8 */
    "#..##..............#"   /* 9 */
    "#..............K...#"   /* 10 */
    "#..............##..#"   /* 11 */
    "#..P.........E.....#"   /* 12 */
    "####################"   /* 13 */
    "####################"   /* 14 */
    ;

static const GuardianSpec LVL0_GUARDS[] = {
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

/* --- Level 1 : Hazard Bay ------------------------------------------ */

static const char LVL1_TILES[] =
    "####################"
    "#..................#"
    "#....K.......K.....#"
    "#....##......##....#"
    "#..................#"
    "#..###..........###."
    "#..................#"
    "#.^^^^....K....^^^.#"
    "#..................#"
    "#..............##..#"
    "#....##............#"
    "#..................#"
    "#..P.....^.^^....E.#"   /* bug in the original: unwinnable (was ^^^^, no jump clears it) */
    "####################"
    "####################"
    ;

static const GuardianSpec LVL1_GUARDS[] = {
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

/* --- Level 2 : The Escalators -------------------------------------- */

/* Full-width conveyor with a single drop-through gap at alternating
 * ends per tier - walk against the push to reach the gap and fall to
 * the row below. Zig-zag descent from row 3 down to the exit floor. */
static const char LVL2_TILES[] =
    "####################"
    "#..................#"
    "#........K.........#"
    "#>>>>>>>>>>>>>>>>>.#"   /* gap col 18 -> fall right */
    "#..................#"
    "#..K............K..#"
    "#.<<<<<<<<<<<<<<<<<#"   /* gap col 1  -> fall left  */
    "#..................#"
    "#........K.........#"
    "#>>>>>>>>>>>>>>>>>.#"   /* gap col 18 -> fall right */
    "#..................#"
    "#..................#"
    "#..P.............E.#"
    "####################"
    "####################"
    ;

static const GuardianSpec LVL2_GUARDS[] = {
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

/* --- Level 3 : Guardian Alley -------------------------------------- */

static const char LVL3_TILES[] =
    "####################"
    "#..................#"
    "#.........K........#"
    "#.......######.....#"
    "#..................#"
    "#..K...........K...#"
    "#..####.......####.#"
    "#..................#"
    "#..................#"
    "#....##......##....#"
    "#..................#"
    "#..................#"
    "#..P.............E.#"
    "####################"
    "####################"
    ;

static const GuardianSpec LVL3_GUARDS[] = {
    /* Two horizontal patrols on the empty corridors + one on the top ledge. */
    { 0, 4, +2, 0, 32, 48, 32, 288 },
    { 0, 6, -2, 0, 240, 128, 32, 288 },
    { 0, 5, +2, 0, 128, 176, 96, 224 },
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

/* --- Level 4 : Collapsing Attic ------------------------------------ */

static const char LVL4_TILES[] =
    "####################"
    "#..................#"
    "#.........K........#"
    "#........cccc......#"
    "#..................#"
    "#..K...............#"
    "#.cccc.............#"
    "#..............K...#"
    "#............cccc..#"
    "#..................#"
    "#....K.............#"
    "#..cccc............#"
    "#..P.............E.#"
    "####################"
    "####################"
    ;

static const GuardianSpec LVL4_GUARDS[] = {
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

/* --- Level 5 : Full Ensemble --------------------------------------- */

static const char LVL5_TILES[] =
    "####################"
    "#..................#"
    "#.........K........#"
    "#>>>>.>>>>>>>>>>>>>#"   /* bug in the original: unwinnable - gap up to the top key */
    "#..................#"
    "#..K......^^....K..#"
    "#..##..........##..#"
    "#..................#"
    "#..cccc........cccc#"
    "#..................#"
    "#<<<<<.<<<<<<<.<<<<#"   /* bug in the original: unwinnable - gaps up (col 6) and down past the spikes (col 14) */
    "#....#.............#"   /* bug in the original: unwinnable - step up to the col 6 gap */
    "#..P....^^^^.....E.#"
    "####################"
    "####################"
    ;

static const GuardianSpec LVL5_GUARDS[] = {
    { 0, 4, +2, 0, 48, 80, 32, 288 },      /* top level patroller */
    { 0, 6, +3, 0, 200, 176, 32, 288 },    /* mid patroller (fast) */
    { 0, 5, -2, 0, 240, 144, 32, 288 },    /* mid-lower patroller */
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

/* --- Table --------------------------------------------------------- */

const LevelSpec LEVELS[] = {
    { "FIRST SHAFT",      LVL0_TILES, 0, LVL0_GUARDS },
    { "HAZARD BAY",       LVL1_TILES, 1, LVL1_GUARDS },
    { "THE ESCALATORS",   LVL2_TILES, 2, LVL2_GUARDS },
    { "GUARDIAN ALLEY",   LVL3_TILES, 3, LVL3_GUARDS },
    { "COLLAPSING ATTIC", LVL4_TILES, 0, LVL4_GUARDS },
    { "FULL ENSEMBLE",    LVL5_TILES, 1, LVL5_GUARDS },
};

const LONG NUM_LEVELS = (LONG)(sizeof(LEVELS) / sizeof(LEVELS[0]));
