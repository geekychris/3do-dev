// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Canyon Rail's tests: the ride and the canyon, the sight,
 * shooting (hits through the projection, misses, the rate of fire),
 * turrets, drones and pods, bolts landing and shot down, the multiplier,
 * targets passed by, the guardian and the stage flow, the shield running
 * out, the autopilot clearing stages, and a whole autopilot game whose
 * final state hash (tests/vectors.h, from `make vectors` on the host) the
 * Amiga must reproduce exactly. On the host (make host-test), on the
 * Amiga (canyon_rail_test) and inside the game (the selftest hook).
 */
#include <string.h>
#include "../cr.h"
#define CR_TRACE_TICKS 4500             /* three minutes: the whole game */
#include "vectors.h"

const char test_client[] = "canyon_rail_test";
static CRGame tg;                       /* off the Amiga's stack */

static int count_events(CRGame *g, int type)
{
    CREvent e;
    int n = 0;
    while (cr_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

/* stage 1 with just one target: k = 0, `type`, 400 ahead, still */
static void one_target(CRGame *g, int type)
{
    cr_new_game(g, 5, 1);
    g->ntg = 1;
    g->tg[0].type = type;
    g->tg[0].alive = 1;
    g->tg[0].hp = type == TG_TURRET ? 2 : 1;
    g->tg[0].s = g->cam_s + 400;
    g->tg[0].amp = 0;
    g->tg[0].cool = 9999;
    g->nev = 0;
}

/* the sight onto target k (or bolt k, or node k) */
static void aim(CRGame *g, int kind, int k)
{
    int32_t x, y, z, d;
    int px, py;
    if (kind == 0) cr_target_pos(g, k, &x, &y, &z);
    else if (kind == 1) cr_bolt_pos(g, k, &x, &y, &z);
    else cr_node_pos(g, k, &x, &y, &z);
    if (cr_project(g, x, y, z, &px, &py, &d)) { g->sx = px; g->sy = py; }
}

static int play_stage(CRGame *g, uint32_t seed, int stage, int max)
{
    int t;
    cr_new_game(g, seed, stage);
    for (t = 0; t < max && !g->over && !g->between; t++) cr_tick(g, cr_bot(g));
    return g->between > 0;
}

int run_tests(void (*check)(int cond, const char *name))
{
    CRGame *g = &tg;
    int k, ok;
    int32_t s0;
    uint32_t h1;

    /* the ride */
    for (ok = 1, k = 0; k < CR_LEN; k += 50) {
        int32_t dx = cr_path_x(k + 50) - cr_path_x(k), w = cr_half_width(k);
        if (dx > 40 || dx < -40 || w < 110 || w > 210) ok = 0;
    }
    check(ok, "the ride winds smoothly down a canyon 220 to 400 wide");
    for (ok = 1, k = 1; k <= CR_STAGES; k++) {
        int j;
        cr_new_game(g, 7, k);
        if (g->ntg != 18 + 6 * k || g->shield != CR_SHIELD || g->cam_s != 0) ok = 0;
        for (j = 1; j < g->ntg; j++) if (g->tg[j].s < g->tg[j - 1].s || g->tg[j].s > CR_LEN) ok = 0;
    }
    check(ok, "every stage: more targets, spread along the ride");
    cr_new_game(g, 7, 1);
    for (k = 0; k < 10; k++) cr_tick(g, 0);
    check(g->cam_s == 10 * CR_SPEED, "the camera rides at a steady 9 a tick");

    /* the sight */
    for (k = 0; k < 5; k++) cr_tick(g, CR_IN_RIGHT | CR_IN_UP);
    check(g->sx == CR_VIEW_W / 2 + 5 * CR_SIGHT_SPEED && g->sy == CR_VIEW_H / 2 - 5 * CR_SIGHT_SPEED, "the sight moves");
    for (k = 0; k < 60; k++) cr_tick(g, CR_IN_RIGHT);
    check(g->sx == CR_VIEW_W - 8, "and stops at the edge");

    /* shooting */
    one_target(g, TG_DRONE);
    aim(g, 0, 0);
    cr_tick(g, CR_IN_FIRE);
    check(!g->tg[0].alive && g->kills == 1 && g->score == 150 && g->combo == 1, "the sight on a drone: shot down");
    one_target(g, TG_DRONE);
    g->sx = 10; g->sy = 10;
    cr_tick(g, CR_IN_FIRE);
    check(g->tg[0].alive && g->shots == 1 && g->hits == 0 && count_events(g, CRE_MISS) == 1, "the sight on nothing: a miss");
    for (k = 0; k < 20; k++) cr_tick(g, CR_IN_FIRE);
    check(g->shots == 1 + 4, "held, it fires every 5 ticks");
    one_target(g, TG_TURRET);
    aim(g, 0, 0);
    cr_tick(g, CR_IN_FIRE);
    check(g->tg[0].alive && g->tg[0].hp == 1, "a turret takes two hits");
    for (k = 0; k < CR_FIRE_GAP; k++) { aim(g, 0, 0); cr_tick(g, CR_IN_FIRE); }
    check(!g->tg[0].alive && g->score >= 100, "and goes at the second");
    one_target(g, TG_POD);
    g->shield = 50;
    aim(g, 0, 0);
    cr_tick(g, CR_IN_FIRE);
    check(g->shield == 75 && count_events(g, CRE_POD) == 1, "a pod shot: the shield tops up");
    one_target(g, TG_POD);
    g->shield = 90;
    aim(g, 0, 0);
    cr_tick(g, CR_IN_FIRE);
    check(g->shield == CR_SHIELD, "but not past full");

    /* the multiplier */
    cr_new_game(g, 5, 1);
    g->combo = 4;
    one_target(g, TG_DRONE);
    g->combo = 4;
    aim(g, 0, 0);
    cr_tick(g, CR_IN_FIRE);
    check(g->score == 300, "four hits in a row: x2");

    /* bolts */
    one_target(g, TG_TURRET);
    g->tg[0].s = g->cam_s + 600;
    g->tg[0].cool = 1;
    cr_tick(g, 0);
    check(g->bolt[0].alive && count_events(g, CRE_FIRE) == 1, "a turret in range fires a bolt");
    g->tg[0].cool = 9999;
    for (k = 0; k < 60 && g->bolt[0].alive; k++) cr_tick(g, 0);
    check(g->shield == CR_SHIELD - CR_BOLT_DMG && g->hurt == 1, "left alone, it lands: the shield takes it");
    one_target(g, TG_TURRET);
    g->tg[0].s = g->cam_s + 600;
    g->tg[0].cool = 1;
    cr_tick(g, 0);
    g->tg[0].cool = 9999;
    for (k = 0; k < 8; k++) cr_tick(g, 0);
    aim(g, 1, 0);
    g->fire_cool = 0;
    cr_tick(g, CR_IN_FIRE);
    check(!g->bolt[0].alive && g->shield == CR_SHIELD && g->score == 25, "shot down on its way: no harm, 25");
    one_target(g, TG_TURRET);
    g->tg[0].s = g->cam_s + 25;
    cr_tick(g, 0);
    check(!g->tg[0].alive && g->kills == 0, "a target passed by is gone");

    /* the guardian */
    cr_new_game(g, 5, 1);
    g->ntg = 0;
    g->cam_s = CR_LEN - CR_SPEED;
    cr_tick(g, 0);
    check(g->boss && g->cam_s == CR_LEN && count_events(g, CRE_BOSS) == 1, "the ride's end: the guardian");
    s0 = g->cam_s;
    cr_tick(g, 0);
    check(g->cam_s == s0, "the ride stops for it");
    g->boss_cool = 9999;
    for (k = 0; k < 200 && !g->between; k++) {
        int n;
        for (n = 0; n < CR_NODES && g->node_hp[n] <= 0; n++) ;
        if (n < CR_NODES) aim(g, 2, n);
        cr_tick(g, CR_IN_FIRE);
    }
    check(g->between == CR_BETWEEN || g->between > 0, "its four nodes shot out: it goes down");
    check(g->score >= 4 * 500 + 2000, "500 a node, 2000 the guardian");
    for (k = 0; k < CR_BETWEEN; k++) cr_tick(g, 0);
    check(g->stage == 2 && g->cam_s == 0 && !g->boss, "then stage 2 from the top");
    cr_new_game(g, 5, CR_STAGES);
    g->between = 1;
    cr_tick(g, 0);
    check(g->won, "the last guardian down wins");
    cr_new_game(g, 5, 1);
    g->shield = CR_BOLT_DMG;
    g->bolt[0].alive = 1; g->bolt[0].t = 0; g->bolt[0].flight = 1;
    cr_tick(g, 0);
    check(g->over && g->shield == 0, "the shield gone: over");

    /* the autopilot */
    for (k = 1; k <= 3; k++) check(play_stage(g, (uint32_t)k, 1, 3000) && g->shield >= 50, "autopilot: clears stage 1");
    check(play_stage(g, 1, 3, 3000), "autopilot: clears stage 3");

    /* determinism: the host and the Amiga must agree */
    cr_new_game(g, 1234, 1);
    for (k = 0; k < CR_TRACE_TICKS && !g->over && !g->won; k++) cr_tick(g, cr_bot(g));
    h1 = cr_state_hash(g);
    check(g->won, "autopilot: the whole game");
    check(h1 == CR_TRACE_HASH, "the whole autopilot game matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef CR_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    int k;
    cr_new_game(&tg, 1234, 1);
    for (k = 0; k < CR_TRACE_TICKS && !tg.over && !tg.won; k++) cr_tick(&tg, cr_bot(&tg));
    printf("/* make vectors: seed 1234 from stage 1, %d ticks: %s, stage %d, score %ld, %d/%d hits, best combo %d */\n",
           k, tg.won ? "won" : "stopped", tg.stage, (long)tg.score, tg.hits, tg.shots, tg.best_combo);
    printf("#define CR_TRACE_HASH 0x%08lXUL\n", (unsigned long)cr_state_hash(&tg));
    return 0;
}
#endif
