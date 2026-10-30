// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * td_core.c - Tank Duel's rules: driving and collisions, shells (banked
 * once off the walls), the bulwark's shield, mortar rounds and their
 * splash, crates, the enemies' minds, rounds, and the autopilot. Fixed
 * point throughout, so the host, the classic Amiga and AmigaOS 4 fight a
 * seed the same way.
 */
#include <string.h>
#include "a3d.h"
#include "td.h"

const char *const td_event_names[TDE_COUNT] = {
    "NONE", "FIRE", "EFIRE", "LOB", "BOUNCE", "GLANCE", "HIT", "KILL", "HURT", "SPLASH",
    "CRATE_HIT", "CRATE_BREAK", "ROUND_CLEAR", "ROUND", "DEAD", "WIN"
};
const char *const td_tank_names[TK_COUNT] = { "PLAYER", "SCOUT", "MORTAR", "BULWARK" };

#ifndef BULWARK_TURN
#define BULWARK_TURN 100                /* the bulwark turns slowly: 14 degrees a second, so it can be flanked */
#endif
/* by type: top speed, turn rate, hits it takes, reload */
static const fix vmax[TK_COUNT] = { FX(6), FX(7), FX(3), FX(4) };
static const int turn[TK_COUNT] = { 700, 650, 300, BULWARK_TURN };
static const int hits[TK_COUNT] = { TD_HP, 1, 2, 2 };
static const int reload_t[TK_COUNT] = { TD_RELOAD, 60, 110, 55 };
#define SHIELD_ARC 6370                 /* the bulwark's shield: 35 degrees each side of ahead */
#define MORTAR_FLIGHT 45

/* the enemies each round: scouts, mortars, bulwarks */
static const uint8_t rounds[TD_ROUNDS][3] = {
    { 2, 0, 0 }, { 2, 0, 1 }, { 1, 1, 1 }, { 2, 0, 2 }, { 2, 2, 0 }, { 1, 1, 2 }, { 2, 2, 2 }, { 1, 2, 3 }
};

static void event(TDGame *g, int type, int a, int b, fix x, fix z)
{
    if (g->nev < TD_MAX_EVENTS) {
        TDEvent *e = &g->ev[g->nev++];
        e->type = type; e->a = a; e->b = b; e->x = x; e->z = z;
    }
}

int td_pop_event(TDGame *g, TDEvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (g->nev - 1) * sizeof(TDEvent));
    g->nev--;
    return 1;
}

/* mulberry32 */
uint32_t td_rand(TDGame *g)
{
    uint32_t t = (g->rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

static fix fabs_(fix a) { return a < 0 ? -a : a; }
static int iabs(int a) { return a < 0 ? -a : a; }
static int adiff(int a, int b) { return (int)(int16_t)(a - b); }    /* a - b, the short way round */

int td_angle_to(fix dx, fix dz) { return a3_atan2(dx >> 8, dz >> 8); }

static fix dist(fix dx, fix dz) { return fhypot(dx, dz); }

int td_enemies_left(const TDGame *g)
{
    int k, n = 0;
    for (k = 1; k < g->ntanks; k++) if (g->tk[k].alive) n++;
    return n;
}

int td_blocked(const TDGame *g, fix x, fix z, int r)
{
    int k;
    fix lim = FROMI(TD_ARENA - r);
    if (x > lim || x < -lim || z > lim || z < -lim) return 1;
    for (k = 0; k < g->nobs; k++) {
        const Obstacle *o = &g->ob[k];
        if (o->kind == OB_CRATE) {
            if (o->hp > 0 && fabs_(x - o->x) < FROMI(o->r + r) && fabs_(z - o->z) < FROMI(o->r + r)) return 1;
        } else if (dist(x - o->x, z - o->z) < FROMI(o->r + r)) return 1;
    }
    return 0;
}

int td_add_tank(TDGame *g, int type, fix x, fix z, int yaw)
{
    Tank *t;
    if (g->ntanks >= TD_MAX_TANKS) return -1;
    t = &g->tk[g->ntanks];
    memset(t, 0, sizeof(*t));
    t->type = type;
    t->alive = 1;
    t->hp = hits[type];
    t->x = x;
    t->z = z;
    t->yaw = yaw & 0xFFFF;
    t->reload = reload_t[type] / 2 + (int)(td_rand(g) % 30);
    t->orbit = (td_rand(g) & 1) ? 1 : -1;
    return g->ntanks++;
}

static void place_obstacles(TDGame *g)
{
    int k, tries;
    static const uint8_t kinds[13] = { OB_PILLAR, OB_PILLAR, OB_PILLAR, OB_PILLAR, OB_PYRAMID, OB_PYRAMID, OB_PYRAMID,
                                       OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE, OB_CRATE };
    g->nobs = 0;
    for (k = 0; k < 13; k++) {
        Obstacle *o = &g->ob[g->nobs];
        o->kind = kinds[k];
        o->r = o->kind == OB_PILLAR ? 26 : o->kind == OB_PYRAMID ? 50 : 34;
        o->hp = o->kind == OB_CRATE ? 2 : 1;
        for (tries = 0; tries < 40; tries++) {
            fix x = FROMI((int)(td_rand(g) % 1500) - 750), z = FROMI((int)(td_rand(g) % 1300) - 650);
            int j, ok = 1;
            if (fabs_(x) < FROMI(220) && z < -FROMI(450)) ok = 0;        /* clear of the player's start */
            for (j = 0; j < g->nobs && ok; j++)
                if (dist(x - g->ob[j].x, z - g->ob[j].z) < FROMI(o->r + g->ob[j].r + 90)) ok = 0;
            if (ok) { o->x = x; o->z = z; g->nobs++; break; }
        }
    }
}

void td_load_round(TDGame *g, int round)
{
    int k, n = 0, t;
    Tank keep = g->tk[0];
    g->round = round;
    memset(g->sh, 0, sizeof(g->sh));
    memset(g->mo, 0, sizeof(g->mo));
    place_obstacles(g);
    g->ntanks = 0;
    td_add_tank(g, TK_PLAYER, 0, -FROMI(800), 0);
    if (round > 1) {
        g->tk[0].hp = keep.hp + 30 > TD_HP ? TD_HP : keep.hp + 30;
    }
    g->tk[0].reload = 0;
    for (t = 0; t < 3; t++)
        for (k = 0; k < rounds[round - 1][t]; k++, n++) {
            fix x = FROMI(-600 + (int)((n * 397 + (int)(td_rand(g) % 160)) % 1200));
            fix z = FROMI(450 + (int)(td_rand(g) % 350));
            int j;
            for (j = 0; j < 20 && td_blocked(g, x, z, TD_TANK_R + 10); j++) x += FROMI(60);
            td_add_tank(g, TK_SCOUT + t, x, z, 0x8000);
        }
}

void td_new_game(TDGame *g, uint32_t seed, int round)
{
    memset(g, 0, sizeof(*g));
    g->seed = g->rng = seed;
    if (round < 1) round = 1;
    if (round > TD_ROUNDS) round = TD_ROUNDS;
    td_load_round(g, round);
    g->tk[0].hp = TD_HP;                /* a new game starts whole, whatever the round */
}

/* ---------- moving and colliding ---------- */
static void push_out(TDGame *g, Tank *t)
{
    int k;
    fix lim = FROMI(TD_ARENA - TD_TANK_R);
    for (k = 0; k < g->nobs; k++) {
        const Obstacle *o = &g->ob[k];
        fix dx = t->x - o->x, dz = t->z - o->z;
        if (o->kind == OB_CRATE) {
            fix px, pz, reach = FROMI(o->r + TD_TANK_R);
            if (o->hp <= 0 || fabs_(dx) >= reach || fabs_(dz) >= reach) continue;
            px = reach - fabs_(dx);
            pz = reach - fabs_(dz);
            if (px < pz) t->x += dx < 0 ? -px : px;
            else t->z += dz < 0 ? -pz : pz;
        } else {
            fix d = dist(dx, dz), reach = FROMI(o->r + TD_TANK_R);
            if (d >= reach) continue;
            if (d < FONE) { dx = FONE; d = FONE; }
            t->x = o->x + FDIV(FMUL(dx, reach), d);
            t->z = o->z + FDIV(FMUL(dz, reach), d);
        }
    }
    if (t->x > lim) t->x = lim;
    if (t->x < -lim) t->x = -lim;
    if (t->z > lim) t->z = lim;
    if (t->z < -lim) t->z = -lim;
}

static void tanks_apart(TDGame *g)
{
    int i, j;
    for (i = 0; i < g->ntanks; i++)
        for (j = i + 1; j < g->ntanks; j++) {
            Tank *a = &g->tk[i], *b = &g->tk[j];
            fix dx, dz, d, push;
            if (!a->alive || !b->alive) continue;
            dx = b->x - a->x;
            dz = b->z - a->z;
            d = dist(dx, dz);
            if (d >= FROMI(2 * TD_TANK_R)) continue;
            if (d < FONE) { dx = FONE; d = FONE; }
            push = (FROMI(2 * TD_TANK_R) - d) / 2;
            a->x -= FDIV(FMUL(dx, push), d); a->z -= FDIV(FMUL(dz, push), d);
            b->x += FDIV(FMUL(dx, push), d); b->z += FDIV(FMUL(dz, push), d);
        }
}

/* turn towards heading `want` and drive at `speed` (negative: back) */
static void drive(TDGame *g, Tank *t, int want, fix speed)
{
    int d = adiff(want, t->yaw), r = turn[t->type];
    if (d > r) d = r;
    if (d < -r) d = -r;
    t->yaw = (t->yaw + d) & 0xFFFF;
    if (t->v < speed) { t->v += FX(0.5); if (t->v > speed) t->v = speed; }
    else if (t->v > speed) { t->v -= FX(0.6); if (t->v < speed) t->v = speed; }
    t->x += FMUL(t->v, a3_sin(t->yaw));
    t->z += FMUL(t->v, a3_cos(t->yaw));
    push_out(g, t);
}

int td_fire(TDGame *g, int tank)
{
    Tank *t = &g->tk[tank];
    int k;
    fix fx = a3_sin(t->yaw), fz = a3_cos(t->yaw);
    if (t->reload > 0 || !t->alive) return 0;
    for (k = 0; k < TD_MAX_SHELLS; k++) if (!g->sh[k].alive) break;
    if (k == TD_MAX_SHELLS) return 0;
    g->sh[k].alive = 1;
    g->sh[k].owner = tank;
    g->sh[k].bounces = 1;
    g->sh[k].life = TD_SHELL_LIFE;
    g->sh[k].x = t->x + fx * (TD_TANK_R + 6);
    g->sh[k].z = t->z + fz * (TD_TANK_R + 6);
    g->sh[k].dx = FMUL(TD_SHELL_SPEED, fx);
    g->sh[k].dz = FMUL(TD_SHELL_SPEED, fz);
    t->reload = reload_t[t->type];
    if (tank == 0) g->shots++;
    event(g, tank ? TDE_EFIRE : TDE_FIRE, tank, 0, t->x, t->z);
    return 1;
}

static void lob(TDGame *g, int tank)
{
    Tank *t = &g->tk[tank], *p = &g->tk[0];
    int k;
    for (k = 0; k < TD_MAX_MORTARS; k++) if (!g->mo[k].alive) break;
    if (k == TD_MAX_MORTARS) return;
    g->mo[k].alive = 1;
    g->mo[k].owner = tank;
    g->mo[k].t = 0;
    g->mo[k].flight = MORTAR_FLIGHT;
    g->mo[k].x0 = t->x;
    g->mo[k].z0 = t->z;
    /* where the player will be in half the flight */
    g->mo[k].tx = p->x + FMUL(p->v, a3_sin(p->yaw)) * (MORTAR_FLIGHT / 2);
    g->mo[k].tz = p->z + FMUL(p->v, a3_cos(p->yaw)) * (MORTAR_FLIGHT / 2);
    t->reload = reload_t[t->type];
    event(g, TDE_LOB, tank, k, g->mo[k].tx, g->mo[k].tz);
}

static void damage(TDGame *g, int tank, int amount, fix x, fix z)
{
    Tank *t = &g->tk[tank];
    if (!t->alive) return;
    if (tank == 0) {
        t->hp -= amount;
        g->damage_taken += amount;
        event(g, TDE_HURT, amount, t->hp, x, z);
        if (t->hp <= 0) { t->hp = 0; t->alive = 0; g->over = 1; event(g, TDE_DEAD, 0, 0, t->x, t->z); }
        return;
    }
    if (--t->hp > 0) { event(g, TDE_HIT, tank, t->hp, x, z); return; }
    t->alive = 0;
    g->kills++;
    g->score += t->type == TK_SCOUT ? 100 : t->type == TK_MORTAR ? 200 : 300;
    event(g, TDE_KILL, tank, t->type, t->x, t->z);
}

/* ---------- shells ---------- */
static void shells(TDGame *g)
{
    int k, s, j;
    for (k = 0; k < TD_MAX_SHELLS; k++) {
        Shell *sh = &g->sh[k];
        if (!sh->alive) continue;
        if (--sh->life <= 0) { sh->alive = 0; continue; }
        for (s = 0; s < 4 && sh->alive; s++) {
            fix lim = FROMI(TD_ARENA);
            sh->x += sh->dx / 4;
            sh->z += sh->dz / 4;
            /* the walls: one bounce, then it's spent */
            if (sh->x > lim || sh->x < -lim || sh->z > lim || sh->z < -lim) {
                if (!sh->bounces) { sh->alive = 0; event(g, TDE_SPLASH, 0, 0, sh->x, sh->z); break; }
                sh->bounces--;
                if (sh->x > lim || sh->x < -lim) { sh->dx = -sh->dx; sh->x = sh->x > 0 ? 2 * lim - sh->x : -2 * lim - sh->x; }
                if (sh->z > lim || sh->z < -lim) { sh->dz = -sh->dz; sh->z = sh->z > 0 ? 2 * lim - sh->z : -2 * lim - sh->z; }
                event(g, TDE_BOUNCE, k, 0, sh->x, sh->z);
            }
            /* obstacles */
            for (j = 0; j < g->nobs && sh->alive; j++) {
                Obstacle *o = &g->ob[j];
                if (o->kind == OB_CRATE) {
                    if (o->hp <= 0 || fabs_(sh->x - o->x) >= FROMI(o->r) || fabs_(sh->z - o->z) >= FROMI(o->r)) continue;
                    sh->alive = 0;
                    if (--o->hp <= 0) { if (sh->owner == 0) g->score += 20; event(g, TDE_CRATE_BREAK, j, 0, o->x, o->z); }
                    else event(g, TDE_CRATE_HIT, j, o->hp, sh->x, sh->z);
                } else if (dist(sh->x - o->x, sh->z - o->z) < FROMI(o->r)) {
                    sh->alive = 0;
                    event(g, TDE_SPLASH, 1, 0, sh->x, sh->z);
                }
            }
            /* tanks: its own only after a bounce */
            for (j = 0; j < g->ntanks && sh->alive; j++) {
                Tank *t = &g->tk[j];
                fix dx, dz;
                if (!t->alive || (j == sh->owner && sh->bounces)) continue;
                if (sh->owner != 0 && j != 0) continue;         /* enemies' shells pass their own side */
                dx = sh->x - t->x;
                dz = sh->z - t->z;
                if (fabs_(dx) >= FROMI(TD_TANK_R) || fabs_(dz) >= FROMI(TD_TANK_R) || dist(dx, dz) >= FROMI(TD_TANK_R)) continue;
                if (t->type == TK_BULWARK && iabs(adiff(td_angle_to(dx, dz), t->yaw)) < SHIELD_ARC) {
                    /* off the shield */
                    event(g, TDE_GLANCE, j, 0, sh->x, sh->z);
                    if (sh->bounces) {
                        int a = (2 * t->yaw - td_angle_to(-sh->dx, -sh->dz)) & 0xFFFF;
                        sh->bounces--;
                        sh->dx = FMUL(TD_SHELL_SPEED, a3_sin(a));
                        sh->dz = FMUL(TD_SHELL_SPEED, a3_cos(a));
                        sh->x = t->x + FMUL(FROMI(TD_TANK_R + 4), a3_sin(a));
                        sh->z = t->z + FMUL(FROMI(TD_TANK_R + 4), a3_cos(a));
                        sh->owner = j;
                    } else sh->alive = 0;
                    break;
                }
                sh->alive = 0;
                if (sh->owner == 0 && j != 0) g->hits++;
                damage(g, j, TD_SHELL_DMG, sh->x, sh->z);
            }
        }
    }
}

static void mortars(TDGame *g)
{
    int k, j;
    for (k = 0; k < TD_MAX_MORTARS; k++) {
        Mortar *m = &g->mo[k];
        if (!m->alive || ++m->t < m->flight) continue;
        m->alive = 0;
        event(g, TDE_SPLASH, 2, k, m->tx, m->tz);
        for (j = 0; j < g->ntanks; j++) {
            Tank *t = &g->tk[j];
            fix d;
            if (!t->alive) continue;
            d = dist(t->x - m->tx, t->z - m->tz);
            if (d >= FROMI(TD_SPLASH_R)) continue;
            if (j == 0) damage(g, 0, TD_SPLASH_DMG - (int)(FMUL(FROMI(TD_SPLASH_DMG), FDIV(d, FROMI(TD_SPLASH_R))) >> 16), m->tx, m->tz);
            else if (d < FROMI(TD_SPLASH_R / 2)) damage(g, j, 1, m->tx, m->tz);
        }
    }
}

/* ---------- the enemies ---------- */
static void think(TDGame *g, int k)
{
    Tank *t = &g->tk[k], *p = &g->tk[0];
    fix dx = p->x - t->x, dz = p->z - t->z, d = dist(dx, dz), speed = vmax[t->type];
    int to = td_angle_to(dx, dz), want = to;
    t->ai_timer++;
    if (t->reload > 0) t->reload--;
    switch (t->type) {
    case TK_SCOUT:
        if (t->ai_timer % 120 > 70) {           /* turn in to snipe */
            want = to;
            speed = FX(1);
            if (iabs(adiff(to, t->yaw)) < 700 && d < FROMI(900)) td_fire(g, k);
        } else if (d > FROMI(420)) want = to + t->orbit * 0x1000;
        else if (d < FROMI(260)) want = to + t->orbit * 0x6000;
        else want = to + t->orbit * 0x4000;
        if (t->ai_timer % 400 == 399) t->orbit = -t->orbit;
        break;
    case TK_MORTAR:
        if (d < FROMI(450)) want = to + 0x8000;
        else if (d > FROMI(750)) want = to;
        else speed = 0;
        if (t->reload == 0 && d < FROMI(1100)) lob(g, k);
        break;
    case TK_BULWARK:
        if (d < FROMI(240)) speed = 0;
        if (iabs(adiff(to, t->yaw)) < 600 && d < FROMI(900)) td_fire(g, k);
        break;
    }
    /* something in the way ahead: steer round it */
    if (speed > 0 && td_blocked(g, t->x + 70 * a3_sin(t->yaw), t->z + 70 * a3_cos(t->yaw), TD_TANK_R))
        want = t->yaw + t->orbit * 0x2000;
    drive(g, t, want & 0xFFFF, speed);
}

/* ---------- the tick ---------- */
void td_tick(TDGame *g, unsigned in)
{
    Tank *p = &g->tk[0];
    int k, dir;
    fix speed;
    if (g->over || g->won) return;
    g->tick++;
    if (g->between > 0) {
        if (--g->between == 0) {
            if (g->round >= TD_ROUNDS) { g->won = 1; event(g, TDE_WIN, (int)g->score, 0, 0, 0); return; }
            td_load_round(g, g->round + 1);
            event(g, TDE_ROUND, g->round, 0, 0, 0);
        }
        return;
    }
    /* the player */
    dir = (in & TD_IN_RIGHT ? 1 : 0) - (in & TD_IN_LEFT ? 1 : 0);
    speed = (in & TD_IN_UP) ? vmax[0] : (in & TD_IN_DOWN) ? -vmax[0] * 3 / 5 : 0;
    drive(g, p, (p->yaw + dir * turn[0]) & 0xFFFF, speed);
    if (p->reload > 0) p->reload--;
    if (in & TD_IN_FIRE) td_fire(g, 0);
    g->prev_in = in;
    /* the enemies */
    for (k = 1; k < g->ntanks; k++) if (g->tk[k].alive && !g->tk[k].held) think(g, k);
    tanks_apart(g);
    for (k = 0; k < g->ntanks; k++) if (g->tk[k].alive) push_out(g, &g->tk[k]);
    shells(g);
    mortars(g);
    if (!g->over && g->ntanks > 1 && td_enemies_left(g) == 0) {     /* (an arena with no enemies at all: practice) */
        int bonus = 500 * g->round + p->hp * 5;
        g->score += bonus;
        g->between = TD_BETWEEN;
        memset(g->sh, 0, sizeof(g->sh));
        memset(g->mo, 0, sizeof(g->mo));
        event(g, TDE_ROUND_CLEAR, g->round, bonus, 0, 0);
    }
}

/* ---------- the autopilot ---------- */
unsigned td_bot(const TDGame *g)
{
    const Tank *p = &g->tk[0], *e = NULL;
    fix best = 0, dx, dz, d;
    int k, want, diff, to;
    unsigned in = 0;
    if (g->over || g->won || g->between || !p->alive) return 0;
    for (k = 1; k < g->ntanks; k++) {
        const Tank *t = &g->tk[k];
        fix dd;
        if (!t->alive) continue;
        dd = dist(t->x - p->x, t->z - p->z);
        if (!e || dd < best) { e = t; best = dd; }
    }
    if (!e) return 0;
    dx = e->x - p->x;
    dz = e->z - p->z;
    d = best;
    to = td_angle_to(dx, dz);
    /* a bulwark: circle it at 320, the way it is already turning away
     * from, until well off its shield; then stop, aim and fire */
    if (e->type == TK_BULWARK) {
        int rel = adiff(td_angle_to(-dx, -dz), e->yaw), side = rel >= 0 ? 1 : -1;
        if (iabs(rel) < SHIELD_ARC + 2600) {
            int lean = (int)((d - FROMI(320)) >> 16) * 40;
            if (lean > 0x2000) lean = 0x2000;
            if (lean < -0x2000) lean = -0x2000;
            want = to - side * (0x4000 - lean);
            in |= TD_IN_UP;
        } else want = to;
    } else {
        want = to;
        if (d > FROMI(420)) in |= TD_IN_UP;
        else if (d < FROMI(230)) in |= TD_IN_DOWN;
    }
    /* out of a mortar's ring */
    for (k = 0; k < TD_MAX_MORTARS; k++) {
        const Mortar *m = &g->mo[k];
        if (m->alive && dist(p->x - m->tx, p->z - m->tz) < FROMI(TD_SPLASH_R + 40)) {
            want = td_angle_to(p->x - m->tx, p->z - m->tz);
            in = (in & ~TD_IN_DOWN) | TD_IN_UP;
        }
    }
    /* something in the way */
    if ((in & TD_IN_UP) && td_blocked(g, p->x + 60 * a3_sin(p->yaw), p->z + 60 * a3_cos(p->yaw), TD_TANK_R))
        want = p->yaw + 0x3000;
    diff = adiff(want, p->yaw);
    if (diff > 250) in |= TD_IN_RIGHT;
    if (diff < -250) in |= TD_IN_LEFT;
    /* shoot when lined up on an enemy and not at its shield */
    for (k = 1; k < g->ntanks; k++) {
        const Tank *t = &g->tk[k];
        fix tdx, tdz;
        if (!t->alive) continue;
        tdx = t->x - p->x;
        tdz = t->z - p->z;
        if (iabs(adiff(td_angle_to(tdx, tdz), p->yaw)) > 450 || dist(tdx, tdz) > FROMI(1000)) continue;
        if (t->type == TK_BULWARK && iabs(adiff(td_angle_to(-tdx, -tdz), t->yaw)) < SHIELD_ARC) continue;
        if (p->reload == 0) in |= TD_IN_FIRE;
    }
    return in;
}

uint32_t td_state_hash(const TDGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    for (i = 0; i < g->ntanks; i++) {
        const Tank *t = &g->tk[i];
        int32_t v[6];
        int j;
        v[0] = t->alive; v[1] = t->hp; v[2] = t->x; v[3] = t->z; v[4] = t->yaw; v[5] = t->v;
        for (j = 0; j < 6; j++) { h ^= (uint32_t)v[j]; h *= 16777619UL; }
    }
    for (i = 0; i < g->nobs; i++) { h ^= (uint32_t)(g->ob[i].hp + g->ob[i].x); h *= 16777619UL; }
    {
        int32_t v[9];
        v[0] = g->score; v[1] = g->round; v[2] = g->tick; v[3] = g->kills; v[4] = g->shots; v[5] = g->hits; v[6] = g->damage_taken; v[7] = g->over; v[8] = g->won;
        for (i = 0; i < 9; i++) { h ^= (uint32_t)v[i]; h *= 16777619UL; }
    }
    return h;
}
