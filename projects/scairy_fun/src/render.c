// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - ScAIry Fun's renderer: a road drawn as segments, far to near.
 *
 * Each 8 m segment has a cross-section of 8 points: two on each side of
 * the road out in the scenery (levee slope and river, embankment and
 * field, rock wall, cliff and valley, pavement, canal), the shoulder edges
 * and the road edges. Its strips are drawn outer to inner and then the
 * road, lines, rails, the autopilot's planned path, cones, scenery and
 * whatever is on it, so a painter's algorithm by segment gets hills and
 * bends right without a depth sort.
 *
 * Polygons are filled by the CPU straight into the five bitplanes (the
 * span filler and clipper come from Terra Strike); text is drawn the same
 * way from the topaz 8 bitmap, so nothing waits for the blitter except the
 * sky and ground fills at the start of a frame.
 */
#include <string.h>
#include <stdio.h>

#include "sc.h"
#include "models.h"
#include "render.h"

#define SEGF       ((int32_t)SEG_LEN * (1 << FBITS))
#define CAM_BACK   230                  /* chase camera: units behind the car */
#define CAM_UP     92
#define CHASE_PITCH   1150
#define COCKPIT_PITCH 260
#define LINE_SEGS  24                   /* lane lines this far out */
#define SCENE_SEGS 30

RenderStats render_stats;
int32_t render_draw_dist = 32;

/* ---------- colours ---------- */
const uint16_t route_palette[NUM_ROUTES][32] = {
    /* levee road: morning */
    { 0x000, 0xFFF, 0x333, 0x999, 0x8BF, 0x9AB, 0x357, 0x469,
      0x444, 0x555, 0x998, 0x4A3, 0x5B4, 0x763, 0x874, 0x776,
      0xE22, 0x811, 0xFE2, 0xF72, 0x2EF, 0x3F5, 0x36D, 0x128,
      0xEEE, 0xAAB, 0x774, 0x263, 0x124, 0x642, 0xB85, 0x111 },
    /* rush hour: hazy noon */
    { 0x000, 0xFFF, 0x333, 0x999, 0xABC, 0xAAA, 0x467, 0x578,
      0x555, 0x666, 0x999, 0x6A4, 0x7B5, 0x875, 0x986, 0x887,
      0xE22, 0x811, 0xFE2, 0xF72, 0x2EF, 0x3F5, 0x36D, 0x128,
      0xEEE, 0xAAB, 0x774, 0x373, 0x124, 0x642, 0xB85, 0x111 },
    /* mountain pass: sunset */
    { 0x000, 0xFFF, 0x333, 0x999, 0xF96, 0x869, 0x358, 0x469,
      0x434, 0x545, 0x987, 0x574, 0x685, 0x653, 0x764, 0x766,
      0xE22, 0x811, 0xFE2, 0xF72, 0x2EF, 0x3F5, 0x36D, 0x128,
      0xEEE, 0xAAB, 0x774, 0x243, 0x124, 0x642, 0xB85, 0x111 },
    /* flooded town: overcast */
    { 0x000, 0xFFF, 0x333, 0x999, 0x99A, 0x778, 0x456, 0x567,
      0x333, 0x444, 0x888, 0x585, 0x696, 0x665, 0x776, 0x666,
      0xE22, 0x811, 0xFE2, 0xF72, 0x2EF, 0x3F5, 0x36D, 0x128,
      0xEEE, 0xAAB, 0x774, 0x353, 0x124, 0x642, 0xB85, 0x111 },
};

const uint16_t route_sky[NUM_ROUTES][2] = {
    { 0x36C, 0xBDF }, { 0x79B, 0xDDC }, { 0x448, 0xFA5 }, { 0x667, 0xBBB }
};

/* the body colours a model's COL_BODY faces take: lit, shaded */
static const uint8_t body_pens[6][2] = {
    { PEN_BLUE, PEN_BLUE + 1 }, { PEN_CARW, PEN_CARW + 1 }, { PEN_YELLOW, PEN_ORANGE },
    { PEN_KHAKI, PEN_BROWN }, { PEN_RED, PEN_RED + 1 }, { PEN_GREY, PEN_PANEL }
};
#define BODY_PLAYER 4

/* side profiles: (out, dy) of the middle and far points beyond the shoulder */
static const int16_t side_prof[SIDE_COUNT][4] = {
    { 200,    -2, 1600,   -8 },         /* grass */
    { 150, -LEVEE_H, 2000, -LEVEE_H },  /* levee: slope down to the river */
    { 220,  -200, 1600, -210 },         /* drop: embankment down to a field */
    {  30,   180,  700,  260 },         /* rock wall */
    {  50,  -500, 1800, -520 },         /* cliff */
    {  40,     0, 1600,   -4 },         /* median */
    {  70,     3, 1600,    3 },         /* town pavement */
    {  42,   -60, 1800,  -60 },         /* canal */
};

/* ---------- drawing: the shared engine's (a3d_draw.h) ---------- */
#define g_fill_blit  g_fill
#define g_wait       g_sync
#define text_shadow  g_text_shadow
#define text_centre  g_text_centre

/* lines in the 3D area only */
static void hline(Canvas *rp, int x0, int x1, int y, int col)
{
    if (y < 0 || y >= VIEW_H) return;
    g_hline(rp, x0, x1, y, col);
}

static void vline(Canvas *rp, int x, int y0, int y1, int col)
{
    if (y1 > VIEW_H - 1) y1 = VIEW_H - 1;
    if (y0 <= y1) g_vline(rp, x, y0, y1, col);
}

int render_init(void)
{
    a3_draw_init();
    models_init();
    return 1;
}

void render_cleanup(void) {}

/* ---------- draw list: in painter's order ---------- */
typedef struct {
    int16_t n;
    uint8_t col;
    int16_t x[MAX_PV], y[MAX_PV];
} DPoly;

#define MAX_DPOLY 1100
static DPoly pool[MAX_DPOLY];
static int npoly;

static void add_poly(int col, V3 *cv, int n)
{
    V3 cl[MAX_PV];
    int32_t xs[MAX_PV], ys[MAX_PV];
    DPoly *p;
    int i;

    n = clip_near(cv, n, cl);
    if (n < 3) return;
    /* in the drawing's own pixels: finer on AmigaOS 4 (x * FOV fits 32 bits;
     * the finer step comes from the remainder) */
    for (i = 0; i < n; i++) {
        int32_t qx = cl[i].x * FOV, qy = cl[i].y * FOV, z = cl[i].z;
#ifdef AMIGA3DO                         /* the 3DO: a reciprocal, not four software divides */
        xs[i] = VIEW_CX + a3_recip_div(qx, z);
        ys[i] = VIEW_CY - a3_recip_div(qy, z);
#else
        xs[i] = VIEW_CX * A3_SUB + (qx / z) * A3_SUB + ((qx % z) * A3_SUB) / z;
        ys[i] = VIEW_CY * A3_SUB - ((qy / z) * A3_SUB + ((qy % z) * A3_SUB) / z);
#endif
    }
    n = clip_rect(xs, ys, n, 0, 0, VIEW_W * A3_SUB - 1, VIEW_H * A3_SUB - 1);
    if (n < 3) return;
    if (npoly >= MAX_DPOLY) { render_stats.dropped++; return; }
    p = &pool[npoly++];
    p->n = (int16_t)n;
    p->col = (uint8_t)col;
    for (i = 0; i < n; i++) { p->x[i] = (int16_t)xs[i]; p->y[i] = (int16_t)ys[i]; }
}

/* a quad in camera space, skipped when it is clearly outside the view */
static void quad(V3 a, V3 b, V3 c, V3 d, int col)
{
    V3 q[4];
    if (a.z < NEAR_Z && b.z < NEAR_Z && c.z < NEAR_Z && d.z < NEAR_Z) return;
    if (a.x > a.z && b.x > b.z && c.x > c.z && d.x > d.z) return;
    if (a.x < -a.z && b.x < -b.z && c.x < -c.z && d.x < -d.z) return;
    q[0] = a; q[1] = b; q[2] = c; q[3] = d;
    add_poly(col, q, 4);
}

static void flush(Canvas *cv)
{
    int k, y1 = g_clip_y1;
    g_clip_y1 = VIEW_H - 1;
#ifdef AMIGA3DO
    if (a3_cels_begin(cv)) {            /* the 3DO: the cel engine fills them (sdk/amiga3d) */
        for (k = 0; k < npoly; k++) a3_cels_poly(pool[k].x, pool[k].y, pool[k].n, pool[k].col);
        a3_cels_end();
        g_clip_y1 = y1;
        return;
    }
#endif
    for (k = 0; k < npoly; k++) g_poly_sub(cv, pool[k].x, pool[k].y, pool[k].n, pool[k].col);
    g_clip_y1 = y1;
}

/* ---------- camera ---------- */
static int32_t cam_d;                   /* the chase camera's offset, lagging the car */
static int cam_yaw, chase_view = -1;

static int car_yaw_off(const Car *c)
{
    int32_t v = c->v > 8 ? c->v : 8;
    return sc_atan_ratio(c->lat, v);
}

void camera_follow(Cam *c, const World *w)
{
    const Car *car = &w->car;
    int target;
    if (w->view == CAM_COCKPIT) {
        int32_t z = car->z + (4 * (1 << FBITS));
        c->x = road_cx(w, z) + (car->d >> FBITS);
        c->z = z >> FBITS;
        c->y = road_y(w, z) + 17;
        c->yaw = (road_dir(w, car->z) + car_yaw_off(car)) & 0xFFFF;
        c->pitch = COCKPIT_PITCH;
        chase_view = CAM_COCKPIT;
    } else {
        int32_t z = car->z - ((int32_t)CAM_BACK * (1 << FBITS)), y0, y1;
        if (z < 0) z = 0;
        if (chase_view != CAM_CHASE) { cam_d = car->d; cam_yaw = road_dir(w, car->z); }
        cam_d += (car->d - cam_d) / 4;
        target = road_dir(w, car->z) + car_yaw_off(car) / 3;
        cam_yaw += (int16_t)(target - cam_yaw) / 3;
        c->x = road_cx(w, z) + (cam_d >> FBITS);
        c->z = z >> FBITS;
        y0 = road_y(w, z);
        y1 = road_y(w, car->z);
        c->y = (y0 > y1 ? y0 : y1) + CAM_UP;
        c->yaw = cam_yaw & 0xFFFF;
        c->pitch = CHASE_PITCH;
        chase_view = CAM_CHASE;
    }
    cam_setup(c);
}

int render_horizon(int view)
{
    Cam c;
    memset(&c, 0, sizeof(c));
    c.pitch = view == CAM_COCKPIT ? COCKPIT_PITCH : CHASE_PITCH;
    cam_setup(&c);
    return sc_horizon(&c);
}

/* ---------- models ---------- */
static const uint8_t obj_height[OBJ_COUNT] = { 0, 21, 58, 42, 32, 36, 13, 13, 49, 23, 31 };

static void add_model(const Cam *c, int mdl, int32_t wx, int32_t wy, int32_t wz, int yaw,
                      const uint8_t *body, int flash)
{
    const Model *m = &models[mdl];
    V3 cv[MDL_MAXV], ctr;
    int32_t co = sc_cos(yaw) >> 4, si = sc_sin(yaw) >> 4;
    int32_t vis_depth[MDL_MAXF];
    uint8_t vis_face[MDL_MAXF];
    int i, k, nvis = 0;

    cam_xform(c, wx, wy, wz, &ctr);
    if (ctr.z < -400 || ctr.z > FAR_Z + 200) return;
    if (ctr.x > ctr.z + 400 || ctr.x < -ctr.z - 400) return;

    for (i = 0; i < m->nv; i++) {
        int32_t x = m->v[i][0], y = m->v[i][1], z = m->v[i][2];
        int32_t rx = (x * co + z * si) >> 12;
        int32_t rz = (z * co - x * si) >> 12;
        cam_xform(c, wx + rx, wy + y, wz + rz, &cv[i]);
    }
    /* the faces that face the camera, farthest first: a model made of
     * several boxes isn't convex, so culling alone doesn't order them */
    for (k = 0; k < m->nf; k++) {
        const Face *f = &m->f[k];
        const V3 *a = &cv[f->idx[0]], *b = &cv[f->idx[1]], *d = &cv[f->idx[2]];
        int32_t ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
        int32_t vx = d->x - a->x, vy = d->y - a->y, vz = d->z - a->z;
        int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        int32_t depth;
        if (nx * a->x + ny * a->y + nz * a->z >= 0) continue;
        depth = a->z + b->z + d->z + (f->n == 4 ? cv[f->idx[3]].z : d->z);
        if (m->v[f->idx[0]][1] == 1 && m->v[f->idx[1]][1] == 1 && m->v[f->idx[2]][1] == 1)
            depth = 0x7FFFFFFF;                 /* the shadow on the ground goes first */
        for (i = nvis; i > 0 && vis_depth[i - 1] < depth; i--) {
            vis_face[i] = vis_face[i - 1];
            vis_depth[i] = vis_depth[i - 1];
        }
        vis_face[i] = (uint8_t)k;
        vis_depth[i] = depth;
        nvis++;
    }
    for (i = 0; i < nvis; i++) {
        const Face *f = &m->f[vis_face[i]];
        V3 q[4];
        int col = f->col;
        q[0] = cv[f->idx[0]]; q[1] = cv[f->idx[1]]; q[2] = cv[f->idx[2]];
        if (f->n == 4) q[3] = cv[f->idx[3]];
        if (col >= COL_FLASH) col = flash ? PEN_RED : PEN_BLUE;
        else if (col >= COL_BODY) col = body ? body[col - COL_BODY] : PEN_GREY;
        add_poly(col, q, f->n);
    }
    render_stats.models++;
}

/* ---------- the road ---------- */
#define NPTS 8
static int32_t off[NPTS], dyv[NPTS];

static void profile(const World *w, const Seg *g)
{
    const int16_t *L = side_prof[g->side[0]], *R = side_prof[g->side[1]];
    off[3] = road_left(w);  dyv[3] = 0;
    off[4] = road_right(w); dyv[4] = 0;
    off[2] = off[3] - SHOULDER; dyv[2] = 0;
    off[5] = off[4] + SHOULDER; dyv[5] = 0;
    off[1] = off[2] - L[0]; dyv[1] = L[1];
    off[0] = off[2] - L[2]; dyv[0] = L[3];
    off[6] = off[5] + R[0]; dyv[6] = R[1];
    off[7] = off[5] + R[2]; dyv[7] = R[3];
}

static void edge(const World *w, const Cam *c, int e, V3 *out)
{
    const Seg *s = &w->seg[e];
    int32_t z = (int32_t)e << SEG_BITS;
    int k;
    for (k = 0; k < NPTS; k++) cam_xform(c, s->cx + off[k], s->y + dyv[k], z, &out[k]);
}

static void strip(V3 *A, V3 *B, int k0, int k1, int col)
{
    quad(A[k0], A[k1], B[k1], B[k0], col);
}

/* a flat band on the road from x0 to x1 (units from the centre line) */
static void band(const World *w, const Cam *c, int i, int32_t x0a, int32_t x1a, int32_t x0b, int32_t x1b,
                 int lift, int col)
{
    const Seg *s = &w->seg[i], *t = &w->seg[i + 1];
    int32_t za = (int32_t)i << SEG_BITS, zb = za + SEG_LEN;
    V3 a, b, cc, d;
    cam_xform(c, s->cx + x0a, s->y + lift, za, &a);
    cam_xform(c, s->cx + x1a, s->y + lift, za, &b);
    cam_xform(c, t->cx + x1b, t->y + lift, zb, &cc);
    cam_xform(c, t->cx + x0b, t->y + lift, zb, &d);
    quad(a, b, cc, d, col);
}

static void line(const World *w, const Cam *c, int i, int32_t x, int col)
{
    band(w, c, i, x - 2, x + 2, x - 2, x + 2, 0, col);
}

/* a vertical band along the segment (guardrail, barrier) */
static void wall(const World *w, const Cam *c, int i, int32_t x, int y0, int y1, int col)
{
    const Seg *s = &w->seg[i], *t = &w->seg[i + 1];
    int32_t za = (int32_t)i << SEG_BITS, zb = za + SEG_LEN;
    V3 a, b, cc, d;
    cam_xform(c, s->cx + x, s->y + y0, za, &a);
    cam_xform(c, s->cx + x, s->y + y1, za, &b);
    cam_xform(c, t->cx + x, t->y + y1, zb, &cc);
    cam_xform(c, t->cx + x, t->y + y0, zb, &d);
    quad(a, b, cc, d, col);
}

static int far_pen(int side)
{
    switch (side) {
    case SIDE_LEVEE: case SIDE_CANAL: return PEN_WATER;
    case SIDE_CLIFF: return PEN_HAZE;
    case SIDE_TOWN: return PEN_PAVE;
    case SIDE_DROP: return PEN_GRASS + 1;
    default: return PEN_GRASS;
    }
}

static void side_strips(const World *w, int i, int side, V3 *A, V3 *B)
{
    int t = w->seg[i].side[side], alt = i & 1, alt2 = ((i >> 1) + (w->frame >> 4)) & 1;
    int inner, outer, ko = side ? 6 : 0, ki = side ? 5 : 1;
    switch (t) {
    case SIDE_LEVEE: inner = PEN_GRASS + alt; outer = PEN_WATER + alt2; break;
    case SIDE_DROP:  inner = PEN_DIRT + alt;  outer = PEN_GRASS + 1; break;
    case SIDE_WALL:  inner = alt ? PEN_ROCK : PEN_DIRT; outer = PEN_GRASS; break;
    case SIDE_CLIFF: inner = alt ? PEN_ROCK : PEN_DIRT; outer = PEN_HAZE; break;
    case SIDE_TOWN:  inner = PEN_PAVE; outer = PEN_PAVE; break;
    case SIDE_CANAL: inner = PEN_ROCK; outer = PEN_WATER + alt2; break;
    default:         inner = PEN_GRASS + alt; outer = PEN_GRASS; break;
    }
    strip(A, B, ko, ko + 1, outer);
    strip(A, B, ki, ki + 1, inner);
}

/* scenery beside segment i, the same every time (hashed from i) */
static void scenery(const World *w, const Cam *c, int i)
{
    const Seg *s = &w->seg[i];
    int side;
    for (side = 0; side < 2; side++) {
        uint32_t h = sc_hash((uint32_t)i * 2 + side + w->route * 977);
        int t = s->side[side], sgn = side ? 1 : -1, mdl = -1;
        int32_t edge_x = side ? road_right(w) + SHOULDER : road_left(w) - SHOULDER;
        int32_t out = 0, y = 0, z = ((int32_t)i << SEG_BITS) + (int32_t)(h >> 8 & 63);
        int yaw = (int)(h & 0xFFFF);
        switch (t) {
        case SIDE_GRASS:
            if (h % 3 == 0) { mdl = MDL_TREE; out = 90 + (int32_t)(h >> 12 & 511); y = -4; }
            break;
        case SIDE_LEVEE:
            if (side == 1 && i % 6 == 0) { mdl = MDL_POLE; out = 10; yaw = 0; }
            break;
        case SIDE_DROP:
            if (h % 3 == 0) { mdl = MDL_TREE; out = 240 + (int32_t)(h >> 12 & 511); y = -206; }
            else if (i % 6 == 3) { mdl = MDL_POLE; out = 10; yaw = 0; }
            break;
        case SIDE_WALL:
            if (h % 3 == 0) { mdl = MDL_PINE; out = 60 + (int32_t)(h >> 12 & 255); y = 200; }
            else if (h % 7 == 1) { mdl = MDL_BOULDER; out = 6; y = 0; }
            break;
        case SIDE_CLIFF:
            if (h % 2 == 0) { mdl = MDL_PINE; out = 200 + (int32_t)(h >> 12 & 1023); y = -515; }
            break;
        case SIDE_MEDIAN:
            if (i % 4 == 0) { mdl = MDL_LAMP; out = 14; yaw = 16384 * (side ? 1 : 3) - 16384; }
            break;
        case SIDE_TOWN:
            if (i % 2 == 0) { mdl = (h & 4) ? MDL_BLOCK : MDL_HOUSE; out = 120; y = 3; yaw = 0; z = (int32_t)i << SEG_BITS; }
            break;
        }
        if (mdl >= 0) {
            int32_t z_f = z * (1 << FBITS);
            add_model(c, mdl, road_cx(w, z_f) + edge_x + sgn * out, road_y(w, z_f) + y, z,
                      road_dir(w, z_f) + yaw, NULL, 0);
        }
    }
}

/* objects drawn with each segment: per-segment lists, far to near */
#define NVIS_MAX 48
static int16_t seg_first[NVIS_MAX + 2];
static int16_t obj_next[MAX_OBJS + 1];

static int32_t obj_z(const World *w, int idx)
{
    return idx == MAX_OBJS ? w->car.z : w->obj[idx].z;
}

static void sort_objects(const World *w, int s0, int s1)
{
    int i;
    for (i = 0; i <= s1 - s0 + 1; i++) seg_first[i] = -1;
    for (i = 0; i <= MAX_OBJS; i++) {
        int s, prev = -1, cur;
        int32_t z;
        if (i < MAX_OBJS && w->obj[i].type == OBJ_NONE) continue;
        if (i == MAX_OBJS && w->view == CAM_COCKPIT) continue;
        z = obj_z(w, i);
        /* draw it with the segment its near end (towards the camera) is in,
         * so no nearer road is painted over its back */
        s = seg_of(z - (int32_t)(i == MAX_OBJS ? obj_len(OBJ_CAR) : obj_len(w->obj[i].type)) * (1 << FBITS)) - s0;
        if (s < 0 || s > s1 - s0) continue;
        /* insert, keeping the farthest first */
        cur = seg_first[s];
        while (cur >= 0 && obj_z(w, cur) > z) { prev = cur; cur = obj_next[cur]; }
        obj_next[i] = (int16_t)cur;
        if (prev < 0) seg_first[s] = (int16_t)i; else obj_next[prev] = (int16_t)i;
    }
}

static void draw_object(const World *w, const Cam *c, int idx)
{
    static const uint8_t mdl_of[OBJ_COUNT] = {
        0, MDL_CAR, MDL_TRUCK, MDL_TRACTOR, MDL_CYCLIST, MDL_DEER, MDL_CONE,
        MDL_TYRE, MDL_AMBULANCE, MDL_GOAT, MDL_PED
    };
    int32_t z, x, y;
    int yaw, mdl;
    const uint8_t *body;

    if (idx == MAX_OBJS) {
        const Car *car = &w->car;
        z = car->z;
        x = road_cx(w, z) + (car->d >> FBITS);
        y = road_y(w, z);
        if (car->state == CS_SPLASH || car->state == CS_ROLL)
            y -= (50 - car->timer) * (car->state == CS_SPLASH ? 3 : 6);
        yaw = road_dir(w, z) + car_yaw_off(car);
        add_model(c, MDL_CAR, x, y, z >> FBITS, yaw, body_pens[BODY_PLAYER], 0);
        return;
    }
    {
        const Obj *o = &w->obj[idx];
        z = o->z;
        x = road_cx(w, z) + (o->d >> FBITS);
        y = road_y(w, z);
        yaw = road_dir(w, z);
        if (o->v < 0) yaw += 32768;
        else if (o->vd < 0 && o->v == 0) yaw -= 16384;      /* crossing to the left */
        else if (o->vd > 0 && o->v == 0) yaw += 16384;
        mdl = mdl_of[o->type];
        if (o->type == OBJ_DEBRIS && w->route == 2) mdl = MDL_ROCK;
        body = body_pens[o->colour & 3];
        if (o->flags & OF_KNOCKED) y -= 4;
        add_model(c, mdl, x, y, z >> FBITS, yaw, body, (w->frame >> 2) & 1);
    }
}

static int sun_yaw, sun_route = -1, horizon, vanish_x, ground_l, ground_r;

static void draw_segment(const World *w, const Cam *c, int i, int k)
{
    static V3 A[NPTS], B[NPTS];
    const Seg *s = &w->seg[i];
    int32_t rl = road_left(w), rr = road_right(w);
    const RouteInfo *r = &route_info[w->route];
    int flood = s->flags & SF_FLOOD, n, cur;
    int road_pen, sh_pen, lane;
    int32_t cd;

    profile(w, s);
    edge(w, c, i, A);
    edge(w, c, i + 1, B);

    side_strips(w, i, 0, A, B);
    side_strips(w, i, 1, A, B);

    if (flood) {
        road_pen = PEN_WATER + (((i >> 1) + (w->frame >> 3)) & 1);
        sh_pen = road_pen;
    } else if (s->flags & SF_OVERPASS) {
        road_pen = sh_pen = PEN_SHADOW;           /* the shadow that fools it */
    } else {
        road_pen = PEN_ROAD + ((i >> 1) & 1);
        sh_pen = PEN_ROAD + 1;
    }
    strip(A, B, 2, 3, sh_pen);
    strip(A, B, 4, 5, sh_pen);
    strip(A, B, 3, 4, road_pen);

    /* the finish line: a chequered band */
    if (s->flags & SF_FINISH) {
        int32_t x;
        for (x = rl; x < rr; x += 16)
            band(w, c, i, x, x + 8, x, x + 8, 0, ((x >> 4) + i) & 1 ? PEN_WHITE : PEN_BLACK);
    }

    if (k < LINE_SEGS && !flood) {
        int ghost = s->flags & SF_GHOST;
        int real = ghost ? PEN_ROAD + 1 : PEN_WHITE;        /* worn away where the old ones are */
        if (r->no) line(w, c, i, 0, ghost ? PEN_ROAD + 1 : PEN_YELLOW);
        else line(w, c, i, rl + 4, PEN_YELLOW);
        for (n = 1; n < r->nl; n++)
            if (!(i & 1)) line(w, c, i, (int32_t)n * LANE_W, PEN_WHITE);
        for (n = 1; n < r->no; n++)
            if (!(i & 1)) line(w, c, i, -(int32_t)n * LANE_W, PEN_WHITE);
        line(w, c, i, rr - 3, real);
        if (r->no) line(w, c, i, rl + 3, PEN_WHITE);
        if (ghost) {
            /* the old lines: where the road used to go */
            int32_t g0 = s->ghost, g1 = (w->seg[i + 1].flags & SF_GHOST) ? w->seg[i + 1].ghost : g0;
            if (!(i & 1)) {
                band(w, c, i, g0 - 2, g0 + 2, g1 - 2, g1 + 2, 0, PEN_YELLOW);
                band(w, c, i, rr - 5 + g0, rr - 1 + g0, rr - 5 + g1, rr - 1 + g1, 0, PEN_WHITE);
            }
        }
    }

    if (s->flags & SF_RAIL_L) {
        wall(w, c, i, off[2], 6, 12, PEN_GREY);
        wall(w, c, i, off[2], 0, 6, PEN_PANEL);
    }
    if (s->flags & SF_RAIL_R) {
        wall(w, c, i, off[5], 6, 12, PEN_GREY);
        wall(w, c, i, off[5], 0, 6, PEN_PANEL);
    }
    if (s->side[0] == SIDE_MEDIAN) wall(w, c, i, off[2] - 8, 0, 14, PEN_GREY);

    /* the autopilot's planned path */
    if (w->ai.on && w->state != GS_GAMEOVER) {
        int p = i - w->ai.plan_from;
        if (p >= 0 && p < 12 && (!w->ai.blinded || (w->frame & 2))) {
            int32_t a = w->ai.plan[p] >> FBITS, b = w->ai.plan[p + 1] >> FBITS;
            band(w, c, i, a - 3, a + 3, b - 3, b + 3, 1, w->ai.blinded ? PEN_WHITE : PEN_AI);
        }
    }

    /* cones: in the closed lane, and along its line */
    if (s->flags & SF_CONES) {
        int32_t z = ((int32_t)i << SEG_BITS);
        for (lane = 0; lane < 8 && !(s->closed & (1 << lane)); lane++)
            ;
        add_model(c, MDL_CONE, road_cx(w, (z + 64) * (1 << FBITS)) + (int32_t)lane * LANE_W + 4,
                  road_y(w, (z + 64) * (1 << FBITS)), z + 64, 0, NULL, 0);
        if (cone_at(w, i, &cd)) {
            int hit = w->knocked[i >> 3] & (1 << (i & 7));
            add_model(c, MDL_CONE, s->cx + cd + (hit ? 26 : 0), s->y, z + (hit ? 30 : 0), 0, NULL, 0);
        }
    }

    if (k < SCENE_SEGS) scenery(w, c, i);

    if ((s->flags & SF_OVERPASS) && !(w->seg[i - 1 >= 0 ? i - 1 : 0].flags & SF_OVERPASS)) {
        int32_t z = ((int32_t)(i + 1) << SEG_BITS);
        add_model(c, MDL_OVERPASS, road_cx(w, z * (1 << FBITS)) + (rl + rr) / 2, road_y(w, z * (1 << FBITS)), z,
                  road_dir(w, z * (1 << FBITS)), NULL, 0);
    }
    if (i == w->finish) {
        int32_t z = ((int32_t)i << SEG_BITS);
        add_model(c, MDL_GANTRY, s->cx + (rl + rr) / 2, s->y, z, road_dir(w, z * (1 << FBITS)), NULL, 0);
    }

    for (cur = seg_first[k]; cur >= 0; cur = obj_next[cur]) draw_object(w, c, cur);
    render_stats.segs++;
}

void render_build(const World *w, Cam *c)
{
    int s0, s1, i;
    V3 v;

    npoly = 0;
    render_stats.segs = render_stats.models = 0;

    if (w->route != sun_route) {
        /* the sun sits where the road heads into it */
        sun_route = w->route;
        sun_yaw = -1;
        for (i = 0; i < w->nhaz; i++)
            if (w->haz[i].type == HZ_GLARE) {
                sun_yaw = road_dir(w, ((int32_t)w->haz[i].seg + w->haz[i].len / 2) * SEGF);
                break;
            }
    }

    s0 = seg_of((int32_t)c->z * (1 << FBITS));
    if (s0 < 0) s0 = 0;
    s1 = s0 + (render_draw_dist > NVIS_MAX ? NVIS_MAX : render_draw_dist);
    if (s1 > w->nseg - 2) s1 = w->nseg - 2;
    sort_objects(w, s0, s1);
    for (i = s1; i >= s0; i--) draw_segment(w, c, i, i - s0);

    /* where the sky meets the ground */
    horizon = sc_horizon(c);
    cam_xform(c, w->seg[s1].cx + (road_left(w) + road_right(w)) / 2, c->y, (int32_t)s1 << SEG_BITS, &v);
    vanish_x = v.z > NEAR_Z ? (int)(VIEW_CX + (v.x * FOV) / v.z) : VIEW_CX;
    if (vanish_x < 0) vanish_x = 0;
    if (vanish_x > VIEW_W - 1) vanish_x = VIEW_W - 1;
    ground_l = far_pen(w->seg[s1].side[0]);
    ground_r = far_pen(w->seg[s1].side[1]);
    render_stats.polys = npoly;
}

/* ---------- the sky ---------- */
static uint8_t skyline[NUM_ROUTES][64];

static void make_skylines(void)
{
    int r, j;
    for (r = 0; r < NUM_ROUTES; r++)
        for (j = 0; j < 64; j++) {
            uint32_t h = sc_hash((uint32_t)(r * 64 + j));
            int v;
            switch (r) {
            case 0: v = 4 + (int)(h % 5); break;                        /* trees across the river */
            case 1: v = (j & 3) ? 3 + (int)(h % 4) : 8 + (int)(h % 14); break;  /* distant towers */
            case 2: v = 10 + (int)(h % 30); break;                      /* mountains */
            default: v = 6 + (int)(h % 12); break;                      /* rooftops */
            }
            skyline[r][j] = (uint8_t)v;
        }
}

static void draw_skyline(Canvas *cv, const World *w, const Cam *c)
{
    int j, r = w->route, blocky = r == 1 || r == 3;
    for (j = -1; j < 65; j++) {
        int16_t a = (int16_t)(j * 1024 - c->yaw), b = (int16_t)((j + 1) * 1024 - c->yaw);
        int16_t xs[4], ys[4];
        int x0 = VIEW_CX + (int)(((int32_t)a * FOV) / 10430);
        int x1 = VIEW_CX + (int)(((int32_t)b * FOV) / 10430);
        int h0 = skyline[r][j & 63], h1 = skyline[r][(j + 1) & 63];
        if (x1 < 0 || x0 > VIEW_W - 1 || x1 <= x0) continue;
        if (blocky) h1 = h0;
        xs[0] = (int16_t)x0; ys[0] = (int16_t)(horizon - h0);
        xs[1] = (int16_t)x1; ys[1] = (int16_t)(horizon - h1);
        xs[2] = (int16_t)x1; ys[2] = (int16_t)horizon;
        xs[3] = (int16_t)x0; ys[3] = (int16_t)horizon;
        g_poly(cv, xs, ys, 4, PEN_HAZE, 0, VIEW_H - 1);
    }
}

static void octagon(Canvas *cv, int cx, int cy, int r, int col)
{
    int16_t xs[8], ys[8];
    int h = (r * 5) / 12;
    xs[0] = (int16_t)(cx - h); ys[0] = (int16_t)(cy - r);
    xs[1] = (int16_t)(cx + h); ys[1] = (int16_t)(cy - r);
    xs[2] = (int16_t)(cx + r); ys[2] = (int16_t)(cy - h);
    xs[3] = (int16_t)(cx + r); ys[3] = (int16_t)(cy + h);
    xs[4] = (int16_t)(cx + h); ys[4] = (int16_t)(cy + r);
    xs[5] = (int16_t)(cx - h); ys[5] = (int16_t)(cy + r);
    xs[6] = (int16_t)(cx - r); ys[6] = (int16_t)(cy + h);
    xs[7] = (int16_t)(cx - r); ys[7] = (int16_t)(cy - h);
    g_poly(cv, xs, ys, 8, col, 0, VIEW_H - 1);
}

static void draw_sun(Canvas *cv, const World *w, const Cam *c)
{
    int16_t d;
    int x, r;
    if (sun_yaw < 0) return;
    d = (int16_t)(sun_yaw - c->yaw);
    if (d > 9000 || d < -9000) return;
    x = VIEW_CX + (int)(((int32_t)d * FOV) / 10430);
    r = 9 + w->glare / 2;
    octagon(cv, x, horizon - 14, r + 4, PEN_ORANGE);
    octagon(cv, x, horizon - 14, r, PEN_YELLOW);
    octagon(cv, x, horizon - 14, r / 2, PEN_WHITE);
}

/* AmigaOS 4's painted dusk all the way round (`make art`): 2 pi FOV of
 * the view's pixels across, so it turns with the road; its bottom edge on
 * the horizon */
static A3Image *panorama;
static int panorama_tried;

void render_draw(Canvas *cv, const World *w, const Cam *c)
{
    static int skylines_made;
    int hz = horizon;
    if (!skylines_made) { make_skylines(); skylines_made = 1; }
    if (!panorama_tried) { panorama = g_image_load("PROGDIR:scairy_horizon.img"); panorama_tried = 1; }
    if (hz < 0) hz = 0;
    if (hz > VIEW_H) hz = VIEW_H;
    if (panorama) {
        /* the picture's column at the view's left edge */
        int32_t a = (int32_t)(uint16_t)c->yaw - (int32_t)VIEW_CX * 10430 / FOV;
        if (hz > 0) g_image_wrap(cv, panorama, (int)((a * g_image_w(panorama)) >> 16), hz * A3_SUB - g_image_h(panorama), 0, hz * A3_SUB - 1);
    } else g_fill_blit(cv, 0, 0, VIEW_W - 1, hz - 1, PEN_SKY);
    g_fill_blit(cv, 0, hz, vanish_x, VIEW_H - 1, ground_l);
    g_fill_blit(cv, vanish_x + 1, hz, VIEW_W - 1, VIEW_H - 1, ground_r);
    g_wait();
    if (!panorama) draw_skyline(cv, w, c);
    draw_sun(cv, w, c);
    flush(cv);
}

/* ---------- overlays ---------- */
static void bracket(Canvas *cv, int x0, int y0, int x1, int y1, int col)
{
    int l = (x1 - x0) / 4;
    if (l < 2) l = 2;
    if (l > 8) l = 8;
    hline(cv, x0, x0 + l, y0, col); hline(cv, x1 - l, x1, y0, col);
    hline(cv, x0, x0 + l, y1, col); hline(cv, x1 - l, x1, y1, col);
    vline(cv, x0, y0, y0 + l, col); vline(cv, x1, y0, y0 + l, col);
    vline(cv, x0, y1 - l, y1, col); vline(cv, x1, y1 - l, y1, col);
}

static void ai_brackets(Canvas *cv, const World *w, const Cam *c)
{
    const Ai *a = &w->ai;
    int i;
    for (i = 0; i < a->nseen; i++) {
        const Seen *s = &a->seen[i];
        int len = s->phantom ? 30 : obj_len(s->type), wid = s->phantom ? 40 : obj_wid(s->type);
        int h = s->phantom ? 64 : obj_height[s->type];
        int32_t z = (s->z >> FBITS) - len, x = road_cx(w, s->z) + (s->d >> FBITS), y = road_y(w, s->z);
        int32_t x0, y0, x1, y1;
        V3 p, q;
        if (z - c->z > 2200) continue;
        cam_xform(c, x - wid, y, z, &p);
        cam_xform(c, x + wid, y + h, z, &q);
        if (p.z < NEAR_Z + 8 || q.z < NEAR_Z + 8) continue;
        sc_project(&p, &x0, &y0);
        sc_project(&q, &x1, &y1);
        if (x0 > x1) { int32_t t = x0; x0 = x1; x1 = t; }
        if (y0 < y1) { int32_t t = y0; y0 = y1; y1 = t; }
        if (x1 < 0 || x0 >= VIEW_W || y0 < 0 || y1 >= VIEW_H) continue;
        x0 -= 2; x1 += 2; y1 -= 2; y0 += 1;
        if (s->phantom) {
            int col = (w->frame & 4) ? PEN_RED : PEN_YELLOW;
            bracket(cv, (int)x0, (int)y1, (int)x1, (int)y0, col);
            if (y1 > 12) text_shadow(cv, (int)((x0 + x1) / 2) - 4, (int)y1 - 3, col, "?");
        } else {
            bracket(cv, (int)x0, (int)y1, (int)x1, (int)y0, PEN_AI2);
        }
    }
}

static void cockpit(Canvas *cv, const World *w)
{
    static const int16_t pl_x[4] = { 0, 20, 6, 0 }, pl_y[4] = { 0, 0, 178, 178 };
    static const int16_t pr_x[4] = { 300, 319, 319, 313 }, pr_y[4] = { 0, 0, 178, 178 };
    int16_t xs[8], ys[8];
    int k, ang = (int)((w->car.s * 16384) / 256);
    char tmp[24];

    g_poly(cv, pl_x, pl_y, 4, PEN_PANEL, 0, VIEW_H - 1);
    g_poly(cv, pr_x, pr_y, 4, PEN_PANEL, 0, VIEW_H - 1);
    g_rect(cv, 132, 2, 188, 12, PEN_PANEL);                 /* the mirror */
    g_rect(cv, 135, 4, 185, 10, PEN_SKY);
    g_rect(cv, 0, 178, VIEW_W - 1, VIEW_H - 1, PEN_PANEL);   /* the dashboard */
    g_rect(cv, 0, 178, VIEW_W - 1, 179, PEN_GREY);

    /* the steering wheel: a ring of eight quads, turned by the steering */
    for (k = 0; k < 8; k++) {
        int a0 = ang + k * 8192, a1 = a0 + 8192;
        xs[0] = (int16_t)(110 + ((sc_sin(a0) * 40) >> 16)); ys[0] = (int16_t)(206 - ((sc_cos(a0) * 30) >> 16));
        xs[1] = (int16_t)(110 + ((sc_sin(a1) * 40) >> 16)); ys[1] = (int16_t)(206 - ((sc_cos(a1) * 30) >> 16));
        xs[2] = (int16_t)(110 + ((sc_sin(a1) * 33) >> 16)); ys[2] = (int16_t)(206 - ((sc_cos(a1) * 24) >> 16));
        xs[3] = (int16_t)(110 + ((sc_sin(a0) * 33) >> 16)); ys[3] = (int16_t)(206 - ((sc_cos(a0) * 24) >> 16));
        g_poly(cv, xs, ys, 4, PEN_BLACK, 0, VIEW_H - 1);
    }
    for (k = 0; k < 2; k++) {                               /* the spokes */
        int a0 = ang + 16384 + k * 32768;
        xs[0] = (int16_t)(110 + ((sc_sin(a0 - 1500) * 34) >> 16)); ys[0] = (int16_t)(206 - ((sc_cos(a0 - 1500) * 25) >> 16));
        xs[1] = (int16_t)(110 + ((sc_sin(a0 + 1500) * 34) >> 16)); ys[1] = (int16_t)(206 - ((sc_cos(a0 + 1500) * 25) >> 16));
        xs[2] = (int16_t)(110 + ((sc_sin(a0 + 6000) * 7) >> 16));  ys[2] = (int16_t)(206 - ((sc_cos(a0 + 6000) * 6) >> 16));
        xs[3] = (int16_t)(110 + ((sc_sin(a0 - 6000) * 7) >> 16));  ys[3] = (int16_t)(206 - ((sc_cos(a0 - 6000) * 6) >> 16));
        g_poly(cv, xs, ys, 4, PEN_BLACK, 0, VIEW_H - 1);
    }
    /* the car's own screen: what the autopilot is doing */
    g_rect(cv, 196, 184, 300, 207, PEN_BLACK);
    if (w->ai.on) {
        sprintf(tmp, "AUTO x%ld", (long)w->mult);
        g_text(cv, 204, 194, w->ai.blinded && (w->frame & 4) ? PEN_WHITE : PEN_AI, tmp);
    } else {
        g_text(cv, 204, 194, PEN_ORANGE, "MANUAL");
    }
    sprintf(tmp, "%3ld KM/H", (long)KMH(w->car.v));
    g_text(cv, 204, 204, PEN_GREY, tmp);
}

static void panel_box(Canvas *cv, int x0, int y0, int x1, int y1)
{
    g_rect(cv, x0, y0, x1, y1, PEN_PANEL);
    g_rect(cv, x0, y0, x1, y0, PEN_AI);
    g_rect(cv, x0, y1, x1, y1, PEN_AI);
    g_rect(cv, x0, y0, x0, y1, PEN_AI);
    g_rect(cv, x1, y0, x1, y1, PEN_AI);
}

static void state_box(Canvas *cv, const World *w)
{
    const RouteInfo *r = &route_info[w->route];
    char tmp[48];
    switch (w->state) {
    case GS_INTRO:
        panel_box(cv, 40, 54, 279, 150);
        sprintf(tmp, "ROUTE %ld OF %ld", (long)w->route + 1, (long)NUM_ROUTES);
        text_centre(cv, 68, PEN_GREY, tmp);
        g_text_big(cv, (SCREEN_W - (int)strlen(r->name) * 16) / 2, 76, PEN_YELLOW, r->name, 2);
        sprintf(tmp, "TO %s", r->dest);
        text_centre(cv, 106, PEN_WHITE, tmp);
        sprintf(tmp, "AUTOPILOT v%s", r->ai_name);
        text_centre(cv, 120, PEN_AI, tmp);
#ifdef AMIGA3DO
        text_centre(cv, 140, (w->frame & 16) ? PEN_YELLOW : PEN_WHITE, "PRESS A");
#else
        text_centre(cv, 140, (w->frame & 16) ? PEN_YELLOW : PEN_WHITE, "PRESS FIRE");
#endif
        break;
    case GS_ARRIVED:
        panel_box(cv, 32, 34, 287, 166);
        sprintf(tmp, "ARRIVED: %s", r->dest);
        text_centre(cv, 50, PEN_YELLOW, tmp);
        sprintf(tmp, "AUTOPILOT USED   %3ld%%", (long)world_ap_pct(w));
        text_centre(cv, 70, PEN_AI, tmp);
        sprintf(tmp, "GOOD CATCHES     %3ld", (long)w->saves);
        text_centre(cv, 84, PEN_AI2, tmp);
        sprintf(tmp, "NEEDLESS GRABS   %3ld", (long)w->needless);
        text_centre(cv, 98, PEN_ORANGE, tmp);
        sprintf(tmp, "INSURANCE COVER  %3ld", (long)w->cover);
        text_centre(cv, 112, PEN_WHITE, tmp);
        sprintf(tmp, "BONUS %ld", (long)w->route_bonus);
        text_centre(cv, 132, PEN_YELLOW, tmp);
#ifdef AMIGA3DO
        if (w->state_timer < 210) text_centre(cv, 154, (w->frame & 16) ? PEN_YELLOW : PEN_WHITE, "PRESS A");
#else
        if (w->state_timer < 210) text_centre(cv, 154, (w->frame & 16) ? PEN_YELLOW : PEN_WHITE, "PRESS FIRE");
#endif
        break;
    case GS_GAMEOVER:
        panel_box(cv, 40, 60, 279, 140);
        g_text_big(cv, 32 + 40 - 24, 72, PEN_RED, "CANCELLED", 2);
        text_centre(cv, 100, PEN_WHITE, "YOUR INSURER HAS HAD ENOUGH");
        sprintf(tmp, "SCORE %ld", (long)w->score);
        text_centre(cv, 116, PEN_YELLOW, tmp);
#ifdef AMIGA3DO
        if (w->state_timer == 0) text_centre(cv, 132, PEN_WHITE, "PRESS A");
#else
        if (w->state_timer == 0) text_centre(cv, 132, PEN_WHITE, "PRESS FIRE");
#endif
        break;
    case GS_WIN:
        panel_box(cv, 24, 40, 295, 160);
        g_text_big(cv, (SCREEN_W - 9 * 16) / 2, 50, PEN_YELLOW, "YOU'RE HOME", 2);
        sprintf(tmp, "FINAL SCORE %ld", (long)w->score);
        text_centre(cv, 84, PEN_WHITE, tmp);
        sprintf(tmp, "GOOD CATCHES %ld  NEEDLESS %ld", (long)w->total_saves, (long)w->total_needless);
        text_centre(cv, 100, PEN_AI2, tmp);
        text_centre(cv, 120, PEN_AI, "THE AI THANKS YOU");
        text_centre(cv, 132, PEN_AI, "FOR YOUR TRAINING DATA");
#ifdef AMIGA3DO
        if (w->state_timer == 0) text_centre(cv, 152, PEN_WHITE, "PRESS A");
#else
        if (w->state_timer == 0) text_centre(cv, 152, PEN_WHITE, "PRESS FIRE");
#endif
        break;
    }
}

void render_overlay(Canvas *cv, const World *w, const Cam *c)
{
    if (w->ai.on && w->state != GS_TITLE) ai_brackets(cv, w, c);
    if (w->view == CAM_COCKPIT) cockpit(cv, w);
    if (w->msg != MSG_NONE && w->state != GS_TITLE) {
        int col = (w->msg == MSG_TICKET || w->msg == MSG_TOW || w->msg == MSG_CANCELLED) ? PEN_YELLOW
                  : w->msg == MSG_GOOD_CATCH ? PEN_AI2 : PEN_AI;
        text_centre(cv, 24, col, msg_text[w->msg]);
    }
    if (w->state == GS_PLAY && w->car.state == CS_AQUAPLANE && (w->frame & 8))
        text_centre(cv, 40, PEN_WHITE, "AQUAPLANING");
    /* stopped with the autopilot off (after the tow truck): what to do */
    if (w->state == GS_PLAY && !w->ai.on && w->car.state == CS_DRIVING && w->car.v < FROM_KMH(5) &&
        (w->frame & 16))
#ifdef AMIGA3DO
        text_centre(cv, 150, PEN_YELLOW, "A: AUTOPILOT   UP: DRIVE");
#else
        text_centre(cv, 150, PEN_YELLOW, "FIRE: AUTOPILOT   UP: DRIVE");
#endif
    state_box(cv, w);
}

/* ---------- the software update between routes ---------- */
static const char *const patch_notes[NUM_ROUTES][4] = {
    { "", "", "", "" },
    { "+ NOW DETECTS WATER", "+ IMPROVED LANE CONFIDENCE", "- REMOVED FEAR OF TRACTORS",
      "KNOWN ISSUE: SHADOWS LOOK SOLID" },
    { "+ PHANTOM BRAKING FIXED (MOSTLY)", "+ MOUNTAIN MODE", "+ SUNGLASSES (BETA)",
      "KNOWN ISSUE: SUNSETS" },
    { "+ CONFIDENCE INCREASED 400%", "+ CITY DRIVING", "- REMOVED SELF-DOUBT",
      "KNOWN ISSUE: EVERYTHING" },
};

void render_update_screen(Canvas *cv, const World *w)
{
    int next = w->route + 1 < NUM_ROUTES ? w->route + 1 : NUM_ROUTES - 1;
    int done = 200 - w->state_timer, k, fill;
    char tmp[48];

    g_fill_blit(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_BLACK);
    g_wait();
    g_text_big(cv, (SCREEN_W - 15 * 16) / 2, 24, PEN_AI, "SOFTWARE UPDATE", 2);
    sprintf(tmp, "AUTOPILOT v%s", route_info[next].ai_name);
    text_centre(cv, 64, PEN_WHITE, tmp);
    for (k = 0; k < 4; k++)
        if (done > 20 + k * 12)
            g_text(cv, 40, 92 + k * 14, k == 3 ? PEN_ORANGE : PEN_AI2, patch_notes[next][k]);
    fill = done * 2;
    if (fill > 240) fill = 240;
    g_rect(cv, 40, 160, 280, 170, PEN_PANEL);
    if (fill > 0) g_rect(cv, 40, 160, 40 + fill, 170, PEN_AI);
    if (fill >= 240) {
        text_centre(cv, 190, PEN_WHITE, "INSTALLED. I AM BETTER NOW.");
#ifdef AMIGA3DO
        if (done > 40) text_centre(cv, 220, (w->frame & 16) ? PEN_YELLOW : PEN_WHITE, "PRESS A");
#else
        if (done > 40) text_centre(cv, 220, (w->frame & 16) ? PEN_YELLOW : PEN_WHITE, "PRESS FIRE");
#endif
    } else {
        sprintf(tmp, "INSTALLING... %ld%%", (long)(fill * 100 / 240));
        text_centre(cv, 190, PEN_GREY, tmp);
    }
}

/* ---------- the rules ---------- */
static const char *const help_title[HELP_PAGES] = { "HOW TO PLAY", "CONTROLS", "READING THE AI" };
static const char *const help_text[HELP_PAGES][12] = {
    { "YOUR CAR DRIVES ITSELF. YOU SCORE",
      "FOR EVERY METRE IT DRIVES ON AUTOPILOT,",
      "x THE MULTIPLIER: +1 EVERY 8 SECONDS",
      "THE AUTOPILOT STAYS ON, UP TO x8.",
      "",
      "BUT IT HAS BLIND SPOTS. TAKE THE WHEEL",
      "JUST BEFORE IT GETS SOMETHING WRONG,",
      "GET THROUGH, THEN HAND IT BACK.",
      "",
      "GOOD CATCH: YOU TOOK OVER IN TIME +500",
      "NEEDLESS: IT HAD THAT. MULTIPLIER x1.",
      "" },
    { "LEFT / RIGHT   STEER (TAKES OVER)",
      "DOWN           BRAKE (TAKES OVER)",
      "UP             ACCELERATE (MANUAL)",
#ifdef AMIGA3DO
      "A              AUTOPILOT ON / OFF",
#else
      "FIRE / SPACE   AUTOPILOT ON / OFF",
#endif
      "               (ONLY NEAR A LANE",
      "               CENTRE)",
#ifdef AMIGA3DO
      "B              CHASE / COCKPIT VIEW",
#else
      "V              CHASE / COCKPIT VIEW",
#endif
      "P              PAUSE",
#ifdef AMIGA3DO
      "C              THESE PAGES",
#else
      "HELP / H       THESE PAGES",
#endif
#ifdef AMIGA3DO
      "L / X          TITLE / MENU",
#else
      "ESC            TITLE / QUIT",
#endif
      "",
      "" },
    { "CYAN RIBBON    WHERE IT MEANS TO GO",
      "GREEN BOXES    WHAT IT HAS SEEN",
      "RED BOX ?      SOMETHING IT IMAGINES",
      "BOTTOM RIGHT   ITS VIEW FROM ABOVE",
      "",
      "THERE ARE NO WARNINGS. WATCH THE ROAD:",
      "FLOOD WATER, OLD LANE LINES, OVERPASS",
      "SHADOWS, CONES, THE LOW SUN, SIRENS,",
      "DEBRIS. CRASHES COST INSURANCE COVER;",
      "AT ZERO YOUR POLICY IS CANCELLED.",
      "EACH DESTINATION ADDS COVER AND A",
      "BONUS FOR AUTOPILOT USE." },
};

void render_help(Canvas *cv, int page)
{
    char tmp[24];
    int k;
    if (page < 1 || page > HELP_PAGES) return;
    g_fill_blit(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_BLACK);
    g_wait();
    g_text_big(cv, (SCREEN_W - (int)strlen(help_title[page - 1]) * 16) / 2, 14, PEN_AI, help_title[page - 1], 2);
    for (k = 0; k < 12; k++)
        g_text(cv, 4, 54 + k * 14, (page == 1 && k >= 9) ? (k == 9 ? PEN_AI2 : PEN_ORANGE) : PEN_WHITE,
               help_text[page - 1][k]);
    sprintf(tmp, "PAGE %ld OF %ld", (long)page, (long)HELP_PAGES);
    g_text(cv, 4, 236, PEN_GREY, tmp);
#ifdef AMIGA3DO
    g_text(cv, 140, 236, PEN_YELLOW, "C: NEXT  A: PLAY");
#else
    g_text(cv, 140, 236, PEN_YELLOW, "HELP: NEXT  FIRE: PLAY");
#endif
}

/* ---------- the title, over the autopilot driving itself ---------- */
void render_title(Canvas *cv, const World *w)
{
    int x = (SCREEN_W - 10 * 24) / 2;
    g_text_big(cv, x + 3, 33, PEN_BLACK, "SC", 3);
    g_text_big(cv, x, 30, PEN_WHITE, "SC", 3);
    g_text_big(cv, x + 48 + 3, 33, PEN_BLACK, "AI", 3);
    g_text_big(cv, x + 48, 30, PEN_AI, "AI", 3);
    g_text_big(cv, x + 96 + 3, 33, PEN_BLACK, "RY FUN", 3);
    g_text_big(cv, x + 96, 30, PEN_WHITE, "RY FUN", 3);
    text_centre(cv, 70, PEN_YELLOW, "A SELF-DRIVING ADVENTURE");
    text_centre(cv, 150, PEN_WHITE, "LEFT/RIGHT/BRAKE TAKES OVER");
#ifdef AMIGA3DO
    text_centre(cv, 162, PEN_WHITE, "A: AUTOPILOT   B: VIEW");
#else
    text_centre(cv, 162, PEN_WHITE, "FIRE: AUTOPILOT   V: VIEW");
#endif
#ifdef AMIGA3DO
    text_centre(cv, 186, (w->frame & 16) ? PEN_YELLOW : PEN_WHITE, "PRESS A TO START");
#else
    text_centre(cv, 186, (w->frame & 16) ? PEN_YELLOW : PEN_WHITE, "PRESS FIRE TO START");
#endif
#ifdef AMIGA3DO
    text_centre(cv, 200, PEN_GREY, "C: HOW TO PLAY");
#else
    text_centre(cv, 200, PEN_GREY, "HELP OR H: HOW TO PLAY");
#endif
}

/* ---------- the HUD ---------- */
static char hud_cache[2][6][24];
static int hud_valid[2];

static void hud_line(Canvas *rp, int buf, int slot, int x, int y, int w, int col, const char *s)
{
    if (hud_valid[buf] && strcmp(hud_cache[buf][slot], s) == 0) return;
    strncpy(hud_cache[buf][slot], s, 23);
    g_rect(rp, x, y - 7, x + w, y + 1, PEN_PANEL);
    g_text(rp, x, y, col, s);
}

void render_hud_reset(void) { hud_valid[0] = hud_valid[1] = 0; }

static void bar(Canvas *rp, int x, int y, int w, int val, int max, int col)
{
    int fill = (val * w) / max;
    if (fill < 0) fill = 0;
    if (fill > w) fill = w;
    g_rect(rp, x, y, x + w, y + 4, PEN_BLACK);
    if (fill > 0) g_rect(rp, x, y, x + fill, y + 4, col);
}

/* what the autopilot thinks is around it, from above */
static void ai_display(Canvas *rp, const World *w)
{
    const int x0 = 246, y0 = VIEW_H + 3, x1 = 316, y1 = 253;
    const Ai *a = &w->ai;
    const Car *car = &w->car;
    int32_t l = road_left(w) - SHOULDER, r = road_right(w) + SHOULDER, span = r - l, cz = car->z >> FBITS;
    int i, n;
#define AX(u) (x0 + 2 + (int)((((u) - l) * (x1 - x0 - 4)) / span))
#define AY(dz) (y1 - 6 - (int)(((dz) * (y1 - y0 - 10)) / (12 * SEG_LEN)))

    g_rect(rp, x0, y0, x1, y1, PEN_BLACK);
    if (a->blinded) {
        uint32_t h = (uint32_t)w->frame * 2654435761UL;
        for (i = 0; i < 90; i++) {
            int px, py;
            h = h * 1103515245UL + 12345UL;
            px = x0 + 1 + (int)((h >> 8) % (x1 - x0 - 2));
            py = y0 + 1 + (int)((h >> 20) % (y1 - y0 - 2));
            g_span(rp, py, px, px, (h & 1) ? PEN_WHITE : PEN_GREY);
        }
        g_text(rp, x0 + 15, y0 + 22, PEN_RED, "BLIND");
        return;
    }
    for (i = y0 + 1; i < y1; i += 3) {
        g_span(rp, i, AX(road_left(w)), AX(road_left(w)), PEN_GREY);
        g_span(rp, i, AX(road_right(w)), AX(road_right(w)), PEN_GREY);
        if (route_info[w->route].no) g_span(rp, i, AX(0), AX(0), PEN_YELLOW);
        if (i % 6 == 0)
            for (n = 1; n < route_info[w->route].nl; n++)
                g_span(rp, i, AX(n * LANE_W), AX(n * LANE_W), PEN_PANEL);
    }
    if (a->on)
        for (i = 0; i < 12; i++) {
            int32_t dz = (((int32_t)(a->plan_from + i)) << SEG_BITS) - cz;
            int px = AX(a->plan[i] >> FBITS), py = AY(dz);
            if (dz < 0 || py <= y0 || px <= x0 || px >= x1) continue;
            g_span(rp, py, px, px, PEN_AI);
        }
    for (i = 0; i < a->nseen; i++) {
        const Seen *s = &a->seen[i];
        int32_t dz = (s->z >> FBITS) - cz;
        int px = AX(s->d >> FBITS), py = AY(dz);
        if (py - 2 <= y0 || py + 2 >= y1 || px - 1 <= x0 || px + 1 >= x1) continue;
        if (s->phantom) g_rect(rp, px - 4, py - 3, px + 4, py + 2, (w->frame & 4) ? PEN_RED : PEN_YELLOW);
        else g_rect(rp, px - 1, py - 2, px + 1, py + 1, PEN_AI2);
    }
    {
        int px = AX(car->d >> FBITS);
        if (px > x0 + 1 && px < x1 - 1) g_rect(rp, px - 1, y1 - 7, px + 1, y1 - 3, PEN_RED);
    }
    g_text(rp, x0 + 2, y0 + 8, a->on ? PEN_AI : PEN_PANEL, a->on ? "AI" : "OFF");
#undef AX
#undef AY
}

void render_hud(Canvas *rp, int buf, const World *w, int fps, int paused)
{
    char tmp[48];
    int32_t togo;

    if (!hud_valid[buf]) {
        g_rect(rp, 0, VIEW_H, 319, 255, PEN_PANEL);
        g_rect(rp, 0, VIEW_H, 319, VIEW_H, PEN_GREY);
        g_rect(rp, 245, VIEW_H + 2, 317, 254, PEN_GREY);
        if (w->state != GS_TITLE) g_text(rp, 108, VIEW_H + 22, PEN_GREY, "COVER");
    }
    if (w->state == GS_TITLE) {
        hud_line(rp, buf, 0, 6, VIEW_H + 12, 230, PEN_WHITE, "AN AMIGA DEVBENCH GAME");
        hud_line(rp, buf, 1, 6, VIEW_H + 26, 230, PEN_AI, "AUTOPILOT DEMONSTRATION");
        hud_line(rp, buf, 2, 6, VIEW_H + 40, 230, PEN_GREY, "TAKE OVER ONLY WHEN IT MATTERS");
        hud_valid[buf] = 1;
        ai_display(rp, w);
        if (fps > 0) { sprintf(tmp, "%ld", (long)fps); text_shadow(rp, 302, 9, PEN_GREY, tmp); }
        return;
    }
    sprintf(tmp, "SCORE %06ld", (long)w->score);
    hud_line(rp, buf, 0, 6, VIEW_H + 12, 96, PEN_WHITE, tmp);
    if (w->ai.on) sprintf(tmp, "AUTO x%ld", (long)w->mult);
    else strcpy(tmp, "MANUAL");
    hud_line(rp, buf, 1, 6, VIEW_H + 26, 96, w->ai.on ? PEN_AI : PEN_ORANGE, tmp);
    sprintf(tmp, "AP %3ld%% R%ld", (long)world_ap_pct(w), (long)w->route + 1);
    hud_line(rp, buf, 2, 6, VIEW_H + 40, 96, PEN_GREY, tmp);
    sprintf(tmp, "%3ld KM/H", (long)KMH(w->car.v));
    hud_line(rp, buf, 3, 108, VIEW_H + 12, 80, PEN_WHITE, tmp);
    togo = ((int32_t)w->finish - seg_of(w->car.z)) * 8;
    if (togo < 0) togo = 0;
    sprintf(tmp, "%ld.%ld KM", (long)(togo / 1000), (long)((togo % 1000) / 100));
    hud_line(rp, buf, 4, 108, VIEW_H + 40, 80, PEN_GREY, tmp);
    hud_valid[buf] = 1;

    bar(rp, 152, VIEW_H + 17, 84, w->cover, 100, w->cover < 30 ? PEN_RED : PEN_AI2);
    {
        /* progress along the route */
        int p = (int)((seg_of(w->car.z) * 84L) / (w->finish ? w->finish : 1));
        if (p > 84) p = 84;
        g_rect(rp, 196, VIEW_H + 36, 240, VIEW_H + 37, PEN_GREY);
        g_rect(rp, 196, VIEW_H + 34, 240, VIEW_H + 39, PEN_PANEL);
        g_rect(rp, 196, VIEW_H + 36, 240, VIEW_H + 37, PEN_GREY);
        g_rect(rp, 196 + p / 2, VIEW_H + 34, 197 + p / 2, VIEW_H + 39, PEN_RED);
    }
    ai_display(rp, w);

    if (fps > 0) {
        sprintf(tmp, "%ld", (long)fps);
        text_shadow(rp, 302, 9, PEN_GREY, tmp);
    }
    if (paused) text_centre(rp, 100, PEN_WHITE, "PAUSED");
}
