// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.h - the frame loop's game side, shared by the classic Amiga
 * (main.c) and AmigaOS 4 (main_os4.c): input, the fixed 25 Hz simulation,
 * what to draw, and everything exposed over the AmigaBridge.
 */
#ifndef GAME_H
#define GAME_H

#include "ts.h"
#include "render.h"

extern World world;
extern int paused;

/* Supplied by the platform's main */
int32_t  plat_ticks(void);              /* 1/50 s since some start */
uint32_t plat_clock(void);              /* a fast counter, for stage timings */
int32_t  plat_us_since(uint32_t t0);    /* microseconds since plat_clock() was t0 */
void     plat_palette(int title);       /* 1 = the artwork's palette, 0 = the game's */

unsigned game_key_bit(unsigned rawkey); /* IN_* for a raw key code (no up bit) */
void game_init(void);                   /* the island, the bridge vars and hooks */
int  game_input(unsigned in);           /* pause / Esc; 0 = quit */
void game_update(unsigned in);          /* run the simulation up to now */
void game_draw(Canvas *cv, int buf);    /* buf: which screen buffer cv is (HUD cache) */
void game_after_frame(void);            /* bridge mirrors, fps, ab_poll, self-test */

#endif
