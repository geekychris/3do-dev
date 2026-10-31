// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Ace Pilot 2's picture: the sky and the ground (tilted with
 * the bank), the fields round the aeroplane in a patchwork, the river,
 * the trenches and no-man's-land, the roads and the railway, the
 * aerodromes, the hills, the sheds, dumps, guns, balloons, the train and
 * the lorries, the aeroplanes with their smoke, tracer, shell bursts,
 * explosions and clouds; then the HUD (chase view) or the cockpit, the
 * briefing with its map, the results and the help.
 *
 * The camera banks with the aeroplane: a roll of camera space about the
 * line of sight after cam_xform (as orbit_docking does it). The engine's
 * a3_model doesn't roll, so models are drawn here by model() - the same
 * steps with the roll. The camera is placed to a sixteenth of a unit
 * (cam_place), and each model drawn from its own 16.16 position
 * (cam_for), so a chase that follows a moving aeroplane doesn't judder.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "a3d_plat.h"
#include "ap.h"
#include "ace.h"

/* 0 black, 1 white, 2 panel, 3 grey, 4 sky (gradient), 5 grass, 6-9
 * fields, 10 river, 11 mud and roads, 12 dark (rails, trenches), 13-14
 * hills, 15-16 timber, 17 roofs, 18-19 the Swallow, 20-21 Grauland
 * slate, 22-23 the Falcon's silver, 24 balloon canvas, 25 smoke, 26 red,
 * 27 yellow, 28 green, 29 blue, 30 fire, 31 shell smoke */
const uint16_t ace_palette[32] = {
    0x000, 0xFFF, 0x221, 0x998, 0x69C, 0x5A3, 0x7B4, 0xBB5, 0x483, 0xA86, 0x48B, 0x864,
    0x433, 0x7A5, 0x584, 0xA97, 0x765, 0x943, 0x9A5, 0x673, 0x89A, 0x567, 0xDDE, 0x99A,
    0xDC9, 0x888, 0xE33, 0xFE4, 0x6E6, 0x5AF, 0xF93, 0x333
};
#define PEN_PANEL  2
#define PEN_GREY   3
#define PEN_SKY    4
#define PEN_GRASS  5
#define PEN_FIELD  6
#define PEN_RIVER  10
#define PEN_MUD    11
#define PEN_DARK   12
#define PEN_HILL   13
#define PEN_WOOD   15
#define PEN_ROOF   17
#define PEN_SWALLOW 18
#define PEN_SLATE  20
#define PEN_SILVER 22
#define PEN_CANVAS 24
#define PEN_SMOKE  25
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_BLUE   29
#define PEN_FIRE   30
#define PEN_FLAK   31

#define CELL     400                    /* the fields */
int32_t view_range = A3_SUB > 1 ? 3200 : 1600;
#define NEAR_LOD (A3_SUB > 1 ? 900 : 500)   /* aeroplanes nearer than this get every part */
#define FAR_LOD  (A3_SUB > 1 ? 3000 : 1600) /* beyond: a dot */

static Cam cam;
static int32_t rc = 65536, rs = 0;      /* the camera's bank */
static int32_t cfx, cfz;                /* its heading, for culling */
static Model m_plane, m_plane_lo, m_hill[AP_HILLS], m_gt[GT_COUNT], m_wreck;
static const uint8_t plane_pens[PT_COUNT][2] = {
    { PEN_SWALLOW, PEN_SWALLOW + 1 }, { PEN_CANVAS, PEN_GREY }, { PEN_SLATE, PEN_SLATE + 1 },
    { 9, PEN_DARK }, { PEN_WOOD, PEN_WOOD + 1 }, { PEN_SILVER, PEN_SILVER + 1 },
};

/* ---------- the models ---------- */
static void face4(Model *m, int lit, int sh, int a, int b, int c, int d, int ix, int iy, int iz)
{
    mdl_face(m, lit, sh, 4, a, b, c, d, ix, iy, iz);
}

/* a biplane, nose along +z, in quarter units: the fuselage tapering to
 * the tail, the cowling, two wings, the tailplane and fin, the propeller
 * (both sides of its disc, flashing) */
static void build_plane(Model *m, int full)
{
    int a, b, c, d, e, f, h, i;
    mdl_begin(m);
    a = mdl_vtx(m, -6, -5, 20); b = mdl_vtx(m, 6, -5, 20); c = mdl_vtx(m, 6, 7, 20); d = mdl_vtx(m, -6, 7, 20);
    e = mdl_vtx(m, -2, 0, -34); f = mdl_vtx(m, 2, 0, -34); h = mdl_vtx(m, 2, 4, -34); i = mdl_vtx(m, -2, 4, -34);
    face4(m, COL_BODY, COL_BODY + 1, a, b, f, e, 0, 1, -6);         /* belly */
    face4(m, COL_BODY, COL_BODY + 1, d, c, h, i, 0, 1, -6);         /* back */
    face4(m, COL_BODY, COL_BODY + 1, a, d, i, e, 0, 1, -6);         /* sides */
    face4(m, COL_BODY, COL_BODY + 1, b, c, h, f, 0, 1, -6);
    face4(m, COL_BODY, COL_BODY + 1, e, f, h, i, 0, 1, -6);         /* tail end */
    mdl_box(m, PEN_GREY, PEN_DARK, -7, 7, -6, 8, 20, 27);           /* cowling */
    mdl_box(m, COL_BODY, COL_BODY + 1, -40, 40, 17, 20, -6, 10);    /* upper wing */
    if (full) {
        mdl_box(m, COL_BODY, COL_BODY + 1, -36, 36, -7, -4, -4, 10);   /* lower wing */
        mdl_box(m, PEN_DARK, PEN_DARK, -25, -23, -4, 17, 0, 3);         /* struts */
        mdl_box(m, PEN_DARK, PEN_DARK, 23, 25, -4, 17, 0, 3);
    }
    mdl_box(m, COL_BODY, COL_BODY + 1, -15, 15, 1, 3, -34, -24);    /* tailplane */
    mdl_box(m, COL_BODY + 1, COL_BODY + 1, -1, 1, 3, 16, -34, -26); /* fin */
    if (full) {                         /* the propeller's disc, seen from either side */
        a = mdl_vtx(m, -10, -9, 29); b = mdl_vtx(m, 10, -9, 29); c = mdl_vtx(m, 10, 11, 29); d = mdl_vtx(m, -10, 11, 29);
        face4(m, COL_FLASH, COL_FLASH, a, b, c, d, 0, 1, 20);
        face4(m, COL_FLASH, COL_FLASH, a, b, c, d, 0, 1, 40);
    }
}

static void build_hill(Model *m, const Hill *h)
{
    int k, top, ring[6];
    mdl_begin(m);
    top = mdl_vtx(m, 0, h->h, 0);
    for (k = 0; k < 6; k++) {
        int a = k * 65536 / 6 + 3000;
        ring[k] = mdl_vtx(m, (int)((a3_sin(a) * h->r) >> 16), 0, (int)((a3_cos(a) * h->r) >> 16));
    }
    for (k = 0; k < 6; k++) mdl_face(m, PEN_HILL, PEN_HILL + 1, 3, ring[k], ring[(k + 1) % 6], top, 0, 0, h->h / 3, 0);
}

static void build_ground_models(void)
{
    Model *m;
    int a, b, c, d, e, f;
    m = &m_gt[GT_HANGAR];                       /* timber walls, a pitched roof */
    mdl_begin(m);
    mdl_box(m, PEN_WOOD, PEN_WOOD + 1, -30, 30, 0, 20, -24, 24);
    a = mdl_vtx(m, -31, 20, -26); b = mdl_vtx(m, 31, 20, -26); c = mdl_vtx(m, 31, 34, 0); d = mdl_vtx(m, -31, 34, 0);
    e = mdl_vtx(m, -31, 20, 26); f = mdl_vtx(m, 31, 20, 26);
    face4(m, PEN_ROOF, PEN_DARK, a, b, c, d, 0, 22, 0);
    face4(m, PEN_ROOF, PEN_DARK, e, f, c, d, 0, 22, 0);
    mdl_face(m, PEN_WOOD, PEN_WOOD + 1, 3, a, e, d, 0, 0, 22, 0);
    mdl_face(m, PEN_WOOD, PEN_WOOD + 1, 3, b, f, c, 0, 0, 22, 0);
    m = &m_gt[GT_DEPOT];                        /* crates under a tarpaulin */
    mdl_begin(m);
    mdl_box(m, PEN_WOOD, PEN_WOOD + 1, -16, 16, 0, 10, -12, 12);
    mdl_box(m, 9, PEN_DARK, -12, 12, 10, 18, -9, 9);
    m = &m_gt[GT_AA];                           /* sandbags round a gun pointing up */
    mdl_begin(m);
    mdl_box(m, 9, PEN_MUD, -8, 8, 0, 4, -8, 8);
    mdl_box(m, PEN_GREY, PEN_DARK, -3, 3, 4, 8, -3, 3);
    mdl_box(m, PEN_DARK, PEN_DARK, -1, 1, 8, 20, 0, 2);
    m = &m_gt[GT_BALLOON];                      /* a sausage, nose and tail, its fins */
    mdl_begin(m);
    a = mdl_vtx(m, 0, 0, 26); b = mdl_vtx(m, 0, 0, -24);
    c = mdl_vtx(m, 12, 0, 2); d = mdl_vtx(m, 0, 12, 2); e = mdl_vtx(m, -12, 0, 2); f = mdl_vtx(m, 0, -12, 2);
    mdl_face(m, PEN_CANVAS, PEN_GREY, 3, a, c, d, 0, 0, 0, 2); mdl_face(m, PEN_CANVAS, PEN_GREY, 3, a, d, e, 0, 0, 0, 2);
    mdl_face(m, PEN_CANVAS, PEN_GREY, 3, a, e, f, 0, 0, 0, 2); mdl_face(m, PEN_CANVAS, PEN_GREY, 3, a, f, c, 0, 0, 0, 2);
    mdl_face(m, PEN_CANVAS, PEN_GREY, 3, b, c, d, 0, 0, 0, 2); mdl_face(m, PEN_CANVAS, PEN_GREY, 3, b, d, e, 0, 0, 0, 2);
    mdl_face(m, PEN_CANVAS, PEN_GREY, 3, b, e, f, 0, 0, 0, 2); mdl_face(m, PEN_CANVAS, PEN_GREY, 3, b, f, c, 0, 0, 0, 2);
    mdl_box(m, PEN_GREY, PEN_GREY, -1, 1, 0, 14, -24, -14);
    m = &m_gt[GT_LOCO];                         /* boiler, cab, chimney */
    mdl_begin(m);
    mdl_box(m, PEN_FLAK, 0, -6, 6, 0, 10, -8, 14);
    mdl_box(m, PEN_FLAK, 0, -6, 6, 0, 16, -16, -8);
    mdl_box(m, PEN_RED, PEN_DARK, -2, 2, 10, 16, 8, 12);
    m = &m_gt[GT_WAGON];
    mdl_begin(m);
    mdl_box(m, PEN_WOOD + 1, PEN_DARK, -6, 6, 1, 12, -16, 16);
    m = &m_gt[GT_TRUCK];                        /* the load under canvas, the cab */
    mdl_begin(m);
    mdl_box(m, PEN_SLATE, PEN_SLATE + 1, -4, 4, 1, 8, -9, 2);
    mdl_box(m, PEN_SLATE + 1, PEN_DARK, -4, 4, 1, 6, 2, 7);
    mdl_begin(&m_wreck);                        /* what's left: a blackened heap */
    mdl_pyramid(&m_wreck, PEN_FLAK, 0, -14, 14, 0, -12, 12, 6);
}

void render_init(void)
{
    int k;
    build_plane(&m_plane, 1);
    build_plane(&m_plane_lo, 0);
    for (k = 0; k < AP_HILLS; k++) build_hill(&m_hill[k], &ap_hills[k]);
    build_ground_models();
}

/* ---------- the camera, with its bank ---------- */
static void xf(const Cam *c, int32_t x, int32_t y, int32_t z, V3 *o)
{
    cam_xform(c, x, y, z, o);
    if (rs) {
        int32_t a = o->x, b = o->y;
        o->x = (a * (rc >> 4) - b * (rs >> 4)) >> 12;
        o->y = (a * (rs >> 4) + b * (rc >> 4)) >> 12;
    }
}

static void quad(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                 int32_t x2, int32_t y2, int32_t z2, int32_t x3, int32_t y3, int32_t z3, int32_t bias, int col)
{
    V3 a, b, c, d;
    int32_t nz = CAM_NEAR(&cam);
    xf(&cam, x0, y0, z0, &a); xf(&cam, x1, y1, z1, &b);
    xf(&cam, x2, y2, z2, &c); xf(&cam, x3, y3, z3, &d);
    if (a.z < nz && b.z < nz && c.z < nz && d.z < nz) return;
    a3_quad(&cam, ((a.z + b.z + c.z + d.z) >> 2) + bias * CAM_U(&cam), col, a, b, c, d);
}

static void flat(int32_t x0, int32_t x1, int32_t z0, int32_t z1, int32_t y, int32_t bias, int col)
{
    quad(x0, y, z0, x1, y, z0, x1, y, z1, x0, y, z1, bias, col);
}

/* is a point within range and not behind the camera? */
static int ahead(int32_t x, int32_t z, int32_t range, int32_t slack)
{
    int32_t dx = x - cam.x, dz = z - cam.z;
    if (dx > range || dx < -range || dz > range || dz < -range) return 0;
    return ((dx * (cfx >> 4) + dz * (cfz >> 4)) >> 12) > -slack;
}

/* a square facing the camera: r world units, at least a pixel across */
static void spot(int32_t x, int32_t y, int32_t z, int32_t r, int col, int32_t bias)
{
    V3 c, p[4];
    int32_t m;
    xf(&cam, x, y, z, &c);
    r *= CAM_U(&cam);
    if (c.z < CAM_NEAR(&cam) + r) return;
    m = c.z / (cam.fov * A3_SUB) + 1;   /* half a fine pixel or more */
    if (r < m) r = m;
    p[0].x = c.x - r; p[0].y = c.y - r; p[0].z = c.z;
    p[1].x = c.x + r; p[1].y = c.y - r; p[1].z = c.z;
    p[2].x = c.x + r; p[2].y = c.y + r; p[2].z = c.z;
    p[3].x = c.x - r; p[3].y = c.y + r; p[3].z = c.z;
    a3_poly(&cam, c.z + bias * CAM_U(&cam), col, p, 4);
}

static void burst(int32_t x, int32_t y, int32_t z, int32_t r, int col, int t)
{
    V3 c, p[8];
    int k;
    xf(&cam, x, y, z, &c);
    r *= CAM_U(&cam);
    if (c.z < CAM_NEAR(&cam) + r) return;
    for (k = 0; k < 8; k++) {
        int a = k * 8192 + t * 1500;
        int32_t rr = (k & 1) ? r / 2 : r;
        p[k].x = c.x + ((a3_sin(a) * rr) >> 16);
        p[k].y = c.y + ((a3_cos(a) * rr) >> 16);
        p[k].z = c.z;
    }
    a3_poly(&cam, c.z - 8 * CAM_U(&cam), col, p, 8);
}

/* a model at a 16.16 position, turned by rot (NULL: as built; rot may
 * scale it too), banked with the camera, its faces sorted far to near */
static void model(const Model *m, fix x, fix y, fix z, const int32_t *rot, const uint8_t *body, int flash, int32_t bias)
{
    V3 cv[MDL_MAXV], ctr;
    int32_t vis_depth[MDL_MAXF], depth, wx = x >> 16, wy = y >> 16, wz = z >> 16, mg;
    uint8_t vis_face[MDL_MAXF];
    int i, k, nvis = 0;
    Cam c;
    cam_for(&cam, x, y, z, &c);
    xf(&c, wx, wy, wz, &ctr);
    mg = 800 * CAM_U(&c);
    if (ctr.z < -mg || ctr.x > ctr.z + mg || ctr.x < -ctr.z - mg || ctr.y > ctr.z + mg || ctr.y < -ctr.z - mg) return;
    for (i = 0; i < m->nv; i++) {
        int32_t vx = m->v[i][0], vy = m->v[i][1], vz = m->v[i][2];
        if (rot) {
            int32_t rx = (vx * rot[0] + vy * rot[1] + vz * rot[2]) >> 16;
            int32_t ry = (vx * rot[3] + vy * rot[4] + vz * rot[5]) >> 16;
            int32_t rz = (vx * rot[6] + vy * rot[7] + vz * rot[8]) >> 16;
            vx = rx; vy = ry; vz = rz;
        }
        xf(&c, wx + vx, wy + vy, wz + vz, &cv[i]);
    }
    for (k = 0; k < m->nf; k++) {
        const Face *f = &m->f[k];
        const V3 *a = &cv[f->idx[0]], *b = &cv[f->idx[1]], *d = &cv[f->idx[2]];
        int32_t ux = b->x - a->x, uy = b->y - a->y, uz = b->z - a->z;
        int32_t vx = d->x - a->x, vy = d->y - a->y, vz = d->z - a->z;
        int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
#ifdef AMIGA3DO
        if (a3_dot3_nonneg(nx, a->x, ny, a->y, nz, a->z)) continue;
#else
        if ((int64_t)nx * a->x + (int64_t)ny * a->y + (int64_t)nz * a->z >= 0) continue;
#endif
        depth = a->z + b->z + d->z + (f->n == 4 ? cv[f->idx[3]].z : d->z);
        for (i = nvis; i > 0 && vis_depth[i - 1] < depth; i--) {
            vis_face[i] = vis_face[i - 1];
            vis_depth[i] = vis_depth[i - 1];
        }
        vis_face[i] = (uint8_t)k;
        vis_depth[i] = depth;
        nvis++;
    }
    depth = ctr.z - bias * CAM_U(&c);
    for (i = 0; i < nvis; i++) {
        const Face *f = &m->f[vis_face[i]];
        V3 q[4];
        int col = f->col;
        q[0] = cv[f->idx[0]]; q[1] = cv[f->idx[1]]; q[2] = cv[f->idx[2]];
        if (f->n == 4) q[3] = cv[f->idx[3]];
        if (col >= COL_FLASH) col = flash;
        else if (col >= COL_BODY) col = body ? body[col - COL_BODY] : PEN_GREY;
        a3_poly(&c, depth, col, q, f->n);
    }
}

/* ---------- the ground ---------- */
static int32_t floor_div(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

/* the earth below the horizon, tilted with the bank */
static void earth(Canvas *cv)
{
    int32_t h0 = cam_horizon(&cam) - cam.cyc, u[4], v[4], xs[MAX_PV], ys[MAX_PV];
    int16_t px[MAX_PV], py[MAX_PV];
    int k, n;
    if (h0 > 3000) return;
    if (h0 < -3000) h0 = -3000;
    u[0] = -4000; v[0] = h0; u[1] = 4000; v[1] = h0; u[2] = 4000; v[2] = h0 + 6000; u[3] = -4000; v[3] = h0 + 6000;
    for (k = 0; k < 4; k++) {
        int32_t U = (u[k] * (rc >> 4) + v[k] * (rs >> 4)) >> 12, V = (-u[k] * (rs >> 4) + v[k] * (rc >> 4)) >> 12;
        xs[k] = (cam.cx + U) * A3_SUB;
        ys[k] = (cam.cyc + V) * A3_SUB;
    }
    n = clip_rect(xs, ys, 4, 0, 0, PHYS_W - 1, PHYS_H - 1);
    if (n < 3) return;
    for (k = 0; k < n; k++) { px[k] = (int16_t)xs[k]; py[k] = (int16_t)ys[k]; }
    g_poly_sub(cv, px, py, n, PEN_GRASS);
}

/* the patchwork of fields round the camera, hedges of grass between */
static void fields(void)
{
    int32_t R = view_range;
    int i0 = (int)floor_div(cam.x - R, CELL), i1 = (int)floor_div(cam.x + R, CELL);
    int j0 = (int)floor_div(cam.z - R, CELL), j1 = (int)floor_div(cam.z + R, CELL), i, j;
    for (j = j0; j <= j1; j++)
        for (i = i0; i <= i1; i++) {
            int32_t x0 = i * CELL, z0 = j * CELL, dx = x0 + CELL / 2 - cam.x, dz = z0 + CELL / 2 - cam.z;
            uint32_t h = a3_hash((uint32_t)(i * 7919 + j * 104729 + 77));
            if (h % 7 < 2) continue;    /* meadow: the grass itself */
            if (dx * dx + dz * dz > R * R) continue;
            if (!ahead(x0 + CELL / 2, z0 + CELL / 2, R, CELL)) continue;
            flat(x0 + 14, x0 + CELL - 14, z0 + 14, z0 + CELL - 14, 0, 0, PEN_FIELD + (int)((h >> 8) & 3));
        }
}

static void ribbon_seg(int32_t x0, int32_t za, int32_t x1, int32_t zb, int32_t w, int col, int32_t bias)
{
    quad(x0, 0, za - w, x1, 0, zb - w, x1, 0, zb + w, x0, 0, za + w, bias, col);
}

/* the river, the railway, no-man's-land and the trenches: east-west
 * lines, drawn in pieces near the camera */
static void lines(const APGame *g)
{
    int32_t R = view_range, x, x0 = floor_div(cam.x - R, 200) * 200, x1 = cam.x + R;
    int k;
    for (x = x0; x < x1; x += 200) {
        int32_t zr = ap_river_z(x), zr1 = ap_river_z(x + 200), zl = ap_rail_z(x), zl1 = ap_rail_z(x + 200);
        if (ahead(x + 100, zr, R, 300)) ribbon_seg(x, zr, x + 200, zr1, 40, PEN_RIVER, 30);
        if (ahead(x + 100, zl, R, 300)) ribbon_seg(x, zl, x + 200, zl1, 5, PEN_DARK, 20);
        if (ahead(x + 100, 0, R, 300)) ribbon_seg(x, 0, x + 200, 0, 55, PEN_MUD, 40);
    }
    /* the trenches' zigzags, close by */
    if (cam.z > -1500 && cam.z < 1500) {
        int32_t r2 = R < 800 ? R : 800;
        for (x = floor_div(cam.x - r2, 80) * 80; x < cam.x + r2; x += 80) {
            int32_t a = ap_trench_z(x), b = ap_trench_z(x + 80);
            if (!ahead(x + 40, 0, r2, 200)) continue;
            ribbon_seg(x, a - 70, x + 80, b - 70, 4, PEN_DARK, 10);
            ribbon_seg(x, a + 70, x + 80, b + 70, 4, PEN_DARK, 10);
        }
    }
    /* the road from Orme to the front */
    for (k = 0; k < 10; k++) {
        fix ax, az, bx, bz;
        ap_road_pos(AP_ROAD_LEN / 10 * k, &ax, &az);
        ap_road_pos(AP_ROAD_LEN / 10 * (k + 1), &bx, &bz);
        if (!ahead(((ax + bx) >> 17), ((az + bz) >> 17), R, 400)) continue;
        quad((ax >> 16) - 7, 0, az >> 16, (ax >> 16) + 7, 0, az >> 16, (bx >> 16) + 7, 0, bz >> 16, (bx >> 16) - 7, 0, bz >> 16, 20, PEN_MUD);
    }
    /* the aerodromes' strips, the depot's yard */
    if (ahead(AP_HOME_X, AP_HOME_Z, R + AP_RUNWAY_L, 600)) {
        flat(AP_HOME_X - AP_RUNWAY_W, AP_HOME_X + AP_RUNWAY_W, AP_HOME_Z - AP_RUNWAY_L, AP_HOME_Z + AP_RUNWAY_L, 0, 10, PEN_FIELD);
        flat(AP_HOME_X - 8, AP_HOME_X + 8, AP_HOME_Z + AP_RUNWAY_L - 30, AP_HOME_Z + AP_RUNWAY_L - 10, 0, 0, 1);
        flat(AP_HOME_X - 8, AP_HOME_X + 8, AP_HOME_Z - AP_RUNWAY_L + 10, AP_HOME_Z - AP_RUNWAY_L + 30, 0, 0, 1);
    }
    if (ahead(AP_ENEMY_X, AP_ENEMY_Z, R + 400, 600))
        flat(AP_ENEMY_X - 300, AP_ENEMY_X + 300, AP_ENEMY_Z - 60, AP_ENEMY_Z + 30, 0, 10, PEN_FIELD);
    if (ahead(AP_DEPOT_X, AP_DEPOT_Z, R + 300, 600))
        flat(AP_DEPOT_X - 200, AP_DEPOT_X + 200, AP_DEPOT_Z - 120, AP_DEPOT_Z + 120, 0, 10, PEN_MUD);
    /* your shadow, low down */
    {
        const Plane *p = &g->p[0];
        int32_t gy = ap_ground_y(p->x, p->z) >> 16, alt = (p->y >> 16) - gy, px = p->x >> 16, pz = p->z >> 16;
        if (p->state != PS_DEAD && alt < 400 && gy == 0) flat(px - 9, px + 9, pz - 9, pz + 9, 0, -10, PEN_DARK);
    }
}

/* ---------- things ---------- */
static void hills(void)
{
    int k;
    for (k = 0; k < AP_HILLS; k++) {
        const Hill *h = &ap_hills[k];
        if (!ahead(h->x, h->z, view_range * 2 + 600, h->r + 200)) continue;
        model(&m_hill[k], FROMI(h->x), 0, FROMI(h->z), NULL, NULL, 0, 0);
    }
}

static void gmodel(int type, fix x, fix y, fix z, int yaw)
{
    int32_t r[9];
    a3_rot_yaw(r, yaw);
    model(&m_gt[type], x, y, z, r, NULL, 0, 0);
}

/* the scenery that isn't a target this time: our aerodrome and reserve
 * depot, and theirs unless they are the mission */
static void scenery(const APGame *g)
{
    static const int16_t home[][3] = { { 170, -220, GT_HANGAR }, { 170, -80, GT_HANGAR }, { 170, 60, GT_DEPOT } };
    static const int16_t reserve[][2] = { { -300, -1200 }, { -240, -1160 }, { -360, -1150 }, { -300, -1100 } };
    int k;
    int32_t R = view_range + 200;
    for (k = 0; k < 3; k++)
        if (ahead(AP_HOME_X + home[k][0], AP_HOME_Z + home[k][1], R, 100))
            gmodel(home[k][2], FROMI(AP_HOME_X + home[k][0]), 0, FROMI(AP_HOME_Z + home[k][1]), 16384);
    for (k = 0; k < 4; k++)
        if (ahead(reserve[k][0], reserve[k][1], R, 100)) gmodel(GT_DEPOT, FROMI(reserve[k][0]), 0, FROMI(reserve[k][1]), 0);
    if (g->mission != 7)
        for (k = 0; k < 3; k++)
            if (ahead(AP_ENEMY_X - 130 + k * 130, AP_ENEMY_Z + 90, R, 100))
                gmodel(GT_HANGAR, FROMI(AP_ENEMY_X - 130 + k * 130), 0, FROMI(AP_ENEMY_Z + 90), 0);
    if (g->mission != 6)
        for (k = 0; k < 2; k++)
            if (ahead(AP_DEPOT_X - 60 + k * 120, AP_DEPOT_Z, R, 100))
                gmodel(GT_DEPOT, FROMI(AP_DEPOT_X - 60 + k * 120), 0, FROMI(AP_DEPOT_Z), 0);
}

static void ground_things(const APGame *g)
{
    int k;
    for (k = 0; k < g->ngr; k++) {
        const Ground *o = &g->gr[k];
        int32_t x = o->x >> 16, z = o->z >> 16;
        if (!ahead(x, z, view_range + 200, 100)) continue;
        if (!o->alive) {
            if (o->type != GT_BALLOON) model(&m_wreck, o->x, 0, o->z, NULL, NULL, 0, 0);
            continue;
        }
        gmodel(o->type, o->x, o->y, o->z, o->yaw);
        if (o->type == GT_BALLOON && ahead(x, z, 1200, 100))   /* its cable */
            quad(x - 1, 0, z, x + 1, 0, z, x + 1, (o->y >> 16) - 10, z, x - 1, (o->y >> 16) - 10, z, 0, PEN_DARK);
    }
}

static void planes(const APGame *g)
{
    int k;
    for (k = 0; k < g->np; k++) {
        const Plane *p = &g->p[k];
        int32_t dx, dy, dz, d, r[9];
        int j;
        if (p->state == PS_DEAD) continue;
        if (k == 0 && ac.view == VIEW_COCKPIT && ac.state != AS_TITLE) continue;
        dx = (p->x >> 16) - cam.x; dy = (p->y >> 16) - cam.y; dz = (p->z >> 16) - cam.z;
        if (dx > 8000 || dx < -8000 || dz > 8000 || dz < -8000) continue;
        d = a3_hypot(dx, dz);
        d = a3_hypot(d, dy);
        if (d > FAR_LOD) {
            if (d < view_range * 3) spot(p->x >> 16, p->y >> 16, p->z >> 16, 4, plane_pens[p->type][1], 0);
            continue;
        }
        a3_rot_ypr(r, p->yaw, -p->pitch, -p->roll);
        for (j = 0; j < 9; j++) r[j] >>= 2;         /* the model is in quarter units */
        model(d < NEAR_LOD ? &m_plane : &m_plane_lo, p->x, p->y, p->z, r, plane_pens[p->type],
              (g->tick & 1) ? PEN_GREY : PEN_SMOKE, 0);
        if (p->hit_t < 3 && d < 1200) spot(p->x >> 16, (p->y >> 16) + 2, p->z >> 16, 3, PEN_FIRE, -10);
    }
}

static void bullets(const APGame *g)
{
    int k;
    for (k = 0; k < AP_MAX_BULLETS; k++) {
        const Bullet *b = &g->b[k];
        int32_t dx = (b->x >> 16) - cam.x, dy = (b->y >> 16) - cam.y, dz = (b->z >> 16) - cam.z;
        if (!b->alive || (dx < 30 && dx > -30 && dy < 30 && dy > -30 && dz < 30 && dz > -30)) continue;   /* past your ear */
        spot(b->x >> 16, b->y >> 16, b->z >> 16, 1, g->p[b->owner].side == SIDE_HOME ? PEN_YELLOW : PEN_FIRE, -2);
    }
    for (k = 0; k < AP_MAX_FLAK; k++) {
        const Flak *f = &g->fk[k];
        int age = f->t - f->fuse;
        if (!f->alive || age < 0) continue;
        if (age < 3) burst(f->x >> 16, f->y >> 16, f->z >> 16, 14, PEN_FIRE, age);
        else spot(f->x >> 16, f->y >> 16, f->z >> 16, 8 + age / 2, PEN_FLAK, 0);
    }
    for (k = 0; k < MAX_PUFFS; k++) {
        const Puff *p = &ac.puff[k];
        int32_t dx = p->x - cam.x, dy = p->y - cam.y, dz = p->z - cam.z;
        if (p->t <= 0) continue;
        if (dx < 40 && dx > -40 && dy < 40 && dy > -40 && dz < 40 && dz > -40) continue;   /* flying through it */
        spot(p->x, p->y + (40 - p->t) / 3, p->z, p->size + (40 - p->t) / 8, PEN_SMOKE, 0);
    }
    for (k = 0; k < MAX_BLASTS; k++) {
        const Blast *b = &ac.blast[k];
        if (b->t > 0) burst(b->x, b->y + (12 - b->t) * 2, b->z, b->size * (4 + b->t) / 14,
                            b->t > 8 ? PEN_YELLOW : b->t > 4 ? PEN_FIRE : PEN_RED, b->t);
    }
}

/* the patrol marker: a flare's smoke going up; and the clouds */
static void markers(const APGame *g)
{
    if (g->phase == PH_OUT && g->wp_next < g->nwp) {
        const Waypoint *w = &g->wp[g->wp_next];
        int32_t x = w->x >> 16, z = w->z >> 16, top = w->alt + 80, s = 6 + (g->tick & 4);
        int col = (g->tick & 8) ? PEN_GREEN : PEN_YELLOW;
        quad(x - s, 0, z, x + s, 0, z, x + s, top, z, x - s, top, z, 0, col);
        quad(x, 0, z - s, x, 0, z + s, x, top, z + s, x, top, z - s, 0, col);
    }
    {
        int32_t R = view_range * 2, i0 = floor_div(cam.x - R, 1500), i1 = floor_div(cam.x + R, 1500);
        int32_t j0 = floor_div(cam.z - R, 1500), j1 = floor_div(cam.z + R, 1500), i, j;
        for (j = j0; j <= j1; j++)
            for (i = i0; i <= i1; i++) {
                uint32_t h = a3_hash((uint32_t)(i * 31 + j * 1009 + 5));
                int32_t cx = i * 1500 + (int32_t)(h % 900), cz = j * 1500 + (int32_t)((h >> 10) % 900);
                int32_t w = 150 + (int32_t)((h >> 20) % 200), y = 900 + (int32_t)((h >> 4) % 200);
                if (h % 3 == 0 || !ahead(cx, cz, R, w + 200)) continue;
                flat(cx - w, cx + w, cz - w / 2, cz + w / 2, y, 0, 1);
            }
    }
}

/* ---------- the view ---------- */
static void set_camera(const APGame *g)
{
    const Plane *p = &g->p[0];
    int roll, pitch, yaw;
    fix x, y, z;
    if (ac.view == VIEW_COCKPIT && ac.state != AS_TITLE && p->state != PS_DEAD) {
        yaw = p->yaw; pitch = -p->pitch; roll = p->roll;
        x = p->x + 4 * a3_sin(p->yaw); z = p->z + 4 * a3_cos(p->yaw); y = p->y + FX(3);
        cam_view(&cam, SCREEN_W, SCREEN_H, SCREEN_W / 2, 112, 190);
    } else {
        /* behind and a little above, swinging round after the plane */
        int32_t back = 64, cp;
        yaw = ac.cam_yaw;
        pitch = -p->pitch * 3 / 4 + 700;
        roll = p->roll / 2;
        cp = a3_cos(p->pitch);
        x = p->x - back * FMUL(cp, a3_sin(yaw));
        z = p->z - back * FMUL(cp, a3_cos(yaw));
        y = p->y - back * a3_sin(p->pitch) + FX(16);
        if (p->state == PS_DEAD) y += FX(40);
        {
            fix gy = ap_ground_y(x, z) + FX(6);
            if (y < gy) y = gy;
        }
        cam_view(&cam, SCREEN_W, SCREEN_H, SCREEN_W / 2, 120, 200);
    }
    cam_place(&cam, x, y, z);
    cam.yaw = yaw & 0xFFFF;
    cam.pitch = pitch;
    cam_setup(&cam);
    rc = a3_cos(roll); rs = a3_sin(roll);
    if (roll == 0) rs = 0;
    cfx = a3_sin(cam.yaw); cfz = a3_cos(cam.yaw);
}

static void scene(Canvas *cv, const APGame *g)
{
    set_camera(g);
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_SKY);
    g_sync();
    earth(cv);
    a3_begin(view_range + 400);
    fields();
    a3_flush(cv);
    a3_begin(view_range + 400);
    lines(g);
    a3_flush(cv);
    a3_begin(view_range * 3);
    hills();
    scenery(g);
    ground_things(g);
    markers(g);
    planes(g);
    bullets(g);
    a3_flush(cv);
}

/* ---------- the HUD ---------- */
static void bar(Canvas *cv, int x, int y, int w, int v, int max, int col)
{
    int f = max > 0 ? v * (w - 2) / max : 0;
    if (f < 0) f = 0;
    if (f > w - 2) f = w - 2;
    g_box(cv, x, y, x + w, y + 6, 0, PEN_GREY);
    if (f > 0) g_rect(cv, x + 1, y + 1, x + f, y + 5, col);
}

static void radar(Canvas *cv, const APGame *g, int cx, int cy, int R)
{
    const Plane *p = &g->p[0];
    int32_t c = a3_cos(p->yaw), s = a3_sin(p->yaw);
    int k;
    fix ox, oz;
    g_circle(cv, cx, cy, R + 1, PEN_GREY);
    g_circle(cv, cx, cy, R, PEN_PANEL);
#define BLIP(wx, wz, col, sz) do { \
        int32_t dx = ((wx) - p->x) >> 16, dz = ((wz) - p->z) >> 16; \
        int32_t rx = (dx * (c >> 4) - dz * (s >> 4)) >> 12, rz = (dx * (s >> 4) + dz * (c >> 4)) >> 12; \
        int bx = cx + (int)(rx / 70), by = cy - (int)(rz / 70); \
        if ((bx - cx) * (bx - cx) + (by - cy) * (by - cy) < R * R) g_rect(cv, bx - (sz), by - (sz), bx + (sz), by + (sz), col); \
    } while (0)
    BLIP(FROMI(AP_HOME_X), FROMI(AP_HOME_Z), PEN_GREEN, 1);
    for (k = 0; k < g->ngr; k++) if (g->gr[k].alive && g->gr[k].objective) BLIP(g->gr[k].x, g->gr[k].z, PEN_YELLOW, 0);
    if (ap_objective_pos(g, &ox, &oz)) BLIP(ox, oz, (g->tick & 4) ? PEN_YELLOW : 1, 1);
    for (k = 1; k < g->np; k++)
        if (g->p[k].state == PS_FLY) BLIP(g->p[k].x, g->p[k].z, g->p[k].side == SIDE_HOME ? PEN_BLUE : PEN_RED, 1);
#undef BLIP
    g_rect(cv, cx, cy - 2, cx, cy + 1, 1);
}

/* where the guns point: a cross 300 units off the nose */
static void gunsight(Canvas *cv, const APGame *g)
{
    const Plane *p = &g->p[0];
    int32_t cp = a3_cos(p->pitch), sx, sy;
    V3 v;
    if (p->state != PS_FLY) return;
    xf(&cam, (p->x >> 16) + ((300 * FMUL(cp, a3_sin(p->yaw))) >> 16), (p->y >> 16) + ((300 * a3_sin(p->pitch)) >> 16),
       (p->z >> 16) + ((300 * FMUL(cp, a3_cos(p->yaw))) >> 16), &v);
    if (v.z < CAM_NEAR(&cam)) return;
    cam_project(&cam, &v, &sx, &sy);
    if (sx < 8 || sx > SCREEN_W - 8 || sy < 8 || sy > SCREEN_H - 30) return;
    g_rect(cv, (int)sx - 6, (int)sy, (int)sx - 3, (int)sy, PEN_GREEN);
    g_rect(cv, (int)sx + 3, (int)sy, (int)sx + 6, (int)sy, PEN_GREEN);
    g_rect(cv, (int)sx, (int)sy - 6, (int)sx, (int)sy - 3, PEN_GREEN);
    g_rect(cv, (int)sx, (int)sy + 3, (int)sx, (int)sy + 6, PEN_GREEN);
}

/* an arrow to the objective, turned with the view */
static void pointer(Canvas *cv, const APGame *g, int cx, int cy)
{
    fix ox, oz;
    int rel;
    int16_t xs[3], ys[3];
    char tmp[16];
    if (!ap_objective_pos(g, &ox, &oz)) return;
    rel = ap_angle_to(ox - g->p[0].x, oz - g->p[0].z) - cam.yaw;
    xs[0] = (int16_t)(cx + ((a3_sin(rel) * 12) >> 16)); ys[0] = (int16_t)(cy - ((a3_cos(rel) * 12) >> 16));
    xs[1] = (int16_t)(cx + ((a3_sin(rel + 26000) * 8) >> 16)); ys[1] = (int16_t)(cy - ((a3_cos(rel + 26000) * 8) >> 16));
    xs[2] = (int16_t)(cx + ((a3_sin(rel - 26000) * 8) >> 16)); ys[2] = (int16_t)(cy - ((a3_cos(rel - 26000) * 8) >> 16));
    g_poly(cv, xs, ys, 3, g->phase == PH_HOME ? PEN_GREEN : PEN_YELLOW, 0, SCREEN_H - 1);
    sprintf(tmp, "%ld", (long)(fhypot(ox - g->p[0].x, oz - g->p[0].z) >> 16));
    g_text_shadow(cv, cx - (int)strlen(tmp) * 4, cy + 22, 1, tmp);
}

static void warnings(Canvas *cv, const APGame *g)
{
    const Plane *p = &g->p[0];
    if (p->stalled && (g->tick & 8)) g_text_centre(cv, 150, PEN_RED, "STALL");
    if (p->jam) g_text_centre(cv, 162, PEN_FIRE, "GUNS JAMMED");
    else if (p->heat > 75 && (g->tick & 4)) g_text_centre(cv, 162, PEN_FIRE, "GUNS HOT");
    if (p->ammo == 0 && p->state == PS_FLY) g_text_centre(cv, 174, PEN_RED, "OUT OF AMMUNITION: LAND AT HOME");
    if (p->on_ground) g_text_centre(cv, 186, PEN_GREEN, p->ground_t > 0 ? "REARMING" : "ON THE GROUND");
    if (ac.hurt_timer > 0) {
        g_rect(cv, 0, 0, SCREEN_W - 1, 2, PEN_RED);
        g_rect(cv, 0, 0, 2, SCREEN_H - 1, PEN_RED);
        g_rect(cv, SCREEN_W - 3, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_RED);
    }
}

static void hud(Canvas *cv, const APGame *g)
{
    char tmp[48];
    const Plane *p = &g->p[0];
    int k;
    ap_objective_text(g, tmp);
    g_text_shadow(cv, 4, 10, PEN_YELLOW, tmp);
    sprintf(tmp, "%ld", (long)g->score);
    g_text_shadow(cv, 4, 22, 1, tmp);
    for (k = 0; k < g->lives; k++) g_rect(cv, 4 + k * 10, 28, 11 + k * 10, 30, PEN_SWALLOW);
    radar(cv, g, SCREEN_W - 32, 32, 27);
    pointer(cv, g, SCREEN_W / 2, 18);
    gunsight(cv, g);
    g_rect(cv, 0, 232, SCREEN_W - 1, SCREEN_H - 1, PEN_PANEL);
    sprintf(tmp, "SPD %3ld", (long)((p->v * 25) >> 16));
    g_text(cv, 4, 242, 1, tmp);
    sprintf(tmp, "ALT %4ld", (long)((p->y - ap_ground_y(p->x, p->z)) >> 16));
    g_text(cv, 4, 252, 1, tmp);
    g_text(cv, 76, 242, PEN_GREY, "THR");
    bar(cv, 102, 236, 52, p->throttle, 100, PEN_GREEN);
    g_text(cv, 76, 252, PEN_GREY, "GUN");
    bar(cv, 102, 246, 52, p->heat, AP_JAM_HEAT, p->jam ? PEN_RED : p->heat > 75 ? PEN_FIRE : PEN_YELLOW);
    sprintf(tmp, "AMMO %3ld", (long)p->ammo);
    g_text(cv, 162, 242, p->ammo < 60 ? PEN_RED : 1, tmp);
    g_text(cv, 162, 252, PEN_GREY, "DAMAGE");
    bar(cv, 214, 246, 50, AP_HP - p->hp, AP_HP, PEN_RED);
    sprintf(tmp, "M%ld", (long)g->mission);
    g_text(cv, 276, 242, PEN_YELLOW, tmp);
    if (ac.autoplay) g_text(cv, 276, 252, PEN_GREEN, "AUTO");
    else if (ac.invert) g_text(cv, 276, 252, PEN_GREY, "INV");
}

/* the cockpit: the upper wing overhead, its struts, the guns and their
 * ring sight, the instrument panel */
static void dial(Canvas *cv, int cx, int cy, int r, int angle, const char *label)
{
    int16_t xs[3], ys[3];
    g_circle(cv, cx, cy, r + 1, PEN_GREY);
    g_circle(cv, cx, cy, r, 0);
    xs[0] = (int16_t)(cx + ((a3_sin(angle) * (r - 2)) >> 16)); ys[0] = (int16_t)(cy - ((a3_cos(angle) * (r - 2)) >> 16));
    xs[1] = (int16_t)(cx + ((a3_sin(angle + 16384) * 2) >> 16)); ys[1] = (int16_t)(cy - ((a3_cos(angle + 16384) * 2) >> 16));
    xs[2] = (int16_t)(cx - ((a3_sin(angle + 16384) * 2) >> 16)); ys[2] = (int16_t)(cy + ((a3_cos(angle + 16384) * 2) >> 16));
    g_poly(cv, xs, ys, 3, 1, 0, SCREEN_H - 1);
    g_text(cv, cx - (int)strlen(label) * 4, cy + r + 10, PEN_GREY, label);
}

static void cockpit(Canvas *cv, const APGame *g)
{
    const Plane *p = &g->p[0];
    char tmp[40];
    int k;
    int16_t xs[4], ys[4];
    /* the wing and its trailing edge, a cut-out over the cockpit */
    g_rect(cv, 0, 0, SCREEN_W - 1, 14, PEN_SWALLOW + 1);
    g_rect(cv, 0, 15, SCREEN_W - 1, 17, PEN_DARK);
    g_rect(cv, 130, 12, 190, 17, PEN_SKY);
    /* struts, leaning in */
    xs[0] = 54; ys[0] = 17; xs[1] = 60; ys[1] = 17; xs[2] = 26; ys[2] = 206; xs[3] = 20; ys[3] = 206;
    g_poly(cv, xs, ys, 4, PEN_WOOD + 1, 0, SCREEN_H - 1);
    xs[0] = 260; xs[1] = 266; xs[2] = 300; xs[3] = 294;
    g_poly(cv, xs, ys, 4, PEN_WOOD + 1, 0, SCREEN_H - 1);
    /* the ring sight */
    for (k = 0; k < 16; k++) {
        int a = k * 4096, x = 160 + ((a3_sin(a) * 22) >> 16), y = 112 - ((a3_cos(a) * 22) >> 16);
        g_rect(cv, x, y, x + 1, y + 1, PEN_FLAK);
    }
    g_rect(cv, 159, 111, 160, 112, PEN_RED);
    /* the guns on the cowling, kicking when they fire */
    k = (p->gun_cd == 2) ? 3 : 0;
    g_rect(cv, 138, 186 + k, 148, 206, PEN_FLAK);
    g_rect(cv, 172, 186 + k, 182, 206, PEN_FLAK);
    g_rect(cv, 141, 186 + k, 145, 190 + k, 0);
    g_rect(cv, 175, 186 + k, 179, 190 + k, 0);
    /* the panel */
    g_rect(cv, 0, 206, SCREEN_W - 1, SCREEN_H - 1, PEN_WOOD + 1);
    g_rect(cv, 0, 206, SCREEN_W - 1, 207, PEN_DARK);
    dial(cv, 36, 226, 15, (int)(((p->v * 25) >> 16) * 300), "SPEED");
    dial(cv, 90, 226, 15, (int)((((p->y >> 16) % 1000) * 65) & 0xFFFF), "ALT");
    sprintf(tmp, "%4ld", (long)((p->y - ap_ground_y(p->x, p->z)) >> 16));
    g_text(cv, 72, 214, PEN_YELLOW, tmp);
    sprintf(tmp, "%03ld", (long)((p->yaw * 360L) >> 16));
    g_text(cv, 147, 222, PEN_YELLOW, tmp);
    g_text(cv, 143, 236, PEN_GREY, "HDG");
    g_text(cv, 196, 216, 1, "THR");
    bar(cv, 226, 210, 40, p->throttle, 100, PEN_GREEN);
    g_text(cv, 196, 228, 1, "GUN");
    bar(cv, 226, 222, 40, p->heat, AP_JAM_HEAT, p->jam ? PEN_RED : PEN_YELLOW);
    g_text(cv, 196, 240, 1, "DMG");
    bar(cv, 226, 234, 40, AP_HP - p->hp, AP_HP, PEN_RED);
    sprintf(tmp, "%3ld", (long)p->ammo);
    g_text(cv, 274, 228, p->ammo < 60 ? PEN_RED : 1, tmp);
    g_text(cv, 270, 240, PEN_GREY, "RDS");
    ap_objective_text(g, tmp);
    g_text_shadow(cv, 4, 30, PEN_YELLOW, tmp);
    pointer(cv, g, SCREEN_W / 2, 44);
    radar(cv, g, SCREEN_W - 30, 44, 22);
    if (ac.autoplay) g_text_shadow(cv, 4, 42, PEN_GREEN, "AUTO");
}

static void result_box(Canvas *cv, const APGame *g)
{
    char tmp[48];
    int ok = ac.result == PH_DONE;
    int32_t shots = g->shots - ac.m_shots, hits = g->hits - ac.m_hits;
    g_box(cv, 40, 54, 280, 176, PEN_PANEL, ok ? PEN_GREEN : PEN_RED);
    g_text_big(cv, ok ? 64 : 72, 62, ok ? PEN_GREEN : PEN_RED, ok ? "MISSION DONE" : g->p[0].state == PS_FLY ? "MISSION LOST" : "SHOT DOWN", 2);
    if (!ok && g->p[0].state != PS_FLY) g_text_centre(cv, 92, PEN_GREY, g->p[0].hp > 0 ? "INTO THE GROUND" : "BY ENEMY FIRE");
    sprintf(tmp, "AEROPLANES %ld  GROUND %ld", (long)(g->kills - ac.m_kills), (long)(g->gkills - ac.m_gkills));
    g_text_centre(cv, 108, 1, tmp);
    sprintf(tmp, "HITS %ld%%   SCORE %ld", (long)(shots ? hits * 100 / shots : 0), (long)(g->score - ac.m_score));
    g_text_centre(cv, 122, 1, tmp);
    sprintf(tmp, "%s  %ld", ap_ranks[ap_rank(g->score)], (long)g->score);
    g_text_centre(cv, 140, PEN_YELLOW, tmp);
#ifdef AMIGA3DO
    if (ac.state_timer == 0) g_text_centre(cv, 162, (g->tick / 10) & 1 ? PEN_YELLOW : 1, "PRESS A");
#else
    if (ac.state_timer == 0) g_text_centre(cv, 162, (g->tick / 10) & 1 ? PEN_YELLOW : 1, "PRESS FIRE");
#endif
}

static void over_box(Canvas *cv, const APGame *g)
{
    char tmp[48];
    g_box(cv, 30, 54, 290, 190, PEN_PANEL, g->won ? PEN_GREEN : PEN_RED);
    g_text_big(cv, g->won ? 56 : 88, 62, g->won ? PEN_GREEN : PEN_RED, g->won ? "THE WAR IS WON" : "GROUNDED", 2);
    g_text_centre(cv, 92, PEN_GREY, g->won ? "THE GREY FALCON FLIES NO MORE" : "THE SQUADRON HAS NO AEROPLANES LEFT");
    sprintf(tmp, "%ld MISSIONS FLOWN", (long)g->missions_flown);
    g_text_centre(cv, 112, 1, tmp);
    sprintf(tmp, "%ld AEROPLANES, %ld ON THE GROUND", (long)g->kills, (long)g->gkills);
    g_text_centre(cv, 126, 1, tmp);
    sprintf(tmp, "SCORE %ld", (long)g->score);
    g_text_centre(cv, 140, 1, tmp);
    g_text_centre(cv, 156, PEN_YELLOW, ap_ranks[ap_rank(g->score)]);
#ifdef AMIGA3DO
    if (ac.state_timer == 0) g_text_centre(cv, 178, (g->tick / 10) & 1 ? PEN_YELLOW : 1, "PRESS A");
#else
    if (ac.state_timer == 0) g_text_centre(cv, 178, (g->tick / 10) & 1 ? PEN_YELLOW : 1, "PRESS FIRE");
#endif
}

void render_game(Canvas *cv, const APGame *g)
{
    scene(cv, g);
    if (ac.view == VIEW_COCKPIT && g->p[0].state != PS_DEAD) cockpit(cv, g);
    else hud(cv, g);
    warnings(cv, g);
    if (ac.state == AS_PLAY && g->mtick < 75) g_text_centre(cv, 70, PEN_YELLOW, ap_missions[g->mission - 1].name);
    if (ac.state == AS_RESULT) result_box(cv, g);
    if (ac.state == AS_OVER) over_box(cv, g);
    if (ac.paused == 1) g_text_centre(cv, 128, 1, "PAUSED");
    if (ac.flash_timer > 0 && ac.flash[0]) g_text_centre(cv, 60, ac.flash_pen, ac.flash);
}

void render_title(Canvas *cv, const APGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot flies behind the title */
    g_text_big(cv, 18, 22, 0, "ACE PILOT", 4);
    g_text_big(cv, 16, 20, PEN_YELLOW, "ACE PILOT", 4);
    g_text_big(cv, 146, 58, 0, "2", 5);
    g_text_big(cv, 144, 56, PEN_RED, "2", 5);
    g_text_centre(cv, 112, 1, "LARKSPUR SQUADRON OVER THE SELLEN");
    g_box(cv, 24, 190, 296, 252, PEN_PANEL, PEN_GREY);
    sprintf(tmp, "MISSION %ld: %s", (long)ac.start_mission, ap_missions[ac.start_mission - 1].name);
    g_text_centre(cv, 204, PEN_GREEN, tmp);
    g_text_centre(cv, 218, PEN_GREY, "UP / DOWN: CHOOSE THE MISSION");
#ifdef AMIGA3DO
    g_text_centre(cv, 232, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO FLY");
#else
    g_text_centre(cv, 232, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO FLY");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld   R/L THROTTLE  C VIEW", (long)ac.best);
#else
    sprintf(tmp, "BEST %ld   HELP: HOW TO FLY", (long)ac.best);
#endif
    g_text_centre(cv, 246, PEN_GREY, tmp);
}

/* ---------- the briefing, with a map of the sector ---------- */
#define MAP_X0 200
#define MAP_Y0 114
#define MAP_S  108
static int mapx(int32_t x) { return MAP_X0 + (int)((x + AP_WORLD) * MAP_S / (2 * AP_WORLD)); }
static int mapy(int32_t z) { return MAP_Y0 + MAP_S - (int)((z + AP_WORLD) * MAP_S / (2 * AP_WORLD)); }

static void map(Canvas *cv, const APGame *g)
{
    int32_t x;
    int k;
    g_rect(cv, MAP_X0, MAP_Y0, MAP_X0 + MAP_S, MAP_Y0 + MAP_S, PEN_GRASS);
    for (x = -AP_WORLD; x < AP_WORLD; x += 120) {
        g_rect(cv, mapx(x), mapy(ap_river_z(x)), mapx(x) + 1, mapy(ap_river_z(x)), PEN_RIVER);
        g_rect(cv, mapx(x), mapy(ap_rail_z(x)), mapx(x), mapy(ap_rail_z(x)), PEN_DARK);
        if ((x / 120) & 1) g_rect(cv, mapx(x), mapy(0), mapx(x) + 1, mapy(0), PEN_RED);
    }
    for (k = 0; k < AP_HILLS; k++) g_rect(cv, mapx(ap_hills[k].x) - 1, mapy(ap_hills[k].z) - 1, mapx(ap_hills[k].x) + 1, mapy(ap_hills[k].z) + 1, PEN_HILL + 1);
    g_rect(cv, mapx(AP_HOME_X) - 1, mapy(AP_HOME_Z) - 2, mapx(AP_HOME_X) + 1, mapy(AP_HOME_Z) + 2, PEN_GREEN);
    g_rect(cv, mapx(AP_ENEMY_X) - 2, mapy(AP_ENEMY_Z) - 1, mapx(AP_ENEMY_X) + 2, mapy(AP_ENEMY_Z) + 1, PEN_SLATE);
    g_rect(cv, mapx(AP_DEPOT_X) - 1, mapy(AP_DEPOT_Z) - 1, mapx(AP_DEPOT_X) + 1, mapy(AP_DEPOT_Z) + 1, PEN_MUD);
    for (k = 0; k < g->nwp; k++) g_rect(cv, mapx(g->wp[k].x >> 16) - 1, mapy(g->wp[k].z >> 16) - 1, mapx(g->wp[k].x >> 16) + 1, mapy(g->wp[k].z >> 16) + 1, PEN_YELLOW);
    for (k = 0; k < g->ngr; k++)
        if (g->gr[k].objective) g_rect(cv, mapx(g->gr[k].x >> 16) - 1, mapy(g->gr[k].z >> 16) - 1, mapx(g->gr[k].x >> 16) + 1, mapy(g->gr[k].z >> 16) + 1, PEN_YELLOW);
    for (k = 1; k < g->np; k++)
        g_rect(cv, mapx(g->p[k].x >> 16), mapy(g->p[k].z >> 16), mapx(g->p[k].x >> 16) + 1, mapy(g->p[k].z >> 16) + 1, g->p[k].side == SIDE_HOME ? PEN_BLUE : PEN_RED);
    g_rect(cv, MAP_X0 - 1, MAP_Y0 - 1, MAP_X0 + MAP_S + 1, MAP_Y0 - 1, PEN_GREY);
    g_rect(cv, MAP_X0 - 1, MAP_Y0 + MAP_S + 1, MAP_X0 + MAP_S + 1, MAP_Y0 + MAP_S + 1, PEN_GREY);
    g_rect(cv, MAP_X0 - 1, MAP_Y0 - 1, MAP_X0 - 1, MAP_Y0 + MAP_S + 1, PEN_GREY);
    g_rect(cv, MAP_X0 + MAP_S + 1, MAP_Y0 - 1, MAP_X0 + MAP_S + 1, MAP_Y0 + MAP_S + 1, PEN_GREY);
    g_text(cv, MAP_X0 + MAP_S / 2 - 3, MAP_Y0 - 4, PEN_GREY, "N");
}

void render_brief(Canvas *cv, const APGame *g)
{
    const APMission *m = &ap_missions[g->mission - 1];
    char tmp[48];
    int k;
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_PANEL);
    g_sync();
    sprintf(tmp, "MISSION %ld OF %ld", (long)g->mission, (long)AP_MISSIONS);
    g_text(cv, 8, 14, PEN_GREY, tmp);
    g_text_big(cv, 8, 22, PEN_YELLOW, m->name, 2);
    g_text(cv, 8, 50, 1, m->place);
    for (k = 0; k < 4; k++) g_text(cv, 8, 68 + k * 12, 1, m->brief[k]);
    g_box(cv, 6, 122, 188, 218, 0, PEN_GREY);
    g_text(cv, 12, 136, PEN_YELLOW, "PILOT'S RECORD");
    g_text(cv, 12, 150, 1, ap_ranks[ap_rank(g->score)]);
    sprintf(tmp, "SCORE %ld", (long)g->score);
    g_text(cv, 12, 164, 1, tmp);
    sprintf(tmp, "VICTORIES %ld", (long)g->kills);
    g_text(cv, 12, 178, 1, tmp);
    sprintf(tmp, "MISSIONS FLOWN %ld", (long)g->missions_flown);
    g_text(cv, 12, 192, 1, tmp);
    sprintf(tmp, "AEROPLANES LEFT %ld", (long)g->lives);
    g_text(cv, 12, 206, g->lives > 1 ? 1 : PEN_RED, tmp);
    map(cv, g);
#ifdef AMIGA3DO
    g_text_centre(cv, 236, (g->tick / 10) & 1 ? PEN_YELLOW : 1, "PRESS A TO TAKE OFF");
#else
    g_text_centre(cv, 236, (g->tick / 10) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO TAKE OFF");
#endif
#ifdef AMIGA3DO
    g_text_centre(cv, 250, PEN_GREY, "X: BACK TO THE MENU");
#else
    g_text_centre(cv, 250, PEN_GREY, "ESC: BACK TO THE TITLE");
#endif
}

static const char *const help_lines[2][13] = {
    { "FLY YOUR SWALLOW AGAINST GRAULAND.",
      "BANK TO TURN; PULL BACK IN A BANK TO",
      "TURN HARDER. CLIMBING COSTS SPEED AND",
      "DIVING BUYS IT: TOO SLOW AND YOU STALL.",
      "",
      "THE TWIN GUNS HEAT UP: FIRE IN BURSTS",
      "OR THEY JAM. LAND GENTLY ON YOUR OWN",
      "FIELD AND STOP TO REARM AND REPAIR.",
      "",
      "LOW DOWN THE AA GUNS FIND IT HARD TO",
      "AIM. WASPS ARE LIGHT, HORNETS TOUGH,",
      "OX BOMBERS HAVE A REAR GUNNER.",
      "EIGHT MISSIONS; THREE AEROPLANES." },
    { "LEFT / RIGHT     BANK",
      "UP / DOWN        NOSE DOWN / UP",
#ifdef AMIGA3DO
      "A                GUNS",
#else
      "SPACE / FIRE     GUNS",
#endif
#ifdef AMIGA3DO
      "R / L            THROTTLE UP / DOWN",
#else
      "A / Z            THROTTLE UP / DOWN",
#endif
#ifdef AMIGA3DO
      "C                CHASE / COCKPIT",
#else
      "V                CHASE / COCKPIT",
#endif
#ifdef AMIGA3DO
      "",
#else
      "I                INVERT UP / DOWN",
#endif
      "P                PAUSE",
      "B                AUTOPILOT",
#ifdef AMIGA3DO
      "",
#else
      "HELP / H         THESE PAGES",
#endif
#ifdef AMIGA3DO
      "X                THE MENU",
#else
      "ESC              TITLE / QUIT",
#endif
      "",
      "THE RADAR: AHEAD IS UP. RED: ENEMY,",
      "BLUE: OURS, YELLOW: THE TARGET." },
};

void render_help(Canvas *cv, int page)
{
    int k;
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_PANEL);
    g_sync();
    g_text_big(cv, page == 1 ? 88 : 96, 12, PEN_YELLOW, page == 1 ? "HOW TO FLY" : "CONTROLS", 2);
    for (k = 0; k < 13; k++) g_text(cv, 8, 52 + k * 14, 1, help_lines[page - 1][k]);
    g_text(cv, 8, 248, PEN_GREY, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2");
#ifdef AMIGA3DO
    g_text(cv, 156, 248, PEN_YELLOW, "A: BACK");
#else
    g_text(cv, 156, 248, PEN_YELLOW, "HELP: NEXT  FIRE: BACK");
#endif
}
