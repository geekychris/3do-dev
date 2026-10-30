// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * trace_main.c - lander_3d_trace / make host-trace: one autopilot flight
 * printed every 2 s of game time, built for the host and the Amiga so the
 * two can be diffed (how a 68k-only miscompile of the 64-bit square roots
 * was found).
 */
#include <stdio.h>
#include "../ld.h"
static Level L;
int main(void)
{
    LdInput in;
    int t;
    ld_new_level(&L, W_MOON, 1, 1018, 0);
    printf("start %ld %ld %ld v %ld %ld %ld\n", (long)L.c.p[0], (long)L.c.p[1], (long)L.c.p[2], (long)L.c.v[0], (long)L.c.v[1], (long)L.c.v[2]);
    for (t = 0; t < 6000 && !L.outcome; t++) {
        ld_autopilot(&L, &in);
        if (t % 200 == 0)
            printf("t%d p %ld %ld %ld v %ld %ld %ld att %ld %ld in %ld %ld %ld tgt %d down %d\n", t, (long)L.c.p[0], (long)L.c.p[1], (long)L.c.p[2],
                   (long)L.c.v[0], (long)L.c.v[1], (long)L.c.v[2], (long)L.c.pitch, (long)L.c.roll,
                   (long)in.throttle, (long)in.pitch, (long)in.roll, L.ap_target, L.ap_down);
        ld_step(&L, &in);
    }
    printf("end t%d outcome %d\n", t, L.outcome);
    return 0;
}
