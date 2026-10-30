// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * bb_core.c - Block Breaker 3D's rules: the ball, the bricks, the paddle,
 * the capsules and the levels. Integer only (1/256 units), so the host,
 * the classic Amiga and AmigaOS 4 play a seed the same way.
 *
 * The ball moves a quarter of a tick at a time and one axis at a time:
 * if a move along an axis takes it into a brick, the move is undone and
 * that axis' speed turned round, which gives clean bounces off faces.
 */
#include <string.h>
#include "a3d.h"
#include "bb.h"

const char *const bb_event_names[BBE_COUNT] = {
    "NONE", "WALL", "PADDLE", "HIT", "BREAK", "STEEL", "LOST", "SERVE",
    "CAPSULE", "CAP_MISS", "CLEAR", "LEVEL", "OVER", "WIN"
};
const char *const bb_cap_names[CAP_COUNT] = { "WIDE", "SLOW", "LIFE", "PLOUGH" };

/* the levels: 3 layers (nearest first) of 4 rows (top first) of 6.
 * r o y g c b: bricks, T: tough, S: steel, *: a capsule inside */
const char *const bb_level_names[BB_LEVELS] = {
    "FIRST LIGHT", "CHEQUERS", "THE WINDOW", "PILLARS", "STAIRWAY", "THE VAULT", "CROSSFIRE", "THE CORE"
};
static const char *const levels[BB_LEVELS][12] = {
    { "......", "......", "......", "......",
      ".o..o.", "..**..", "..yy..", ".g..g.",
      "rrrrrr", "oooooo", "yyyyyy", "gggggg" },
    { "r.r.r.", ".o.o.o", "y.y.y.", ".g.g.g",
      ".c.c.c", "b.b.b.", ".c*c.c", "b.b.b.",
      "TTTTTT", "cccccc", "bbbbbb", "TTTTTT" },
    { "SS..SS", "S....S", "S....S", "SS..SS",
      "......", ".*yy*.", ".yyyy.", "......",
      "rrrrrr", "rTTTTr", "rTTTTr", "rrrrrr" },
    { "g....g", "g....g", "g....g", "g....g",
      "..TT..", "..TT..", "..**..", "..TT..",
      "cbcbcb", "bcbcbc", "cbcbcb", "bcbcbc" },
    { "......", "......", "......", "oooooo",
      "......", "......", "yyyyyy", "TT**TT",
      "......", "gggggg", "cccccc", "bbbbbb" },
    { "S.SS.S", "......", "......", "S.SS.S",
      "TTTTTT", "T*..*T", "T....T", "TTTTTT",
      "yyyyyy", "yrrrry", "yrrrry", "yyyyyy" },
    { "..rr..", "......", "......", "..rr..",
      "o....o", ".T**T.", ".T..T.", "o....o",
      "SbbbbS", "bcTTcb", "bcTTcb", "SbbbbS" },
    { "TTTTTT", "T....T", "T....T", "TTTTTT",
      "S.SS.S", ".*..*.", ".*..*.", "S.SS.S",
      "rrrrrr", "ooTToo", "yyTTyy", "gggggg" },
};

static void event(BBGame *g, int type, int a, int b)
{
    if (g->nev < BB_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}

int bb_pop_event(BBGame *g, BBEvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (g->nev - 1) * sizeof(BBEvent));
    g->nev--;
    return 1;
}

/* mulberry32 */
uint32_t bb_rand(BBGame *g)
{
    uint32_t t = (g->rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

int bb_breakable(int type) { return type != BK_EMPTY && type != BK_STEEL; }

int bb_cell_box(int i, int32_t *x0, int32_t *y0, int32_t *z0)
{
    int c = i % BB_COLS, r = (i / BB_COLS) % BB_ROWS, l = i / (BB_COLS * BB_ROWS);
    *x0 = -BB_W + c * BB_CW;
    *y0 = -BB_H + r * BB_CH;
    *z0 = BB_Z0 + l * BB_CD;
    return l;
}

int bb_pad_w(const BBGame *g) { return g->wide_t ? BB_WIDE_W : BB_PAD_W; }
int bb_pad_h(const BBGame *g) { return g->wide_t ? BB_WIDE_H : BB_PAD_H; }

static int32_t cur_speed(const BBGame *g) { return g->slow_t ? g->speed * 3 / 4 : g->speed; }

static void set_speed(BBGame *g)
{
    int32_t s = (12 + g->level) * BB_FX + (g->paddle_hits / 10) * (BB_FX / 2);
    if (s > 24 * BB_FX) s = 24 * BB_FX;
    g->speed = s;
}

/* the ball's speed back to cur_speed, keeping its direction across the
 * tunnel within limits so it always makes progress along it */
static void renorm(BBGame *g)
{
    int32_t s = cur_speed(g), lim = s * 3 / 5, rest;
    int sign = g->vz < 0 ? -1 : 1;
    if (g->vx > lim) g->vx = lim;
    if (g->vx < -lim) g->vx = -lim;
    if (g->vy > lim) g->vy = lim;
    if (g->vy < -lim) g->vy = -lim;
    rest = s * s - g->vx * g->vx - g->vy * g->vy;
    g->vz = sign * (int32_t)a3_isqrt((uint32_t)(rest > 0 ? rest : 1));
}

void bb_load_level(BBGame *g, int level)
{
    int l, r, c;
    g->level = level;
    g->left = 0;
    for (l = 0; l < BB_LAYERS; l++)
        for (r = 0; r < BB_ROWS; r++)
            for (c = 0; c < BB_COLS; c++) {
                int i = bb_index(c, BB_ROWS - 1 - r, l), t;
                switch (levels[level - 1][l * BB_ROWS + r][c]) {
                case 'r': t = BK_RED; break;
                case 'o': t = BK_ORANGE; break;
                case 'y': t = BK_YELLOW; break;
                case 'g': t = BK_GREEN; break;
                case 'c': t = BK_CYAN; break;
                case 'b': t = BK_BLUE; break;
                case 'T': t = BK_TOUGH; break;
                case 'S': t = BK_STEEL; break;
                case '*': t = BK_BONUS; break;
                default: t = BK_EMPTY; break;
                }
                g->cell[i] = (uint8_t)t;
                g->hits[i] = (uint8_t)(t == BK_TOUGH ? (level >= 5 ? 3 : 2) : t == BK_EMPTY ? 0 : 1);
                if (bb_breakable(t)) g->left++;
            }
    memset(g->cap, 0, sizeof(g->cap));
    g->wide_t = g->slow_t = g->plough_t = 0;
    g->paddle_hits = 0;
    g->stuck = 1;
    g->px = g->py = 0;
    set_speed(g);
}

void bb_new_game(BBGame *g, uint32_t seed, int level)
{
    memset(g, 0, sizeof(*g));
    g->seed = g->rng = seed;
    g->lives = BB_LIVES;
    if (level < 1) level = 1;
    if (level > BB_LEVELS) level = BB_LEVELS;
    bb_load_level(g, level);
    g->bx = g->px;
    g->by = g->py;
    g->bz = (BB_R + 1) * BB_FX;
}

static void drop_capsule(BBGame *g, int i)
{
    int k;
    int32_t x0, y0, z0;
    uint32_t r = bb_rand(g) % 8;
    for (k = 0; k < BB_MAX_CAPS; k++) if (!g->cap[k].active) break;
    if (k == BB_MAX_CAPS) return;
    bb_cell_box(i, &x0, &y0, &z0);
    g->cap[k].active = 1;
    g->cap[k].type = r < 3 ? CAP_WIDE : r < 5 ? CAP_SLOW : r < 6 ? CAP_LIFE : CAP_PLOUGH;
    g->cap[k].x = (x0 + BB_CW / 2) * BB_FX;
    g->cap[k].y = (y0 + BB_CH / 2) * BB_FX;
    g->cap[k].z = z0 * BB_FX;
    g->caps_dropped++;
}

static void break_brick(BBGame *g, int i)
{
    int t = g->cell[i], l = i / (BB_COLS * BB_ROWS);
    g->cell[i] = BK_EMPTY;
    g->hits[i] = 0;
    g->left--;
    g->broken++;
    g->score += (t == BK_TOUGH ? 30 : t == BK_BONUS ? 50 : 10) + l * 5;
    event(g, BBE_BREAK, i, t);
    if (t == BK_BONUS) drop_capsule(g, i);
}

/* a hit on brick i; 1 if the ball bounces off it */
static int hit_brick(BBGame *g, int i)
{
    int t = g->cell[i];
    if (t == BK_STEEL) { event(g, BBE_STEEL, i, t); return 1; }
    if (g->plough_t) { break_brick(g, i); return 0; }
    if (--g->hits[i] == 0) break_brick(g, i);
    else { g->score += 5; event(g, BBE_HIT, i, g->hits[i]); }
    return 1;
}

/* the occupied cells the ball's box at (x, y, z) overlaps; returns how many */
static int overlap(const BBGame *g, int32_t x, int32_t y, int32_t z, int *out)
{
    int32_t r = BB_R * BB_FX, zl = z - r - BB_Z0 * BB_FX;
    int c0, c1, r0, r1, l0, l1, c, rr, l, n = 0;
    if (z + r <= BB_Z0 * BB_FX) return 0;
    c0 = (x - r + BB_W * BB_FX) / (BB_CW * BB_FX);
    c1 = (x + r - 1 + BB_W * BB_FX) / (BB_CW * BB_FX);
    r0 = (y - r + BB_H * BB_FX) / (BB_CH * BB_FX);
    r1 = (y + r - 1 + BB_H * BB_FX) / (BB_CH * BB_FX);
    l0 = zl < 0 ? 0 : zl / (BB_CD * BB_FX);
    l1 = (z + r - 1 - BB_Z0 * BB_FX) / (BB_CD * BB_FX);
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= BB_COLS) c1 = BB_COLS - 1;
    if (r1 >= BB_ROWS) r1 = BB_ROWS - 1;
    if (l1 >= BB_LAYERS) l1 = BB_LAYERS - 1;
    for (l = l0; l <= l1; l++)
        for (rr = r0; rr <= r1; rr++)
            for (c = c0; c <= c1; c++) {
                int i = bb_index(c, rr, l);
                if (g->cell[i] != BK_EMPTY) out[n++] = i;
            }
    return n;
}

static void paddle_bounce(BBGame *g)
{
    int32_t s = cur_speed(g), pw = bb_pad_w(g) * BB_FX, ph = bb_pad_h(g) * BB_FX;
    g->bz = 2 * BB_R * BB_FX - g->bz;
    g->vz = -g->vz;
    g->vx += (g->bx - g->px) * (s / 2) / pw;
    g->vy += (g->by - g->py) * (s / 2) / ph;
    g->paddle_hits++;
    set_speed(g);
    renorm(g);
    event(g, BBE_PADDLE, (int)((g->bx - g->px) / BB_FX), (int)((g->by - g->py) / BB_FX));
}

static void move_axis(BBGame *g, int axis, int32_t d)
{
    int32_t *p = axis == 0 ? &g->bx : axis == 1 ? &g->by : &g->bz;
    int32_t *v = axis == 0 ? &g->vx : axis == 1 ? &g->vy : &g->vz;
    int32_t before = *p, lim;
    int cells[8], n, k, solid = 0;

    *p += d;
    lim = (axis == 0 ? BB_W - BB_R : axis == 1 ? BB_H - BB_R : BB_D - BB_R) * BB_FX;
    if (*p > lim) { *p = 2 * lim - *p; *v = -*v; event(g, BBE_WALL, axis, 1); }
    else if (axis < 2 && *p < -lim) { *p = -2 * lim - *p; *v = -*v; event(g, BBE_WALL, axis, -1); }
    n = overlap(g, g->bx, g->by, g->bz, cells);
    for (k = 0; k < n; k++) solid |= hit_brick(g, cells[k]);
    if (solid) { *p = before; *v = -*v; }
    /* the paddle, crossing its plane on the way back */
    if (axis == 2 && *v < 0 && before > BB_R * BB_FX && *p <= BB_R * BB_FX) {
        int32_t dx = g->bx - g->px, dy = g->by - g->py;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx <= (bb_pad_w(g) + BB_R) * BB_FX && dy <= (bb_pad_h(g) + BB_R) * BB_FX) paddle_bounce(g);
    }
}

static void serve(BBGame *g, int dx, int dy)
{
    int32_t s;
    g->stuck = 0;
    s = cur_speed(g);
    g->vx = dx * s / 4 + (int32_t)(bb_rand(g) % 61) * s / 300 - s / 10;
    g->vy = dy * s / 4 + (int32_t)(bb_rand(g) % 61) * s / 300 - s / 10;
    g->vz = 1;
    renorm(g);
    event(g, BBE_SERVE, 0, 0);
}

static void lose_ball(BBGame *g)
{
    g->lives--;
    event(g, BBE_LOST, g->lives, 0);
    g->wide_t = g->slow_t = g->plough_t = 0;
    if (g->lives <= 0) { g->over = 1; event(g, BBE_OVER, (int)g->score, 0); return; }
    g->stuck = 1;
    set_speed(g);
}

static void capsules(BBGame *g)
{
    int k;
    for (k = 0; k < BB_MAX_CAPS; k++) {
        Capsule *c = &g->cap[k];
        int32_t before;
        if (!c->active) continue;
        before = c->z;
        c->z -= 7 * BB_FX;
        if (before > 0 && c->z <= 0) {
            int32_t dx = c->x - g->px, dy = c->y - g->py;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx <= (bb_pad_w(g) + 14) * BB_FX && dy <= (bb_pad_h(g) + 10) * BB_FX) {
                c->active = 0;
                g->score += 100;
                switch (c->type) {
                case CAP_WIDE: g->wide_t = BB_POWER_TICKS; break;
                case CAP_SLOW: g->slow_t = BB_POWER_TICKS; if (!g->stuck) renorm(g); break;
                case CAP_LIFE: if (g->lives < 9) g->lives++; break;
                case CAP_PLOUGH: g->plough_t = BB_POWER_TICKS / 2; break;
                }
                event(g, BBE_CAPSULE, c->type, 0);
                continue;
            }
        }
        if (c->z < BB_LOST_Z * BB_FX) { c->active = 0; event(g, BBE_CAP_MISS, c->type, 0); }
    }
}

void bb_tick(BBGame *g, unsigned in)
{
    int dx = (in & BB_IN_RIGHT ? 1 : 0) - (in & BB_IN_LEFT ? 1 : 0);
    int dy = (in & BB_IN_UP ? 1 : 0) - (in & BB_IN_DOWN ? 1 : 0);
    int32_t lim;
    int k;

    if (g->over || g->won) return;
    g->tick++;
    if (g->between > 0) {
        if (--g->between == 0) {
            if (g->level >= BB_LEVELS) { g->won = 1; event(g, BBE_WIN, (int)g->score, 0); return; }
            bb_load_level(g, g->level + 1);
            event(g, BBE_LEVEL, g->level, 0);
        }
        return;
    }
    /* the paddle */
    g->px += dx * BB_PAD_SPEED * BB_FX;
    g->py += dy * BB_PAD_SPEED * BB_FX;
    lim = (BB_W - bb_pad_w(g)) * BB_FX;
    if (g->px > lim) g->px = lim;
    if (g->px < -lim) g->px = -lim;
    lim = (BB_H - bb_pad_h(g)) * BB_FX;
    if (g->py > lim) g->py = lim;
    if (g->py < -lim) g->py = -lim;
    if (g->wide_t) g->wide_t--;
    if (g->plough_t) g->plough_t--;
    if (g->slow_t && --g->slow_t == 0 && !g->stuck) renorm(g);

    if (g->stuck) {
        g->bx = g->px;
        g->by = g->py;
        g->bz = (BB_R + 1) * BB_FX;
        if (in & BB_IN_FIRE) serve(g, dx, dy);
    } else {
        for (k = 0; k < 4; k++) {
            move_axis(g, 0, g->vx / 4);
            move_axis(g, 1, g->vy / 4);
            move_axis(g, 2, g->vz / 4);
        }
        if (g->bz < BB_LOST_Z * BB_FX) lose_ball(g);
    }
    capsules(g);
    if (g->left == 0 && !g->over) {
        g->score += 250 * g->level;
        g->between = BB_BETWEEN;
        g->stuck = 1;
        memset(g->cap, 0, sizeof(g->cap));
        event(g, BBE_CLEAR, g->level, 0);
    }
}

void bb_clear_all(BBGame *g)
{
    int i;
    for (i = 0; i < BB_CELLS; i++) if (bb_breakable(g->cell[i])) break_brick(g, i);
}

/* ---------- the autopilot ---------- */

/* p reflected back and forth within -lim..lim */
static int32_t fold(int32_t p, int32_t lim)
{
    int32_t period = 4 * lim, q = (p + lim) % period;
    if (q < 0) q += period;
    if (q > 2 * lim) q = period - q;
    return q - lim;
}

static uint32_t mix(uint32_t x)
{
    x ^= x >> 16; x *= 0x7FEB352DUL; x ^= x >> 15; x *= 0x846CA68BUL; x ^= x >> 16;
    return x;
}

unsigned bb_bot(const BBGame *g)
{
    int32_t tx, ty, pw = bb_pad_w(g) * BB_FX, ph = bb_pad_h(g) * BB_FX, dead = BB_PAD_SPEED * BB_FX / 2;
    int32_t limx = (BB_W - BB_R) * BB_FX, limy = (BB_H - BB_R) * BB_FX;
    unsigned in = 0;
    int k;

    if (g->over || g->won || g->between) return 0;
    if (g->stuck) {
        uint32_t h = mix(g->seed * 31 + (uint32_t)g->lives * 7 + (uint32_t)g->level * 131 + (uint32_t)g->broken);
        tx = ((int32_t)(h % 201) - 100) * (BB_W - BB_PAD_W) * BB_FX / 100;
        ty = ((int32_t)((h >> 8) % 201) - 100) * (BB_H - BB_PAD_H) * BB_FX / 100;
        if (g->px - tx < dead && tx - g->px < dead && g->py - ty < dead && ty - g->py < dead) return BB_IN_FIRE;
    } else {
        int32_t t, off_x, off_y;
        uint32_t h = mix(g->seed + (uint32_t)g->paddle_hits * 2654435761UL);
        if (g->vz < 0) t = (g->bz - BB_R * BB_FX) / -g->vz;
        else t = ((BB_D - BB_R) * BB_FX - g->bz + (BB_D - 2 * BB_R) * BB_FX) / g->vz;
        tx = fold(g->bx + g->vx * t, limx);
        ty = fold(g->by + g->vy * t, limy);
        /* where on the paddle to take it: varied, so the ball goes everywhere */
        off_x = ((int32_t)(h % 121) - 60) * pw / 100;
        off_y = ((int32_t)((h >> 10) % 121) - 60) * ph / 100;
        tx -= off_x;
        ty -= off_y;
        /* a capsule about to arrive, while the ball is far: fetch it */
        if (g->bz > 350 * BB_FX && g->vz > 0)
            for (k = 0; k < BB_MAX_CAPS; k++)
                if (g->cap[k].active && g->cap[k].z < 150 * BB_FX) { tx = g->cap[k].x; ty = g->cap[k].y; break; }
    }
    if (tx > g->px + dead) in |= BB_IN_RIGHT;
    if (tx < g->px - dead) in |= BB_IN_LEFT;
    if (ty > g->py + dead) in |= BB_IN_UP;
    if (ty < g->py - dead) in |= BB_IN_DOWN;
    return in;
}

uint32_t bb_state_hash(const BBGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    int32_t v[17];
    v[0] = g->score; v[1] = g->level; v[2] = g->lives; v[3] = g->left; v[4] = g->px; v[5] = g->py; v[6] = g->bx; v[7] = g->by; v[8] = g->bz; v[9] = g->vx; v[10] = g->vy; v[11] = g->vz; v[12] = g->paddle_hits; v[13] = g->broken; v[14] = g->tick; v[15] = g->over; v[16] = g->won;
    for (i = 0; i < BB_CELLS; i++) { h ^= g->cell[i] * 16 + g->hits[i]; h *= 16777619UL; }
    for (i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) { h ^= (uint32_t)v[i]; h *= 16777619UL; }
    return h;
}
