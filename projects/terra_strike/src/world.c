// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * world.c - Terra Strike simulation: the craft, enemies, shots, particles,
 * levels. One call to world_update() advances one frame. Everything the
 * platform layer needs to react to (sounds, log lines) comes out as events.
 */
#include "ts.h"

#define CLEAR        20          /* craft height above ground, world units */
#define CEILING      1900
#define LAND_VY      (-44)       /* fine/frame: slowest safe touchdown */
#define LAND_SPEED   64          /* fine/frame */
#define DIVE_VY      (-40)
#define CLIMB_VY     72
#define SHOT_SPEED   320
#define SHOT_DROP    40
#define SHELL_SPEED  200
#define FUEL_MAX     1000
#define SHIELD_MAX   100

#define F(v)  ((int32_t)(v) << FBITS)
#define FMASK ((WORLD_SIZE << FBITS) - 1)
#define W(v)  ((v) >> FBITS)

/* ---- entity properties ---- */
int ent_radius(int type)
{
    switch (type) {
    case ENT_TANK: return 70;
    case ENT_BUILDING: return 80;
    case ENT_SHIP: return 100;
    case ENT_TREE: return 30;
    }
    return 0;
}

int ent_height(int type)
{
    switch (type) {
    case ENT_TANK: return 50;
    case ENT_BUILDING: return 130;
    case ENT_SHIP: return 70;
    case ENT_TREE: return 90;
    }
    return 0;
}

int ent_points(int type)
{
    switch (type) {
    case ENT_TANK: return 100;
    case ENT_BUILDING: return 50;
    case ENT_SHIP: return 150;
    }
    return 0;
}

const char *ent_name(int type)
{
    switch (type) {
    case ENT_TANK: return "tank";
    case ENT_BUILDING: return "building";
    case ENT_SHIP: return "ship";
    case ENT_TREE: return "tree";
    }
    return "none";
}

static int is_target(int type)
{
    return type == ENT_TANK || type == ENT_BUILDING || type == ENT_SHIP;
}

void world_defaults(Tune *t)
{
    t->god_mode = 0;
    t->inf_fuel = 0;
    t->enemy_fire = 90;
    t->enemy_range = 2200;
    t->turn_rate = 2;
    t->lift = 7;
    t->gravity = 4;
    t->max_speed = 256;
    t->start_lives = 3;
}

/* ---- events ---- */
static void emit(World *w, int type, int a, int b)
{
    if (w->nev < MAX_EVENTS) {
        w->ev[w->nev].type = type;
        w->ev[w->nev].a = a;
        w->ev[w->nev].b = b;
        w->nev++;
    }
}

int world_pop_event(World *w, Event *e)
{
    int i;
    if (w->nev == 0) return 0;
    *e = w->ev[0];
    for (i = 1; i < w->nev; i++) w->ev[i - 1] = w->ev[i];
    w->nev--;
    return 1;
}

/* ---- spawning ---- */
int world_spawn(World *w, int type, int32_t x, int32_t z, int yaw)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) {
        Ent *e = &w->ents[i];
        if (e->type == ENT_NONE) {
            x = ts_wrap(x);
            z = ts_wrap(z);
            e->type = type;
            e->hp = type == ENT_BUILDING ? 2 : 1;
            e->yaw = yaw & 255;
            e->timer = -(int)(ts_rand(&w->rng) % 120);
            e->x = F(x);
            e->z = F(z);
            e->y = F(type == ENT_SHIP ? SEA_LEVEL : terrain_height(w, x, z));
            if (is_target(type)) w->targets_left++;
            return i;
        }
    }
    return -1;
}

void world_clear_enemies(World *w)
{
    int i;
    for (i = 0; i < MAX_ENTS; i++) w->ents[i].type = ENT_NONE;
    for (i = 0; i < MAX_SHELLS; i++) w->shells[i].active = 0;
    w->targets_left = 0;
}

int world_count_targets(const World *w)
{
    int i, n = 0;
    for (i = 0; i < MAX_ENTS; i++)
        if (is_target(w->ents[i].type)) n++;
    return n;
}

static int site_ok(const World *w, int type, int32_t x, int32_t z)
{
    int32_t px, pz, h;
    int i;

    pad_centre(w, &px, &pz);
    {
        int32_t dx = ts_wrap_delta(x, px), dz = ts_wrap_delta(z, pz);
        if (dx > -3 * TILE && dx < 3 * TILE && dz > -3 * TILE && dz < 3 * TILE) return 0;
    }
    for (i = 0; i < MAX_ENTS; i++) {
        const Ent *e = &w->ents[i];
        int32_t dx, dz;
        if (e->type == ENT_NONE) continue;
        dx = ts_wrap_delta(x, W(e->x));
        dz = ts_wrap_delta(z, W(e->z));
        if (dx > -200 && dx < 200 && dz > -200 && dz < 200) return 0;
    }
    h = terrain_height(w, x, z);
    switch (type) {
    case ENT_SHIP:     return h < SEA_LEVEL - 40;
    case ENT_TANK:     return h > SEA_LEVEL + 40 && h < 600;
    case ENT_BUILDING: return h > SEA_LEVEL + 40 && h < 700;
    case ENT_TREE:     return h > SEA_LEVEL + 50 && h < 800;
    }
    return 0;
}

/* Place `count` entities, the first ones within `near_tiles` of the pad
 * so there is action as soon as the player lifts off. */
static void scatter(World *w, int type, int count, int near_tiles)
{
    int placed = 0, tries = 0;
    int32_t px, pz;
    pad_centre(w, &px, &pz);
    while (placed < count && tries < 600) {
        int32_t x, z;
        int r = (tries < 300 && placed * 2 < count + 1) ? near_tiles : MAP_N / 2;
        x = px + ((int32_t)(ts_rand(&w->rng) % (2 * r * TILE)) - r * TILE);
        z = pz + ((int32_t)(ts_rand(&w->rng) % (2 * r * TILE)) - r * TILE);
        x = ts_wrap(x);
        z = ts_wrap(z);
        tries++;
        if (!site_ok(w, type, x, z)) continue;
        world_spawn(w, type, x, z, (int)(ts_rand(&w->rng) & 255));
        placed++;
    }
}

void world_place_player(World *w, int32_t x, int32_t y, int32_t z, int yaw, int flying)
{
    Craft *p = &w->p;
    p->x = F(ts_wrap(x));
    p->z = F(ts_wrap(z));
    p->y = F(y);
    p->yaw = yaw & 255;
    p->vy = 0;
    p->speed = 0;
    p->bank = 0;
    p->cooldown = 0;
    p->timer = 0;
    p->state = flying ? PS_FLYING : PS_LANDED;
}

static void player_to_pad(World *w)
{
    int32_t px, pz;
    pad_centre(w, &px, &pz);
    world_place_player(w, px, w->pad_h + CLEAR, pz, 0, 0);
    w->p.fuel = FUEL_MAX;
    w->p.shield = SHIELD_MAX;
}

void world_start_level(World *w, int level)
{
    int i;
    w->level = level;
    w->state = GS_PLAY;
    w->state_timer = 0;
    world_clear_enemies(w);
    for (i = 0; i < MAX_SHOTS; i++) w->shots[i].active = 0;
    for (i = 0; i < MAX_PARTS; i++) w->parts[i].life = 0;
    w->rng = w->seed * 31 + (uint32_t)level * 977;
    scatter(w, ENT_TREE, 24, 10);
    scatter(w, ENT_BUILDING, 3 + level, 8);
    scatter(w, ENT_TANK, 2 + level, 9);
    scatter(w, ENT_SHIP, 2 + level / 2, MAP_N / 2);
    player_to_pad(w);
}

void world_init(World *w, uint32_t seed)
{
    int i;
    char *b = (char *)w;
    for (i = 0; i < (int)sizeof(World); i++) b[i] = 0;
    world_defaults(&w->tune);
    terrain_generate(w, seed);
    w->lives = w->tune.start_lives;
    world_start_level(w, 1);
    w->state = GS_TITLE;
}

void world_start_game(World *w)
{
    w->score = 0;
    w->lives = w->tune.start_lives;
    w->nev = 0;
    world_start_level(w, 1);
}

/* ---- particles ---- */
void world_explode(World *w, int32_t x, int32_t y, int32_t z, int n, int col)
{
    int i;
    for (i = 0; i < MAX_PARTS && n > 0; i++) {
        Part *q = &w->parts[i];
        if (q->life > 0) continue;
        q->x = F(x); q->y = F(y); q->z = F(z);
        q->vx = (int32_t)(ts_rand(&w->rng) % 97) - 48;
        q->vz = (int32_t)(ts_rand(&w->rng) % 97) - 48;
        q->vy = 24 + (int32_t)(ts_rand(&w->rng) % 72);
        q->life = 25 + (int)(ts_rand(&w->rng) % 35);
        q->col = col + (int)(ts_rand(&w->rng) % 2);
        n--;
    }
}

static void update_parts(World *w)
{
    int i;
    for (i = 0; i < MAX_PARTS; i++) {
        Part *q = &w->parts[i];
        if (q->life <= 0) continue;
        q->x = (q->x + q->vx) & FMASK; q->y += q->vy; q->z = (q->z + q->vz) & FMASK;
        q->vy -= 3;
        q->life--;
        if (W(q->y) < ground_height(w, W(q->x), W(q->z))) q->life = 0;
    }
}

/* ---- the craft ---- */
static void player_die(World *w)
{
    Craft *p = &w->p;
    world_explode(w, W(p->x), W(p->y), W(p->z), 28, PART_FIRE);
    p->state = PS_DEAD;
    p->timer = 90;
    p->speed = 0;
    p->vy = 0;
    w->lives--;
    emit(w, EV_CRASH, w->lives, 0);
}

/* Approximate 3D length: max + min/4 + mid/2 is within ~10%. */
static int32_t approx_len(int32_t x, int32_t y, int32_t z)
{
    int32_t a = x < 0 ? -x : x, b = y < 0 ? -y : y, c = z < 0 ? -z : z, t;
    if (a < b) { t = a; a = b; b = t; }
    if (a < c) { t = a; a = c; c = t; }
    if (b < c) { t = b; b = c; c = t; }
    return a + b / 2 + c / 4;
}

/* Aim assist: the keyboard can't pitch the nose, so a shot fired with a
 * target within ~22 degrees of the heading and AIM_RANGE units is aimed to
 * arrive at the target's middle. Returns the entity index or -1. */
#define AIM_RANGE 1800
static int aim_target(const World *w, int32_t *tdx, int32_t *tdy, int32_t *tdz)
{
    const Craft *p = &w->p;
    int i, best = -1;
    int32_t bd = AIM_RANGE;
    for (i = 0; i < MAX_ENTS; i++) {
        const Ent *e = &w->ents[i];
        int32_t dx, dz, d;
        int off;
        if (!is_target(e->type)) continue;
        dx = ts_wrap_delta(W(e->x), W(p->x));
        dz = ts_wrap_delta(W(e->z), W(p->z));
        if (dx > AIM_RANGE || dx < -AIM_RANGE || dz > AIM_RANGE || dz < -AIM_RANGE) continue;
        off = ((ts_atan2(dx, dz) - p->yaw + 128) & 255) - 128;
        if (off < -16 || off > 16) continue;
        d = approx_len(dx, 0, dz);
        if (d < bd && d > 60) {
            bd = d; best = i;
            *tdx = dx; *tdz = dz;
            *tdy = W(e->y) + ent_height(e->type) / 2 - W(p->y);
        }
    }
    return best;
}

static void fire_shot(World *w)
{
    Craft *p = &w->p;
    int i;
    int32_t tdx, tdy, tdz;
    for (i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &w->shots[i];
        if (s->active) continue;
        s->active = 1;
        s->life = 50;
        s->vx = (TS_SIN(p->yaw) * (p->speed + SHOT_SPEED)) >> 16;
        s->vz = (TS_COS(p->yaw) * (p->speed + SHOT_SPEED)) >> 16;
        s->vy = p->vy - SHOT_DROP;
        if (aim_target(w, &tdx, &tdy, &tdz) >= 0) {
            int32_t hs = p->speed + SHOT_SPEED;             /* fine/frame */
            int32_t d = approx_len(tdx, 0, tdz);
            int32_t t = (F(d) + hs / 2) / hs;                /* frames to arrive */
            if (t < 1) t = 1;
            s->vx = (F(tdx)) / t;
            s->vz = (F(tdz)) / t;
            s->vy = (F(tdy) + (t * t) / 2) / t;             /* gravity is 1 fine/frame^2 */
        }
        s->x = p->x;
        s->z = p->z;
        s->y = p->y - F(4);
        p->cooldown = 6;
        emit(w, EV_FIRE, 0, 0);
        return;
    }
}

static void update_player(World *w, unsigned in)
{
    Craft *p = &w->p;
    Tune *t = &w->tune;
    int32_t gx, gz, g;

    if (p->cooldown > 0) p->cooldown--;

    if (p->state == PS_DEAD) {
        if (--p->timer <= 0) {
            if (w->lives > 0) {
                player_to_pad(w);
                emit(w, EV_RESPAWN, w->lives, 0);
            } else {
                w->state = GS_GAMEOVER;
                w->state_timer = 120;
                emit(w, EV_GAME_OVER, (int)w->score, w->level);
            }
        }
        return;
    }

    if (in & IN_LEFT)  p->yaw = (p->yaw - t->turn_rate) & 255;
    if (in & IN_RIGHT) p->yaw = (p->yaw + t->turn_rate) & 255;
    p->bank = (in & IN_LEFT) ? -1 : (in & IN_RIGHT) ? 1 : 0;

    if (p->state == PS_LANDED) {
        p->speed = 0;
        p->vy = 0;
        if (p->fuel < FUEL_MAX) p->fuel += 8;
        if (p->fuel > FUEL_MAX) p->fuel = FUEL_MAX;
        if (p->shield < SHIELD_MAX && (w->frame & 3) == 0) p->shield++;
        if ((in & IN_UP) && p->fuel > 0) {
            p->state = PS_FLYING;
            p->vy = t->lift * 4;
            p->y += F(2);
            emit(w, EV_TAKEOFF, p->fuel, 0);
        }
        return;
    }

    /* flying: throttle */
    if (in & IN_FAST) { p->speed += 3; if (p->speed > t->max_speed) p->speed = t->max_speed; }
    if (in & IN_SLOW) { p->speed -= 4; if (p->speed < 0) p->speed = 0; }

    /* vertical: lift, dive, or hover while there is fuel */
    if (p->fuel > 0 || t->inf_fuel) {
        if (in & IN_UP) {
            p->vy += t->lift - t->gravity;
            if (p->vy > CLIMB_VY) p->vy = CLIMB_VY;
            if (!t->inf_fuel) p->fuel -= 2;
        } else if (in & IN_DOWN) {
            p->vy -= 3;
            if (p->vy < DIVE_VY) p->vy = DIVE_VY;
        } else {
            p->vy = (p->vy * 7) / 8;      /* hover assist */
        }
        if (!t->inf_fuel && (w->frame & 7) == 0) p->fuel--;
        if (p->fuel < 0) p->fuel = 0;
        if (p->fuel == 150 && !t->inf_fuel) emit(w, EV_LOW_FUEL, p->fuel, 0);
    } else {
        p->vy -= t->gravity;
        if (p->vy < -128) p->vy = -128;
    }

    p->x = (p->x + ((TS_SIN(p->yaw) * p->speed) >> 16)) & FMASK;
    p->z = (p->z + ((TS_COS(p->yaw) * p->speed) >> 16)) & FMASK;
    p->y += p->vy;
    if (W(p->y) > CEILING) { p->y = F(CEILING); if (p->vy > 0) p->vy = 0; }

    gx = W(p->x); gz = W(p->z);
    g = ground_height(w, gx, gz);
    if (W(p->y) < g + CLEAR) {
        if (on_pad(w, gx, gz) && p->vy >= LAND_VY && p->speed <= LAND_SPEED) {
            p->state = PS_LANDED;
            p->y = F(w->pad_h + CLEAR);
            p->vy = 0;
            p->speed = 0;
            emit(w, EV_LANDED, p->fuel, 0);
        } else if (t->god_mode) {
            p->y = F(g + CLEAR);
            if (p->vy < 0) p->vy = 0;
        } else {
            player_die(w);
            return;
        }
    }

    if ((in & IN_FIRE) && p->cooldown == 0) fire_shot(w);
}

/* ---- shots ---- */
static void update_shots(World *w)
{
    int i, j;
    for (i = 0; i < MAX_SHOTS; i++) {
        Shot *s = &w->shots[i];
        int32_t sx, sy, sz;
        if (!s->active) continue;
        s->x = (s->x + s->vx) & FMASK; s->y += s->vy; s->z = (s->z + s->vz) & FMASK;
        s->vy -= 1;
        sx = W(s->x); sy = W(s->y); sz = W(s->z);
        if (--s->life <= 0) { s->active = 0; continue; }

        for (j = 0; j < MAX_ENTS; j++) {
            Ent *e = &w->ents[j];
            int32_t dx, dz, r, ey;
            if (e->type == ENT_NONE) continue;
            r = ent_radius(e->type);
            dx = ts_wrap_delta(sx, W(e->x));
            dz = ts_wrap_delta(sz, W(e->z));
            ey = W(e->y);
            if (dx > -r && dx < r && dz > -r && dz < r &&
                sy >= ey - 20 && sy <= ey + ent_height(e->type) + 20) {
                s->active = 0;
                if (--e->hp <= 0) {
                    int type = e->type;
                    world_explode(w, W(e->x), ey + ent_height(type) / 2, W(e->z),
                                  type == ENT_TREE ? 6 : 20, type == ENT_SHIP ? PART_RED : PART_FIRE);
                    e->type = ENT_NONE;
                    if (is_target(type)) {
                        w->targets_left--;
                        w->score += ent_points(type);
                        emit(w, EV_KILL, type, w->targets_left);
                    }
                } else {
                    world_explode(w, sx, sy, sz, 4, PART_FIRE);
                }
                break;
            }
        }
        if (s->active && sy < ground_height(w, sx, sz)) {
            s->active = 0;
            world_explode(w, sx, ground_height(w, sx, sz), sz, 3,
                          terrain_height(w, sx, sz) <= SEA_LEVEL ? PART_WATER : PART_FIRE);
        }
    }
}

static void enemy_fire(World *w, Ent *e)
{
    Craft *p = &w->p;
    int i;
    int32_t ex = W(e->x), ey = W(e->y) + ent_height(e->type), ez = W(e->z);
    int32_t dx = ts_wrap_delta(W(p->x), ex), dy = W(p->y) - ey, dz = ts_wrap_delta(W(p->z), ez);
    int32_t d = approx_len(dx, dy, dz);
    if (d == 0) return;
    for (i = 0; i < MAX_SHELLS; i++) {
        Shot *s = &w->shells[i];
        if (s->active) continue;
        s->active = 1;
        s->life = 100;
        s->x = F(ex); s->y = F(ey); s->z = F(ez);
        s->vx = (dx * SHELL_SPEED) / d;
        s->vy = (dy * SHELL_SPEED) / d;
        s->vz = (dz * SHELL_SPEED) / d;
        emit(w, EV_ENEMY_FIRE, e->type, 0);
        return;
    }
}

static void update_ents(World *w)
{
    Craft *p = &w->p;
    int i;
    for (i = 0; i < MAX_ENTS; i++) {
        Ent *e = &w->ents[i];
        int32_t ex, ez, ax, az, ha, speed;
        if (e->type == ENT_NONE || e->type == ENT_TREE) continue;

        if (e->type == ENT_TANK || e->type == ENT_SHIP) {
            speed = e->type == ENT_TANK ? 20 : 28;
            ex = W(e->x); ez = W(e->z);
            ax = ex + ((TS_SIN(e->yaw) * 160) >> 16);
            az = ez + ((TS_COS(e->yaw) * 160) >> 16);
            ha = terrain_height(w, ax, az);
            if (e->type == ENT_TANK ?
                    (ha < SEA_LEVEL + 30 || ha - terrain_height(w, ex, ez) > 140) :
                    (ha > SEA_LEVEL - 25)) {
                e->yaw = (e->yaw + 64 + (int)(ts_rand(&w->rng) % 64)) & 255;
            } else {
                e->x = (e->x + ((TS_SIN(e->yaw) * speed) >> 16)) & FMASK;
                e->z = (e->z + ((TS_COS(e->yaw) * speed) >> 16)) & FMASK;
                if ((ts_rand(&w->rng) & 127) == 0)
                    e->yaw = (e->yaw + (int)(ts_rand(&w->rng) % 64) - 32) & 255;
            }
            if (e->type == ENT_TANK) e->y = F(terrain_height(w, W(e->x), W(e->z)));
        }

        /* fire at the player when in range */
        if (p->state == PS_FLYING && e->type != ENT_BUILDING && w->tune.enemy_fire > 0) {
            int32_t dx = ts_wrap_delta(W(p->x), W(e->x));
            int32_t dz = ts_wrap_delta(W(p->z), W(e->z));
            int32_t r = w->tune.enemy_range;
            if (dx > -r && dx < r && dz > -r && dz < r) {
                if (++e->timer >= w->tune.enemy_fire) {
                    e->timer = -(int)(ts_rand(&w->rng) % 30);   /* jitter the next shot */
                    enemy_fire(w, e);
                }
            }
        }
    }
}

static void update_shells(World *w)
{
    Craft *p = &w->p;
    int i;
    for (i = 0; i < MAX_SHELLS; i++) {
        Shot *s = &w->shells[i];
        int32_t sx, sy, sz;
        if (!s->active) continue;
        s->x = (s->x + s->vx) & FMASK; s->y += s->vy; s->z = (s->z + s->vz) & FMASK;
        sx = W(s->x); sy = W(s->y); sz = W(s->z);
        if (--s->life <= 0 || sy < ground_height(w, sx, sz)) { s->active = 0; continue; }
        if (p->state == PS_FLYING) {
            int32_t dx = ts_wrap_delta(sx, W(p->x)), dy = sy - W(p->y), dz = ts_wrap_delta(sz, W(p->z));
            if (dx > -40 && dx < 40 && dy > -40 && dy < 40 && dz > -40 && dz < 40) {
                s->active = 0;
                world_explode(w, sx, sy, sz, 5, PART_RED);
                if (!w->tune.god_mode) {
                    p->shield -= 25;
                    emit(w, EV_HIT_PLAYER, p->shield, 0);
                    if (p->shield <= 0) { p->shield = 0; player_die(w); }
                }
            }
        }
    }
}

void world_update(World *w, unsigned in)
{
    w->frame++;
    switch (w->state) {
    case GS_TITLE:
        update_ents(w);
        if ((in & ~w->last_in) & (IN_FIRE | IN_START)) world_start_game(w);
        break;
    case GS_PLAY:
        update_player(w, in);
        update_ents(w);
        update_shots(w);
        update_shells(w);
        update_parts(w);
        if (w->targets_left <= 0 && w->state == GS_PLAY) {
            w->state = GS_LEVELCLEAR;
            w->state_timer = 100;
            w->score += 500 * w->level;
            emit(w, EV_LEVEL_CLEAR, w->level, (int)w->score);
        }
        break;
    case GS_LEVELCLEAR:
        update_parts(w);
        if (--w->state_timer <= 0) world_start_level(w, w->level + 1);
        break;
    case GS_GAMEOVER:
        update_parts(w);
        if (w->state_timer > 0) w->state_timer--;
        else if ((in & ~w->last_in) & (IN_FIRE | IN_START)) w->state = GS_TITLE;
        break;
    }
    w->last_in = in;
}

/* ---- scenes: put the game into a known situation (bridge "scene" hook) ---- */
static int nearest(const World *w, int type, int32_t x, int32_t z)
{
    int i, best = -1;
    int32_t bd = 0x7FFFFFFF;
    for (i = 0; i < MAX_ENTS; i++) {
        const Ent *e = &w->ents[i];
        int32_t dx, dz, d;
        if (e->type != type) continue;
        dx = ts_wrap_delta(W(e->x), x);
        dz = ts_wrap_delta(W(e->z), z);
        d = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

/* Hover `dist` units from entity i, facing it, `alt` above the ground. */
static void approach(World *w, int i, int32_t dist, int32_t alt)
{
    Ent *e = &w->ents[i];
    int32_t ex = W(e->x), ez = W(e->z), x, z, g;
    int yaw = (int)(ts_rand(&w->rng) & 255), k;
    /* pick the approach bearing with the lowest ground so we don't start in a hill */
    int best_yaw = yaw;
    int32_t best_g = 0x7FFFFFFF;
    for (k = 0; k < 8; k++, yaw += 32) {
        x = ex - ((TS_SIN(yaw) * dist) >> 16);
        z = ez - ((TS_COS(yaw) * dist) >> 16);
        g = ground_height(w, x, z);
        if (g < best_g) { best_g = g; best_yaw = yaw & 255; }
    }
    x = ex - ((TS_SIN(best_yaw) * dist) >> 16);
    z = ez - ((TS_COS(best_yaw) * dist) >> 16);
    g = ground_height(w, x, z);
    if (g < W(e->y)) g = W(e->y);
    world_place_player(w, x, g + alt, z, best_yaw, 1);
}

int world_scene(World *w, const char *name)
{
    int32_t px, pz;
    int i;
    if (w->state != GS_PLAY) world_start_game(w);
    w->p.fuel = 1000;
    w->p.shield = 100;
    pad_centre(w, &px, &pz);

    if (name[0] == 'p') {                       /* pad */
        world_start_level(w, w->level);
        return 1;
    }
    if (name[0] == 'l' && name[1] == 'a') {     /* landing: hover over the pad */
        world_place_player(w, px, w->pad_h + 140, pz, 0, 1);
        return 1;
    }
    if (name[0] == 'l' && name[1] == 'o') {     /* lowfuel */
        world_place_player(w, px + 4 * TILE, ground_height(w, px + 4 * TILE, pz) + 200, pz, 192, 1);
        w->p.fuel = 160;
        return 1;
    }
    i = nearest(w, name[0] == 's' ? ENT_SHIP : name[0] == 't' ? ENT_TANK :
                   name[0] == 'c' ? ENT_BUILDING : ENT_NONE, px, pz);
    if (i < 0) return 0;
    approach(w, i, 800, 150);
    return 1;
}
