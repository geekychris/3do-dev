// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * world.c - ScAIry Fun's simulation, one step per 1/25 s: the car, the
 * autopilot (ai_think / ai_steer), traffic, the hazards, damage and the
 * score, and the route -> arrival -> software update -> next route loop.
 */
#include <string.h>
#include "sc.h"

#define SEGF       ((int32_t)SEG_LEN * (1 << FBITS))     /* fine units per segment */
#define CAR_LEN    36
#define CAR_WID    15

const char *const msg_text[MSG_COUNT] = {
    "", "AUTOPILOT ENGAGED", "AUTOPILOT DISENGAGED", "I HAD THAT.",
    "GOOD CATCH.", "RECALCULATING...", "THAT IS A ROAD. PROBABLY.",
    "OBSTACLE DETECTED!", "KEEP YOUR HANDS ON THE WHEEL", "OOPS.",
    "UPDATE INSTALLED. I AM BETTER NOW.", "AUTOPILOT UNAVAILABLE",
    "WELCOME TO SCAIRY FUN", "WATER DETECTED. PROCEEDING ANYWAY.",
    "YOU HAVE ARRIVED.", "CAMERA BLINDED", "I AM VERY PATIENT.",
    "TICKET: FAILED TO YIELD", "TOW TRUCK ON ITS WAY", "POLICY CANCELLED"
};

const char *const ev_name[EV_COUNT] = {
    "none", "ap_on", "ap_off", "unavailable", "save", "needless", "scrape",
    "cone", "debris", "rearend", "crash", "splash", "roll", "ticket",
    "respawn", "phantom", "blinded", "aquaplane", "arrived", "game_over",
    "mult_up", "siren", "route_start", "hit_animal", "say", "win"
};

static const char *const hz_names[HZ_COUNT] = {
    "none", "flood", "ghost", "phantom", "cones", "glare", "siren", "debris",
    "deer", "cyclist", "tractor", "merge", "goats", "peds"
};

static const char *const obj_names[OBJ_COUNT] = {
    "none", "car", "truck", "tractor", "cyclist", "deer", "cone", "debris",
    "ambulance", "goat", "pedestrian"
};

/* half length, half width, units */
static const uint8_t obj_size[OBJ_COUNT][2] = {
    { 0, 0 }, { 36, 15 }, { 90, 20 }, { 30, 16 }, { 14, 5 }, { 16, 6 },
    { 4, 4 }, { 10, 10 }, { 50, 17 }, { 10, 4 }, { 5, 5 }
};

const char *hz_name(int type) { return type >= 0 && type < HZ_COUNT ? hz_names[type] : "?"; }
const char *obj_name(int type) { return type >= 0 && type < OBJ_COUNT ? obj_names[type] : "?"; }
int obj_len(int type) { return obj_size[type][0]; }
int obj_wid(int type) { return obj_size[type][1]; }

static int32_t iabs(int32_t a) { return a < 0 ? -a : a; }

static void push(World *w, int type, int a, int b)
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
    if (!w->nev) return 0;
    *e = w->ev[0];
    memmove(&w->ev[0], &w->ev[1], (w->nev - 1) * sizeof(Event));
    w->nev--;
    return 1;
}

void world_say(World *w, int msg)
{
    w->msg = msg;
    w->msg_timer = 75;
    push(w, EV_SAY, msg, 0);
}

void world_defaults(Tune *t)
{
    t->god_mode = 0;
    t->ai_perfect = 0;
    t->traffic_pct = 100;
    t->speed_pct = 100;
    t->mult_secs = 8;
    t->aqua_kmh = 50;
}

/* ---------- damage ---------- */
static void hurt(World *w, int n, int ev)
{
    w->incident = 1;
    if (w->tune.god_mode || w->state != GS_PLAY) n = 0;
    w->cover -= n;
    if (w->cover < 0) w->cover = 0;
    push(w, ev, n, w->cover);
    if (w->cover == 0 && w->state == GS_PLAY) {
        w->state = GS_GAMEOVER;
        w->state_timer = 50;
        w->ai.on = 0;
        push(w, EV_GAME_OVER, w->score, w->route);
        world_say(w, MSG_CANCELLED);
    }
}

/* ---------- objects ---------- */
int world_spawn(World *w, int type, int32_t z, int32_t d, int32_t v, int flags)
{
    int i;
    for (i = 0; i < MAX_OBJS; i++) {
        Obj *o = &w->obj[i];
        if (o->type != OBJ_NONE) continue;
        memset(o, 0, sizeof(*o));
        o->type = (uint8_t)type;
        o->flags = (uint8_t)flags;
        o->hz = -1;
        o->z = z;
        o->d = d;
        o->v = o->vt = v;
        o->lane = (d < 0 ? -1 - (int)((-d >> FBITS) / LANE_W) : lane_of(w, d));
        o->colour = (uint8_t)(sc_rand(&w->rng) & 3);
        return i;
    }
    return -1;
}

/* is lane free of moving things from dz0 to dz1 segments around z? */
static int lane_clear(const World *w, int lane, int32_t z, int dz0, int dz1, int skip)
{
    int i;
    for (i = 0; i < MAX_OBJS; i++) {
        const Obj *o = &w->obj[i];
        int32_t dz;
        if (i == skip || o->type == OBJ_NONE || o->lane != lane) continue;
        dz = o->z - z;
        if (dz > dz0 * SEGF && dz < dz1 * SEGF) return 0;
    }
    return 1;
}

/* where to pass something small in the lane: on the shoulder if there's
 * room, otherwise to its left */
static int32_t swerve_to(const World *w, const Obj *o)
{
    int32_t right = (o->d >> FBITS) + obj_wid(o->type) + CAR_WID + 12;
    if (right + CAR_WID <= road_right(w) + SHOULDER) return right * (1 << FBITS);
    return o->d - ((int32_t)(obj_wid(o->type) + CAR_WID + 12) * (1 << FBITS));
}

/* may object `skip` move into lane at z? (the car counts too) */
static int lane_free_for(const World *w, int lane, int32_t z, int skip)
{
    const Car *c = &w->car;
    int32_t dz = c->z - z;
    if (c->d >= 0 && lane_of(w, c->d) == lane && dz > -3 * SEGF && dz < 3 * SEGF) return 0;
    if (lane < 0 && c->d < 0 && dz > -3 * SEGF && dz < 3 * SEGF) return 0;
    return lane_clear(w, lane, z, -2, 3, skip);
}

int cone_at(const World *w, int seg, int32_t *d)
{
    const Seg *g;
    int start, i, lane;
    if (seg < 0 || seg >= w->nseg) return 0;
    g = &w->seg[seg];
    if (!(g->flags & SF_CONES)) return 0;
    for (lane = 0; lane < 8 && !(g->closed & (1 << lane)); lane++)
        ;
    for (start = seg; start > 0 && (w->seg[start - 1].flags & SF_CONES); start--)
        ;
    i = seg - start;
    if (i < 3) {                        /* the taper, across the lane */
        *d = (int32_t)lane * LANE_W + ((i + 1) * LANE_W) / 4;
        return 1;
    }
    if (i % 3 == 0) {
        *d = lane_centre(w, lane);
        return 1;
    }
    return 0;
}

static int knocked(const World *w, int seg) { return w->knocked[seg >> 3] & (1 << (seg & 7)); }

static void spawn_hazard(World *w, int i)
{
    const Hazard *h = &w->haz[i];
    const RouteInfo *r = &route_info[w->route];
    Car *c = &w->car;
    int32_t z = (int32_t)h->seg * SEGF, rr = road_right(w);
    int k, o = -1;

    switch (h->type) {
    case HZ_CYCLIST:
        o = world_spawn(w, OBJ_CYCLIST, z, (rr - 10) * (1 << FBITS), FROM_KMH(20), 0);
        break;
    case HZ_DEER:
        o = world_spawn(w, OBJ_DEER, z, (rr + 50) * (1 << FBITS), 0, 0);
        break;
    case HZ_GOATS:
    case HZ_PEDS:
        for (k = 0; k < 3; k++) {
            o = world_spawn(w, h->type == HZ_GOATS ? OBJ_GOAT : OBJ_PED,
                            z + k * (SEGF / 3), (rr + 40 + k * 22) * (1 << FBITS), 0, 0);
            if (o >= 0) w->obj[o].hz = i;
        }
        o = -1;
        break;
    case HZ_TRACTOR:
        o = world_spawn(w, OBJ_TRACTOR, z, lane_centre(w, r->nl - 1) * (1 << FBITS), FROM_KMH(25), 0);
        break;
    case HZ_MERGE:
        o = world_spawn(w, OBJ_TRUCK, z, (rr + 34) * (1 << FBITS), FROM_KMH(60), 0);
        if (o >= 0) w->obj[o].lane = r->nl;     /* on the slip road, for now */
        break;
    case HZ_PHANTOM:
        o = world_spawn(w, OBJ_TRUCK, c->z - 4 * SEGF,
                        lane_centre(w, lane_of(w, c->d)) * (1 << FBITS), c->v + FROM_KMH(15), OF_TAILGATE);
        break;
    case HZ_SIREN:
        o = world_spawn(w, OBJ_AMBULANCE, c->z - 12 * SEGF,
                        lane_centre(w, lane_of(w, c->d)) * (1 << FBITS), c->v + FROM_KMH(40), 0);
        w->siren = 1;
        push(w, EV_SIREN, h->seg, 0);
        break;
    case HZ_DEBRIS: {
        int lane = h->param >= 0 ? h->param : (c->d >= 0 ? lane_of(w, c->d) : r->nl - 1);
        o = world_spawn(w, OBJ_DEBRIS, z, lane_centre(w, lane) * (1 << FBITS), 0, 0);
        break;
    }
    }
    if (o >= 0) w->obj[o].hz = i;
}

static void resolve_hazard(World *w, int i)
{
    w->haz_state[i] = 2;
    if (w->pending_save != i) return;
    if (!w->incident && w->state == GS_PLAY) {
        w->saves++;
        w->total_saves++;
        w->score += 500;
        push(w, EV_SAVE, w->haz[i].type, w->saves);
        world_say(w, MSG_GOOD_CATCH);
    }
    w->pending_save = -1;
}

static void hazards_update(World *w)
{
    int cs = seg_of(w->car.z), i;
    for (i = 0; i < w->nhaz; i++) {
        const Hazard *h = &w->haz[i];
        if (w->haz_state[i] == 0) {
            int trig = h->type == HZ_SIREN ? cs >= h->seg : h->seg - cs <= 32;
            if (trig) {
                w->haz_state[i] = 1;
                spawn_hazard(w, i);
            }
        } else if (w->haz_state[i] == 1 && h->type != HZ_SIREN && cs > h->seg + h->len + 2) {
            resolve_hazard(w, i);
        }
    }
}

int world_hazard_ahead(const World *w, int segs)
{
    int cs = seg_of(w->car.z), i;
    for (i = 0; i < w->nhaz; i++) {
        const Hazard *h = &w->haz[i];
        if (!HZ_FAILS(h->type) || w->haz_state[i] == 2) continue;
        if (h->type == HZ_SIREN) {
            if (w->haz_state[i] == 1) return i;
            continue;
        }
        if (h->seg - cs <= segs && cs <= h->seg + h->len + 1) return i;
    }
    return -1;
}

/* ---------- traffic ---------- */
static void traffic_spawn(World *w)
{
    const RouteInfo *r = &route_info[w->route];
    Car *c = &w->car;
    int want = (r->traffic * 70 / 100) * w->tune.traffic_pct / 100;
    int n = 0, i, cs = seg_of(c->z), seg, lane, type;
    int32_t d, v, z;

    for (i = 0; i < MAX_OBJS; i++)
        if (w->obj[i].hz < 0 && (w->obj[i].type == OBJ_CAR || w->obj[i].type == OBJ_TRUCK)) n++;
    if (n >= want || (sc_rand(&w->rng) & 7)) return;

    type = (sc_rand(&w->rng) % 5) ? OBJ_CAR : OBJ_TRUCK;
    if (r->no && (sc_rand(&w->rng) % 3) == 0) {
        seg = cs + 30 + (int)(sc_rand(&w->rng) % 6);
        lane = -1 - (int)(sc_rand(&w->rng) % r->no);
        v = -FROM_KMH(r->limit_kmh * (85 + (int)(sc_rand(&w->rng) % 20)) / 100);
    } else if (r->nl > 1 && (sc_rand(&w->rng) % 4) == 0) {
        /* a faster car coming up from behind in a lane we're not in */
        seg = cs - 9;
        lane = (int)(sc_rand(&w->rng) % (r->nl - 1));
        if (c->d >= 0 && lane == lane_of(w, c->d)) return;
        v = FROM_KMH(r->limit_kmh * (110 + (int)(sc_rand(&w->rng) % 15)) / 100);
        type = OBJ_CAR;
    } else {
        seg = cs + 30 + (int)(sc_rand(&w->rng) % 6);
        lane = (int)(sc_rand(&w->rng) % r->nl);
        if (r->nl == 1)                 /* no overtaking here: about the limit */
            v = FROM_KMH(r->limit_kmh * (92 + (int)(sc_rand(&w->rng) % 14)) / 100);
        else
            v = FROM_KMH(r->limit_kmh * (type == OBJ_TRUCK ? 55 : 65 + (int)(sc_rand(&w->rng) % 25)) / 100);
    }
    if (seg < 1 || seg >= w->finish) return;
    if (lane >= 0 && (w->seg[seg].closed & (1 << lane))) return;
    if (lane < 0)                       /* not inside a detour through the oncoming lane */
        for (i = seg - 3; i <= seg + 3; i++)
            if (i >= 0 && i < w->nseg && (w->seg[i].closed & 1) && r->nl == 1) return;
    z = (int32_t)seg * SEGF;
    if (!lane_clear(w, lane, z, -3, 3, -1)) return;
    d = lane_centre(w, lane) * (1 << FBITS);
    i = world_spawn(w, type, z, d, v, v < 0 ? OF_ONCOMING : 0);
    if (i >= 0) w->obj[i].lane = lane;
}

/* the speed history, for the truck that reacts late */
static int32_t car_v_ago(const World *w, int frames)
{
    return w->car.hist[(w->car.hist_pos - frames) & 31];
}

static void vehicle_follow(World *w, Obj *o, int i)
{
    Car *c = &w->car;
    int32_t best = 5 * SEGF, lead_v = 0, dz, gap, des, t = o->vt;
    int k, len = obj_len(o->type), found = 0;

    for (k = 0; k < MAX_OBJS; k++) {
        const Obj *p = &w->obj[k];
        if (k == i || p->type == OBJ_NONE || p->v < 0 || p->type == OBJ_CYCLIST ||
            p->type == OBJ_DEBRIS) continue;
        if (iabs(p->d - o->d) > (40 * (1 << FBITS))) continue;
        dz = p->z - o->z;
        if (dz > 0 && dz < best) { best = dz; lead_v = p->v; found = 1; }
    }
    dz = c->z - o->z;
    if (c->state == CS_DRIVING && dz > 0 && dz < best &&
        (iabs(c->d - o->d) < (40 * (1 << FBITS)) || (c->d >= 0 && lane_of(w, c->d) == o->lane))) {
        best = dz;
        lead_v = c->v;
        found = 1;
    }
    if (found) {
        gap = (best >> FBITS) - len - CAR_LEN;
        des = (o->v * 25) / 16 + 30;
        if (gap < des) {
            int32_t lim = lead_v + (gap - des) * 2;
            if (lim < 0) lim = 0;
            if (lim < t) t = lim;
        }
    }
    if (o->v < t) { o->v += 3; if (o->v > t) o->v = t; }
    else if (o->v > t) { o->v -= 10; if (o->v < t) o->v = t; }
}

static void move_across(Obj *o, int32_t target, int32_t rate)
{
    if (o->d < target) { o->d += rate; if (o->d > target) o->d = target; }
    else if (o->d > target) { o->d -= rate; if (o->d < target) o->d = target; }
}

static void objects_update(World *w)
{
    const RouteInfo *r = &route_info[w->route];
    Car *c = &w->car;
    int i;

    for (i = 0; i < MAX_OBJS; i++) {
        Obj *o = &w->obj[i];
        int32_t dz;
        if (o->type == OBJ_NONE) continue;
        if (o->cool > 0) o->cool--;
        dz = o->z - c->z;

        switch (o->type) {
        case OBJ_CAR:
        case OBJ_TRUCK:
            if (o->flags & OF_ONCOMING) {
                if (o->flags & OF_KNOCKED) o->v = 0;           /* a wreck */
                else {
                    /* brake for the car in our lane, and wait after a detour
                     * through our lane until the car is through it */
                    int32_t gap = (dz >> FBITS) - obj_len(o->type) - CAR_LEN;
                    int os = seg_of(o->z), cs = seg_of(c->z), k, wait = 0;
                    int32_t to;                 /* C89: before the statements */
                    int blocking;
                    if (r->nl == 1 && dz > 0)
                        for (k = os - 2; k >= cs && k > os - 40; k--)
                            if (k < w->nseg && (w->seg[k].closed & 1)) {
                                /* k is the zone's far end: wait there while the
                                 * car is about to come through */
                                int32_t stop = (int32_t)(k + 4) * SEGF;
                                int start = k;
                                while (start > 0 && (w->seg[start - 1].closed & 1)) start--;
                                wait = cs >= start - 12 && o->z > stop && o->z - stop < 3 * SEGF;
                                break;
                            }
                    to = lane_centre(w, o->lane) * (1 << FBITS);
                    blocking = iabs(c->d - o->d) < (34 * (1 << FBITS)) && gap > -10 && gap < 4 * SEG_LEN;
                    if (blocking && o->v == 0 && ++o->timer > 50) o->flags |= OF_PASSING;
                    if (o->flags & OF_PASSING) {
                        /* given up waiting: over to the kerb until the car is past */
                        to = (road_left(w) - 26) * (1 << FBITS);
                        if (dz < -2 * SEGF) { o->flags &= ~OF_PASSING; o->timer = 0; }
                        blocking = dz > 0 && gap < 2 * SEG_LEN && iabs(o->d - to) > (4 * (1 << FBITS));
                    }
                    if (wait || blocking) {
                        o->v += 10;
                        if (o->v > 0) o->v = 0;
                    } else if (!o->cool && o->v > o->vt) {
                        o->v -= 4;
                        if (o->v < o->vt) o->v = o->vt;
                    }
                    move_across(o, to, 16);
                }
                break;
            }
            if (o->flags & OF_TAILGATE) {
                /* sits close behind and reacts to the car half a second late */
                int32_t gap = (-dz >> FBITS) - obj_len(o->type) - CAR_LEN;
                int32_t late = car_v_ago(w, 12), t = late + (gap - 48) * 2;
                if (t > late + FROM_KMH(30)) t = late + FROM_KMH(30);
                if (t < 0) t = 0;
                if (o->v < t) { o->v += 6; if (o->v > t) o->v = t; }
                else if (o->v > t) { o->v -= 12; if (o->v < t) o->v = t; }
                o->d = c->d >= 0 ? lane_centre(w, lane_of(w, c->d)) * (1 << FBITS) : o->d;
                if (o->hz >= 0 && w->haz_state[o->hz] == 2) {
                    o->flags &= ~OF_TAILGATE;   /* off it goes */
                    o->vt = FROM_KMH(r->limit_kmh);
                    o->lane = (r->nl > 1 ? 0 : r->nl - 1);
                }
                break;
            }
            if (o->hz >= 0 && w->haz[o->hz].type == HZ_MERGE && o->lane == r->nl) {
                if (dz > 0 && dz < 8 * SEGF) o->lane = w->haz[o->hz].param;
                break;
            }
            /* round a closed lane: the next lane, or the oncoming one */
            if (o->lane >= 0) {
                int s = seg_of(o->z) + 6;
                if (s < w->nseg && (w->seg[s].closed & (1 << o->lane))) {
                    int to = o->lane > 0 ? o->lane - 1 : -1;
                    if (lane_free_for(w, to, o->z, i)) o->lane = to;
                }
                else if (s < w->nseg && !(w->seg[s].flags & SF_CONES) && o->lane < 0 && !(o->flags & OF_ONCOMING))
                    o->lane = 0;
            }
            vehicle_follow(w, o, i);
            {
                /* round a cyclist, within the lane */
                int32_t to = lane_centre(w, o->lane) * (1 << FBITS);
                int k;
                for (k = 0; k < MAX_OBJS; k++) {
                    const Obj *p = &w->obj[k];
                    int32_t ahead = p->z - o->z;
                    if (ahead <= -SEGF || ahead >= 6 * SEGF || iabs(p->d - to) >= (34 * (1 << FBITS)))
                        continue;
                    if (p->type == OBJ_CYCLIST) to = p->d - (34 * (1 << FBITS));
                    else if (p->type == OBJ_DEBRIS) to = swerve_to(w, p);
                }
                move_across(o, to, 20);
            }
            break;

        case OBJ_AMBULANCE:
            if (!(o->flags & OF_PASSING)) {
                /* catch up, then sit behind the car until it moves over */
                int32_t gap = (-dz >> FBITS) - obj_len(o->type) - CAR_LEN;
                if (gap > 120) o->v = c->v + FROM_KMH(40);
                else {
                    o->v = c->v;
                    o->timer++;
                }
                if (iabs(c->d - o->d) > (45 * (1 << FBITS)) || c->state != CS_DRIVING) {
                    o->flags |= OF_PASSING;
                } else if (o->timer > 75) {
                    /* a ticket, unless the traffic left nowhere to move over to */
                    int lane = c->d >= 0 ? lane_of(w, c->d) : 0, blocked = r->nl > 1;
                    if (blocked && lane > 0 && lane_clear(w, lane - 1, c->z, -2, 3, i)) blocked = 0;
                    if (blocked && lane < r->nl - 1 && lane_clear(w, lane + 1, c->z, -2, 3, i)) blocked = 0;
                    if (blocked) o->timer = 15;     /* and time to move once it can */
                    else {
                        o->flags |= OF_PASSING;
                        hurt(w, 10, EV_TICKET);
                        world_say(w, MSG_TICKET);
                    }
                }
            } else {
                /* out from behind the car first, then away */
                o->v = iabs(c->d - o->d) < (34 * (1 << FBITS)) && dz < 0 ? c->v : c->v + FROM_KMH(50);
                if (iabs(c->d - o->d) <= (45 * (1 << FBITS)) && dz < 2 * SEGF) {
                    /* still in the way: round it, by another lane or the oncoming one */
                    if (r->nl > 1) {
                        int lane = c->d >= 0 ? lane_of(w, c->d) : 0;
                        o->lane = (lane > 0 ? lane - 1 : 1);
                    } else {
                        o->lane = -1;
                    }
                }
                move_across(o, lane_centre(w, o->lane) * (1 << FBITS), 24);
                if (dz > 3 * SEGF && o->hz >= 0 && w->haz_state[o->hz] == 1) {
                    w->siren = 0;
                    resolve_hazard(w, o->hz);
                }
            }
            break;

        case OBJ_TRACTOR:
            if (o->hz >= 0 && seg_of(o->z) > w->haz[o->hz].seg + w->haz[o->hz].len) {
                o->d += 20;             /* turns off into a field */
                o->v = FROM_KMH(10);
                if ((o->d >> FBITS) > road_right(w) + 220) o->type = OBJ_NONE;
            } else {
                vehicle_follow(w, o, i);
            }
            break;

        case OBJ_CYCLIST:
            break;

        case OBJ_DEER:
        case OBJ_GOAT:
        case OBJ_PED:
            /* cross when the car is close; a hit one is thrown aside */
            if (o->vd == 0 && dz > 0 && dz < (o->type == OBJ_PED ? 6 : 5) * SEGF && o->timer == 0) {
                o->timer = 1;
                o->vd = -(o->type == OBJ_PED ? 18 : 30);
            }
            o->d += o->vd;
            break;
        }
        o->z += o->v;
        if (o->type == OBJ_NONE) continue;
        dz = o->z - c->z;
        if (dz < -20 * SEGF || dz > 48 * SEGF || seg_of(o->z) >= w->nseg - 1 || o->z < 0) {
            if (o->type == OBJ_AMBULANCE) {
                w->siren = 0;
                if (o->hz >= 0 && w->haz_state[o->hz] == 1) resolve_hazard(w, o->hz);
            }
            o->type = OBJ_NONE;
        }
    }
}

static void collide(World *w)
{
    Car *c = &w->car;
    int i, k, cs;

    if (c->state != CS_DRIVING && c->state != CS_AQUAPLANE) return;
    for (i = 0; i < MAX_OBJS; i++) {
        Obj *o = &w->obj[i];
        int32_t dz, dd;
        int ln, wd;
        if (o->type == OBJ_NONE || o->cool || (o->flags & OF_KNOCKED)) continue;
        dz = (o->z - c->z) >> FBITS;
        dd = (o->d - c->d) >> FBITS;
        ln = obj_len(o->type);
        wd = obj_wid(o->type);
        if (iabs(dz) >= CAR_LEN + ln || iabs(dd) >= CAR_WID + wd) continue;
        switch (o->type) {
        case OBJ_DEBRIS:
            o->type = OBJ_NONE;
            c->v = c->v * 3 / 4;
            hurt(w, 10, EV_DEBRIS);
            world_say(w, MSG_OOPS);
            break;
        case OBJ_DEER: case OBJ_GOAT: case OBJ_PED: case OBJ_CYCLIST:
            o->cool = 1000;
            o->vd = dd > 0 ? 60 : -60;
            o->v = 0;
            c->v /= 2;
            hurt(w, 20, EV_HIT_ANIMAL);
            world_say(w, MSG_OOPS);
            break;
        default:
            o->cool = 50;
            if (o->v < 0) {
                /* head on: the other car ends up a wreck at the roadside */
                c->v = 0;
                o->v = 0;
                o->flags |= OF_KNOCKED;
                o->d -= 48 * (1 << FBITS);
                hurt(w, 35, EV_CRASH);
            } else if (dz > 0) {
                c->v = o->v * 3 / 4;
                c->z = o->z - ((int32_t)(CAR_LEN + ln + 2) * (1 << FBITS));
                hurt(w, 25, EV_CRASH);
            } else {
                o->v = c->v * 3 / 4;
                o->z = c->z - ((int32_t)(CAR_LEN + ln + 2) * (1 << FBITS));
                o->flags &= ~OF_TAILGATE;
                hurt(w, 15, EV_REAREND);
            }
            world_say(w, MSG_OOPS);
            break;
        }
    }

    /* cones in a closed lane */
    cs = seg_of(c->z);
    for (k = cs; k <= cs + 1; k++) {
        int32_t cd;
        if (k >= w->nseg || knocked(w, k) || !cone_at(w, k, &cd)) continue;
        if (iabs(((int32_t)k * SEGF - c->z) >> FBITS) < CAR_LEN + 4 &&
            iabs((c->d >> FBITS) - cd) < CAR_WID + 4) {
            w->knocked[k >> 3] |= (uint8_t)(1 << (k & 7));
            c->v = c->v * 7 / 8;
            hurt(w, 2, EV_CONE);
        }
    }
}

/* ---------- the autopilot ---------- */
int world_ap_allowed(const World *w)
{
    const Car *c = &w->car;
    int lane;
    if (c->state != CS_DRIVING || c->d < 0) return 0;      /* it can pull away from a standstill */
    if (w->state != GS_PLAY && w->state != GS_TITLE) return 0;
    lane = lane_of(w, c->d);
    return iabs((c->d >> FBITS) - lane_centre(w, lane)) <= LANE_W / 4;
}

static void takeover(World *w)
{
    int h;
    w->ai.on = 0;
    w->takeovers++;
    w->mult = 1;
    w->ap_frames = 0;
    push(w, EV_AP_OFF, w->takeovers, 0);
    if (w->state != GS_PLAY) return;
    h = world_hazard_ahead(w, 24);
    if (h >= 0) {
        w->pending_save = h;
        w->incident = 0;
        world_say(w, MSG_AP_OFF);
    } else {
        w->needless++;
        w->total_needless++;
        push(w, EV_NEEDLESS, w->needless, 0);
        world_say(w, MSG_HAD_THAT);
    }
}

void world_set_ap(World *w, int on)
{
    if (on && !w->ai.on) {
        if (!world_ap_allowed(w)) {
            push(w, EV_UNAVAILABLE, 0, 0);
            world_say(w, MSG_UNAVAILABLE);
            return;
        }
        w->ai.on = 1;
        w->ai.lane = lane_of(w, w->car.d);
        w->ap_frames = 0;
        push(w, EV_AP_ON, 0, 0);
        world_say(w, MSG_AP_ON);
    } else if (!on && w->ai.on) {
        takeover(w);
    }
}

static int perfect(const World *w)
{
    return w->tune.ai_perfect || w->state == GS_TITLE;
}

/* the lane change rules on a multi-lane road */
static void ai_lanes(World *w)
{
    Ai *a = &w->ai;
    Car *c = &w->car;
    const RouteInfo *r = &route_info[w->route];
    int i, cs = seg_of(c->z);

    if (a->lane_hold > 0) { a->lane_hold--; return; }
    if (perfect(w)) {
        /* round a closed lane */
        for (i = 2; i < 14; i++)
            if (a->lane >= 0 && cs + i < w->nseg && (w->seg[cs + i].closed & (1 << a->lane))) {
                int to = a->lane > 0 ? a->lane - 1 : (r->nl > 1 ? 1 : -1), end = cs + i;
                while (end < w->nseg - 1 && (w->seg[end].flags & SF_CONES)) end++;
                if (to < 0 && !lane_clear(w, -1, c->z, -2, end - cs + 1, -1)) return;
                a->lane = to;
                a->lane_hold = 50;
                return;
            }
        if (a->lane < 0 && !(w->seg[cs < w->nseg - 14 ? cs + 13 : cs].flags & SF_CONES) &&
            !(w->seg[cs].flags & SF_CONES)) {
            a->lane = 0;
            a->lane_hold = 50;
            return;
        }
    }
    if (r->nl < 2 || a->lane < 0) return;
    if (perfect(w)) {
        /* move over for an ambulance */
        for (i = 0; i < MAX_OBJS; i++) {
            const Obj *o = &w->obj[i];
            int32_t dz = o->z - c->z;
            if (o->type == OBJ_AMBULANCE && !(o->flags & OF_PASSING) && o->lane == a->lane &&
                dz < 0 && dz > -10 * SEGF) {
                int to = a->lane > 0 ? a->lane - 1 : a->lane + 1;
                if (!lane_clear(w, to, c->z, -2, 3, i)) to = a->lane < r->nl - 1 ? a->lane + 1 : -9;
                if (to >= 0 && to < r->nl && lane_clear(w, to, c->z, -2, 3, i)) {
                    a->lane = to;
                    a->lane_hold = 125;
                }
                return;
            }
        }
    }
    /* overtake a slow car ahead, then keep right */
    for (i = 0; i < MAX_OBJS; i++) {
        const Obj *o = &w->obj[i];
        int32_t dz = o->z - c->z;
        if (o->type == OBJ_NONE || o->lane != a->lane || dz <= 0 || dz > 6 * SEGF) continue;
        if (o->type != OBJ_CAR && o->type != OBJ_TRUCK && o->type != OBJ_TRACTOR) continue;
        if (o->v < a->target_v - FROM_KMH(10) && a->lane > 0 &&
            lane_clear(w, a->lane - 1, c->z, -4, 5, -1)) {
            a->lane--;
            a->lane_hold = 100;
            return;
        }
    }
    if (a->lane < r->nl - 1 && lane_clear(w, a->lane + 1, c->z, -3, 7, -1)) {
        for (i = 0; i < 16 && cs + i < w->nseg; i++)
            if (w->seg[cs + i].closed & (1 << (a->lane + 1))) return;
        a->lane++;
        a->lane_hold = 100;
    }
}

void ai_think(World *w)
{
    Ai *a = &w->ai;
    Car *c = &w->car;
    const RouteInfo *r = &route_info[w->route];
    int cs = seg_of(c->z), k, i, perf = perfect(w);
    int32_t limit;

    /* the low sun */
    if (!perf && w->glare >= 12) {
        if (!a->blinded) {
            a->blinded = 1;
            a->blind_z = c->z;
            a->blind_x = (road_cx(w, c->z) * (1 << FBITS)) + c->d;
            a->blind_dir = w->seg[cs < w->nseg ? cs : w->nseg - 1].dir;
            if (a->on) { push(w, EV_BLINDED, cs, 0); world_say(w, MSG_BLINDED); }
        }
    } else if (a->blinded && w->glare < 6) {
        a->blinded = 0;
        if (a->on) world_say(w, MSG_RECALC);
    }

    if (a->on) ai_lanes(w);

    /* the plan: where it means to be at each of the next 16 segments */
    a->plan_from = cs;
    for (k = 0; k < 16; k++) {
        int32_t z = (int32_t)(cs + k) * SEGF, d;
        if (a->blinded) {
            int32_t x = a->blind_x + ((a->blind_dir * (z - a->blind_z)) >> SEG_BITS);
            d = x - (road_cx(w, z) * (1 << FBITS));
        } else {
            d = lane_centre(w, a->lane) * (1 << FBITS);
            if (!perf) d += ghost_at(w, z) * (1 << FBITS);
        }
        a->plan[k] = d;
    }
    /* give a cyclist room: move over within the lane to pass */
    for (i = 0; i < MAX_OBJS; i++) {
        const Obj *o = &w->obj[i];
        int32_t dz = o->z - c->z;
        int s0;
        if (o->type != OBJ_CYCLIST || dz < -2 * SEGF || dz > 8 * SEGF || a->blinded) continue;
        s0 = seg_of(o->z) - cs;
        for (k = s0 - 6; k <= s0 + 2; k++)
            if (k >= 0 && k < 16 && iabs(a->plan[k] - o->d) < (34 * (1 << FBITS)))
                a->plan[k] = o->d - (34 * (1 << FBITS));
    }
    if (perf) {
        /* pull over for an ambulance, swerve round debris */
        for (i = 0; i < MAX_OBJS; i++) {
            const Obj *o = &w->obj[i];
            int32_t dz = o->z - c->z;
            if (o->type == OBJ_AMBULANCE && !(o->flags & OF_PASSING) && dz < 0 && dz > -8 * SEGF &&
                r->nl == 1) {
                for (k = 0; k < 16; k++) a->plan[k] += 50 * (1 << FBITS);
            }
            if (o->type == OBJ_DEBRIS && dz > -SEGF && dz < 12 * SEGF &&
                iabs(o->d - a->plan[0]) < (30 * (1 << FBITS))) {
                int s = seg_of(o->z) - cs;
                int32_t to = swerve_to(w, o);
                for (k = s - 8; k <= s + 1; k++)
                    if (k >= 0 && k < 16) a->plan[k] = to;
            }
        }
    }

    /* what it sees */
    a->nseen = 0;
    if (!a->blinded) {
        for (i = 0; i < MAX_OBJS && a->nseen < MAX_SEEN; i++) {
            const Obj *o = &w->obj[i];
            int32_t dz = o->z - c->z;
            if (o->type == OBJ_NONE || dz < -6 * SEGF || dz > 30 * SEGF) continue;
            if (o->type == OBJ_DEBRIS && !perf) continue;      /* too small to matter, it thinks */
            a->seen[a->nseen].z = o->z;
            a->seen[a->nseen].d = o->d;
            a->seen[a->nseen].type = o->type;
            a->seen[a->nseen].phantom = 0;
            a->nseen++;
        }
    }

    /* the overpass shadow: something huge in the lane, it decides */
    if (!perf && a->on && a->phantom_timer == 0) {
        for (k = 1; k <= 6 && cs + k < w->nseg; k++) {
            int s = cs + k;
            if ((w->seg[s].flags & SF_OVERPASS) && !(w->seg[s - 1].flags & SF_OVERPASS) &&
                s != a->phantom_seg) {
                a->phantom_seg = s;
                a->phantom_timer = 30;
                push(w, EV_PHANTOM, s, 0);
                world_say(w, MSG_OBSTACLE);
                break;
            }
        }
    }
    if (a->phantom_timer > 0) {
        if (a->nseen < MAX_SEEN) {
            a->seen[a->nseen].z = (int32_t)a->phantom_seg * SEGF;
            a->seen[a->nseen].d = c->d;
            a->seen[a->nseen].type = OBJ_CAR;
            a->seen[a->nseen].phantom = 1;
            a->nseen++;
        }
        if (--a->phantom_timer == 0 && a->on) world_say(w, MSG_RECALC);
    }

    /* speed */
    limit = FROM_KMH(r->limit_kmh * w->tune.speed_pct / 100);
    if (perf) {
        for (k = 0; k < 9 && cs + k < w->nseg; k++)
            if (w->seg[cs + k].flags & SF_FLOOD) {
                int32_t slow = FROM_KMH(aqua_kmh(w) * 7 / 10);
                if (slow < limit) limit = slow;
                break;
            }
    }
    for (i = 0; i < a->nseen; i++) {
        const Seen *s = &a->seen[i];
        int32_t dz = s->z - c->z, pd, gap, des, ov = 0;
        int idx, len;
        if (dz <= 0 || s->phantom) continue;
        idx = seg_of(s->z) - cs;
        if (idx > 15) idx = 15;
        pd = a->plan[idx < 0 ? 0 : idx];
        len = obj_len(s->type);
        /* in the path, or about to cross it */
        {
            int32_t dd = iabs(s->d - pd), band = (int32_t)(CAR_WID + obj_wid(s->type) + 6) * (1 << FBITS);
            int hit = dd < band;
            for (k = 0; k < MAX_OBJS && !hit; k++) {
                const Obj *o = &w->obj[k];
                if (o->type == s->type && o->z == s->z && o->d == s->d && o->vd) {
                    int32_t later = o->d + o->vd * 45;
                    hit = (o->d > pd + band && later < pd + band) || (o->d < pd - band && later > pd - band);
                    ov = o->v;
                }
            }
            if (!hit) continue;
        }
        for (k = 0; k < MAX_OBJS; k++)
            if (w->obj[k].type == s->type && w->obj[k].z == s->z) { ov = w->obj[k].v; break; }
        if (ov < 0) ov = 0;
        gap = (dz >> FBITS) - len - CAR_LEN;
        if (ov == 0) {
            /* standing or crossing: stop short of it */
            int32_t stop = (c->v * c->v) / (2 * 8 * 16) + 50;
            if (gap < stop + 60) limit = 0;
        } else {
            des = (c->v * 25) / 16 + 30;
            if (gap < des) {
                int32_t t = ov + (gap - des) * 2;
                if (t < 0) t = 0;
                if (t < limit) limit = t;
            }
        }
    }
    if (a->phantom_timer > 0) limit = 0;
    if (perf) {
        /* wait before cones it can't get round yet; creep while moving over */
        int in = c->d >= 0 ? lane_of(w, c->d) : -1;
        for (k = 1; k <= 5 && cs + k < w->nseg; k++) {
            uint8_t cl = w->seg[cs + k].closed;
            if (a->lane >= 0 && (cl & (1 << a->lane))) { limit = 0; break; }
            if (in >= 0 && (cl & (1 << in)) &&
                iabs(c->d - (lane_centre(w, a->lane) * (1 << FBITS))) > (12 * (1 << FBITS))) {
                if (limit > FROM_KMH(12)) limit = FROM_KMH(12);
                break;
            }
        }
    }
    if (cs >= w->finish) limit = (w->nseg - 6 - cs) > 0 ? FROM_KMH(30) : 0;
    a->target_v = limit;

    /* comments */
    for (i = 0; i < w->nhaz; i++) {
        const Hazard *h = &w->haz[i];
        if (w->haz_said[i] || !a->on || w->state != GS_PLAY) continue;
        if (h->type == HZ_FLOOD && w->route > 0 && cs >= h->seg - 3 && cs < h->seg) {
            w->haz_said[i] = 1;
            world_say(w, MSG_WATER_ANYWAY);
        } else if (h->type == HZ_GHOST && ghost_at(w, c->z + 4 * SEGF) > LANE_W / 2) {
            w->haz_said[i] = 1;
            world_say(w, MSG_ROAD_PROBABLY);
        } else if (h->type == HZ_TRACTOR && cs > h->seg + 6 && cs < h->seg + h->len &&
                   c->v < FROM_KMH(30)) {
            w->haz_said[i] = 1;
            world_say(w, MSG_PATIENT);
        }
    }
}

int32_t ai_steer(const World *w)
{
    const Ai *a = &w->ai;
    const Car *c = &w->car;
    int idx = seg_of(c->z + 2 * SEGF) - a->plan_from;
    int32_t target, k, s;
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
    target = a->plan[idx];
    k = a->blinded ? 0 : road_curve(w, c->z);
    s = (k * c->v * 9) / 40 + ((target - c->d) >> FBITS) * 4;
    if (s > 256) s = 256;
    if (s < -256) s = -256;
    return s;
}

/* ---------- the car ---------- */
/* the town's flooded streets are deeper: they aquaplane at lower speed */
int aqua_kmh(const World *w)
{
    return w->route == 3 ? w->tune.aqua_kmh * 3 / 5 : w->tune.aqua_kmh;
}

static void respawn(World *w)
{
    Car *c = &w->car;
    const RouteInfo *r = &route_info[w->route];
    int s = seg_of(c->z) - 4, i;

    while (s > 2 && (w->seg[s].flags & (SF_FLOOD | SF_GHOST | SF_CONES | SF_GLARE))) s--;
    /* and well back from the start of whatever caught it, so there's room
     * to get going and take it by hand */
    for (i = 0; i < w->nhaz; i++)
        if (w->haz[i].seg >= s && w->haz[i].seg < s + 10) s = w->haz[i].seg - 10;
    if (s < 2) s = 2;
    c->z = (int32_t)s * SEGF;
    c->d = lane_centre(w, r->nl - 1) * (1 << FBITS);
    c->v = c->s = c->lat = 0;
    c->state = CS_DRIVING;
    c->cool = 0;
    w->ai.on = 0;
    w->ai.lane = r->nl - 1;
    w->ai.blinded = 0;
    w->mult = 1;
    w->ap_frames = 0;
    for (i = 0; i < MAX_OBJS; i++) {
        Obj *o = &w->obj[i];
        if (o->type != OBJ_NONE && iabs(o->z - c->z) < 4 * SEGF && o->type != OBJ_DEBRIS &&
            o->type != OBJ_AMBULANCE) o->type = OBJ_NONE;
    }
    push(w, EV_RESPAWN, s, 0);
}

static void off_road(World *w, int side, int32_t depth, int32_t edge)
{
    Car *c = &w->car;
    const Seg *g = &w->seg[seg_of(c->z) < w->nseg ? seg_of(c->z) : w->nseg - 1];
    int t = g->side[side];
    int rail = g->flags & (side ? SF_RAIL_R : SF_RAIL_L);

    if (rail || t == SIDE_MEDIAN || t == SIDE_WALL) {
        c->d = (edge + (side ? -2 : 2)) * (1 << FBITS);
        c->s = 0;
        if (!c->cool) {
            c->v = c->v * 3 / 4;
            c->cool = 15;
            hurt(w, rail ? 5 : t == SIDE_MEDIAN ? 8 : 10, EV_SCRAPE);
        }
    } else if (t == SIDE_GRASS) {
        if (c->v > FROM_KMH(40)) c->v -= 6;
        if (depth > 160) c->d = (edge + (side ? 160 : -160)) * (1 << FBITS);
    } else if (t == SIDE_TOWN) {
        if (depth > 40) {
            c->d = (edge + (side ? 38 : -38)) * (1 << FBITS);
            if (!c->cool) {
                c->v /= 3;
                c->cool = 25;
                hurt(w, 25, EV_CRASH);
                world_say(w, MSG_OOPS);
            }
        }
    } else if ((t == SIDE_LEVEE || t == SIDE_CANAL) ? depth > 16 : depth > 8) {
        int splash = t == SIDE_LEVEE || t == SIDE_CANAL;
        c->state = splash ? CS_SPLASH : CS_ROLL;
        c->timer = 50;
        c->lat = side ? 24 : -24;
        w->ai.on = 0;
        hurt(w, 40, splash ? EV_SPLASH : EV_ROLL);
        if (w->state == GS_PLAY) world_say(w, MSG_TOW);
    }
}

static void car_update(World *w, unsigned in)
{
    Car *c = &w->car;
    Ai *a = &w->ai;
    unsigned edge = in & ~w->last_in;
    int cs = seg_of(c->z);
    int32_t k, pull, le, re, u;
    int playing = w->state == GS_PLAY;

    if (c->cool > 0) c->cool--;
    if (c->state == CS_SPLASH || c->state == CS_ROLL) {
        c->v = c->v * 15 / 16;
        c->z += c->v;
        c->d += c->lat;
        if (--c->timer <= 0) respawn(w);
        return;
    }

    if (playing) {
        if (edge & IN_FIRE) world_set_ap(w, !a->on);
        else if (a->on && (in & (IN_LEFT | IN_RIGHT | IN_DOWN))) world_set_ap(w, 0);
    }

    if (a->on || !playing) {
        int dec = a->phantom_timer > 0 ? 18 : 8;
        c->s = ai_steer(w);
        if (c->v < a->target_v) { c->v += 3; if (c->v > a->target_v) c->v = a->target_v; }
        else if (c->v > a->target_v) { c->v -= dec; if (c->v < a->target_v) c->v = a->target_v; }
    } else {
        if (in & IN_LEFT) { c->s -= 40; if (c->s < -256) c->s = -256; }
        else if (in & IN_RIGHT) { c->s += 40; if (c->s > 256) c->s = 256; }
        else if (c->s > 0) { c->s -= 64; if (c->s < 0) c->s = 0; }
        else if (c->s < 0) { c->s += 64; if (c->s > 0) c->s = 0; }
        if (in & IN_DOWN) c->v -= 12;
        else if (in & IN_UP) c->v += 4;
        else c->v -= 1;
        if (c->v < 0) c->v = 0;
        if (c->v > FROM_KMH(MAX_KMH)) c->v = FROM_KMH(MAX_KMH);
    }

    k = road_curve(w, c->z);
    pull = (k * c->v * c->v) / 8000;
    if (cs < w->nseg && (w->seg[cs].flags & SF_FLOOD) && c->v > FROM_KMH(aqua_kmh(w))) {
        if (c->state != CS_AQUAPLANE) push(w, EV_AQUAPLANE, KMH(c->v), 0);
        c->state = CS_AQUAPLANE;
        c->lat = -((c->v - FROM_KMH(aqua_kmh(w))) * 3) / 2;
        c->v -= 1;
    } else {
        c->state = CS_DRIVING;
        c->lat = (c->s * c->v) / 1800 - pull;
    }
    c->d += c->lat;
    c->z += c->v;

    u = c->d >> FBITS;
    le = road_left(w) - SHOULDER;
    re = road_right(w) + SHOULDER;
    if (u < le) off_road(w, 0, le - u, le);
    else if (u > re) off_road(w, 1, u - re, re);

    c->hist[c->hist_pos & 31] = c->v;
    c->hist_pos++;
}

/* ---------- route and game flow ---------- */
static void place_car(World *w)
{
    const RouteInfo *r = &route_info[w->route];
    Car *c = &w->car;
    memset(c, 0, sizeof(*c));
    c->z = 3 * SEGF;
    c->d = lane_centre(w, r->nl - 1) * (1 << FBITS);
    c->v = FROM_KMH(r->limit_kmh * 7 / 10);
    memset(&w->ai, 0, sizeof(w->ai));
    w->ai.on = 1;
    w->ai.lane = r->nl - 1;
    w->ai.phantom_seg = -1;
}

void world_start_route(World *w, int route)
{
    route_build(w, route);
    memset(w->obj, 0, sizeof(w->obj));
    memset(w->knocked, 0, sizeof(w->knocked));
    memset(w->haz_said, 0, sizeof(w->haz_said));
    place_car(w);
    w->mult = 1;
    w->ap_frames = 0;
    w->ap_rem = 0;
    w->dist_ap = w->dist_all = 0;
    w->saves = w->needless = w->takeovers = 0;
    w->pending_save = -1;
    w->incident = 0;
    w->route_bonus = 0;
    w->glare = 0;
    w->siren = 0;
    w->state = GS_INTRO;
    w->state_timer = 100;
    push(w, EV_ROUTE_START, route, 0);
}

void world_start_game(World *w)
{
    w->score = 0;
    w->cover = 100;
    w->total_saves = w->total_needless = 0;
    world_start_route(w, 0);
    world_say(w, MSG_HANDS);
}

void world_init(World *w)
{
    Tune t = w->tune;
    int keep = w->tune.speed_pct != 0;
    memset(w, 0, sizeof(*w));
    if (keep) w->tune = t; else world_defaults(&w->tune);
    w->rng = 1234567;
    world_start_route(w, 0);
    w->nev = 0;
    w->state = GS_TITLE;
    w->cover = 100;
}

int32_t world_ap_pct(const World *w)
{
    if (w->dist_all <= 0) return 100;
    return (int32_t)(((w->dist_ap >> 8) * 100) / ((w->dist_all >> 8) ? (w->dist_all >> 8) : 1));
}

static void arrive(World *w)
{
    int32_t pct = world_ap_pct(w);
    w->route_bonus = pct * 50 + w->saves * 500;
    w->score += w->route_bonus;
    w->cover += 30;
    if (w->cover > 100) w->cover = 100;
    w->state = GS_ARRIVED;
    w->state_timer = 250;
    w->ai.on = 1;
    w->ai.blinded = 0;
    w->pending_save = -1;
    push(w, EV_ARRIVED, w->route, w->score);
    world_say(w, MSG_ARRIVED);
}

static void score_update(World *w)
{
    Car *c = &w->car;
    int32_t m;
    if (w->state != GS_PLAY || c->state == CS_SPLASH || c->state == CS_ROLL) return;
    w->dist_all += c->v;
    if (!w->ai.on) return;
    w->dist_ap += c->v;
    w->ap_rem += c->v * w->mult;
    w->score += w->ap_rem >> 8;
    w->ap_rem &= 255;
    w->ap_frames++;
    m = 1 + w->ap_frames / (w->tune.mult_secs * 25);
    if (m > 8) m = 8;
    if (m > w->mult) {
        w->mult = m;
        push(w, EV_MULT_UP, m, 0);
    }
}

void world_update(World *w, unsigned in)
{
    unsigned edge = in & ~w->last_in, raw = in;
    int cs, k, g;

    w->frame++;
    if (w->msg_timer > 0 && --w->msg_timer == 0) w->msg = MSG_NONE;
    if (w->state_timer > 0) w->state_timer--;

    switch (w->state) {
    case GS_TITLE:
        if (edge & (IN_FIRE | IN_START)) { world_start_game(w); w->last_in = in; return; }
        if (seg_of(w->car.z) >= w->finish - 2) place_car(w);
        break;
    case GS_INTRO:
        if (w->state_timer == 0 || (edge & (IN_FIRE | IN_START))) {
            w->state = GS_PLAY;
            edge = 0;
            in &= ~IN_FIRE;
        }
        break;
    case GS_ARRIVED:
        if (w->state_timer == 0 || (w->state_timer < 210 && (edge & (IN_FIRE | IN_START)))) {
            if (w->route < NUM_ROUTES - 1) {
                w->state = GS_UPDATE;
                w->state_timer = 200;
                world_say(w, MSG_UPDATED);
            } else {
                w->state = GS_WIN;
                w->state_timer = 50;
                push(w, EV_WIN, w->score, 0);
            }
        }
        break;
    case GS_UPDATE:
        if (w->state_timer == 0 || (w->state_timer < 160 && (edge & (IN_FIRE | IN_START))))
            world_start_route(w, w->route + 1);
        w->last_in = in;
        return;
    case GS_GAMEOVER:
    case GS_WIN:
        if (w->state_timer == 0 && (edge & (IN_FIRE | IN_START))) {
            world_init(w);
            w->last_in = in;
            return;
        }
        break;
    }

    /* the sun: glare builds up over the segments ahead */
    cs = seg_of(w->car.z);
    g = 0;
    for (k = 0; k < 4 && cs + k < w->nseg; k++)
        if (w->seg[cs + k].flags & SF_GLARE) { g = 16; break; }
    if (w->glare < g) w->glare++;
    else if (w->glare > g) w->glare--;

    if (w->state == GS_PLAY || w->state == GS_TITLE) {
        hazards_update(w);
        traffic_spawn(w);
    }
    ai_think(w);
    car_update(w, w->state == GS_PLAY ? in : 0);
    objects_update(w);
    collide(w);
    score_update(w);

    if (w->state == GS_PLAY && seg_of(w->car.z) >= w->finish && w->car.state == CS_DRIVING)
        arrive(w);
    w->last_in = raw;                   /* the press that started the route isn't a new one next frame */
}

/* Put the car just before a hazard (or the finish) on whichever route
 * has one, for testing from the host. */
int world_scene(World *w, const char *name)
{
    int r, i, t, hz = -1, rt = w->route, s;

    if (strcmp(name, "finish")) {
        for (t = 1; t < HZ_COUNT && strcmp(name, hz_names[t]); t++)
            ;
        if (t >= HZ_COUNT) return 0;
        for (r = 0; r < NUM_ROUTES && hz < 0; r++) {
            rt = (w->route + r) % NUM_ROUTES;
            route_build(w, rt);
            for (i = 0; i < w->nhaz; i++)
                if (w->haz[i].type == t) { hz = i; break; }
        }
        if (hz < 0) return 0;
    }
    if (w->state == GS_TITLE || w->state == GS_GAMEOVER || w->state == GS_WIN) {
        w->score = 0;
        w->cover = 100;
        w->total_saves = w->total_needless = 0;
    }
    world_start_route(w, rt);
    if (hz < 0) s = w->finish - 12;
    else {
        s = w->haz[hz].type == HZ_SIREN ? w->haz[hz].seg - 1 : w->haz[hz].seg - 22;
        /* not in the middle of the hazard before it */
        for (i = 0; i < w->nhaz; i++)
            if (i != hz && s >= w->haz[i].seg - 4 && s <= w->haz[i].seg + w->haz[i].len + 2 &&
                w->haz[i].seg + w->haz[i].len + 3 < w->haz[hz].seg - 6)
                s = w->haz[i].seg + w->haz[i].len + 3;
    }
    if (s < 3) s = 3;
    w->car.z = (int32_t)s * SEGF;
    for (i = 0; i < w->nhaz; i++)
        if (w->haz[i].seg + w->haz[i].len + 2 < s)
            w->haz_state[i] = 2;
    w->state = GS_PLAY;
    w->nev = 0;
    return 1;
}
