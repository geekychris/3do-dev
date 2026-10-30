// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - ScAIry Fun's unit and gameplay tests. The same suite runs
 * on the host (make host-test), on the Amiga (scairy_fun_test) and inside
 * the game (the "selftest" hook).
 *
 * The gameplay tests drive the simulation headless: each hazard is set up
 * with the scene code the bridge hook uses, then run with the autopilot
 * left alone (it should get it wrong) or with a player taking over (a save).
 */
#include <string.h>
#include <stdio.h>
#include "../sc.h"

#define SEGF ((int32_t)SEG_LEN * (1 << FBITS))

static World tw;                    /* big: keep it off the Amiga stack */
static int evcount[EV_COUNT];
static int32_t min_kmh;

typedef unsigned (*Driver)(World *w, int frame);

static void run(World *w, int frames, Driver drive)
{
    int f;
    Event e;
    memset(evcount, 0, sizeof(evcount));
    min_kmh = 999;
    for (f = 0; f < frames; f++) {
        world_update(w, drive ? drive(w, f) : 0);
        while (world_pop_event(w, &e))
            if (e.type > 0 && e.type < EV_COUNT) evcount[e.type]++;
        if (evcount[EV_ARRIVED]) break;
        if (w->state == GS_PLAY && KMH(w->car.v) < min_kmh) min_kmh = KMH(w->car.v);
    }
}

static void setup(World *w, const char *scene)
{
    memset(w, 0, sizeof(*w));
    world_init(w);
    w->tune.traffic_pct = 0;        /* nothing random in the way */
    world_scene(w, scene);
}

/* the player: take over (brake) when the hazard is close, then drive
 * through slowly in the lane, then hand back */
static int32_t hold_kmh;
static unsigned careful(World *w, int f)
{
    int h = world_hazard_ahead(w, 6);
    (void)f;
    if (h >= 0 || w->ai.on == 0) {
        unsigned in = 0;
        int32_t off = (w->car.d >> FBITS) - lane_centre(w, route_info[w->route].nl - 1);
        if (w->ai.on) return IN_DOWN;
        if (KMH(w->car.v) > hold_kmh) in |= IN_DOWN;
        else if (KMH(w->car.v) < hold_kmh - 5) in |= IN_UP;
        /* steer back to the lane centre: the bends pull the car outwards */
        if (off > 3 || (off > 0 && w->car.lat > 0)) in |= IN_LEFT;
        else if (off < -3 || (off < 0 && w->car.lat < 0)) in |= IN_RIGHT;
        return in;
    }
    return 0;
}

/* the player keeps speed through the overpass instead of letting it brake */
static unsigned keep_going(World *w, int f)
{
    if (f == 2) return IN_FIRE;     /* autopilot off */
    (void)w;
    return f > 2 ? IN_UP : 0;
}

static unsigned start_held(World *w, int f)
{
    (void)w; (void)f;
    return IN_FIRE;                 /* pressed and held over a few frames */
}

static unsigned nudge(World *w, int f)
{
    (void)w;
    return f == 5 ? IN_LEFT : 0;
}

/* pull over to the right for the ambulance */
static unsigned pull_over(World *w, int f)
{
    int nl = route_info[w->route].nl;
    unsigned in = KMH(w->car.v) < 50 ? IN_UP : 0;
    (void)f;
    if (w->ai.on) return w->siren ? IN_FIRE : 0;
    if (nl > 1) {                   /* over to the next lane */
        if ((w->car.d >> FBITS) > lane_centre(w, nl - 2) + 3) in |= IN_LEFT;
    } else if ((w->car.d >> FBITS) < lane_centre(w, 0) + 40) {
        in |= IN_RIGHT;             /* onto the shoulder */
    }
    return in;
}

/* go round the debris on the shoulder */
static unsigned swerve(World *w, int f)
{
    (void)f;
    if (w->ai.on && world_hazard_ahead(w, 8) >= 0) return IN_FIRE;
    if (!w->ai.on && w->obj[0].type == OBJ_DEBRIS &&
        (w->car.d >> FBITS) > (w->obj[0].d >> FBITS) - 34) return IN_LEFT;
    return 0;
}

int sc_run_tests(sc_assert_fn check)
{
    World *w = &tw;
    int r, i, ok;

    /* maths */
    check(sc_sin(0) == 0, "sin 0");
    check(sc_sin(16384) == 65536, "sin quarter turn");
    check(sc_cos(0) == 65536, "cos 0");
    check(sc_sin(8192) > 46000 && sc_sin(8192) < 46700, "sin 45 deg interpolated");
    check(sc_atan_ratio(128, 128) == 8192, "atan 1 = 45 deg");
    check(sc_atan_ratio(-64, 128) < -4800 && sc_atan_ratio(-64, 128) > -4900, "atan -0.5");
    {
        Cam c;
        V3 v;
        int32_t sx, sy;
        memset(&c, 0, sizeof(c));
        cam_setup(&c);
        cam_xform(&c, 0, 0, 1000, &v);
        sc_project(&v, &sx, &sy);
        check(v.z == 1000 && sx == VIEW_CX && sy == VIEW_CY, "camera straight ahead");
        c.yaw = 16384;              /* facing +X */
        cam_setup(&c);
        cam_xform(&c, 1000, 0, 0, &v);
        check(v.z > 990 && v.x > -5 && v.x < 5, "camera yaw quarter turn");
        c.yaw = 0; c.pitch = 1000;
        cam_setup(&c);
        check(sc_horizon(&c) < VIEW_CY, "horizon above centre when looking down");
    }

    /* the routes */
    for (r = 0; r < NUM_ROUTES; r++) {
        memset(w, 0, sizeof(*w));
        route_build(w, r);
        ok = w->nseg > 350 && w->nseg <= MAX_SEGS && w->finish < w->nseg - 30;
        check(ok, "route length");
        ok = 1;
        for (i = 0; i < w->nseg - 1; i++)
            if (w->seg[i + 1].cx - w->seg[i].cx != w->seg[i].dir) ok = 0;
        check(ok, "route centre line follows dir");
        ok = 1;
        for (i = 0; i < w->nhaz; i++)
            if (w->haz[i].seg < 20 || w->haz[i].seg + w->haz[i].len >= w->finish) ok = 0;
        check(ok, "hazards inside the route");
        check(road_cx(w, 10 * SEGF) == w->seg[10].cx, "road_cx at a segment start");
        check(road_cx(w, 10 * SEGF + SEGF / 2) == w->seg[10].cx + w->seg[10].dir / 2,
              "road_cx halfway");
    }

    /* the perfect autopilot gets every route right, start to finish */
    for (r = 0; r < NUM_ROUTES; r++) {
        memset(w, 0, sizeof(*w));
        world_init(w);
        w->tune.ai_perfect = 1;
        w->tune.traffic_pct = 50;
        world_start_game(w);
        if (r) world_start_route(w, r);
        w->state = GS_PLAY;
        run(w, 25 * 300, 0);
        check(evcount[EV_ARRIVED] == 1, "perfect autopilot arrives");
        check(evcount[EV_SPLASH] + evcount[EV_ROLL] == 0, "perfect autopilot stays on the road");
        check(evcount[EV_SCRAPE] + evcount[EV_CONE] + evcount[EV_DEBRIS] == 0,
              "perfect autopilot: no scrapes, cones or debris");
        {
            static char name[64];
            sprintf(name, "perfect autopilot, route %ld: no ticket (%ld), no rear-end (%ld)", (long)r + 1,
                    (long)evcount[EV_TICKET], (long)evcount[EV_REAREND]);
            check(evcount[EV_TICKET] + evcount[EV_REAREND] == 0, name);
        }
        check(evcount[EV_HIT_ANIMAL] == 0, "perfect autopilot misses the animals");
        check(world_ap_pct(w) == 100, "perfect autopilot: 100% autopilot");
    }

    /* scoring */
    setup(w, "cyclist");
    w->car.z = 3 * SEGF;
    run(w, 25 * 9, 0);
    check(w->score > 0, "autopilot scores");
    check(w->mult == 2, "multiplier after mult_secs");
    check(evcount[EV_MULT_UP] == 1, "one multiplier step");
    {
        int32_t s0;
        run(w, 10, nudge);
        s0 = w->score;
        check(evcount[EV_AP_OFF] == 1 && w->ai.on == 0, "steering takes over");
        check(evcount[EV_NEEDLESS] == 1, "takeover with nothing ahead is needless");
        check(w->mult == 1, "takeover resets the multiplier");
        run(w, 25, 0);
        check(w->score == s0, "no points without autopilot");
    }
    w->car.d += 20 * (1 << FBITS);        /* off the lane centre */
    run(w, 3, keep_going);
    check(evcount[EV_UNAVAILABLE] == 1 || evcount[EV_AP_OFF] == 0, "autopilot needs the lane centre");

    /* the hazards, left to the autopilot */
    setup(w, "flood");
    run(w, 25 * 12, 0);
    check(evcount[EV_AQUAPLANE] >= 1, "flood: aquaplanes");
    check(evcount[EV_SPLASH] == 1, "flood: into the river");
    check(w->cover == 60, "flood: costs 40 cover");
    check(evcount[EV_RESPAWN] == 1 && w->ai.on == 0, "flood: towed back, autopilot off");

    /* the town's floods are deeper: the autopilot's 50 km/h is too fast */
    memset(w, 0, sizeof(*w));
    world_init(w);
    w->tune.traffic_pct = 0;
    world_start_route(w, 3);
    w->state = GS_PLAY;
    w->car.z = (int32_t)(w->haz[1].seg - 20) * SEGF;
    run(w, 25 * 14, 0);
    check(w->haz[1].type == HZ_FLOOD && evcount[EV_SPLASH] == 1, "town flood: into the canal");

    /* starting the route with fire doesn't also switch the autopilot off */
    memset(w, 0, sizeof(*w));
    world_init(w);
    world_start_game(w);
    run(w, 3, start_held);
    check(w->state == GS_PLAY && w->ai.on, "fire to start: autopilot stays on");

    setup(w, "flood");
    hold_kmh = 45;
    run(w, 25 * 30, careful);
    check(evcount[EV_SPLASH] == 0, "flood taken slowly: dry");
    check(evcount[EV_SAVE] == 1 && w->saves == 1, "flood taken over: a save");
    check(evcount[EV_NEEDLESS] == 0, "flood taken over: not needless");

    setup(w, "ghost");
    run(w, 25 * 20, 0);
    check(evcount[EV_ROLL] == 1, "old lane lines: over the embankment");

    setup(w, "ghost");
    hold_kmh = 60;
    run(w, 25 * 28, careful);
    check(evcount[EV_ROLL] == 0 && evcount[EV_SAVE] == 1, "old lane lines taken over: a save");

    setup(w, "phantom");
    run(w, 25 * 10, 0);
    check(evcount[EV_PHANTOM] == 1, "overpass: phantom braking");
    check(evcount[EV_REAREND] == 1, "overpass: the truck runs into us");

    setup(w, "phantom");
    run(w, 25 * 10, keep_going);
    check(evcount[EV_PHANTOM] == 0 && evcount[EV_REAREND] == 0, "overpass driven through: no phantom");
    check(evcount[EV_SAVE] == 1, "overpass taken over: a save");

    setup(w, "cones");
    run(w, 25 * 12, 0);
    check(evcount[EV_CONE] >= 3, "cones: driven through");

    setup(w, "glare");
    run(w, 25 * 16, 0);
    check(evcount[EV_BLINDED] == 1, "low sun: blinded");
    check(evcount[EV_SCRAPE] + evcount[EV_ROLL] >= 1, "low sun: off the road");

    setup(w, "siren");
    run(w, 25 * 14, 0);
    check(evcount[EV_SIREN] == 1, "ambulance comes");
    check(evcount[EV_TICKET] == 1, "ambulance: a ticket for not yielding");

    setup(w, "siren");
    run(w, 25 * 14, pull_over);
    check(evcount[EV_TICKET] == 0 && evcount[EV_SAVE] == 1, "ambulance let past: a save");

    setup(w, "debris");
    run(w, 25 * 8, 0);
    check(evcount[EV_DEBRIS] == 1, "debris: hit");

    setup(w, "debris");
    run(w, 25 * 8, swerve);
    check(evcount[EV_DEBRIS] == 0, "debris: avoided by hand");

    /* bait: the autopilot gets these right */
    setup(w, "deer");
    run(w, 25 * 10, 0);
    check(evcount[EV_HIT_ANIMAL] == 0, "deer: the autopilot stops");
    check(min_kmh < 20, "deer: it slows right down");

    setup(w, "deer");
    run(w, 8, nudge);
    check(evcount[EV_NEEDLESS] == 1, "deer: taking over is needless");

    setup(w, "merge");
    run(w, 25 * 10, 0);
    check(evcount[EV_CRASH] + evcount[EV_REAREND] == 0, "merging truck: yields");

    setup(w, "finish");
    run(w, 25 * 8, 0);
    check(evcount[EV_ARRIVED] == 1 && w->state == GS_ARRIVED, "arrives at the destination");
    check(w->route_bonus >= 100 * 50, "arrival bonus");
    return 0;
}

/* the shared engine's test runners (test_host.c, test_amiga.c) */
const char test_client[] = "scairy_fun_test";
#ifdef A3D_TEST
/* the 3DO: tests/trace_main.c's run (route 1, 50% traffic, 7500 frames),
 * printed the same way, for test.py to compare with the host's */
static World trace_w;
static void trace_run(void)
{
    void tdo_log(const char *fmt, ...);
    World *w = &trace_w;
    Event e;
    int f;
    world_init(w);
    w->tune.ai_perfect = 1;
    w->tune.traffic_pct = 50;
    world_start_game(w);
    w->state = GS_PLAY;
    for (f = 0; f < 7500; f++) {
        world_update(w, 0);
        while (world_pop_event(w, &e))
            if (e.type != EV_SAY && e.type != EV_MULT_UP)
                tdo_log("TRACE f%ld %s %ld %ld\n", (long)f, ev_name[e.type], (long)e.a, (long)e.b);
        if (f % 250 == 0)
            tdo_log("TRACE f%ld seg %ld d %ld v %ld rng %lu\n", (long)f, (long)seg_of(w->car.z),
                    (long)(w->car.d >> FBITS), (long)w->car.v, (unsigned long)w->rng);
        if (w->state != GS_PLAY) break;
    }
}
int run_tests(sc_assert_fn check) { int r = sc_run_tests(check); trace_run(); return r; }
#else
int run_tests(sc_assert_fn check) { return sc_run_tests(check); }
#endif
