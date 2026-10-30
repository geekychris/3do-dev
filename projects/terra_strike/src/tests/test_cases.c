// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Terra Strike unit tests, shared by three runners:
 *   tests/host_main.c        native build on the Mac (make host-test)
 *   tests/amiga_test_main.c  terra_strike_test on the Amiga (amiga_run_tests)
 *   main.c "selftest" hook   inside the running game (amiga_call_hook)
 * Each runner supplies the assert callback.
 */
#include "../ts.h"

static World tw;          /* the tests' own world: never the live game's */
static ts_assert_fn CHECK;
static int nchecks;

#define T(cond, name) do { nchecks++; CHECK((cond) ? 1 : 0, (name)); } while (0)

static void run(World *w, unsigned in, int frames)
{
    while (frames-- > 0) world_update(w, in);
}

static int count_events(World *w, int type)
{
    Event e;
    int n = 0;
    while (world_pop_event(w, &e)) if (e.type == type) n++;
    return n;
}

static int find_type(const World *w, int type)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) if (w->ents[i].type == type) return i;
    return -1;
}

static void test_math(void)
{
    Cam c;
    V3 v;
    int32_t sx, sy;

    T(TS_SIN(0) == 0 && TS_SIN(64) == 65536 && TS_COS(128) == -65536, "trig table quadrants");
    T(ts_wrap_delta(10, WORLD_SIZE - 10) == 20, "wrap delta across the seam");
    T(ts_wrap_delta(WORLD_SIZE - 10, 10) == -20, "wrap delta negative");
    T(ts_wrap(-1) == WORLD_SIZE - 1, "wrap negative coordinate");
    T(ts_atan2(0, 100) == 0 && ts_atan2(100, 0) == 64 &&
      ts_atan2(0, -100) == 128 && ts_atan2(-100, 0) == 192, "atan2 axes");
    T(ts_atan2(100, 100) == 32, "atan2 diagonal");

    c.x = 1000; c.y = 500; c.z = 1000; c.yaw = 0; c.pitch = 0;
    cam_setup(&c);
    cam_xform(&c, 1000, 500, 2000, &v);
    T(v.x == 0 && v.y == 0 && v.z > 990 && v.z <= 1000, "point ahead is on the view axis");
    cam_project(&v, &sx, &sy);
    T(sx == VIEW_CX && sy == VIEW_CY, "point ahead projects to screen centre");
    cam_xform(&c, 1500, 500, 2000, &v);
    cam_project(&v, &sx, &sy);
    T(sx > VIEW_CX, "point to the right projects right of centre");

    c.yaw = 64;      /* facing +X: a point at +X is now dead ahead */
    cam_setup(&c);
    cam_xform(&c, 2000, 500, 1000, &v);
    T(v.x > -2 && v.x < 2 && v.z > 990, "yaw 64 faces +X");

    c.yaw = 0; c.x = 100; c.z = 100;   /* the world wraps: a point "behind" the seam is near */
    cam_setup(&c);
    cam_xform(&c, 100, 500, WORLD_SIZE - 100, &v);
    T(v.z < 0 && v.z > -210, "camera sees the nearest wrapped copy");
}

static void test_clip(void)
{
    V3 in[4], out[MAX_PV];
    int32_t xs[MAX_PV], ys[MAX_PV];
    int n, i, ok;

    in[0].x = -100; in[0].y = 0; in[0].z = -50;
    in[1].x =  100; in[1].y = 0; in[1].z = -50;
    in[2].x =  100; in[2].y = 0; in[2].z = -10;
    in[3].x = -100; in[3].y = 0; in[3].z = -10;
    T(clip_near(in, 4, out) == 0, "polygon behind the camera is culled");

    in[0].z = -50; in[1].z = -50; in[2].z = 200; in[3].z = 200;
    n = clip_near(in, 4, out);
    ok = n == 4;
    for (i = 0; i < n; i++) if (out[i].z < NEAR_Z) ok = 0;
    T(ok, "straddling polygon is clipped to the near plane");

    xs[0] = -50; ys[0] = 10; xs[1] = 100; ys[1] = 10; xs[2] = 100; ys[2] = 100;
    n = clip_rect(xs, ys, 3, 0, 0, 319, 211);
    ok = n >= 3;
    for (i = 0; i < n; i++) if (xs[i] < 0 || xs[i] > 319 || ys[i] < 0 || ys[i] > 211) ok = 0;
    T(ok, "2D clip keeps every vertex on screen");

    xs[0] = 400; ys[0] = 10; xs[1] = 500; ys[1] = 10; xs[2] = 450; ys[2] = 100;
    T(clip_rect(xs, ys, 3, 0, 0, 319, 211) == 0, "off-screen polygon is dropped");

    {
        /* regression: two polygons sharing an edge that crosses the screen
         * border must clip it to the same point (else black cracks) */
        int32_t ax[MAX_PV] = { -37, 150, 150 }, ay[MAX_PV] = { 13, 101, 13 };
        int32_t bx[MAX_PV] = { 150, -37, -37 }, by[MAX_PV] = { 101, 13, 150 };
        int na = clip_rect(ax, ay, 3, 0, 0, 319, 211), nb = clip_rect(bx, by, 3, 0, 0, 319, 211);
        int k, l, shared = 0;
        for (k = 0; k < na; k++) for (l = 0; l < nb; l++)
            if (ax[k] == 0 && bx[l] == 0 && ay[k] == by[l]) shared = 1;
        T(shared, "shared edge clips to the same border point");
    }

    xs[0] = -40000; ys[0] = -40000; xs[1] = 40000; ys[1] = -40000; xs[2] = 0; ys[2] = 40000;
    n = clip_rect(xs, ys, 3, 0, 0, 319, 211);
    T(n >= 3, "huge polygon clips without overflow");
}

static void test_spans(void)
{
    int16_t xs[4], ys[4], xl[SPAN_MAX], xr[SPAN_MAX];
    int rows, ymin, i, ok;

    /* axis-aligned square 10..20 x 5..15 */
    xs[0] = 10; ys[0] = 5; xs[1] = 20; ys[1] = 5; xs[2] = 20; ys[2] = 15; xs[3] = 10; ys[3] = 15;
    rows = poly_spans(xs, ys, 4, xl, xr, &ymin);
    ok = rows == 11 && ymin == 5;
    for (i = 0; i < rows; i++) if (xl[i] != 10 || xr[i] != 20) ok = 0;
    T(ok, "spans of a square");

    /* triangle: apex at top, widens downwards */
    xs[0] = 50; ys[0] = 0; xs[1] = 100; ys[1] = 50; xs[2] = 0; ys[2] = 50;
    rows = poly_spans(xs, ys, 3, xl, xr, &ymin);
    ok = rows == 51 && xl[0] == 50 && xr[0] == 50 && xl[50] == 0 && xr[50] == 100;
    for (i = 1; i < rows; i++) if (xl[i] > xl[i - 1] || xr[i] < xr[i - 1]) ok = 0;
    T(ok, "triangle spans widen monotonically");

    /* two quads sharing an edge leave no gap */
    xs[0] = 0; ys[0] = 0; xs[1] = 37; ys[1] = 3; xs[2] = 41; ys[2] = 60; xs[3] = 0; ys[3] = 60;
    poly_spans(xs, ys, 4, xl, xr, &ymin);
    {
        int16_t xl2[SPAN_MAX], xr2[SPAN_MAX];
        int ymin2;
        int16_t xs2[4] = { 37, 90, 90, 41 }, ys2[4] = { 3, 3, 60, 60 };
        poly_spans(xs2, ys2, 4, xl2, xr2, &ymin2);
        ok = 1;
        for (i = 3; i <= 60; i++) if (xr[i - ymin] < xl2[i - ymin2]) ok = 0;
        T(ok, "adjacent polygons share their edge pixels");
    }
}

static void test_terrain(void)
{
    int i, ok, sea = 0, land = 0, x, z;
    int32_t px, pz;

    world_init(&tw, 1234);
    ok = 1;
    for (i = 0; i < MAP_N * MAP_N; i++) {
        if (tw.h[i] < 0 || tw.h[i] > MAX_HEIGHT) ok = 0;
        if (tw.h[i] <= SEA_LEVEL) sea++; else land++;
    }
    T(ok, "heights within 0..MAX_HEIGHT");
    T(sea > MAP_N * MAP_N / 6 && land > MAP_N * MAP_N / 2, "map has both sea and land");

    {
        int16_t first = tw.h[777];
        terrain_generate(&tw, 1234);
        T(tw.h[777] == first, "generation is deterministic per seed");
        terrain_generate(&tw, 99);
        T(tw.h[777] != first || tw.h[1500] != first, "different seed, different map");
        terrain_generate(&tw, 1234);
    }

    T(terrain_height(&tw, 300, 700) == terrain_height(&tw, 300 + WORLD_SIZE, 700 - WORLD_SIZE),
      "terrain wraps seamlessly");
    T(terrain_height(&tw, 3 * TILE, 5 * TILE) == terrain_vertex(&tw, 3, 5),
      "height at a vertex equals the vertex");

    pad_centre(&tw, &px, &pz);
    ok = 1;
    for (z = -TILE + 8; z < TILE - 8; z += 64)
        for (x = -TILE + 8; x < TILE - 8; x += 64)
            if (terrain_height(&tw, px + x, pz + z) != tw.pad_h) ok = 0;
    T(ok, "landing pad is flat");
    T(tw.pad_h > SEA_LEVEL, "landing pad is on dry land");
    T(on_pad(&tw, px, pz) && !on_pad(&tw, px + 3 * TILE, pz), "on_pad bounds");
    T(terrain_type(SEA_LEVEL) == TER_SEA && terrain_type(1000) == TER_SNOW, "terrain bands");
}

static void test_spawns(void)
{
    int i, ok_tank = 1, ok_ship = 1, ships = 0;

    world_init(&tw, 1234);
    world_start_game(&tw);
    T(tw.state == GS_PLAY && tw.level == 1, "game starts at level 1");
    T(tw.targets_left == world_count_targets(&tw) && tw.targets_left > 0, "targets counted");

    run(&tw, 0, 200);   /* let tanks and ships wander */
    for (i = 0; i < MAX_ENTS; i++) {
        Ent *e = &tw.ents[i];
        int32_t h = terrain_height(&tw, e->x >> FBITS, e->z >> FBITS);
        if (e->type == ENT_TANK && h <= SEA_LEVEL) ok_tank = 0;
        if (e->type == ENT_SHIP) { ships++; if (h > SEA_LEVEL) ok_ship = 0; }
    }
    T(ok_tank, "tanks never drive into the sea");
    T(ok_ship && ships > 0, "ships stay on the water");
}

static void test_flight(void)
{
    int32_t y0, px, pz;
    Craft *p = &tw.p;

    world_init(&tw, 1234);
    world_start_game(&tw);
    tw.tune.enemy_fire = 0;
    T(p->state == PS_LANDED && on_pad(&tw, p->x >> FBITS, p->z >> FBITS), "craft starts landed on the pad");

    y0 = p->y;
    run(&tw, IN_UP, 40);
    T(p->state == PS_FLYING && p->y > y0 + (100 << FBITS), "holding UP lifts off and climbs");
    T(count_events(&tw, EV_TAKEOFF) == 1, "take-off event");
    T(p->fuel < 1000, "climbing burns fuel");

    y0 = p->y;
    run(&tw, 0, 30);
    T(p->y >= y0 - (40 << FBITS), "hover assist holds altitude");

    run(&tw, IN_FAST, 30);
    T(p->speed > 60, "throttle builds speed");
    run(&tw, IN_SLOW, 40);
    T(p->speed == 0, "brake to a stop");

    {
        int yaw = p->yaw;
        run(&tw, IN_RIGHT, 10);
        T(p->yaw == ((yaw + 10 * tw.tune.turn_rate) & 255), "turning right increases yaw");
    }

    /* descend back onto the pad */
    pad_centre(&tw, &px, &pz);
    world_place_player(&tw, px, tw.pad_h + 150, pz, 0, 1);
    p->fuel = 400;
    run(&tw, IN_DOWN, 120);
    T(p->state == PS_LANDED, "gentle descent onto the pad lands");
    T(count_events(&tw, EV_LANDED) == 1, "landed event");
    run(&tw, 0, 50);
    T(p->fuel > 400, "refuelling on the pad");
}

static void test_crash(void)
{
    int32_t px, pz, x;
    int lives;
    Craft *p = &tw.p;

    world_init(&tw, 1234);
    world_start_game(&tw);
    tw.tune.enemy_fire = 0;
    lives = tw.lives;
    pad_centre(&tw, &px, &pz);
    /* find a spot off the pad and dive into it */
    for (x = 4 * TILE; x < 30 * TILE; x += TILE)
        if (terrain_height(&tw, px + x, pz) > SEA_LEVEL + 50) break;
    world_place_player(&tw, px + x, terrain_height(&tw, px + x, pz) + 100, pz, 0, 1);
    run(&tw, IN_DOWN, 80);
    T(p->state == PS_DEAD && tw.lives == lives - 1, "diving into the ground costs a life");
    T(count_events(&tw, EV_CRASH) == 1, "crash event");
    run(&tw, 0, 100);
    T(p->state == PS_LANDED, "respawn on the pad");

    tw.tune.god_mode = 1;
    world_place_player(&tw, px + x, terrain_height(&tw, px + x, pz) + 100, pz, 0, 1);
    run(&tw, IN_DOWN, 80);
    T(p->state == PS_FLYING && tw.lives == lives - 1, "god mode survives a ground hit");
}

static void test_combat(void)
{
    int32_t px, pz, tx, tz;
    int i, before, score0;
    Craft *p = &tw.p;

    world_init(&tw, 1234);
    world_start_game(&tw);
    tw.tune.enemy_fire = 0;
    pad_centre(&tw, &px, &pz);

    /* a lone tank 600 units ahead of a hovering craft */
    world_clear_enemies(&tw);
    tx = px; tz = pz + 3 * TILE;
    i = world_spawn(&tw, ENT_TANK, tx, tz, 0);
    T(i >= 0 && tw.targets_left == 1, "spawn a tank");
    world_spawn(&tw, ENT_BUILDING, px + 20 * TILE, pz + 20 * TILE, 0);   /* keep the level open */
    before = tw.targets_left;
    score0 = tw.score;
    tw.ents[i].type = ENT_TANK;
    {
        /* keep the tank still for a deterministic shot */
        int32_t ty = terrain_height(&tw, tx, tz);
        int k;
        world_place_player(&tw, px, ty + 110, pz + TILE, 0, 1);   /* bullets drop ~85 units over 2 tiles */
        p->fuel = 1000;
        for (k = 0; k < 60 && tw.ents[i].type == ENT_TANK; k++) {
            tw.ents[i].x = tx << FBITS; tw.ents[i].z = tz << FBITS; tw.ents[i].yaw = 0;
            world_update(&tw, IN_FIRE);
        }
    }
    T(tw.ents[i].type == ENT_NONE, "shots destroy the tank ahead");
    T(tw.targets_left == before - 1 && tw.score == score0 + 100, "kill scores 100 and counts down");
    {
        Event e; int kills = 0;
        while (world_pop_event(&tw, &e)) if (e.type == EV_KILL && e.a == ENT_TANK) kills++;
        T(kills == 1, "kill event names the tank");
    }

    /* aim assist: from 200 up, a tank 600 ahead and a little off the nose is hit */
    world_clear_enemies(&tw);
    world_spawn(&tw, ENT_BUILDING, px + 20 * TILE, pz + 20 * TILE, 0);
    {
        int32_t ty, k, tx2 = px + 150, tz2 = pz + 2 * TILE + 600;
        i = world_spawn(&tw, ENT_TANK, tx2, tz2, 0);
        ty = terrain_height(&tw, tx2, tz2);
        world_place_player(&tw, px, ty + 200, pz + 2 * TILE, 0, 1);
        for (k = 0; k < 60 && tw.ents[i].type == ENT_TANK; k++) {
            tw.ents[i].x = tx2 << FBITS; tw.ents[i].z = tz2 << FBITS;
            world_update(&tw, k < 2 ? IN_FIRE : 0);
        }
        T(tw.ents[i].type == ENT_NONE, "aim assist hits a tank below and off the nose");
    }

    /* enemy fire: a ship's shells hit a craft that sits still */
    tw.tune.enemy_fire = 5;
    world_clear_enemies(&tw);
    {
        int32_t sx = -1, sz = 0, x, z;
        for (z = 0; z < WORLD_SIZE && sx < 0; z += TILE)
            for (x = 0; x < WORLD_SIZE; x += TILE)
                if (terrain_height(&tw, x, z) < SEA_LEVEL - 60) { sx = x; sz = z; break; }
        T(sx >= 0, "found open water");
        world_spawn(&tw, ENT_SHIP, sx, sz, 0);
        tw.tune.inf_fuel = 1;
        world_place_player(&tw, sx + 300, SEA_LEVEL + 250, sz, 0, 1);
        p->shield = 100;
        count_events(&tw, EV_NONE);
        run(&tw, 0, 150);
        T(count_events(&tw, EV_HIT_PLAYER) > 0, "ship shells damage the craft");
        {
            /* regression: the fire timer used to reset to >= the rate, so
             * every enemy fired every frame */
            int f, shots = 0;
            tw.tune.enemy_fire = 30;
            tw.tune.god_mode = 1;
            world_place_player(&tw, sx + 300, SEA_LEVEL + 250, sz, 0, 1);
            for (f = 0; f < 120; f++) {
                world_update(&tw, 0);
                shots += count_events(&tw, EV_ENEMY_FIRE);
            }
            T(shots >= 2 && shots <= 6, "enemy fire respects the fire rate");
        }
    }
}

static void test_level_clear(void)
{
    int i, lvl;
    world_init(&tw, 1234);
    world_start_game(&tw);
    tw.tune.enemy_fire = 0;
    lvl = tw.level;
    for (i = 0; i < MAX_ENTS; i++)
        if (tw.ents[i].type != ENT_TREE) tw.ents[i].type = ENT_NONE;
    tw.targets_left = 0;
    run(&tw, 0, 1);
    T(tw.state == GS_LEVELCLEAR, "no targets left clears the level");
    T(count_events(&tw, EV_LEVEL_CLEAR) == 1, "level clear event");
    run(&tw, 0, 120);
    T(tw.state == GS_PLAY && tw.level == lvl + 1, "next level starts");
    T(tw.targets_left > 0, "next level has more targets");
}

static void test_scenes(void)
{
    static const char *names[] = { "sea", "tanks", "city", "landing", "lowfuel", "pad" };
    int i, ok = 1;
    world_init(&tw, 1234);
    for (i = 0; i < 6; i++) {
        Craft *p = &tw.p;
        int32_t g;
        if (!world_scene(&tw, names[i])) { ok = 0; continue; }
        g = ground_height(&tw, p->x >> FBITS, p->z >> FBITS);
        if (tw.state != GS_PLAY || (p->y >> FBITS) < g) ok = 0;
    }
    T(ok, "every scene starts play above the ground");
    world_scene(&tw, "sea");
    {
        int32_t dmin = 0x7FFFFFFF;
        for (i = 0; i < MAX_ENTS; i++) if (tw.ents[i].type == ENT_SHIP) {
            int32_t dx = ts_wrap_delta(tw.ents[i].x >> FBITS, tw.p.x >> FBITS);
            int32_t dz = ts_wrap_delta(tw.ents[i].z >> FBITS, tw.p.z >> FBITS);
            int32_t d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
            if (d < dmin) dmin = d;
        }
        T(dmin < 1200, "sea scene puts a ship in front of the craft");
    }
    world_scene(&tw, "landing");
    tw.tune.enemy_fire = 0;
    run(&tw, IN_DOWN, 150);
    T(tw.p.state == PS_LANDED, "landing scene can land");
}

static void test_radar(void)
{
    int rx, ry;
    radar_map(0, 1000, 0, 6, &rx, &ry);
    T(rx == 0 && ry < 0, "target ahead shows above radar centre");
    radar_map(1000, 0, 0, 6, &rx, &ry);
    T(rx > 0 && ry == 0, "target to the right shows right");
    radar_map(1000, 0, 64, 6, &rx, &ry);
    T(rx == 0 && ry < 0, "radar rotates with the craft");
}

int ts_run_tests(ts_assert_fn check)
{
    CHECK = check;
    nchecks = 0;
    test_math();
    test_clip();
    test_spans();
    test_terrain();
    test_spawns();
    test_flight();
    test_crash();
    test_combat();
    test_level_clear();
    test_scenes();
    test_radar();
    (void)find_type;
    return nchecks;
}
