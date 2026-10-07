// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "engine3d.h"

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>

#include <proto/graphics.h>

#include <string.h>

/* Palette layout — caller sets these up at boot, engine just uses. */
#define PEN_SPACE     0
#define PEN_STAR_BASE 1
#define PEN_STAR_CT   4
#define PEN_SHIP_BASE 8      /* base of the 32-entry shading ramp */
#define PEN_SHIP_CT   32

/* Viewport state, initialised via e3d_init. */
static int g_w = 320, g_h = 200;
static LONG g_cx = 160, g_cy = 100;
static LONG g_fov = 340;      /* focal length in pixels */

/* Sin/cos table — 360 entries covering one full rotation. */
static WORD sintab[360];

/* Bhaskara sine approximation, TRIG_ONE = ONE = 4096.
 *   sin(x) = 16 x (180-x) / (5*180² - 4 x (180-x))    for x in [0,180]
 * Mirror to negative for the second half. */
static void build_sintab(void)
{
    LONG i;
    for (i = 0; i < 360; i++) {
        LONG a = i, sign = 1;
        LONG x;
        LONG y;
        LONG num;
        LONG den;
        if (a >= 180) { a -= 180; sign = -1; }
        x = a;
        y = 180 - a;
        num = 16L * x * y;
        den = (5L * 180 * 180 - 4L * x * y) / ONE;
        if (den == 0) den = 1;
        sintab[i] = (WORD)((num / den) * sign);
    }
}

#ifdef AMIGA3DO
/* 3DO port: same result as "a %= 360; if (a < 0) a += 360;" without a
 * software division per call for the usual near-range angles */
static LONG wrap360(LONG a)
{
    if (a >= 3600 || a <= -3600) a %= 360;
    while (a >= 360) a -= 360;
    while (a < 0) a += 360;
    return a;
}
LONG e3d_sin(LONG a) { return sintab[wrap360(a)]; }
LONG e3d_cos(LONG a) { return sintab[wrap360(a + 90)]; }
#else
LONG e3d_sin(LONG a) {
    a %= 360; if (a < 0) a += 360;
    return sintab[a];
}
LONG e3d_cos(LONG a) {
    a += 90;
    a %= 360; if (a < 0) a += 360;
    return sintab[a];
}
#endif

void e3d_init(int w, int h)
{
    g_w = w; g_h = h;
    g_cx = w / 2;
    g_cy = h / 2;
    g_fov = (h * 340) / 256;      /* scales with viewport height */
    build_sintab();
}

/* Integer square root — for face normal length. */
static LONG isqrt_val(LONG v)
{
    LONG r = 0, bit = 1L << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

void e3d_compute_normals(Model *m)
{
    int f;
    for (f = 0; f < m->num_faces; f++) {
        LONG *a = m->vbase[m->face[f][0]];
        LONG *b = m->vbase[m->face[f][1]];
        LONG *c = m->vbase[m->face[f][2]];
        LONG ax = b[0] - a[0], ay = b[1] - a[1], az = b[2] - a[2];
        LONG bx = c[0] - a[0], by = c[1] - a[1], bz = c[2] - a[2];
        LONG nx = (ay * bz - az * by) / 1000;
        LONG ny = (az * bx - ax * bz) / 1000;
        LONG nz = (ax * by - ay * bx) / 1000;
        LONG len = isqrt_val(nx * nx + ny * ny + nz * nz);
        if (len < 1) len = 1;
        m->fnorm[f][0] = (nx * ONE) / len;
        m->fnorm[f][1] = (ny * ONE) / len;
        m->fnorm[f][2] = (nz * ONE) / len;
    }
}

/* -------------------------------------------------------------
 * Per-frame projection helpers.
 *
 * Camera transform: to render the world from the camera's
 * viewpoint we translate every world point by -cam.pos and
 * rotate by the *inverse* of the camera's orientation. Because
 * rotation matrices are orthonormal, the inverse == transpose
 * — so we rotate by (-cam.yaw, -cam.pitch, -cam.roll) applied
 * in the opposite order (roll first, then pitch, then yaw).
 *
 * We compose the camera into a single 3x3 matrix at frame start
 * to avoid recomputing per-vertex.
 * ------------------------------------------------------------- */

typedef struct {
    LONG m[3][3];       /* rotation matrix, ONE-scaled */
} Mat3;

static Mat3 g_view_rot;
static LONG g_view_tx, g_view_ty, g_view_tz;

static void build_view_matrix(const Camera *cam)
{
    /* Inverse of applying yaw then pitch then roll to a world
     * vector — inverse ordering is roll^-1 then pitch^-1 then
     * yaw^-1, with each angle negated. */
    LONG sp = e3d_sin(-cam->pitch), cp = e3d_cos(-cam->pitch);
    LONG sy = e3d_sin(-cam->yaw),   cy = e3d_cos(-cam->yaw);
    LONG sr = e3d_sin(-cam->roll),  cr = e3d_cos(-cam->roll);
    /* Row-major composition Rx * Ry * Rz applied in that order to
     * an already-translated point. Precomputed once per frame. */
    /* Ry (yaw around Y) */
    LONG y00 =  cy, y01 =    0, y02 = sy;
    LONG y10 =   0, y11 =  ONE, y12 =  0;
    LONG y20 = -sy, y21 =    0, y22 = cy;
    /* Rx (pitch around X) */
    LONG x00 = ONE, x01 =    0, x02 =   0;
    LONG x10 =   0, x11 =   cp, x12 = -sp;
    LONG x20 =   0, x21 =   sp, x22 =  cp;
    /* Rz (roll around Z) */
    LONG z00 =  cr, z01 = -sr, z02 =   0;
    LONG z10 =  sr, z11 =  cr, z12 =   0;
    LONG z20 =   0, z21 =   0, z22 = ONE;
    /* T = Rx * Ry (pitch about x, then yaw about y). */
    LONG t00 = (x00 * y00 + x01 * y10 + x02 * y20) >> FP;
    LONG t01 = (x00 * y01 + x01 * y11 + x02 * y21) >> FP;
    LONG t02 = (x00 * y02 + x01 * y12 + x02 * y22) >> FP;
    LONG t10 = (x10 * y00 + x11 * y10 + x12 * y20) >> FP;
    LONG t11 = (x10 * y01 + x11 * y11 + x12 * y21) >> FP;
    LONG t12 = (x10 * y02 + x11 * y12 + x12 * y22) >> FP;
    LONG t20 = (x20 * y00 + x21 * y10 + x22 * y20) >> FP;
    LONG t21 = (x20 * y01 + x21 * y11 + x22 * y21) >> FP;
    LONG t22 = (x20 * y02 + x21 * y12 + x22 * y22) >> FP;
    /* M = Rz * T (roll last). */
    g_view_rot.m[0][0] = (z00 * t00 + z01 * t10 + z02 * t20) >> FP;
    g_view_rot.m[0][1] = (z00 * t01 + z01 * t11 + z02 * t21) >> FP;
    g_view_rot.m[0][2] = (z00 * t02 + z01 * t12 + z02 * t22) >> FP;
    g_view_rot.m[1][0] = (z10 * t00 + z11 * t10 + z12 * t20) >> FP;
    g_view_rot.m[1][1] = (z10 * t01 + z11 * t11 + z12 * t21) >> FP;
    g_view_rot.m[1][2] = (z10 * t02 + z11 * t12 + z12 * t22) >> FP;
    g_view_rot.m[2][0] = (z20 * t00 + z21 * t10 + z22 * t20) >> FP;
    g_view_rot.m[2][1] = (z20 * t01 + z21 * t11 + z22 * t21) >> FP;
    g_view_rot.m[2][2] = (z20 * t02 + z21 * t12 + z22 * t22) >> FP;
    g_view_tx = -cam->x;
    g_view_ty = -cam->y;
    g_view_tz = -cam->z;
}

/* Apply view transform to a world point -> camera space. */
static void view_transform(LONG wx, LONG wy, LONG wz,
                                  LONG *cx, LONG *cy, LONG *cz)
{
    LONG tx = wx + g_view_tx;
    LONG ty = wy + g_view_ty;
    LONG tz = wz + g_view_tz;
    *cx = (g_view_rot.m[0][0] * tx
         + g_view_rot.m[0][1] * ty
         + g_view_rot.m[0][2] * tz) >> FP;
    *cy = (g_view_rot.m[1][0] * tx
         + g_view_rot.m[1][1] * ty
         + g_view_rot.m[1][2] * tz) >> FP;
    *cz = (g_view_rot.m[2][0] * tx
         + g_view_rot.m[2][1] * ty
         + g_view_rot.m[2][2] * tz) >> FP;
}

int e3d_project(const Camera *cam, LONG wx, LONG wy, LONG wz,
                int *sx, int *sy, LONG *depth)
{
    LONG cx, cy, cz;
    LONG px;
    LONG py;
    build_view_matrix(cam);
    view_transform(wx, wy, wz, &cx, &cy, &cz);
    if (cz < 32) return 0;                /* behind or too close */
    px = g_cx + (cx * g_fov) / cz;
    py = g_cy - (cy * g_fov) / cz;
    if (px < -50 || px > g_w + 50) return 0;
    if (py < -50 || py > g_h + 50) return 0;
    if (sx)    *sx = (int)px;
    if (sy)    *sy = (int)py;
    if (depth) *depth = cz;
    return 1;
}

/* -------------------------------------------------------------
 * Entity rendering.
 *
 * For each active object we apply the object's own local rotation
 * (pitch/yaw/roll around its own axes), then translate by its
 * world position, then view-transform.
 *
 * The face normal light dot-product uses a world-space light dir
 * (fixed at (0.5, 0.7, 0.5) here) rotated into the OBJECT's local
 * frame — that way we don't need to re-rotate every normal per
 * vertex-transform pass.
 * ------------------------------------------------------------- */

static WORD  proj_x[MAX_VERTS];
static WORD  proj_y[MAX_VERTS];
static LONG  proj_z[MAX_VERTS];
static UBYTE proj_ok[MAX_VERTS];
static int   face_order[MAX_FACES];
static UBYTE face_shade[MAX_FACES];
static UBYTE face_vis  [MAX_FACES];

static void build_object_matrix(const Entity *o, Mat3 *out)
{
    /* Entity rotation: yaw, pitch, roll applied in Ry*Rx*Rz. */
    LONG sp = e3d_sin(o->pitch), cp = e3d_cos(o->pitch);
    LONG sy = e3d_sin(o->yaw),   cy = e3d_cos(o->yaw);
    LONG sr = e3d_sin(o->roll),  cr = e3d_cos(o->roll);
    LONG y00 =  cy, y02 = sy;
    LONG y20 = -sy, y22 = cy;
    LONG x11 =  cp, x12 = -sp;
    LONG x21 =  sp, x22 =  cp;
    LONG z00 =  cr, z01 = -sr;
    LONG z10 =  sr, z11 =  cr;
    /* Rx * Rz  */
    LONG a00 = z00,                                      a01 = z01,                                      a02 = 0;
    LONG a10 = (x11 * z10 + x12 * 0) >> FP,              a11 = (x11 * z11 + x12 * 0) >> FP,              a12 = (x11 * 0   + x12 * ONE) >> FP;
    LONG a20 = (x21 * z10 + x22 * 0) >> FP,              a21 = (x21 * z11 + x22 * 0) >> FP,              a22 = (x21 * 0   + x22 * ONE) >> FP;
    /* Ry * (Rx*Rz) */
    out->m[0][0] = (y00 * a00 + 0 * a10 + y02 * a20) >> FP;
    out->m[0][1] = (y00 * a01 + 0 * a11 + y02 * a21) >> FP;
    out->m[0][2] = (y00 * a02 + 0 * a12 + y02 * a22) >> FP;
    out->m[1][0] = a10;
    out->m[1][1] = a11;
    out->m[1][2] = a12;
    out->m[2][0] = (y20 * a00 + 0 * a10 + y22 * a20) >> FP;
    out->m[2][1] = (y20 * a01 + 0 * a11 + y22 * a21) >> FP;
    out->m[2][2] = (y20 * a02 + 0 * a12 + y22 * a22) >> FP;
}

static void draw_one_object(struct RastPort *rp, const Entity *o)
{
    const Model *m = o->model;
    Mat3 obj_rot;
    int v;
    LONG light_wx;
    LONG light_wy;
    LONG light_wz;
    int n;
    int f;
    int i;
    build_object_matrix(o, &obj_rot);

    for (v = 0; v < m->num_verts; v++) {
        LONG lx = m->vbase[v][0], ly = m->vbase[v][1], lz = m->vbase[v][2];
        /* Local -> world (obj rotate then translate). */
        LONG wx = ((obj_rot.m[0][0] * lx + obj_rot.m[0][1] * ly + obj_rot.m[0][2] * lz) >> FP) + o->x;
        LONG wy = ((obj_rot.m[1][0] * lx + obj_rot.m[1][1] * ly + obj_rot.m[1][2] * lz) >> FP) + o->y;
        LONG wz = ((obj_rot.m[2][0] * lx + obj_rot.m[2][1] * ly + obj_rot.m[2][2] * lz) >> FP) + o->z;
        /* World -> camera (view rot + translate). */
        LONG cx, cy, cz;
        LONG sxp;
        LONG syp;
        view_transform(wx, wy, wz, &cx, &cy, &cz);
        if (cz < 32) {
            proj_ok[v] = 0;
            proj_z[v]  = 1;
            proj_x[v]  = 0;
            proj_y[v]  = 0;
            continue;
        }
        sxp = g_cx + (cx * g_fov) / cz;
        syp = g_cy - (cy * g_fov) / cz;
        if (sxp < 0) sxp = 0; else if (sxp > (LONG)g_w - 1) sxp = g_w - 1;
        if (syp < 0) syp = 0; else if (syp > (LONG)g_h - 1) syp = g_h - 1;
        proj_x[v]  = (WORD)sxp;
        proj_y[v]  = (WORD)syp;
        proj_z[v]  = cz;
        proj_ok[v] = 1;
    }

    /* Light dir in world space (fixed). Rotate into camera space
     * so we can dot against camera-space normals — but we've kept
     * normals in the object's local frame. Easier: also rotate the
     * normal by (obj_rot * view_rot) — do it inline per face. */
    light_wx = ONE / 2;
    light_wy = (ONE * 7) / 10;
    light_wz = ONE / 2;

    /* Collect visible faces + shade + z-sort key. */
    n = 0;
    for (f = 0; f < m->num_faces; f++) {
        /* Rotate face normal by object rotation to get world normal. */
        LONG nlx = m->fnorm[f][0], nly = m->fnorm[f][1], nlz = m->fnorm[f][2];
        LONG wnx = (obj_rot.m[0][0] * nlx + obj_rot.m[0][1] * nly + obj_rot.m[0][2] * nlz) >> FP;
        LONG wny = (obj_rot.m[1][0] * nlx + obj_rot.m[1][1] * nly + obj_rot.m[1][2] * nlz) >> FP;
        LONG wnz = (obj_rot.m[2][0] * nlx + obj_rot.m[2][1] * nly + obj_rot.m[2][2] * nlz) >> FP;
        /* View direction = vector from face to camera. Approximate
         * with vector from object centre to camera (cheap). */
        LONG vdx = -o->x + 0;    /* cam - obj, using cam at origin post-transform */
        LONG vdy = -o->y + 0;
        LONG vdz = -o->z + 0;
        /* Actually simpler: dot the world normal with (obj->cam) */
        LONG dot_facing = wnx * vdx + wny * vdy + wnz * vdz;
        LONG light_dot;
        int shade;
        if (dot_facing < 0) { face_vis[f] = 0; continue; }
        face_vis[f] = 1;
        /* Diffuse: dot world normal with light dir (both unit in FP). */
        light_dot = (wnx * light_wx + wny * light_wy + wnz * light_wz) >> FP;
        if (light_dot < 0) light_dot = 0;
        shade = (int)((light_dot * (PEN_SHIP_CT - 1)) >> FP);
        if (shade < 0) shade = 0;
        if (shade > PEN_SHIP_CT - 1) shade = PEN_SHIP_CT - 1;
        face_shade[f] = (UBYTE)(o->pen_base + shade);
        face_order[n++] = f;
    }

    /* Painter's sort: draw back-to-front (max z first). */
    for (i = 1; i < n; i++) {
        int key = face_order[i], j = i - 1;
        LONG kz = proj_z[m->face[key][0]] + proj_z[m->face[key][1]]
                + proj_z[m->face[key][2]];
        while (j >= 0) {
            int gf = face_order[j];
            LONG gz = proj_z[m->face[gf][0]] + proj_z[m->face[gf][1]]
                    + proj_z[m->face[gf][2]];
            if (gz >= kz) break;
            face_order[j + 1] = face_order[j];
            j--;
        }
        face_order[j + 1] = key;
    }

    /* Draw filled triangles. AreaMove/AreaDraw/AreaEnd uses the
     * RastPort's TmpRas + AreaInfo the caller must have set. */
    for (i = 0; i < n; i++) {
        int ff = face_order[i];
        int a = m->face[ff][0], b = m->face[ff][1], c = m->face[ff][2];
        if (!proj_ok[a] || !proj_ok[b] || !proj_ok[c]) continue;
        SetAPen(rp, face_shade[ff]);
        AreaMove(rp, proj_x[a], proj_y[a]);
        AreaDraw(rp, proj_x[b], proj_y[b]);
        AreaDraw(rp, proj_x[c], proj_y[c]);
        AreaEnd(rp);
    }
}

/* Starfield — 128 world-space stars around the origin. When the
 * camera is anywhere in the finite universe they project onto the
 * background, giving the illusion of star parallax. */
#define NUM_STARS  128
#define STAR_RANGE 8000
static LONG  star_x[NUM_STARS];
static LONG  star_y[NUM_STARS];
static LONG  star_z[NUM_STARS];
static UBYTE star_pen[NUM_STARS];
static UBYTE g_stars_built = 0;

static ULONG srand_state = 0x1234567UL;
static ULONG srand_next(void)
{
    srand_state = srand_state * 1664525UL + 1013904223UL;
    return srand_state;
}

static void build_stars(void)
{
    int i;
    srand_state = 0x87654321UL;
    for (i = 0; i < NUM_STARS; i++) {
        star_x[i] = (LONG)(srand_next() % (2 * STAR_RANGE)) - STAR_RANGE;
        star_y[i] = (LONG)(srand_next() % (2 * STAR_RANGE)) - STAR_RANGE;
        star_z[i] = (LONG)(srand_next() % (2 * STAR_RANGE)) - STAR_RANGE;
        star_pen[i] = (UBYTE)(PEN_STAR_BASE + (srand_next() % PEN_STAR_CT));
    }
    g_stars_built = 1;
}

void e3d_render_frame(struct RastPort *rp,
                      const Camera *cam,
                      const Entity *objs, int num_objs)
{
    int i;
    int nobj;
    int order[64];
    LONG cz_key[64];
    if (!g_stars_built) build_stars();

    /* Wipe backdrop. */
    SetAPen(rp, PEN_SPACE);
    RectFill(rp, 0, 0, g_w - 1, g_h - 1);

    build_view_matrix(cam);

    /* Draw stars — project each, WritePixel if in front. Stars
     * wrap in a cube of side 2 * STAR_RANGE around the camera so
     * they never run out. */
    for (i = 0; i < NUM_STARS; i++) {
        LONG rx = star_x[i] - cam->x;
        LONG ry = star_y[i] - cam->y;
        LONG rz = star_z[i] - cam->z;
        LONG cx;
        LONG cy;
        LONG cz;
        LONG sxp;
        LONG syp;
        /* Wrap into the cube centred on the camera. */
        while (rx >  STAR_RANGE) rx -= 2 * STAR_RANGE;
        while (rx < -STAR_RANGE) rx += 2 * STAR_RANGE;
        while (ry >  STAR_RANGE) ry -= 2 * STAR_RANGE;
        while (ry < -STAR_RANGE) ry += 2 * STAR_RANGE;
        while (rz >  STAR_RANGE) rz -= 2 * STAR_RANGE;
        while (rz < -STAR_RANGE) rz += 2 * STAR_RANGE;
        cx = (g_view_rot.m[0][0] * rx + g_view_rot.m[0][1] * ry + g_view_rot.m[0][2] * rz) >> FP;
        cy = (g_view_rot.m[1][0] * rx + g_view_rot.m[1][1] * ry + g_view_rot.m[1][2] * rz) >> FP;
        cz = (g_view_rot.m[2][0] * rx + g_view_rot.m[2][1] * ry + g_view_rot.m[2][2] * rz) >> FP;
        if (cz < 64) continue;
        sxp = g_cx + (cx * g_fov) / cz;
        syp = g_cy - (cy * g_fov) / cz;
        if (sxp < 0 || sxp >= g_w) continue;
        if (syp < 0 || syp >= g_h) continue;
        SetAPen(rp, star_pen[i]);
        WritePixel(rp, (WORD)sxp, (WORD)syp);
    }

    /* Sort objects back-to-front by camera-space z (rough — using
     * centre distance is fine for now since they're solid poly and
     * far enough apart in typical play). */
    nobj = 0;
    for (i = 0; i < num_objs && i < 64; i++) {
        LONG cx, cy, cz;
        if (!objs[i].active) continue;
        view_transform(objs[i].x, objs[i].y, objs[i].z, &cx, &cy, &cz);
        if (cz < 32) continue;
        order[nobj] = i;
        cz_key[nobj] = cz;
        nobj++;
    }
    for (i = 1; i < nobj; i++) {
        int k = order[i];
        LONG kz = cz_key[i];
        int j = i - 1;
        while (j >= 0 && cz_key[j] < kz) {
            order[j+1] = order[j];
            cz_key[j+1] = cz_key[j];
            j--;
        }
        order[j+1] = k;
        cz_key[j+1] = kz;
    }

    for (i = 0; i < nobj; i++) draw_one_object(rp, &objs[order[i]]);
}
