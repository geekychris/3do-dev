/*
 * trace.c - (the 3DO port) a scripted flight for comparing the 3DO's
 * simulation with the host's: the same inputs every run (lift off, then
 * turns, climbs, throttle and the cannon on a fixed pattern) for 4000
 * frames, the state printed every 250. The test disc prints it as TRACE
 * lines; test.py builds this file natively (-DTS_TRACE_MAIN) and compares.
 */
#include <stdio.h>
#include "../ts.h"

static World trw;

static unsigned script(int f)
{
    unsigned in = IN_FIRE;
    if (f < 60) return IN_UP;
    if ((f / 90) % 4 == 1) in |= IN_LEFT;
    if ((f / 90) % 4 == 3) in |= IN_RIGHT;
    if ((f / 40) % 5 == 0) in |= IN_UP;
    if ((f / 40) % 5 == 2) in |= IN_DOWN;
    if ((f % 200) < 30) in |= IN_FAST;
    if ((f % 300) > 280) in |= IN_SLOW;
    return in;
}

void ts_trace(void (*out)(const char *line))
{
    char line[160];
    Event e;
    int f;
    world_init(&trw, 1987);
    world_start_game(&trw);
    for (f = 0; f < 4000; f++) {
        world_update(&trw, script(f));
        while (world_pop_event(&trw, &e)) {
            if (e.type == EV_FIRE || e.type == EV_ENEMY_FIRE) continue;
            sprintf(line, "f%ld ev %ld %ld %ld", (long)f, (long)e.type, (long)e.a, (long)e.b);
            out(line);
        }
        if (f % 250 == 0) {
            sprintf(line, "f%ld st %ld score %ld lives %ld targets %ld p %ld %ld %ld fuel %ld shield %ld yaw %ld rng %lu",
                    (long)f, (long)trw.state, (long)trw.score, (long)trw.lives, (long)trw.targets_left,
                    (long)trw.p.x, (long)trw.p.y, (long)trw.p.z, (long)trw.p.fuel, (long)trw.p.shield,
                    (long)trw.p.yaw, (unsigned long)trw.rng);
            out(line);
        }
    }
}

#ifdef TS_TRACE_MAIN
static void put(const char *s) { printf("%s\n", s); }
int main(void) { ts_trace(put); return 0; }
#endif
