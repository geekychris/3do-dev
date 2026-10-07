// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef VT_SFX_H
#define VT_SFX_H

#include <exec/types.h>

/*
 * Procedural 8-bit signed sample bank routed through modplay's
 * channel-3 SFX slot. Generation runs once at boot; play() hands
 * a pointer to modplay_sfx() which briefly steals ch3 from the
 * music and plays the sample.
 */
enum {
    SFX_LASER      = 0,
    SFX_EXPLOSION  = 1,
    SFX_HIT        = 2,
    SFX_DOCK       = 3,
    SFX_BUY        = 4,
    SFX_COUNT      = 5,
};

int  vt_sfx_init(void);
void vt_sfx_shutdown(void);
void vt_sfx_play(int id);

#endif
