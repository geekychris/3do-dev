// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef VT_COMBAT_H
#define VT_COMBAT_H

#include "engine3d.h"

/*
 * Combat subsystem — bullets + enemy AI + hit detection.
 *
 * Bullets are short 3D line segments in world space. Each has an
 * owner team so we can decide who can shoot whom.
 *
 * Enemy AI (Team 1) turns its ship toward the player over time and
 * fires periodic aimed bolts. Static, one-behaviour, one-hp for now
 * — Phase 3+ can layer classes and difficulty tiers on top.
 *
 * Hit detection: bullet-vs-ship uses a sphere test in world space.
 * On kill: ship becomes inactive and spawns a fragment burst
 * (short-lived non-colliding "shards"). Player death sets a global
 * game-over flag.
 */

#define VT_MAX_BULLETS   24
#define VT_BULLET_LIFE   80          /* frames */
#define VT_BULLET_SPEED  300         /* world units/tick */
#define VT_HIT_RADIUS    800         /* ship hitsphere in world units */
#define VT_PLAYER_COOL   10          /* frames between shots */
#define VT_ENEMY_COOL    60
#define VT_PLAYER_MAX_ENERGY  1000
#define VT_ENEMY_DAMAGE       120    /* per hit */

typedef struct {
    LONG  x, y, z;
    LONG  vx, vy, vz;
    UWORD life;
    UBYTE owner;                    /* 0=player, 1=enemy, 255=inactive */
    UBYTE _pad;
} Bullet;

typedef struct {
    Bullet bullets[VT_MAX_BULLETS];
    UWORD  player_cooldown;
    LONG   player_energy;
    LONG   score;
    UBYTE  game_over;               /* 1 = player destroyed */
    UBYTE  _pad[3];
} Combat;

void vt_combat_init(Combat *c);

/* Called once per frame. cam gives the player's pose; entities is
 * the world (index 0 skipped as the player, entities 1..N-1 hit-
 * testable). Enemies with team==1 tick their AI here too. */
void vt_combat_tick(Combat *c, Camera *cam,
                    Entity *entities, int num_entities,
                    UWORD input_flags);

/* Render bullets on top of the 3D scene — call after
 * e3d_render_frame with the same view matrix / rp / cam. */
struct RastPort;
void vt_combat_render(struct RastPort *rp, const Combat *c,
                      const Camera *cam);

/* Input bit that fires a player bolt. Wired in main.c. */
#define VT_IN_FIRE 0x0100

#endif
