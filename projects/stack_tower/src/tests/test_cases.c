// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Stack Tower's tests: the C port against games played by
 * the browser's own engine (vectors.h, from gen_vectors.js), and the
 * rules one by one. On the host (make host-test), on the Amiga
 * (stack_tower_test) and inside the game (the selftest hook).
 */
#include <string.h>
#include "../st.h"
#include "vectors.h"

const char test_client[] = "stack_tower_test";
static StGame tg;                       /* off the Amiga's stack */

static uint32_t board_hash(const StGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    for (i = 0; i < ST_COLS * ST_TOTAL; i++) { h ^= g->board[i]; h *= 16777619UL; }
    return h;
}

static void play_script(StGame *g, int mode, uint32_t seed, int *pieces)
{
    int k, i;
    st_new_game(g, mode, seed, 1, 0);
    for (k = 0; k < 120 && !g->over && !g->won; k++) {
        int r = (int)((k * 7 + seed) % 5), m = (int)((k * 13 + seed) % 9) - 4;
        if (k % 11 == 5) st_hold(g);
        for (i = 0; i < r; i++) st_rotate(g, i % 2 ? -1 : 1);
        for (i = 0; i < (m < 0 ? -m : m); i++) st_move(g, m < 0 ? -1 : 1);
        st_hard_drop(g);
    }
    *pieces = k;
}

/* fill row r except the cells the piece will take */
static void fill_row(StGame *g, int r, int skip0, int skip1)
{
    int c;
    for (c = 0; c < ST_COLS; c++)
        if (c != skip0 && c != skip1) g->board[r * ST_COLS + c] = 1;
}

int run_tests(void (*check)(int cond, const char *name))
{
    StGame *g = &tg;
    int v, ok, pieces, q[5], i;
    StEvent e;

    /* the same games as the browser */
    for (v = 0; v < (int)(sizeof(vectors) / sizeof(vectors[0])); v++) {
        play_script(g, vectors[v].mode, (uint32_t)vectors[v].seed, &pieces);
        st_preview(g, 5, q);
        check(pieces == vectors[v].pieces, "browser vector: pieces played");
        check(g->score == vectors[v].score, "browser vector: score");
        check(g->lines == vectors[v].lines && g->over == vectors[v].over && g->won == vectors[v].won,
              "browser vector: lines, over, won");
        check(board_hash(g) == (uint32_t)vectors[v].board, "browser vector: the board");
        for (ok = 1, i = 0; i < 5; i++) if (q[i] != vectors[v].queue[i]) ok = 0;
        check(ok, "browser vector: the next five pieces");
        check(g->hold == vectors[v].hold, "browser vector: the held piece");
    }

    /* pieces */
    {
        Piece p = { 0, 0, 15, 5 };
        int c[5][2], n = st_cells_of(&p, c);
        check(n == 3 && c[0][0] == 14 && c[2][0] == 0, "the rod wraps round the seam");
        p.rot = 1;
        st_cells_of(&p, c);
        check(c[0][0] == 15 && c[0][1] == 6 && c[2][1] == 4, "a quarter turn stands the rod up");
        p.rot = 4;
        st_cells_of(&p, c);
        check(c[0][0] == 14, "four quarter turns: back as it was");
    }
    check(st_mod(-1, 16) == 15 && st_mod(17, 16) == 1, "columns wrap");
    check(st_interval_ms(1) == 900 && st_interval_ms(2) == 738 && st_interval_ms(30) == 33, "gravity speeds up");

    /* a ring clears, the rings above drop, score x level */
    st_new_game(g, MODE_MARATHON, 1, 3, 0);
    memset(g->board, 0, sizeof(g->board));
    fill_row(g, 0, 7, 8);
    fill_row(g, 1, 7, 8);
    g->board[2 * ST_COLS + 3] = 5;
    g->piece.type = 1;                  /* elbow: (0,0) (1,0) (0,1) -> needs a 2x2 hole */
    g->piece.rot = 0;
    g->piece.col = 7;
    g->piece.row = 10;
    g->nev = 0;
    st_hard_drop(g);
    check(g->lines == 1, "a full ring clears");
    check(g->board[1 * ST_COLS + 3] == 5, "the rings above drop");
    check(g->score == 100 * 3 + 2 * 10, "one ring at level 3: 300 + 2 a row hard dropped");
    for (ok = 0; st_pop_event(g, &e);) if (e.type == STE_CLEAR && e.a == 1) ok = 1;
    check(ok, "clear event");

    /* combos */
    st_new_game(g, MODE_MARATHON, 1, 1, 0);
    memset(g->board, 0, sizeof(g->board));
    g->combo = 0;
    fill_row(g, 0, 0, 1);
    g->piece.type = 0; g->piece.rot = 0; g->piece.col = 1; g->piece.row = 6;   /* rod: cols 0..2 */
    g->board[0 * ST_COLS + 2] = 0;
    st_hard_drop(g);
    check(g->combo == 1 && g->score == 100 + 50 + 2 * 6, "a second clear in a row adds a combo bonus");

    /* holding */
    st_new_game(g, MODE_MARATHON, 7, 1, 0);
    {
        int first = g->piece.type, next = g->queue[0];
        check(st_hold(g) && g->hold == first && g->piece.type == next, "hold swaps in the next piece");
        check(!st_hold(g), "only one hold per piece");
    }

    /* gravity and the lock delay */
    st_new_game(g, MODE_MARATHON, 7, 1, 0);
    {
        int r0 = g->piece.row;
        st_tick(g, 899);
        check(g->piece.row == r0, "not yet a row at 0.899 s");
        st_tick(g, 2);
        check(g->piece.row == r0 - 1, "one row after 0.9 s at level 1");
        st_soft_drop(g, 1);
        st_tick(g, 45);
        check(g->piece.row == r0 - 2 && g->score == 1, "soft drop: 45 ms a row at level 1, 1 point a row");
        st_soft_drop(g, 0);
        g->piece.row = st_ghost_row(g);
        i = g->pieces;
        st_tick(g, 499);
        check(g->pieces == i, "on the stack: no lock before 0.5 s");
        st_tick(g, 2);
        check(g->pieces == i + 1, "locks after 0.5 s");
    }

    /* rising and dig */
    st_new_game(g, MODE_RISING, 3, 1, 0);
    st_tick(g, st_rise_ms(1));
    check(st_count_garbage_rows(g) == 1, "rising: a garbage ring pushes up");
    st_new_game(g, MODE_DIG, 3, 1, 0);
    check(st_count_garbage_rows(g) == ST_DIG_ROWS, "dig: ten garbage rings to clear");
    memset(g->board, 0, sizeof(g->board));
    fill_row(g, 0, 0, 1);
    for (i = 2; i < ST_COLS; i++) g->board[i] = ST_GARBAGE;
    g->piece.type = 0; g->piece.rot = 0; g->piece.col = 1; g->piece.row = 6;
    g->board[2] = 0;
    st_hard_drop(g);
    check(g->won, "dig: the last garbage ring cleared wins");

    /* game over at the top */
    st_new_game(g, MODE_MARATHON, 9, 1, 0);
    for (i = 0; i < 40 && !g->over; i++) st_hard_drop(g);
    check(g->over, "stacking in one place ends the game");
    return 0;
}
