// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ap_core.c - Ace Pilot 2's simulation: the flight model, the guns and
 * their heat, bullets and hits, the anti-aircraft guns' shells, the
 * computer's pilots, the train and the lorries, the eight missions and
 * the campaign, and the autopilot. Pure and deterministic (integers only):
 * the host and both Amigas fly the same war tick for tick.
 */
#include <string.h>
#include "a3d.h"
#include "ap.h"

/* the flight model, per tick */
#define T_MAX       FX(0.052)           /* full throttle's thrust: level flight at 3.6 */
#define DRAG        FX(0.004)           /* times v squared */
#define GRAV        FX(0.06)            /* times the sine of the pitch */
#define ROLL_RATE   700
#define LEVEL_RATE  140                 /* the bank eases off by itself */
#define PITCH_RATE  300
#define TURN_RATE   360                 /* yaw a tick at a 90-degree bank */
#define BANK_DROOP  60                  /* the nose drops in a bank, unless pulled */
#define V_LIFT      FX(2.6)             /* slower than this, it sinks */

const char *const ap_event_names[APE_COUNT] = {
    "none", "fire", "hit", "kill", "gkill", "hurt", "jam", "unjam", "stall", "flak",
    "waypoint", "objective", "homeward", "done", "failed", "down", "crash", "landed",
    "rearmed", "arrive", "bombs", "ally_lost", "edge"
};
const char *const ap_plane_names[PT_COUNT] = { "Swallow", "Heron", "Wasp", "Hornet", "Ox", "Grey Falcon" };
const char *const ap_ground_names[GT_COUNT] = { "hangar", "supply dump", "AA gun", "balloon", "locomotive", "wagon", "lorry" };
const char *const ap_ranks[5] = { "SECOND LIEUTENANT", "LIEUTENANT", "CAPTAIN", "MAJOR", "WING COMMANDER" };

const APMission ap_missions[AP_MISSIONS] = {
    { "DAWN PATROL", "THE SELLEN FRONT",
      { "FLY THE PATROL LINE OVER THE TRENCHES:", "THREE MARKERS, WEST TO EAST.",
        "GRAULAND WASPS ARE ABOUT.", "THEN HOME TO LARKSPUR FIELD." } },
    { "EYES IN THE SKY", "THE SELLEN BEND",
      { "TWO OBSERVATION BALLOONS ARE SPOTTING", "FOR THEIR GUNS BEHIND THE RIVER.",
        "BURN THEM BOTH. THEIR ANTI-AIRCRAFT", "GUNS STAND ROUND THEM." } },
    { "SHEPHERD", "ORME DEPOT",
      { "TWO HERON TWO-SEATERS WILL PHOTOGRAPH", "THE SUPPLY DEPOT AT ORME.",
        "KEEP THE WASPS OFF THEM AND BRING", "AT LEAST ONE HERON HOME." } },
    { "IRON ROAD", "THE KESSIN LINE",
      { "A SUPPLY TRAIN IS RUNNING EAST ON THE", "KESSIN RAILWAY. STOP IT: DESTROY THE",
        "ENGINE AND WAGONS - THREE IN ALL -", "BEFORE IT REACHES THE EAST." } },
    { "HOLD THE LINE", "OUR RESERVE DEPOT",
      { "OX BOMBERS ARE COMING FOR OUR RESERVE", "DEPOT BEHIND THE TRENCHES, A HORNET",
        "WITH THEM. SHOOT DOWN ALL FOUR. IF TWO", "GET THROUGH, THE DEPOT IS LOST." } },
    { "SUPPLY LINES", "ORME DEPOT",
      { "THE ENEMY'S SHELLS AND FUEL ARE", "STACKED AT ORME. DESTROY THE FOUR",
        "SUPPLY DUMPS. LORRIES ON THE ROAD", "ARE WORTH A BURST TOO." } },
    { "HORNETS' NEST", "KESSIN AERODROME",
      { "RAID THE ENEMY AERODROME AT KESSIN,", "FAR BEHIND THE LINES: DESTROY ITS",
        "THREE HANGARS. EXPECT HEAVY GUNS AND", "HORNETS TAKING OFF." } },
    { "THE GREY FALCON", "OVER THE FRONT",
      { "THEIR ACE, THE GREY FALCON, IS UP", "WITH TWO WINGMEN. HE HAS SHOT DOWN",
        "NINE OF OURS THIS MONTH.", "BRING HIM DOWN." } },
};

/* hills, clear of the river, the railway, the roads and the aerodromes */
const Hill ap_hills[AP_HILLS] = {
    { -3200, -1800, 600, 170 }, { 2200, -2600, 700, 200 }, { -2000, -3800, 500, 140 }, { 900, -1200, 450, 110 },
    { 3800, -900, 600, 180 }, { -4200, -600, 550, 150 }, { 400, -4300, 650, 190 }, { -400, 1800, 480, 150 },
    { 3000, 2000, 480, 170 }, { -4000, 1700, 600, 160 }, { 500, 3600, 450, 130 }, { -1800, 4300, 700, 220 },
    { 4200, 4000, 600, 200 }, { 3600, -3600, 500, 150 }, { -5200, 3600, 700, 230 }, { 5200, 1500, 480, 160 },
};

/* per type: thrust and turn (percent), hit points, size, a forward gun,
 * a rear gunner, the score for it, the aim's scatter */
static const struct { int thrust, turn, hp, r, gun, rear, score, scatter; } ptype[PT_COUNT] = {
    { 100, 110, AP_HP, 10, 2, 0, 0, 0 },
    { 86, 70, 16, 12, 0, 1, 0, 500 },
    { 96, 96, 10, 10, 1, 0, 100, 760 },
    { 103, 100, 14, 10, 1, 0, 150, 640 },
    { 82, 62, 22, 14, 0, 1, 200, 800 },
    { 110, 108, 26, 10, 2, 0, 600, 560 },
};
/* ground things: hit points, radius, the middle's height, score */
static const struct { int hp, r, cy, score; } gtype[GT_COUNT] = {
    { 16, 32, 14, 150 }, { 10, 22, 10, 120 }, { 6, 14, 6, 80 }, { 8, 20, 0, 250 },
    { 12, 16, 10, 200 }, { 8, 16, 10, 60 }, { 5, 10, 6, 50 },
};

uint32_t ap_rand(APGame *g) { return a3_rand(&g->rng); }

static int iabs(int a) { return a < 0 ? -a : a; }
static fix fabs_(fix a) { return a < 0 ? -a : a; }
static int16_t sang(int a) { return (int16_t)(a & 0xFFFF); }   /* an angle difference, signed */

static void event(APGame *g, int type, int a, int b, fix x, fix y, fix z)
{
    APEvent *e;
    if (g->nev >= AP_MAX_EVENTS) return;
    e = &g->ev[g->nev++];
    e->type = type; e->a = a; e->b = b; e->x = x; e->y = y; e->z = z;
}

int ap_pop_event(APGame *g, APEvent *e)
{
    if (g->nev <= 0) return 0;
    *e = g->ev[0];
    g->nev--;
    memmove(&g->ev[0], &g->ev[1], (size_t)g->nev * sizeof(APEvent));
    return 1;
}

/* ---------- the map ---------- */
int ap_angle_to(fix dx, fix dz) { return a3_atan2(dx >> 4, dz >> 4); }

int32_t ap_river_z(int32_t x) { return 900 + ((350 * a3_sin((int)(x * 7))) >> 16); }
int32_t ap_rail_z(int32_t x) { return AP_RAIL_Z + ((150 * a3_sin((int)(x * 9))) >> 16); }
int32_t ap_trench_z(int32_t x)
{
    int32_t t = (x + 70000) % 160;      /* a zigzag: 160 units a tooth */
    return (t < 80 ? t : 160 - t) / 2 - 20;
}

void ap_road_pos(fix s, fix *x, fix *z)
{
    /* Orme depot (-2500, 2000) to the front at (-1200, 100) */
    int32_t f = (int32_t)(s >> 8) * 256 / (AP_ROAD_LEN >> 16);   /* 0..65536 */
    *x = FROMI(AP_DEPOT_X) + 1300 * f;
    *z = FROMI(AP_DEPOT_Z) - 1900 * f;
}

fix ap_ground_y(fix x, fix z)
{
    int32_t wx = x >> 16, wz = z >> 16;
    fix best = 0;
    int k;
    for (k = 0; k < AP_HILLS; k++) {
        const Hill *h = &ap_hills[k];
        int32_t dx = wx - h->x, dz = wz - h->z, d;
        fix y;
        if (dx > h->r || dx < -h->r || dz > h->r || dz < -h->r) continue;
        d = a3_hypot(dx, dz);
        if (d >= h->r) continue;
        y = (FROMI(h->h) / h->r) * (h->r - d);
        if (y > best) best = y;
    }
    return best;
}

int ap_on_runway(fix x, fix z)
{
    int32_t dx = (x >> 16) - AP_HOME_X, dz = (z >> 16) - AP_HOME_Z;
    return dx > -AP_RUNWAY_W && dx < AP_RUNWAY_W && dz > -AP_RUNWAY_L && dz < AP_RUNWAY_L;
}

static int32_t home_dist(fix x, fix z)
{
    return a3_hypot((x >> 16) - AP_HOME_X, (z >> 16) - AP_HOME_Z);
}

int ap_rank(int32_t score)
{
    return score >= 16000 ? 4 : score >= 10000 ? 3 : score >= 5500 ? 2 : score >= 2200 ? 1 : 0;
}

/* ---------- setting up ---------- */
int ap_add_plane(APGame *g, int type, int side, fix x, fix y, fix z, int yaw)
{
    Plane *p;
    int k;
    for (k = 1; k < AP_MAX_PLANES; k++) if (k >= g->np || g->p[k].state == PS_DEAD) break;
    if (k >= AP_MAX_PLANES) return -1;
    if (k >= g->np) g->np = k + 1;
    p = &g->p[k];
    memset(p, 0, sizeof(*p));
    p->type = type; p->side = side; p->state = PS_FLY;
    p->hp = p->maxhp = ptype[type].hp;
    p->x = x; p->y = y; p->z = z; p->yaw = yaw & 0xFFFF;
    p->v = AP_V_CRUISE * 9 / 10;
    p->throttle = 100;
    p->ammo = 9999;
    p->ai = (type == PT_HERON || type == PT_OX) ? AI_ROUTE : AI_PATROL;
    p->tx = x; p->tz = z; p->talt = y >> 16;
    p->target = -1;
    p->hit_t = 999;
    return k;
}

int ap_add_ground(APGame *g, int type, fix x, fix y, fix z, int yaw, int objective)
{
    Ground *o;
    if (g->ngr >= AP_MAX_GROUND) return -1;
    o = &g->gr[g->ngr];
    memset(o, 0, sizeof(*o));
    o->type = type; o->alive = 1; o->hp = gtype[type].hp; o->objective = objective;
    o->x = x; o->y = y; o->z = z; o->yaw = yaw & 0xFFFF;
    o->cd = 30 + g->ngr * 7;
    if (objective) g->need += 0;        /* the mission sets need */
    return g->ngr++;
}

static int add_on_rail(APGame *g, int type, int32_t x, fix ds, int objective)
{
    int k = ap_add_ground(g, type, FROMI(x), 0, FROMI(ap_rail_z(x)), 16384, objective);
    if (k >= 0) { g->gr[k].line = 1; g->gr[k].s = FROMI(x); g->gr[k].ds = ds; }
    return k;
}

static int add_on_road(APGame *g, int32_t s, fix ds)
{
    fix x, z;
    int k;
    ap_road_pos(FROMI(s), &x, &z);
    k = ap_add_ground(g, GT_TRUCK, x, 0, z, 0, 0);
    if (k >= 0) { g->gr[k].line = 2; g->gr[k].s = FROMI(s); g->gr[k].ds = ds; }
    return k;
}

static void wp(Waypoint *w, int32_t x, int32_t z, int alt) { w->x = FROMI(x); w->z = FROMI(z); w->alt = alt; }

static int enemy(APGame *g, int type, int32_t x, int alt, int32_t z, int yaw, int32_t px, int32_t pz)
{
    int k = ap_add_plane(g, type, SIDE_ENEMY, FROMI(x), FROMI(alt), FROMI(z), yaw);
    if (k > 0) { g->p[k].tx = FROMI(px); g->p[k].tz = FROMI(pz); g->p[k].talt = alt; }
    return k;
}

void ap_load_mission(APGame *g, int mission)
{
    Plane *p = &g->p[0];
    int k;
    if (mission < 1) mission = 1;
    if (mission > AP_MISSIONS) mission = AP_MISSIONS;
    g->mission = mission;
    g->phase = PH_OUT;
    g->end_t = 0;
    g->mtick = 0;
    g->np = 1;
    g->ngr = 0;
    g->nwp = g->wp_next = 0;
    g->nroute = 0;
    g->loiter = 0;
    g->need = g->done = 0;
    g->lost = 0;
    g->lost_max = 99;
    g->wave_t = 0;
    g->wave_n = 0;
    g->bot_mode = g->bot_t = 0;
    memset(g->b, 0, sizeof(g->b));
    memset(g->fk, 0, sizeof(g->fk));
    memset(p, 0, sizeof(*p));
    p->type = PT_SWALLOW; p->side = SIDE_HOME; p->state = PS_FLY;
    p->hp = p->maxhp = AP_HP;
    p->x = FROMI(AP_HOME_X); p->y = FROMI(300); p->z = FROMI(AP_HOME_Z + 500);
    p->yaw = 0;
    p->v = AP_V_CRUISE;
    p->throttle = 100;
    p->ammo = AP_AMMO;
    p->target = -1;
    p->hit_t = 999;
    switch (mission) {
    case 1:                             /* the patrol line; Wasps after the first marker */
        wp(&g->wp[0], -1800, 150, 320); wp(&g->wp[1], 0, 650, 320); wp(&g->wp[2], 1800, 150, 320);
        g->nwp = 3;
        g->wave_n = 2;
        break;
    case 2:                             /* two balloons, their guns, a Wasp */
        ap_add_ground(g, GT_BALLOON, FROMI(-1400), FROMI(230), FROMI(800), 0, 1);
        ap_add_ground(g, GT_BALLOON, FROMI(1300), FROMI(230), FROMI(700), 0, 1);
        ap_add_ground(g, GT_AA, FROMI(-1280), 0, FROMI(960), 0, 0);
        ap_add_ground(g, GT_AA, FROMI(-1560), 0, FROMI(640), 0, 0);
        ap_add_ground(g, GT_AA, FROMI(1180), 0, FROMI(860), 0, 0);
        ap_add_ground(g, GT_AA, FROMI(1460), 0, FROMI(580), 0, 0);
        enemy(g, PT_WASP, 0, 380, 1400, 0x8000, 0, 900);
        g->need = 2;
        break;
    case 3:                             /* the Herons to Orme and back */
        for (k = 0; k < 2; k++) {
            int h = ap_add_plane(g, PT_HERON, SIDE_HOME, p->x + FROMI(k ? 90 : -90), FROMI(280), p->z + FROMI(60), 0);
            g->p[h].talt = 280;
        }
        wp(&g->route[0], -1500, 300, 300);
        wp(&g->route[1], AP_DEPOT_X, AP_DEPOT_Z, 300);
        g->nroute = 2;
        g->loiter = 180;
        ap_add_ground(g, GT_AA, FROMI(AP_DEPOT_X + 120), 0, FROMI(AP_DEPOT_Z - 80), 0, 0);
        ap_add_ground(g, GT_AA, FROMI(AP_DEPOT_X - 110), 0, FROMI(AP_DEPOT_Z + 90), 0, 0);
        g->lost_max = 2;
        g->wave_n = 2;                  /* two waves of two Wasps */
        g->need = 1;
        break;
    case 4:                             /* the train: an engine, four wagons, one of them guns */
        add_on_rail(g, GT_LOCO, -4300, FX(1.3), 1);
        add_on_rail(g, GT_WAGON, -4340, FX(1.3), 1);
        add_on_rail(g, GT_WAGON, -4380, FX(1.3), 1);
        add_on_rail(g, GT_AA, -4420, FX(1.3), 0);
        add_on_rail(g, GT_WAGON, -4460, FX(1.3), 1);
        enemy(g, PT_HORNET, -2600, 420, 2900, 0x8000, -2600, 2700);
        enemy(g, PT_HORNET, -2400, 460, 3000, 0x8000, -2600, 2700);
        g->need = 3;
        break;
    case 5:                             /* the bombers, in two waves */
        wp(&g->route[0], -300, -1200, 260);
        g->nroute = 1;
        for (k = 0; k < 2; k++) {
            int o = enemy(g, PT_OX, -800 + k * 140, 340, 5000 + k * 80, 0x8000, -300, -1200);
            g->p[o].objective = 1;
        }
        enemy(g, PT_HORNET, -700, 420, 5200, 0x8000, -300, 1200);   /* their escort */
        g->wave_n = 1;
        g->need = 4;
        g->lost_max = 2;
        break;
    case 6:                             /* Orme: four dumps, guns, lorries, Wasps */
        ap_add_ground(g, GT_DEPOT, FROMI(AP_DEPOT_X - 60), 0, FROMI(AP_DEPOT_Z - 40), 0, 1);
        ap_add_ground(g, GT_DEPOT, FROMI(AP_DEPOT_X + 60), 0, FROMI(AP_DEPOT_Z - 40), 0, 1);
        ap_add_ground(g, GT_DEPOT, FROMI(AP_DEPOT_X - 60), 0, FROMI(AP_DEPOT_Z + 50), 0, 1);
        ap_add_ground(g, GT_DEPOT, FROMI(AP_DEPOT_X + 60), 0, FROMI(AP_DEPOT_Z + 50), 0, 1);
        ap_add_ground(g, GT_AA, FROMI(AP_DEPOT_X + 170), 0, FROMI(AP_DEPOT_Z), 0, 0);
        ap_add_ground(g, GT_AA, FROMI(AP_DEPOT_X - 170), 0, FROMI(AP_DEPOT_Z + 20), 0, 0);
        add_on_road(g, 300, FX(0.9));
        add_on_road(g, 900, FX(0.9));
        add_on_road(g, 1600, FX(-0.9));
        enemy(g, PT_WASP, -2300, 400, 2600, 0x8000, AP_DEPOT_X, AP_DEPOT_Z);
        enemy(g, PT_WASP, -2700, 430, 2700, 0x8000, AP_DEPOT_X, AP_DEPOT_Z);
        g->need = 4;
        break;
    case 7:                             /* Kessin: three hangars, three guns, Hornets */
        for (k = 0; k < 3; k++)
            ap_add_ground(g, GT_HANGAR, FROMI(AP_ENEMY_X - 130 + k * 130), 0, FROMI(AP_ENEMY_Z + 90), 0, 1);
        ap_add_ground(g, GT_AA, FROMI(AP_ENEMY_X - 260), 0, FROMI(AP_ENEMY_Z - 60), 0, 0);
        ap_add_ground(g, GT_AA, FROMI(AP_ENEMY_X + 260), 0, FROMI(AP_ENEMY_Z - 40), 0, 0);
        ap_add_ground(g, GT_AA, FROMI(AP_ENEMY_X), 0, FROMI(AP_ENEMY_Z - 250), 0, 0);
        enemy(g, PT_HORNET, AP_ENEMY_X - 300, 420, AP_ENEMY_Z - 900, 0x8000, AP_ENEMY_X, AP_ENEMY_Z - 600);
        enemy(g, PT_HORNET, AP_ENEMY_X + 300, 460, AP_ENEMY_Z - 1000, 0x8000, AP_ENEMY_X, AP_ENEMY_Z - 600);
        g->wave_n = 1;                  /* one more takes off when a hangar burns */
        g->need = 3;
        break;
    default: {                          /* the ace and his wingmen, who wait above */
        int f = enemy(g, PT_FALCON, 0, 480, 2600, 0x8000, 0, 600);
        g->p[f].objective = 1;
        k = enemy(g, PT_HORNET, -160, 640, 2700, 0x8000, -300, 900);
        g->p[k].objective = 2;
        k = enemy(g, PT_HORNET, 160, 660, 2700, 0x8000, 300, 900);
        g->p[k].objective = 2;
        g->need = 1;
        break;
    }
    }
}

void ap_new_game(APGame *g, uint32_t seed, int mission)
{
    memset(g, 0, sizeof(*g));
    g->seed = seed;
    g->rng = seed * 2654435761UL + 1;
    g->lives = AP_LIVES;
    ap_load_mission(g, mission);
}

int ap_next(APGame *g)
{
    if (g->over || g->won) return 0;
    if (g->phase == PH_DONE) {
        if (g->mission >= AP_MISSIONS) { g->won = 1; return 0; }
        ap_load_mission(g, g->mission + 1);
    } else if (g->phase == PH_FAILED) {
        g->lives--;
        if (g->lives <= 0) { g->over = 1; return 0; }
        ap_load_mission(g, g->mission);
    }
    return 1;
}

/* ---------- flying ---------- */
static void forward(const Plane *p, fix *fx, fix *fy, fix *fz)
{
    int32_t cp = a3_cos(p->pitch);
    *fx = FMUL(cp, a3_sin(p->yaw));
    *fy = a3_sin(p->pitch);
    *fz = FMUL(cp, a3_cos(p->yaw));
}

static void crash(APGame *g, Plane *p, int idx)
{
    p->state = PS_DEAD;
    p->v = 0;
    event(g, APE_CRASH, idx, p->type, p->x, p->y, p->z);
}

void ap_fly(APGame *g, Plane *p, unsigned in)
{
    int tt = ptype[p->type].turn, idx = (int)(p - g->p);
    int pull = (in & AP_IN_NOSEUP ? 1 : 0) - (in & AP_IN_NOSEDN ? 1 : 0);
    int rollin = (in & AP_IN_RIGHT ? 1 : 0) - (in & AP_IN_LEFT ? 1 : 0);
    int32_t eff, cr, sr, k;
    fix thrust, h, gy;

    p->in = in;
    if (in & AP_IN_THR_UP) p->throttle += 5;
    if (in & AP_IN_THR_DN) p->throttle -= 5;
    if (p->throttle > 100) p->throttle = 100;
    if (p->throttle < 0) p->throttle = 0;

    eff = FDIV(p->v, AP_V_CRUISE);
    if (eff > FONE) eff = FONE;
    if (eff < FX(0.3)) eff = FX(0.3);
    if (p->stalled) eff = FX(0.25);

    if (p->state == PS_FALL) {          /* shot down: a burning spiral */
        rollin = 1; pull = 0;
        p->roll += 900;
        p->yaw = (p->yaw + 300) & 0xFFFF;
        if (p->pitch > -9000) p->pitch -= 260;
        p->throttle = 30;
    }

    if (p->on_ground) {
        p->roll = 0;
        p->yaw = (p->yaw + rollin * 200) & 0xFFFF;
        if (p->pitch < 0) p->pitch = 0;
        if (pull > 0 && p->v > V_LIFT) {
            p->on_ground = 0;
            p->pitch = 1400;
            p->y += FX(1.5);
            p->ground_t = 0;
        } else p->pitch = 0;
    } else if (p->state == PS_FLY) {
        k = ROLL_RATE * tt / 100;
        if (rollin) p->roll += (int)((rollin * k * eff) >> 16);
        else if (p->roll > 0) p->roll -= p->roll > LEVEL_RATE ? LEVEL_RATE : p->roll;
        else if (p->roll < 0) p->roll += -p->roll > LEVEL_RATE ? LEVEL_RATE : -p->roll;
        if (p->roll > AP_BANK_MAX) p->roll = AP_BANK_MAX;
        if (p->roll < -AP_BANK_MAX) p->roll = -AP_BANK_MAX;
        cr = a3_cos(p->roll); sr = a3_sin(p->roll);
        k = (PITCH_RATE * tt / 100 * eff) >> 16;
        p->pitch += (int)((pull * k * cr) >> 16);
        p->yaw += (int)((pull * k * sr) >> 16);
        k = (TURN_RATE * tt / 100 * eff) >> 16;
        p->yaw = (p->yaw + (int)((k * sr) >> 16)) & 0xFFFF;
        if (pull <= 0) p->pitch -= (int)((BANK_DROOP * (FONE - cr)) >> 16);
        /* the stall: below flying speed the nose drops and the wings rock */
        if (!p->stalled && p->v < AP_V_STALL) {
            p->stalled = 1;
            if (idx == 0) event(g, APE_STALL, 0, 0, p->x, p->y, p->z);
        } else if (p->stalled && p->v > AP_V_STALL + FX(0.4)) p->stalled = 0;
        if (p->stalled) {
            if (p->pitch > -6000) p->pitch -= 400;
            p->roll += (g->tick & 8) ? 160 : -160;
        }
    }
    if (p->pitch > AP_PITCH_MAX) p->pitch = AP_PITCH_MAX;
    if (p->pitch < -AP_PITCH_MAX) p->pitch = -AP_PITCH_MAX;
    if (p->roll > 32767) p->roll -= 65536;
    if (p->roll < -32768) p->roll += 65536;

    /* speed: thrust against drag and gravity along the flight path */
    thrust = (T_MAX / 100) * p->throttle / 100 * ptype[p->type].thrust;
    p->v += thrust - FMUL(FMUL(p->v, p->v), DRAG) - FMUL(GRAV, a3_sin(p->pitch));
    if (p->on_ground) p->v -= p->throttle < 30 ? FX(0.05) : FX(0.01);
    if (p->v > AP_V_MAX) p->v = AP_V_MAX;
    if (p->v < (p->on_ground ? 0 : FX(0.6))) p->v = p->on_ground ? 0 : FX(0.6);

    h = FMUL(p->v, a3_cos(p->pitch));
    p->x += FMUL(h, a3_sin(p->yaw));
    p->z += FMUL(h, a3_cos(p->yaw));
    p->y += FMUL(p->v, a3_sin(p->pitch));
    if (p->stalled) p->y -= FX(0.6);
    else if (!p->on_ground && p->v < V_LIFT) p->y -= FMUL(V_LIFT - p->v, FX(0.5));   /* slow: the wings hold less */
    if (p->y > AP_CEILING) { p->y = AP_CEILING; if (p->pitch > 0) p->pitch -= 120; }

    /* the edge of the map turns you back */
    if (p->x > FROMI(AP_WORLD) || p->x < -FROMI(AP_WORLD) || p->z > FROMI(AP_WORLD) || p->z < -FROMI(AP_WORLD)) {
        if (p->x > FROMI(AP_WORLD)) p->x = FROMI(AP_WORLD);
        if (p->x < -FROMI(AP_WORLD)) p->x = -FROMI(AP_WORLD);
        if (p->z > FROMI(AP_WORLD)) p->z = FROMI(AP_WORLD);
        if (p->z < -FROMI(AP_WORLD)) p->z = -FROMI(AP_WORLD);
        p->yaw = (ap_angle_to(-p->x, -p->z)) & 0xFFFF;
        if (idx == 0) event(g, APE_EDGE, 0, 0, p->x, p->y, p->z);
    }

    /* the ground */
    gy = ap_ground_y(p->x, p->z);
    if (p->on_ground) {
        p->y = gy;
        if (gy > FX(2)) crash(g, p, idx);       /* taxied into a hill */
    } else if (p->y <= gy + FX(1)) {
        int gentle = p->pitch > -1500 && p->pitch < 2400 && iabs(p->roll) < 2700 && p->v < FX(3.3);
        if (p->state == PS_FLY && gy == 0 && gentle) {
            p->on_ground = 1;
            p->y = 0;
            p->roll = 0;
            p->pitch = 0;
            p->stalled = 0;
            if (idx == 0) { g->landed++; event(g, APE_LANDED, 0, 0, p->x, p->y, p->z); }
        } else {
            p->y = gy;
            crash(g, p, idx);
        }
    }
}

/* ---------- guns ---------- */
static void spawn_bullet(APGame *g, int owner, fix x, fix y, fix z, int yaw, int pitch, fix speed)
{
    int k;
    for (k = 0; k < AP_MAX_BULLETS; k++)
        if (!g->b[k].alive) {
            Bullet *b = &g->b[k];
            int32_t cp = a3_cos(pitch);
            b->alive = 1; b->owner = owner; b->life = AP_BULLET_LIFE;
            b->dx = FMUL(speed, FMUL(cp, a3_sin(yaw)));
            b->dy = FMUL(speed, a3_sin(pitch));
            b->dz = FMUL(speed, FMUL(cp, a3_cos(yaw)));
            b->x = x; b->y = y; b->z = z;
            return;
        }
}

static void guns(APGame *g, int idx, int firing)
{
    Plane *p = &g->p[idx];
    if (p->gun_cd > 0) p->gun_cd--;
    if (p->heat > 0) p->heat -= 2;
    if (p->heat < 0) p->heat = 0;
    if (p->jam > 0) {
        if (--p->jam == 0 && idx == 0) event(g, APE_UNJAM, 0, 0, p->x, p->y, p->z);
        return;
    }
    if (!firing || p->gun_cd > 0 || p->ammo <= 0 || !ptype[p->type].gun || p->state != PS_FLY || p->on_ground) return;
    {
        int sc = ptype[p->type].scatter, yaw = p->yaw, pitch = p->pitch, n = ptype[p->type].gun;
        fix fx, fy, fz, side = p->gun_side ? FX(4) : -FX(4);
        if (sc) {
            yaw += (int)(ap_rand(g) % (uint32_t)(2 * sc)) - sc;
            pitch += (int)(ap_rand(g) % (uint32_t)(2 * sc)) - sc;
        }
        forward(p, &fx, &fy, &fz);
        while (n-- > 0 && p->ammo > 0) {    /* one gun, or both of a twin at once */
            spawn_bullet(g, idx, p->x + 10 * fx + FMUL(side, a3_cos(p->yaw)), p->y + 10 * fy,
                         p->z + 10 * fz - FMUL(side, a3_sin(p->yaw)), yaw, pitch, p->v + AP_BULLET_V);
            side = -side;
            p->ammo--;
            if (idx == 0) g->shots++;   /* rounds fired */
        }
    }
    p->gun_side ^= 1;
    p->gun_cd = 2;
    if (idx == 0) {
        p->heat += 7;
        event(g, APE_FIRE, 0, 0, p->x, p->y, p->z);
        if (p->heat >= AP_JAM_HEAT) {
            p->jam = AP_JAM_TICKS;
            event(g, APE_JAM, 0, 0, p->x, p->y, p->z);
        }
    }
}

/* the closest the segment from a (moving d in one tick) comes to c, against
 * radius r: in sixteenths of a unit */
static int sweep_hit(fix ax, fix ay, fix az, fix dx, fix dy, fix dz, fix cx, fix cy, fix cz, int r)
{
    int32_t wx = (cx - ax) >> 12, wy = (cy - ay) >> 12, wz = (cz - az) >> 12;
    int32_t ux = dx >> 12, uy = dy >> 12, uz = dz >> 12, wd, dd, qx, qy, qz, lim = (40 + r) * 16;
    if (wx > lim || wx < -lim || wy > lim || wy < -lim || wz > lim || wz < -lim) return 0;
    dd = ux * ux + uy * uy + uz * uz;
    wd = wx * ux + wy * uy + wz * uz;
    if (wd < 0) wd = 0;
    if (wd > dd) wd = dd;
    if (dd > 0) {
        /* wd <= dd < 2^19 and |u| < 2^9: the products fit 32 bits */
        qx = wx - wd * ux / dd;
        qy = wy - wd * uy / dd;
        qz = wz - wd * uz / dd;
    } else { qx = wx; qy = wy; qz = wz; }
    return qx * qx + qy * qy + qz * qz < r * r * 256;
}

static void objective_gone(APGame *g)
{
    g->done++;
    event(g, APE_OBJECTIVE, g->done, g->need, 0, 0, 0);
}

static void plane_hit(APGame *g, int t, int owner, int dmg)
{
    Plane *p = &g->p[t];
    if (p->state != PS_FLY) return;
    p->hp -= dmg;
    p->hit_t = 0;
    if (owner == 0) { g->hits++; event(g, APE_HIT, t, p->type, p->x, p->y, p->z); }
    if (t == 0) event(g, APE_HURT, owner, dmg, p->x, p->y, p->z);
    if (p->hp > 0) {
        if (t != 0 && p->ai != AI_ROUTE && p->ai != AI_HELD && p->ai != AI_FLEE && owner >= 0 && p->ai != AI_BREAK) {
            p->target = owner;           /* turn on whoever shot */
            p->ai = AI_PURSUE;
        }
        return;
    }
    p->hp = 0;
    p->state = PS_FALL;
    p->on_ground = 0;
    if (t == 0) { event(g, APE_DOWN, owner, 0, p->x, p->y, p->z); return; }
    if (p->side == SIDE_ENEMY) {
        if (owner == 0) { g->kills++; g->score += ptype[p->type].score; }
        event(g, APE_KILL, t, p->type, p->x, p->y, p->z);
        if (p->objective == 1) objective_gone(g);
    } else {
        g->lost++;
        event(g, APE_ALLY_LOST, t, p->type, p->x, p->y, p->z);
    }
}

static void ground_hit(APGame *g, int k, int owner)
{
    Ground *o = &g->gr[k];
    o->hp--;
    if (owner == 0) { g->hits++; event(g, APE_HIT, -1 - k, o->type, o->x, o->y, o->z); }
    if (o->hp > 0) return;
    o->alive = 0;
    if (owner == 0) { g->gkills++; g->score += gtype[o->type].score; }
    event(g, APE_GKILL, k, o->type, o->x, o->y + FROMI(gtype[o->type].cy), o->z);
    if (o->type == GT_LOCO) {           /* the train stops */
        int j;
        for (j = 0; j < g->ngr; j++) if (g->gr[j].line == 1) g->gr[j].ds = 0;
    }
    if (o->objective) objective_gone(g);
}

static void bullets(APGame *g)
{
    int k, t;
    for (k = 0; k < AP_MAX_BULLETS; k++) {
        Bullet *b = &g->b[k];
        int side;
        if (!b->alive) continue;
        side = g->p[b->owner].side;
        for (t = 0; t < g->np && b->alive; t++) {
            Plane *p = &g->p[t];
            if (p->state != PS_FLY || p->side == side) continue;
            if (sweep_hit(b->x, b->y, b->z, b->dx, b->dy, b->dz, p->x, p->y, p->z, ptype[p->type].r)) {
                plane_hit(g, t, b->owner, 1);
                b->alive = 0;
            }
        }
        if (side == SIDE_HOME)
            for (t = 0; t < g->ngr && b->alive; t++) {
                Ground *o = &g->gr[t];
                if (!o->alive) continue;
                if (sweep_hit(b->x, b->y, b->z, b->dx, b->dy, b->dz, o->x, o->y + FROMI(gtype[o->type].cy), o->z, gtype[o->type].r)) {
                    ground_hit(g, t, b->owner);
                    b->alive = 0;
                }
            }
        if (!b->alive) continue;
        b->x += b->dx; b->y += b->dy; b->z += b->dz;
        if (--b->life <= 0 || b->y < ap_ground_y(b->x, b->z)) b->alive = 0;
    }
}

/* ---------- anti-aircraft guns ---------- */
static void flak(APGame *g)
{
    Plane *p = &g->p[0];
    int k;
    for (k = 0; k < g->ngr; k++) {
        Ground *o = &g->gr[k];
        int32_t dx, dz, d, alt;
        if (o->type != GT_AA || !o->alive) continue;
        if (o->cd > 0) { o->cd--; continue; }
        if (p->state != PS_FLY || p->on_ground || g->phase >= PH_DONE) continue;
        dx = (p->x - o->x) >> 16; dz = (p->z - o->z) >> 16; alt = p->y >> 16;
        d = a3_hypot(dx, dz);
        if (d > 1400 || alt < 50) continue;
        {
            int j, fuse = 18 + (int)(a3_hypot(d, alt) / 90);
            int32_t err = 36 + d / 9 + (alt < 200 ? (200 - alt) / 2 : 0);   /* hard to lay on a low aeroplane */
            fix h = FMUL(p->v, a3_cos(p->pitch));
            for (j = 0; j < AP_MAX_FLAK; j++)
                if (!g->fk[j].alive) {
                    Flak *f = &g->fk[j];
                    f->alive = 1; f->t = 0; f->fuse = fuse;
                    f->x = p->x + FMUL(h, a3_sin(p->yaw)) * fuse + FROMI((int32_t)(ap_rand(g) % (uint32_t)(2 * err)) - err);
                    f->y = p->y + FMUL(p->v, a3_sin(p->pitch)) * fuse + FROMI((int32_t)(ap_rand(g) % (uint32_t)err) - err / 2);
                    f->z = p->z + FMUL(h, a3_cos(p->yaw)) * fuse + FROMI((int32_t)(ap_rand(g) % (uint32_t)(2 * err)) - err);
                    break;
                }
            o->cd = 60 + (int)(ap_rand(g) % 40);
        }
    }
    for (k = 0; k < AP_MAX_FLAK; k++) {
        Flak *f = &g->fk[k];
        if (!f->alive) continue;
        f->t++;
        if (f->t == f->fuse) {
            int32_t d = (int32_t)(fhypot3(p->x - f->x, p->y - f->y, p->z - f->z) >> 16);
            event(g, APE_FLAK, k, 0, f->x, f->y, f->z);
            if (p->state == PS_FLY && d < 42) plane_hit(g, 0, -1, d < 20 ? 3 : 1);
        }
        if (f->t > f->fuse + 30) f->alive = 0;
    }
}

/* ---------- steering: the computer's pilots and the autopilot ---------- */
unsigned ap_steer(const APGame *g, const Plane *p, fix tx, fix ty, fix tz, int hard)
{
    fix dx = tx - p->x, dy = ty - p->y, dz = tz - p->z, hd = fhypot(dx, dz);
    int dyaw = sang(ap_angle_to(dx, dz) - p->yaw);
    int want_pitch = sang(a3_atan2(dy >> 4, hd >> 4));
    int lim = hard ? 6400 : 4500, want_roll, bank = hard ? AP_BANK_MAX : 10000;
    int32_t alt = (p->y - ap_ground_y(p->x, p->z)) >> 16;
    unsigned in = AP_IN_THR_UP;
    (void)g;
    if (want_pitch > lim) want_pitch = lim;
    if (want_pitch < -lim) want_pitch = -lim;
    want_roll = dyaw * 4;
    if (want_roll > bank) want_roll = bank;
    if (want_roll < -bank) want_roll = -bank;
    /* the ground first */
    if (alt < 130 && want_pitch < 1800) want_pitch = 1800;
    if (alt < 70) { want_pitch = 3500; if (want_roll > 4000) want_roll = 4000; if (want_roll < -4000) want_roll = -4000; }
    if (p->v < AP_V_STALL + FX(0.5) && want_pitch > 0) want_pitch = 0;   /* too slow to climb */
    if (want_roll > p->roll + 350) in |= AP_IN_RIGHT;
    else if (want_roll < p->roll - 350) in |= AP_IN_LEFT;
    else if (want_roll > 1500 && (g->tick & 1)) in |= AP_IN_RIGHT;   /* hold the bank against its easing off */
    else if (want_roll < -1500 && (g->tick & 1)) in |= AP_IN_LEFT;
    if (iabs(dyaw) > 2500 && iabs(p->roll) > 4000 && sang(want_pitch - p->pitch) > -1500 && alt > 70) in |= AP_IN_NOSEUP;
    else if (sang(want_pitch - p->pitch) > 250) in |= AP_IN_NOSEUP;
    else if (sang(want_pitch - p->pitch) < -250) in |= AP_IN_NOSEDN;
    return in;
}

/* the point to aim at for a burst to meet the target */
static void lead(const Plane *p, const Plane *t, fix *ax, fix *ay, fix *az)
{
    fix d = fhypot3(t->x - p->x, t->y - p->y, t->z - p->z);
    int32_t ticks = (int32_t)(d / (p->v + AP_BULLET_V));
    fix h = FMUL(t->v, a3_cos(t->pitch));
    if (ticks > 40) ticks = 40;
    *ax = t->x + FMUL(h, a3_sin(t->yaw)) * ticks;
    *ay = t->y + FMUL(t->v, a3_sin(t->pitch)) * ticks;
    *az = t->z + FMUL(h, a3_cos(t->yaw)) * ticks;
}

/* how far off the nose a point is: the larger of the yaw and pitch errors */
static int off_nose(const Plane *p, fix x, fix y, fix z)
{
    fix dx = x - p->x, dy = y - p->y, dz = z - p->z;
    int ey = iabs(sang(ap_angle_to(dx, dz) - p->yaw));
    int ep = iabs(sang(a3_atan2(dy >> 4, fhypot(dx, dz) >> 4) - p->pitch));
    return ey > ep ? ey : ep;
}

static int nearest_foe(const APGame *g, int idx, int32_t range)
{
    const Plane *p = &g->p[idx];
    int k, best = -1;
    int32_t bd = range;
    for (k = 0; k < g->np; k++) {
        const Plane *t = &g->p[k];
        int32_t d;
        if (k == idx || t->state != PS_FLY || t->side == p->side) continue;
        d = (int32_t)(fhypot3(t->x - p->x, t->y - p->y, t->z - p->z) >> 16);
        if (t->type == PT_HERON) d -= 300;      /* the escorted are the prize */
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

static void rear_gun(APGame *g, int idx)
{
    Plane *p = &g->p[idx];
    int t;
    if (p->rear_cd > 0) { p->rear_cd--; return; }
    t = nearest_foe(g, idx, 260);
    if (t < 0 || off_nose(p, g->p[t].x, g->p[t].y, g->p[t].z) < 4500) return;   /* the gunner can't fire through the propeller */
    {
        const Plane *q = &g->p[t];
        fix ax, ay, az, dx, dy, dz;
        int sc = ptype[p->type].scatter, yaw, pitch;
        lead(p, q, &ax, &ay, &az);
        dx = ax - p->x; dy = ay - p->y; dz = az - p->z;
        sc += sc / 2;                   /* from a swaying cockpit */
        yaw = ap_angle_to(dx, dz) + (int)(ap_rand(g) % (uint32_t)(2 * sc)) - sc;
        pitch = sang(a3_atan2(dy >> 4, fhypot(dx, dz) >> 4)) + (int)(ap_rand(g) % (uint32_t)(2 * sc)) - sc;
        spawn_bullet(g, idx, p->x, p->y + FX(3), p->z, yaw, pitch, AP_BULLET_V);
        p->rear_cd = 11;
    }
}

static unsigned fighter(APGame *g, int idx)
{
    Plane *p = &g->p[idx];
    unsigned in = 0;
    int32_t alt = (p->y - ap_ground_y(p->x, p->z)) >> 16;
    if (p->ai_t > 0) p->ai_t--;
    if (p->hp <= p->maxhp / 4 && p->type != PT_FALCON && p->ai != AI_FLEE) {
        p->ai = AI_FLEE;
        p->tx = p->x; p->tz = FROMI(AP_WORLD - 400);
    }
    if (alt < 110 && p->ai != AI_CLIMB && p->ai != AI_FLEE) { p->ai = AI_CLIMB; p->ai_t = 40; }
    switch (p->ai) {
    case AI_CLIMB:
        in = ap_steer(g, p, p->x + 200 * a3_sin(p->yaw), p->y + FROMI(150), p->z + 200 * a3_cos(p->yaw), 0);
        if (p->ai_t == 0 && alt > 200) p->ai = AI_PATROL;
        break;
    case AI_FLEE:
        in = ap_steer(g, p, p->tx, FROMI(200), p->tz, 0);
        break;
    case AI_BREAK:
        in = AP_IN_THR_UP | AP_IN_NOSEUP | (p->wp ? AP_IN_RIGHT : AP_IN_LEFT);
        if (p->ai_t == 0) p->ai = AI_PURSUE;
        break;
    case AI_PURSUE: {
        const Plane *t = p->target >= 0 ? &g->p[p->target] : NULL;
        fix ax, ay, az;
        int32_t d;
        if (!t || t->state != PS_FLY || (g->tick + idx) % 25 == 0) {
            p->target = nearest_foe(g, idx, 3200);
            if (p->target < 0) { p->ai = AI_PATROL; break; }
            t = &g->p[p->target];
        }
        d = (int32_t)(fhypot3(t->x - p->x, t->y - p->y, t->z - p->z) >> 16);
        lead(p, t, &ax, &ay, &az);
        in = ap_steer(g, p, ax, ay, az, 1);
        if (d < 360 && off_nose(p, ax, ay, az) < 950 && p->rest == 0) {
            in |= AP_IN_FIRE;
            if (++p->burst > 14) { p->burst = 0; p->rest = 22; }
        }
        if (p->rest > 0) p->rest--;
        if (d < 90 || (d < 240 && off_nose(t, p->x, p->y, p->z) < 3500 && off_nose(p, t->x, t->y, t->z) < 3500)) {
            /* too close, or head on: break away, climbing over (the player dives under) */
            p->ai = AI_BREAK; p->ai_t = 20;
            p->wp = sang(ap_angle_to(t->x - p->x, t->z - p->z) - p->yaw) < 0;   /* away from it */
        }
        /* someone on its tail: break, now and then */
        else if (d < 330 && off_nose(t, p->x, p->y, p->z) < 2000 && ap_rand(g) % 50 == 0) {
            p->ai = AI_BREAK; p->ai_t = 25 + (int)(ap_rand(g) % 25); p->wp = (int)(ap_rand(g) & 1);
        }
        break;
    }
    default:                            /* patrol: circle the post, look for a fight */
        if (p->objective == 2) {        /* the ace's wingmen watch, until he is hurt */
            int k, waiting = 0;
            for (k = 1; k < g->np; k++)
                if (g->p[k].type == PT_FALCON && g->p[k].state == PS_FLY && g->p[k].hp * 3 > g->p[k].maxhp * 2) waiting = 1;
            if (waiting) {
                p->tx = g->p[0].x; p->tz = g->p[0].z;
                in = ap_steer(g, p, p->x + 300 * a3_sin(p->yaw + 9000), FROMI(p->talt), p->z + 300 * a3_cos(p->yaw + 9000), 0);
                break;
            }
            p->objective = 0;
        }
        if ((g->tick + idx) % 10 == 0) {
            int t = nearest_foe(g, idx, 1800);
            if (t >= 0) { p->ai = AI_PURSUE; p->target = t; break; }
        }
        {
            int32_t dx = (p->tx - p->x) >> 16, dz = (p->tz - p->z) >> 16;
            if (a3_hypot(dx, dz) > 500) in = ap_steer(g, p, p->tx, FROMI(p->talt), p->tz, 0);
            else in = ap_steer(g, p, p->x + 300 * a3_sin(p->yaw + 9000), FROMI(p->talt), p->z + 300 * a3_cos(p->yaw + 9000), 0);
        }
        break;
    }
    if (p->ai != AI_PURSUE) p->burst = 0;
    return in;
}

/* the escorted two-seaters and the bombers: along the route */
static unsigned route_flyer(APGame *g, int idx)
{
    Plane *p = &g->p[idx];
    if (p->home) return ap_steer(g, p, p->tx, FROMI(p->talt), p->tz, 0);
    if (p->type == PT_HERON) {
        if (p->wp < g->nroute) {
            const Waypoint *w = &g->route[p->wp];
            int32_t d = a3_hypot((w->x - p->x) >> 16, (w->z - p->z) >> 16);
            if (d < 220) {
                p->wp++;
                if (p->wp == g->nroute) { p->ai_t = g->loiter; event(g, APE_ARRIVE, idx, p->type, p->x, p->y, p->z); }
            }
            return ap_steer(g, p, w->x, FROMI(w->alt), w->z, 0);
        }
        if (p->ai_t > 0) {              /* photographing: a wide circle over it */
            p->ai_t--;
            return ap_steer(g, p, p->x + 300 * a3_sin(p->yaw + 7000), FROMI(320), p->z + 300 * a3_cos(p->yaw + 7000), 0);
        }
        if (home_dist(p->x, p->z) < AP_HOME_R) {
            p->home = 1;
            p->tx = p->x; p->tz = p->z - FROMI(2000);
            event(g, APE_ARRIVE, idx, p->type, p->x, p->y, p->z);
        }
        return ap_steer(g, p, FROMI(AP_HOME_X), FROMI(300), FROMI(AP_HOME_Z), 0);
    }
    /* the bomber: to the target, the bombs, then away north */
    {
        int32_t d = a3_hypot((p->tx - p->x) >> 16, (p->tz - p->z) >> 16);
        if (d < 120) {
            g->lost++;
            event(g, APE_BOMBS, idx, p->type, p->x, p->y, p->z);
            p->home = 1;
            if (p->objective) g->need--;    /* through: no longer one to shoot down */
            p->objective = 0;
            p->tx = p->x; p->tz = FROMI(AP_WORLD - 300);
        }
        return ap_steer(g, p, p->tx, FROMI(p->talt), p->tz, 0);
    }
}

static void held_fly(Plane *p)
{
    fix h = FMUL(p->v, a3_cos(p->pitch));
    p->x += FMUL(h, a3_sin(p->yaw));
    p->z += FMUL(h, a3_cos(p->yaw));
    p->y += FMUL(p->v, a3_sin(p->pitch));
}

/* ---------- the autopilot ---------- */
static int nearest_ground(const APGame *g, int objective_only)
{
    const Plane *p = &g->p[0];
    int k, best = -1;
    int32_t bd = 0x7FFFFFFF;
    for (k = 0; k < g->ngr; k++) {
        const Ground *o = &g->gr[k];
        int32_t d;
        if (!o->alive || (objective_only && !o->objective)) continue;
        d = a3_hypot((o->x - p->x) >> 16, (o->z - p->z) >> 16);
        if (d < bd) { bd = d; best = k; }
    }
    return best;
}

unsigned ap_bot(APGame *g)
{
    Plane *p = &g->p[0];
    unsigned in;
    int t, k;
    int32_t alt;
    if (p->state != PS_FLY) return 0;
    if (p->on_ground) return AP_IN_THR_UP | (p->v > V_LIFT + FX(0.1) ? AP_IN_NOSEUP : 0);
    alt = (p->y - ap_ground_y(p->x, p->z)) >> 16;
    if (g->bot_t > 0) g->bot_t--;
    if (g->bot_mode == 1 && g->bot_t > 0)    /* climbing away after a strafing run */
        return ap_steer(g, p, p->x + 400 * a3_sin(p->yaw), FROMI(280), p->z + 400 * a3_cos(p->yaw), 0);
    if (g->bot_mode == 1) g->bot_mode = 2;    /* then out, to come round again */
    /* aeroplanes first: whatever is near, or (defending) the bombers anywhere */
    t = nearest_foe(g, 0, g->mission == 5 ? 6000 : g->ngr ? 900 : 1500);
    if (g->phase == PH_OUT && g->mission == 3) {    /* escorting: whoever is near a Heron */
        int best = -1;
        int32_t bd = 1300;
        for (k = 1; k < g->np; k++) {
            const Plane *h = &g->p[k];
            int j;
            if (h->type != PT_HERON || h->state != PS_FLY) continue;
            for (j = 1; j < g->np; j++) {
                const Plane *e = &g->p[j];
                int32_t d;
                if (e->side != SIDE_ENEMY || e->state != PS_FLY) continue;
                d = (int32_t)(fhypot3(e->x - h->x, e->y - h->y, e->z - h->z) >> 16);
                if (d < bd) { bd = d; best = j; }
            }
        }
        if (best >= 0) t = best;
    }
    if ((g->mission == 5 || g->mission == 8) && g->phase == PH_OUT) {   /* the bombers, the ace: before the rest */
        int obj = -1;
        for (k = 1; k < g->np; k++)
            if (g->p[k].state == PS_FLY && g->p[k].objective == 1) { obj = k; break; }
        t = nearest_foe(g, 0, 500);
        if (t >= 0 && g->p[t].objective == 2) t = -1;        /* leave the waiting wingmen be */
        if (t >= 0 && obj >= 0 && g->p[obj].type == PT_OX && off_nose(&g->p[t], p->x, p->y, p->z) > 3000) t = -1;
        if (t < 0) t = obj;             /* a fighter on your tail first, then the bombers */
    }
    if (t >= 0 && g->p[t].ai == AI_FLEE && g->p[t].objective == 0 && g->phase != PH_OUT) t = -1;
    for (k = 1; k < g->np; k++) {       /* anything about to hit you: up and away */
        const Plane *q = &g->p[k];
        if (q->state == PS_FLY && fabs_(q->x - p->x) < FX(45) && fabs_(q->z - p->z) < FX(45) && fabs_(q->y - p->y) < FX(25))
            return AP_IN_THR_UP | (q->y > p->y ? AP_IN_NOSEDN : AP_IN_NOSEUP) |
                   (sang(ap_angle_to(q->x - p->x, q->z - p->z) - p->yaw) > 0 ? AP_IN_LEFT : AP_IN_RIGHT);
    }
    if (t >= 0) {
        const Plane *q = &g->p[t];
        fix ax, ay, az;
        int32_t d = (int32_t)(fhypot3(q->x - p->x, q->y - p->y, q->z - p->z) >> 16);
        lead(p, q, &ax, &ay, &az);
        in = ap_steer(g, p, ax, ay, az, 1);
        if (d < 380 && off_nose(p, ax, ay, az) < 700 && p->heat < 80 && !p->jam) in |= AP_IN_FIRE;
        if ((d < 110 || (d < 260 && off_nose(q, p->x, p->y, p->z) < 3500)) && off_nose(p, q->x, q->y, q->z) < 5000) {
            /* don't ram it: under a head-on pass, or over the top of one ahead */
            int under = d >= 110 || q->y > p->y;
            in = AP_IN_THR_UP | (in & AP_IN_FIRE) | (under ? AP_IN_NOSEDN : AP_IN_NOSEUP);
            if (d < 70) in &= ~AP_IN_FIRE;
        }
        return in;
    }
    if (g->phase == PH_OUT) {
        /* a ground target: line up from height, dive at it, fire, climb away */
        int k2 = nearest_ground(g, 1);
        if (k2 < 0 && g->mission == 6) k2 = nearest_ground(g, 0);
        if (k2 >= 0 && g->gr[k2].type != GT_BALLOON)       /* the guns round it first */
            for (k = 0; k < g->ngr; k++) {
                const Ground *a = &g->gr[k];
                if (a->alive && a->type == GT_AA && !a->line &&
                    a3_hypot((a->x - g->gr[k2].x) >> 16, (a->z - g->gr[k2].z) >> 16) < 450 &&
                    a3_hypot((a->x - p->x) >> 16, (a->z - p->z) >> 16) <
                    a3_hypot((g->gr[k2].x - p->x) >> 16, (g->gr[k2].z - p->z) >> 16) + 200) { k2 = k; break; }
            }
        if (k2 >= 0) {
            const Ground *o = &g->gr[k2];
            fix cy = o->y + FROMI(gtype[o->type].cy), ox = o->x, oz = o->z;
            int32_t hd = a3_hypot((o->x - p->x) >> 16, (o->z - p->z) >> 16);
            int32_t aim_alt = (o->y >> 16) + 15;
            if (o->ds) {                /* moving: aim where it will be */
                int32_t ticks = (int32_t)(fhypot3(o->x - p->x, cy - p->y, o->z - p->z) / (p->v + AP_BULLET_V));
                fix s2 = o->s + o->ds * ticks;
                if (o->line == 1) { ox = s2; oz = FROMI(ap_rail_z(s2 >> 16)); }
                else ap_road_pos(s2, &ox, &oz);
            }
            if (o->type == GT_BALLOON) {
                in = ap_steer(g, p, o->x, cy, o->z, 1);
                if (hd < 420 && off_nose(p, o->x, cy, o->z) < 900 && p->heat < 85 && !p->jam) in |= AP_IN_FIRE;
                if (hd < 70) { g->bot_mode = 1; g->bot_t = 40; }
                return in;
            }
            {
                int err = iabs(sang(ap_angle_to(o->x - p->x, o->z - p->z) - p->yaw));
                /* too close to line up: extend away, then turn in */
                if (g->bot_mode == 2 && hd < 900) return ap_steer(g, p, p->x - (o->x - p->x), FROMI(aim_alt + 260), p->z - (o->z - p->z), 0);
                g->bot_mode = 0;
                if (hd < 650 && err > 3500) { g->bot_mode = 2; return ap_steer(g, p, p->x - (o->x - p->x), FROMI(aim_alt + 260), p->z - (o->z - p->z), 0); }
                if (err > 2500 || hd > 850) return ap_steer(g, p, o->x, FROMI(hd > 1500 ? 330 : aim_alt + 260), o->z, 0);
            }
            in = ap_steer(g, p, ox, cy, oz, 1);
            if (hd < 450 && off_nose(p, ox, cy, oz) < 800 && p->heat < 85 && !p->jam) in |= AP_IN_FIRE;
            if (alt < 70 || hd < 90) { g->bot_mode = 1; g->bot_t = 45; }
            return in;
        }
        if (g->wp_next < g->nwp) {
            const Waypoint *w = &g->wp[g->wp_next];
            return ap_steer(g, p, w->x, FROMI(w->alt), w->z, 0);
        }
        if (g->mission == 3) {          /* keep with the Herons */
            for (k = 1; k < g->np; k++)
                if (g->p[k].type == PT_HERON && g->p[k].state == PS_FLY && !g->p[k].home) {
                    const Plane *h = &g->p[k];
                    return ap_steer(g, p, h->x - 200 * a3_sin(h->yaw), h->y + FROMI(60), h->z - 200 * a3_cos(h->yaw), 0);
                }
        }
        t = nearest_foe(g, 0, 9000);    /* nothing else to do: find a fight */
        if (t >= 0 && (g->p[t].objective || g->mission == 8)) return ap_steer(g, p, g->p[t].x, g->p[t].y, g->p[t].z, 0);
        if (g->mission == 5)            /* waiting for the bombers over the front */
            return ap_steer(g, p, FROMI(-200), FROMI(420), FROMI(1200), 0);
    }
    return ap_steer(g, p, FROMI(AP_HOME_X), FROMI(300), FROMI(AP_HOME_Z), 0);
}

/* ---------- the mission's progress ---------- */
static void mission_update(APGame *g)
{
    Plane *p = &g->p[0];
    int k, n;
    if (g->phase >= PH_DONE) return;
    /* the patrol line */
    if (g->wp_next < g->nwp) {
        const Waypoint *w = &g->wp[g->wp_next];
        if (a3_hypot((w->x - p->x) >> 16, (w->z - p->z) >> 16) < 260) {
            g->wp_next++;
            g->score += 100;
            event(g, APE_WAYPOINT, g->wp_next, g->nwp, w->x, FROMI(w->alt), w->z);
        }
    }
    switch (g->mission) {
    case 1:
        if (g->wave_n && g->wp_next >= 1) {
            g->wave_n = 0;
            enemy(g, PT_WASP, 0, 420, 2300, 0x8000, 0, 600);
            enemy(g, PT_WASP, 200, 460, 2400, 0x8000, 600, 600);
        }
        if (g->phase == PH_OUT && g->wp_next >= g->nwp) { g->phase = PH_HOME; event(g, APE_HOMEWARD, 0, 0, 0, 0, 0); }
        break;
    case 3:
        for (k = 1, n = 0; k < g->np; k++) if (g->p[k].type == PT_HERON && g->p[k].state == PS_FLY && g->p[k].wp >= 1) n++;
        if (g->wave_n == 2 && n) {
            g->wave_n = 1;
            enemy(g, PT_WASP, -1900, 420, 2200, 0x8000, -1500, 900);
            enemy(g, PT_WASP, -1700, 450, 2300, 0x8000, -1500, 900);
        }
        for (k = 1, n = 0; k < g->np; k++) if (g->p[k].type == PT_HERON && g->p[k].ai_t > 0 && g->p[k].wp >= g->nroute) n++;
        if (g->wave_n == 1 && n) {
            g->wave_n = 0;
            enemy(g, PT_WASP, -3800, 450, 3200, 0x6000, AP_DEPOT_X, AP_DEPOT_Z);
            enemy(g, PT_WASP, -1200, 430, 3400, 0xA000, AP_DEPOT_X, AP_DEPOT_Z);
        }
        for (k = 1, n = 0; k < g->np; k++) if (g->p[k].type == PT_HERON && g->p[k].home && g->p[k].state == PS_FLY) n++;
        if (g->phase == PH_OUT && n > 0) {
            int flying = 0;
            for (k = 1; k < g->np; k++) if (g->p[k].type == PT_HERON && g->p[k].state == PS_FLY && !g->p[k].home) flying++;
            if (!flying) { g->done = 1; event(g, APE_OBJECTIVE, 1, 1, 0, 0, 0); }
        }
        break;
    case 4: {
        const Ground *loco = &g->gr[0];
        if (g->phase == PH_OUT && loco->alive && loco->s > FROMI(4600)) {
            g->phase = PH_FAILED; g->end_t = AP_END_TICKS;
            event(g, APE_FAILED, 1, 0, loco->x, 0, loco->z);
        }
        break;
    }
    case 5:
        if (g->wave_n && g->mtick >= 900) {
            g->wave_n = 0;
            for (k = 0; k < 2; k++) {
                int o = enemy(g, PT_OX, 700 + k * 150, 360, 5200 + k * 70, 0x8000, -300, -1200);
                if (o > 0) g->p[o].objective = 1;
            }
        }
        break;
    case 7:
        if (g->wave_n && g->done >= 1) {
            g->wave_n = 0;
            k = enemy(g, PT_HORNET, AP_ENEMY_X, 30, AP_ENEMY_Z - 300, 0x8000, AP_ENEMY_X, AP_ENEMY_Z - 600);
            if (k > 0) { g->p[k].ai = AI_CLIMB; g->p[k].ai_t = 60; }
        }
        break;
    }
    if (g->phase == PH_OUT && g->lost >= g->lost_max) {
        g->phase = PH_FAILED; g->end_t = AP_END_TICKS;
        event(g, APE_FAILED, 2, g->lost, 0, 0, 0);
        return;
    }
    if (g->phase == PH_OUT && g->need > 0 && g->done >= g->need) {
        g->phase = PH_HOME;
        g->score += 300;
        event(g, APE_HOMEWARD, 0, 0, 0, 0, 0);
    }
    if (g->phase == PH_HOME && p->state == PS_FLY && home_dist(p->x, p->z) < AP_HOME_R) {
        g->phase = PH_DONE;
        g->end_t = AP_END_TICKS;
        g->missions_flown++;
        g->score += 1000 + 20 * p->hp;
        event(g, APE_DONE, g->mission, 0, p->x, p->y, p->z);
    }
}

static void move_ground(APGame *g)
{
    int k;
    for (k = 0; k < g->ngr; k++) {
        Ground *o = &g->gr[k];
        if (!o->alive || !o->ds) continue;
        o->s += o->ds;
        if (o->line == 1) {
            int32_t x = o->s >> 16;
            o->x = o->s;
            o->z = FROMI(ap_rail_z(x));
            o->yaw = ap_angle_to(FROMI(8), FROMI(ap_rail_z(x + 8) - ap_rail_z(x)));
        } else if (o->line == 2) {
            if (o->s > AP_ROAD_LEN || o->s < 0) { o->ds = -o->ds; o->s += 2 * o->ds; }
            ap_road_pos(o->s, &o->x, &o->z);
            o->yaw = o->ds > 0 ? ap_angle_to(FROMI(1300), -FROMI(1900)) : ap_angle_to(-FROMI(1300), FROMI(1900));
        }
    }
}

void ap_tick(APGame *g, unsigned in)
{
    Plane *p = &g->p[0];
    int k;
    if (g->over || g->won) return;
    g->tick++;
    g->mtick++;
    if (g->end_t > 0) g->end_t--;
    if (g->phase >= PH_DONE) in &= ~AP_IN_FIRE;
    /* you */
    if (p->state != PS_DEAD) {
        ap_fly(g, p, p->state == PS_FLY ? in : 0);
        guns(g, 0, (in & AP_IN_FIRE) != 0);
        if (p->on_ground && ap_on_runway(p->x, p->z) && p->v < FX(0.2) && p->state == PS_FLY) {
            if (++p->ground_t == 50) {
                p->ammo = AP_AMMO; p->hp = AP_HP; p->heat = 0; p->jam = 0;
                event(g, APE_REARMED, 0, 0, p->x, p->y, p->z);
            }
        } else p->ground_t = 0;
    }
    if (p->hit_t < 999) p->hit_t++;
    /* everyone else */
    for (k = 1; k < g->np; k++) {
        Plane *q = &g->p[k];
        unsigned qi = 0;
        if (q->state == PS_DEAD) continue;
        if (q->hit_t < 999) q->hit_t++;
        if (q->state == PS_FLY) {
            if (q->held) { held_fly(q); continue; }
            qi = q->ai == AI_ROUTE ? route_flyer(g, k) : fighter(g, k);
            if (ptype[q->type].rear) rear_gun(g, k);
        }
        ap_fly(g, q, qi);
        guns(g, k, (qi & AP_IN_FIRE) != 0);
    }
    /* collisions between aeroplanes */
    for (k = 1; k < g->np; k++) {
        Plane *q = &g->p[k];
        if (q->state != PS_FLY || p->state != PS_FLY) continue;
        if (fabs_(q->x - p->x) < FX(8) && fabs_(q->y - p->y) < FX(4) && fabs_(q->z - p->z) < FX(8) && q->hit_t > 10) {
            plane_hit(g, k, 0, 12);       /* a glancing blow: a real one would end you both */
            plane_hit(g, 0, k, 12);
        }
    }
    bullets(g);
    flak(g);
    move_ground(g);
    if (p->state != PS_FLY && g->phase < PH_DONE) {   /* shot down, or into the ground */
        g->phase = PH_FAILED; g->end_t = AP_END_TICKS;
        event(g, APE_FAILED, 0, 0, p->x, p->y, p->z);
    }
    mission_update(g);
}

int ap_enemies_left(const APGame *g)
{
    int k, n = 0;
    for (k = 1; k < g->np; k++) if (g->p[k].side == SIDE_ENEMY && g->p[k].state == PS_FLY) n++;
    return n;
}

int ap_objective_pos(const APGame *g, fix *x, fix *z)
{
    const Plane *p = &g->p[0];
    int k, best = -1;
    int32_t bd = 0x7FFFFFFF;
    if (g->phase == PH_HOME) { *x = FROMI(AP_HOME_X); *z = FROMI(AP_HOME_Z); return 1; }
    if (g->phase != PH_OUT) return 0;
    if (g->wp_next < g->nwp) { *x = g->wp[g->wp_next].x; *z = g->wp[g->wp_next].z; return 1; }
    for (k = 0; k < g->ngr; k++)
        if (g->gr[k].alive && g->gr[k].objective) {
            int32_t d = a3_hypot((g->gr[k].x - p->x) >> 16, (g->gr[k].z - p->z) >> 16);
            if (d < bd) { bd = d; best = k; }
        }
    if (best >= 0) { *x = g->gr[best].x; *z = g->gr[best].z; return 1; }
    for (k = 1; k < g->np; k++) {
        const Plane *q = &g->p[k];
        if (q->state == PS_FLY && (q->objective == 1 || (g->mission == 3 && q->type == PT_HERON && !q->home))) {
            int32_t d = a3_hypot((q->x - p->x) >> 16, (q->z - p->z) >> 16);
            if (d < bd) { bd = d; best = k; }
        }
    }
    if (best >= 0) { *x = g->p[best].x; *z = g->p[best].z; return 1; }
    return 0;
}

static void put_num(char *b, int n)
{
    char t[12];
    int k = 0, j = 0;
    if (n < 0) { b[j++] = '-'; n = -n; }
    do { t[k++] = (char)('0' + n % 10); n /= 10; } while (n && k < 11);
    while (k) b[j++] = t[--k];
    b[j] = 0;
}

void ap_objective_text(const APGame *g, char *buf)
{
    char n1[12], n2[12];
    buf[0] = 0;
    switch (g->phase) {
    case PH_HOME: strcpy(buf, "RETURN TO LARKSPUR FIELD"); return;
    case PH_DONE: strcpy(buf, "MISSION COMPLETE"); return;
    case PH_FAILED: strcpy(buf, "MISSION FAILED"); return;
    }
    if (g->wp_next < g->nwp) {
        put_num(n1, g->wp_next + 1); put_num(n2, g->nwp);
        strcpy(buf, "PATROL MARKER "); strcat(buf, n1); strcat(buf, " OF "); strcat(buf, n2);
        return;
    }
    put_num(n1, g->done); put_num(n2, g->need);
    switch (g->mission) {
    case 2: strcpy(buf, "BALLOONS "); break;
    case 3: strcpy(buf, "ESCORT THE HERONS"); return;
    case 4: strcpy(buf, "TRAIN "); break;
    case 5: strcpy(buf, "BOMBERS "); break;
    case 6: strcpy(buf, "SUPPLY DUMPS "); break;
    case 7: strcpy(buf, "HANGARS "); break;
    case 8: strcpy(buf, "THE GREY FALCON"); return;
    default: strcpy(buf, "TARGETS "); break;
    }
    strcat(buf, n1); strcat(buf, "/"); strcat(buf, n2);
}

uint32_t ap_state_hash(const APGame *g)
{
    uint32_t h = 2166136261UL;
    int i, j;
#define MIX(v) do { h ^= (uint32_t)(v); h *= 16777619UL; } while (0)
    for (i = 0; i < g->np; i++) {
        const Plane *p = &g->p[i];
        int32_t v[15];
        v[0] = p->type; v[1] = p->state; v[2] = p->hp; v[3] = p->x; v[4] = p->y; v[5] = p->z; v[6] = p->v; v[7] = p->yaw; v[8] = p->pitch; v[9] = p->roll; v[10] = p->throttle; v[11] = p->heat; v[12] = p->ammo; v[13] = p->ai; v[14] = p->on_ground;
        for (j = 0; j < 15; j++) MIX(v[j]);
    }
    for (i = 0; i < g->ngr; i++) { MIX(g->gr[i].alive); MIX(g->gr[i].hp); MIX(g->gr[i].x); MIX(g->gr[i].z); }
    for (i = 0; i < AP_MAX_BULLETS; i++) if (g->b[i].alive) { MIX(g->b[i].x); MIX(g->b[i].y); MIX(g->b[i].z); }
    {
        int32_t v[15];
        v[0] = g->score; v[1] = g->mission; v[2] = g->phase; v[3] = g->tick; v[4] = g->mtick; v[5] = g->kills; v[6] = g->gkills; v[7] = g->shots; v[8] = g->hits; v[9] = g->lives; v[10] = g->over; v[11] = g->won; v[12] = g->done; v[13] = g->lost; v[14] = (int32_t)g->rng;
        for (i = 0; i < 15; i++) MIX(v[i]);
    }
#undef MIX
    return h;
}
