/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

/*
 * Port addition: a recorded run through the first level, played back
 * from the title screen (see README.md). One {input flags, ticks} pair
 * per run of identical input: cavern 1 of tools/routes.json, found by a
 * breadth-first search over the game's own tick function (tools/route_search.cpp)
 * and checked by replaying it from the title.
 */
#ifndef DEMO_RUN_H
#define DEMO_RUN_H

static const UBYTE DEMO_RUN[][2] = {
    {  1,   4 },    {  5,   1 },    {  1,   3 },    {  0,   6 },    {  2,  79 },    {  6,   1 },
    {  2,  20 },    {  4,   1 },    {  1,  13 },    {  4,   1 },    {  0,  10 },    {  2,   5 },
    {  1,  43 },    {  5,   1 },    {  1,  21 },    {  2,   1 },    {  6,   1 },    {  2,  11 },
    {  0,   5 },    {  2,  44 },
};
#define DEMO_RUNS ((int)(sizeof(DEMO_RUN) / sizeof(DEMO_RUN[0])))

#endif
