// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef VT_GAMEMODE_H
#define VT_GAMEMODE_H

/*
 * Top-level game mode. Drives which subsystems tick and which
 * screen the renderer paints each frame.
 *
 *   FLIGHT     — 3D world + combat + scanner active
 *   DOCKING    — cinematic slot approach, player controls
 *                partially locked, "DOCKING" banner
 *   DOCKED     — station interior menu (trading in Phase 5)
 *   UNDOCKING  — cinematic pull-out back to FLIGHT
 *   GAME_OVER  — final screen, waits for restart
 */
enum GameMode {
    GM_TITLE     = 0,   /* pre-game briefing screen */
    GM_FLIGHT    = 1,
    GM_DOCKING   = 2,
    GM_DOCKED    = 3,
    GM_UNDOCKING = 4,
    GM_GAME_OVER = 5,
    GM_WIN       = 6,
};

/* Win the game by earning this many credits. */
#define WIN_CREDITS_TARGET  5000

/* Enemy respawn cooldown — after all enemies are dead this many
 * frames elapse before a new Krait spawns in a random-ish spot. */
#define ENEMY_RESPAWN_FRAMES 300

/* Distance from station centre within which the docking prompt
 * appears and D triggers the approach. */
#define DOCK_APPROACH_RANGE  4500

/* Distance at which the DOCKING animation completes to DOCKED. */
#define DOCK_LOCK_RANGE      1200

#endif
