// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * st_core.c - Stack Tower's rules (see st.h), line for line from the
 * browser's stack-tower-core.js.
 */
#include <stdio.h>
#include <string.h>
#include "st.h"
#ifdef AMIGA3DO
#include "a3d_i64.h"
#endif

/* Cells as [dx, dy] about the pivot, y up: two trominoes and six
 * pentominoes, none of the classic four-square pieces. */
const int8_t st_cells[ST_NPIECES][5][2] = {
    { { -1, 0 }, { 0, 0 }, { 1, 0 } },                        /* rod */
    { { 0, 0 }, { 1, 0 }, { 0, 1 } },                         /* elbow */
    { { -1, 1 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { 1, 1 } },   /* cup */
    { { 0, -1 }, { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },    /* flag */
    { { 0, -1 }, { -1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 } },   /* star */
    { { -1, 1 }, { 0, 1 }, { 1, 1 }, { 0, 0 }, { 0, -1 } },   /* anchor */
    { { -1, 1 }, { -1, 0 }, { 0, 0 }, { 0, -1 }, { 1, -1 } }, /* stairs */
    { { -1, 1 }, { 0, 1 }, { 0, 0 }, { 0, -1 }, { 1, -1 } },  /* bolt */
};
const int st_ncells[ST_NPIECES] = { 3, 3, 5, 5, 5, 5, 5, 5 };
const char *const st_piece_names[ST_NPIECES] = { "ROD", "ELBOW", "CUP", "FLAG", "STAR", "ANCHOR", "STAIRS", "BOLT" };
const char *const st_mode_names[MODE_COUNT] = { "marathon", "rising", "dig" };
const char *const st_event_names[STE_COUNT] = { "none", "move", "rotate", "kick", "drop", "hold",
    "lock", "clear", "level", "garbage", "win", "over" };

/* tried in order after a rotation that collides: none, sideways, up, further */
static const int8_t kicks[10][2] = { { 0, 0 }, { 1, 0 }, { -1, 0 }, { 0, 1 }, { 2, 0 }, { -2, 0 },
                                     { 1, 1 }, { -1, 1 }, { 0, 2 }, { 0, -1 } };
static const int line_score[7] = { 0, 100, 300, 600, 1000, 1500, 2100 };
/* gravity: 0.9 s a row at level 1, x0.82 a level, never under 1/30 s */
static const int16_t interval_tab[30] = { 900, 738, 605, 496, 407, 334, 274, 224, 184, 151, 124, 101,
    83, 68, 56, 46, 38, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33 };

int st_mod(int a, int n)
{
    if (n > 0 && !(n & (n - 1))) return a & (n - 1);    /* the same, without the ARM60's slow divides */
    return ((a % n) + n) % n;
}
int st_interval_ms(int level) { return interval_tab[level < 1 ? 0 : level > 30 ? 29 : level - 1]; }
int st_rise_ms(int level)
{
    int v = 14000 - level * 1200;
    return v < 4000 ? 4000 : v;
}

uint32_t st_hash(const char *s)
{
    uint32_t h = 2166136261UL;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619UL; }
    return h;
}

/* mulberry32, as the browser's rng() */
uint32_t st_rand32(uint32_t *a)
{
    uint32_t t;
    *a += 0x6D2B79F5UL;
    t = *a;
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

int st_rand_below(uint32_t *a, int n)
{
#ifdef AMIGA3DO                         /* the 3DO: no 64-bit arithmetic (a3d_i64.h) */
    return (int)a3_umul_hi(st_rand32(a), (uint32_t)n);
#else
    return (int)(((uint64_t)st_rand32(a) * (uint32_t)n) >> 32);
#endif
}

static void push_ev(StGame *g, int type, int a, int b)
{
    if (g->nev < ST_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}

int st_pop_event(StGame *g, StEvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(&g->ev[0], &g->ev[1], (g->nev - 1) * sizeof(StEvent));
    g->nev--;
    return 1;
}

/* a piece's cells after rot quarter turns clockwise: (x, y) -> (y, -x) */
int st_cells_of(const Piece *p, int out[5][2])
{
    int n = st_ncells[p->type], i, k, r = st_mod(p->rot, 4);
    for (i = 0; i < n; i++) {
        int x = st_cells[p->type][i][0], y = st_cells[p->type][i][1];
        for (k = 0; k < r; k++) { int t = x; x = y; y = -t; }
        out[i][0] = st_mod(p->col + x, ST_COLS);
        out[i][1] = p->row + y;
    }
    return n;
}

static int idx(int c, int r) { return r * ST_COLS + st_mod(c, ST_COLS); }

int st_collides(const StGame *g, const Piece *p)
{
    int c[5][2], n = st_cells_of(p, c), i;
    for (i = 0; i < n; i++)
        if (c[i][1] < 0 || c[i][1] >= ST_TOTAL || g->board[idx(c[i][0], c[i][1])]) return 1;
    return 0;
}

static void bag(StGame *g)
{
    int b[ST_NPIECES], i;
    for (i = 0; i < ST_NPIECES; i++) b[i] = i;
    for (i = ST_NPIECES - 1; i > 0; i--) {
        int j = st_rand_below(&g->rng, i + 1), t = b[i];
        b[i] = b[j];
        b[j] = t;
    }
    for (i = 0; i < ST_NPIECES && g->nqueue < ST_QUEUE; i++) g->queue[g->nqueue++] = b[i];
}

static void refill(StGame *g) { while (g->nqueue < 8) bag(g); }

int st_preview(StGame *g, int n, int *out)
{
    int i;
    refill(g);
    for (i = 0; i < n && i < g->nqueue; i++) out[i] = g->queue[i];
    return i;
}

static void garbage_row(StGame *g, int gaps, uint8_t *row)
{
    int freec[ST_COLS], nf = 0, i;
    memset(row, ST_GARBAGE, ST_COLS);
    while (nf < gaps) {
        int c = st_rand_below(&g->rng, ST_COLS), seen = 0;
        for (i = 0; i < nf; i++) if (freec[i] == c) seen = 1;
        if (!seen) freec[nf++] = c;
    }
    for (i = 0; i < nf; i++) row[freec[i]] = 0;
}

static void spawn(StGame *g, int type, int col)
{
    int fresh = type < 0;
    refill(g);
    if (fresh) {
        type = g->queue[0];
        memmove(&g->queue[0], &g->queue[1], (g->nqueue - 1) * sizeof(int));
        g->nqueue--;
    }
    g->piece.type = type;
    g->piece.rot = 0;
    g->piece.col = st_mod(col, ST_COLS);
    g->piece.row = ST_ROWS + 1;
    g->fall_ms = g->lock_ms = g->resets = 0;
    if (fresh) g->can_hold = 1;
    if (st_collides(g, &g->piece)) {
        g->piece.row++;                       /* try one higher before giving up */
        if (st_collides(g, &g->piece)) { g->over = 1; push_ev(g, STE_OVER, 0, 0); }
    }
}

void st_new_game(StGame *g, int mode, uint32_t seed, int level, int col)
{
    char key[40];
    int r;
    memset(g, 0, sizeof(*g));
    g->mode = mode;
    g->seed = seed;
    sprintf(key, "%lu:%s", (unsigned long)seed, st_mode_names[mode]);
    g->rng = st_hash(key);
    if (!g->rng) g->rng = 0x9e3779b9UL;
    g->hold = -1;
    g->can_hold = 1;
    g->level = g->start_level = level < 1 ? 1 : level;
    g->combo = -1;
    g->last_col = col;
    if (mode == MODE_DIG) {
        for (r = 0; r < ST_DIG_ROWS; r++) garbage_row(g, r % 3 == 0 ? 2 : 1, &g->board[r * ST_COLS]);
        g->garbage_left = ST_DIG_ROWS;
    }
    refill(g);
    spawn(g, -1, col);
}

static int touching(const StGame *g)
{
    Piece p = g->piece;
    p.row--;
    return st_collides(g, &p);
}

static void moved(StGame *g)
{
    /* a successful move or turn on the stack restarts the lock timer, a limited number of times */
    if (touching(g) && g->resets < ST_LOCK_RESETS) { g->lock_ms = 0; g->resets++; }
}

int st_move(StGame *g, int dir)
{
    Piece p;
    if (g->over || g->won) return 0;
    p = g->piece;
    p.col = st_mod(p.col + dir, ST_COLS);
    if (st_collides(g, &p)) return 0;
    g->piece = p;
    g->last_col = p.col;
    moved(g);
    push_ev(g, STE_MOVE, dir, 0);
    return 1;
}

int st_rotate(StGame *g, int dir)
{
    int k;
    if (g->over || g->won) return 0;
    for (k = 0; k < 10; k++) {
        Piece p = g->piece;
        p.rot = st_mod(p.rot + dir, 4);
        p.col = st_mod(p.col + kicks[k][0] * dir, ST_COLS);
        p.row += kicks[k][1];
        if (!st_collides(g, &p)) {
            g->piece = p;
            g->last_col = p.col;
            moved(g);
            push_ev(g, k ? STE_KICK : STE_ROTATE, dir, 0);
            return 1;
        }
    }
    return 0;
}

int st_ghost_row(const StGame *g)
{
    Piece p = g->piece;
    while (1) {
        p.row--;
        if (st_collides(g, &p)) return p.row + 1;
    }
}

int st_full_rows(const StGame *g, int *rows)
{
    int r, c, n = 0;
    for (r = 0; r < ST_TOTAL; r++) {
        for (c = 0; c < ST_COLS && g->board[r * ST_COLS + c]; c++)
            ;
        if (c == ST_COLS) rows[n++] = r;
    }
    return n;
}

int st_count_garbage_rows(const StGame *g)
{
    int r, c, n = 0;
    for (r = 0; r < ST_TOTAL; r++)
        for (c = 0; c < ST_COLS; c++)
            if (g->board[r * ST_COLS + c] == ST_GARBAGE) { n++; break; }
    return n;
}

static void clear_rows(StGame *g, const int *rows, int n)
{
    uint8_t keep[ST_COLS * ST_TOTAL];
    int r, k = 0, i, skip;
    for (r = 0; r < ST_TOTAL; r++) {
        for (skip = 0, i = 0; i < n; i++) if (rows[i] == r) skip = 1;
        if (!skip) memcpy(&keep[k++ * ST_COLS], &g->board[r * ST_COLS], ST_COLS);
    }
    memset(g->board, 0, sizeof(g->board));
    memcpy(g->board, keep, k * ST_COLS);
}

static void lock_piece(StGame *g)
{
    int cells[5][2], n = st_cells_of(&g->piece, cells), i, all_hidden = 1, rows[ST_TOTAL], nr;
    for (i = 0; i < n; i++) {
        g->board[idx(cells[i][0], cells[i][1])] = (uint8_t)(g->piece.type + 1);
        if (cells[i][1] < ST_ROWS) all_hidden = 0;
    }
    g->pieces++;
    push_ev(g, STE_LOCK, g->piece.type, 0);
    if (all_hidden) { g->over = 1; push_ev(g, STE_OVER, 1, 0); return; }
    nr = st_full_rows(g, rows);
    if (nr) {
        int32_t pts;
        clear_rows(g, rows, nr);
        g->combo++;
        g->lines += nr;
        pts = (int32_t)line_score[nr < 7 ? nr : 6] * g->level + (g->combo > 0 ? 50L * g->combo * g->level : 0);
        g->score += pts;
        push_ev(g, STE_CLEAR, nr, (int)pts);
        if (g->mode == MODE_DIG) {
            g->garbage_left = st_count_garbage_rows(g);
            if (g->garbage_left == 0) { g->won = 1; push_ev(g, STE_WIN, g->time_ms, 0); return; }
        }
        {
            int lv = g->start_level + g->lines / 10;
            if (g->mode != MODE_DIG && lv > g->level) { g->level = lv; push_ev(g, STE_LEVEL, lv, 0); }
        }
    } else {
        g->combo = -1;
    }
    spawn(g, -1, g->last_col);
}

int st_hard_drop(StGame *g)
{
    int to, dist;
    if (g->over || g->won) return 0;
    to = st_ghost_row(g);
    dist = g->piece.row - to;
    g->piece.row = to;
    g->score += 2 * dist;
    push_ev(g, STE_DROP, dist, 0);
    lock_piece(g);
    return dist;
}

void st_soft_drop(StGame *g, int on) { g->soft = on != 0; }

int st_hold(StGame *g)
{
    int cur;
    if (g->over || g->won || !g->can_hold) return 0;
    cur = g->piece.type;
    if (g->hold < 0) { g->hold = cur; spawn(g, -1, g->last_col); }
    else { int t = g->hold; g->hold = cur; spawn(g, t, g->last_col); }
    g->can_hold = 0;
    push_ev(g, STE_HOLD, cur, 0);
    return 1;
}

void st_add_garbage(StGame *g, int gaps)
{
    uint8_t row[ST_COLS];
    int c, spilled = 0;
    garbage_row(g, gaps, row);
    for (c = 0; c < ST_COLS; c++) if (g->board[(ST_TOTAL - 1) * ST_COLS + c]) spilled = 1;
    memmove(&g->board[ST_COLS], &g->board[0], ST_COLS * (ST_TOTAL - 1));
    memcpy(&g->board[0], row, ST_COLS);
    if (spilled) { g->over = 1; push_ev(g, STE_OVER, 2, 0); return; }
    if (st_collides(g, &g->piece)) {
        g->piece.row++;
        if (st_collides(g, &g->piece)) { g->over = 1; push_ev(g, STE_OVER, 2, 0); return; }
    }
    push_ev(g, STE_GARBAGE, 0, 0);
}

void st_tick(StGame *g, int dt)
{
    int step, iv;
    if (g->over || g->won) return;
    g->time_ms += dt;
    if (g->mode == MODE_RISING) {
        int every = st_rise_ms(g->level);
        g->rise_ms += dt;
        if (g->rise_ms >= every) {
            g->rise_ms -= every;
            st_add_garbage(g, g->level >= 6 ? 1 : 2);
            if (g->over) return;
        }
    }
    iv = st_interval_ms(g->level);
    step = iv;
    if (g->soft) { int s = iv / 20 > 17 ? iv / 20 : 17; step = s < iv ? s : iv; }
    if (touching(g)) {
        g->fall_ms = 0;
        g->lock_ms += dt;
        if (g->lock_ms >= ST_LOCK_MS || (g->resets >= ST_LOCK_RESETS && g->lock_ms >= ST_LOCK_MS / 3))
            lock_piece(g);
        return;
    }
    g->fall_ms += dt;
    while (g->fall_ms >= step && !touching(g)) {
        g->fall_ms -= step;
        g->piece.row--;
        if (g->soft) g->score += 1;
    }
    if (touching(g)) g->fall_ms = 0;
}
