// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * dc_core.c - Drone Courier's rules: the city, the drone (turning, flying
 * forward and back, climbing and sinking, the wind), towers' sides and
 * roofs, landing on the pads, the parcels, the battery, the clock, the
 * days, and the autopilot (climbs over the skyline, flies to the pad,
 * stops over it and settles down onto it). Fixed point throughout, so the
 * host, the classic Amiga and AmigaOS 4 fly a seed the same way.
 */
#include <string.h>
#include "a3d.h"
#include "dc.h"

const char *const dc_event_names[DCE_COUNT] = {
    "NONE", "PICKUP", "DELIVER", "BUMP", "BROKEN", "HARD", "LATE", "GUST", "FLAT", "DAY", "OVER", "WIN"
};

#define ACC      FX(0.3)
#define TURN     520
#define CLIMB    FX(0.35)
#define SINK     -FX(1.2)

static void event(DCGame *g, int type, int a, int b)
{
    if (g->nev < DC_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}

int dc_pop_event(DCGame *g, DCEvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (g->nev - 1) * sizeof(DCEvent));
    g->nev--;
    return 1;
}

static uint32_t rnd(DCGame *g)
{
    uint32_t t = (g->rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

static fix fabs_(fix a) { return a < 0 ? -a : a; }

fix dc_block_x(int col) { return FROMI(-DC_HALF + col * DC_PITCH + DC_PITCH / 2); }
fix dc_block_z(int row) { return FROMI(-DC_HALF + row * DC_PITCH + DC_PITCH / 2); }

int dc_block_at(fix x, fix z)
{
    int32_t ix = (x >> 16) + DC_HALF, iz = (z >> 16) + DC_HALF;
    int c, r;
    if (ix < 0 || iz < 0) return -1;
    c = (int)(ix / DC_PITCH);
    r = (int)(iz / DC_PITCH);
    if (c >= DC_N || r >= DC_N) return -1;
    if (fabs_(x - dc_block_x(c)) >= FROMI(DC_FOOT) || fabs_(z - dc_block_z(r)) >= FROMI(DC_FOOT)) return -1;
    return r * DC_N + c;
}

int dc_roof(const DCGame *g, int b) { return b < 0 ? 0 : g->height[b / DC_N][b % DC_N]; }

fix dc_ground(const DCGame *g, fix x, fix z) { return FROMI(dc_roof(g, dc_block_at(x, z))); }

int dc_goal(const DCGame *g) { return g->carrying ? g->target : g->depot; }

static int pick_target(DCGame *g)
{
    int k;
    for (k = 0; k < 200; k++) {
        int b = (int)(rnd(g) % (DC_N * DC_N)), dr = b / DC_N - g->depot / DC_N, dcol = b % DC_N - g->depot % DC_N;
        if (dc_roof(g, b) < 40 || b == g->depot) continue;
        if (dr * dr + dcol * dcol < 9 + g->day * 4) continue;
        return b;
    }
    return (g->depot + DC_N * 5 + 5) % (DC_N * DC_N);
}

static void new_job(DCGame *g)
{
    g->target = pick_target(g);
    g->job_time = (75 - g->day * 5) * 25;
}

void dc_load_day(DCGame *g, int day)
{
    int r, c;
    g->day = day;
    for (r = 0; r < DC_N; r++)
        for (c = 0; c < DC_N; c++) {
            int dr = 2 * r - (DC_N - 1), dc = 2 * c - (DC_N - 1), centre = 400 - (dr * dr + dc * dc) * 3;
            uint32_t h = rnd(g);
            g->height[r][c] = (int16_t)((h % 10) == 0 ? 0 : 50 + (int)((h >> 8) % 120) + (centre > 0 ? centre / 2 : 0));
            if (g->height[r][c] > 380) g->height[r][c] = 380;
        }
    /* the depot: a mid-height tower near a corner */
    g->depot = 1 * DC_N + 1;
    g->height[1][1] = 120;
    g->x = dc_block_x(1);
    g->z = dc_block_z(1);
    g->y = FROMI(120);
    g->vx = g->vy = g->vz = 0;
    g->yaw = 0x2000;
    g->landed = 1;
    g->carrying = 1;                    /* the first parcel is aboard */
    g->fragile = day >= 2 && (rnd(g) % 3) == 0;
    g->battery = FROMI(100);
    g->jobs = 3 + day;
    g->jobs_done = 0;
    g->pad_r = 36 - day * 2;
    g->wind_x = (fix)((int32_t)(rnd(g) % 2001) - 1000) * (day - 1) * 41 / 100;     /* up to 0.025 a tick^2 by day 5 */
    g->wind_z = (fix)((int32_t)(rnd(g) % 2001) - 1000) * (day - 1) * 41 / 100;
    g->gust_x = g->gust_z = 0;
    g->gust_timer = 100;
    new_job(g);
}

void dc_new_game(DCGame *g, uint32_t seed, int day)
{
    memset(g, 0, sizeof(*g));
    g->seed = g->rng = seed;
    if (day < 1) day = 1;
    if (day > DC_DAYS) day = DC_DAYS;
    dc_load_day(g, day);
}

static void fail(DCGame *g, int type)
{
    g->fails++;
    event(g, type, g->fails, 0);
    if (g->fails >= DC_MAX_FAILS) { g->over = 1; event(g, DCE_OVER, (int)g->score, 0); }
}

/* touched down on block b's roof (or the street, b < 0) at speeds vy, vside */
static void touchdown(DCGame *g, int b, fix vy, fix vside)
{
    int goal = dc_goal(g);
    fix dx, dz;
    int hard = -vy > DC_LAND_DOWN || vside > DC_LAND_SIDE;
    if (b != goal) return;
    dx = g->x - dc_block_x(b % DC_N);
    dz = g->z - dc_block_z(b / DC_N);
    if (fhypot(dx, dz) > FROMI(g->pad_r)) return;          /* on the roof, not the pad */
    if (hard) {
        event(g, DCE_HARD, (int)(-vy >> 12), (int)(vside >> 12));
        if (g->carrying && g->fragile) {
            g->breakages++;
            g->carrying = 0;
            fail(g, DCE_BROKEN);
            new_job(g);
        }
        return;
    }
    if (!g->carrying) {
        g->carrying = 1;
        g->fragile = g->day >= 2 && (rnd(g) % 3) == 0;
        g->battery = FROMI(100);
        event(g, DCE_PICKUP, g->fragile, 0);
        return;
    }
    {
        int bonus = 200 + (int)(g->job_time / 25) * 5 + (g->fragile ? 150 : 0);
        g->score += bonus;
        g->carrying = 0;
        g->jobs_done++;
        event(g, DCE_DELIVER, g->jobs_done, bonus);
        if (g->jobs_done >= g->jobs) { g->score += 500 * g->day; g->between = DC_BETWEEN; return; }
        new_job(g);
    }
}

void dc_tick(DCGame *g, unsigned in)
{
    int dir, k;
    fix fx, fz, sp, drain;
    if (g->over || g->won) return;
    g->tick++;
    if (g->between > 0) {
        if (--g->between == 0) {
            if (g->day >= DC_DAYS) { g->won = 1; event(g, DCE_WIN, (int)g->score, 0); return; }
            dc_load_day(g, g->day + 1);
            event(g, DCE_DAY, g->day, 0);
        }
        return;
    }
    /* the clock */
    if (--g->job_time <= 0) {
        fail(g, DCE_LATE);
        if (g->over) return;
        new_job(g);
    }
    /* gusts from day 2 */
    if (g->day >= 2 && --g->gust_timer <= 0) {
        g->gust_x = g->gust_x ? 0 : (fix)((int32_t)(rnd(g) % 2001) - 1000) * g->day * 52 / 100;    /* up to 0.04 */
        g->gust_z = g->gust_x ? (fix)((int32_t)(rnd(g) % 2001) - 1000) * g->day * 52 / 100 : 0;
        g->gust_timer = 60 + (int)(rnd(g) % 140);
        if (g->gust_x) event(g, DCE_GUST, 0, 0);
    }
    /* turning, flying, climbing */
    dir = (in & DC_IN_RIGHT ? 1 : 0) - (in & DC_IN_LEFT ? 1 : 0);
    g->yaw = (g->yaw + dir * TURN) & 0xFFFF;
    fx = a3_sin(g->yaw);
    fz = a3_cos(g->yaw);
    if (in & DC_IN_UP) { g->vx += FMUL(ACC, fx); g->vz += FMUL(ACC, fz); }
    if (in & DC_IN_DOWN) { g->vx -= FMUL(ACC, fx); g->vz -= FMUL(ACC, fz); }
    if (!g->landed) { g->vx += g->wind_x + g->gust_x; g->vz += g->wind_z + g->gust_z; }
    g->vx -= g->vx / 24;
    g->vz -= g->vz / 24;
    sp = fhypot(g->vx, g->vz);
    if (sp > DC_VMAX) { g->vx = FDIV(FMUL(g->vx, DC_VMAX), sp); g->vz = FDIV(FMUL(g->vz, DC_VMAX), sp); }
    if (in & DC_IN_CLIMB) { g->vy += CLIMB; if (g->vy > FX(4)) g->vy = FX(4); }
    else if (g->vy > SINK) { g->vy -= FX(0.15); if (g->vy < SINK) g->vy = SINK; }
    /* the battery */
    drain = FX(0.012) + ((in & DC_IN_CLIMB) ? FX(0.03) : 0) + ((in & (DC_IN_UP | DC_IN_DOWN)) ? FX(0.01) : 0);
    g->battery -= drain;
    if (g->battery <= 0) { g->battery = 0; g->over = 1; event(g, DCE_FLAT, 0, 0); event(g, DCE_OVER, (int)g->score, 0); return; }

    if (g->landed && g->vy <= 0) {          /* sitting: only the wind's nothing, the throttle slides it */
        g->vy = 0;
        g->vx -= g->vx / 4;
        g->vz -= g->vz / 4;
    }
    for (k = 0; k < 2; k++) {
        fix px = g->x, pz = g->z, py = g->y, roof;
        int b, pb;
        g->x += g->vx / 2;
        g->z += g->vz / 2;
        g->y += g->vy / 2;
        if (g->x > FROMI(DC_HALF + 200)) g->x = FROMI(DC_HALF + 200);
        if (g->x < -FROMI(DC_HALF + 200)) g->x = -FROMI(DC_HALF + 200);
        if (g->z > FROMI(DC_HALF + 200)) g->z = FROMI(DC_HALF + 200);
        if (g->z < -FROMI(DC_HALF + 200)) g->z = -FROMI(DC_HALF + 200);
        if (g->y > FROMI(DC_CEILING)) { g->y = FROMI(DC_CEILING); g->vy = 0; }
        b = dc_block_at(g->x, g->z);
        roof = FROMI(dc_roof(g, b));
        if (g->y < roof) {
            pb = dc_block_at(px, pz);
            if (pb == b && py >= roof - FONE) {
                /* down onto the roof (or the street) */
                fix vy = g->vy, vside = fhypot(g->vx, g->vz);
                g->y = roof;
                g->vy = 0;
                if (!g->landed) { g->landed = 1; touchdown(g, b, vy, vside); }
            } else {
                /* into a tower's side: back out, bounced */
                fix speed = fhypot(g->vx, g->vz);
                int cross_x = fabs_(px - dc_block_x(b % DC_N)) >= FROMI(DC_FOOT);
                g->x = px;
                g->z = pz;
                if (cross_x) g->vx = -g->vx * 3 / 10; else g->vz = -g->vz * 3 / 10;
                g->bumps++;
                event(g, DCE_BUMP, b, (int)(speed >> 12));
                if (g->carrying && g->fragile && speed > FX(2.5)) {
                    g->breakages++;
                    g->carrying = 0;
                    fail(g, DCE_BROKEN);
                    if (g->over) return;
                    new_job(g);
                }
            }
        } else if (g->y > roof + FX(0.5)) g->landed = 0;
    }
}

/* ---------- the autopilot ---------- */
unsigned dc_bot(const DCGame *g)
{
    unsigned in = 0;
    int goal, r, c, want, diff, sky = 0;
    fix gx, gz, dx, dz, d, along, side, sp_want, cruise;
    if (g->over || g->won || g->between) return 0;
    goal = dc_goal(g);
    gx = dc_block_x(goal % DC_N);
    gz = dc_block_z(goal / DC_N);
    for (r = 0; r < DC_N; r++) for (c = 0; c < DC_N; c++) if (g->height[r][c] > sky) sky = g->height[r][c];
    cruise = FROMI(sky + 50);
    dx = gx - g->x;
    dz = gz - g->z;
    d = fhypot(dx, dz);
    /* the speed along and across the drone's heading */
    along = FMUL(g->vx, a3_sin(g->yaw)) + FMUL(g->vz, a3_cos(g->yaw));
    side = FMUL(g->vx, a3_cos(g->yaw)) - FMUL(g->vz, a3_sin(g->yaw));
    if (d > FROMI(10)) {
        want = a3_atan2((dx >> 8) - (FMUL(g->vx, FROMI(12)) >> 8), (dz >> 8) - (FMUL(g->vz, FROMI(12)) >> 8));   /* allowing for drift */
        diff = (int)(int16_t)(want - g->yaw);
        if (diff > 300) in |= DC_IN_RIGHT;
        if (diff < -300) in |= DC_IN_LEFT;
    } else diff = 0;
    /* up over the skyline before going anywhere far; down when over the pad */
    if (d > FROMI(g->pad_r / 2) || fhypot(g->vx, g->vz) > FX(0.8)) {
        int high = g->y >= cruise - FROMI(15);
        if (!high && d > FROMI(60)) { if (g->vy < FX(3)) in |= DC_IN_CLIMB; sp_want = 0; }
        else {
            if (g->y < cruise && g->vy < FX(0.5) && d > FROMI(60)) in |= DC_IN_CLIMB;
            if (d <= FROMI(60) && g->vy < 0 && g->y < FROMI(dc_roof(g, goal)) + FROMI(40)) in |= DC_IN_CLIMB;
            sp_want = d / 16;
            if (sp_want > DC_VMAX - FX(1)) sp_want = DC_VMAX - FX(1);
            if (diff > 6000 || diff < -6000) sp_want = 0;
        }
        if (along < sp_want - FX(0.2)) in |= DC_IN_UP;
        else if (along > sp_want + FX(0.2)) in |= DC_IN_DOWN;
    } else {
        /* over the pad: hold still and settle gently */
        if (along > FX(0.15)) in |= DC_IN_DOWN;
        else if (along < -FX(0.15)) in |= DC_IN_UP;
        if (g->vy < -FX(1.2)) in |= DC_IN_CLIMB;
    }
    (void)side;
    return in;
}

uint32_t dc_state_hash(const DCGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    int32_t v[19];
    v[0] = g->score; v[1] = g->day; v[2] = g->x; v[3] = g->y; v[4] = g->z; v[5] = g->vx; v[6] = g->vy; v[7] = g->vz; v[8] = g->yaw; v[9] = g->carrying; v[10] = g->target; v[11] = g->jobs_done; v[12] = g->fails; v[13] = g->battery; v[14] = g->job_time; v[15] = g->tick; v[16] = g->bumps; v[17] = g->over; v[18] = g->won;
    for (i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) { h ^= (uint32_t)v[i]; h *= 16777619UL; }
    return h;
}
