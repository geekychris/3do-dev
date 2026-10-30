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
    {  2, 112 },    {  8,  32 },    {  1,  85 },    {  2,  85 },    {  4,  16 },    {  1,   8 },
    {  4,   8 },    {  1, 112 },    {  8,  48 },    {  2,  27 },    {  8,   4 },    {  2,  73 },
    {  8,  16 },
};
#define DEMO_RUNS ((int)(sizeof(DEMO_RUN) / sizeof(DEMO_RUN[0])))

#endif
