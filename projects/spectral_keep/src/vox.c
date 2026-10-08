/*
 * The models of Models.cs - boxes and the odd sphere - drawn into images by
 * a small software rasteriser, once: at startup for the characters and
 * props (every facing and walk frame), at room entry for the room's blocks
 * and its floor and back walls (straight into the background image).
 *
 * The camera is Unity's orthographic Euler(30, 45) view snapped to whole
 * pixels: one unit along x is (+14, -7) on screen, along z (-14, -7), up
 * 17. Everything in a room is then the same image moved by whole pixels,
 * which is what the cel engine is good at.
 *
 * Lighting follows the Unity scene: trilight ambient plus the sun from
 * (0.28, -1, 0.62); textures are the project's own, baked by
 * tools/assets.py. Fixed point throughout (directions Q14, positions Q12).
 */
#include <stdlib.h>
#include <string.h>
#include "keep.h"

extern const unsigned char keep_tex[7][32 * 32 * 3];
enum { TX_BLOCK, TX_BRICKS, TX_COBBLE, TX_CRATE, TX_PLANKS, TX_ROCK, TX_SLAB };

Sprite spr_walker[3][8][3];
Sprite spr_sage[8], spr_ghost[8], spr_bouncer[8], spr_crate, spr_spikes, spr_gate[2],
       spr_lift, spr_piston[9], spr_relic[16], spr_potion, spr_key[16], spr_throne,
       spr_shadow[3];
Sprite spr_block[2][BLOCK_VARIANTS];
Sprite spr_arch[2];

/* ---- colours (Zx.cs Modern palette, ProjectSetup.cs materials), 0..255 ---- */

static const unsigned char pal[8][3] = {
    { 15, 15, 20 }, { 69, 115, 242 }, { 224, 74, 64 }, { 199, 97, 224 },
    { 97, 199, 97 }, { 71, 199, 219 }, { 250, 201, 69 }, { 237, 232, 222 }
};
static const short stone[3] = { 143, 138, 133 }, ground[3] = { 77, 74, 71 }, wood[3] = { 168, 120, 79 };

typedef struct { short r, g, b, tex, glow; } Mat;

static Mat mat(int r, int g, int b, int tex, int glow)
{
    Mat m;
    m.r = (short)(r > 255 ? 255 : r);
    m.g = (short)(g > 255 ? 255 : g);
    m.b = (short)(b > 255 ? 255 : b);
    m.tex = (short)tex;
    m.glow = (short)glow;
    return m;
}

/* lerp(a, palette[i], t/1000) * k/1000 */
static Mat tint(const short *a, int i, int t, int k, int tex)
{
    int c[3], j;
    for (j = 0; j < 3; j++) c[j] = (a[j] + (pal[i][j] - a[j]) * t / 1000) * k / 1000;
    return mat(c[0], c[1], c[2], tex, 0);
}

static Mat ink(int i)  { return mat(pal[i & 7][0], pal[i & 7][1], pal[i & 7][2], -1, 0); }
static Mat dim(int i)  { return tint(stone, i, 320, 1000, -1); }
static Mat glow(int i) { Mat m = ink(i); m.glow = 1; return m; }

unsigned short ink_rgb15(int i, int bright)
{
    int r = pal[i & 7][0], g = pal[i & 7][1], b = pal[i & 7][2];
    if (!bright) { r = r * 3 / 5; g = g * 3 / 5; b = b * 3 / 5; }
    return (unsigned short)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

/* ---- angles: 65536 per circle ---- */

static const short qsin[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384
};

static long fsin(long a)                    /* Q14 */
{
    int q, i, f;
    long v0, v1, v;
    a &= 0xFFFF;
    q = (int)(a >> 14);
    i = (int)((a >> 8) & 63);
    f = (int)(a & 255);
    if (q & 1) { v0 = qsin[64 - i]; v1 = qsin[63 - i < 0 ? 0 : 63 - i]; }
    else { v0 = qsin[i]; v1 = qsin[i + 1]; }
    v = v0 + (((v1 - v0) * f) >> 8);
    return (q & 2) ? -v : v;
}
static long fcos(long a) { return fsin(a + 16384); }
#define DEG(d) ((long)((d) * 65536L / 360))

/* ---- transforms: world = m * local + t, m Q14 ---- */

typedef struct { long m[9]; fix t[3]; } Xf;
static Xf xs[8];
static int xsp;

static void x_reset(void)
{
    xsp = 0;
    memset(&xs[0], 0, sizeof(Xf));
    xs[0].m[0] = xs[0].m[4] = xs[0].m[8] = 16384;
}
static void x_push(void) { xs[xsp + 1] = xs[xsp]; xsp++; }
static void x_pop(void) { xsp--; }

static void x_apply(const Xf *x, const fix *p, fix *o)
{
    int i;
    for (i = 0; i < 3; i++)
        o[i] = x->t[i] + ((x->m[i * 3] * p[0] + x->m[i * 3 + 1] * p[1] + x->m[i * 3 + 2] * p[2]) >> 14);
}

static void x_translate(fix x, fix y, fix z)
{
    fix p[3], o[3];
    p[0] = x; p[1] = y; p[2] = z;
    x_apply(&xs[xsp], p, o);
    xs[xsp].t[0] = o[0]; xs[xsp].t[1] = o[1]; xs[xsp].t[2] = o[2];
}

static void mat_mul(long *a, const long *b)     /* a = a * b */
{
    long r[9];
    int i, j;
    for (i = 0; i < 3; i++)
        for (j = 0; j < 3; j++)
            r[i * 3 + j] = (a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j]) >> 14;
    memcpy(a, r, sizeof(r));
}

/* Unity's Euler(x, y, z) = Ry * Rx * Rz, angles 65536 per circle */
static void x_rot(long ax, long ay, long az)
{
    long r[9];
    long s, c;
    if (ay) {
        s = fsin(ay); c = fcos(ay);
        r[0] = c; r[1] = 0; r[2] = s; r[3] = 0; r[4] = 16384; r[5] = 0; r[6] = -s; r[7] = 0; r[8] = c;
        mat_mul(xs[xsp].m, r);
    }
    if (ax) {
        s = fsin(ax); c = fcos(ax);
        r[0] = 16384; r[1] = 0; r[2] = 0; r[3] = 0; r[4] = c; r[5] = -s; r[6] = 0; r[7] = s; r[8] = c;
        mat_mul(xs[xsp].m, r);
    }
    if (az) {
        s = fsin(az); c = fcos(az);
        r[0] = c; r[1] = -s; r[2] = 0; r[3] = s; r[4] = c; r[5] = 0; r[6] = 0; r[7] = 0; r[8] = 16384;
        mat_mul(xs[xsp].m, r);
    }
}

/* ---- primitives ---- */

typedef struct {
    int sphere;
    fix c[8][3];                    /* box corners (bit 0 x, 1 y, 2 z), world Q12 */
    long ax[3][3];                  /* box axes, world, Q14 */
    fix cen[3], rad;                /* sphere */
    Mat m;
    int vary;                       /* Q8 */
    long key;
} Prim;
#define MAXPRIM 220
static Prim prim[MAXPRIM];
static int nprim, cur_vary = 256;

#define F(v) FXF(v)                /* constants only */
#define FI(n) ((fix)(n) << 12)      /* integer variables */

static void box(fix cx, fix cy, fix cz, fix sx, fix sy, fix sz, Mat m)
{
    Prim *p;
    fix l[3];
    int k, i;
    if (nprim >= MAXPRIM) return;
    p = &prim[nprim++];
    p->sphere = 0;
    p->m = m;
    p->vary = cur_vary;
    for (k = 0; k < 8; k++) {
        l[0] = cx + ((k & 1) ? sx / 2 : -sx / 2);
        l[1] = cy + ((k & 2) ? sy / 2 : -sy / 2);
        l[2] = cz + ((k & 4) ? sz / 2 : -sz / 2);
        x_apply(&xs[xsp], l, p->c[k]);
    }
    for (i = 0; i < 3; i++) {
        p->ax[i][0] = xs[xsp].m[i];
        p->ax[i][1] = xs[xsp].m[3 + i];
        p->ax[i][2] = xs[xsp].m[6 + i];
    }
}

static void sphere(fix cx, fix cy, fix cz, fix sx, fix sy, fix sz, Mat m)
{
    Prim *p;
    fix l[3];
    if (nprim >= MAXPRIM) return;
    p = &prim[nprim++];
    p->sphere = 1;
    p->m = m;
    p->vary = cur_vary;
    l[0] = cx; l[1] = cy; l[2] = cz;
    x_apply(&xs[xsp], l, p->cen);
    p->rad = (sx + sy + sz) / 6;
}

/* ---- rasterising ---- */

typedef struct {
    unsigned short *pix;
    long *dep;
    int w, h;
    long ox4, oy4;                  /* world origin, pixel coords * 16 */
} Target;

static long proj_x4(const Target *t, const fix *p) { return t->ox4 + (((p[0] - p[2]) * ISO_X) >> 8); }
static long proj_y4(const Target *t, const fix *p)
{
    return t->oy4 - ((p[1] * ISO_Y + (p[0] + p[2]) * ISO_Z) >> 8);
}
static long depth8(const fix *p) { return ((p[0] + p[2]) * 17 - p[1] * 14) >> 4; }

static const long view_q14[3] = { 10012, -8246, 10012 };     /* (17, -14, 17) normalised */

/* ambient trilight + sun, Q8 per channel, for a unit normal (Q14) */
static void light(const long *n, int *out)
{
    static const int sky[3] = { 118, 123, 143 }, eq[3] = { 77, 77, 84 }, gr[3] = { 36, 36, 41 };
    static const int sun[3] = { 269, 255, 237 };
    long d = (-3793L * n[0] + 13546L * n[1] - 8399L * n[2]) >> 14;   /* n . -L */
    long ny = n[1];
    int i;
    if (d < 0) d = 0;
    for (i = 0; i < 3; i++) {
        long amb = ny >= 0 ? eq[i] + (((sky[i] - eq[i]) * ny) >> 14) : eq[i] + (((gr[i] - eq[i]) * -ny) >> 14);
        out[i] = (int)(amb + ((sun[i] * d) >> 14));
    }
}

/* a face's colour: multipliers per channel (Q8 of the texel), worked out
 * once per face so the pixel loop has no divides */
typedef struct { int k[3]; const unsigned char *tex; unsigned short flat; } Shade;

static unsigned short pack(long r, long g, long b)
{
    unsigned short v;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    v = (unsigned short)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    return v ? v : 1;                                          /* 0 is transparent */
}

static void shade_init(Shade *sh, const Prim *p, const int *lt)
{
    int alb[3], i;
    alb[0] = p->m.r; alb[1] = p->m.g; alb[2] = p->m.b;
    for (i = 0; i < 3; i++) {
        long a = (long)alb[i] * p->vary >> 8;
        sh->k[i] = (int)((a * (lt[i] + (p->m.glow ? 230 : 0))) >> 8);   /* 256 = texel unchanged */
    }
    sh->tex = p->m.tex >= 0 ? keep_tex[p->m.tex] : 0;
    sh->flat = pack(sh->k[0] > 255 ? 255 : sh->k[0], sh->k[1], sh->k[2]);
}

static unsigned short shade(const Shade *sh, int tu, int tv)
{
    const unsigned char *tx;
    if (!sh->tex) return sh->flat;
    tx = sh->tex + ((tv & 31) * 32 + (tu & 31)) * 3;
    return pack((tx[0] * (long)sh->k[0]) >> 8, (tx[1] * (long)sh->k[1]) >> 8, (tx[2] * (long)sh->k[2]) >> 8);
}

/* one parallelogram face: origin corner o, edges to corners e1 and e2 */
static void face(const Target *t, const Prim *p, int o, int e1, int e2, const long *n, int side)
{
    long x0, y0, ux, uy, vx, vy, det, dudx, dudy, dvdx, dvdy, d0, dd1, dd2;
    long minx, maxx, miny, maxy, xs4[4], ys4[4];
    int lt[3], i, j, k;
    Shade sh;
    if ((n[0] * view_q14[0] + n[1] * view_q14[1] + n[2] * view_q14[2]) >= 0) return;   /* faces away */
    x0 = proj_x4(t, p->c[o]); y0 = proj_y4(t, p->c[o]);
    ux = proj_x4(t, p->c[e1]) - x0; uy = proj_y4(t, p->c[e1]) - y0;
    vx = proj_x4(t, p->c[e2]) - x0; vy = proj_y4(t, p->c[e2]) - y0;
    det = ux * vy - uy * vx;
    if (det > -256 && det < 256) return;                       /* edge-on: under a pixel */
    dudx = (vy << 20) / det;  dudy = -(vx << 20) / det;        /* Q16 per pixel */
    dvdx = -(uy << 20) / det; dvdy = (ux << 20) / det;
    d0 = depth8(p->c[o]);
    dd1 = depth8(p->c[e1]) - d0;
    dd2 = depth8(p->c[e2]) - d0;
    light(n, lt);
    shade_init(&sh, p, lt);
    xs4[0] = x0; xs4[1] = x0 + ux; xs4[2] = x0 + vx; xs4[3] = x0 + ux + vx;
    ys4[0] = y0; ys4[1] = y0 + uy; ys4[2] = y0 + vy; ys4[3] = y0 + uy + vy;
    minx = maxx = xs4[0]; miny = maxy = ys4[0];
    for (k = 1; k < 4; k++) {
        if (xs4[k] < minx) minx = xs4[k];
        if (xs4[k] > maxx) maxx = xs4[k];
        if (ys4[k] < miny) miny = ys4[k];
        if (ys4[k] > maxy) maxy = ys4[k];
    }
    minx >>= 4; miny >>= 4; maxx = (maxx + 15) >> 4; maxy = (maxy + 15) >> 4;
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > t->w) maxx = t->w;
    if (maxy > t->h) maxy = t->h;
    for (j = (int)miny; j < maxy; j++) {
        long py = ((long)j << 4) + 8 - y0;
        long u = (((minx << 4) + 8 - x0) * dudx + py * dudy) >> 4;
        long v = (((minx << 4) + 8 - x0) * dvdx + py * dvdy) >> 4;
        unsigned short *dst = t->pix + j * t->w;
        long *dp = t->dep + j * t->w;
        for (i = (int)minx; i < maxx; i++, u += dudx, v += dvdx) {
            long d;
            int tu, tv;
            if (u < 0 || u >= 65536 || v < 0 || v >= 65536) continue;
            d = d0 + ((u * dd1) >> 16) + ((v * dd2) >> 16);
            if (d >= dp[i]) continue;
            dp[i] = d;
            tu = (int)(u >> 11);
            tv = side ? 31 - (int)(v >> 11) : (int)(v >> 11);
            dst[i] = shade(&sh, tu, tv);
        }
    }
}

static unsigned long isqrt(unsigned long v)
{
    unsigned long r = 0, b = 1UL << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

static void draw_sphere(const Target *t, const Prim *p)
{
    long cx = proj_x4(t, p->cen), cy = proj_y4(t, p->cen), cd = depth8(p->cen);
    long r4 = (p->rad * 317) >> 12;                            /* 19.8 px per unit, * 16 */
    long r2 = r4 * r4;
    int i, j, x0 = (int)((cx - r4) >> 4), x1 = (int)((cx + r4 + 15) >> 4);
    int y0 = (int)((cy - r4) >> 4), y1 = (int)((cy + r4 + 15) >> 4);
    if (r4 <= 0) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > t->w) x1 = t->w;
    if (y1 > t->h) y1 = t->h;
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++) {
            long dx = ((long)i << 4) + 8 - cx, dy = ((long)j << 4) + 8 - cy, d2 = dx * dx + dy * dy, dz, d;
            long cn[3], n[3];
            int lt[3];
            Shade sh;
            if (d2 >= r2) continue;
            dz = (long)isqrt((unsigned long)(r2 - d2));
            d = cd - ((dz * 359) >> 4);
            if (d >= t->dep[j * t->w + i]) continue;
            t->dep[j * t->w + i] = d;
            cn[0] = (dx << 14) / r4; cn[1] = (-dy << 14) / r4; cn[2] = (dz << 14) / r4;
            /* camera (right, up, towards the viewer) -> world */
            n[0] = (11585L * cn[0] + 5793L * cn[1] - 10012L * cn[2]) >> 14;
            n[1] = (14189L * cn[1] + 8246L * cn[2]) >> 14;
            n[2] = (-11585L * cn[0] + 5793L * cn[1] - 10012L * cn[2]) >> 14;
            light(n, lt);
            shade_init(&sh, p, lt);
            t->pix[j * t->w + i] = sh.flat;
        }
}

static void draw_prims(const Target *t)
{
    int i;
    for (i = 0; i < nprim; i++) {
        Prim *p = &prim[i];
        long n[3];
        int k;
        if (p->sphere) { draw_sphere(t, p); continue; }
        /* faces: +y top, -z south, -x west, and the other three for rotated parts */
        for (k = 0; k < 3; k++) n[k] = p->ax[1][k];
        face(t, p, 2, 3, 6, n, 0);
        for (k = 0; k < 3; k++) n[k] = -p->ax[1][k];
        face(t, p, 0, 1, 4, n, 0);
        for (k = 0; k < 3; k++) n[k] = -p->ax[2][k];
        face(t, p, 0, 1, 2, n, 1);
        for (k = 0; k < 3; k++) n[k] = p->ax[2][k];
        face(t, p, 4, 5, 6, n, 1);
        for (k = 0; k < 3; k++) n[k] = -p->ax[0][k];
        face(t, p, 0, 4, 2, n, 1);
        for (k = 0; k < 3; k++) n[k] = p->ax[0][k];
        face(t, p, 1, 5, 3, n, 1);
    }
}

/* render the collected primitives into a fresh, cropped sprite */
static void begin(void)
{
    nprim = 0;
    cur_vary = 256;
    x_reset();
}

static void end(Sprite *s)
{
    Target t;
    long minx = 0x7FFFFFFF, maxx = -0x7FFFFFFF, miny = 0x7FFFFFFF, maxy = -0x7FFFFFFF;
    int i, k, n;
    t.ox4 = t.oy4 = 0;
    for (i = 0; i < nprim; i++) {
        Prim *p = &prim[i];
        if (p->sphere) {
            long cx = proj_x4(&t, p->cen), cy = proj_y4(&t, p->cen), r4 = (p->rad * 317) >> 12;
            if (cx - r4 < minx) minx = cx - r4;
            if (cx + r4 > maxx) maxx = cx + r4;
            if (cy - r4 < miny) miny = cy - r4;
            if (cy + r4 > maxy) maxy = cy + r4;
            continue;
        }
        for (k = 0; k < 8; k++) {
            long x = proj_x4(&t, p->c[k]), y = proj_y4(&t, p->c[k]);
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }
    s->pix = 0;
    s->ccb = 0;
    s->w = s->h = 0;
    if (!nprim) return;
    /* whole pixels, a pixel of margin, even width */
    minx = (minx >> 4) - 1;
    miny = (miny >> 4) - 1;
    maxx = ((maxx + 15) >> 4) + 1;
    maxy = ((maxy + 15) >> 4) + 1;
    t.w = (int)(maxx - minx);
    t.w = (t.w + 1) & ~1;
    t.h = (int)(maxy - miny);
    t.ox4 = -minx << 4;
    t.oy4 = -miny << 4;
    n = t.w * t.h;
    t.pix = (unsigned short *)malloc(n * 2);
    t.dep = (long *)malloc(n * 4);
    if (!t.pix || !t.dep) {
        if (t.pix) free(t.pix);
        if (t.dep) free(t.dep);
        return;
    }
    memset(t.pix, 0, n * 2);
    for (i = 0; i < n; i++) t.dep[i] = 0x7FFFFFFF;
    draw_prims(&t);
    free(t.dep);
    s->pix = t.pix;
    s->w = (short)t.w;
    s->h = (short)t.h;
    s->ax = (short)(-minx);
    s->ay = (short)(-miny);
}

static void sprite_free(Sprite *s)
{
    if (s->pix) free(s->pix);
    s->pix = 0;
    s->w = s->h = 0;
}

/* ---- the models (Models.cs) ---- */

/* a limb hanging from its pivot (hip / shoulder), swung about x */
static void limb(fix px, fix py, fix pz, fix sx, fix sy, fix sz, Mat m, long swing)
{
    x_push();
    x_translate(px, py, pz);
    x_rot(swing, 0, 0);
    box(0, -sy / 2, 0, sx, sy, sz, m);
    x_pop();
}

static void explorer(long sw, fix bob)
{
    Mat w = ink(7), d = dim(7), k = ink(0);
    limb(F(-0.12), F(0.3), 0, F(0.17), F(0.3), F(0.2), w, sw);
    limb(F(0.12), F(0.3), 0, F(0.17), F(0.3), F(0.2), w, -sw);
    x_push();
    x_translate(0, F(0.3) + bob, 0);
    box(0, F(0.17), 0, F(0.44), F(0.34), F(0.3), w);               /* torso */
    box(0, F(0.2), F(-0.22), F(0.34), F(0.3), F(0.16), d);         /* pack */
    box(0, F(0.47), 0, F(0.3), F(0.26), F(0.28), w);               /* head */
    box(0, F(0.46), F(0.17), F(0.08), F(0.08), F(0.08), w);        /* nose */
    box(F(-0.07), F(0.52), F(0.145), F(0.06), F(0.06), F(0.02), k); /* eyes */
    box(F(0.07), F(0.52), F(0.145), F(0.06), F(0.06), F(0.02), k);
    box(0, F(0.62), 0, F(0.52), F(0.04), F(0.5), w);               /* brim */
    box(0, F(0.7), 0, F(0.3), F(0.13), F(0.3), w);                 /* crown */
    limb(F(-0.27), F(0.25), F(0.02), F(0.1), F(0.26), F(0.12), w, -sw);
    limb(F(0.27), F(0.25), F(0.02), F(0.1), F(0.26), F(0.12), w, sw);
    x_pop();
}

static void guard(long sw, fix bob)
{
    Mat m = ink(2), d = dim(2);
    limb(F(-0.12), F(0.3), 0, F(0.17), F(0.3), F(0.2), m, sw);
    limb(F(0.12), F(0.3), 0, F(0.17), F(0.3), F(0.2), m, -sw);
    x_push();
    x_translate(0, F(0.3) + bob, 0);
    box(0, F(0.2), 0, F(0.5), F(0.4), F(0.34), m);
    box(0, F(0.55), 0, F(0.36), F(0.3), F(0.34), m);               /* helmet */
    box(0, F(0.53), F(0.17), F(0.26), F(0.05), F(0.02), ink(0));   /* visor slit */
    box(0, F(0.78), F(-0.02), F(0.06), F(0.18), F(0.3), d);        /* plume */
    box(F(0.33), F(0.35), F(0.12), F(0.06), F(1.15), F(0.06), d);  /* spear */
    x_push();
    x_translate(F(0.33), F(0.98), F(0.12));
    x_rot(0, 0, DEG(45));
    box(0, 0, 0, F(0.12), F(0.12), F(0.04), m);
    x_pop();
    box(F(-0.3), F(0.2), F(0.04), F(0.1), F(0.36), F(0.36), d);    /* shield */
    x_pop();
}

static void hound(long sw)
{
    Mat m = ink(6), d = dim(6), k = ink(0);
    box(0, F(0.36), F(-0.05), F(0.36), F(0.26), F(0.62), m);
    box(0, F(0.52), F(0.33), F(0.3), F(0.28), F(0.3), m);          /* head */
    box(0, F(0.46), F(0.52), F(0.16), F(0.12), F(0.14), d);        /* snout */
    box(F(-0.11), F(0.7), F(0.3), F(0.08), F(0.12), F(0.06), m);   /* ears */
    box(F(0.11), F(0.7), F(0.3), F(0.08), F(0.12), F(0.06), m);
    box(F(-0.08), F(0.56), F(0.485), F(0.05), F(0.05), F(0.02), k);
    box(F(0.08), F(0.56), F(0.485), F(0.05), F(0.05), F(0.02), k);
    x_push();
    x_translate(0, F(0.52), F(-0.42));
    x_rot(DEG(-35), 0, 0);
    box(0, 0, 0, F(0.07), F(0.07), F(0.3), m);                     /* tail */
    x_pop();
    /* trot: diagonal pairs move together */
    limb(F(-0.12), F(0.24), F(0.16), F(0.1), F(0.24), F(0.1), m, sw);
    limb(F(0.12), F(0.24), F(-0.22), F(0.1), F(0.24), F(0.1), m, sw);
    limb(F(0.12), F(0.24), F(0.16), F(0.1), F(0.24), F(0.1), m, -sw);
    limb(F(-0.12), F(0.24), F(-0.22), F(0.1), F(0.24), F(0.1), m, -sw);
}

static void sage(void)
{
    Mat m = ink(4);
    box(0, F(0.2), 0, F(0.56), F(0.4), F(0.5), m);                 /* robe */
    box(0, F(0.5), 0, F(0.44), F(0.24), F(0.4), m);
    box(0, F(0.74), 0, F(0.3), F(0.26), F(0.3), m);                /* head */
    box(0, F(0.6), F(0.13), F(0.24), F(0.26), F(0.08), ink(7));    /* beard */
    box(0, F(0.92), 0, F(0.44), F(0.05), F(0.44), m);              /* hat */
    box(0, F(1.02), 0, F(0.26), F(0.16), F(0.26), m);
    box(0, F(1.15), F(-0.03), F(0.14), F(0.14), F(0.14), m);
    box(F(0.36), F(0.55), F(0.1), F(0.06), F(1.1), F(0.06), dim(4)); /* staff */
    box(F(0.36), F(1.13), F(0.1), F(0.14), F(0.14), F(0.14), ink(6));
}

static void ghost(void)
{
    Mat m = ink(3), k = ink(0);
    int i;
    sphere(0, F(0.62), 0, F(0.62), F(0.56), F(0.62), m);
    box(0, F(0.38), 0, F(0.6), F(0.42), F(0.6), m);
    for (i = 0; i < 3; i++) box(F(-0.2) + F(0.2) * i, F(0.13), F(0.2), F(0.14), F(0.12), F(0.14), m);
    box(F(-0.12), F(0.62), F(0.31), F(0.1), F(0.14), F(0.02), k);
    box(F(0.12), F(0.62), F(0.31), F(0.1), F(0.14), F(0.02), k);
    box(0, F(0.44), F(0.31), F(0.16), F(0.08), F(0.02), k);
}

static void bouncer(long roll)
{
    static const signed char axes[6][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    int i;
    x_push();
    x_translate(0, F(0.32), 0);
    x_rot(roll, 0, 0);
    sphere(0, 0, 0, F(0.56), F(0.56), F(0.56), ink(4));
    for (i = 0; i < 6; i++) {
        x_push();
        x_translate(F(0.3) * axes[i][0], F(0.3) * axes[i][1], F(0.3) * axes[i][2]);
        x_rot(DEG(45), DEG(45), 0);
        box(0, 0, 0, F(0.14), F(0.14), F(0.14), dim(4));
        x_pop();
    }
    x_pop();
}

static void spikes(void)
{
    int i, j;
    box(0, F(0.04), 0, F(0.94), F(0.08), F(0.94), dim(7));
    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++) {
            x_push();
            x_translate(F(-0.22) + F(0.44) * i, F(0.24), F(-0.22) + F(0.44) * j);
            x_rot(DEG(45), 0, DEG(45));
            box(0, 0, 0, F(0.16), F(0.42), F(0.16), ink(7));
            x_pop();
        }
}

static void gate(int rot)
{
    int i;
    if (rot) x_rot(0, DEG(90), 0);
    for (i = 0; i < 4; i++) box(F(-0.36) + F(0.24) * i, F(1), 0, F(0.09), F(2), F(0.09), ink(2));
    for (i = 0; i < 4; i++) box(0, F(0.3) + F(0.5) * i, 0, F(0.94), F(0.08), F(0.08), dim(2));
}

static void throne(void)
{
    Mat m = ink(6);
    int i;
    box(0, F(0.2), 0, F(0.8), F(0.4), F(0.7), m);
    box(0, F(0.43), 0, F(0.6), F(0.08), F(0.55), ink(3));
    box(0, F(0.85), F(0.3), F(0.8), F(1.3), F(0.14), m);
    box(F(-0.36), F(0.55), 0, F(0.1), F(0.3), F(0.66), m);
    box(F(0.36), F(0.55), 0, F(0.1), F(0.3), F(0.66), m);
    for (i = -1; i <= 1; i++) box(F(0.25) * i, F(1.58), F(0.3), F(0.12), F(0.16), F(0.12), ink(6));
}

static void relic(void)
{
    x_push();
    x_rot(DEG(45), 0, DEG(45));
    box(0, 0, 0, F(0.32), F(0.32), F(0.32), glow(6));
    x_pop();
    box(0, 0, 0, F(0.5), F(0.05), F(0.05), ink(7));
    box(0, 0, 0, F(0.05), F(0.5), F(0.05), ink(7));
}

static void key(void)
{
    Mat m = glow(5);
    box(0, F(0.17), 0, F(0.24), F(0.05), F(0.05), m);
    box(0, F(0.03), 0, F(0.24), F(0.05), F(0.05), m);
    box(F(-0.1), F(0.1), 0, F(0.05), F(0.18), F(0.05), m);
    box(F(0.1), F(0.1), 0, F(0.05), F(0.18), F(0.05), m);
    box(0, F(-0.15), 0, F(0.05), F(0.34), F(0.05), m);
    box(F(0.06), F(-0.22), 0, F(0.08), F(0.04), F(0.05), m);
    box(F(0.06), F(-0.3), 0, F(0.1), F(0.04), F(0.05), m);
}

static void potion(void)
{
    Mat m = glow(3);
    sphere(0, F(-0.06), 0, F(0.3), F(0.3), F(0.3), m);
    box(0, F(0.12), 0, F(0.1), F(0.14), F(0.1), m);
    box(0, F(0.22), 0, F(0.13), F(0.06), F(0.13), ink(7));
}

/* a flat ellipse on the ground: any non-zero pixel; the cel's PIXC darkens */
static void shadow_sprite(Sprite *s, int rpx)
{
    int w = rpx * 2 + 2, h = rpx + 2, i, j;
    s->w = (short)w;
    s->h = (short)h;
    s->ax = (short)(rpx + 1);
    s->ay = (short)(rpx / 2 + 1);
    s->ccb = 0;
    s->pix = (unsigned short *)malloc(w * h * 2);
    if (!s->pix) { s->w = 0; return; }
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            long dx = (long)(i - s->ax) * 2 + 1, dy = (long)(j - s->ay) * 4 + 2;
            s->pix[j * w + i] = (unsigned short)(dx * dx + dy * dy <= (long)rpx * rpx * 4 ? 1 : 0);
        }
}

int vox_init(void)
{
    int m, f, k;
    static const long swing[3] = { 0, DEG(32), DEG(-32) };
    for (m = 0; m < 3; m++)
        for (f = 0; f < 8; f++)
            for (k = 0; k < 3; k++) {
                fix bob = k ? F(0.025) : 0;
                begin();
                x_rot(0, DEG(45) * f, 0);
                if (m == 0) explorer(swing[k], bob);
                else if (m == 1) guard(swing[k], bob);
                else hound(swing[k]);
                end(&spr_walker[m][f][k]);
            }
    for (f = 0; f < 8; f++) {
        begin(); x_rot(0, DEG(45) * f, 0); sage(); end(&spr_sage[f]);
        begin(); x_rot(0, DEG(45) * f, 0); ghost(); end(&spr_ghost[f]);
        begin(); bouncer(DEG(11.25) * f); end(&spr_bouncer[f]);
    }
    for (f = 0; f < 16; f++) {
        begin(); x_translate(0, F(0.45), 0); x_rot(0, DEG(22.5) * f, 0); relic(); end(&spr_relic[f]);
        begin(); x_translate(0, F(0.45), 0); x_rot(0, DEG(22.5) * f, 0); key(); end(&spr_key[f]);
    }
    begin(); x_translate(0, F(0.45), 0); potion(); end(&spr_potion);
    begin(); box(0, F(0.48), 0, F(0.94), F(0.94), F(0.94), mat(219, 163, 97, TX_CRATE, 0)); end(&spr_crate);
    begin(); spikes(); end(&spr_spikes);
    begin(); gate(0); end(&spr_gate[0]);
    begin(); gate(1); end(&spr_gate[1]);
    begin(); throne(); end(&spr_throne);
    begin();
    box(0, F(0.125), 0, F(0.96), F(0.25), F(0.96), ink(5));
    box(0, F(0.26), 0, F(0.6), F(0.04), F(0.6), dim(5));
    end(&spr_lift);
    for (k = 0; k < 9; k++) {
        begin();
        if (k) box(0, -F(0.125) * k, 0, F(0.24), F(0.25) * k, F(0.24), dim(5));
        end(&spr_piston[k]);
    }
    shadow_sprite(&spr_shadow[0], 5);
    shadow_sprite(&spr_shadow[1], 7);
    shadow_sprite(&spr_shadow[2], 9);
    return spr_walker[0][0][0].pix != 0;
}

/* ---- rooms ---- */

static int vary_q8(int seed, int amount)      /* MatLib.Vary, amount in 1/1000 */
{
    unsigned long h = (unsigned long)seed * 2654435761UL;
    long f = (long)((h >> 8) % 1000) - 500;
    return (int)(256 + f * 2 * amount * 256 / 1000000L);
}

int block_variant(int x, int y, int z)
{
    unsigned long h = (unsigned long)(x * 7 + y * 131 + z * 29) * 2654435761UL;
    return (int)(((h >> 8) % 1000) * BLOCK_VARIANTS / 1000);
}

static Mat wall_mat(const KRoom *r)
{
    return r->wall == 1 ? tint(stone, r->ink, 280, 1200, TX_ROCK) : tint(stone, r->ink, 320, 1180, TX_BRICKS);
}

/* MatLib.Vary is only a brightness factor: variants are the base image scaled */
static void scaled(Sprite *dst, const Sprite *src, int k)
{
    unsigned short lr[32], lg[32], lb[32];
    int i, n = src->w * src->h;
    *dst = *src;
    dst->ccb = 0;
    dst->pix = (unsigned short *)malloc(n * 2);
    if (!dst->pix) { dst->w = 0; return; }
    for (i = 0; i < 32; i++) {
        int c = (i * k) >> 8;
        if (c > 31) c = 31;
        lr[i] = (unsigned short)(c << 10);
        lg[i] = (unsigned short)(c << 5);
        lb[i] = (unsigned short)c;
    }
    for (i = 0; i < n; i++) {
        unsigned short v = src->pix[i];
        if (v) {
            v = (unsigned short)(lr[(v >> 10) & 31] | lg[(v >> 5) & 31] | lb[v & 31]);
            if (!v) v = 1;
        }
        dst->pix[i] = v;
    }
}

Sprite spr_tile[VARIANTS], spr_brick[VARIANTS], spr_step[2];

static int variant_of(int vary_q8, int amount_q8)     /* 256 +- amount -> 0..VARIANTS-1 */
{
    int v = (vary_q8 - (256 - amount_q8)) * VARIANTS / (2 * amount_q8 + 1);
    return v < 0 ? 0 : v >= VARIANTS ? VARIANTS - 1 : v;
}

void vox_room(const KRoom *r)
{
    Sprite base;
    int t, v, s, lo, hi;
    Mat fm = r->floor == 1 ? tint(wood, r->ink, 120, 1150, TX_PLANKS)
           : r->floor == 2 ? tint(ground, r->ink, 200, 1350, TX_COBBLE)
           : tint(ground, r->ink, 220, 1200, TX_SLAB);
    for (v = 0; v < BLOCK_VARIANTS; v++) { sprite_free(&spr_block[0][v]); sprite_free(&spr_block[1][v]); }
    for (v = 0; v < VARIANTS; v++) { sprite_free(&spr_tile[v]); sprite_free(&spr_brick[v]); }
    for (s = 0; s < 2; s++) { sprite_free(&spr_arch[s]); sprite_free(&spr_step[s]); spr_arch[s].ccb = 0; }

    /* blocks: Vary(..., 0.05) */
    for (t = 0; t < 2; t++) {
        begin();
        box(F(0.5), F(0.5), F(0.5), F(0.94), F(0.94), F(0.94),
            t == 0 ? tint(stone, r->ink, 600, 1150, TX_BLOCK) : wall_mat(r));
        end(&base);
        for (v = 0; v < BLOCK_VARIANTS; v++)
            if (base.pix) scaled(&spr_block[t][v], &base, 256 - 13 + (26 * (2 * v + 1)) / (2 * BLOCK_VARIANTS));
        sprite_free(&base);
    }
    /* floor tiles: Vary(..., 0.09); wall bricks: Vary(..., 0.07) */
    begin();
    box(F(0.5), F(-0.06), F(0.5), F(0.97), F(0.12), F(0.97), fm);
    end(&base);
    for (v = 0; v < VARIANTS; v++)
        if (base.pix) scaled(&spr_tile[v], &base, 256 - 23 + (46 * (2 * v + 1)) / (2 * VARIANTS));
    sprite_free(&base);
    begin();
    box(F(0.5), F(0.5), F(0.5), F(0.95), F(0.92), F(0.95), wall_mat(r));
    end(&base);
    for (v = 0; v < VARIANTS; v++)
        if (base.pix) scaled(&spr_brick[v], &base, 256 - 18 + (36 * (2 * v + 1)) / (2 * VARIANTS));
    sprite_free(&base);
    /* door steps in the back walls, and the arches of the front doorways (world origin) */
    door_span(r->w, &lo, &hi);
    begin();
    box((FI(lo) + FI(hi)) / 2, F(0.01), FI(r->d) + F(0.5), FI(hi - lo), F(0.02), F(0.9), dim(r->ink));
    end(&spr_step[0]);
    door_span(r->d, &lo, &hi);
    begin();
    box(FI(r->w) + F(0.5), F(0.01), (FI(lo) + FI(hi)) / 2, F(0.9), F(0.02), FI(hi - lo), dim(r->ink));
    end(&spr_step[1]);
    for (s = 0; s < 2; s++) {
        int side = s == 0 ? SIDE_S : SIDE_W, len = side == SIDE_S ? r->w : r->d;
        Mat m = ink(r->ink), d = dim(r->ink);
        door_span(len, &lo, &hi);
        begin();
        if (side == SIDE_S) {
            box(FI(lo) - F(0.12), F(1.1), F(-0.12), F(0.22), F(2.2), F(0.22), m);
            box(FI(hi) + F(0.12), F(1.1), F(-0.12), F(0.22), F(2.2), F(0.22), m);
            box((FI(lo) + FI(hi)) / 2, F(2.2), F(-0.12), FI(hi - lo) + F(0.46), F(0.2), F(0.22), d);
        } else {
            box(F(-0.12), F(1.1), FI(lo) - F(0.12), F(0.22), F(2.2), F(0.22), m);
            box(F(-0.12), F(1.1), FI(hi) + F(0.12), F(0.22), F(2.2), F(0.22), m);
            box(F(-0.12), F(2.2), (FI(lo) + FI(hi)) / 2, F(0.22), F(0.2), FI(hi - lo) + F(0.46), d);
        }
        end(&spr_arch[s]);
    }
}

/* which shade of tile and brick a cell gets (MatLib.Vary seeds from RoomView) */
int tile_variant(int x, int z) { return variant_of(vary_q8(x * 31 + z * 17 + ((x + z) & 1) * 977, 90), 23); }
int brick_variant(int along, int y, int east) { return variant_of(vary_q8(along * 13 + y * 7 + (east ? 101 : 0), 70), 18); }
