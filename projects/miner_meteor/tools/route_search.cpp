// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>
/*
 * Miner Meteor route search: a breadth-first search over the port's own
 * src/game.c and src/levels.c, built for the host, one game_tick per step
 * with the six inputs (none, left, right, jump, left+jump, right+jump).
 * A state is the player (position, fall speed, footing, jump lock), the
 * keys, every tile's kind and collapse timer and every guardian's position
 * and direction; each is expanded once (128-bit hash), and a state that
 * loses a life is dropped. Prints the shortest route that clears the
 * cavern as a routes.json entry ([input flags, ticks] runs).
 *
 *   tools/route_check.sh search <cavern 1-6>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>
extern "C" {
#include "game.h"
#include "levels.h"
}

struct H { unsigned long long a, b; bool operator==(const H &o) const { return a == o.a && b == o.b; } };
struct HH { size_t operator()(const H &h) const { return (size_t)(h.a ^ (h.b * 0x9e3779b97f4a7c15ULL)); } };
static H hash2(const std::string &k)
{
    unsigned long long a = 1469598103934665603ULL, b = 0x84222325cbf29ce4ULL;
    for (size_t i = 0; i < k.size(); i++) {
        unsigned char c = (unsigned char)k[i];
        a = (a ^ c) * 1099511628211ULL;
        b = (b ^ c) * 0x100000001b3ULL; b ^= b >> 29;
    }
    H h = { a, b };
    return h;
}
struct Step { int parent; unsigned char in; };

int main(int argc, char **argv)
{
    static const UBYTE inputs[] = { 0, INPUT_LEFT, INPUT_RIGHT, INPUT_JUMP,
                                    INPUT_LEFT | INPUT_JUMP, INPUT_RIGHT | INPUT_JUMP };
    LONG lv = argc > 1 ? atol(argv[1]) - 1 : 0;
    static GameState work;
    game_init(&work);
    work.mode = GM_PLAYING;
    game_load_level(lv);
    std::vector<GameState> cur(1, work), next;
    std::vector<std::vector<Step> > hist;
    std::unordered_set<H, HH> seen;
    for (int depth = 1; depth <= 6000 && !cur.empty(); depth++) {
        std::vector<Step> steps;
        next.clear();
        for (size_t ci = 0; ci < cur.size(); ci++)
            for (size_t ii = 0; ii < sizeof(inputs); ii++) {
                UBYTE in = inputs[ii];
                work = cur[ci];
                game_tick(in);
                if (work.lives < cur[ci].lives || work.mode == GM_LOSE) continue;
                if (work.mode == GM_LEVELWIN || work.mode == GM_WIN) {
                    std::vector<UBYTE> path(1, in);
                    for (int d = (int)hist.size() - 1, p = (int)ci; d >= 0; d--) {
                        path.push_back(hist[d][p].in); p = hist[d][p].parent;
                    }
                    std::reverse(path.begin(), path.end());
                    fprintf(stderr, "cavern %ld cleared in %d ticks\n", (long)lv + 1, depth);
                    printf("[");
                    for (size_t i = 0; i < path.size();) {
                        size_t j = i;
                        while (j < path.size() && path[j] == path[i]) j++;
                        printf("%s[%d, %d]", i ? ", " : "", path[i], (int)(j - i));
                        i = j;
                    }
                    printf("]\n");
                    return 0;
                }
                const Player &p = work.player;
                LONG m[6] = { p.x, p.y, p.vy, p.on_ground, p.jump_lock, work.keys_remaining };
                std::string k((const char *)m, sizeof(m));
                for (int r = 0; r < GRID_H; r++)
                    for (int c = 0; c < GRID_W; c++) {
                        k.push_back((char)work.grid[r][c].kind);
                        k.push_back((char)work.grid[r][c].timer);
                    }
                for (int g = 0; g < work.num_guards; g++) {
                    LONG gg[4] = { work.guards[g].x, work.guards[g].y, work.guards[g].dx, work.guards[g].dy };
                    k.append((const char *)gg, sizeof(gg));
                }
                if (!seen.insert(hash2(k)).second) continue;
                Step s = { (int)ci, in };
                steps.push_back(s);
                next.push_back(work);
            }
        hist.push_back(steps);
        cur.swap(next);
    }
    fprintf(stderr, "cavern %ld: no route found\n", (long)lv + 1);
    return 1;
}
