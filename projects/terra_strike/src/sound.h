// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * sound.h - Sound effects for Terra Strike
 *
 * sound.c synthesises the effects. The classic Amiga plays them, and the
 * music, with ptplayer on Paula; AmigaOS 4 plays them through ahi.device
 * and the music with modplay.c (sound_ahi.c).
 */
#ifndef SOUND_H
#define SOUND_H

#include <exec/types.h>

#ifdef __PPC__
/* the fields of ptplayer's SfxStructure that sound.c fills in */
typedef struct {
    APTR  sfx_ptr;   /* sample start */
    WORD  sfx_len;   /* length in words */
    WORD  sfx_per;   /* Paula period */
    WORD  sfx_vol;   /* volume 0..64 */
    BYTE  sfx_cha;
    BYTE  sfx_pri;   /* a playing effect is only cut by one of equal or higher priority */
} SfxStructure;

/* sound_ahi.c */
int  ahi_open(void);                     /* 0: no ahi.device, so no sound */
void ahi_close(void);
void ahi_sfx(const SfxStructure *s);
int  ahi_music(const void *mod, ULONG len);   /* 0: not a module modplay can play */
void ahi_update(void);                   /* once a frame: keeps the music queued */
#else
#include "ptplayer.h"
#define CUSTOM_BASE ((void *)0xdff000)
#endif

void sound_init(void);
void sound_cleanup(void);

void sfx_gunfire(void);
void sfx_explosion(void);
void sfx_hit(void);
void sfx_die(void);

#endif
