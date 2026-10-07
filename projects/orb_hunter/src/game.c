// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - Core game logic
 */
#include <string.h>
#include "game.h"

/* Tile properties table */
UBYTE tile_props[NUM_TILE_TYPES] = {
    0,                          /* EMPTY */
    TPROP_SOLID,                /* ROCK */
    TPROP_SOLID,                /* BRICK */
    TPROP_SOLID,                /* METAL */
    TPROP_PLATFORM,             /* PLATFORM */
    TPROP_DAMAGE,               /* LAVA */
    TPROP_SOLID | TPROP_DOOR,   /* DOOR_LOCKED */
    0,                          /* DOOR_OPEN */
    TPROP_SOLID,                /* ITEM_BLOCK */
    0                           /* SAVE_POINT */
};

/* --- RNG --- */

static ULONG rng_state = 12345;

static WORD rng(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (WORD)((rng_state >> 16) & 0x7FFF);
}

/* --- Collision helpers --- */

static WORD tile_at(GameState *gs, WORD tx, WORD ty)
{
    if (tx < 0 || tx >= ROOM_W || ty < 0 || ty >= ROOM_H)
        return TILE_ROCK; /* out of bounds = solid */
    return world_rooms[gs->room_y][gs->room_x][ty][tx];
}

static WORD tile_is_solid(GameState *gs, WORD tx, WORD ty)
{
    UBYTE t = tile_at(gs, tx, ty);
    return (tile_props[t] & TPROP_SOLID) ? 1 : 0;
}

static WORD tile_is_platform(GameState *gs, WORD tx, WORD ty)
{
    UBYTE t = tile_at(gs, tx, ty);
    return (tile_props[t] & TPROP_PLATFORM) ? 1 : 0;
}

/* --- Enemy spawn data per room --- */

typedef struct {
    WORD room_x, room_y;
    WORD tile_x, tile_y;
    WORD type;
} EnemySpawn;

static EnemySpawn room_enemies[] = {
    /* Cavern rooms */
    { 1, 1,  5, 13, ENEMY_CRAWLER }, { 1, 1, 14, 13, ENEMY_FLYER },
    { 2, 1,  8, 13, ENEMY_CRAWLER }, { 2, 1, 15, 10, ENEMY_HOPPER },
    { 3, 1,  6, 13, ENEMY_CRAWLER }, { 3, 1, 12, 13, ENEMY_TURRET },
    { 1, 2,  5, 13, ENEMY_CRAWLER }, { 1, 2, 16, 13, ENEMY_CRAWLER },
    { 3, 2,  4, 13, ENEMY_HOPPER },  { 3, 2, 15, 13, ENEMY_FLYER },
    { 1, 3,  8, 13, ENEMY_CRAWLER }, { 1, 3, 14, 10, ENEMY_TURRET },
    { 2, 3, 10, 13, ENEMY_HOPPER },  { 2, 3, 16, 13, ENEMY_CRAWLER },
    /* Magma rooms */
    { 5, 1,  6, 12, ENEMY_FLYER },   { 5, 1, 14, 12, ENEMY_CRAWLER },
    { 6, 1,  8, 12, ENEMY_TURRET },  { 6, 1, 15, 12, ENEMY_FLYER },
    { 5, 2,  5, 12, ENEMY_HOPPER },  { 5, 2, 12, 12, ENEMY_CRAWLER },
    { 6, 2,  7, 10, ENEMY_FLYER },   { 6, 2, 14, 12, ENEMY_TURRET },
    { 5, 3,  6, 12, ENEMY_CRAWLER }, { 5, 3, 13, 12, ENEMY_HOPPER },
    /* Corridor rooms */
    { 4, 1,  8, 13, ENEMY_CRAWLER }, { 4, 2, 10, 13, ENEMY_FLYER },
    { 3, 3,  8, 13, ENEMY_CRAWLER },
    /* Citadel rooms */
    { 3, 4,  6, 13, ENEMY_TURRET },  { 3, 4, 14, 13, ENEMY_CRAWLER },
    { 4, 4,  5, 13, ENEMY_HOPPER },  { 4, 4, 15, 13, ENEMY_TURRET },
    { 5, 4,  8, 13, ENEMY_FLYER },   { 5, 4, 14, 13, ENEMY_CRAWLER },
    /* Roll form area */
    { 1, 4,  8,  9, ENEMY_CRAWLER },
    /* Boss rooms */
    { 4, 3, 10,  8, ENEMY_BOSS_BEAST },
    { 6, 3, 10,  6, ENEMY_BOSS_DRAKE },
    /* Sentinel */
    { -1, -1, -1, -1, -1 }
};

/* --- Init --- */

void game_init(GameState *gs)
{
    WORD i;

    memset(gs, 0, sizeof(GameState));

    /* Regenerate all mutable world state so opened doors, destroyed tiles,
       and every prior collected item are reset for the new game. */
    levels_init();
    for (i = 0; i < g_num_items; i++) {
        g_items[i].collected = 0;
    }

    /* Start in room (2,2) - the starting area */
    gs->room_x = 2;
    gs->room_y = 2;

    /* Init hunter */
    gs->hunter.x = FIX(5 * TILE_W);
    /* Place hunter on the ground floor - floor is 2 tiles thick at bottom,
       so top of floor = (ROOM_H-2)*TILE_H. Hunter bottom should be there. */
    gs->hunter.y = FIX((ROOM_H - 2) * TILE_H - HUNTER_H);
    gs->hunter.dy = FIX(1); /* give a tiny push down so collision snaps us */
    gs->hunter.alive = 1;
    gs->hunter.health = START_ENERGY;
    gs->hunter.max_energy = START_ENERGY;
    gs->hunter.max_missiles = 0;
    gs->hunter.facing = 0;
    gs->hunter.invuln_timer = INVULN_TIME;

    gs->state = STATE_PLAYING;
    game_load_room(gs);
}

void game_load_room(GameState *gs)
{
    WORD i;
    WORD ei = 0;

    /* Clear entities */
    for (i = 0; i < MAX_BULLETS; i++) gs->bullets[i].active = 0;
    for (i = 0; i < MAX_BOMBS; i++) gs->bombs[i].active = 0;
    for (i = 0; i < MAX_ENEMIES; i++) gs->enemies[i].active = 0;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) gs->enemy_bullets[i].active = 0;
    for (i = 0; i < MAX_PARTICLES; i++) gs->particles[i].active = 0;

    /* Spawn enemies for this room */
    for (i = 0; room_enemies[i].room_x >= 0 && ei < MAX_ENEMIES; i++) {
        if (room_enemies[i].room_x == gs->room_x &&
            room_enemies[i].room_y == gs->room_y) {
            Enemy *e = &gs->enemies[ei];
            e->x = FIX(room_enemies[i].tile_x * TILE_W);
            e->y = FIX(room_enemies[i].tile_y * TILE_H);
            e->type = room_enemies[i].type;
            e->active = 1;
            e->facing = 1;
            e->state = 0;
            e->timer = 0;
            e->anim_frame = 0;
            e->fire_timer = 0;

            switch (e->type) {
                case ENEMY_CRAWLER:     e->health = 3;  break;
                case ENEMY_FLYER:       e->health = 4;  break;
                case ENEMY_HOPPER:      e->health = 3;  break;
                case ENEMY_TURRET:      e->health = 5;  break;
                case ENEMY_BOSS_BEAST:  e->health = 20; break;
                case ENEMY_BOSS_DRAKE: e->health = 30; break;
            }
            ei++;
        }
    }
}

/* --- Hunter update --- */

static void update_hunter(GameState *gs, WORD inp_left, WORD inp_right,
                          WORD inp_up, WORD inp_down,
                          WORD inp_jump, WORD inp_fire)
{
    Hunter *h = &gs->hunter;
    WORD cur_h;

    if (!h->alive) return;

    /* Invulnerability countdown */
    if (h->invuln_timer > 0) h->invuln_timer--;

    /* Gun cooldown */
    if (h->gun_timer > 0) h->gun_timer--;

    /* Bomb timer */
    if (h->bomb_timer > 0) h->bomb_timer--;

    cur_h = h->rolled ? HUNTER_ROLL_H : HUNTER_H;

    /* Roll form toggle: press down when on ground and have roll form */
    if (inp_down && h->on_ground && h->has_roll_form && !inp_left && !inp_right) {
        if (!h->rolled) {
            h->rolled = 1;
            /* Adjust Y so hunter doesn't float */
            h->y += FIX(HUNTER_H - HUNTER_ROLL_H);
        }
    }
    /* Un-roll: press up when rolled */
    if (inp_up && h->rolled && h->on_ground) {
        /* Check ceiling clearance */
        WORD tx1 = FIX_INT(h->x) / TILE_W;
        WORD tx2 = (FIX_INT(h->x) + HUNTER_W - 1) / TILE_W;
        WORD check_y = (FIX_INT(h->y) - (HUNTER_H - HUNTER_ROLL_H)) / TILE_H;
        if (!tile_is_solid(gs, tx1, check_y) && !tile_is_solid(gs, tx2, check_y)) {
            h->y -= FIX(HUNTER_H - HUNTER_ROLL_H);
            h->rolled = 0;
        }
    }

    /* Horizontal movement */
    if (inp_left) {
        h->dx = -HUNTER_SPEED;
        h->facing = 1;
    } else if (inp_right) {
        h->dx = HUNTER_SPEED;
        h->facing = 0;
    } else {
        h->dx = 0;
    }

    /* Jump (not when rolled) */
    if (inp_jump && h->on_ground && !h->jump_held && !h->rolled) {
        if (h->has_high_jump) {
            h->dy = HUNTER_HIGH_JUMP;
        } else {
            h->dy = HUNTER_JUMP_VEL;
        }
        h->on_ground = 0;
        h->jump_held = 1;
        sfx_jump();
    }
    if (!inp_jump) {
        h->jump_held = 0;
        if (h->dy < 0) {
            h->dy = h->dy / 2;
        }
    }

    /* Gravity */
    h->dy += HUNTER_GRAVITY;
    if (h->dy > HUNTER_MAX_FALL) h->dy = HUNTER_MAX_FALL;

    /* Horizontal collision */
    {
        Fixed new_x = h->x + h->dx;
        WORD px_left, px_right, px_top, px_bot;
        WORD tx_l, tx_r, ty_t, ty_b;

        /* Only clamp the right edge here; leaving new_x negative lets
           check_room_transition() detect a left-room transition. */
        if (FIX_INT(new_x) + HUNTER_W > ROOM_W * TILE_W)
            new_x = FIX(ROOM_W * TILE_W - HUNTER_W);

        if (new_x < 0) {
            /* Moving off left edge: let position stand for the transition
               check but skip tile collision on negative coords. */
            h->x = new_x;
        } else {
            px_left = FIX_INT(new_x);
            px_right = px_left + HUNTER_W - 1;
            px_top = FIX_INT(h->y);
            px_bot = px_top + cur_h - 1;

            tx_l = px_left / TILE_W;
            tx_r = px_right / TILE_W;
            ty_t = px_top / TILE_H;
            ty_b = px_bot / TILE_H;

            if (tile_is_solid(gs, tx_l, ty_t) || tile_is_solid(gs, tx_l, ty_b) ||
                tile_is_solid(gs, tx_r, ty_t) || tile_is_solid(gs, tx_r, ty_b)) {
                h->dx = 0;
            } else {
                h->x = new_x;
            }
        }
    }

    /* Vertical collision */
    {
        Fixed new_y = h->y + h->dy;
        WORD px_left, px_right, px_top, px_bot;
        WORD tx_l, tx_r, ty_t, ty_b;

        px_left = FIX_INT(h->x);
        px_right = px_left + HUNTER_W - 1;
        px_top = FIX_INT(new_y);
        px_bot = px_top + cur_h - 1;

        tx_l = px_left / TILE_W;
        tx_r = px_right / TILE_W;
        ty_t = px_top / TILE_H;
        ty_b = px_bot / TILE_H;

        h->on_ground = 0;

        if (h->dy >= 0) {
            /* Falling - check floor: which tile row are the feet in? */
            if (tile_is_solid(gs, tx_l, ty_b) || tile_is_solid(gs, tx_r, ty_b) ||
                tile_is_platform(gs, tx_l, ty_b) || tile_is_platform(gs, tx_r, ty_b)) {
                /* Snap: place hunter so bottom pixel is 1 above the tile top */
                h->y = FIX(ty_b * TILE_H - cur_h);
                h->dy = 0;
                h->on_ground = 1;
            } else {
                h->y = new_y;
            }
        } else {
            /* Rising - let a negative new_y through so check_room_transition()
               can detect a top-room exit before we clamp against the ceiling. */
            if (px_top < 0) {
                h->y = new_y;
            } else if (tile_is_solid(gs, tx_l, ty_t) || tile_is_solid(gs, tx_r, ty_t)) {
                h->y = FIX((ty_t + 1) * TILE_H);
                h->dy = 0;
            } else {
                h->y = new_y;
            }
        }
    }

    /* Lava damage */
    {
        WORD px = FIX_INT(h->x) + HUNTER_W / 2;
        WORD py = FIX_INT(h->y) + cur_h;
        WORD tx = px / TILE_W;
        WORD ty = py / TILE_H;
        UBYTE tt = tile_at(gs, tx, ty);
        if ((tile_props[tt] & TPROP_DAMAGE) && h->invuln_timer == 0) {
            WORD dmg = h->has_heat_suit ? 1 : 5;
            h->health -= dmg;
            h->invuln_timer = INVULN_TIME / 2;
            sfx_player_hit();
            if (h->health <= 0) {
                h->alive = 0;
                gs->state = STATE_DEAD;
                gs->state_timer = 120;
                game_spawn_particles(gs, h->x, h->y, 16, 8);
                sfx_explode();
            }
        }
    }

    /* Fire weapon */
    if (inp_fire && h->gun_timer == 0) {
        if (h->rolled) {
            /* Lay bomb if rolled and has bombs */
            if (h->has_bombs && h->bomb_timer == 0) {
                WORD bi;
                for (bi = 0; bi < MAX_BOMBS; bi++) {
                    if (!gs->bombs[bi].active) {
                        gs->bombs[bi].x = h->x + FIX(HUNTER_W / 2);
                        gs->bombs[bi].y = h->y + FIX(HUNTER_ROLL_H / 2);
                        gs->bombs[bi].timer = BOMB_FUSE;
                        gs->bombs[bi].active = 1;
                        h->bomb_timer = 15;
                        break;
                    }
                }
            }
        } else {
            /* Shoot beam or missile */
            WORD use_missile = (inp_up && h->has_missiles && h->missiles > 0);
            WORD bi;
            for (bi = 0; bi < MAX_BULLETS; bi++) {
                if (!gs->bullets[bi].active) {
                    Bullet *b = &gs->bullets[bi];
                    b->x = h->x + (h->facing ? 0 : FIX(HUNTER_W));
                    b->y = h->y + FIX(6);
                    if (use_missile) {
                        b->dx = h->facing ? -MISSILE_SPEED : MISSILE_SPEED;
                        b->type = BULLET_TYPE_MISSILE;
                        b->power = 5;
                        h->missiles--;
                        sfx_missile();
                    } else {
                        Fixed spd = BULLET_SPEED;
                        b->dx = h->facing ? -spd : spd;
                        if (h->has_ice_beam) {
                            b->type = BULLET_TYPE_ICE;
                            b->power = 2;
                        } else {
                            b->type = BULLET_TYPE_NORMAL;
                            b->power = h->has_long_beam ? 2 : 1;
                        }
                        sfx_shoot();
                    }
                    b->dy = 0;
                    b->life = h->has_long_beam ? 45 : BULLET_LIFE;
                    b->active = 1;
                    h->gun_timer = GUN_COOLDOWN;
                    break;
                }
            }
        }
    }

    /* Animation */
    if (h->rolled) {
        h->anim_frame = (gs->frame / 3) & 3;
    } else if (h->dx != 0) {
        h->anim_frame = (gs->frame / 4) & 3;
    } else {
        h->anim_frame = 0;
    }
}

/* --- Room transitions --- */

static void check_room_transition(GameState *gs)
{
    Hunter *h = &gs->hunter;
    WORD cur_h = h->rolled ? HUNTER_ROLL_H : HUNTER_H;

    /* Right edge */
    if (FIX_INT(h->x) + HUNTER_W >= ROOM_W * TILE_W) {
        if (gs->room_x < WORLD_W - 1) {
            gs->room_x++;
            h->x = FIX(2);
            game_load_room(gs);
        } else {
            h->x = FIX(ROOM_W * TILE_W - HUNTER_W - 1);
        }
        return;
    }

    /* Left edge */
    if (FIX_INT(h->x) < 0) {
        if (gs->room_x > 0) {
            gs->room_x--;
            h->x = FIX(ROOM_W * TILE_W - HUNTER_W - 2);
            game_load_room(gs);
        } else {
            h->x = 0;
        }
        return;
    }

    /* Bottom edge */
    if (FIX_INT(h->y) + cur_h >= ROOM_H * TILE_H) {
        if (gs->room_y < WORLD_H - 1) {
            gs->room_y++;
            h->y = FIX(2);
            game_load_room(gs);
        } else {
            h->y = FIX(ROOM_H * TILE_H - cur_h - 1);
        }
        return;
    }

    /* Top edge */
    if (FIX_INT(h->y) < 0) {
        if (gs->room_y > 0) {
            gs->room_y--;
            h->y = FIX(ROOM_H * TILE_H - cur_h - 2);
            game_load_room(gs);
        } else {
            h->y = 0;
        }
        return;
    }
}

/* --- Bullet update --- */

static void update_bullets(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &gs->bullets[i];
        if (!b->active) continue;

        b->x += b->dx;
        b->y += b->dy;
        b->life--;

        if (b->life <= 0) { b->active = 0; continue; }

        /* Tile collision */
        {
            WORD px = FIX_INT(b->x);
            WORD py = FIX_INT(b->y);
            WORD tx = px / TILE_W;
            WORD ty = py / TILE_H;
            UBYTE tt = tile_at(gs, tx, ty);

            /* Missile opens locked doors */
            if ((tile_props[tt] & TPROP_DOOR) && b->type == BULLET_TYPE_MISSILE) {
                world_rooms[gs->room_y][gs->room_x][ty][tx] = TILE_DOOR_OPEN;
                b->active = 0;
                sfx_door();
                game_spawn_particles(gs, b->x, b->y, 6, 4);
                continue;
            }
            if (tile_props[tt] & TPROP_SOLID) {
                b->active = 0;
                game_spawn_particles(gs, b->x, b->y, 3, 10);
            }
        }

        /* Off room bounds */
        if (FIX_INT(b->x) < 0 || FIX_INT(b->x) >= ROOM_W * TILE_W ||
            FIX_INT(b->y) < 0 || FIX_INT(b->y) >= ROOM_H * TILE_H) {
            b->active = 0;
        }
    }
}

/* --- Bomb update --- */

static void update_bombs(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_BOMBS; i++) {
        Bomb *bm = &gs->bombs[i];
        if (!bm->active) continue;

        bm->timer--;
        if (bm->timer <= 0) {
            /* Detonate */
            WORD j;
            bm->active = 0;
            sfx_explode();
            game_spawn_particles(gs, bm->x, bm->y, 10, 9);

            /* Damage nearby enemies */
            for (j = 0; j < MAX_ENEMIES; j++) {
                Enemy *e = &gs->enemies[j];
                WORD edx, edy;
                if (!e->active) continue;
                edx = FIX_INT(bm->x) - FIX_INT(e->x) - 8;
                edy = FIX_INT(bm->y) - FIX_INT(e->y) - 8;
                if (edx > -BOMB_RADIUS && edx < BOMB_RADIUS &&
                    edy > -BOMB_RADIUS && edy < BOMB_RADIUS) {
                    e->health -= 3;
                }
            }

            /* Bomb jump: push hunter up if nearby */
            {
                WORD hdx = FIX_INT(bm->x) - FIX_INT(gs->hunter.x) - HUNTER_W/2;
                WORD hdy = FIX_INT(bm->y) - FIX_INT(gs->hunter.y) - HUNTER_ROLL_H/2;
                if (hdx > -BOMB_RADIUS && hdx < BOMB_RADIUS &&
                    hdy > -BOMB_RADIUS && hdy < BOMB_RADIUS) {
                    gs->hunter.dy = FIX(-3);
                }
            }
        }
    }
}

/* --- Enemy AI --- */

static void enemy_fire(GameState *gs, Enemy *e)
{
    WORD i;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!gs->enemy_bullets[i].active) {
            Fixed dx;
            if (gs->hunter.x < e->x) dx = -FIX(2);
            else dx = FIX(2);

            gs->enemy_bullets[i].x = e->x + FIX(8);
            gs->enemy_bullets[i].y = e->y + FIX(4);
            gs->enemy_bullets[i].dx = dx;
            gs->enemy_bullets[i].dy = 0;
            gs->enemy_bullets[i].life = 50;
            gs->enemy_bullets[i].active = 1;
            break;
        }
    }
}

static void update_enemies(GameState *gs)
{
    WORD i;

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        if (!e->active) continue;

        /* Kill dead enemies */
        if (e->health <= 0) {
            e->active = 0;
            sfx_explode();
            game_spawn_particles(gs, e->x + FIX(8), e->y + FIX(8), 10, 9);
            gs->score += (e->type >= ENEMY_BOSS_BEAST) ? 2000 : 100;
            continue;
        }

        e->timer++;
        e->fire_timer++;

        switch (e->type) {
            case ENEMY_CRAWLER:
                e->dx = e->facing ? -FIX(1) : FIX(1);
                e->x += e->dx;
                /* Wall check */
                {
                    WORD px = FIX_INT(e->x) + (e->facing ? 0 : 15);
                    WORD py = FIX_INT(e->y) + 8;
                    if (tile_is_solid(gs, px / TILE_W, py / TILE_H))
                        e->facing ^= 1;
                }
                /* Edge check */
                {
                    WORD px = FIX_INT(e->x) + (e->facing ? 0 : 15);
                    WORD py = FIX_INT(e->y) + 17;
                    if (!tile_is_solid(gs, px / TILE_W, py / TILE_H) &&
                        !tile_is_platform(gs, px / TILE_W, py / TILE_H))
                        e->facing ^= 1;
                }
                e->anim_frame = (gs->frame / 6) & 3;
                break;

            case ENEMY_FLYER:
                e->dx = e->facing ? -FIX(1) : FIX(1);
                e->x += e->dx;
                /* Sine bob */
                {
                    WORD bob = (e->timer * 4) & 0xFF;
                    if (bob < 64) e->dy = FIX(1);
                    else if (bob < 192) e->dy = -FIX(1);
                    else e->dy = FIX(1);
                    e->y += e->dy / 4;
                }
                /* Chase player Y gently */
                if (gs->hunter.y < e->y) e->y -= FIX(1) / 8;
                else e->y += FIX(1) / 8;
                /* Bounds */
                if (FIX_INT(e->x) < TILE_W || FIX_INT(e->x) > (ROOM_W - 2) * TILE_W)
                    e->facing ^= 1;
                /* Shoot */
                if (e->fire_timer > 80) {
                    enemy_fire(gs, e);
                    e->fire_timer = 0;
                }
                e->anim_frame = (gs->frame / 4) & 3;
                break;

            case ENEMY_HOPPER:
                if (e->state == 0) {
                    /* On ground, waiting */
                    if (e->timer > 30) {
                        e->facing = (gs->hunter.x < e->x) ? 1 : 0;
                        e->dx = e->facing ? -FIX(2) : FIX(2);
                        e->dy = -FIX(3);
                        e->state = 1;
                        e->timer = 0;
                    }
                } else {
                    /* In air */
                    e->x += e->dx;
                    e->dy += HUNTER_GRAVITY;
                    e->y += e->dy;
                    /* Landing */
                    {
                        WORD py = FIX_INT(e->y) + 15;
                        WORD px = FIX_INT(e->x) + 8;
                        if (e->dy > 0 &&
                            (tile_is_solid(gs, px / TILE_W, py / TILE_H) ||
                             tile_is_platform(gs, px / TILE_W, py / TILE_H))) {
                            e->y = FIX((py / TILE_H) * TILE_H - 16);
                            e->dy = 0;
                            e->dx = 0;
                            e->state = 0;
                            e->timer = 0;
                        }
                    }
                }
                e->anim_frame = (e->state == 1) ? 1 : 0;
                break;

            case ENEMY_TURRET:
                e->facing = (gs->hunter.x < e->x) ? 1 : 0;
                if (e->fire_timer > 50) {
                    enemy_fire(gs, e);
                    e->fire_timer = 0;
                }
                e->anim_frame = (gs->frame / 8) & 1;
                break;

            case ENEMY_BOSS_BEAST:
                /* Slow movement, fires spread projectiles */
                e->dx = (e->state == 0) ? FIX(1) / 2 : -FIX(1) / 2;
                if (e->timer > 80) { e->state ^= 1; e->timer = 0; }
                e->x += e->dx;
                { WORD fr = 25 + e->health;
                  if (fr < 20) fr = 20;
                  if (e->fire_timer > fr) {
                    WORD k;
                    for (k = 0; k < 3; k++) { WORD bi;
                      for (bi = 0; bi < MAX_ENEMY_BULLETS; bi++) {
                        if (!gs->enemy_bullets[bi].active) {
                          Fixed adx = (gs->hunter.x < e->x) ? -FIX(2) : FIX(2);
                          gs->enemy_bullets[bi].x = e->x + FIX(8);
                          gs->enemy_bullets[bi].y = e->y + FIX(k * 10);
                          gs->enemy_bullets[bi].dx = adx;
                          gs->enemy_bullets[bi].dy = FIX(k - 1);
                          gs->enemy_bullets[bi].life = 50;
                          gs->enemy_bullets[bi].active = 1; break;
                    } } }
                    e->fire_timer = 0;
                } }
                e->anim_frame = (gs->frame / 4) & 3;
                break;

            case ENEMY_BOSS_DRAKE:
                /* Flying boss, swoops at player */
                if (e->state == 0) {
                    e->y += (e->timer & 32) ? FIX(1)/4 : -FIX(1)/4;
                    e->facing = (gs->hunter.x < e->x) ? 1 : 0;
                    if (e->timer > 60) {
                        e->state = 1; e->timer = 0;
                        e->dx = (gs->hunter.x < e->x) ? -FIX(3) : FIX(3);
                        e->dy = FIX(2);
                    }
                    if (e->fire_timer > 40) { enemy_fire(gs, e); e->fire_timer = 0; }
                } else if (e->state == 1) {
                    e->x += e->dx; e->y += e->dy;
                    if (e->timer > 30) { e->state = 2; e->timer = 0; e->dy = -FIX(2); }
                } else {
                    e->x += e->dx / 2; e->y += e->dy;
                    if (e->timer > 20) { e->state = 0; e->timer = 0; e->dx = 0; e->dy = 0; }
                }
                if (FIX_INT(e->x) < TILE_W*2) { e->x = FIX(TILE_W*2); e->dx = -e->dx; }
                if (FIX_INT(e->x) > (ROOM_W-3)*TILE_W) { e->x = FIX((ROOM_W-3)*TILE_W); e->dx = -e->dx; }
                if (FIX_INT(e->y) < TILE_W*2) e->y = FIX(TILE_W*2);
                e->anim_frame = (gs->frame / 3) & 3;
                break;
        }
    }
}

/* --- Enemy bullet update --- */

static void update_enemy_bullets(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        EnemyBullet *b = &gs->enemy_bullets[i];
        if (!b->active) continue;

        b->x += b->dx;
        b->y += b->dy;
        b->life--;

        if (b->life <= 0) { b->active = 0; continue; }

        /* Tile collision */
        {
            WORD px = FIX_INT(b->x);
            WORD py = FIX_INT(b->y);
            if (tile_is_solid(gs, px / TILE_W, py / TILE_H))
                b->active = 0;
        }

        /* Hit player */
        if (gs->hunter.alive && gs->hunter.invuln_timer == 0) {
            WORD cur_h = gs->hunter.rolled ? HUNTER_ROLL_H : HUNTER_H;
            WORD dx = FIX_INT(b->x) - FIX_INT(gs->hunter.x) - HUNTER_W/2;
            WORD dy = FIX_INT(b->y) - FIX_INT(gs->hunter.y) - cur_h/2;
            if (dx > -10 && dx < 10 && dy > -cur_h/2 && dy < cur_h/2) {
                b->active = 0;
                gs->hunter.health -= 5;
                gs->hunter.invuln_timer = INVULN_TIME;
                sfx_player_hit();
                if (gs->hunter.health <= 0) {
                    gs->hunter.alive = 0;
                    gs->state = STATE_DEAD;
                    gs->state_timer = 120;
                    game_spawn_particles(gs, gs->hunter.x, gs->hunter.y, 16, 8);
                    sfx_explode();
                }
            }
        }
    }
}

/* --- Collision: bullets vs enemies --- */

static void check_bullet_enemy(GameState *gs)
{
    WORD i, j;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &gs->bullets[i];
        if (!b->active) continue;

        for (j = 0; j < MAX_ENEMIES; j++) {
            Enemy *e = &gs->enemies[j];
            WORD rad;
            WORD dx, dy;
            if (!e->active) continue;

            dx = FIX_INT(b->x) - FIX_INT(e->x) - 8;
            dy = FIX_INT(b->y) - FIX_INT(e->y) - 8;
            rad = (e->type >= ENEMY_BOSS_BEAST) ? 16 : 10;

            if (dx > -rad && dx < rad && dy > -rad && dy < rad) {
                e->health -= b->power;
                b->active = 0;
                sfx_hit();
                game_spawn_particles(gs, b->x, b->y, 4, 10);
                break;
            }
        }
    }
}

/* --- Collision: hunter vs enemies --- */

static void check_hunter_enemy(GameState *gs)
{
    WORD i;
    Hunter *h = &gs->hunter;
    WORD cur_h;

    if (!h->alive || h->invuln_timer > 0) return;

    cur_h = h->rolled ? HUNTER_ROLL_H : HUNTER_H;

    /* Spin strike kills enemies on contact */
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        WORD dx, dy;
        if (!e->active) continue;

        dx = FIX_INT(h->x) + HUNTER_W/2 - FIX_INT(e->x) - 8;
        dy = FIX_INT(h->y) + cur_h/2 - FIX_INT(e->y) - 8;
        if (dx > -14 && dx < 14 && dy > -cur_h/2 && dy < cur_h/2) {
            if (h->has_spin_strike && !h->on_ground && !h->rolled) {
                /* Spin strike kills on contact */
                e->health = 0;
                sfx_explode();
                game_spawn_particles(gs, e->x + FIX(8), e->y + FIX(8), 10, 9);
            } else {
                h->health -= 5;
                h->invuln_timer = INVULN_TIME;
                sfx_player_hit();
                if (h->health <= 0) {
                    h->alive = 0;
                    gs->state = STATE_DEAD;
                    gs->state_timer = 120;
                    game_spawn_particles(gs, h->x, h->y, 16, 8);
                    sfx_explode();
                }
            }
            break;
        }
    }
}

/* --- Item collection --- */

static void check_items(GameState *gs)
{
    WORD i;
    Hunter *h = &gs->hunter;
    WORD cur_h;

    if (!h->alive) return;

    cur_h = h->rolled ? HUNTER_ROLL_H : HUNTER_H;

    for (i = 0; i < g_num_items; i++) {
        ItemSpawn *it = &g_items[i];
        WORD dx, dy;

        if (it->collected) continue;
        if (it->room_x != gs->room_x || it->room_y != gs->room_y) continue;

        dx = FIX_INT(h->x) + HUNTER_W/2 - it->tile_x * TILE_W - TILE_W/2;
        dy = FIX_INT(h->y) + cur_h/2 - it->tile_y * TILE_H - TILE_H/2;

        if (dx > -16 && dx < 16 && dy > -16 && dy < 16) {
            it->collected = 1;
            gs->items_collected |= (1L << i);
            sfx_powerup();
            game_spawn_particles(gs, FIX(it->tile_x * TILE_W),
                                 FIX(it->tile_y * TILE_H), 8, 4);

            switch (it->type) {
                case ITEM_ENERGY_TANK:
                    h->max_energy += 30;
                    h->health = h->max_energy;
                    strcpy(gs->item_name, "ENERGY TANK");
                    break;
                case ITEM_MISSILE_PACK:
                    h->has_missiles = 1;
                    h->max_missiles += 5;
                    h->missiles = h->max_missiles;
                    strcpy(gs->item_name, "MISSILE PACK");
                    break;
                case ITEM_ROLL_FORM:
                    h->has_roll_form = 1;
                    strcpy(gs->item_name, "ROLL FORM");
                    break;
                case ITEM_BOMBS:
                    h->has_bombs = 1;
                    strcpy(gs->item_name, "BOMBS");
                    break;
                case ITEM_LONG_BEAM:
                    h->has_long_beam = 1;
                    strcpy(gs->item_name, "LONG BEAM");
                    break;
                case ITEM_ICE_BEAM:
                    h->has_ice_beam = 1;
                    strcpy(gs->item_name, "ICE BEAM");
                    break;
                case ITEM_HIGH_JUMP:
                    h->has_high_jump = 1;
                    strcpy(gs->item_name, "HIGH JUMP BOOTS");
                    break;
                case ITEM_SPIN_STRIKE:
                    h->has_spin_strike = 1;
                    strcpy(gs->item_name, "SPIN STRIKE");
                    break;
                case ITEM_HEAT_SUIT:
                    h->has_heat_suit = 1;
                    strcpy(gs->item_name, "HEAT SUIT");
                    break;
                default:
                    strcpy(gs->item_name, "UNKNOWN");
                    break;
            }

            gs->state = STATE_ITEM_GET;
            gs->state_timer = 90;
            return;
        }
    }
}

/* --- Particles --- */

void game_spawn_particles(GameState *gs, Fixed x, Fixed y,
                          WORD count, WORD color)
{
    WORD i, spawned = 0;
    for (i = 0; i < MAX_PARTICLES && spawned < count; i++) {
        if (!gs->particles[i].active) {
            gs->particles[i].x = x;
            gs->particles[i].y = y;
            gs->particles[i].dx = (rng() % 512 - 256) * 128;
            gs->particles[i].dy = (rng() % 512 - 256) * 128;
            gs->particles[i].life = 10 + (rng() % 20);
            gs->particles[i].color = color;
            gs->particles[i].active = 1;
            spawned++;
        }
    }
}

static void update_particles(GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        if (!p->active) continue;
        p->x += p->dx;
        p->y += p->dy;
        p->dy += FIX(1) / 8;
        p->life--;
        if (p->life <= 0) p->active = 0;
    }
}

/* --- Main update --- */

void game_update(GameState *gs, WORD inp_left, WORD inp_right,
                 WORD inp_up, WORD inp_down,
                 WORD inp_jump, WORD inp_fire)
{
    gs->frame++;

    switch (gs->state) {
        case STATE_PLAYING:
            update_hunter(gs, inp_left, inp_right, inp_up, inp_down,
                          inp_jump, inp_fire);
            update_bullets(gs);
            update_bombs(gs);
            update_enemies(gs);
            update_enemy_bullets(gs);
            update_particles(gs);
            check_bullet_enemy(gs);
            check_hunter_enemy(gs);
            check_items(gs);
            check_room_transition(gs);
            break;

        case STATE_DEAD:
            update_particles(gs);
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                gs->state = STATE_GAMEOVER;
                gs->state_timer = 180;
            }
            break;

        case STATE_GAMEOVER:
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                gs->state = STATE_TITLE;
            }
            break;

        case STATE_ITEM_GET:
            /* Brief pause when collecting a powerup */
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                gs->state = STATE_PLAYING;
            }
            break;

        case STATE_ROOM_TRANSITION:
            gs->state_timer--;
            if (gs->state_timer <= 0) {
                gs->state = STATE_PLAYING;
            }
            break;
    }
}
