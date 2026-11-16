/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */
/*
 * Miner Meteor route check: plays a route through each of the six caverns
 * on the port's own src/game.c and src/levels.c, built for the host, one
 * game_tick per input, and reports whether each cavern is cleared (all the
 * keys, then the exit) without losing a life.
 *
 * tools/routes.json holds one list of [input flags, ticks] runs per cavern
 * (1 left, 2 right, 4 jump), found by tools/route_search.cpp over the same
 * game code. A run also fails if the player is ever standing (on_ground)
 * with no floor under their feet, which the landing snap used to do (the
 * bob). Exit status 0 only when all six are cleared.
 *
 *   tools/route_check.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "levels.h"

#define MAXRUNS 512
static int runs[6][MAXRUNS][2], nruns[6];

/* routes.json: [[[flags, n], ...], ...] - read the numbers in order */
static void load(const char *path)
{
    FILE *f = fopen(path, "r");
    int depth = 0, c, have = 0, num = 0, first = -1, lv = -1;
    if (!f) { perror(path); exit(2); }
    while ((c = fgetc(f)) != EOF) {
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); have = 1; continue; }
        if (have) {
            if (first < 0) first = num;
            else if (lv >= 0 && lv < 6 && nruns[lv] < MAXRUNS) {
                runs[lv][nruns[lv]][0] = first; runs[lv][nruns[lv]][1] = num; nruns[lv]++; first = -1;
            }
            num = 0; have = 0;
        }
        if (c == '[' && ++depth == 2) lv++;
        if (c == ']') depth--;
    }
    fclose(f);
}

static int solid(const GameState *s, LONG px, LONG py)
{
    UBYTE k;
    if (px < 0 || py < 0 || px >= PLAY_W || py >= PLAY_H) return 1;
    k = s->grid[py / TILE_H][px / TILE_W].kind;
    return k == T_SOLID || k == T_COLLAPSE || k == T_CONV_L || k == T_CONV_R;
}

int main(int argc, char **argv)
{
    static GameState gs;
    LONG lv;
    int cleared = 0;
    load(argc > 1 ? argv[1] : "tools/routes.json");
    for (lv = 0; lv < NUM_LEVELS; lv++) {
        LONG lives;
        int ticks = 0, done = 0, r, n;
        const char *why = "route ended";
        game_init(&gs);
        gs.mode = GM_PLAYING;
        game_load_level(lv);
        lives = gs.lives;
        for (r = 0; r < nruns[lv] && !done; r++)
            for (n = 0; n < runs[lv][r][1] && !done; n++) {
                game_tick((UBYTE)runs[lv][r][0]);
                ticks++;
                if (gs.mode == GM_LEVELWIN || gs.mode == GM_WIN) done = 1;
                else if (gs.lives < lives || gs.mode == GM_LOSE) { why = "lost a life"; done = -1; }
                else if (gs.player.on_ground && !solid(&gs, gs.player.x + 1, gs.player.y + 16)
                         && !solid(&gs, gs.player.x + 14, gs.player.y + 16)) { why = "stood on air (bob)"; done = -1; }
            }
        if (done == 1) cleared++;
        printf("cavern %ld %-16s %s after %d ticks (keys left %ld)\n", (long)lv + 1,
               LEVELS[lv].name, done == 1 ? "cleared" : why, ticks, (long)gs.keys_remaining);
    }
    printf("%d of %ld caverns cleared\n", cleared, (long)NUM_LEVELS);
    return cleared == NUM_LEVELS ? 0 : 1;
}
