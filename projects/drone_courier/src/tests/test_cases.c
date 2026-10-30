// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Drone Courier's tests: the city, flying (turning,
 * forward, climbing, sinking, the wind), roofs and towers' sides,
 * collecting and delivering on the pads, hard landings and fragile
 * parcels, the clock, the battery, the days, the autopilot working whole
 * days, and a long autopilot game whose final state hash (tests/vectors.h,
 * from `make vectors` on the host) the Amiga must reproduce exactly. On
 * the host (make host-test), on the Amiga (drone_courier_test) and inside
 * the game (the selftest hook).
 */
#include <string.h>
#include "../dc.h"
#define DC_TRACE_TICKS 9000             /* six minutes of deliveries */
#include "vectors.h"

const char test_client[] = "drone_courier_test";
static DCGame tg;                       /* off the Amiga's stack */

static int count_events(DCGame *g, int type)
{
    DCEvent e;
    int n = 0;
    while (dc_pop_event(g, &e)) if (e.type == type) n++;
    return n;
}

/* a still day: day 1, no wind, the drone hovering `above` over block b's roof */
static void hover_over(DCGame *g, int b, int above)
{
    dc_new_game(g, 3, 1);
    g->x = dc_block_x(b % DC_N);
    g->z = dc_block_z(b / DC_N);
    g->y = FROMI(dc_roof(g, b) + above);
    g->vx = g->vy = g->vz = 0;
    g->landed = 0;
    g->nev = 0;
}

/* sink straight down until landed */
static void settle(DCGame *g)
{
    int k;
    for (k = 0; k < 400 && !g->landed; k++) dc_tick(g, g->vy < -FX(1.0) ? DC_IN_CLIMB : 0);
}

static int work(DCGame *g, uint32_t seed, int day, int max)
{
    int t;
    dc_new_game(g, seed, day);
    for (t = 0; t < max && !g->over && !g->between; t++) dc_tick(g, dc_bot(g));
    return g->between > 0;
}

int run_tests(void (*check)(int cond, const char *name))
{
    DCGame *g = &tg;
    int k, ok, b;
    fix y0;
    uint32_t h1;

    /* the city */
    for (ok = 1, k = 1; k <= DC_DAYS; k++) {
        dc_new_game(g, 5, k);
        if (!g->landed || !g->carrying || dc_block_at(g->x, g->z) != g->depot || g->y != FROMI(120)) ok = 0;
        if (dc_roof(g, g->target) < 40 || g->target == g->depot || g->jobs != 3 + k) ok = 0;
    }
    check(ok, "every day: on the depot's roof with a parcel, a customer on a tower");
    check(dc_block_at(dc_block_x(4), dc_block_z(7)) == 7 * DC_N + 4, "blocks and coordinates agree");
    check(dc_block_at(dc_block_x(4) + FROMI(DC_FOOT + 5), dc_block_z(7)) == -1, "between towers: a street");
    dc_new_game(g, 5, 1);
    check(g->wind_x == 0 && g->wind_z == 0, "day 1 is still");
    dc_new_game(g, 5, 5);
    check(g->wind_x != 0 && g->pad_r < 30, "day 5: wind, and smaller pads");

    /* flying */
    hover_over(g, 6 * DC_N + 6, 150);
    g->yaw = 0;
    for (k = 0; k < 10; k++) dc_tick(g, DC_IN_UP | DC_IN_CLIMB);
    check(g->vz > FX(2) && g->vx == 0 && g->vy > 0, "forward along the heading, climbing");
    for (k = 0; k < 45; k++) dc_tick(g, 0);
    y0 = g->y;
    for (k = 0; k < 10; k++) dc_tick(g, 0);
    check(g->vy == -FX(1.2) && g->y - (y0 - 10 * FX(1.2)) < FX(0.01) && g->y - (y0 - 10 * FX(1.2)) > -FX(0.01), "let go, it sinks slowly (1.2 a tick)");
    check(g->vz < FX(2), "and drifts to a stop");
    for (k = 0; k < 8; k++) dc_tick(g, DC_IN_RIGHT);
    check(g->yaw == 8 * 520, "RIGHT turns");
    dc_new_game(g, 5, 4);
    g->wind_x = FX(0.02);
    g->wind_z = 0;
    g->gust_timer = 9999;
    for (k = 0; k < 10; k++) dc_tick(g, 0);
    check(g->vx == 0 && g->landed, "sitting on a roof, the wind doesn't move it");
    for (k = 0; k < 30; k++) dc_tick(g, DC_IN_CLIMB);
    check(!g->landed && g->vx > 0, "in the air, it does");

    /* roofs and sides */
    b = 6 * DC_N + 6;
    hover_over(g, b, 30);
    settle(g);
    check(g->landed && g->y == FROMI(dc_roof(g, b)), "it settles onto a roof");
    hover_over(g, b, -20);
    g->x = dc_block_x(b % DC_N) - FROMI(DC_FOOT + 20);
    g->yaw = 0x4000;
    g->vx = FX(3);
    g->carrying = 0;
    for (k = 0; k < 20 && !g->bumps; k++) dc_tick(g, 0);
    check(g->bumps == 1 && g->vx < 0 && dc_block_at(g->x, g->z) != b, "into a tower's side: bounced back off it");
    hover_over(g, b, -20);
    g->x = dc_block_x(b % DC_N) - FROMI(DC_FOOT + 20);
    g->vx = FX(4);
    g->carrying = 1;
    g->fragile = 1;
    for (k = 0; k < 20 && !g->bumps; k++) dc_tick(g, 0);
    check(g->breakages == 1 && !g->carrying && g->fails == 1, "a fragile parcel knocked hard: broken, a job failed");

    /* the pads */
    dc_new_game(g, 3, 1);
    b = g->target;
    hover_over(g, b, 25);
    g->carrying = 1;
    g->target = b;
    settle(g);
    check(g->jobs_done == 1 && !g->carrying && g->score >= 200 && count_events(g, DCE_DELIVER) == 1, "landed on the customer's pad: delivered");
    check(g->target != b, "and the next customer is someone else");
    hover_over(g, 1 * DC_N + 1, 40);
    g->carrying = 0;
    g->battery = FROMI(30);
    settle(g);
    check(g->carrying && g->battery == FROMI(100) && count_events(g, DCE_PICKUP) == 1, "landed on the depot: the next parcel, and the battery charged");
    dc_new_game(g, 3, 1);
    b = g->target;
    hover_over(g, b, 3);
    g->carrying = 1;
    g->target = b;
    g->x += FROMI(DC_FOOT - 8);
    g->z += FROMI(DC_FOOT - 8);
    settle(g);
    check(g->landed && g->carrying && g->jobs_done == 0, "on the roof but off the pad: nothing");
    dc_new_game(g, 3, 1);
    b = g->target;
    hover_over(g, b, 2);
    g->carrying = 1;
    g->fragile = 1;
    g->target = b;
    g->vy = -FX(4);
    dc_tick(g, 0);
    check(g->breakages == 1 && g->fails == 1, "dropped hard onto the pad with a fragile parcel: broken");

    /* the clock, the battery, the days */
    dc_new_game(g, 5, 1);
    b = g->target;
    g->job_time = 1;
    dc_tick(g, 0);
    check(g->fails == 1 && g->target != b && count_events(g, DCE_LATE) == 1, "out of time: a job failed, a new customer");
    g->fails = DC_MAX_FAILS - 1;
    g->job_time = 1;
    dc_tick(g, 0);
    check(g->over, "the third failure ends the game");
    dc_new_game(g, 5, 1);
    g->battery = FX(0.01);
    dc_tick(g, 0);
    check(g->over && count_events(g, DCE_FLAT) == 1, "a flat battery ends it too");
    dc_new_game(g, 5, 1);
    g->jobs_done = g->jobs - 1;
    b = g->target;
    dc_new_game(g, 3, 1);
    g->jobs_done = g->jobs - 1;
    b = g->target;
    g->x = dc_block_x(b % DC_N); g->z = dc_block_z(b / DC_N); g->y = FROMI(dc_roof(g, b) + 20);
    g->carrying = 1;
    g->landed = 0;
    settle(g);
    check(g->between == DC_BETWEEN, "the day's last parcel: the day's done");
    for (k = 0; k < DC_BETWEEN; k++) dc_tick(g, 0);
    check(g->day == 2 && g->jobs_done == 0 && g->carrying, "day 2 starts at the depot");

    /* the autopilot */
    for (k = 1; k <= 3; k++) check(work(g, (uint32_t)k, 1, 9000) && g->fails == 0 && g->bumps == 0, "autopilot: a whole first day, cleanly");
    check(work(g, 1, 4, 12000) && g->fails == 0, "autopilot: day 4 (wind, gusts, small pads)");

    /* determinism: the host and the Amiga must agree */
    dc_new_game(g, 1234, 1);
    for (k = 0; k < DC_TRACE_TICKS && !g->over && !g->won; k++) dc_tick(g, dc_bot(g));
    h1 = dc_state_hash(g);
    check(g->day >= 2, "autopilot: on into day 2");
    check(h1 == DC_TRACE_HASH, "the long autopilot game matches the host's (tests/vectors.h)");
    return 0;
}

#ifdef DC_TRACE_MAIN
#include <stdio.h>
int main(void)
{
    int k;
    dc_new_game(&tg, 1234, 1);
    for (k = 0; k < DC_TRACE_TICKS && !tg.over && !tg.won; k++) dc_tick(&tg, dc_bot(&tg));
    printf("/* make vectors: seed 1234 from day 1, %d ticks: day %d, jobs %d, fails %d, score %ld */\n",
           k, tg.day, tg.jobs_done, tg.fails, (long)tg.score);
    printf("#define DC_TRACE_HASH 0x%08lXUL\n", (unsigned long)dc_state_hash(&tg));
    return 0;
}
#endif
