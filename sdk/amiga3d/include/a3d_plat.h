// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_plat.h - the shared engine's platform layer: what a game supplies,
 * and what the classic Amiga (main_68k.c) and AmigaOS 4 (main_os4.c)
 * mains give it. A game is the same code on both.
 *
 * The frame: the main reads the keyboard (game_key_bit maps each raw key
 * to the game's input bits) and the joystick (game_joy_bits), then calls
 * game_input, game_update, game_draw and game_after_frame. 25 frames a
 * second on both: every other PAL frame on the classic Amiga, a fixed
 * 40 ms schedule on AmigaOS 4.
 */
#ifndef A3D_PLAT_H
#define A3D_PLAT_H

#include <stdint.h>
#include "a3d_draw.h"

/* ---------- the game supplies ---------- */
extern const char game_title[];         /* window / screen title */
extern const char game_client[];        /* bridge client name, and the program's name */
extern const char game_music[];         /* "PROGDIR:x.mod", or "" */
extern const char game_voice[];         /* "PROGDIR:x.voc" (a3d_sound.c), or "" */

#define A3_JOY_LEFT  1
#define A3_JOY_RIGHT 2
#define A3_JOY_UP    4
#define A3_JOY_DOWN  8
#define A3_JOY_FIRE  16

unsigned game_key_bit(unsigned rawkey); /* the game's input bit for a raw key (no up bit) */
unsigned game_joy_bits(unsigned joy);   /* A3_JOY_* -> the game's input bits */
void game_init(void);                   /* the bridge client is registered already */
void game_sound_init(void);             /* define the effects (a3d_sound.h) */
int  game_input(unsigned in);           /* 0 = quit */
void game_update(unsigned in);
void game_draw(Canvas *cv, int buf);    /* buf: which of two screen buffers cv is */
void game_after_frame(void);

/* ---------- the platform supplies ---------- */
int32_t  plat_ticks(void);              /* 1/50 s since some start */
uint32_t plat_clock(void);              /* a fast counter, for stage timings */
int32_t  plat_us_since(uint32_t t0);    /* microseconds since plat_clock() was t0 */
void     plat_palette(const uint16_t pal[32]);     /* 12-bit RGB */
/* a gradient on one pen over rows 0..rows-1 (the copper on the classic
 * Amiga, per row on AmigaOS 4); rows 0 turns it off */
void     plat_sky(int pen, uint16_t top, uint16_t bottom, int rows);
extern int32_t plat_in_keys, plat_in_joy;  /* every input bit seen over the last 10 s */
void     plat_activate(void);           /* the game's window to the front and active (keys go to it) */

#endif
