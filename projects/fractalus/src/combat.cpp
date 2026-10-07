// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "combat.h"
#include "game.h"
#include "sfx.h"

extern "C" {
#include "bridge_client.h"
}

extern LONG sin_table[ANGLE_FULL];
static inline LONG isin(LONG a) { return sin_table[a & ANGLE_MASK]; }
static inline LONG icos(LONG a) { return sin_table[(a + ANGLE_QUART) & ANGLE_MASK]; }

static ULONG cseed;
static inline ULONG crng()
{
    cseed = cseed * 1664525UL + 1013904223UL;
    return cseed;
}
static inline LONG crand_range(LONG lo, LONG hi)
{
    return lo + (LONG)(crng() % (ULONG)(hi - lo + 1));
}

void Combat::init(ULONG seed, LONG cam_x, LONG cam_y, LONG cam_z)
{
    cseed = seed ? seed : 0x5AABBCDDUL;
    player_cooldown = 0;
    /* sfx pointer left as-is on reset — bind once at boot. */

    /* Saucers cruise well above the tallest terrain peak so a player
     * flying low in a valley can still see them silhouetted against
     * the sky. cam_y is unused for altitude now but kept for API. */
    (void)cam_y;
    const LONG SAUCER_CRUISE_Y = 550;   /* max peak ≈ 486 */

    for (LONG i = 0; i < MAX_SAUCERS; i++) {
        Saucer &s = saucers[i];
        LONG angle = (i * ANGLE_FULL) / MAX_SAUCERS
                   + crand_range(-256, 256);
        LONG dist  = crand_range(SAUCER_SPAWN_MIN, SAUCER_SPAWN_MAX);
        s.x = cam_x + ((isin(angle) * dist) >> TRIG_SHIFT);
        s.z = cam_z + ((icos(angle) * dist) >> TRIG_SHIFT);
        s.y = SAUCER_CRUISE_Y + crand_range(-30, 60);
        s.state = SS_APPROACHING;
        s.dying_timer = 0;
        /* Grace period on mission start: 6-10 seconds before the first
         * shot. Was crand_range(30, SAUCER_FIRE_COOL) which meant the
         * player could be hit within one second of spawning, before
         * they'd even oriented themselves. */
        s.fire_cooldown = crand_range(180, 300);
        s.hp = 3;
    }

    for (LONG i = 0; i < MAX_BULLETS; i++) {
        bullets[i].owner = BO_INACTIVE;
    }
}

LONG Combat::spawn_bullet(LONG x, LONG y, LONG z,
                          LONG vx, LONG vy, LONG vz, UBYTE owner)
{
    for (LONG i = 0; i < MAX_BULLETS; i++) {
        if (bullets[i].owner == BO_INACTIVE) {
            Bullet &b = bullets[i];
            b.x = x; b.y = y; b.z = z;
            b.vx = vx; b.vy = vy; b.vz = vz;
            b.lifetime = BULLET_LIFETIME;
            b.owner = owner;
            return i;
        }
    }
    return -1;
}

LONG Combat::live_saucer_count() const
{
    LONG n = 0;
    for (LONG i = 0; i < MAX_SAUCERS; i++)
        if (saucers[i].state == SS_APPROACHING
            || saucers[i].state == SS_ATTACKING) n++;
    return n;
}

/* Squared distance helper — avoids the sqrt when just comparing. */
static inline LONG dist2(LONG dx, LONG dy, LONG dz)
{
    return dx * dx + dy * dy + dz * dz;
}

/* Digit-by-digit integer square root of a non-negative LONG. Used
 * in three places for vector normalise; returns 1 on zero input so
 * callers can divide without a guard. */
static inline LONG isqrt_len(LONG d2)
{
    LONG r = 0, bit = 1L << 30, v = d2;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r ? r : 1;
}

void Combat::tick(LONG cam_x, LONG cam_y, LONG cam_z, LONG cam_yaw,
                  UWORD input_flags, LONG *shield_out, LONG *score_out)
{
    /* --- Player firing ---------------------------------------------- */
    if (player_cooldown > 0) player_cooldown--;
    if ((input_flags & INPUT_FIRE) && player_cooldown == 0) {
        /* Auto-aim: find the nearest live saucer and aim the bullet at
         * it. Without this, bullets fly horizontally at ship altitude
         * (~200) while saucers cruise at ~550 — every shot passes
         * hundreds of units below the target and never hits. */
        LONG best_i = -1;
        LONG best_d2 = 0x7FFFFFFF;
        for (LONG i = 0; i < MAX_SAUCERS; i++) {
            const Saucer &s = saucers[i];
            if (s.state != SS_APPROACHING && s.state != SS_ATTACKING) continue;
            LONG dx = s.x - cam_x;
            LONG dy = s.y - cam_y;
            LONG dz = s.z - cam_z;
            LONG d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < best_d2) { best_d2 = d2; best_i = i; }
        }

        LONG bvx, bvy, bvz;
        if (best_i >= 0) {
            const Saucer &s = saucers[best_i];
            LONG dx = s.x - cam_x;
            LONG dy = s.y - cam_y;
            LONG dz = s.z - cam_z;
            LONG len = isqrt_len(best_d2);
            bvx = (dx * BULLET_SPEED) / len;
            bvy = (dy * BULLET_SPEED) / len;
            bvz = (dz * BULLET_SPEED) / len;
        } else {
            /* No live target — fire forward along yaw. */
            LONG sy = isin(cam_yaw);
            LONG cy = icos(cam_yaw);
            bvx = (BULLET_SPEED * sy) >> TRIG_SHIFT;
            bvy = 0;
            bvz = (BULLET_SPEED * cy) >> TRIG_SHIFT;
        }
        spawn_bullet(cam_x, cam_y - 4, cam_z, bvx, bvy, bvz, BO_PLAYER);
        player_cooldown = PLAYER_FIRE_COOL;
        if (sfx) sfx->play(SFX_LASER);
    }

    /* --- Saucer AI -------------------------------------------------- */
    for (LONG i = 0; i < MAX_SAUCERS; i++) {
        Saucer &s = saucers[i];
        if (s.state == SS_INACTIVE) continue;

        if (s.state == SS_DYING) {
            if (s.dying_timer > 0) s.dying_timer--;
            else s.state = SS_INACTIVE;
            continue;
        }

        /* Vector to player. */
        LONG dx = cam_x - s.x;
        LONG dz = cam_z - s.z;
        LONG d2 = dx * dx + dz * dz;

        /* Ease into attack range at SAUCER_SPEED. */
        LONG horiz_len = isqrt_len(d2);

        if (horiz_len > SAUCER_ATTACK_DIST) {
            /* Move toward player. */
            s.x += (dx * SAUCER_SPEED) / horiz_len;
            s.z += (dz * SAUCER_SPEED) / horiz_len;
            s.state = SS_APPROACHING;
        } else {
            /* Orbit-ish: strafe perpendicular. */
            LONG px = -dz, pz = dx;
            s.x += (px * (SAUCER_SPEED / 2)) / horiz_len;
            s.z += (pz * (SAUCER_SPEED / 2)) / horiz_len;
            s.state = SS_ATTACKING;
        }

        /* Fire at player if in range. */
        if (s.fire_cooldown > 0) s.fire_cooldown--;
        if (s.state == SS_ATTACKING && s.fire_cooldown == 0) {
            /* Aim at the player. */
            LONG dy = cam_y - s.y;
            LONG total_len = isqrt_len(d2 + dy * dy);
            LONG bvx = (dx * BULLET_SPEED) / total_len;
            LONG bvy = (dy * BULLET_SPEED) / total_len;
            LONG bvz = (dz * BULLET_SPEED) / total_len;
            spawn_bullet(s.x, s.y, s.z, bvx, bvy, bvz, BO_ENEMY);
            s.fire_cooldown = SAUCER_FIRE_COOL + (LONG)(crng() % 40);
        }
    }

    /* --- Bullet update + collisions --------------------------------- */
    for (LONG i = 0; i < MAX_BULLETS; i++) {
        Bullet &b = bullets[i];
        if (b.owner == BO_INACTIVE) continue;

        b.x += b.vx;
        b.y += b.vy;
        b.z += b.vz;
        if (b.lifetime > 0) b.lifetime--;
        if (b.lifetime == 0) { b.owner = BO_INACTIVE; continue; }

        if (b.owner == BO_PLAYER) {
            /* Check hit against each live saucer. */
            for (LONG s = 0; s < MAX_SAUCERS; s++) {
                Saucer &sc = saucers[s];
                if (sc.state != SS_APPROACHING
                    && sc.state != SS_ATTACKING) continue;
                LONG dx = b.x - sc.x;
                LONG dy = b.y - sc.y;
                LONG dz = b.z - sc.z;
                /* Generous 150-unit collision — the bullets are only a
                 * few pixels wide once projected and demanding pixel-
                 * perfect aim on a 2-4 FPS raycaster is cruel. */
                if (dist2(dx, dy, dz) < 150L * 150L) {
                    sc.hp--;
                    b.owner = BO_INACTIVE;
                    if (sc.hp <= 0) {
                        sc.state = SS_DYING;
                        sc.dying_timer = 12;
                        if (score_out) *score_out += 300;
                        if (sfx) sfx->play(SFX_EXPLOSION);
                    }
                    break;
                }
            }
        } else if (b.owner == BO_ENEMY) {
            /* Check hit against player. */
            LONG dx = b.x - cam_x;
            LONG dy = b.y - cam_y;
            LONG dz = b.z - cam_z;
            if (dist2(dx, dy, dz) < 60L * 60L) {
                if (shield_out) {
                    *shield_out -= ENEMY_HIT_DAMAGE;
                    if (*shield_out < 0) *shield_out = 0;
                }
                b.owner = BO_INACTIVE;
                if (sfx) sfx->play(SFX_DAMAGE);
            }
        }
    }
}
