// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * rr_core.c - Ridge Runner's rules: the ground, the course, the bike
 * (steering, throttle, brake, boost, hovering over the ground and flying
 * off crests), the gates and the clock, the rivals, the places and the
 * autopilot. Fixed point throughout, so the host, the classic Amiga and
 * AmigaOS 4 race a seed the same way.
 */
#include <string.h>
#include "a3d.h"
#include "rr.h"

const char *const rr_event_names[RRE_COUNT] = {
    "NONE", "GATE", "MISS", "JUMP", "LAND", "BUMP", "RIVAL_FINISH", "FINISH",
    "QUALIFY", "FAIL", "TRACK", "TIMEOUT", "OVER", "WIN"
};
const char *const rr_track_names[RR_TRACKS] = {
    "GREEN VALLEY", "RED CANYON", "DUNE RUN", "FROST PASS", "BASALT GORGE", "SKY RIDGE"
};

#ifndef RPACE
#define RPACE    FX(11.0)                /* the rivals' pace on track 1 (the middle one) */
#endif
#ifndef RSTEP
#define RSTEP    FX(0.05)                /* and how much more each track */
#endif
#ifndef BEND_LIFT
#define BEND_LIFT  3400                 /* the autopilot lifts off for a bend sharper than this ahead */
#endif
#ifndef BEND_BRAKE
#define BEND_BRAKE 4200                 /* and brakes for one sharper than this */
#endif
#define GRAV     FX(0.9)
#define ACC      FX(0.3)
#define ACC_B    FX(0.6)
#define BRAKE    FX(0.8)
#define COAST    FX(0.08)

static void event(RRGame *g, int type, int a, int b)
{
    if (g->nev < RR_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}

int rr_pop_event(RRGame *g, RREvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (g->nev - 1) * sizeof(RREvent));
    g->nev--;
    return 1;
}

/* mulberry32 */
uint32_t rr_rand(RRGame *g)
{
    uint32_t t = (g->rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

#define s16 rr_s16                        /* the 3DO's types.h has an s16 */
static fix fabs_(fix a) { return a < 0 ? -a : a; }
static int32_t s16(int32_t amp, int32_t angle) { return (amp * a3_sin((int)(angle & 0xFFFF))) >> 16; }

int32_t rr_centre(const RRGame *g, int32_t z)
{
    return s16(g->c1, z * 13 + g->ph1) + s16(g->c2, z * 34 + g->ph2);
}

int32_t rr_height(const RRGame *g, int32_t x, int32_t z)
{
    int32_t d = x - rr_centre(g, z), h;
    if (d < 0) d = -d;
    h = s16(g->a1, z * 20 + x * 7 + g->hph) + s16(g->a2, x * 29 - z * 17) + s16(g->a3, z * 156);
    if (d > RR_SHOULDER) h += (d - RR_SHOULDER) * 3 / 5;        /* the valley's sides, past a rough shoulder */
    return h;
}

int rr_on_track(const RRGame *g, int32_t x, int32_t z)
{
    int32_t d = x - rr_centre(g, z);
    return d > -RR_TRACK_HW && d < RR_TRACK_HW;
}

int rr_place_now(const RRGame *g)
{
    int k, p = 1;
    if (g->finished) return g->place;
    for (k = 0; k < RR_RIVALS; k++) if (g->rv[k].finished || g->rv[k].z > g->z) p++;
    return p;
}

void rr_load_track(RRGame *g, int track)
{
    int k;
    g->track = track;
    g->c1 = 300 + track * 45;
    g->c2 = 110 + track * 20;
    g->ph1 = (int32_t)(rr_rand(g) & 0xFFFF);
    g->ph2 = (int32_t)(rr_rand(g) & 0xFFFF);
    g->hph = (int32_t)(rr_rand(g) & 0xFFFF);
    g->a1 = 50 + track * 12;
    g->a2 = 22 + track * 6;
    g->a3 = 10 + track * 3;
    g->x = (int32_t)rr_centre(g, 0) * FONE;
    g->z = 0;
    g->y = (rr_height(g, g->x >> 16, 0) + RR_HOVER) * FONE;
    g->v = g->vy = 0;
    g->yaw = 0;
    g->steer = 0;
    g->air = 0;
    g->energy = RR_ENERGY;
    g->boosting = g->offtrack = 0;
    g->gate = 0;
    g->gates_hit = g->missed = 0;
    g->finished = g->place = g->qualified = 0;
    g->nfinished = 0;
    g->race_ticks = 0;
    g->time_left = 30 * 25;
    for (k = 0; k < RR_RIVALS; k++) {
        Rival *r = &g->rv[k];
        r->z = -FROMI(40 + k * 45);
        r->x = 0;
        r->v = 0;
        r->pace = RPACE + track * RSTEP + (k - 1) * FX(0.5) + (fix)(rr_rand(g) % 8192);
        r->weave = (int)(rr_rand(g) & 0xFFFF);
        r->finished = 0;
    }
}

void rr_new_game(RRGame *g, uint32_t seed, int track)
{
    memset(g, 0, sizeof(*g));
    g->seed = g->rng = seed;
    if (track < 1) track = 1;
    if (track > RR_TRACKS) track = RR_TRACKS;
    rr_load_track(g, track);
}

static void rivals(RRGame *g)
{
    int k;
    for (k = 0; k < RR_RIVALS; k++) {
        Rival *r = &g->rv[k];
        fix want;
        int32_t z;
        int32_t h0;                     /* C89: before the statements */
        if (r->finished) continue;
        z = r->z >> 16;
        h0 = rr_height(g, r->x >> 16, z);
        want = r->pace + FMUL(FX(0.8), a3_sin((z * 3 + r->weave) & 0xFFFF));
        if (r->v < want) { r->v += ACC; if (r->v > want) r->v = want; }
        else r->v -= COAST;
        {
            /* along the winding course, not straight down it */
            fix slope = FROMI(rr_centre(g, z + 40) - rr_centre(g, z)) / 40;
            r->z += FDIV(r->v, fhypot(FONE, slope));
        }
        z = r->z >> 16;
        r->x = (rr_centre(g, z) + s16(40, z * 9 + r->weave)) * FONE;
        /* the hills slow them as they slow you */
        r->v -= (rr_height(g, r->x >> 16, z) - h0) * FX(0.06);
        if (r->v < FX(2)) r->v = FX(2);
        if (r->z >= FROMI(RR_LEN)) {
            r->finished = ++g->nfinished;
            event(g, RRE_RIVAL_FINISH, k, r->finished);
        }
    }
}

static void bike(RRGame *g, unsigned in)
{
    int dir = (in & RR_IN_RIGHT ? 1 : 0) - (in & RR_IN_LEFT ? 1 : 0), rate, k;
    fix vmax, acc, gy, follow;
    int32_t x, z;

    /* steering: quicker at low speed, hardly any in the air */
    rate = 520 - (int)(g->v >> 16) * 14;
    if (g->air) rate /= 3;
    g->yaw = (g->yaw + dir * rate) & 0xFFFF;
    g->steer = dir;

    /* throttle, brake, boost */
    /* once empty, the boost needs a fifth of a charge before it starts again */
    g->boosting = (in & RR_IN_FIRE) && (g->boosting ? g->energy > 0 : g->energy >= RR_ENERGY / 5);
    vmax = g->boosting ? RR_VBOOST : RR_VMAX;
    acc = g->boosting ? ACC_B : ACC;
    if (g->boosting) { g->energy -= FX(1.2); if (g->energy < 0) g->energy = 0; }
    else if (g->energy < RR_ENERGY) { g->energy += FX(0.25); if (g->energy > RR_ENERGY) g->energy = RR_ENERGY; }
    g->offtrack = !rr_on_track(g, g->x >> 16, g->z >> 16);
    if (g->offtrack) vmax = vmax * 3 / 5;
    if (!g->air) {
        if ((in & RR_IN_UP) || g->boosting) { if (g->v < vmax) { g->v += acc; if (g->v > vmax) g->v = vmax; } }
        if (in & RR_IN_DOWN) g->v -= BRAKE;
        if (!(in & (RR_IN_UP | RR_IN_DOWN)) && !g->boosting) g->v -= COAST;
        if (g->v > vmax) g->v -= g->v / 20;     /* rough ground, or the boost run out */
    }
    if (g->v < 0) g->v = 0;

    g->x += FMUL(g->v, a3_sin(g->yaw));
    g->z += FMUL(g->v, a3_cos(g->yaw));
    if (g->z < -FROMI(200)) g->z = -FROMI(200);
    x = g->x >> 16;
    z = g->z >> 16;

    /* hovering: follow the ground, unless it falls away faster than
     * gravity can: then fly */
    gy = (rr_height(g, x, z) + RR_HOVER) * FONE;
    if (!g->air) {
        follow = gy - g->y;
        if (follow < g->vy - GRAV && g->v > FX(4)) {
            g->air = 1;
            g->vy -= GRAV;
            g->y += g->vy;
        } else {
            g->vy = follow;
            g->y = gy;
            g->v -= FMUL(follow, FX(0.06));     /* uphill slows, downhill speeds */
            if (g->v < 0) g->v = 0;
        }
    } else {
        g->air++;
        g->vy -= GRAV;
        g->y += g->vy;
        if (g->y <= gy) {
            fix impact = -g->vy;
            if (g->air > 8) { g->jumps++; event(g, RRE_JUMP, g->air, (int)(impact >> 16)); }
            g->y = gy;
            g->vy = 0;
            g->air = 0;
            if (impact > FX(9)) { g->v = g->v * 4 / 5; g->bumps++; event(g, RRE_LAND, (int)(impact >> 16), 1); }
        }
    }

    /* the rivals: bumping one slows you and pushes you aside */
    if (g->bump_timer > 0) g->bump_timer--;
    for (k = 0; k < RR_RIVALS; k++) {
        const Rival *r = &g->rv[k];
        if (r->finished || g->air) continue;
        if (fabs_(r->x - g->x) < FROMI(22) && fabs_(r->z - g->z) < FROMI(36)) {
            g->x += r->x < g->x ? FROMI(4) : -FROMI(4);
            if (g->bump_timer) continue;
            g->v = g->v * 15 / 16;
            g->bumps++;
            g->bump_timer = 12;
            event(g, RRE_BUMP, k, 0);
        }
    }
}

void rr_tick(RRGame *g, unsigned in)
{
    if (g->over || g->won) return;
    g->tick++;
    if (g->between > 0) {
        rivals(g);
        if (--g->between == 0) {
            if (!g->qualified) { g->over = 1; event(g, RRE_OVER, (int)g->score, 0); return; }
            if (g->track >= RR_TRACKS) { g->won = 1; event(g, RRE_WIN, (int)g->score, 0); return; }
            rr_load_track(g, g->track + 1);
            event(g, RRE_TRACK, g->track, 0);
        }
        return;
    }
    g->race_ticks++;
    bike(g, in);
    rivals(g);
    g->prev_in = in;

    /* the gates */
    if (g->gate < RR_GATES && g->z >= FROMI((g->gate + 1) * RR_GATE_GAP)) {
        int32_t gz = (g->gate + 1) * RR_GATE_GAP, d = (g->x >> 16) - rr_centre(g, gz);
        if (d > -RR_GATE_HW && d < RR_GATE_HW) {
            g->gates_hit++;
            g->score += 100;
            g->time_left += (9 - g->track / 2) * 25;
            event(g, RRE_GATE, g->gate, 0);
        } else {
            g->missed++;
            g->time_left -= 3 * 25;
            event(g, RRE_MISS, g->gate, 0);
        }
        g->gate++;
    }
    /* the finish */
    if (g->z >= FROMI(RR_LEN)) {
        int bonus;
        g->finished = 1;
        g->place = ++g->nfinished;
        g->qualified = g->place <= 3;
        bonus = (4 - g->place) * 1000 + (int)(g->time_left / 25) * 10;
        if (bonus > 0) g->score += bonus;
        event(g, RRE_FINISH, g->place, bonus);
        event(g, g->qualified ? RRE_QUALIFY : RRE_FAIL, g->place, 0);
        g->between = RR_BETWEEN;
        return;
    }
    if (--g->time_left <= 0) {
        g->time_left = 0;
        g->over = 1;
        event(g, RRE_TIMEOUT, g->gate, 0);
        event(g, RRE_OVER, (int)g->score, 0);
    }
}

/* ---------- the autopilot ---------- */
unsigned rr_bot(const RRGame *g)
{
    int32_t x = g->x >> 16, z = g->z >> 16, zt, xt;
    int want, diff;
    unsigned in = 0;
    if (g->over || g->won || g->between) return 0;
    zt = z + 170 + (int32_t)(g->v >> 16) * 8;
    xt = rr_centre(g, zt);
    want = a3_atan2(xt - x, zt - z);
    diff = (int)(int16_t)(want - g->yaw);
    if (diff > 200) in |= RR_IN_RIGHT;
    if (diff < -200) in |= RR_IN_LEFT;
    {
        /* how sharply the course bends ahead: ease off for it */
        int32_t za = z + 120, zb = z + 260;
        int bend = (int)(int16_t)(a3_atan2(rr_centre(g, zb) - rr_centre(g, za), zb - za) -
                                  a3_atan2(rr_centre(g, za) - rr_centre(g, z), za - z));
        if (bend < 0) bend = -bend;
        if (diff < 0) diff = -diff;
        if ((diff > 5000 && g->v > FX(8)) || (bend > BEND_BRAKE && g->v > FX(10))) in |= RR_IN_DOWN;
        else if (diff < 3000 && (bend < BEND_LIFT || g->v < FX(10))) in |= RR_IN_UP;
        diff = (int)(int16_t)(want - g->yaw);
    }
    if (diff < 1200 && diff > -1200 && g->energy > FX(40) && !g->offtrack && !g->air) in |= RR_IN_FIRE;
    if (g->boosting && g->energy > 0 && diff < 2500 && diff > -2500 && !g->offtrack) in |= RR_IN_FIRE;
    return in;
}

uint32_t rr_state_hash(const RRGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    int32_t v[25];
    v[0] = g->score; v[1] = g->track; v[2] = g->x; v[3] = g->y; v[4] = g->z; v[5] = g->v; v[6] = g->vy; v[7] = g->yaw; v[8] = g->air; v[9] = g->energy; v[10] = g->gate; v[11] = g->gates_hit; v[12] = g->missed; v[13] = g->bumps; v[14] = g->jumps; v[15] = g->time_left; v[16] = g->tick; v[17] = g->place; v[18] = g->over; v[19] = g->won; v[20] = g->rv[0].z; v[21] = g->rv[1].z; v[22] = g->rv[2].z; v[23] = g->rv[0].x; v[24] = g->nfinished;
    for (i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) { h ^= (uint32_t)v[i]; h *= 16777619UL; }
    return h;
}
