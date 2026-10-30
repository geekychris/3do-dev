// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * mt_core.c - Marble Tilt's rules: the levels, the tilt, the marble rolling
 * and bouncing, bumpers, gates, sand, holes and checkpoints, the clock,
 * and the autopilot (a route found across the grid, followed by tilting
 * to roll at a safe speed). Fixed point throughout, so the host, the
 * classic Amiga and AmigaOS 4 roll a level the same way.
 */
#include <string.h>
#include "a3d.h"
#include "mt.h"

const char *const mt_event_names[MTE_COUNT] = {
    "NONE", "WALL", "BUMP", "FALL", "RESPAWN", "CHECKPOINT", "GATE", "EXIT", "LEVEL", "TIMEOUT", "OVER", "WIN"
};
const char *const mt_level_names[MT_LEVELS] = {
    "FIRST ROLL", "THE MOAT", "ZIGZAG", "GATEHOUSE", "PINBALL", "SAND TRAP", "TIGHTROPE", "THE GAUNTLET"
};
const char *const mt_levels[MT_LEVELS][MT_N] = {
    { "############",
      "#S....#....#",
      "#.##..#.##.#",
      "#.##....##.#",
      "#..........#",
      "####.##.####",
      "#....##....#",
      "#.##....##.#",
      "#.##.##.##.#",
      "#....##...E#",
      "#....##....#",
      "############" },
    { "############",
      "#S.........#",
      "#.        .#",
      "#. ...... .#",
      "#. .####. .#",
      "#. .#E .. .#",
      "#. .#.##. .#",
      "#. ...... .#",
      "#.   .    .#",
      "#..........#",
      "#..........#",
      "############" },
    { "############",
      "#S.....    #",
      "#####..    #",
      "#   ...    #",
      "#   ..######",
      "#   .....  #",
      "#####...   #",
      "#    ...   #",
      "#    ..#####",
      "#     ....E#",
      "#      ... #",
      "############" },
    { "############",
      "#S...#.....#",
      "#....G.....#",
      "#....#.....#",
      "###.####G###",
      "#..........#",
      "#.  ....  .#",
      "#.  .##.  .#",
      "#.  .##.  .#",
      "#....GG...E#",
      "#..........#",
      "############" },
    { "############",
      "#S.........#",
      "#..B...B...#",
      "#..........#",
      "#....B.....#",
      "#.B......B.#",
      "#....  ....#",
      "#...    ...#",
      "#.B..  ..B.#",
      "#..........#",
      "#....B....E#",
      "############" },
    { "############",
      "#S.ZZZZ....#",
      "#..ZZZZ.##.#",
      "#.......#  #",
      "####.####  #",
      "#....ZZZ...#",
      "#.##.ZZZ.#.#",
      "#.#  ...  ##",
      "#.#  .#.   #",
      "#....#E.   #",
      "#....#..   #",
      "############" },
    { "############",
      "#S.        #",
      "# .  .....E#",
      "# .  .     #",
      "# .  .  ...#",
      "# ...C  .  #",
      "#       .  #",
      "#  ......  #",
      "#  .       #",
      "#  ......  #",
      "#       .  #",
      "############" },
    { "############",
      "#S..B.  ...#",
      "#.....  .G.#",
      "###.##  .#.#",
      "#  .....C#.#",
      "#  .B.  ##.#",
      "#  ...  ...#",
      "#####.###G##",
      "#.....B....#",
      "#.  ....  .#",
      "#E  .  .  .#",
      "############" },
};

#define G_TILT     FX(1.0)              /* units a tick^2 at a radian of tilt */
#define TILT_RATE  (MT_TILT_MAX / 5)
#define VMAX       FX(5)
#define BUMP_KICK  FX(2.5)
#define BUMPER_R   13

static void event(MTGame *g, int type, int a, int b)
{
    if (g->nev < MT_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}

int mt_pop_event(MTGame *g, MTEvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (g->nev - 1) * sizeof(MTEvent));
    g->nev--;
    return 1;
}

static fix fabs_(fix a) { return a < 0 ? -a : a; }

fix mt_cell_x(int col) { return FROMI((col - MT_N / 2) * MT_CELL + MT_CELL / 2); }
fix mt_cell_z(int row) { return FROMI((MT_N / 2 - row) * MT_CELL - MT_CELL / 2); }
int mt_col_of(fix x) { int32_t v = (x >> 16) + MT_N / 2 * MT_CELL; return v < 0 ? -1 : (int)(v / MT_CELL); }
int mt_row_of(fix z) { int32_t v = MT_N / 2 * MT_CELL - (z >> 16); return v < 0 ? -1 : (int)(v / MT_CELL); }

char mt_at(const MTGame *g, int row, int col)
{
    if (row < 0 || col < 0 || row >= MT_N || col >= MT_N) return '#';
    return g->cell[row][col];
}

int mt_solid(const MTGame *g, int row, int col)
{
    char c = mt_at(g, row, col);
    return c == '#' || (c == 'G' && !g->gates_open);
}

static void put_at(MTGame *g, int row, int col)
{
    g->x = mt_cell_x(col);
    g->z = mt_cell_z(row);
    g->vx = g->vz = 0;
}

void mt_load_level(MTGame *g, int level)
{
    int r, c;
    g->level = level;
    for (r = 0; r < MT_N; r++) {
        memcpy(g->cell[r], mt_levels[level - 1][r], MT_N);
        g->cell[r][MT_N] = 0;
        for (c = 0; c < MT_N; c++)
            if (g->cell[r][c] == 'S') { g->cp_row = r; g->cp_col = c; }
    }
    put_at(g, g->cp_row, g->cp_col);
    g->roll = g->pitch = 0;
    g->falling = 0;
    g->gates_open = 1;
    g->time_left = (50 + level * 10) * 25;
    g->level_ticks = 0;
}

void mt_new_game(MTGame *g, uint32_t seed, int level)
{
    memset(g, 0, sizeof(*g));
    g->seed = seed;
    if (level < 1) level = 1;
    if (level > MT_LEVELS) level = MT_LEVELS;
    mt_load_level(g, level);
}

static fix ease(fix v, fix target)
{
    if (v < target) { v += TILT_RATE; if (v > target) v = target; }
    else if (v > target) { v -= TILT_RATE; if (v < target) v = target; }
    return v;
}

/* the marble against the walls round it, and the bumpers */
static void collide(MTGame *g)
{
    int r0 = mt_row_of(g->z), c0 = mt_col_of(g->x), r, c;
    fix rad = FROMI(MT_R);
    for (r = r0 - 1; r <= r0 + 1; r++)
        for (c = c0 - 1; c <= c0 + 1; c++) {
            char ch = mt_at(g, r, c);
            fix cx = mt_cell_x(c), cz = mt_cell_z(r), half = FROMI(MT_CELL / 2);
            if (ch == 'B') {
                fix dx = g->x - cx, dz = g->z - cz, d = fhypot(dx, dz), reach = FROMI(BUMPER_R + MT_R), vn;
                if (d >= reach) continue;
                if (d < FONE) { dx = FONE; d = FONE; }
                g->x = cx + FDIV(FMUL(dx, reach), d);
                g->z = cz + FDIV(FMUL(dz, reach), d);
                vn = FDIV(FMUL(g->vx, dx) + FMUL(g->vz, dz), d);
                if (vn < 0) { g->vx -= FDIV(FMUL(2 * vn, dx), d); g->vz -= FDIV(FMUL(2 * vn, dz), d); }
                g->vx += FDIV(FMUL(BUMP_KICK, dx), d);
                g->vz += FDIV(FMUL(BUMP_KICK, dz), d);
                g->bumps++;
                event(g, MTE_BUMP, r, c);
            } else if (mt_solid(g, r, c)) {
                /* the nearest point of the cell's square */
                fix nx = g->x < cx - half ? cx - half : g->x > cx + half ? cx + half : g->x;
                fix nz = g->z < cz - half ? cz - half : g->z > cz + half ? cz + half : g->z;
                fix dx = g->x - nx, dz = g->z - nz, d;
                if (fabs_(dx) >= rad || fabs_(dz) >= rad) continue;
                d = fhypot(dx, dz);
                if (d >= rad) continue;
                if (d < 256) {                  /* the middle inside the wall: out the way it came */
                    if (fabs_(g->vx) > fabs_(g->vz)) { dx = g->vx > 0 ? -FONE : FONE; dz = 0; }
                    else { dz = g->vz > 0 ? -FONE : FONE; dx = 0; }
                    d = FONE;
                }
                g->x += FDIV(FMUL(dx, rad - d), d);
                g->z += FDIV(FMUL(dz, rad - d), d);
                {
                    fix vn = FDIV(FMUL(g->vx, dx) + FMUL(g->vz, dz), d);
                    if (vn < 0) {
                        /* bounce back at 40% */
                        g->vx -= FDIV(FMUL(FMUL(vn, FX(1.4)), dx), d);
                        g->vz -= FDIV(FMUL(FMUL(vn, FX(1.4)), dz), d);
                        if (vn < -FX(1.5)) event(g, MTE_WALL, (int)(-vn >> 12), 0);
                    }
                }
            }
        }
}

void mt_tick(MTGame *g, unsigned in)
{
    int r, c, k;
    char ch;
    if (g->over || g->won) return;
    g->tick++;
    if (g->between > 0) {
        if (--g->between == 0) {
            if (g->level >= MT_LEVELS) { g->won = 1; event(g, MTE_WIN, (int)g->score, 0); return; }
            mt_load_level(g, g->level + 1);
            event(g, MTE_LEVEL, g->level, 0);
        }
        return;
    }
    g->level_ticks++;
    /* the gates */
    {
        int open = (g->level_ticks / MT_GATE_T) % 2 == 0;
        if (!open && mt_at(g, mt_row_of(g->z), mt_col_of(g->x)) == 'G') open = 1;   /* not on the marble */
        if (open != g->gates_open) { g->gates_open = open; event(g, MTE_GATE, open, 0); }
    }
    /* the tilt */
    g->roll = ease(g->roll, (in & MT_IN_RIGHT) ? MT_TILT_MAX : (in & MT_IN_LEFT) ? -MT_TILT_MAX : 0);
    g->pitch = ease(g->pitch, (in & MT_IN_UP) ? MT_TILT_MAX : (in & MT_IN_DOWN) ? -MT_TILT_MAX : 0);

    if (g->falling) {
        if (--g->falling == 0) {
            put_at(g, g->cp_row, g->cp_col);
            event(g, MTE_RESPAWN, g->cp_row, g->cp_col);
        }
    } else {
        fix ax = FMUL(G_TILT, fsin(g->roll)), az = FMUL(G_TILT, fsin(g->pitch)), sp;
        r = mt_row_of(g->z);
        c = mt_col_of(g->x);
        ch = mt_at(g, r, c);
        g->vx += ax;
        g->vz += az;
        /* rolling friction; sand drags */
        g->vx -= ch == 'Z' ? g->vx / 8 : g->vx / 100;
        g->vz -= ch == 'Z' ? g->vz / 8 : g->vz / 100;
        sp = fhypot(g->vx, g->vz);
        if (sp > VMAX) { g->vx = FDIV(FMUL(g->vx, VMAX), sp); g->vz = FDIV(FMUL(g->vz, VMAX), sp); }
        for (k = 0; k < 4; k++) {
            g->x += g->vx / 4;
            g->z += g->vz / 4;
            collide(g);
        }
        r = mt_row_of(g->z);
        c = mt_col_of(g->x);
        ch = mt_at(g, r, c);
        if (ch == ' ') {
            g->falling = MT_FALL_T;
            g->falls++;
            g->time_left -= 3 * 25;
            event(g, MTE_FALL, r, c);
        } else if (ch == 'C' && (g->cp_row != r || g->cp_col != c)) {
            g->cp_row = r;
            g->cp_col = c;
            event(g, MTE_CHECKPOINT, r, c);
        } else if (ch == 'E') {
            int bonus = (int)(g->time_left / 25) * 10 + 100 * g->level;
            g->score += bonus;
            g->between = MT_BETWEEN;
            event(g, MTE_EXIT, g->level, bonus);
            return;
        }
    }
    if (--g->time_left <= 0) {
        g->time_left = 0;
        g->over = 1;
        event(g, MTE_TIMEOUT, 0, 0);
        event(g, MTE_OVER, (int)g->score, 0);
    }
}

/* ---------- the autopilot ---------- */

/* the next cell on the shortest route from (row, col) to the exit,
 * through floor, sand, checkpoints and gates (open or not), round walls,
 * bumpers and holes; *steps: how far. Returns row * MT_N + col, or -1. */
int mt_path(const MTGame *g, int row, int col, int *steps)
{
    int16_t dist[MT_N * MT_N];
    int16_t queue[MT_N * MT_N];
    int head = 0, tail = 0, k, best = -1, er = -1, ec = -1;
    static const int8_t d4[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (k = 0; k < MT_N * MT_N; k++) {
        dist[k] = -1;
        if (g->cell[k / MT_N][k % MT_N] == 'E') { er = k / MT_N; ec = k % MT_N; }
    }
    if (er < 0) return -1;
    /* from the exit back */
    dist[er * MT_N + ec] = 0;
    queue[tail++] = (int16_t)(er * MT_N + ec);
    while (head < tail) {
        int cur = queue[head++], r = cur / MT_N, c = cur % MT_N;
        for (k = 0; k < 4; k++) {
            int nr = r + d4[k][0], nc = c + d4[k][1], n;
            char ch = mt_at(g, nr, nc);
            if (ch == '#' || ch == ' ' || ch == 'B') continue;
            n = nr * MT_N + nc;
            if (dist[n] >= 0) continue;
            dist[n] = dist[cur] + 1;
            queue[tail++] = (int16_t)n;
        }
    }
    if (row < 0 || col < 0 || row >= MT_N || col >= MT_N || dist[row * MT_N + col] < 0) return -1;
    if (steps) *steps = dist[row * MT_N + col];
    if (dist[row * MT_N + col] == 0) return row * MT_N + col;
    for (k = 0; k < 4; k++) {
        int nr = row + d4[k][0], nc = col + d4[k][1];
        if (nr < 0 || nc < 0 || nr >= MT_N || nc >= MT_N) continue;
        if (dist[nr * MT_N + nc] == dist[row * MT_N + col] - 1) { best = nr * MT_N + nc; break; }
    }
    return best;
}

unsigned mt_bot(const MTGame *g)
{
    int r, c, next, nr, nc, steps = 0;
    fix tx, tz, dx, dz, d, vdx, vdz, cap, wx, wz;
    unsigned in = 0;
    if (g->over || g->won || g->between || g->falling) return 0;
    r = mt_row_of(g->z);
    c = mt_col_of(g->x);
    next = mt_path(g, r, c, &steps);
    if (next < 0) return 0;
    nr = next / MT_N;
    nc = next % MT_N;
    tx = mt_cell_x(nc);
    tz = mt_cell_z(nr);
    /* the cell after next, if the route goes straight on: aim through */
    {
        int s2, n2 = mt_path(g, nr, nc, &s2);
        if (n2 >= 0 && n2 != next && ((n2 / MT_N - nr) == (nr - r)) && ((n2 % MT_N - nc) == (nc - c))) {
            tx = mt_cell_x(n2 % MT_N);
            tz = mt_cell_z(n2 / MT_N);
        }
    }
    /* a gate ahead that is shut: wait short of it */
    if (mt_at(g, nr, nc) == 'G' && !g->gates_open) { tx = mt_cell_x(c); tz = mt_cell_z(r); }
    dx = tx - g->x;
    dz = tz - g->z;
    d = fhypot(dx, dz);
    /* slow beside holes */
    cap = FX(1.6);
    {
        int k;
        static const int8_t d8[8][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { -1, -1 }, { -1, 1 }, { 1, -1 }, { 1, 1 } };
        for (k = 0; k < 8; k++) if (mt_at(g, r + d8[k][0], c + d8[k][1]) == ' ') { cap = FX(0.9); break; }
    }
    if (d > FX(0.5)) {
        fix sp = d / 12 < cap ? d / 12 : cap;
        vdx = FDIV(FMUL(dx, sp), d);
        vdz = FDIV(FMUL(dz, sp), d);
    } else vdx = vdz = 0;
    /* tilt to bring the speed to that */
    wx = vdx - g->vx;
    wz = vdz - g->vz;
    if (wx > FX(0.08)) in |= MT_IN_RIGHT;
    else if (wx < -FX(0.08)) in |= MT_IN_LEFT;
    if (wz > FX(0.08)) in |= MT_IN_UP;
    else if (wz < -FX(0.08)) in |= MT_IN_DOWN;
    return in;
}

uint32_t mt_state_hash(const MTGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    int32_t v[18];
    v[0] = g->score; v[1] = g->level; v[2] = g->x; v[3] = g->z; v[4] = g->vx; v[5] = g->vz; v[6] = g->roll; v[7] = g->pitch; v[8] = g->falling; v[9] = g->falls; v[10] = g->bumps; v[11] = g->time_left; v[12] = g->tick; v[13] = g->cp_row; v[14] = g->cp_col; v[15] = g->over; v[16] = g->won; v[17] = g->gates_open;
    for (i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) { h ^= (uint32_t)v[i]; h *= 16777619UL; }
    return h;
}
