// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Minimal MOD-style music player - direct Paula hardware access
 */
#ifndef MODPLAY_H
#define MODPLAY_H

#include <exec/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Song IDs mirrored from modplay.c so callers can pass by name. */
#define MODPLAY_SONG_GAME  0
#define MODPLAY_SONG_TITLE 1

int  modplay_init(void);           /* allocate samples, build both songs */
void modplay_start(void);          /* begin playback of current song */
void modplay_start_song(int id);   /* switch to song, then start */
void modplay_stop(void);           /* stop playback, silence Paula */
void modplay_cleanup(void);        /* free chip RAM */
void modplay_tick(void);           /* call once per frame (VBlank) */

/* Play a one-shot SFX on channel 3, temporarily overriding music */
void modplay_sfx(BYTE *data, UWORD len_words, UWORD period, UWORD volume);

#ifdef __cplusplus
}
#endif

#endif
