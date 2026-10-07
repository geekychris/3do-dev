// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef FRACTALUS_PILOTS_H
#define FRACTALUS_PILOTS_H

#include <exec/types.h>

/*
 * Downed pilots scattered across the map. The player lands near one,
 * cycles the airlock, and either rescues them — or gets jumped by a
 * Jaggi wearing the pilot's flight suit. Fractalus's signature moment.
 */

#define MAX_PILOTS      12
#define PILOT_SPAWN_RADIUS  1200    /* world units around player start */

/* State per pilot slot. */
enum PilotState {
    PILOT_ACTIVE   = 0,   /* waiting to be rescued */
    PILOT_RESCUED  = 1,   /* picked up */
    PILOT_JAGGI    = 2,   /* was actually a jaggi in disguise (used) */
    PILOT_EMPTY    = 3,   /* slot unused */
};

struct Pilot {
    LONG x;         /* world coords */
    LONG z;
    LONG y;         /* terrain height at (x,z) — computed at spawn */
    UBYTE state;    /* PilotState */
    UBYTE is_jaggi; /* rolled at spawn — determines airlock outcome */
    UBYTE _pad[2];
};

class Terrain;

class PilotList {
public:
    void spawn(LONG center_x, LONG center_z, ULONG seed,
               const Terrain &world);

    /* Nearest active pilot within radius, or -1 if none. */
    LONG find_nearest(LONG x, LONG z, LONG max_radius,
                      LONG *out_dist) const;

    const Pilot &operator[](LONG i) const { return pilots[i]; }
    Pilot       &operator[](LONG i)       { return pilots[i]; }
    LONG count() const { return MAX_PILOTS; }

    LONG active_count() const;

private:
    Pilot pilots[MAX_PILOTS];
};

#endif
