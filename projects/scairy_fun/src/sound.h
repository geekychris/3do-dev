// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/* sound.h - ScAIry Fun's effects and the AI's voice, through the shared
 * engine (../amiga3d/a3d_sound.h): ptplayer on the classic Amiga, AHI on
 * AmigaOS 4. The effects are synthesised in game.c (game_sound_init). */
#ifndef SOUND_H
#define SOUND_H

#include "a3d_sound.h"

enum { SFX_CRASH, SFX_SPLASH, SFX_SCRAPE, SFX_CHIME_ON, SFX_CHIME_OFF, SFX_CONE,
       SFX_SAVE, SFX_ALERT, SFX_SIREN, SFX_BLIP, SFX_COUNT };

#define sfx(which)    a3_sfx(which)
#define voice_say(m)  a3_voice_say(m)
#define voice_clips() a3_voice_clips()

#endif
