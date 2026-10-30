// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Lander 3D's tests: the fixed-point maths, the terrain and
 * pads, the physics, the touchdown rules and the autopilot landing every
 * world. On the host (make host-test), on the Amiga (lander_3d_test) and
 * inside the game (the selftest hook).
 */
#include <string.h>
#include "../ld.h"

const char test_client[] = "lander_3d_test";
static Level tl;                        /* big: off the stack */

static fix fabs_(fix v) { return v < 0 ? -v : v; }

static void fly_autopilot(Level *L, int seconds)
{
    LdInput in;
    int t;
    for (t = 0; t < seconds * LD_STEP_HZ && !L->outcome; t++) {
        ld_autopilot(L, &in);
        ld_step(L, &in);
    }
}

int run_tests(void (*check)(int cond, const char *name))
{
    Level *L = &tl;
    int w, lv, k;

    /* maths */
    check(fabs_(fsqrt(FROMI(2)) - FX(1.41421)) < 8, "fsqrt 2");
    check(fabs_(fsin(FX(1.5707963)) - FONE) < 64, "sin pi/2");
    check(fabs_(fcos(FX(3.1415926)) + FONE) < 64, "cos pi");
    check(fabs_(fatan2(FONE, FONE) - FX(0.785398)) < 200, "atan2(1, 1) = pi/4");
    check(fabs_(fatan2(FONE, -FONE) - FX(2.356194)) < 200, "atan2(1, -1) = 3pi/4");
    check(fabs_(fatan2(-FONE, 0) + FX(1.570796)) < 200, "atan2(-1, 0) = -pi/2");
    check(fabs_(fexp_neg(FONE) - FX(0.367879)) < 300, "e^-1");
    check(fabs_(fhypot(FROMI(3), FROMI(4)) - FROMI(5)) < 4, "hypot 3 4");

    /* the terrain */
    for (w = 0; w < W_COUNT; w++) {
        LevelParams p;
        static Terrain a, b;
        int ok = 1, m;
        ld_level_params(w, 1, &p);
        ld_make_terrain(&a, w, 1234, &p);
        ld_make_terrain(&b, w, 1234, &p);
        check(!memcmp(a.h, b.h, sizeof(a.h)), "terrain: the same seed, the same ground");
        for (m = 0; m < 3; m++) {
            const Pad *pd = &a.pads[m];
            for (k = 0; k < 8; k++) {
                fix x = pd->x + FMUL(fcos(FROMI(k) * 3 / 4), FMUL(pd->r, FX(0.8)));
                fix z = pd->z + FMUL(fsin(FROMI(k) * 3 / 4), FMUL(pd->r, FX(0.8)));
                if (fabs_(ld_height(&a, x, z) - pd->y) > FX(0.05)) ok = 0;
            }
        }
        check(ok, "terrain: every pad is flat");
        check(a.pads[0].mult == 1 && a.pads[1].mult == 2 && a.pads[2].mult == 5, "pads x1 x2 x5");
        check(fhypot(a.pads[0].x - a.pads[2].x, a.pads[0].z - a.pads[2].z) > FMUL(FROMI(a.size), FX(0.2)),
              "pads well apart");
    }

    /* free fall on the Moon: 1 s with the engine off */
    ld_new_level(L, W_MOON, 1, 99, 0);
    L->c.p[1] += FROMI(500);
    {
        fix v0 = L->c.v[1];
        LdInput in = { 0, 0, 0 };
        for (k = 0; k < LD_STEP_HZ; k++) ld_step(L, &in);
        check(fabs_(L->c.v[1] - (v0 - FX(1.62))) < FX(0.02), "free fall: 1.62 m/s faster after 1 s on the Moon");
    }
    /* full throttle, level: up at amax - g */
    {
        fix v0 = L->c.v[1], f0 = L->fuel;
        LdInput in = { FONE, 0, 0 };
        for (k = 0; k < LD_STEP_HZ; k++) ld_step(L, &in);
        check(fabs_(L->c.v[1] - (v0 + FX(4.4 - 1.62))) < FX(0.03), "full throttle: amax - g");
        check(fabs_((f0 - L->fuel) - FONE) < FX(0.02), "one second of full throttle burns 1 unit of fuel");
    }
    /* attitude: the stick tips it, assisted mode holds it */
    {
        LdInput in = { 0, FONE, 0 };
        for (k = 0; k < LD_STEP_HZ / 2; k++) ld_step(L, &in);
        in.pitch = 0;
        for (k = 0; k < LD_STEP_HZ; k++) ld_step(L, &in);
        check(L->c.pitch > FX(0.2) && fabs_(L->c.wp) < FX(0.05), "pitch up, then held when the stick is centred");
    }

    /* touchdown: soft on a pad lands; hard crashes; level ground off a pad */
    for (k = 0; k < 3; k++) {
        static const fix vs[3] = { FX(-1.0), FX(-4.0), FX(-1.0) };
        static const int want[3] = { OUT_LANDED, OUT_CRASHED, OUT_LANDED };
        LdInput in = { 0, 0, 0 };
        int t;
        ld_new_level(L, W_MOON, 1, 7, 0);
        L->c.p[0] = L->T.pads[k == 2 ? 2 : 0].x;
        L->c.p[2] = L->T.pads[k == 2 ? 2 : 0].z;
        L->c.p[1] = L->T.pads[k == 2 ? 2 : 0].y + FX(2.5);
        L->c.v[0] = L->c.v[2] = 0;
        L->c.v[1] = vs[k];
        for (t = 0; t < 300 && !L->outcome; t++) ld_step(L, &in);
        check(L->outcome == want[k], k == 0 ? "1 m/s onto the x1 pad: landed" : k == 1 ? "4 m/s: crashed"
                                                                                   : "onto the x5 pad: landed");
        if (k == 0) check(L->result.score > 100 && L->result.pad == 1, "a landing scores 100 + softness + fuel");
        if (k == 2) check(L->result.pad == 5 && L->result.score >= 5 * 100, "x5 pad: five times the score");
    }
    ld_new_level(L, W_MOON, 1, 7, 0);
    {
        LdInput in = { 0, 0, 0 };
        int t;
        L->c.p[0] = L->T.pads[0].x + L->T.pads[0].r + FROMI(4);    /* just off the pad, still flat ground */
        L->c.p[2] = L->T.pads[0].z;
        L->c.p[1] = ld_height(&L->T, L->c.p[0], L->c.p[2]) + FX(2.3);
        L->c.v[0] = L->c.v[2] = 0;
        L->c.v[1] = FX(-1.0);
        for (t = 0; t < 300 && !L->outcome; t++) ld_step(L, &in);
        check(L->outcome == OUT_OFFPAD && L->result.score == 25, "a safe landing off the pads scores 25");
    }
    ld_new_level(L, W_MOON, 1, 7, 0);
    {
        LdInput in = { 0, 0, 0 };
        int t;
        L->c.p[0] = L->T.pads[0].x; L->c.p[2] = L->T.pads[0].z; L->c.p[1] = L->T.pads[0].y + FX(2.5);
        L->c.v[0] = FX(3); L->c.v[2] = 0; L->c.v[1] = FX(-1.0);
        for (t = 0; t < 300 && !L->outcome; t++) ld_step(L, &in);
        check(L->outcome == OUT_CRASHED && !L->result.hs_ok, "sliding at 3 m/s: crashed");
    }

    /* the autopilot lands every world (this is what gives each level its fuel) */
    for (w = 0; w < W_COUNT; w++)
        for (lv = 1; lv <= 6; lv += 5 - (lv == 1 ? 3 : 0)) {
            ld_new_level(L, w, lv, 1000 + w * 17 + lv, 1);
            check(L->probe_outcome == OUT_LANDED, "the autopilot's probe flight lands");
            check(L->fuel >= L->params.fuel, "the tank holds at least the level's fuel");
            fly_autopilot(L, 400);
            check(L->outcome == OUT_LANDED, "the autopilot lands with the level's fuel");
        }
#ifdef A3D_TEST
    /* the 3DO: tests/trace_main.c's flight, printed the same way, for
     * test.py to compare with the host's (the same code built natively) */
    {
        void tdo_log(const char *fmt, ...);
        LdInput in;
        int t;
        ld_new_level(L, W_MOON, 1, 1018, 0);
        tdo_log("TRACE start %ld %ld %ld v %ld %ld %ld\n", (long)L->c.p[0], (long)L->c.p[1], (long)L->c.p[2], (long)L->c.v[0], (long)L->c.v[1], (long)L->c.v[2]);
        for (t = 0; t < 6000 && !L->outcome; t++) {
            ld_autopilot(L, &in);
            if (t % 200 == 0)
                tdo_log("TRACE t%d p %ld %ld %ld v %ld %ld %ld att %ld %ld in %ld %ld %ld tgt %d down %d\n", t, (long)L->c.p[0], (long)L->c.p[1], (long)L->c.p[2],
                        (long)L->c.v[0], (long)L->c.v[1], (long)L->c.v[2], (long)L->c.pitch, (long)L->c.roll,
                        (long)in.throttle, (long)in.pitch, (long)in.roll, L->ap_target, L->ap_down);
            ld_step(L, &in);
        }
        tdo_log("TRACE end t%d outcome %d\n", t, L->outcome);
    }
#endif
    return 0;
}
