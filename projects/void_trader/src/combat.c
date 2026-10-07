// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "combat.h"
#include "sfx.h"

#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>
#include <string.h>

/* Pens set up in main.c's install_palette.
 *   125 = laser green (player bolts)
 *   124 = warning red (enemy bolts + player-hit flash)
 *   1..4 = starfield whites (used for explosion fragments)
 */
#define PEN_PLAYER_BOLT 125
#define PEN_ENEMY_BOLT  124
#define PEN_FRAG        3

void vt_combat_init(Combat *c)
{
    int i;
    memset(c, 0, sizeof(*c));
    c->player_energy = VT_PLAYER_MAX_ENERGY;
    for (i = 0; i < VT_MAX_BULLETS; i++) c->bullets[i].owner = 255;
}

static int spawn_bullet(Combat *c, LONG x, LONG y, LONG z,
                        LONG vx, LONG vy, LONG vz, UBYTE owner)
{
    int i;
    for (i = 0; i < VT_MAX_BULLETS; i++) {
        if (c->bullets[i].owner == 255) {
            c->bullets[i].x  = x;  c->bullets[i].y  = y;  c->bullets[i].z  = z;
            c->bullets[i].vx = vx; c->bullets[i].vy = vy; c->bullets[i].vz = vz;
            c->bullets[i].life = VT_BULLET_LIFE;
            c->bullets[i].owner = owner;
            return i;
        }
    }
    return -1;
}

/* Camera-forward vector in world space. Cam yaw/pitch drive it;
 * roll doesn't affect the forward vector. Returns FP-scaled units
 * — caller shifts before use as world offsets. */
static void cam_forward(const Camera *cam, LONG *fx, LONG *fy, LONG *fz)
{
    LONG sp = e3d_sin(cam->pitch), cp = e3d_cos(cam->pitch);
    LONG sy = e3d_sin(cam->yaw),   cy = e3d_cos(cam->yaw);
    *fx = ( sy * cp) >> FP;
    *fy = (-sp);
    *fz = ( cy * cp) >> FP;
}

/* Signed integer sqrt for distances. */
static LONG isqrt_len(LONG v)
{
    LONG r = 0, bit = 1L << 30;
    if (v <= 0) return 1;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r ? r : 1;
}

/* Rotate a value toward a target by at most `rate` — modular over
 * 360 so wrapping stays sane (turn the short way). */
static LONG turn_toward(LONG cur, LONG target, LONG rate)
{
    LONG diff = target - cur;
    LONG next;
    while (diff >  180) diff -= 360;
    while (diff < -180) diff += 360;
    if (diff >  rate) diff =  rate;
    if (diff < -rate) diff = -rate;
    next = cur + diff;
    while (next >= 360) next -= 360;
    while (next <    0) next += 360;
    return next;
}

/* Compute target yaw + pitch to look from src to tgt (Y=up). */
static void look_angles(LONG dx, LONG dy, LONG dz,
                        LONG *out_yaw, LONG *out_pitch)
{
    /* Approximate atan2 using the sin table by scanning. Cheap — 360
     * comparisons max; we only do this once per enemy per frame. */
    LONG best_yaw = 0, best_pitch = 0;
    LONG best_err = 0x7FFFFFFF;
    LONG horiz2 = dx * dx + dz * dz;
    LONG horiz  = isqrt_len(horiz2);
    /* yaw: sin(yaw) = dx / horiz, cos(yaw) = dz / horiz. Pick the
     * yaw whose sin/cos best matches. */
    int a;
    LONG len;
    for (a = 0; a < 360; a += 3) {
        LONG s = e3d_sin(a), c = e3d_cos(a);
        LONG want_dx = (horiz * s) >> FP;
        LONG want_dz = (horiz * c) >> FP;
        LONG err = (want_dx - dx) * (want_dx - dx)
                 + (want_dz - dz) * (want_dz - dz);
        if (err < best_err) { best_err = err; best_yaw = a; }
    }
    /* pitch: sin(pitch) = -dy / len. */
    len = isqrt_len(horiz2 + dy * dy);
    best_err = 0x7FFFFFFF;
    for (a = -90; a < 90; a += 3) {
        LONG s = e3d_sin(a);
        LONG want_dy = (-len * s) >> FP;
        LONG err = (want_dy - dy) * (want_dy - dy);
        if (err < best_err) { best_err = err; best_pitch = a; }
    }
    *out_yaw   = (best_yaw + 360) % 360;
    *out_pitch = (best_pitch + 360) % 360;
}

/* Per-enemy AI — called for each team==1 entity that's alive.
 * Rotates the ship toward the player, fires when facing us
 * within a loose cone. Uses a small state kept in Entity.hp
 * (reused as a fire cooldown for now). */
/* 3DO port (C89): was a static local declared mid-block */
static UWORD cooldowns[8];

static void tick_enemy(Entity *e, const Camera *cam, Combat *c,
                       ULONG *rng)
{
    /* Vector to player. */
    LONG dx = cam->x - e->x;
    LONG dy = cam->y - e->y;
    LONG dz = cam->z - e->z;
    LONG dist = isqrt_len(dx * dx + dy * dy + dz * dz);

    /* Look at the player. Turn max 3° per frame in yaw + pitch. */
    LONG target_yaw, target_pitch;
    LONG mvx, mvz;
    UWORD slot;
    look_angles(dx, dy, dz, &target_yaw, &target_pitch);
    e->yaw   = turn_toward(e->yaw,   target_yaw,   3);
    e->pitch = turn_toward(e->pitch, target_pitch, 3);

    /* Move toward the player at fixed speed if further than 4000
     * units, otherwise strafe (perpendicular in the horizontal
     * plane) so the fight has some dynamism. */
    if (dist > 4000) {
        mvx = (dx * 30) / dist;
        mvz = (dz * 30) / dist;
    } else {
        mvx = (-dz * 20) / dist;
        mvz = ( dx * 20) / dist;
    }
    e->x += mvx;
    e->z += mvz;

    /* Fire cooldown reuses the .hp slot's high bits — we keep hp
     * in the low byte, cooldown in a static per-slot map keyed off
     * pointer. Simpler: use a static array of cooldowns since we
     * cap enemies at MAX in Phase 2. */
    (void)rng;
    slot = (UWORD)(((ULONG)e >> 4) & 7); /* cheap hash */
    if (cooldowns[slot] > 0) cooldowns[slot]--;
    if (cooldowns[slot] == 0 && dist < 8000 && dist > 200) {
        /* Fire — aim the bolt at where the player is right now.
         * Bolt spawns just in front of the enemy along its yaw. */
        LONG len = isqrt_len(dx * dx + dy * dy + dz * dz);
        LONG bvx = (dx * VT_BULLET_SPEED) / len;
        LONG bvy = (dy * VT_BULLET_SPEED) / len;
        LONG bvz = (dz * VT_BULLET_SPEED) / len;
        spawn_bullet(c, e->x, e->y, e->z, bvx, bvy, bvz, 1);
        cooldowns[slot] = VT_ENEMY_COOL;
    }
}

void vt_combat_tick(Combat *c, Camera *cam,
                    Entity *entities, int num_entities,
                    UWORD input_flags)
{
    int i, j;
    static ULONG rng = 0xB00B1E5UL;

    if (c->game_over) return;

    /* Player fire cooldown + trigger. */
    if (c->player_cooldown > 0) c->player_cooldown--;
    if ((input_flags & VT_IN_FIRE) && c->player_cooldown == 0) {
        LONG fx, fy, fz;
        LONG bvx;
        LONG bvy;
        LONG bvz;
        LONG sx;
        LONG sy;
        LONG sz;
        cam_forward(cam, &fx, &fy, &fz);
        bvx = (fx * VT_BULLET_SPEED) >> FP;
        bvy = (fy * VT_BULLET_SPEED) >> FP;
        bvz = (fz * VT_BULLET_SPEED) >> FP;
        /* Spawn a bit ahead of the camera so the muzzle flash
         * doesn't spawn inside our own cockpit hitsphere. */
        sx = cam->x + ((fx * 500) >> FP);
        sy = cam->y + ((fy * 500) >> FP);
        sz = cam->z + ((fz * 500) >> FP);
        spawn_bullet(c, sx, sy, sz, bvx, bvy, bvz, 0);
        c->player_cooldown = VT_PLAYER_COOL;
        vt_sfx_play(SFX_LASER);
    }

    /* Enemy AI. Skip entity 0 (player marker, unused for now). */
    for (i = 0; i < num_entities; i++) {
        Entity *e = &entities[i];
        if (!e->active) continue;
        if (e->team == 1 && e->hp > 0) tick_enemy(e, cam, c, &rng);
    }

    /* Advance bullets + collision. */
    for (i = 0; i < VT_MAX_BULLETS; i++) {
        Bullet *b = &c->bullets[i];
        if (b->owner == 255) continue;
        b->x += b->vx;
        b->y += b->vy;
        b->z += b->vz;
        if (b->life > 0) b->life--;
        if (b->life == 0) { b->owner = 255; continue; }

        if (b->owner == 0) {
            /* Player bolt: check against every enemy team==1. */
            for (j = 0; j < num_entities; j++) {
                Entity *e = &entities[j];
                LONG dx;
                LONG dy;
                LONG dz;
                if (!e->active || e->team != 1 || e->hp == 0) continue;
                dx = b->x - e->x;
                dy = b->y - e->y;
                dz = b->z - e->z;
                if (dx * dx + dy * dy + dz * dz
                    < (LONG)VT_HIT_RADIUS * (LONG)VT_HIT_RADIUS) {
                    b->owner = 255;
                    if (e->hp > 0) e->hp--;
                    if (e->hp == 0) {
                        e->active = 0;
                        c->score += 100;
                        vt_sfx_play(SFX_EXPLOSION);
                    }
                    break;
                }
            }
        } else if (b->owner == 1) {
            /* Enemy bolt: check against player. */
            LONG dx = b->x - cam->x;
            LONG dy = b->y - cam->y;
            LONG dz = b->z - cam->z;
            if (dx * dx + dy * dy + dz * dz
                < (LONG)VT_HIT_RADIUS * (LONG)VT_HIT_RADIUS) {
                b->owner = 255;
                c->player_energy -= VT_ENEMY_DAMAGE;
                if (c->player_energy <= 0) {
                    c->player_energy = 0;
                    c->game_over = 1;
                    vt_sfx_play(SFX_EXPLOSION);
                } else {
                    vt_sfx_play(SFX_HIT);
                }
            }
        }
    }
    (void)rng;
}

/* Render each active bullet as a short line segment: project both
 * ends and Move/Draw between them if in front of the camera. */
void vt_combat_render(struct RastPort *rp, const Combat *c,
                      const Camera *cam)
{
    int i;
    for (i = 0; i < VT_MAX_BULLETS; i++) {
        const Bullet *b = &c->bullets[i];
        int sx0, sy0, sx1, sy1;
        LONG d0, d1;
        LONG hx, hy, hz;
        LONG tx;
        LONG ty;
        LONG tz;
        if (b->owner == 255) continue;
        /* Segment tail = position behind the current point, so the
         * bolt visually leads with its head. */
        hx = b->x; hy = b->y; hz = b->z;
        tx = b->x - b->vx / 3;
        ty = b->y - b->vy / 3;
        tz = b->z - b->vz / 3;
        if (!e3d_project(cam, hx, hy, hz, &sx0, &sy0, &d0)) continue;
        if (!e3d_project(cam, tx, ty, tz, &sx1, &sy1, &d1)) continue;
        SetAPen(rp, (UBYTE)(b->owner == 0 ? PEN_PLAYER_BOLT
                                          : PEN_ENEMY_BOLT));
        Move(rp, sx0, sy0);
        Draw(rp, sx1, sy1);
    }
}
