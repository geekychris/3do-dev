// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Harbour Crane's tests: the trolley, the winch and the
 * swing, grabbing, landing (square, dented, off a slot's edge, across
 * uneven stacks, off the end, in the harbour, back on the quay), the
 * ship's list and capsizing, the level flow, the autopilot loading whole
 * levels, and a long autopilot game whose final state hash
 * (tests/vectors.h, from `make vectors` on the host) the Amiga must
 * reproduce exactly. On the host (make host-test), on the Amiga
 * (harbour_crane_test) and inside the game (the selftest hook).
 */
#include <string.h>
#include "../hc.h"
#define HC_TRACE_TICKS 15000            /* 10 minutes of play */
#include "vectors.h"

const char test_client[] = "harbour_crane_test";
static HCGame tg;                       /* off the Amiga's stack */

static int count_events(HCGame *g, int type)
{
    HCEvent e;
    int n = 0;
    while (hc_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

static fix fabs_(fix a) { return a < 0 ? -a : a; }

/* a calm level: no wind, no swell, one-slot boxes of weight w */
static void calm(HCGame *g, int w)
{
    int k;
    hc_new_game(g, 9, 1);
    g->swell = 0;
    g->wind = 0;
    for (k = 0; k < g->nbox; k++) { g->box[k].len = 1; g->box[k].weight = w; }
    g->nev = 0;
}

/* let box b go with its left end at x, h units above whatever is under
 * it, and wait for it to land; returns the events' count of type ev */
static int drop(HCGame *g, int b, int x, int h, int ev)
{
    int k, n = 0;
    HCEvent e;
    g->nev = 0;
    g->falling = b;
    g->fx = FROMI(x);
    g->fy = hc_support_y(g, g->fx, g->fx + FROMI(hc_box_w(g, b)), 0) + FROMI(h);
    g->fy0 = g->fy;
    g->fvx = g->fvy = 0;
    for (k = 0; k < 60 && g->falling >= 0; k++) {
        hc_tick(g, 0);
        while (hc_pop_event(g, &e)) if (e.type == ev) n++;
    }
    hc_tick(g, 0);                      /* the ship settles to her new load */
    while (hc_pop_event(g, &e)) if (e.type == ev) n++;
    return n;
}

static int slot_x(int s) { return HC_SHIP_X0 + s * HC_SLOT_W; }

static int autoplay(HCGame *g, uint32_t seed, int level, int max, int stop_after_clear)
{
    int t;
    HCEvent e;
    hc_new_game(g, seed, level);
    for (t = 0; t < max && !g->over && !g->won; t++) {
        hc_tick(g, hc_bot(g));
        while (hc_pop_event(g, &e)) if (e.type == HCE_CLEAR && stop_after_clear) return 1;
    }
    return 0;
}

int run_tests(void (*check)(int cond, const char *name))
{
    HCGame *g = &tg;
    int k, ok, b;
    fix th0, x0;
    uint32_t h1;

    /* levels */
    for (k = 1; k <= HC_LEVELS; k++) {
        int j;
        hc_new_game(g, 3, k);
        ok = g->nbox == (5 + k < 12 ? 5 + k : 12);
        for (j = 0; j < g->nbox; j++)
            if (g->box[j].len < 1 || g->box[j].len > 2 || g->box[j].weight < 1 || g->box[j].weight > 4) ok = 0;
        check(ok, "every level: 6 to 12 containers, 1 or 2 slots long, weight 1 to 4");
    }
    hc_new_game(g, 3, 1);
    for (ok = 1, k = 0; k < g->nbox; k++) if (g->box[k].len != 1) ok = 0;
    check(ok && g->wind == 0, "level 1: short containers and no wind");
    check(g->carrying < 0 && g->tx == FROMI(HC_PICK_X), "the trolley starts over the quay");

    /* the trolley and the winch */
    calm(g, 1);
    for (k = 0; k < 10; k++) hc_tick(g, HC_IN_RIGHT);
    check(g->tv > 0 && g->tv < FX(1), "the trolley gathers speed slowly");
    for (k = 0; k < 200; k++) hc_tick(g, HC_IN_RIGHT);
    check(g->tx == FROMI(HC_TX_MAX) && g->tv == 0, "and stops at the end of the gantry");
    calm(g, 1);
    x0 = g->L;
    for (k = 0; k < 5; k++) hc_tick(g, HC_IN_UP);
    check(g->L == x0 - 5 * FX(4), "UP winds the cable in");
    for (k = 0; k < 200; k++) hc_tick(g, HC_IN_UP);
    check(g->L == FROMI(HC_L_MIN), "as far as it goes");

    /* the swing */
    calm(g, 1);
    g->L = FROMI(200);
    g->th = th0 = FX(0.3);
    for (k = 0; k < 38; k++) hc_tick(g, 0);
    check(g->th < -FX(0.2), "a load swings across in half a swing (about 1.5 s on 200 units)");
    for (k = 0; k < 400; k++) hc_tick(g, 0);
    check(fabs_(g->th) < th0 && fabs_(g->th) + fabs_(g->om) * 10 > FX(0.02), "and slowly settles");
    calm(g, 1);
    g->L = FROMI(200);
    for (k = 0; k < 8; k++) hc_tick(g, HC_IN_RIGHT);
    check(g->th < 0, "the trolley pulling away leaves the load behind");

    /* grabbing */
    calm(g, 1);
    for (k = 0; k < 80; k++) hc_tick(g, HC_IN_DOWN);
    check(fabs_(hc_hook_y(g) - FROMI(HC_BOX_H + 4)) < FROMI(2), "the hook comes to rest on the container");
    g->nev = 0;
    hc_tick(g, HC_IN_FIRE);
    check(g->carrying == 0 && g->next == 1 && count_events(g, HCE_GRAB) == 1, "fire grabs it");
    calm(g, 1);
    hc_tick(g, HC_IN_FIRE);
    check(g->carrying < 0 && g->next == 0, "fire with the hook up in the air does nothing");

    /* landing */
    calm(g, 2);
    check(drop(g, 0, slot_x(2), 10, HCE_LAND) == 1, "a container lands square in a slot");
    check(g->height[2] == 1 && g->loaded == 1 && g->score == 100 && g->nplaced == 1, "it is loaded and scores 50 x its weight");
    check(drop(g, 1, slot_x(2) + 9, 10, HCE_LAND) == 1 && g->height[2] == 2, "a little off the slot's edge still stacks square");
    check(drop(g, 2, slot_x(3), 90, HCE_DENT) == 1 && g->dents == 1, "dropped from high up: dented");
    check(drop(g, 3, slot_x(4) + 25, 5, HCE_LOST) == 1 && g->lost == 1 && g->height[4] == 0,
          "landed across two slots: it topples into the harbour");
    check(drop(g, 4, HC_SHIP_X0 + HC_SLOTS * HC_SLOT_W - 10, 5, HCE_LOST) == 1, "off the end of the deck: lost");
    check(g->lost == 2 && !g->over, "two lost so far");
    check(drop(g, 5, 60, 5, HCE_LOST) == 1 && g->lost == 3 && g->over, "in the water: lost, and three lost ends the game");
    calm(g, 1);
    g->next = 1;
    check(drop(g, 0, HC_PICK_X - 24, 10, HCE_LAND) == 1 && g->next == 0, "let go over the quay: it waits there again");

    /* long containers and stacks */
    calm(g, 1);
    g->box[0].len = 2;
    drop(g, 1, slot_x(0), 5, HCE_LAND);
    check(drop(g, 0, slot_x(0), 5, HCE_LOST) == 1, "a long one across uneven stacks topples");
    calm(g, 1);
    g->box[0].len = 2;
    check(drop(g, 0, slot_x(1), 5, HCE_LAND) == 1 && g->height[1] == 1 && g->height[2] == 1, "a long one fills two slots");
    calm(g, 1);
    for (k = 0; k < HC_TIERS; k++) drop(g, k, slot_x(5), 5, HCE_LAND);
    check(g->height[5] == HC_TIERS && g->loaded == HC_TIERS, "four high");
    check(drop(g, HC_TIERS, slot_x(5), 5, HCE_LOST) == 1, "and no higher");

    /* the ship */
    calm(g, 3);
    drop(g, 0, slot_x(5), 5, HCE_LAND);
    check(g->list > 0, "a heavy box on the right lists the ship to the right");
    drop(g, 1, slot_x(0), 5, HCE_LAND);
    check(fabs_(g->list) < FX(0.005), "one the same on the left trims it");
    check(hc_best_slot(g, 2) == 2 || hc_best_slot(g, 2) == 3, "the autopilot loads the middle next");
    calm(g, 3);
    for (k = 0; k < 4; k++) { g->box[k].len = 2; g->box[k].weight = 4; }
    for (k = 0; k < 4 && !g->over; k++) drop(g, k, slot_x(4), 3, HCE_LAND);
    hc_tick(g, 0);
    check(g->capsized && g->over, "four heavy long containers at one end: she capsizes");
    calm(g, 1);
    g->swell = FX(0.05);
    for (k = 0; k < 160; k++) hc_tick(g, 0);
    check(g->heel != 0 && fabs_(g->heel) <= FX(0.051), "the swell rolls her");

    /* the level flow */
    calm(g, 1);
    for (k = 0; k < g->nbox; k++) drop(g, k, slot_x(k % HC_SLOTS), 3, HCE_LAND);
    g->next = g->nbox;
    g->nev = 0;
    hc_tick(g, 0);
    check(g->between == HC_BETWEEN && count_events(g, HCE_CLEAR) == 1, "the last container loaded clears the level");
    for (k = 0; k < HC_BETWEEN; k++) hc_tick(g, 0);
    check(g->level == 2 && g->nplaced == 0 && g->next == 0, "then level 2, on an empty ship");
    hc_new_game(g, 3, HC_LEVELS);
    g->next = g->nbox;
    hc_tick(g, 0);
    for (k = 0; k <= HC_BETWEEN; k++) hc_tick(g, 0);
    check(g->won, "clearing the last level wins");
    calm(g, 1);
    g->time_left = 3;
    for (k = 0; k < 5; k++) hc_tick(g, 0);
    check(g->over && !g->capsized, "the clock running out ends it");

    /* the autopilot */
    for (b = 1; b <= 3; b++) {
        check(autoplay(g, (uint32_t)b, 1, 6000, 1), "autopilot: loads level 1");
        check(g->lost == 0 && !g->capsized, "autopilot: nothing lost, still afloat");
    }
    check(autoplay(g, 4, 4, 9000, 1) && !g->capsized, "autopilot: loads level 4 (wind, gusts, long containers)");

    /* determinism: the host and the Amiga must agree */
    autoplay(g, 1234, 1, HC_TRACE_TICKS, 0);
    h1 = hc_state_hash(g);
    autoplay(g, 1234, 1, HC_TRACE_TICKS, 0);
    check(hc_state_hash(g) == h1, "the same seed plays the same game");
    check(h1 == HC_TRACE_HASH, "the long autopilot game matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef HC_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    autoplay(&tg, 1234, 1, HC_TRACE_TICKS, 0);
    printf("/* make vectors: seed 1234 from level 1, %d ticks: level %d, score %ld, loaded %d, lost %d, dents %d */\n",
           HC_TRACE_TICKS, tg.level, (long)tg.score, tg.total_loaded, tg.lost, tg.dents);
    printf("#define HC_TRACE_HASH 0x%08lXUL\n", (unsigned long)hc_state_hash(&tg));
    return 0;
}
#endif
