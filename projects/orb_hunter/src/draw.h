// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - Drawing function declarations
 */
#ifndef DRAW_H
#define DRAW_H

#include <graphics/rastport.h>
#include "game.h"

void draw_frame(struct RastPort *rp, GameState *gs);
void draw_title(struct RastPort *rp);
void draw_hud(struct RastPort *rp, GameState *gs);
void draw_item_get(struct RastPort *rp, GameState *gs);
void draw_gameover(struct RastPort *rp);
void draw_text(struct RastPort *rp, WORD x, WORD y, const char *str, WORD color);

#endif
