// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * st.h - Stack Tower's game logic, ported from the browser version
 * (geekychris.github.io/tools/stack-tower, stack-tower-core.js): falling
 * pieces land on the outside of a round tower of 16 columns that wrap
 * round, so moving a piece left or right is turning the tower under it.
 * A full ring clears.
 *
 * The same random generator, bag and rules, so a seed deals the same
 * pieces as the browser game; times are whole milliseconds. No Amiga
 * headers: tested on the host too.
 */
#ifndef ST_H
#define ST_H

#include <stdint.h>

#define ST_COLS     16
#define ST_ROWS     20
#define ST_HIDDEN   4
#define ST_TOTAL    (ST_ROWS + ST_HIDDEN)
#define ST_GARBAGE  9
#define ST_NPIECES  8
#define ST_LOCK_MS  500
#define ST_LOCK_RESETS 15
#define ST_DIG_ROWS 10
#define ST_QUEUE    24

enum { MODE_MARATHON, MODE_RISING, MODE_DIG, MODE_COUNT };

/* events, for sound and the log */
enum { STE_NONE, STE_MOVE, STE_ROTATE, STE_KICK, STE_DROP, STE_HOLD, STE_LOCK, STE_CLEAR,
       STE_LEVEL, STE_GARBAGE, STE_WIN, STE_OVER, STE_COUNT };
typedef struct { int type, a, b; } StEvent;
#define ST_MAX_EVENTS 16

typedef struct { int type, rot, col, row; } Piece;

typedef struct {
    int mode;
    uint32_t seed, rng;
    uint8_t board[ST_COLS * ST_TOTAL];
    int queue[ST_QUEUE], nqueue;
    int hold;                            /* -1: none */
    int can_hold;
    Piece piece;
    int level, start_level, lines, combo, pieces;
    int32_t score, time_ms;
    int fall_ms, lock_ms, resets, soft;
    int over, won;
    int rise_ms, garbage_left, last_col;
    StEvent ev[ST_MAX_EVENTS];
    int nev;
} StGame;

extern const int8_t st_cells[ST_NPIECES][5][2];
extern const int st_ncells[ST_NPIECES];
extern const char *const st_piece_names[ST_NPIECES];
extern const char *const st_mode_names[MODE_COUNT];
extern const char *const st_event_names[STE_COUNT];

uint32_t st_hash(const char *s);
uint32_t st_rand32(uint32_t *a);             /* mulberry32: the next 32 bits */
int  st_rand_below(uint32_t *a, int n);      /* floor(rand() * n), as the browser's */
int  st_mod(int a, int n);
int  st_cells_of(const Piece *p, int out[5][2]);   /* columns wrapped; returns the count */
int  st_collides(const StGame *g, const Piece *p);
int  st_interval_ms(int level);
int  st_rise_ms(int level);

void st_new_game(StGame *g, int mode, uint32_t seed, int level, int col);
int  st_preview(StGame *g, int n, int *out);
int  st_move(StGame *g, int dir);
int  st_rotate(StGame *g, int dir);
int  st_ghost_row(const StGame *g);
int  st_hard_drop(StGame *g);
void st_soft_drop(StGame *g, int on);
int  st_hold(StGame *g);
void st_add_garbage(StGame *g, int gaps);
void st_tick(StGame *g, int dt_ms);
int  st_full_rows(const StGame *g, int *rows);
int  st_count_garbage_rows(const StGame *g);
int  st_pop_event(StGame *g, StEvent *e);

/* tests (tests/test_cases.c) */
int run_tests(void (*check)(int cond, const char *name));

#endif
