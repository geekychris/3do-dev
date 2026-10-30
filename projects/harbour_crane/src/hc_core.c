// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * hc_core.c - Harbour Crane's rules: the trolley, the winch and the
 * swinging load, landing containers, the ship's list and roll, the wind,
 * the levels and the autopilot. 16.16 fixed point throughout, so the
 * host, the classic Amiga and AmigaOS 4 play a seed the same way.
 *
 * The load is a pendulum on the trolley: its angle th from straight down
 * turns at om, pulled back by gravity, pushed by the trolley's
 * acceleration and the wind, and sped up as the winch shortens the cable
 * (and slowed as it lets it out), integrated four times a tick.
 */
#include <string.h>
#include "a3d.h"
#include "hc.h"

const char *const hc_event_names[HCE_COUNT] = {
    "NONE", "GRAB", "RELEASE", "LAND", "DENT", "LOST", "BUMP", "SPLASH",
    "CLEAR", "LEVEL", "CAPSIZE", "TIMEOUT", "OVER", "WIN", "GUST"
};

#define G          FX(1.4)              /* the pendulum's gravity: a 200-unit cable swings in 3 s */
#define G_FALL     FX(1.6)              /* a dropped box */
#define VMAX       FX(5)
#define TH_MAX     FX(1.2)
#define TWO_PI     FX(6.2831853)
#ifndef APROF
#define APROF FX(0.04)                  /* the autopilot's planned braking: gentler than the trolley's */
#endif
#ifndef KOFF
#define KOFF FX(0.06)                   /* the autopilot chases the load's offset: damps its swing */
#endif
#define SWELL_STEP FX(0.0418879)        /* 2 pi / 150: a 6 s swell */

static void event(HCGame *g, int type, int a, int b)
{
    if (g->nev < HC_MAX_EVENTS) { g->ev[g->nev].type = type; g->ev[g->nev].a = a; g->ev[g->nev].b = b; g->nev++; }
}

int hc_pop_event(HCGame *g, HCEvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (g->nev - 1) * sizeof(HCEvent));
    g->nev--;
    return 1;
}

/* mulberry32 */
uint32_t hc_rand(HCGame *g)
{
    uint32_t t = (g->rng += 0x6D2B79F5UL);
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}

static fix fabs_(fix a) { return a < 0 ? -a : a; }

int hc_box_w(const HCGame *g, int b) { return g->box[b].len * HC_SLOT_W - 2; }

fix hc_hook_x(const HCGame *g) { return g->tx + FMUL(g->L, fsin(g->th)); }
fix hc_hook_y(const HCGame *g) { return FROMI(HC_GANTRY_Y) - FMUL(g->L, fcos(g->th)); }

int hc_weight(const HCGame *g)
{
    int k, w = 0;
    for (k = 0; k < g->nplaced; k++) w += g->box[g->placed[k].box].weight;
    return w;
}

/* weight x (distance of its middle from the deck's middle, in slots) */
fix hc_moment(const HCGame *g, int extra_box, int extra_slot)
{
    int k;
    fix m = 0;
    for (k = 0; k < g->nplaced; k++) {
        const HCPlaced *p = &g->placed[k];
        m += FROMI(g->box[p->box].weight) * (2 * p->slot + g->box[p->box].len - HC_SLOTS) / 2;
    }
    if (extra_box >= 0)
        m += FROMI(g->box[extra_box].weight) * (2 * extra_slot + g->box[extra_box].len - HC_SLOTS) / 2;
    return m;
}

/* the list: proportional to the moment (a 3-ton box at the end of the
 * deck lists her 0.075 radians; the capsize point is 0.30 with the roll) */
fix hc_list_for(const HCGame *g, fix moment, int weight)
{
    (void)g;
    (void)weight;
    return FMUL(moment, FX(0.010));
}

int hc_slot_top(const HCGame *g, int slot) { return HC_DECK_Y + g->height[slot] * HC_BOX_H; }

/* a point on the ship (its own coordinates) to the world: rolled by heel
 * (positive: the right side down; small angles) and lifted by the swell */
static fix ship_y(const HCGame *g, fix lx, fix ly)
{
    return ly - FMUL(lx - FROMI(HC_PIVOT_X), fsin(g->heel)) + g->bob;
}

static int pick_there(const HCGame *g) { return g->carrying < 0 && g->falling < 0 && g->next < g->nbox; }

fix hc_support_y(const HCGame *g, fix x0, fix x1, int with_pick)
{
    fix top = FROMI(HC_WATER_Y), y;
    int s;
    if (x0 < FROMI(HC_QUAY_X1) && top < 0) top = 0;
    if (with_pick && pick_there(g)) {
        fix half = FROMI(hc_box_w(g, g->next)) / 2;
        if (x1 > FROMI(HC_PICK_X) - half && x0 < FROMI(HC_PICK_X) + half && top < FROMI(HC_BOX_H)) top = FROMI(HC_BOX_H);
    }
    if (x1 > FROMI(HC_HULL_X0) && x0 < FROMI(HC_HULL_X1)) {
        fix a = x0 < FROMI(HC_HULL_X0) ? FROMI(HC_HULL_X0) : x0, b = x1 > FROMI(HC_HULL_X1) ? FROMI(HC_HULL_X1) : x1;
        y = ship_y(g, a, FROMI(HC_DECK_Y));
        if (y > top) top = y;
        y = ship_y(g, b, FROMI(HC_DECK_Y));
        if (y > top) top = y;
    }
    for (s = 0; s < HC_SLOTS; s++) {
        fix s0 = FROMI(HC_SHIP_X0 + s * HC_SLOT_W), s1 = s0 + FROMI(HC_SLOT_W);
        if (x1 <= s0 || x0 >= s1 || !g->height[s]) continue;
        y = ship_y(g, s0 + FROMI(HC_SLOT_W / 2), FROMI(hc_slot_top(g, s)));
        if (y > top) top = y;
    }
    return top;
}

int hc_fits(const HCGame *g, int b, int slot)
{
    int k;
    if (slot < 0 || slot + g->box[b].len > HC_SLOTS) return 0;
    for (k = 1; k < g->box[b].len; k++) if (g->height[slot + k] != g->height[slot]) return 0;
    return g->height[slot] < HC_TIERS;
}

void hc_load_level(HCGame *g, int level)
{
    int k;
    g->level = level;
    g->nbox = 5 + level < 12 ? 5 + level : 12;
    for (k = 0; k < g->nbox; k++) {
        uint32_t r = hc_rand(g) % 100;
        HCBox *b = &g->box[k];
        b->len = (level >= 2 && r < 25 + level * 2) ? 2 : 1;
        r = hc_rand(g) % 100;
        b->weight = r < 45 - level * 3 ? 1 : r < 85 - level * 3 ? 2 : 3;
        if (b->len == 2 && b->weight < 4 && (hc_rand(g) & 1)) b->weight++;
    }
    memset(g->height, 0, sizeof(g->height));
    g->nplaced = 0;
    g->next = 0;
    g->carrying = g->falling = -1;
    g->tx = FROMI(HC_PICK_X);
    g->tv = 0;
    g->L = FROMI(140);
    g->th = g->om = g->ldot = 0;
    g->wind = level >= 2 ? (level - 1) * FX(0.006) * ((level & 1) ? -1 : 1) : 0;
    g->gust = 0;
    g->gust_timer = 100;
    g->swell = FX(0.02) + (level - 1) * FX(0.011);
    g->phase = 0;
    g->list = g->heel = g->bob = 0;
    g->loaded = g->lost = g->dents = 0;
    g->time_left = (60 + 25 * g->nbox) * 25;
}

void hc_new_game(HCGame *g, uint32_t seed, int level)
{
    memset(g, 0, sizeof(*g));
    g->seed = g->rng = seed;
    if (level < 1) level = 1;
    if (level > HC_LEVELS) level = HC_LEVELS;
    hc_load_level(g, level);
}

static void lose(HCGame *g, int b)
{
    g->lost++;
    g->score -= 100;
    event(g, HCE_LOST, b, g->lost);
    if (g->lost >= HC_MAX_LOST) { g->over = 1; event(g, HCE_OVER, (int)g->score, 0); }
}

/* the falling box reaches whatever is below it */
static void land(HCGame *g)
{
    int b = g->falling, len = g->box[b].len, s;
    fix x0 = g->fx, x1 = x0 + FROMI(hc_box_w(g, b)), off;
    g->falling = -1;
    if (x1 <= FROMI(HC_QUAY_X1)) {           /* back on the quay: it waits there again */
        g->next = b;
        if (g->fy0 - g->fy > FROMI(HC_DENT_FALL)) { g->dents++; g->score -= 40; event(g, HCE_DENT, b, 0); }
        event(g, HCE_LAND, -1, 0);
        return;
    }
    s = (int)((x0 - FROMI(HC_SHIP_X0) + FROMI(HC_SLOT_W) / 2) / FROMI(HC_SLOT_W));
    if (x0 < FROMI(HC_SHIP_X0) - FROMI(HC_SLOT_W) / 2) s = -1;
    off = x0 - FROMI(HC_SHIP_X0 + s * HC_SLOT_W);
    if (!hc_fits(g, b, s) || fabs_(off) > FROMI(HC_SNAP)) { lose(g, b); return; }
    g->placed[g->nplaced].box = b;
    g->placed[g->nplaced].slot = s;
    g->placed[g->nplaced].tier = g->height[s];
    g->nplaced++;
    for (; len > 0; len--) g->height[s + len - 1]++;
    g->loaded++;
    g->total_loaded++;
    if (g->fy0 - g->fy > FROMI(HC_DENT_FALL)) { g->dents++; g->score -= 40; event(g, HCE_DENT, b, 0); }
    else g->score += 50 * g->box[b].weight;
    event(g, HCE_LAND, s, g->placed[g->nplaced - 1].tier);
}

static void ship(HCGame *g)
{
    int k, w = hc_weight(g);
    fix tiers = 0, swell;
    g->phase += SWELL_STEP;
    if (g->phase >= TWO_PI) g->phase -= TWO_PI;
    g->list = hc_list_for(g, hc_moment(g, -1, 0), w);
    for (k = 0; k < g->nplaced; k++)
        tiers += FROMI(g->box[g->placed[k].box].weight) * (2 * g->placed[k].tier + 1) / 2;
    if (w) tiers /= w;                       /* the load's mean height, in tiers */
    swell = FMUL(g->swell, FONE + tiers / 2);
    g->heel = g->list + FMUL(swell, fsin(g->phase));
    g->bob = FMUL(FROMI(3 + g->level / 2), fsin(g->phase + FX(1.3)));
}

static void swing(HCGame *g, fix accel)
{
    int k;
    fix wind = g->wind + g->gust;
    if (g->carrying < 0) wind /= 4;         /* the bare hook hardly catches it */
    for (k = 0; k < 4; k++) {
        fix sn = fsin(g->th), cs = fcos(g->th);
        fix a = -FDIV(FMUL(G, sn) + FMUL(accel - wind, cs) + 2 * FMUL(g->ldot, g->om), g->L);
        g->om += a / 4;
        g->om -= g->om / 300;
        g->th += g->om / 4;
        if (g->th > TH_MAX) { g->th = TH_MAX; g->om = 0; }
        if (g->th < -TH_MAX) { g->th = -TH_MAX; g->om = 0; }
    }
}

/* the load (or the bare hook) can't go through what is under it */
static void collide(HCGame *g)
{
    fix hx = hc_hook_x(g), hy = hc_hook_y(g), half, bottom, sup, pen, speed;
    if (g->carrying >= 0) { half = FROMI(hc_box_w(g, g->carrying)) / 2; bottom = hy - FROMI(HC_BOX_H); }
    else { half = FROMI(4); bottom = hy - FROMI(4); }
    sup = hc_support_y(g, hx - half, hx + half, 1);
    if (bottom >= sup) return;
    pen = sup - bottom;
    speed = fabs_(FMUL(g->om, g->L)) + fabs_(g->ldot);
    g->L -= pen;
    if (g->L < FROMI(HC_L_MIN)) g->L = FROMI(HC_L_MIN);
    g->ldot = 0;
    if (sup <= FROMI(HC_WATER_Y)) { if (speed > FX(0.5)) event(g, HCE_SPLASH, 0, 0); g->om -= g->om / 4; return; }
    if (speed > FX(1.5)) {
        g->bumps++;
        event(g, HCE_BUMP, (int)(speed >> 16), 0);
        if (g->carrying >= 0 && speed > FX(3.5)) { g->dents++; g->score -= 20; event(g, HCE_DENT, g->carrying, 1); }
    }
    g->om = -g->om / 2;
}

static void fire(HCGame *g)
{
    fix hx = hc_hook_x(g), hy = hc_hook_y(g);
    if (g->carrying >= 0) {
        fix sv = FMUL(g->om, g->L);
        g->falling = g->carrying;
        g->carrying = -1;
        g->fx = hx - FROMI(hc_box_w(g, g->falling)) / 2;
        g->fy = g->fy0 = hy - FROMI(HC_BOX_H);
        g->fvx = g->tv + FMUL(sv, fcos(g->th));
        g->fvy = FMUL(sv, fsin(g->th)) - FMUL(g->ldot, fcos(g->th));
        event(g, HCE_RELEASE, g->falling, 0);
        return;
    }
    if (!pick_there(g)) return;
    if (fabs_(hx - FROMI(HC_PICK_X)) < FROMI(12) && hy - FROMI(HC_BOX_H) < FROMI(8) &&
        fabs_(FMUL(g->om, g->L)) < FX(1.5)) {
        g->carrying = g->next++;
        event(g, HCE_GRAB, g->carrying, 0);
    }
}

void hc_tick(HCGame *g, unsigned in)
{
    int w;
    fix acc, target, prev_v, winch;

    if (g->over || g->won) return;
    g->tick++;
    if (g->between > 0) {
        if (--g->between == 0) {
            if (g->level >= HC_LEVELS) { g->won = 1; event(g, HCE_WIN, (int)g->score, 0); return; }
            hc_load_level(g, g->level + 1);
            event(g, HCE_LEVEL, g->level, 0);
        }
        return;
    }
    ship(g);
    if (fabs_(g->heel) > HC_CAPSIZE) {
        g->capsized = 1;
        g->over = 1;
        event(g, HCE_CAPSIZE, (int)(g->heel >> 10), 0);
        event(g, HCE_OVER, (int)g->score, 0);
        return;
    }
    if (--g->time_left <= 0) { g->over = 1; event(g, HCE_TIMEOUT, 0, 0); event(g, HCE_OVER, (int)g->score, 0); return; }

    /* gusts from level 4 */
    if (g->level >= 4 && --g->gust_timer <= 0) {
        int r = (int)(hc_rand(g) % 200);
        g->gust = g->gust ? 0 : (r - 100) * FX(0.0002) * g->level;
        g->gust_timer = 40 + (int)(hc_rand(g) % 120);
        if (g->gust) event(g, HCE_GUST, (int)(g->gust >> 8), 0);
    }

    /* the trolley: heavier loads start and stop more slowly */
    w = g->carrying >= 0 ? g->box[g->carrying].weight : 0;
    acc = FX(0.08) - w * FX(0.008);         /* about 2.5 s to full speed: one swing */
    target = (in & HC_IN_RIGHT) ? VMAX : (in & HC_IN_LEFT) ? -VMAX : 0;
    prev_v = g->tv;
    if (g->tv < target) { g->tv += acc; if (g->tv > target) g->tv = target; }
    else if (g->tv > target) { g->tv -= acc; if (g->tv < target) g->tv = target; }
    g->tx += g->tv;
    if (g->tx < FROMI(HC_TX_MIN)) { g->tx = FROMI(HC_TX_MIN); g->tv = 0; }
    if (g->tx > FROMI(HC_TX_MAX)) { g->tx = FROMI(HC_TX_MAX); g->tv = 0; }

    /* the winch */
    winch = FX(4) - w * FX(0.5);
    g->ldot = (in & HC_IN_UP) ? -winch : (in & HC_IN_DOWN) ? winch : 0;
    g->L += g->ldot;
    if (g->L < FROMI(HC_L_MIN)) { g->L = FROMI(HC_L_MIN); g->ldot = 0; }
    if (g->L > FROMI(HC_L_MAX)) { g->L = FROMI(HC_L_MAX); g->ldot = 0; }

    swing(g, g->tv - prev_v);
    collide(g);

    if ((in & HC_IN_FIRE) && !(g->prev_in & HC_IN_FIRE)) fire(g);
    g->prev_in = in;

    /* a box on its way down */
    if (g->falling >= 0) {
        fix sup;
        g->fvy -= G_FALL;
        g->fx += g->fvx;
        g->fy += g->fvy;
        sup = hc_support_y(g, g->fx, g->fx + FROMI(hc_box_w(g, g->falling)), 0);
        if (g->fy <= sup) {
            g->fy = sup;
            if (sup <= FROMI(HC_WATER_Y)) { int b = g->falling; event(g, HCE_SPLASH, 1, 0); g->falling = -1; lose(g, b); }
            else land(g);
        }
    }
    if (!g->over && g->next >= g->nbox && g->carrying < 0 && g->falling < 0) {
        int bonus = (g->time_left / 25) * 2 + (fabs_(g->list) < FX(0.04) ? 200 : 0);
        g->score += bonus;
        g->between = HC_BETWEEN;
        event(g, HCE_CLEAR, g->level, bonus);
    }
}

/* ---------- the autopilot ---------- */

int hc_best_slot(const HCGame *g, int b)
{
    int s, best = -1, w = hc_weight(g) + g->box[b].weight;
    fix best_cost = 0;
    for (s = 0; s < HC_SLOTS; s++) {
        fix cost;
        if (!hc_fits(g, b, s)) continue;
        cost = fabs_(hc_list_for(g, hc_moment(g, b, s), w)) * 8 + FROMI(g->height[s]) / 10;
        if (best < 0 || cost < best_cost) { best = s; best_cost = cost; }
    }
    return best;
}

/* the trolley's speed to aim for, err from the goal: speed builds up and
 * dies away over about one swing (so the swing it starts is cancelled by
 * the time it stops), and a little of the load's swing is chased to damp
 * what is left */
static fix approach(const HCGame *g, fix err, fix off)
{
    fix a = APROF, dist = fabs_(err), v;
    (void)g;
    v = fsqrt(2 * FMUL(a, dist));         /* slowing down in time to stop there */
    if (v > VMAX) v = VMAX;
    if (dist < FROMI(2)) v = 0;
    else if (v < FX(0.3)) v = FX(0.3);    /* creep the last few units */
    return (err < 0 ? -v : v) + FMUL(off, KOFF);
}

unsigned hc_bot(const HCGame *g)
{
    fix hx, hy, goal, want_y, vdes, sv, top = FROMI(HC_BOX_H), half, travel;
    unsigned in = 0;
    int s, near, low, b;

    if (g->over || g->won || g->between || g->falling >= 0) return 0;
    hx = hc_hook_x(g);
    hy = hc_hook_y(g);
    sv = FMUL(g->om, g->L);
    for (s = 0; s < HC_SLOTS; s++) {
        fix y = ship_y(g, FROMI(HC_SHIP_X0 + s * HC_SLOT_W + HC_SLOT_W / 2), FROMI(hc_slot_top(g, s)));
        if (y > top) top = y;
    }
    if (g->carrying < 0) {
        if (g->next >= g->nbox) return 0;
        b = g->next;
        goal = FROMI(HC_PICK_X);
        half = FROMI(4);
        travel = top + FROMI(3 * HC_BOX_H);
    } else {
        b = g->carrying;
        s = hc_best_slot(g, b);
        if (s < 0) s = 0;
        half = FROMI(hc_box_w(g, b)) / 2;
        goal = FROMI(HC_SHIP_X0 + s * HC_SLOT_W) + half + FROMI(1);
        travel = top + FROMI(2 * HC_BOX_H + 20);
    }
    /* lowered once it is over the goal and still; it carries on down
     * unless it drifts well off */
    low = hy < travel - FROMI(15);
    near = low ? (fabs_(hx - goal) < FROMI(9) && fabs_(sv) < FX(1.2))
               : (fabs_(hx - goal) < FROMI(6) && fabs_(g->tx - goal) < FROMI(8) && fabs_(sv) < FX(0.8));
    if (near) {
        fix sup = hc_support_y(g, hx - half, hx + half, g->carrying < 0);
        want_y = sup + FROMI(g->carrying < 0 ? 4 : HC_BOX_H + 4);
        if (hy - want_y < FROMI(5) && fabs_(hx - goal) < FROMI(7) && !(g->prev_in & HC_IN_FIRE)) return HC_IN_FIRE;
    } else want_y = travel;
    /* the winch */
    if (hy < want_y - FROMI(3)) in |= HC_IN_UP;
    else if (hy > want_y + FROMI(3)) in |= HC_IN_DOWN;
    /* the trolley, held still while the load is still low and off target */
    vdes = (low && !near && fabs_(hx - goal) > FROMI(9)) ? FMUL(hx - g->tx, KOFF) : approach(g, goal - g->tx, hx - g->tx);
    if (g->tv < vdes - FX(0.04)) in |= HC_IN_RIGHT;
    else if (g->tv > vdes + FX(0.04)) in |= HC_IN_LEFT;
    return in;
}

uint32_t hc_state_hash(const HCGame *g)
{
    uint32_t h = 2166136261UL;
    int i;
    int32_t v[23];
    v[0] = g->score; v[1] = g->level; v[2] = g->tx; v[3] = g->tv; v[4] = g->L; v[5] = g->th; v[6] = g->om; v[7] = g->carrying; v[8] = g->next; v[9] = g->falling; v[10] = g->fx; v[11] = g->fy; v[12] = g->heel; v[13] = g->list; v[14] = g->loaded; v[15] = g->lost; v[16] = g->dents; v[17] = g->bumps; v[18] = g->tick; v[19] = g->over; v[20] = g->won; v[21] = g->time_left; v[22] = g->total_loaded;
    for (i = 0; i < HC_SLOTS; i++) { h ^= (uint32_t)g->height[i]; h *= 16777619UL; }
    for (i = 0; i < (int)(sizeof(v) / sizeof(v[0])); i++) { h ^= (uint32_t)v[i]; h *= 16777619UL; }
    return h;
}
