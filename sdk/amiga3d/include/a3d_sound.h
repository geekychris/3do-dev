// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_sound.h - sound effects and voice clips on both Amigas. A game
 * defines its effects at start-up (game_sound_init) by filling in sample
 * buffers with the synthesis helpers; a3_sfx plays one. On the classic
 * Amiga they live in chip RAM and ptplayer plays them on the channels the
 * music leaves (never the melody or bass channels, 0 and 1); on AmigaOS 4
 * ahi.device plays them.
 *
 * Voice clips (a .voc made by tools/modgen, see scairy_fun) stay in fast
 * RAM; one at a time is copied into a chip buffer as it is said.
 */
#ifndef A3D_SOUND_H
#define A3D_SOUND_H

#include <stdint.h>

#define A3_MAX_SFX   16
#define A3_FX_PERIOD 300                /* the effects play at 11.8 kHz */
#define A3_FX_RATE   (3546895L / A3_FX_PERIOD)

/* define effect id: a buffer of len bytes (even) to fill, played at
 * A3_FX_PERIOD; returns the buffer (NULL: no memory, no effect) */
int8_t *a3_sfx_define(int id, int len, int vol, int pri);
void a3_sfx(int id);
void a3_sfx_cleanup(void);

/* synthesis: a triangle sweeping f0 -> f1 Hz, fading over the last `fade`
 * samples; noise decaying (lp: smoothing, attack: samples to full) */
void a3_tone(int8_t *d, int n, int f0, int f1, int amp, int fade);
void a3_noise(int8_t *d, int n, int amp, int lp, int attack);

int  a3_voice_load(const char *path);   /* clips, 0 = none */
void a3_voice_say(int n);
int  a3_voice_clips(void);

/* the platform's side (main_68k.c / main_os4.c) */
void a3_sound_play(int8_t *data, int len, int period, int vol, int pri);
void *a3_sound_alloc(int len);          /* chip RAM on 68k */
void a3_sound_free(void *p, int len);

#endif
