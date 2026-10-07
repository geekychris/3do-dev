// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "terrain.h"
#include "fixed.h"

/*
 * Midpoint displacement (diamond-square variant, square-only for
 * simplicity). We generate a heightfield at TERRAIN_SIZE resolution
 * with wraparound edges — no seams when the player crosses the world
 * boundary.
 *
 * Each recursion halves the step and halves the perturbation range,
 * which gives self-similar detail at every scale — the whole point of
 * Fractalus's terrain style.
 */

static ULONG rng_state;

static inline ULONG rng(void)
{
    /* Linear congruential — Numerical Recipes constants. Cheap. */
    rng_state = rng_state * 1664525UL + 1013904223UL;
    return rng_state;
}

/* Return a signed perturbation in the range [-amp, +amp]. */
static inline LONG jitter(LONG amp)
{
    if (amp <= 0) return 0;
    return (LONG)(rng() % (ULONG)(2 * amp + 1)) - amp;
}

/* Wrap into [0, TERRAIN_SIZE). */
static inline LONG w(LONG v) { return v & TERRAIN_MASK; }

/* Convenience: read/write the wrapped heightfield. */
static inline UBYTE gh(const UBYTE *H, LONG z, LONG x)
{
    return H[w(z) * TERRAIN_SIZE + w(x)];
}
static inline void sh(UBYTE *H, LONG z, LONG x, LONG v)
{
    if (v < 0)   v = 0;
    if (v > 255) v = 255;
    H[w(z) * TERRAIN_SIZE + w(x)] = (UBYTE)v;
}

void Terrain::midpoint_displace(ULONG seed)
{
    rng_state = seed ? seed : 0xC0FFEE01UL;
    UBYTE *H = heights;

    /* Seed heights[0] — with wraparound this doubles as all four
     * outermost corners for the first diamond. */
    sh(H, 0, 0, 128);

    LONG step = TERRAIN_SIZE;
    LONG amp  = 128;                /* was 96 — bigger peaks */

    while (step > 1) {
        LONG half = step >> 1;
        LONG x, z;

        /* Diamond: centre of each square = avg of 4 corners + jitter. */
        for (z = 0; z < TERRAIN_SIZE; z += step) {
            for (x = 0; x < TERRAIN_SIZE; x += step) {
                LONG a = gh(H, z,        x);
                LONG b = gh(H, z,        x + step);
                LONG c = gh(H, z + step, x);
                LONG d = gh(H, z + step, x + step);
                sh(H, z + half, x + half, ((a + b + c + d) >> 2) + jitter(amp));
            }
        }

        /* Square: each square owns its NORTH edge midpoint (z, x+half)
         * and its WEST edge midpoint (z+half, x). Neighbours own the
         * east/south edges via wraparound. Each midpoint is the average
         * of 2 corners + 2 diamond centres (the one just filled + the
         * one in the adjacent square, which also just got filled). */
        for (z = 0; z < TERRAIN_SIZE; z += step) {
            for (x = 0; x < TERRAIN_SIZE; x += step) {
                /* West edge midpoint at (z+half, x). */
                {
                    LONG a = gh(H, z,          x);         /* NW corner */
                    LONG b = gh(H, z + step,   x);         /* SW corner */
                    LONG c = gh(H, z + half,   x - half);  /* W neighbour diamond */
                    LONG d = gh(H, z + half,   x + half);  /* this diamond */
                    sh(H, z + half, x, ((a + b + c + d) >> 2) + jitter(amp));
                }
                /* North edge midpoint at (z, x+half). */
                {
                    LONG a = gh(H, z,          x);         /* NW corner */
                    LONG b = gh(H, z,          x + step);  /* NE corner */
                    LONG c = gh(H, z - half,   x + half);  /* N neighbour diamond */
                    LONG d = gh(H, z + half,   x + half);  /* this diamond */
                    sh(H, z, x + half, ((a + b + c + d) >> 2) + jitter(amp));
                }
            }
        }

        step = half;
        amp  = (amp * 6) >> 3;   /* roughness = 0.75 per octave — keeps
                                    high-freq detail alive so the terrain
                                    reads as rough rather than rolling */
    }
}

void Terrain::smooth()
{
    /* One 3x3 box blur pass — knocks off the sharp midpoint artefacts
     * that otherwise produce single-pixel spikes when the ray-march
     * grazes a ridge. Wraparound edges. static so we don't blow the
     * stack — 16 KB scratch is too big for a call frame. */
    static UBYTE tmp[TERRAIN_SIZE * TERRAIN_SIZE];
    for (LONG z = 0; z < TERRAIN_SIZE; z++) {
        for (LONG x = 0; x < TERRAIN_SIZE; x++) {
            LONG sum = 0;
            for (LONG dz = -1; dz <= 1; dz++)
                for (LONG dx = -1; dx <= 1; dx++)
                    sum += heights[w(z + dz) * TERRAIN_SIZE + w(x + dx)];
            tmp[z * TERRAIN_SIZE + x] = (UBYTE)(sum / 9);
        }
    }
    for (LONG i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++)
        heights[i] = tmp[i];
}

void Terrain::generate(ULONG seed)
{
    for (LONG i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) heights[i] = 0;
    midpoint_displace(seed);
    /* Skip smooth() — was averaging out the sharp midpoint spikes and
     * making the terrain look too rolling. The raycaster's adaptive
     * step naturally averages micro-detail so aliasing isn't a problem. */
}

LONG Terrain::height_at_world(LONG wx, LONG wz) const
{
    /* Bilinear sample so landing feels smooth rather than stepping
     * between cells. Convert world-units -> cells scaled by 2^TRIG_SHIFT
     * (integer cell index in high bits, sub-cell fraction in low). */
    LONG fx = wx << (TRIG_SHIFT - TERRAIN_CELL_SHIFT);
    LONG fz = wz << (TRIG_SHIFT - TERRAIN_CELL_SHIFT);
    LONG gx = fx >> TRIG_SHIFT;
    LONG gz = fz >> TRIG_SHIFT;
    LONG tx = fx & ((1L << TRIG_SHIFT) - 1);
    LONG tz = fz & ((1L << TRIG_SHIFT) - 1);

    LONG h00 = at(gx,     gz)     << TERRAIN_HEIGHT_SHIFT;
    LONG h10 = at(gx + 1, gz)     << TERRAIN_HEIGHT_SHIFT;
    LONG h01 = at(gx,     gz + 1) << TERRAIN_HEIGHT_SHIFT;
    LONG h11 = at(gx + 1, gz + 1) << TERRAIN_HEIGHT_SHIFT;

    LONG a = h00 + (((h10 - h00) * tx) >> TRIG_SHIFT);
    LONG b = h01 + (((h11 - h01) * tx) >> TRIG_SHIFT);
    return a + (((b - a) * tz) >> TRIG_SHIFT);
}
