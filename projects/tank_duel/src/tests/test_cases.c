// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Tank Duel's tests: the rounds' line-ups and arenas,
 * driving, walls and obstacles, firing and reloading, hits and kills,
 * shells banked off a wall, the bulwark's shield, crates, mortar rounds
 * and their splash, dying, the round flow, the autopilot clearing rounds,
 * and a long autopilot fight whose final state hash (tests/vectors.h, from
 * `make vectors` on the host) the Amiga must reproduce exactly. On the
 * host (make host-test), on the Amiga (tank_duel_test) and inside the game
 * (the selftest hook).
 */
#include <string.h>
#include "../td.h"
#define TD_TRACE_TICKS 9000             /* six minutes of fighting */
#include "vectors.h"

const char test_client[] = "tank_duel_test";
static TDGame tg;                       /* off the Amiga's stack */

static int count_events(TDGame *g, int type)
{
    TDEvent e;
    int n = 0;
    while (td_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

/* an empty arena: the player at (0, -800) facing +z, nothing else */
static void empty(TDGame *g)
{
    td_new_game(g, 5, 1);
    g->nobs = 0;
    g->ntanks = 1;
    g->nev = 0;
}

static fix fabs_(fix a) { return a < 0 ? -a : a; }

static int fight(TDGame *g, uint32_t seed, int round, int max, int stop_after_clear)
{
    int t;
    TDEvent e;
    td_new_game(g, seed, round);
    for (t = 0; t < max && !g->over && !g->won; t++) {
        td_tick(g, td_bot(g));
        while (td_pop_event(g, &e)) if (e.type == TDE_ROUND_CLEAR && stop_after_clear) return 1;
    }
    return 0;
}

int run_tests(void (*check)(int cond, const char *name))
{
    TDGame *g = &tg;
    int k, ok, e;
    fix z0;
    uint32_t h1;

    /* the rounds */
    for (ok = 1, k = 1; k <= TD_ROUNDS; k++) {
        int j;
        td_new_game(g, 7, k);
        if (g->ntanks < 3 || g->nobs < 10 || td_blocked(g, g->tk[0].x, g->tk[0].z, TD_TANK_R)) ok = 0;
        for (j = 1; j < g->ntanks; j++) if (td_blocked(g, g->tk[j].x, g->tk[j].z, TD_TANK_R)) ok = 0;
    }
    check(ok, "every round: enemies and cover, nobody starting inside anything");
    td_new_game(g, 7, 1);
    check(g->ntanks == 3 && g->tk[1].type == TK_SCOUT && g->tk[2].type == TK_SCOUT, "round 1: two scouts");
    td_new_game(g, 7, 8);
    check(g->ntanks == 7 && g->tk[6].type == TK_BULWARK, "round 8: six, three of them bulwarks");
    check(g->tk[0].hp == TD_HP, "a game starts with the tank whole");

    /* driving */
    empty(g);
    for (k = 0; k < 20; k++) td_tick(g, TD_IN_UP);
    check(g->tk[0].z > -FROMI(800) + FROMI(80) && g->tk[0].x == 0 && g->tk[0].v == FX(6), "UP drives straight ahead, up to speed");
    for (k = 0; k < 5; k++) td_tick(g, TD_IN_RIGHT);
    check(g->tk[0].yaw == 3500, "RIGHT turns right");
    z0 = g->tk[0].z;
    for (k = 0; k < 20; k++) td_tick(g, TD_IN_DOWN);
    check(g->tk[0].z < z0, "DOWN backs up");
    empty(g);
    g->tk[0].yaw = 0x8000;
    for (k = 0; k < 100; k++) td_tick(g, TD_IN_UP);
    check(g->tk[0].z == -FROMI(TD_ARENA - TD_TANK_R), "the wall stops it");
    empty(g);
    g->nobs = 1;
    g->ob[0].kind = OB_PILLAR; g->ob[0].r = 26; g->ob[0].x = 0; g->ob[0].z = -FROMI(650); g->ob[0].hp = 1;
    for (k = 0; k < 60; k++) td_tick(g, TD_IN_UP);
    check(g->tk[0].z <= -FROMI(650 - 26 - TD_TANK_R) + FONE, "and a pillar");

    /* firing */
    empty(g);
    td_tick(g, TD_IN_FIRE);
    check(g->sh[0].alive && g->sh[0].owner == 0 && count_events(g, TDE_FIRE) == 1, "FIRE fires a shell");
    for (k = 0; k < TD_RELOAD - 2; k++) td_tick(g, TD_IN_FIRE);
    check(g->shots == 1, "then the gun reloads");
    for (k = 0; k < 4; k++) td_tick(g, TD_IN_FIRE);
    check(g->shots == 2, "for a second");
    empty(g);
    e = td_add_tank(g, TK_SCOUT, 0, -FROMI(600), 0x8000);
    g->tk[e].reload = 999;
    td_tick(g, TD_IN_FIRE);
    for (k = 0; k < 10 && g->tk[e].alive; k++) td_tick(g, 0);
    check(!g->tk[e].alive && g->kills == 1 && g->score >= 100 && g->hits == 1, "a hit kills a scout");

    /* a bank shot */
    empty(g);
    g->tk[0].x = FROMI(900);
    g->tk[0].z = 0;
    g->tk[0].yaw = 0x2000;                 /* ahead and to the right, into the right wall */
    td_tick(g, TD_IN_FIRE);
    for (k = 0; k < 12 && g->sh[0].alive; k++) td_tick(g, 0);
    check(g->sh[0].alive && g->sh[0].dx < 0 && g->sh[0].dz > 0, "a shell banks off the wall");
    for (k = 0; k < TD_SHELL_LIFE && g->sh[0].alive; k++) td_tick(g, 0);
    check(!g->sh[0].alive, "but only once");

    /* the bulwark */
    empty(g);
    e = td_add_tank(g, TK_BULWARK, 0, -FROMI(650), 0x8000);
    g->tk[e].reload = 999;
    td_tick(g, TD_IN_FIRE);
    g->nev = 0;
    for (k = 0; k < 8; k++) td_tick(g, 0);
    check(g->tk[e].hp == 2 && count_events(g, TDE_GLANCE) == 1, "a bulwark's shield turns a shot from the front");
    empty(g);
    e = td_add_tank(g, TK_BULWARK, 0, -FROMI(650), 0);
    g->tk[e].reload = 999;
    td_tick(g, TD_IN_FIRE);
    for (k = 0; k < 8; k++) td_tick(g, 0);
    check(g->tk[e].hp == 1 && g->hits == 1, "but not one from behind");

    /* crates */
    empty(g);
    g->nobs = 1;
    g->ob[0].kind = OB_CRATE; g->ob[0].r = 34; g->ob[0].x = 0; g->ob[0].z = -FROMI(600); g->ob[0].hp = 2;
    td_tick(g, TD_IN_FIRE);
    for (k = 0; k < 10; k++) td_tick(g, 0);
    check(g->ob[0].hp == 1, "a crate takes a hit");
    for (k = 0; k < TD_RELOAD; k++) td_tick(g, TD_IN_FIRE);
    for (k = 0; k < 10; k++) td_tick(g, 0);
    check(g->ob[0].hp == 0 && !td_blocked(g, 0, -FROMI(600), 4), "and breaks at the second, leaving the way open");

    /* mortars */
    empty(g);
    e = td_add_tank(g, TK_MORTAR, 0, FROMI(100), 0x8000);
    g->tk[e].reload = 0;
    g->nev = 0;
    td_tick(g, 0);
    check(g->mo[0].alive && fabs_(g->mo[0].tx) < FROMI(5) && fabs_(g->mo[0].tz + FROMI(800)) < FROMI(5) && count_events(g, TDE_LOB) == 1,
          "a mortar lobs a round at where you stand");
    for (k = 0; k < 50; k++) td_tick(g, 0);
    check(!g->mo[0].alive && g->tk[0].hp <= TD_HP - TD_SPLASH_DMG + 2, "standing still under it hurts");
    empty(g);
    e = td_add_tank(g, TK_MORTAR, 0, FROMI(100), 0x8000);
    g->tk[e].reload = 0;
    td_tick(g, 0);
    g->tk[e].reload = 999;
    for (k = 0; k < 50; k++) td_tick(g, TD_IN_UP);
    check(g->tk[0].hp == TD_HP, "driving out of the ring doesn't");

    /* dying, and the rounds */
    empty(g);
    g->tk[0].hp = 10;
    e = td_add_tank(g, TK_BULWARK, 0, -FROMI(600), 0x8000);
    g->tk[e].reload = 0;
    for (k = 0; k < 40 && !g->over; k++) td_tick(g, 0);
    check(g->over && !g->tk[0].alive && g->tk[0].hp == 0, "shot with 10 left: dead");
    td_new_game(g, 3, 1);
    g->tk[0].hp = 50;
    for (k = 1; k < g->ntanks; k++) g->tk[k].alive = 0;
    g->nev = 0;
    td_tick(g, 0);
    check(g->between == TD_BETWEEN && count_events(g, TDE_ROUND_CLEAR) == 1 && g->score == 500 + 250, "every enemy gone: the round is won");
    for (k = 0; k < TD_BETWEEN; k++) td_tick(g, 0);
    check(g->round == 2 && g->tk[0].hp == 80 && td_enemies_left(g) == 3, "round 2, the tank patched up by 30");
    td_new_game(g, 3, TD_ROUNDS);
    for (k = 1; k < g->ntanks; k++) g->tk[k].alive = 0;
    for (k = 0; k <= TD_BETWEEN + 1; k++) td_tick(g, 0);
    check(g->won, "the last round won wins");

    /* the autopilot */
    for (k = 1; k <= 3; k++) check(fight(g, (uint32_t)k, 1, 3000, 1), "autopilot: clears round 1");
    check(fight(g, 1, 2, 6000, 1), "autopilot: clears round 2 (a bulwark: flanked)");
    check(fight(g, 2, 3, 6000, 1), "autopilot: clears round 3 (scout, mortar, bulwark)");

    /* determinism: the host and the Amiga must agree */
    fight(g, 1234, 1, TD_TRACE_TICKS, 0);
    h1 = td_state_hash(g);
    fight(g, 1234, 1, TD_TRACE_TICKS, 0);
    check(td_state_hash(g) == h1, "the same seed fights the same fight");
    check(h1 == TD_TRACE_HASH, "the long autopilot fight matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef TD_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    fight(&tg, 1234, 1, TD_TRACE_TICKS, 0);
    printf("/* make vectors: seed 1234 from round 1, %d ticks: round %d, score %ld, kills %d, shots %d, hits %d, hp %d%s */\n",
           TD_TRACE_TICKS, tg.round, (long)tg.score, tg.kills, tg.shots, tg.hits, tg.tk[0].hp, tg.over ? ", dead" : "");
    printf("#define TD_TRACE_HASH 0x%08lXUL\n", (unsigned long)td_state_hash(&tg));
    return 0;
}
#endif
