/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

/*
 * Port addition: a recorded run through the first level, played back
 * from the title screen (see README.md). One {input flags, ticks} pair
 * per run of identical input; found by a breadth-first search over the
 * game's own tick function and checked by replaying it from the title.
 */
#ifndef DEMO_RUN_H
#define DEMO_RUN_H

static const UBYTE DEMO_RUN[][2] = {
    {  1,   4 },    {  5,   1 },    {  1,   3 },    {  0,   9 },    {  2,   1 },    {  0,   1 },
    {  2,   5 },    {  6,   1 },    {  2,  69 },    {  6,   1 },    {  2,  15 },    {  1,   4 },
    {  0,   8 },    {  2,  12 },    {  0,   2 },    {  1,   9 },
};
#define DEMO_RUNS ((int)(sizeof(DEMO_RUN) / sizeof(DEMO_RUN[0])))

#endif
