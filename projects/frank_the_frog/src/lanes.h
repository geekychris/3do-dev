// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Frank the Frog - Lane objects (traffic + river)
 */
#ifndef LANES_H
#define LANES_H

#include <exec/types.h>
#include <graphics/rastport.h>

/* Initialize all lanes for a given level */
void lanes_init(int level);

/* Advance all lane objects by one frame */
void lanes_tick(void);

/* Draw all lane objects */
void lanes_draw(struct RastPort *rp);

/* Check if pixel position (px,py) collides with a vehicle. Returns 1 if hit. */
int lanes_check_car(int px, int py);

/*
 * Check if pixel position (px,py) is on a log/raft.
 * Returns 0 when the frog is in water (death), or 1 when on a safe log/raft.
 * If on a log/raft, *ride_dx receives the movement amount (with sign) to
 * apply this frame so the caller can ride the frog along.
 */
int lanes_check_river(int px, int py, int *ride_dx);

/* Check if frog is over an empty home pond. Returns its index or -1. */
int lanes_check_home(int px);

/* Centre x of home pond `slot` */
int lanes_home_cx(int slot);

/* Mark a home pond as filled */
void lanes_fill_home(int slot);

/* Returns 1 if every home pond is filled */
int lanes_all_home(void);

/* Reset home slots for new level */
void lanes_reset_home(void);

#endif /* LANES_H */
