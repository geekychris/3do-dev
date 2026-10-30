// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * trace_main.c - scairy_fun_trace: drive a route with the perfect autopilot
 * and print every event and the car every 250 frames. Built for the host
 * (make host-trace) and the Amiga (scairy_fun_trace), so the two can be
 * diffed when they disagree.
 *
 *   scairy_fun_trace [route 1-4] [traffic %] [frames] [detail from frame]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../sc.h"

static World w;

int main(int argc, char **argv)
{
    int route = argc > 1 ? atoi(argv[1]) - 1 : 0, f, i, frames = argc > 3 ? atoi(argv[3]) : 7500;
    int detail = argc > 4 ? atoi(argv[4]) : 1 << 30;
    Event e;
    world_init(&w);
    w.tune.ai_perfect = 1;
    w.tune.traffic_pct = argc > 2 ? atoi(argv[2]) : 50;
    world_start_game(&w);
    if (route) world_start_route(&w, route);
    w.state = GS_PLAY;
    for (f = 0; f < frames; f++) {
        world_update(&w, 0);
        while (world_pop_event(&w, &e))
            if (e.type != EV_SAY && e.type != EV_MULT_UP)
                printf("f%ld %s %ld %ld\n", (long)f, ev_name[e.type], (long)e.a, (long)e.b);
        if (f % 250 == 0)
            printf("f%ld seg %ld d %ld v %ld rng %lu\n", (long)f, (long)seg_of(w.car.z),
                   (long)(w.car.d >> FBITS), (long)w.car.v, (unsigned long)w.rng);
        if (f >= detail) {
            printf("f%ld car z %ld d %ld v %ld s %ld lane %ld tv %ld\n", (long)f, (long)w.car.z, (long)w.car.d,
                   (long)w.car.v, (long)w.car.s, (long)w.ai.lane, (long)w.ai.target_v);
            for (i = 0; i < MAX_OBJS; i++)
                if (w.obj[i].type)
                    printf("  %ld %s z %ld d %ld v %ld lane %ld t %ld fl %ld hz %ld vt %ld cool %ld\n", (long)i,
                           obj_name(w.obj[i].type), (long)w.obj[i].z, (long)w.obj[i].d, (long)w.obj[i].v,
                           (long)w.obj[i].lane, (long)w.obj[i].timer, (long)w.obj[i].flags, (long)w.obj[i].hz,
                           (long)w.obj[i].vt, (long)w.obj[i].cool);
        }
        if (w.state != GS_PLAY) break;
    }
    return 0;
}
