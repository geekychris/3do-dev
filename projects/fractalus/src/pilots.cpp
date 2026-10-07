// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "pilots.h"
#include "terrain.h"

/* Debug knob — settable from the bridge. 0 = normal 33% jaggi rate,
 * 1 = force every pilot to be a jaggi (for animating/testing the
 * jumpscare path without hunting for one). Read by spawn() only at
 * mission-start, so toggle it then press RETURN to regen. */
LONG g_debug_all_jaggis = 0;

static ULONG plseed;
static inline ULONG pl_rng()
{
    plseed = plseed * 1664525UL + 1013904223UL;
    return plseed;
}

void PilotList::spawn(LONG center_x, LONG center_z, ULONG seed,
                      const Terrain &world)
{
    plseed = seed ? seed : 0xDEADBEEFUL;

    for (LONG i = 0; i < MAX_PILOTS; i++) {
        /* Uniform in a square around the player, biased away from
         * dead-centre to spread them out. */
        LONG dx = (LONG)(pl_rng() % (2 * PILOT_SPAWN_RADIUS))
                - PILOT_SPAWN_RADIUS;
        LONG dz = (LONG)(pl_rng() % (2 * PILOT_SPAWN_RADIUS))
                - PILOT_SPAWN_RADIUS;
        pilots[i].x = center_x + dx;
        pilots[i].z = center_z + dz;
        /* Precompute ground altitude — the renderer projects pilots
         * as sprites sitting on the terrain surface. */
        pilots[i].y = world.height_at_world(pilots[i].x, pilots[i].z);
        pilots[i].state = PILOT_ACTIVE;
        /* Normal: 33% chance each pilot is a jaggi. Debug override
         * forces all pilots to be jaggis for animation testing. */
        if (g_debug_all_jaggis) {
            pilots[i].is_jaggi = 1;
        } else {
            pilots[i].is_jaggi = ((pl_rng() % 100) < 33) ? 1 : 0;
        }
    }
}

LONG PilotList::find_nearest(LONG x, LONG z, LONG max_radius,
                             LONG *out_dist) const
{
    LONG best_i = -1;
    /* Clamp max_radius so max_radius^2 fits in signed 32-bit LONG:
     * sqrt(2^31 - 1) ≈ 46340. Renderer passes 99999 as "unlimited"
     * which would overflow to a negative best_d2 and make every
     * pilot appear farther than "-inf", i.e. never selected. */
    if (max_radius > 46340) max_radius = 46340;
    LONG best_d2 = max_radius * max_radius;
    for (LONG i = 0; i < MAX_PILOTS; i++) {
        if (pilots[i].state != PILOT_ACTIVE) continue;
        LONG dx = pilots[i].x - x;
        LONG dz = pilots[i].z - z;
        LONG d2 = dx * dx + dz * dz;
        if (d2 < best_d2) {
            best_d2 = d2;
            best_i = i;
        }
    }
    if (best_i >= 0 && out_dist) {
        /* Cheap integer sqrt — accurate to ±1. */
        LONG v = best_d2, r = 0, bit = 1L << 30;
        while (bit > v) bit >>= 2;
        while (bit) {
            if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
            else r >>= 1;
            bit >>= 2;
        }
        *out_dist = r;
    }
    return best_i;
}

LONG PilotList::active_count() const
{
    LONG n = 0;
    for (LONG i = 0; i < MAX_PILOTS; i++)
        if (pilots[i].state == PILOT_ACTIVE) n++;
    return n;
}
