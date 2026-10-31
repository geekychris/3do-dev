// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Ace Pilot 2's tests: the map, the missions' set-ups, the
 * flight model (throttle, pitch, bank-to-turn, climbing costing speed, the
 * stall, the ground, landing and re-arming), the guns (firing, heat and
 * the jam, running dry), hits and kills, the anti-aircraft guns, the
 * computer's pilots, the train, the mission flow and the campaign, the
 * autopilot flying missions, and a long autopilot campaign whose final
 * state hash (tests/vectors.h, from `make vectors` on the host) the Amiga
 * must reproduce exactly. On the host (make host-test), on the Amiga
 * (ace_pilot2_test) and inside the game (the selftest hook).
 */
#include <string.h>
#include "../ap.h"
#include "a3d.h"
#define AP_TRACE_TICKS 20000            /* thirteen minutes of the campaign */
#include "vectors.h"

const char test_client[] = "ace_pilot2_test";
static APGame tg;                       /* off the Amiga's stack */

static int count_events(APGame *g, int type)
{
    APEvent e;
    int n = 0;
    while (ap_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

/* an empty sky: the player alone over the home side, level at 400 */
static void empty(APGame *g)
{
    ap_new_game(g, 5, 2);
    g->np = 1;
    g->ngr = 0;
    g->need = 99;                       /* never done */
    g->nev = 0;
    g->p[0].x = FROMI(-500); g->p[0].z = FROMI(-500); g->p[0].y = FROMI(400);
}

static int iabs_(int a) { return a < 0 ? -a : a; }
static int sang_test(int a) { return (int16_t)(a & 0xFFFF); }

/* the autopilot flies from `mission` until it ends; returns the phase it ended in */
static int fly(APGame *g, uint32_t seed, int mission, int max)
{
    int t;
    ap_new_game(g, seed, mission);
    for (t = 0; t < max && g->phase < PH_DONE; t++) ap_tick(g, ap_bot(g));
    return g->phase;
}

/* the whole campaign, missions advancing as they end */
static void campaign(APGame *g, uint32_t seed, int ticks)
{
    int t;
    ap_new_game(g, seed, 1);
    for (t = 0; t < ticks && !g->over && !g->won; t++) {
        ap_tick(g, ap_bot(g));
        g->nev = 0;
        if (g->phase >= PH_DONE && g->end_t == 0) ap_next(g);
    }
}

int run_tests(void (*check)(int cond, const char *name))
{
    APGame *g = &tg;
    int k, ok, e, t;
    fix y0, v0;
    uint32_t h1;

    /* the map */
    check(ap_ground_y(FROMI(AP_HOME_X), FROMI(AP_HOME_Z)) == 0 && ap_on_runway(FROMI(AP_HOME_X), FROMI(AP_HOME_Z + 300)),
          "Larkspur Field: flat, the runway along z");
    check(iabs_(ap_ground_y(FROMI(ap_hills[0].x), FROMI(ap_hills[0].z)) - FROMI(ap_hills[0].h)) < FX(1), "a hill's top is its height");
    for (ok = 1, k = -6000; k <= 6000; k += 50) {
        if (ap_ground_y(FROMI(k), FROMI(ap_river_z(k))) != 0) ok = 0;
        if (ap_ground_y(FROMI(k), FROMI(ap_rail_z(k))) != 0) ok = 0;
        if (ap_ground_y(FROMI(k), FROMI(ap_trench_z(k))) != 0) ok = 0;
    }
    check(ok, "no hill on the river, the railway or the trenches");
    {
        fix x, z;
        for (ok = 1, k = 0; k <= 2300; k += 25) {
            ap_road_pos(FROMI(k), &x, &z);
            if (ap_ground_y(x, z) != 0) ok = 0;
        }
        check(ok, "nor on the road from Orme");
    }

    /* the missions */
    for (ok = 1, k = 1; k <= AP_MISSIONS; k++) {
        int j;
        ap_new_game(g, 7, k);
        if (g->mission != k || g->phase != PH_OUT || g->p[0].hp != AP_HP || g->p[0].ammo != AP_AMMO) ok = 0;
        if (g->need == 0 && g->nwp == 0) ok = 0;
        for (j = 0; j < g->ngr; j++) if (g->gr[j].type != GT_BALLOON && ap_ground_y(g->gr[j].x, g->gr[j].z) != 0) ok = 0;
        for (j = 1; j < g->np; j++) if (g->p[j].y < ap_ground_y(g->p[j].x, g->p[j].z) + FROMI(20)) ok = 0;
    }
    check(ok, "every mission: something to do, the player whole, nothing in the ground");
    ap_new_game(g, 7, 2);
    check(g->need == 2 && g->ngr == 6 && g->gr[0].type == GT_BALLOON, "mission 2: two balloons and their guns");
    ap_new_game(g, 7, 3);
    check(g->np == 3 && g->p[1].type == PT_HERON && g->p[2].type == PT_HERON && g->p[1].side == SIDE_HOME, "mission 3: two Herons to escort");
    ap_new_game(g, 7, 8);
    check(g->p[1].type == PT_FALCON && g->p[1].objective, "mission 8: the Grey Falcon");

    /* flying: straight and level at full throttle holds cruising speed */
    empty(g);
    for (k = 0; k < 100; k++) ap_tick(g, 0);
    check(iabs_(g->p[0].v - AP_V_CRUISE) < FX(0.05) && g->p[0].z > -FROMI(500) + FROMI(340), "full throttle, level: cruising speed, straight ahead");
    check(iabs_(g->p[0].y - FROMI(400)) < FX(1) && g->p[0].x == -FROMI(500), "level flight holds its height and line");
    v0 = g->p[0].v;
    for (k = 0; k < 30; k++) ap_tick(g, AP_IN_THR_DN);
    check(g->p[0].throttle == 0, "throttle down: closed");
    for (k = 0; k < 30; k++) ap_tick(g, 0);
    check(g->p[0].v < v0 - FX(0.5), "throttle closed: slowing");
    for (k = 0; k < 30; k++) ap_tick(g, AP_IN_THR_UP);
    check(g->p[0].throttle == 100, "throttle up: open");
    /* climbing costs speed, diving buys it */
    empty(g);
    y0 = g->p[0].y;
    for (k = 0; k < 12; k++) ap_tick(g, AP_IN_NOSEUP);
    check(g->p[0].pitch > 3000, "stick back: the nose comes up");
    for (k = 0; k < 40; k++) ap_tick(g, 0);
    check(g->p[0].y > y0 + FROMI(30) && g->p[0].v < AP_V_CRUISE - FX(0.4), "climbing: height bought with speed");
    empty(g);
    for (k = 0; k < 12; k++) ap_tick(g, AP_IN_NOSEDN);
    for (k = 0; k < 40; k++) ap_tick(g, 0);
    check(g->p[0].y < FROMI(400) - FROMI(60) && g->p[0].v > AP_V_CRUISE + FX(0.4), "diving: speed bought with height");
    /* bank to turn */
    empty(g);
    for (k = 0; k < 10; k++) ap_tick(g, AP_IN_RIGHT);
    check(g->p[0].roll > 5000, "right: the right wing goes down");
    check(g->p[0].yaw > 500 && g->p[0].yaw < 16384, "banked right: turning right");
    for (k = 0; k < 60; k++) ap_tick(g, 0);
    check(g->p[0].roll == 0, "let go: the wings level by themselves");
    empty(g);
    for (k = 0; k < 60; k++) ap_tick(g, AP_IN_LEFT | (k > 10 ? AP_IN_NOSEUP : 0));
    check(g->p[0].yaw > 22000 && g->p[0].yaw < 42000, "banked left and pulling: half a turn left in 2.4 s");
    /* the stall */
    empty(g);
    for (k = 0; k < 200 && !g->p[0].stalled; k++) ap_tick(g, AP_IN_THR_DN | AP_IN_NOSEUP);
    check(g->p[0].stalled && g->p[0].v < AP_V_STALL, "nose up, throttle closed: a stall");
    e = count_events(g, APE_STALL);
    check(e == 1, "the stall is told");
    for (k = 0; k < 40; k++) ap_tick(g, AP_IN_NOSEUP);
    check(g->p[0].pitch < 0, "stalled: the nose drops whatever the stick does");
    for (k = 0; k < 120 && g->p[0].stalled; k++) ap_tick(g, AP_IN_THR_UP | AP_IN_NOSEDN);
    check(!g->p[0].stalled, "stick forward, throttle open: flying again");
    /* the ground */
    empty(g);
    g->p[0].pitch = -6000;
    for (k = 0; k < 200 && g->p[0].state == PS_FLY; k++) ap_tick(g, 0);
    check(g->p[0].state == PS_DEAD && g->phase == PH_FAILED, "diving into the ground: a crash, the mission failed");
    /* landing on the runway, re-armed and patched up */
    empty(g);
    g->p[0].x = FROMI(AP_HOME_X); g->p[0].z = FROMI(AP_HOME_Z - 380); g->p[0].y = FROMI(6);
    g->p[0].v = FX(2.2); g->p[0].throttle = 0; g->p[0].pitch = 0;
    g->p[0].ammo = 3; g->p[0].hp = 5;
    for (k = 0; k < 20 && !g->p[0].on_ground; k++) ap_tick(g, 0);
    check(g->p[0].on_ground && g->p[0].state == PS_FLY && count_events(g, APE_LANDED) == 1, "a gentle touchdown: landed");
    for (k = 0; k < 200 && g->p[0].ammo < AP_AMMO; k++) ap_tick(g, AP_IN_THR_DN);
    check(g->p[0].ammo == AP_AMMO && g->p[0].hp == AP_HP && ap_on_runway(g->p[0].x, g->p[0].z), "stopped on the runway: re-armed and repaired");
    for (k = 0; k < 150 && g->p[0].on_ground; k++) ap_tick(g, AP_IN_THR_UP | (g->p[0].v > FX(2.7) ? AP_IN_NOSEUP : 0));
    check(!g->p[0].on_ground && g->p[0].state == PS_FLY, "throttle open, stick back: off again");
    empty(g);
    g->p[0].y = FROMI(3); g->p[0].v = FX(3.6); g->p[0].roll = 6000; g->p[0].pitch = -1000;
    for (k = 0; k < 10; k++) ap_tick(g, AP_IN_RIGHT);
    check(g->p[0].state == PS_DEAD, "touching down banked and fast: a crash");

    /* the guns */
    empty(g);
    for (k = 0; k < 10; k++) ap_tick(g, AP_IN_FIRE);
    check(g->shots == 10 && g->p[0].ammo == AP_AMMO - 10, "fire held: both guns every other tick");
    for (k = 0; k < 200 && !g->p[0].jam; k++) { ap_tick(g, AP_IN_FIRE); g->nev = 0; }
    check(g->p[0].jam > 0 && g->p[0].heat >= AP_JAM_HEAT, "held too long: the guns overheat and jam");
    t = g->shots;
    for (k = 0; k < 20; k++) ap_tick(g, AP_IN_FIRE);
    check(g->shots == t, "jammed: nothing fires");
    for (k = 0; k < AP_JAM_TICKS; k++) ap_tick(g, 0);
    check(g->p[0].jam == 0 && count_events(g, APE_UNJAM) == 1, "the jam clears");
    g->p[0].ammo = 1;
    g->p[0].heat = 0;
    for (k = 0; k < 10; k++) ap_tick(g, AP_IN_FIRE);
    check(g->p[0].ammo == 0 && g->shots == t + 1, "out of ammunition: the guns are silent");

    /* hits and kills: a Wasp held straight ahead */
    empty(g);
    e = ap_add_plane(g, PT_WASP, SIDE_ENEMY, g->p[0].x, g->p[0].y, g->p[0].z + FROMI(200), 0);
    g->p[e].held = 1; g->p[e].v = g->p[0].v;
    for (k = 0; k < 60 && g->p[e].state == PS_FLY; k++) ap_tick(g, AP_IN_FIRE);
    check(g->p[e].state == PS_FALL && g->kills == 1 && g->hits >= 6 && g->score == 100, "a Wasp ahead: six hits bring it down");
    for (k = 0; k < 400 && g->p[e].state != PS_DEAD; k++) ap_tick(g, 0);
    check(g->p[e].state == PS_DEAD, "and it falls to the ground");
    empty(g);
    e = ap_add_plane(g, PT_WASP, SIDE_ENEMY, g->p[0].x + FROMI(150), g->p[0].y, g->p[0].z + FROMI(200), 0);
    g->p[e].held = 1; g->p[e].v = g->p[0].v;
    for (k = 0; k < 30; k++) ap_tick(g, AP_IN_FIRE);
    check(g->hits == 0 && g->p[e].hp == g->p[e].maxhp, "one off to the side: missed");
    /* ground targets */
    empty(g);
    e = ap_add_ground(g, GT_DEPOT, g->p[0].x, 0, g->p[0].z + FROMI(260), 0, 1);
    g->need = 1;
    g->p[0].y = FROMI(120);
    g->p[0].pitch = -sang_test(a3_atan2(110, 260));
    for (k = 0; k < 40 && g->gr[e].alive; k++) ap_tick(g, AP_IN_FIRE);
    check(!g->gr[e].alive && g->gkills == 1 && g->done == 1 && g->phase == PH_HOME, "a dive on a supply dump: destroyed, home");
    /* enemy fire: a Hornet held behind, its gun at you */
    empty(g);
    e = ap_add_plane(g, PT_HORNET, SIDE_ENEMY, g->p[0].x, g->p[0].y, g->p[0].z - FROMI(150), 0);
    g->p[e].ai = AI_PURSUE; g->p[e].target = 0;
    for (k = 0; k < 60 && g->p[0].hp == AP_HP; k++) ap_tick(g, 0);
    check(g->p[0].hp < AP_HP && count_events(g, APE_HURT) > 0, "a Hornet on your tail: hit");
    g->p[0].hp = 1;
    for (k = 0; k < 1500 && g->p[0].state == PS_FLY; k++) ap_tick(g, 0);
    check(g->p[0].state != PS_FLY && g->phase == PH_FAILED, "shot down: the mission is failed");
    /* the anti-aircraft guns */
    empty(g);
    ap_add_ground(g, GT_AA, g->p[0].x + FROMI(300), 0, g->p[0].z + FROMI(500), 0, 0);
    for (k = 0, e = 0; k < 400; k++) { ap_tick(g, 0); e += count_events(g, APE_FLAK); }
    check(e >= 3, "an AA gun below: shells bursting round you");
    empty(g);
    g->p[0].y = FROMI(40);
    g->p[0].x = FROMI(-2600); g->p[0].z = FROMI(-2600);
    ap_add_ground(g, GT_AA, g->p[0].x + FROMI(300), 0, g->p[0].z + FROMI(500), 0, 0);
    for (k = 0, e = 0; k < 100; k++) { ap_tick(g, AP_IN_NOSEUP * (g->p[0].y < FROMI(40))); e += count_events(g, APE_FLAK); }
    check(e == 0, "hedge-hopping: under the guns");

    /* the computer's pilots */
    empty(g);
    e = ap_add_plane(g, PT_WASP, SIDE_ENEMY, g->p[0].x + FROMI(900), FROMI(450), g->p[0].z + FROMI(900), 0x8000);
    for (k = 0; k < 400; k++) ap_tick(g, 0);
    check(g->p[e].ai == AI_PURSUE || g->p[e].ai == AI_BREAK, "a Wasp sees you and comes for you");
    for (ok = 1, k = 0; k < 3000 && g->p[0].state == PS_FLY; k++) {
        ap_tick(g, ap_steer(g, &g->p[0], FROMI(-500), FROMI(400), FROMI(2000), 0));
        if (g->p[e].state == PS_DEAD) ok = 0;
    }
    check(ok && g->p[0].state != PS_FLY, "and shoots you down (never flying into the ground)");
    empty(g);
    e = ap_add_plane(g, PT_HORNET, SIDE_ENEMY, g->p[0].x, FROMI(400), g->p[0].z + FROMI(1200), 0x8000);
    g->p[e].hp = 2;
    g->p[e].ai = AI_PURSUE;
    for (k = 0; k < 10; k++) ap_tick(g, 0);
    check(g->p[e].ai == AI_FLEE, "badly hit: it runs for home");
    /* the train */
    ap_new_game(g, 3, 4);
    v0 = g->gr[0].x;
    for (k = 0; k < 100; k++) ap_tick(g, 0);
    check(g->gr[0].x > v0 + FROMI(120) && g->gr[0].z == FROMI(ap_rail_z(g->gr[0].x >> 16)), "the train runs east along the railway");
    g->gr[0].hp = 1;
    g->p[0].x = g->gr[0].x - FROMI(40); g->p[0].z = g->gr[0].z - FROMI(200); g->p[0].y = FROMI(80);
    g->p[0].yaw = ap_angle_to(g->gr[0].x - g->p[0].x, g->gr[0].z - g->p[0].z);
    g->p[0].pitch = -sang_test(a3_atan2(70, 200));
    for (k = 0; k < 30 && g->gr[0].alive; k++) ap_tick(g, AP_IN_FIRE);
    v0 = g->gr[1].x;
    ap_tick(g, 0);
    check(!g->gr[0].alive && g->gr[1].x == v0 && g->done == 1, "the engine destroyed: the train stops");

    /* the flow: home, done, the next; failed, a plane lost, again */
    empty(g);
    g->need = 1; g->done = 1;
    ap_tick(g, 0);
    check(g->phase == PH_HOME, "the objective done: home");
    g->p[0].x = FROMI(AP_HOME_X); g->p[0].z = FROMI(AP_HOME_Z);
    t = g->score;
    ap_tick(g, 0);
    check(g->phase == PH_DONE && g->score == t + 1000 + 20 * AP_HP && g->missions_flown == 1, "over the aerodrome: mission complete");
    for (k = 0; k < AP_END_TICKS; k++) ap_tick(g, 0);
    check(g->end_t == 0 && ap_next(g) && g->mission == 3 && g->phase == PH_OUT, "and on to the next mission");
    g->phase = PH_FAILED;
    check(ap_next(g) && g->lives == AP_LIVES - 1 && g->mission == 3, "failed: an aeroplane lost, the mission again");
    g->phase = PH_FAILED; ap_next(g);
    g->phase = PH_FAILED;
    check(!ap_next(g) && g->over, "the last aeroplane lost: the campaign is over");
    ap_new_game(g, 3, AP_MISSIONS);
    g->phase = PH_DONE;
    check(!ap_next(g) && g->won, "the last mission done: the war is won");
    check(ap_rank(0) == 0 && ap_rank(20000) == 4, "ranks by score");
    /* escort failure, bombers through */
    ap_new_game(g, 3, 3);
    g->p[1].hp = 1; g->p[2].hp = 1;
    { int h; for (h = 1; h <= 2; h++) { g->p[h].hp = 0; g->p[h].state = PS_FALL; } }
    g->lost = 2;
    ap_tick(g, 0);
    check(g->phase == PH_FAILED, "both Herons lost: failed");

    /* the autopilot */
    check(fly(g, 1, 1, 6000) == PH_DONE, "autopilot: flies the dawn patrol");
    check(fly(g, 2, 2, 7000) == PH_DONE, "autopilot: burns the balloons");
    check(fly(g, 3, 4, 8000) == PH_DONE, "autopilot: stops the train");
    check(fly(g, 4, 6, 8000) == PH_DONE, "autopilot: destroys the supply dumps");
    check(fly(g, 1, 3, 8000) == PH_DONE, "autopilot: brings a Heron home");
    check(fly(g, 1, 5, 8000) == PH_DONE, "autopilot: stops the bombers");
    check(fly(g, 1, 7, 8000) == PH_DONE, "autopilot: burns the hangars at Kessin");
    check(fly(g, 1, 8, 8000) == PH_DONE, "autopilot: brings down the Grey Falcon");

    /* determinism: the host and the Amiga must agree */
    campaign(g, 1234, AP_TRACE_TICKS);
    h1 = ap_state_hash(g);
    campaign(g, 1234, AP_TRACE_TICKS);
    check(ap_state_hash(g) == h1, "the same seed flies the same war");
    check(h1 == AP_TRACE_HASH, "the long autopilot campaign matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef AP_TRACE_MAIN
#include <stdio.h>
int main(int argc, char **argv)
{
    if (argc > 1) {                     /* a probe: each mission flown by the autopilot */
        int m, s;
        for (m = 1; m <= AP_MISSIONS; m++)
            for (s = 1; s <= 4; s++) {
                int ph = fly(&tg, (uint32_t)s, m, 9000);
                printf("mission %d seed %d: %s at %ld, kills %d, ground %d, hp %d, ammo %d, done %d/%d, lost %d\n", m, s,
                       ph == PH_DONE ? "done" : ph == PH_FAILED ? "FAILED" : "unfinished", (long)tg.mtick, tg.kills,
                       tg.gkills, tg.p[0].hp, tg.p[0].ammo, tg.done, tg.need, tg.lost);
            }
        return 0;
    }
    campaign(&tg, 1234, AP_TRACE_TICKS);
    printf("/* make vectors: seed 1234 from mission 1, %d ticks: mission %d, score %ld, kills %d, ground %d, shots %d, hits %d, lives %d%s%s */\n",
           AP_TRACE_TICKS, tg.mission, (long)tg.score, tg.kills, tg.gkills, tg.shots, tg.hits, tg.lives,
           tg.over ? ", over" : "", tg.won ? ", won" : "");
    printf("#define AP_TRACE_HASH 0x%08lXUL\n", (unsigned long)ap_state_hash(&tg));
    return 0;
}
#endif
