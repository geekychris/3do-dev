/*
 * The course on screen (IsoCamera.cs): an orthographic camera at any yaw,
 * pitch and zoom, so the 3D is real - every visible face is projected each
 * frame and drawn by the cel engine as a flat quad.
 *
 * Orthographic means no divides: a vertex is three dot products. Faces
 * come in render cells (8 x 8 units) with bounding spheres, so only the
 * cells near the view are looked at; their vertices are transformed once a
 * frame (cached by frame number); faces pointing away are dropped by the
 * winding of their projected corners; the rest are bucket-sorted by their
 * farthest corner and drawn far to near. The course compiler cut every
 * face to <= 3 units, which keeps that sort honest.
 *
 * Colours were lit offline (tools/courses.py) at six fog depths; the few
 * things that move and turn (crushers, sweeper arms) are lit here with the
 * same model. Round things - marbles, steel balls, blobs, balloons - are
 * shaded sphere sprites drawn at startup.
 */
#include <stdlib.h>
#include "rs.h"
#include "cels.h"

int render_stats_quads, render_stats_cels;

/* ---- sorting ---- */
typedef struct {
    short next;
    unsigned char type;             /* 0 quad, 1 sprite */
    unsigned char tex;
    unsigned short col;
    unsigned long pixc;
    long a[8];
} DrawItem;
#define MAXITEMS 1000
#define NBUCKET  256
static DrawItem items[MAXITEMS];
static short head[NBUCKET];
static int nitems;

/* the item last handed out by new_item(), so push() needn't subtract pointers
 * (a divide by the item size on an ARM60) */
static int last_item;

static void push(long key, DrawItem *it)
{
    long b = (key + 64L * FX) >> 11;                        /* half-unit buckets over +-64 units */
    if (b < 0) b = 0;
    if (b >= NBUCKET) b = NBUCKET - 1;
    it->next = head[b];
    head[b] = (short)last_item;
}

static DrawItem *new_item(void)
{
    if (nitems >= MAXITEMS) return 0;
    last_item = nitems;
    return &items[nitems++];
}

/* ---- lighting (tools/courses.py shade(), for the things that turn) ---- */
static const short srgb_thr[31] = {
    5, 16, 30, 50, 76, 109, 148, 195, 250, 313, 385, 465, 554, 652, 759, 877, 1004, 1142, 1289, 1448,
    1617, 1798, 1989, 2192, 2407, 2633, 2871, 3122, 3384, 3660, 3947
};
static unsigned char srgb5[1025];

static void srgb_init(void)
{
    int i, k = 0;
    for (i = 0; i <= 1024; i++) {
        while (k < 31 && (i << 2) >= srgb_thr[k]) k++;
        srgb5[i] = (unsigned char)k;
    }
}

static int lin5(long v) { v >>= 2; if (v < 0) v = 0; if (v > 1024) v = 1024; return srgb5[v]; }

typedef struct { short a[3], e[3]; short metal; } Look;
static const Look L_DANGER = { { 1954, 136, 111 }, { 163, 10, 6 }, 1434 };
static const Look L_PROP = { { 489, 544, 733 }, { 0, 0, 0 }, 2253 };

static unsigned short shade(const Look *m, long nx, long ny, long nz)
{
    static const short fill[3] = { 700, 1081, 2843 };     /* the fill light's blue, linear */
    long k = (nx * 1261 + ny * 3228 - nz * 2184) >> 12;   /* n . key */
    long f = (-nx * 1316 + ny * 1401 + nz * 3617) >> 12;  /* n . fill */
    int c[3], i;
    if (k < 0) k = 0;
    if (f < 0) f = 0;
    for (i = 0; i < 3; i++) {
        long light = 246 + ((2867 * k) >> 12) + ((((1229 * fill[i]) >> 12) * f) >> 12) + (ny > 0 ? ny / 2 : 0);
        long v = (((m->a[i] * light) >> 12) * (FX - ((3 * m->metal) >> 2)) >> 12) + ((m->a[i] * ((m->metal * 901) >> 12)) >> 12) + m->e[i];
        c[i] = lin5(v);
    }
    i = (c[0] << 10) | (c[1] << 5) | c[2];
    return (unsigned short)(i ? i : 1);
}

/* ---- sphere sprites ---- */
static int tex_id[SP_COUNT];

static void make_sphere(int look, const short *alb, const short *emis, int metal, int rim)
{
    enum { S = 32 };
    unsigned short *px = (unsigned short *)malloc(S * S * 2);
    int i, j, ch;
    if (!px) { tex_id[look] = -1; return; }
    for (j = 0; j < S; j++)
        for (i = 0; i < S; i++) {
            long nx = (i * 2 + 1 - S) * 4096L / S, ny = -(j * 2 + 1 - S) * 4096L / S;
            long d2 = (nx * nx + ny * ny) >> 12, nz, k, spec;
            unsigned short v = 0;
            if (d2 < 4096) {
                int c[3];
                nz = (long)isqrt((unsigned long)((4096 - d2) << 12));     /* towards the viewer */
                /* the key light seen from the default camera: (0.31, 0.34, 0.89) */
                k = (nx * 1261 + ny * 1388 + nz * 3641) >> 12;
                if (k < 0) k = 0;
                /* its highlight */
                spec = k > 3700 ? (k - 3700) * 10 : 0;
                if (spec > 4096) spec = 4096;
                for (ch = 0; ch < 3; ch++) {
                    long light = 330 + ((2600 * k) >> 12) + (ny > 0 ? ny / 4 : 0);
                    long val = (alb[ch] * light) >> 12;
                    val += metal ? (spec * 3800) >> 12 : (spec * 900) >> 12;
                    val += emis[ch];
                    c[ch] = lin5(val);
                    if (rim && d2 > 3300) c[ch] = c[ch] * 2 / 3;
                }
                v = (unsigned short)((c[0] << 10) | (c[1] << 5) | c[2]);
                if (!v) v = 1;
            }
            px[j * S + i] = v;
        }
    tex_id[look] = cels_tex(S, S, px);
}

int render_init(void)
{
    static const short alb[8][3] = {
        { 2910, 3066, 3474 }, { 4096, 1954, 603 }, { 364, 388, 516 }, { 388, 3559, 544 },
        { 1078, 3645, 4096 }, { 1078, 2614, 3645 }, { 4096, 489, 1610 }, { 4096, 1610, 261 }
    };
    static const short emis[8][3] = {
        { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 35, 603, 48 },
        { 300, 600, 700 }, { 16, 80, 163 }, { 544, 20, 163 }, { 1305, 300, 20 }
    };
    static const char metal[8] = { 1, 1, 1, 0, 0, 0, 0, 0 };
    int i;
    unsigned short *px;
    srgb_init();
    if (!cels_init()) return 0;
    for (i = 0; i < 8; i++) make_sphere(i, alb[i], emis[i], metal[i], i == SP_GHOST);
    /* shadow: a disc of anything non-zero (the cel's PIXC darkens) */
    px = (unsigned short *)malloc(16 * 16 * 2);
    if (px) {
        int x, y;
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
                px[y * 16 + x] = (unsigned short)(((2 * x - 15) * (2 * x - 15) + (2 * y - 15) * (2 * y - 15) < 225) ? 1 : 0);
        tex_id[SP_SHADOW] = cels_tex(16, 16, px);
    }
    px = (unsigned short *)malloc(4 * 4 * 2);
    if (px) {
        for (i = 0; i < 16; i++) px[i] = (unsigned short)((24 << 10) | (16 << 5) | 6);
        tex_id[SP_SPARK] = cels_tex(4, 4, px);
    }
    return 1;
}

/* ---- the camera ---- */

void cam_basis(Cam *c)
{
    long sy = isin(c->yaw) >> 2, cy = icos(c->yaw) >> 2, sp = isin(c->pitch) >> 2, cp = icos(c->pitch) >> 2;
    /* Unity's Euler(pitch, yaw, 0) */
    c->r[0] = cy; c->r[1] = 0; c->r[2] = -sy;
    c->u[0] = (sp * sy) >> 12; c->u[1] = cp; c->u[2] = (sp * cy) >> 12;
    c->f[0] = (cp * sy) >> 12; c->f[1] = -sp; c->f[2] = (cp * cy) >> 12;
    c->scale = (long)((120L << 16) / (c->size >> 4));        /* px per unit, Q8 */
}

static Cam *cam;
static long cx16, cy16;                                     /* viewport centre, 16.16 */

static void proj(V3 p, long *sx, long *sy, long *dz)
{
    long rx = (p.x - cam->focus.x) >> 2, ry = (p.y - cam->focus.y) >> 2, rz = (p.z - cam->focus.z) >> 2;
    long x = (rx * cam->r[0] + rz * cam->r[2]) >> 10;
    long y = (rx * cam->u[0] + ry * cam->u[1] + rz * cam->u[2]) >> 10;
    *dz = (rx * cam->f[0] + ry * cam->f[1] + rz * cam->f[2]) >> 10;
    *sx = cx16 + ((x * cam->scale) >> 4);
    *sy = cy16 - ((y * cam->scale) >> 4);
}

int render_project(const Cam *c, V3 p, long *sx, long *sy)
{
    long dz;
    Cam *keep = cam;
    cam = (Cam *)c;
    cx16 = ((long)c->x0 + c->w / 2) << 16;
    cy16 = 120L << 16;
    proj(p, sx, sy, &dz);
    cam = keep;
    return 1;
}

/* ---- per-frame vertex cache ---- */
#define MAXV 2600
static long vsx[MAXV], vsy[MAXV], vdz[MAXV];
static w32 vstamp[MAXV];
static w32 stamp;

static unsigned short fog_col(const uw32 *col, long key)
{
    long lv = (key - 4 * FX) >> 14;                         /* a level every 4 units */
    unsigned long w;
    if (lv < 0) lv = 0;
    if (lv > 5) lv = 5;
    w = col[lv >> 1];
    return (unsigned short)((lv & 1) ? (w & 0xFFFF) : (w >> 16));
}

/* a quad from four projected corners; returns 1 if it faced the camera */
static int quad4(const long *x, const long *y, long key, unsigned short col, unsigned long pixc)
{
    DrawItem *it;
    long cr = ((x[1] - x[0]) >> 8) * ((y[3] - y[0]) >> 8) - ((y[1] - y[0]) >> 8) * ((x[3] - x[0]) >> 8);
    if (cr <= 0) return 0;
    it = new_item();
    if (!it) return 0;
    it->type = 0;
    it->col = col;
    it->pixc = pixc;
    it->a[0] = x[0]; it->a[1] = y[0]; it->a[2] = x[1]; it->a[3] = y[1];
    it->a[4] = x[2]; it->a[5] = y[2]; it->a[6] = x[3]; it->a[7] = y[3];
    push(key, it);
    return 1;
}

static void sprite(int look, V3 p, fix r, long key_bias, unsigned long pixc)
{
    long sx, sy, dz, h;
    DrawItem *it;
    if (tex_id[look] < 0) return;
    proj(p, &sx, &sy, &dz);
    h = (r * cam->scale) >> 4;                               /* Q12 * Q8 >> 4 = 16.16 */
    if (sx + h < (long)cam->x0 << 16 || sx - h > (long)(cam->x0 + cam->w) << 16 || sy + h < 0 || sy - h > 240L << 16)
        return;
    it = new_item();
    if (!it) return;
    it->type = 1;
    it->tex = (unsigned char)tex_id[look];
    it->pixc = pixc;
    it->a[0] = sx; it->a[1] = sy; it->a[2] = h; it->a[3] = h;
    push(dz - key_bias, it);
}

/* a box (yaw-only), lit here, for the crushers and sweeper arms */
static void box(V3 c, fix hx, fix hy, fix hz, long cs, long sn, const Look *m)
{
    static const signed char face[6][4][3] = {
        { { 1, -1, -1 }, { 1, -1, 1 }, { 1, 1, 1 }, { 1, 1, -1 } },
        { { -1, -1, -1 }, { -1, 1, -1 }, { -1, 1, 1 }, { -1, -1, 1 } },
        { { -1, 1, -1 }, { 1, 1, -1 }, { 1, 1, 1 }, { -1, 1, 1 } },
        { { -1, -1, -1 }, { -1, -1, 1 }, { 1, -1, 1 }, { 1, -1, -1 } },
        { { -1, -1, 1 }, { -1, 1, 1 }, { 1, 1, 1 }, { 1, -1, 1 } },
        { { -1, -1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { -1, 1, -1 } }
    };
    static const signed char nrm[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    int f, k;
    cs >>= 2; sn >>= 2;                                      /* Q14 -> Q12 */
    for (f = 0; f < 6; f++) {
        long x[4], y[4], key = -0x7FFFFFFF;
        long nx = (nrm[f][0] * cs + nrm[f][2] * sn), nz = (-nrm[f][0] * sn + nrm[f][2] * cs);
        for (k = 0; k < 4; k++) {
            long lx = face[f][k][0] * hx, ly = face[f][k][1] * hy, lz = face[f][k][2] * hz, dz;
            V3 w;
            w.x = c.x + ((lx * cs + lz * sn) >> 12);
            w.y = c.y + ly;
            w.z = c.z + ((-lx * sn + lz * cs) >> 12);
            proj(w, &x[k], &y[k], &dz);
            if (dz > key) key = dz;
        }
        quad4(x, y, key, shade(m, nx, nrm[f][1] * FX, nz), 0);
    }
}

/* ---- the scene ---- */

void render_begin(void)
{
    cels_begin();
    nitems = 0;
    render_stats_quads = 0;
}

/* the void the courses float in: the Unity camera's background */
void render_bg(void)
{
    cels_quad(0, 0, 320L << 16, 0, 320L << 16, 240L << 16, 0, 240L << 16, (unsigned short)((1 << 10) | (1 << 5) | 3), 0);
}

int render_mark(void) { return cels_mark(); }
void *render_segment(int from, int to) { return cels_segment(from, to); }

/* darken a screen rectangle: the HUD's translucent boxes; k eighths of what's there */
void render_shade(int x0, int y0, int x1, int y1, int k)
{
    unsigned long h = 0x8000UL | ((unsigned long)(k - 1) << 10) | 0x0300UL;
    cels_quad((long)x0 << 16, (long)y0 << 16, (long)x1 << 16, (long)y0 << 16, (long)x1 << 16, (long)y1 << 16,
              (long)x0 << 16, (long)y1 << 16, 1, (h << 16) | h);
}

static void course_faces(void)
{
    const Grid *g = &C.rgrid;
    long cell, k, i, gx, gz, gx0, gx1, gz0, gz1, reach;
    long marg = (long)cam->w << 15, vmarg = 120L << 16;
    /* only cells within reach of what the camera looks at: the view is
     * about 2.7 x size across, and lower decks further along it show too */
    reach = (cam->size * 3 + 40 * FX) >> 15;                 /* in 8-unit cells */
    gx = (cam->focus.x - g->x0) >> 15;
    gz = (cam->focus.z - g->z0) >> 15;
    gx0 = gx - reach; gx1 = gx + reach; gz0 = gz - reach; gz1 = gz + reach;
    if (gx0 < 0) gx0 = 0;
    if (gz0 < 0) gz0 = 0;
    if (gx1 > g->nx - 1) gx1 = g->nx - 1;
    if (gz1 > g->nz - 1) gz1 = g->nz - 1;
    for (gz = gz0; gz <= gz1; gz++)
    for (gx = gx0; gx <= gx1; gx++) {
        const w32 *sp;
        long sx, sy, dz, rr;
        V3 c;
        cell = gz * g->nx + gx;
        sp = g->sphere + cell * 4;
        if (g->off[cell] == g->off[cell + 1]) continue;
        c.x = sp[0]; c.y = sp[1]; c.z = sp[2];
        proj(c, &sx, &sy, &dz);
        rr = (sp[3] * cam->scale) >> 4;
        if (sx + rr < cx16 - marg || sx - rr > cx16 + marg || sy + rr < cy16 - vmarg || sy - rr > cy16 + vmarg)
            continue;
        for (k = g->off[cell]; k < g->off[cell + 1]; k++) {
            const RQuad *q = &C.q[g->idx[k]];
            long x[4], y[4], key = -0x7FFFFFFF;
            int group;
            long ydrop = 0, xj = 0;
            /* facing away? (orthographic: the normal against the view) */
            {
                long nw = q->nrm, nx = (nw << 2) >> 22, ny = (nw << 12) >> 22, nz = (nw << 22) >> 22;
                if (nx * cam->f[0] + ny * cam->f[1] + nz * cam->f[2] >= 0) continue;
            }
            group = (int)(q->flags >> 8);
            if (group) {
                const Crumble *cr = &Wd.crumble[group - 1];
                if (cr->state == 2) {
                    if (cr->fall > 10 * FX) continue;
                    ydrop = (cr->fall * cam->scale) >> 4;
                } else if (cr->state == 1)
                    xj = ((Wd.clock & 1) ? 1L : -1L) << 15;      /* shiver */
            }
            for (i = 0; i < 4; i++) {
                long vi = q->v[i];
                if (vstamp[vi] != stamp) {
                    const V3 *p = &C.v[vi];
                    long rx = (p->x - cam->focus.x) >> 2, ry = (p->y - cam->focus.y) >> 2, rz = (p->z - cam->focus.z) >> 2;
                    vsx[vi] = cx16 + ((((rx * cam->r[0] + rz * cam->r[2]) >> 10) * cam->scale) >> 4);
                    vsy[vi] = cy16 - ((((rx * cam->u[0] + ry * cam->u[1] + rz * cam->u[2]) >> 10) * cam->scale) >> 4);
                    vdz[vi] = (rx * cam->f[0] + ry * cam->f[1] + rz * cam->f[2]) >> 10;
                    vstamp[vi] = stamp;
                }
                x[i] = vsx[vi] + xj;
                y[i] = vsy[vi] + ((ydrop * cam->u[1]) >> 12);
                if (vdz[vi] > key) key = vdz[vi];
            }
            /* decals (acid, fans, boost strips) lie on a deck: after it */
            if (quad4(x, y, (q->flags & 255) == 2 ? key - 3 * FX : key, fog_col(q->col, key), 0)) render_stats_quads++;
        }
    }
}

static long dv[3 * 256];

static void decor(void)
{
    int d;
    long t = Wd.clock * 82;                                   /* seconds, Q12 */
    for (d = 0; d < C.ndecor; d++) {
        const Decor *o = &C.deco[d];
        long sx, sy, dz, i, k, bob, yaw, ang, cs, sn, sc = o->scale, rr;
        V3 pos;
        pos.x = o->x; pos.y = o->y; pos.z = o->z;
        proj(pos, &sx, &sy, &dz);
        rr = ((12L * sc) * cam->scale) >> 4;                  /* about 12 units across at most */
        if (sx + rr < (long)cam->x0 << 16 || sx - rr > (long)(cam->x0 + cam->w) << 16 ||
            sy + rr < 0 || sy - rr > 240L << 16)
            continue;
        /* FloatBob: up and down by sin(t * speed + phase) * amp, turning about up */
        ang = ((((t >> 6) * (o->speed >> 6)) % 25736L) * 10430L >> 12) + ((o->phase * 10430L) >> 12);
        bob = (isin(ang) * (o->amp >> 2)) >> 12;
        yaw = ((o->yaw + (o->spin >> 6) * (t >> 6)) * 2) / 45;
        cs = icos(yaw) >> 2; sn = isin(yaw) >> 2;
        pos.y += bob;
        if (o->nv > 256) continue;
        for (i = 0; i < o->nv; i++) {
            V3 w;
            long lx = (o->v[i].x * sc) >> 12, ly = (o->v[i].y * sc) >> 12, lz = (o->v[i].z * sc) >> 12;
            w.x = pos.x + ((lx * cs + lz * sn) >> 12);
            w.y = pos.y + ly;
            w.z = pos.z + ((-lx * sn + lz * cs) >> 12);
            proj(w, &dv[i * 3], &dv[i * 3 + 1], &dv[i * 3 + 2]);
        }
        for (i = 0; i < o->nq; i++) {
            const w32 *q = o->q + i * 7;
            long x[4], y[4], key = -0x7FFFFFFF;
            for (k = 0; k < 4; k++) {
                x[k] = dv[q[k] * 3];
                y[k] = dv[q[k] * 3 + 1];
                if (dv[q[k] * 3 + 2] > key) key = dv[q[k] * 3 + 2];
            }
            quad4(x, y, key, fog_col((const uw32 *)(q + 4), key), 0);
        }
        for (i = 0; i < o->ns; i++) {
            const w32 *s = o->s + i * 5;
            V3 w;
            long lx = (s[0] * sc) >> 12, lz = (s[2] * sc) >> 12;
            w.x = pos.x + ((lx * cs + lz * sn) >> 12);
            w.y = pos.y + ((s[1] * sc) >> 12);
            w.z = pos.z + ((-lx * sn + lz * cs) >> 12);
            sprite((int)s[4] < SP_COUNT ? (int)s[4] : SP_CRYSTAL, w, (s[3] * sc) >> 12, 0, 0);
        }
    }
}

static void shadow(V3 p, fix r)
{
    fix gy;
    long sx, sy, dz, hx, hy;
    DrawItem *it;
    V3 g;
    if (tex_id[SP_SHADOW] < 0 || !phys_ground_below(p, &gy)) return;
    if (p.y - gy > 14 * FX) return;
    g = p;
    g.y = gy + 41;
    proj(g, &sx, &sy, &dz);
    hx = (((r * 7) / 8) * cam->scale) >> 4;
    hy = (hx * (-cam->f[1])) >> 12;                          /* the ground seen at the pitch */
    it = new_item();
    if (!it) return;
    it->type = 1;
    it->tex = (unsigned char)tex_id[SP_SHADOW];
    it->pixc = PIXC_SHADOW;
    it->a[0] = sx; it->a[1] = sy; it->a[2] = hx; it->a[3] = hy > 0 ? hy : 1;
    push(dz - 205, it);
}

void render_scene(Cam *c, const RBall *b, int nb)
{
    int i;
    render_bg();
    cam = c;
    cam_basis(c);
    cx16 = ((long)c->x0 + c->w / 2) << 16;
    cy16 = 120L << 16;
    stamp++;
    for (i = 0; i < NBUCKET; i++) head[i] = -1;
    course_faces();
    decor();
    for (i = 0; i < C.ncrush && i < 8; i++) {
        const Crusher *k = &C.crush[i];
        V3 p;
        p.x = k->x; p.z = k->z;
        p.y = k->y + ((k->lift * Wd.crush_h[i]) >> 12);
        box(p, k->w / 2, 3686, k->w / 2, k->cs, k->sn, &L_DANGER);
    }
    for (i = 0; i < C.nsweep && i < 8; i++) {
        const Sweeper *s = &C.sweep[i];
        long a = s->yaw * 2 / 45 + (Wd.sweep_angle[i] >> 8);
        V3 p;
        p.x = s->x; p.y = s->y; p.z = s->z;
        box(p, s->len / 2, 717, 922, icos(a), isin(a), &L_DANGER);
    }
    for (i = 0; i < Wd.nenemy; i++) {
        const Enemy *e = &Wd.enemy[i];
        if (e->kind == 0) shadow(e->b.p, e->b.r);
        sprite(e->kind ? SP_BLOB : SP_STEEL, e->b.p, e->b.r, 410, 0);
    }
    for (i = 0; i < nb; i++) {
        if (b[i].shadow) shadow(b[i].p, b[i].r);
        sprite(b[i].look, b[i].p, b[i].r, 410, b[i].pixc);
    }
    /* far to near */
    for (i = NBUCKET - 1; i >= 0; i--) {
        short k;
        for (k = head[i]; k >= 0; k = items[k].next) {
            DrawItem *it = &items[k];
            if (it->type == 0)
                cels_quad(it->a[0], it->a[1], it->a[2], it->a[3], it->a[4], it->a[5], it->a[6], it->a[7], it->col, it->pixc);
            else
                cels_sprite(it->tex, it->a[0], it->a[1], it->a[2], it->a[3], it->pixc);
        }
    }
    nitems = 0;
}

void render_flash(int r, int g, int b)
{
    unsigned short col = (unsigned short)((r << 10) | (g << 5) | b);
    if (!col) return;
    cels_quad(0, 0, 320L << 16, 0, 320L << 16, 240L << 16, 0, 240L << 16, col, PIXC_ADD);
}

void *render_end(void)
{
    render_stats_cels = cels_count();
    return cels_list();
}
