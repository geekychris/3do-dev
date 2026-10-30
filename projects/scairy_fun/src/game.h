// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.h - the frame loop's game side: input, the fixed 25 Hz simulation,
 * what to draw, sound and voice for the simulation's events, and
 * everything exposed over the AmigaBridge.
 */
#ifndef GAME_H
#define GAME_H

#include "sc.h"
#include "render.h"
#include "a3d_plat.h"                   /* the shared engine's mains call game_init ... game_after_frame */

extern World world;
extern int paused;

#endif
