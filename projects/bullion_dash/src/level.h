// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Bullion Dash - Level management
 */
#ifndef LEVEL_H
#define LEVEL_H

#include "game.h"

/* Load a level by number into game state */
void level_load(GameState *gs, int level_num);

/* Tick brick regeneration timers */
void level_tick_bricks(GameState *gs);

/* Reveal hidden ladders (called when all gold collected) */
void level_reveal_hidden_ladders(GameState *gs);

/* Dig a brick at grid position - returns 1 if successful */
int level_dig_brick(GameState *gs, int gx, int gy);

/* Count remaining gold tiles */
int level_count_gold(void);

/* Save current level as text file - returns 0 on success */
int level_save_text(const char *path);

/* Load level from text file - returns 0 on success */
int level_load_text(const char *path);

/* Scan tile grid for TILE_PLAYER / TILE_ENEMY spawn markers, convert
 * them to active entities and clear the marker tiles. Used by the
 * editor when transitioning to test-play. */
void level_activate_spawn_markers(GameState *gs);

#endif /* LEVEL_H */
