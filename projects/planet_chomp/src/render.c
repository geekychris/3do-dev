/*
 * The planet in cels. Each frame:
 *  - camera basis as in PlanetCamera.cs (hover above the focus, tilted back,
 *    looking at a point just ahead of it; up = the transported tangent)
 *  - the planet's silhouette as a fan of triangles
 *  - walls on the camera's side of the horizon: the side facing the camera
 *    and the top, as quads; crumbs as small squares; the player
 *  - painter's order by depth (bucket sort), so the cel engine draws far
 *    to near
 */
#include "pc.h"

#define FOCAL     246L                /* px: 52 deg vertical field of view on 240 lines */
#define NEAR_Z    Q8(0.6)
#define TILT      Q8(0.58)            /* fractions as Q8 too */
#define LOOKAHEAD Q8(0.08)
#define CRUMB_H   Q8(0.5)
#define CRUMB_R   Q8(0.13)
#define PLAYER_R  Q8(0.42)
#define WALL_MIDH Q8(0.37)
#define BUCKETS   256

int rd_stats_walls, rd_stats_cels;
int tex_key_id = -1;

static V3  campos;                    /* Q8 */
static V3  right, cup, fwd;           /* Q14 */

/* FOCAL/z in 16.16 for z (Q8) in steps of 4, so projecting needs no
 * divide (the ARM60 has none): 4096 entries cover 64 world units */
#define RECIP_N 4096
static long recip[RECIP_N];

/* one projected point: 16.16 display coords; 0 if behind the near plane */
static int project_xyz(fix px, fix py, fix pz, long *sx, long *sy, fix *zout)
{
    fix rx = px - campos.x, ry = py - campos.y, rz = pz - campos.z;
    fix zc = (rx * fwd.x + ry * fwd.y + rz * fwd.z) >> 14;
    fix xc, yc, s;
    if (zc < NEAR_Z)
        return 0;
    xc = (rx * right.x + ry * right.y + rz * right.z) >> 14;
    yc = (rx * cup.x + ry * cup.y + rz * cup.z) >> 14;
    s = (zc >> 2) < RECIP_N ? recip[zc >> 2] : recip[RECIP_N - 1];
    *sx = (160L << 16) + xc * s;
    *sy = (120L << 16) - yc * s;
    if (zout) *zout = zc;
    return 1;
}

#define project(P_, SX_, SY_, Z_) project_xyz((P_).x, (P_).y, (P_).z, SX_, SY_, Z_)

static unsigned short shade(int r, int g, int b, fix k)   /* k: Q8 brightness */
{
    r = (int)((r * k) >> 8); g = (int)((g * k) >> 8); b = (int)((b * k) >> 8);
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    return RGB15(r, g, b);
}

/* draw list */
#define MAX_ITEMS 1400
enum { IT_WALL, IT_CRUMB, IT_SPRITE, IT_NEST };
static long  it_half[MAX_ITEMS];
static int   it_tex[MAX_ITEMS];
static long  nest_x[9], nest_y[9];        /* nest pad: centre + 8 rim points */
static short it_type[MAX_ITEMS], it_idx[MAX_ITEMS], it_next[MAX_ITEMS];
static long  it_x[MAX_ITEMS][4], it_y[MAX_ITEMS][4];    /* screen corners */
static long  it_x2[MAX_ITEMS][4], it_y2[MAX_ITEMS][4];  /* walls: the top */
static unsigned short it_c[MAX_ITEMS], it_c2[MAX_ITEMS];
static short bucket[BUCKETS];
static int   nitems;

static void add_item(int type, int idx, fix z)
{
    int b;
    if (nitems >= MAX_ITEMS)
        return;
    b = (int)(z >> 5);                              /* 1/8 unit buckets */
    if (b < 0) b = 0;
    if (b >= BUCKETS) b = BUCKETS - 1;
    it_type[nitems] = (short)type;
    it_idx[nitems] = (short)idx;
    it_next[nitems] = bucket[b];
    bucket[b] = (short)nitems;
    nitems++;
}

static void square(int item, V3 p, fix radius, fix z)
{
    long cx, cy, r;
    fix zz;
    if (!project(p, &cx, &cy, &zz))
        return;
    (void)z;
    r = radius * ((zz >> 2) < RECIP_N ? recip[zz >> 2] : recip[RECIP_N - 1]);   /* 16.16 px */
    it_x[item][0] = cx - r; it_y[item][0] = cy - r;
    it_x[item][1] = cx + r; it_y[item][1] = cy - r;
    it_x[item][2] = cx + r; it_y[item][2] = cy + r;
    it_x[item][3] = cx - r; it_y[item][3] = cy + r;
}

/* the starfield: one 160 x 120 texture drawn at 2x behind everything */
#define SKY_W 160
#define SKY_H 120
static unsigned short sky[SKY_W * SKY_H];
static int sky_tex = -1;

void rd_init(void)
{
    int i;
    unsigned long r = 7;
    for (i = 0; i < RECIP_N; i++)
        recip[i] = (FOCAL << 16) / (i * 4 + 2);   /* px per Q8 unit, 16.16 */
    for (i = 0; i < SKY_W * SKY_H; i++) sky[i] = RGB15(4, 6, 16);
    for (i = 0; i < 90; i++) {
        int b, x, y;
        r = r * 1103515245UL + 12345UL; x = (int)((r >> 16) % SKY_W);
        r = r * 1103515245UL + 12345UL; y = (int)((r >> 16) % SKY_H);
        r = r * 1103515245UL + 12345UL; b = 110 + (int)((r >> 16) % 146);
        sky[y * SKY_W + x] = RGB15(b, b, b + (b > 200 ? 0 : 20));
    }
    sky_tex = pc_tex_create(SKY_W, SKY_H, sky);
}

int rd_project(V3 p, long *sx, long *sy) { return project(p, sx, sy, 0); }

void rd_frame(const Camera *cam, const unsigned char *crumb, const unsigned char *key,
              const RSprite *spr, int nspr, int flags)
{
    fix h = cam->height, d2, dcam, c_r, rr;
    V3 target, f = cam->focus, up = cam->up, chat;
    int i, c, b;

    /* ---- camera ---- */
    campos = v3_sub(v3_scale14(f, PLANET_R + h), v3_scale14(up, (h * TILT) >> 8));
    target = v3_add(v3_scale14(f, PLANET_R), v3_scale14(up, (h * LOOKAHEAD) >> 8));
    fwd = v3_norm14(v3_sub(target, campos));
    right = v3_norm14(v3_cross14(up, fwd));
    cup = v3_cross14(fwd, right);

    pc_cels_begin();
    /* space and stars */
    if (sky_tex >= 0)
        pc_sprite(sky_tex, 160L << 16, 120L << 16, 160L << 16);   /* 160 px half width: 2x */
    else
        pc_quad(0, 0, 320L << 16, 0, 320L << 16, 240L << 16, 0, 240L << 16, RGB15(4, 6, 16));

    /* ---- planet silhouette: circle where sight lines graze the sphere ---- */
    d2 = (campos.x >> 4) * (campos.x >> 4) + (campos.y >> 4) * (campos.y >> 4) +
         (campos.z >> 4) * (campos.z >> 4);         /* (Q8/16)^2 */
    dcam = (fix)isqrt32((unsigned long)d2) << 4;    /* |C|, Q8 */
    chat = v3_norm14(campos);
    c_r = (PLANET_R * PLANET_R) / dcam;             /* centre offset R^2/D, Q8 */
    rr = (fix)isqrt32((unsigned long)(PLANET_R * PLANET_R - c_r * c_r));   /* radius, Q8 */
    {
        static const short cs[25][2] = {
            {16384,0},{15827,4240},{14189,8192},{11585,11585},{8192,14189},{4240,15827},
            {0,16384},{-4240,15827},{-8192,14189},{-11585,11585},{-14189,8192},{-15827,4240},
            {-16384,0},{-15827,-4240},{-14189,-8192},{-11585,-11585},{-8192,-14189},{-4240,-15827},
            {0,-16384},{4240,-15827},{8192,-14189},{11585,-11585},{14189,-8192},{15827,-4240},
            {16384,0}
        };
        V3 e1 = v3_norm14(v3_sub(right, v3_scale14(chat, v3_dot14(right, chat))));
        V3 e2 = v3_cross14(chat, e1), centre = v3_scale14(chat, c_r);
        long px[25], py[25], ox, oy;
        int ok = 1;
        for (i = 0; i < 25 && ok; i++) {
            V3 p = v3_add(centre, v3_add(v3_scale14(e1, (rr * cs[i][0]) >> 14),
                                         v3_scale14(e2, (rr * cs[i][1]) >> 14)));
            ok = project(p, &px[i], &py[i], 0);
        }
        if (ok && project(v3_scale14(f, PLANET_R), &ox, &oy, 0))
            for (i = 0; i < 24; i++)
                pc_quad(ox, oy, px[i], py[i], px[i + 1], py[i + 1], px[i + 1], py[i + 1],
                        RGB15(52, 56, 84));
    }

    /* ---- collect walls, crumbs, player ---- */
    for (b = 0; b < BUCKETS; b++) bucket[b] = -1;
    nitems = 0;
    rd_stats_walls = 0;
    for (i = 0; i < mz_nwalls; i++) {
        Wall *w = &mz_wall[i];
        fix z, facing, k;
        long sx[8], sy[8];
        int v, it, plus;
        static const int sp[4] = { 1, 2, 6, 5 }, sm[4] = { 0, 3, 7, 4 }, tp[4] = { 4, 5, 6, 7 };
        const int *sv;
        /* far side of the horizon: d . C must exceed ~R */
        if (((w->mid.x * campos.x + w->mid.y * campos.y + w->mid.z * campos.z) >> 14) < PLANET_R - Q8(0.6))
            continue;
        {
            long mx, my;
            if (!project(w->midp, &mx, &my, &z)) continue;
        }
        /* which long side faces the camera: only it and the top are drawn,
         * so only those 6 corners are projected */
        facing = w->side.x * (campos.x - w->midp.x) + w->side.y * (campos.y - w->midp.y) +
                 w->side.z * (campos.z - w->midp.z);
        plus = facing > 0;
        sv = plus ? sp : sm;
        for (v = 0; v < 4; v++)
            if (!project(w->v[sv[v]], &sx[sv[v]], &sy[sv[v]], 0)) break;
        if (v < 4) continue;
        if (!project(w->v[plus ? 4 : 5], &sx[plus ? 4 : 5], &sy[plus ? 4 : 5], 0) ||
            !project(w->v[plus ? 7 : 6], &sx[plus ? 7 : 6], &sy[plus ? 7 : 6], 0))
            continue;
        it = nitems;
        add_item(IT_WALL, i, z);
        if (it == nitems) break;
        for (v = 0; v < 4; v++) {
            it_x[it][v] = sx[sv[v]]; it_y[it][v] = sy[sv[v]];
            it_x2[it][v] = sx[tp[v]]; it_y2[it][v] = sy[tp[v]];
        }
        k = v3_dot14(w->side, fwd);
        if (k < 0) k = -k;
        k = 120 + ((k * 150) >> 14);
        if (flags & RD_WALL_FLASH) {
            it_c[it] = RGB15(235, 240, 255);
            it_c2[it] = RGB15(255, 255, 255);
        } else {
            it_c[it] = shade(50, 80, 220, k);
            it_c2[it] = RGB15(130, 180, 255);
        }
        rd_stats_walls++;
    }
    for (c = 0; c < CELLS; c++) {
        fix z;
        int it;
        V3 p;
        if (!crumb[c]) continue;
        if (((mz_dir[c].x * campos.x + mz_dir[c].y * campos.y + mz_dir[c].z * campos.z) >> 14) < PLANET_R)
            continue;
        p = v3_scale14(mz_dir[c], PLANET_R + CRUMB_H);
        {
            long px, py;
            if (!project(p, &px, &py, &z)) continue;
        }
        it = nitems;
        add_item(IT_CRUMB, c, z);
        if (it == nitems) break;
        square(it, p, CRUMB_R, z);
        it_c[it] = RGB15(255, 214, 170);
    }
    /* keys */
    for (c = 0; c < 4; c++) {
        int kc = mz_keys[c], it;
        fix z;
        long px, py;
        V3 p;
        if (!key[kc]) continue;
        p = v3_scale14(mz_dir[kc], PLANET_R + Q8(0.45));
        if (!project(p, &px, &py, &z)) continue;
        it = nitems;
        add_item(IT_SPRITE, 0, z);
        if (it == nitems) break;
        it_tex[it] = tex_key_id;
        it_x[it][0] = px; it_y[it][0] = py;
        it_half[it] = Q8(0.42) * recip[(z >> 2) < RECIP_N ? z >> 2 : RECIP_N - 1];
    }
    /* sprites (chomper, spooks) */
    for (i = 0; i < nspr; i++) {
        fix z;
        long px, py;
        int it;
        if (spr[i].tex < 0 || !project(spr[i].pos, &px, &py, &z)) continue;
        it = nitems;
        add_item(IT_SPRITE, i, z);
        if (it == nitems) break;
        it_tex[it] = spr[i].tex;
        it_x[it][0] = px; it_y[it][0] = py;
        it_half[it] = spr[i].half * recip[(z >> 2) < RECIP_N ? z >> 2 : RECIP_N - 1];
    }
    /* the nest: a flat pink disc on the surface */
    {
        static const short cs[9][2] = { {16384,0},{11585,11585},{0,16384},{-11585,11585},
            {-16384,0},{-11585,-11585},{0,-16384},{11585,-11585},{16384,0} };
        V3 n = mz_dir[mz_nest], t1 = mz_tan[mz_nest][0], t2 = mz_tan[mz_nest][1];
        V3 ctr = v3_scale14(n, PLANET_R + Q8(0.02));
        fix z;
        int ok = project(ctr, &nest_x[8], &nest_y[8], &z), k2;
        for (k2 = 0; k2 < 8 && ok; k2++) {
            V3 p = v3_add(ctr, v3_add(v3_scale14(t1, (Q8(1.25) * cs[k2][0]) >> 14),
                                      v3_scale14(t2, (Q8(1.25) * cs[k2][1]) >> 14)));
            ok = project(p, &nest_x[k2], &nest_y[k2], 0);
        }
        if (ok) add_item(IT_NEST, 0, z + Q8(0.3));   /* under anything standing on it */
    }

    /* ---- far to near ---- */
    for (b = BUCKETS - 1; b >= 0; b--) {
        for (i = bucket[b]; i >= 0; i = it_next[i]) {
            if (it_type[i] == IT_SPRITE) {
                pc_sprite(it_tex[i], it_x[i][0], it_y[i][0], it_half[i]);
                continue;
            }
            if (it_type[i] == IT_NEST) {
                int k2;
                for (k2 = 0; k2 < 8; k2++)
                    pc_quad(nest_x[8], nest_y[8], nest_x[k2], nest_y[k2], nest_x[k2 + 1 < 8 ? k2 + 1 : 0],
                            nest_y[k2 + 1 < 8 ? k2 + 1 : 0], nest_x[k2 + 1 < 8 ? k2 + 1 : 0],
                            nest_y[k2 + 1 < 8 ? k2 + 1 : 0], RGB15(240, 110, 190));
                continue;
            }
            pc_quad(it_x[i][0], it_y[i][0], it_x[i][1], it_y[i][1],
                    it_x[i][2], it_y[i][2], it_x[i][3], it_y[i][3], it_c[i]);
            if (it_type[i] == IT_WALL)
                pc_quad(it_x2[i][0], it_y2[i][0], it_x2[i][1], it_y2[i][1],
                        it_x2[i][2], it_y2[i][2], it_x2[i][3], it_y2[i][3], it_c2[i]);
        }
    }
    rd_stats_cels = pc_cels_count();
}
