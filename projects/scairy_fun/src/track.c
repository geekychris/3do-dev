// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * track.c - the four routes: sections of road (bends, hills, what lies
 * beside it, guardrails) built into 8 m segments, and the hazards along
 * them.
 */
#include "sc.h"

const RouteInfo route_info[NUM_ROUTES] = {
    /* name            destination      AI      nl no limit traffic sky */
    { "LEVEE ROAD",    "THE MARINA",    "1.0",  1, 1,  80,  10, 0 },
    { "RUSH HOUR",     "THE OFFICE",    "2.0",  3, 0, 110,  34, 1 },
    { "MOUNTAIN PASS", "THE SKI LODGE", "3.0",  1, 1,  70,   8, 2 },
    { "FLOODED TOWN",  "HOME",          "4.0",  1, 1,  50,  12, 3 },
};

/* A section: len segments; the road turns by `turn` (change of dir, units
 * of sideways travel per segment) and climbs by `hill` units over it. */
typedef struct {
    int16_t len, turn, hill;
    uint8_t left, right, flags;
} Sec;

#define L SIDE_LEVEE
#define D SIDE_DROP
#define G SIDE_GRASS
#define W SIDE_WALL
#define C SIDE_CLIFF
#define M SIDE_MEDIAN
#define T SIDE_TOWN
#define K SIDE_CANAL
#define RL SF_RAIL_L
#define RR SF_RAIL_R

static const Sec levee[] = {
    {  40,   0,   0, L, D, 0 },
    {  24,  36,   0, L, D, 0 },
    {  24, -36,   0, L, D, 0 },
    {  50,   0,   0, L, D, 0 },        /* 100: flooded */
    {  30, -40,   0, L, D, 0 },        /* 150..: a left bend with old lines */
    {  30,  40,   0, L, D, 0 },
    {  40,   0,  60, L, D, 0 },
    {  40,  30, -60, L, D, 0 },
    {  40, -30,   0, L, D, 0 },
    {  60,   0,   0, L, D, 0 },        /* 340: flooded again */
    {  60,   0,   0, L, G, 0 },
    {   0 }
};

static const Sec freeway[] = {
    {  50,   0,   0, M, G, 0 },
    {  40,  30,  40, M, D, RR },
    {  40, -30, -40, M, D, RR },
    {  60,   0,   0, M, G, 0 },
    {  50, -24,   0, M, G, 0 },
    {  50,  24,   0, M, G, 0 },
    {  60,   0,  50, M, D, RR },
    {  40,   0, -50, M, G, 0 },
    {  80,   0,   0, M, G, 0 },
    {   0 }
};

static const Sec mountain[] = {
    {  30,   0,   0, W, C, RR },
    {  30,  60,  80, W, C, RR },
    {  30, -60,  80, W, C, RR },       /* 90..: into the sun */
    {  24, -50,  60, W, C, RR },
    {  24,  50,  40, W, C, RR },
    {  40,   0,  60, W, C, RR },
    {  30,  56,  60, W, C, RR },
    {  30, -56,  40, W, C, 0 },        /* 208..: old lines, no rail */
    {  30, -60,  40, W, C, RR },       /* 268..: into the sun again */
    {  30,  60,  20, W, C, RR },
    {  40,   0,   0, W, C, RR },
    {  60,   0, -40, G, G, 0 },
    {   0 }
};

static const Sec town[] = {
    {  40,   0,   0, T, T, 0 },
    {  30,  30,   0, K, T, RL },
    {  30, -30,   0, K, T, 0 },        /* 84..: flooded, towards the canal */
    {  50,   0,   0, K, T, 0 },
    {  30, -40,   0, T, T, 0 },
    {  30,  40,   0, T, T, 0 },
    {  60,   0,   0, K, T, 0 },        /* 250..: flooded again */
    {  40,  24,   0, T, T, 0 },
    {  40, -24,   0, T, T, 0 },
    {  60,   0,   0, T, T, 0 },
    {   0 }
};

static const Sec *const route_secs[NUM_ROUTES] = { levee, freeway, mountain, town };

/* hazards: type, segment, length, param */
static const Hazard levee_haz[] = {
    { HZ_CYCLIST,  50,  0, 0 },
    { HZ_FLOOD,   108, 12, -1 },       /* water pulls towards the river */
    { HZ_DEER,    140,  0, 0 },
    { HZ_GHOST,   152, 26, 150 },      /* the old lines lead over the drop */
    { HZ_TRACTOR, 236, 20, 0 },
    { HZ_FLOOD,   352, 10, -1 },
    { HZ_NONE }
};

static const Hazard freeway_haz[] = {
    { HZ_MERGE,    40,  0, 2 },
    { HZ_PHANTOM, 100,  2, 0 },
    { HZ_CONES,   170, 36, 2 },        /* the right lane is closed */
    { HZ_SIREN,   236,  0, 0 },
    { HZ_PHANTOM, 300,  2, 0 },
    { HZ_DEBRIS,  360,  0, -1 },
    { HZ_NONE }
};

static const Hazard mountain_haz[] = {
    { HZ_GLARE,    88, 22, 0 },
    { HZ_DEBRIS,  150,  0, -1 },
    { HZ_GOATS,   185,  0, 0 },
    { HZ_GHOST,   206, 24, 140 },
    { HZ_GLARE,   266, 22, 0 },
    { HZ_DEBRIS,  330,  0, -1 },
    { HZ_NONE }
};

static const Hazard town_haz[] = {
    { HZ_PEDS,     30,  0, 0 },
    { HZ_FLOOD,    84, 14, -1 },       /* towards the canal */
    { HZ_CONES,   150, 30, 0 },        /* detour: our lane is closed */
    { HZ_SIREN,   205,  0, 0 },
    { HZ_FLOOD,   252, 12, -1 },
    { HZ_PHANTOM, 300,  2, 0 },
    { HZ_PEDS,    330,  0, 0 },
    { HZ_NONE }
};

static const Hazard *const route_haz[NUM_ROUTES] = { levee_haz, freeway_haz, mountain_haz, town_haz };

#define RUNOUT 40                       /* straight road after the finish */

void route_build(World *w, int route)
{
    const Sec *s;
    const Hazard *h;
    int32_t cx = 0, y = 0, dir = 0;
    int n = 0, i, k;

    w->route = route;
    for (s = route_secs[route]; s->len && n < MAX_SEGS - RUNOUT - 1; s++) {
        int32_t dir0 = dir, y0 = y;
        for (i = 0; i < s->len && n < MAX_SEGS - RUNOUT - 1; i++, n++) {
            Seg *g = &w->seg[n];
            dir = dir0 + (s->turn * (i + 1)) / s->len;
            g->cx = cx;
            g->y = (int16_t)y;
            g->dir = (int16_t)dir;
            g->side[0] = s->left;
            g->side[1] = s->right;
            g->flags = s->flags;
            g->closed = 0;
            g->ghost = 0;
            cx += dir;
            y = y0 + (s->hill * (i + 1)) / s->len;
        }
    }
    /* the destination, then straight road to look at */
    w->finish = n;
    for (i = 0; i < RUNOUT + 1; i++, n++) {
        Seg *g = &w->seg[n];
        g->cx = cx;
        g->y = (int16_t)y;
        g->dir = (int16_t)dir;
        g->side[0] = w->seg[n - 1].side[0];
        g->side[1] = w->seg[n - 1].side[1];
        g->flags = i < 2 ? SF_FINISH : 0;
        g->closed = 0;
        g->ghost = 0;
        cx += dir;
    }
    w->nseg = n;

    w->nhaz = 0;
    for (h = route_haz[route]; h->type != HZ_NONE && w->nhaz < MAX_HAZ; h++) {
        w->haz[w->nhaz] = *h;
        w->haz_state[w->nhaz] = 0;
        w->nhaz++;
        for (k = 0; k < h->len && h->seg + k < n; k++) {
            Seg *g = &w->seg[h->seg + k];
            switch (h->type) {
            case HZ_FLOOD:   g->flags |= SF_FLOOD; break;
            case HZ_PHANTOM: g->flags |= SF_OVERPASS; break;
            case HZ_GLARE:   g->flags |= SF_GLARE; break;
            case HZ_CONES:   g->flags |= SF_CONES; g->closed |= 1 << h->param; break;
            case HZ_GHOST:
                g->flags |= SF_GHOST;
                /* the old lines peel away over the first two thirds */
                g->ghost = (int16_t)(k * 3 >= h->len * 2 ? h->param
                                     : (h->param * k * 3) / (h->len * 2));
                break;
            }
        }
    }
}

int seg_of(int32_t z)
{
    return (int)(z >> (FBITS + SEG_BITS));
}

static int clamp_seg(const World *w, int i)
{
    if (i < 0) return 0;
    if (i > w->nseg - 2) return w->nseg - 2;
    return i;
}

int32_t road_cx(const World *w, int32_t z)
{
    int i = seg_of(z), j = clamp_seg(w, i);
    int32_t f = (z >> FBITS) - ((int32_t)j << SEG_BITS);
    return w->seg[j].cx + ((w->seg[j].dir * f) >> SEG_BITS);
}

int32_t road_y(const World *w, int32_t z)
{
    int j = clamp_seg(w, seg_of(z));
    int32_t f = (z >> FBITS) - ((int32_t)j << SEG_BITS);
    return w->seg[j].y + (((w->seg[j + 1].y - w->seg[j].y) * f) >> SEG_BITS);
}

int road_dir(const World *w, int32_t z)
{
    int j = clamp_seg(w, seg_of(z));
    int32_t f = (z >> FBITS) - ((int32_t)j << SEG_BITS);
    int32_t d = w->seg[j].dir + (((w->seg[j + 1].dir - w->seg[j].dir) * f) >> SEG_BITS);
    return sc_atan_ratio(d, SEG_LEN);
}

int32_t road_curve(const World *w, int32_t z)
{
    int j = clamp_seg(w, seg_of(z));
    return w->seg[j + 1].dir - w->seg[j].dir;
}

int32_t ghost_at(const World *w, int32_t z)
{
    int j = clamp_seg(w, seg_of(z));
    int32_t f = (z >> FBITS) - ((int32_t)j << SEG_BITS);
    if (!(w->seg[j].flags & SF_GHOST)) return 0;
    return w->seg[j].ghost + (((w->seg[j + 1].ghost - w->seg[j].ghost) * f) >> SEG_BITS);
}

int32_t road_left(const World *w)
{
    const RouteInfo *r = &route_info[w->route];
    return r->no ? -(int32_t)r->no * LANE_W : -8;
}

int32_t road_right(const World *w)
{
    return (int32_t)route_info[w->route].nl * LANE_W;
}

int32_t lane_centre(const World *w, int lane)
{
    (void)w;
    return (int32_t)lane * LANE_W + LANE_W / 2;
}

int lane_of(const World *w, int32_t d)
{
    int32_t u = d >> FBITS;
    int n = route_info[w->route].nl;
    if (u < 0) return -1;
    if (u >= n * LANE_W) return n - 1;
    return (int)(u / LANE_W);
}
