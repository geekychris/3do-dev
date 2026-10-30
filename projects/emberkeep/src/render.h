/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

#ifndef EMBERKEEP_RENDER_H
#define EMBERKEEP_RENDER_H

#include <exec/types.h>
#include "game.h"

/* 3DO port: render.h with the GameState references as pointers; the
 * screen is the layer's (sdk/amiga), so there is no render_get_screen. */
LONG render_open(void);
void render_close(void);

void render_frame(const GameState *gs);
void render_title(const GameState *gs);
void render_endscreen(const GameState *gs);

/* Show the frame (gfx_swap, paced to 50 fps). */
void render_flip(void);

void render_text(LONG x, LONG y, UBYTE pen, const char *text);

#endif
