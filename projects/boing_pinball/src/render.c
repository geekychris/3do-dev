// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Boing Pinball's drawing. The table is seen from the player's
 * end, tilted: an oblique view where a millimetre across is 0.55 pixels,
 * one up the table 0.44 and one of height 0.33 (heights go up the
 * screen). Everything that doesn't move is drawn once into a tall
 * picture (the engine's g_pic) when a table is chosen: the playfield and
 * its art, the walls, posts, pop bumpers and the ramps. Each frame copies
 * 224 rows of it under the dot-matrix display (the blitter on the classic
 * Amiga) and draws what moves over it: the flippers, the lit lamps, the
 * standing drop targets, the spinners, the plunger and the Boing balls.
 */
#include <string.h>
#include <stdio.h>

#include "a3d.h"
#include "a3d_plat.h"
#include "pinball.h"
#define Item pb_Item                     /* the 3DO's headers have an Item */

#define KX   FX(0.55)
#define KY   FX(0.44)
#define KZ   FX(0.33)
#define OX   17                         /* screen column of x = 0 */
#define OY   478                        /* picture row of y = 0 at height 0 */

static int oy;                          /* picture row -> canvas row: 0 in the picture, VIEW_Y - cam on screen */

/* Positions come out in the drawing's own pixels: A3_SUB (2) to each of the
 * games' on AmigaOS 4, so the table is drawn at 640 pixels across there;
 * the same on the classic Amiga. */
#define S A3_SUB
static int px_(fix x) { return OX * S + (int)((FMUL(x, KX * S) + 32768) >> 16); }
static int py_(fix y, fix z) { return (OY + oy) * S - (int)((FMUL(y, KY * S) + FMUL(z, KZ * S) + 32768) >> 16); }
#define PXI(x)    px_(FROMI(x))
#define PYI(y, z) py_(FROMI(y), FROMI(z))
static int rx_(fix r) { int v = (int)((FMUL(r, KX * S) + 32768) >> 16); return v < 1 ? 1 : v; }
static int ry_(fix r) { int v = (int)((FMUL(r, KY * S) + 32768) >> 16); return v < 1 ? 1 : v; }

/* ---------------------------------------------------------------- palettes */

static const uint16_t common_pal[32] = {
    0x000, 0x213, 0x324, 0x436, 0x547, 0xA86, 0x667, 0xCCD,
    0x413, 0x736, 0xFFE, 0x999, 0xE22, 0x811, 0xFF8, 0x101,
    0x5BF, 0x235, 0xFD4, 0x542, 0xF54, 0x522, 0x6F9, 0xF4F,
    0xFB2, 0x443, 0x210, 0xFA2, 0x741, 0x8CF, 0x631, 0xFFF,
};
/* per table: the playfield, its art and the cabinet's walls */
static const uint16_t theme_pal[TB_COUNT][9] = {
    /* field1 field2 art1  art2   art3   cab_d  cab_l  shadow wood */
    { 0x213, 0x324, 0x436, 0x547, 0xA86, 0x413, 0x736, 0x102, 0x631 },     /* Comet Tour: deep space */
    { 0x024, 0x035, 0x047, 0x0A9, 0xEC8, 0x134, 0x368, 0x012, 0x234 },     /* Deep Dive: the sea */
    { 0x322, 0x433, 0x544, 0x765, 0xDA2, 0x333, 0x777, 0x111, 0x421 },     /* Robot Foundry: iron and hazard yellow */
};

void render_palette(int theme)
{
    uint16_t pal[32];
    memcpy(pal, common_pal, sizeof pal);
    pal[P_FIELD1] = theme_pal[theme][0]; pal[P_FIELD2] = theme_pal[theme][1];
    pal[P_ART1] = theme_pal[theme][2]; pal[P_ART2] = theme_pal[theme][3]; pal[P_ART3] = theme_pal[theme][4];
    pal[P_CAB_D] = theme_pal[theme][5]; pal[P_CAB_L] = theme_pal[theme][6];
    pal[P_SHADOW] = theme_pal[theme][7]; pal[P_WOOD] = theme_pal[theme][8];
    plat_palette(pal);
}

static const uint8_t lamp_on[6] = { P_BLUE, P_YEL, P_LRED, P_GREEN, P_MAG, P_ORANGE };
static const uint8_t lamp_off[6] = { P_BLUE_D, P_YEL_D, P_LRED_D, P_DIM, P_DIM, P_DIM };

/* ---------------------------------------------------------------- 2D helpers */

/* everything below is in the drawing's own pixels */
static void ellipse(Canvas *cv, int cx, int cy, int rx, int ry, int col)
{
    int dy;
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    if (cy + ry < g_clip_y0 * S || cy - ry > g_clip_y1 * S + S - 1) return;
    for (dy = -ry; dy <= ry; dy++) {
        int w = (int)((rx * (int32_t)a3_isqrt((uint32_t)(ry * ry - dy * dy) << 8) + ry * 8) / (ry * 16));
        g_span_sub(cv, cy + dy, cx - w, cx + w, col);
    }
}

/* see-through: alpha 0..256 (full colour on AmigaOS 4, a dither on the classic Amiga) */
static void blend_ellipse(Canvas *cv, int cx, int cy, int rx, int ry, int col, int alpha)
{
    int dy;
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;
    if (cy + ry < g_clip_y0 * S || cy - ry > g_clip_y1 * S + S - 1) return;
    for (dy = -ry; dy <= ry; dy++) {
        int w = (int)((rx * (int32_t)a3_isqrt((uint32_t)(ry * ry - dy * dy) << 8) + ry * 8) / (ry * 16));
        g_blend_sub(cv, cy + dy, cx - w, cx + w, col, alpha);
    }
}

static void rect(Canvas *cv, int x0, int y0, int x1, int y1, int col)
{
    int y;
    for (y = y0; y <= y1; y++) g_span_sub(cv, y, x0, x1, col);
}

/* a line w of the games' pixels thick (squares stamped along it) */
static void line(Canvas *cv, int x0, int y0, int x1, int y1, int col, int w)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, h;
    w *= S;
    h = w / 2;
    if ((y0 < g_clip_y0 * S - w && y1 < g_clip_y0 * S - w) || (y0 > g_clip_y1 * S + S + w && y1 > g_clip_y1 * S + S + w)) return;
    for (;;) {
        if (w <= 1) g_span_sub(cv, y0, x0, x0, col);
        else rect(cv, x0 - h, y0 - h, x0 - h + w - 1, y0 - h + w - 1, col);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
    }
}

/* the convex hull of up to 8 points (Andrew's monotone chain), then filled */
static int cross_(int ox, int oy2, int ax, int ay, int bx, int by) { return (ax - ox) * (by - oy2) - (ay - oy2) * (bx - ox); }

static void hull_fill(Canvas *cv, int *xs, int *ys, int n, int col)
{
    int i, j, k = 0, idx[8], hx[17], hy[17];
    int16_t px[16], py[16];
    for (i = 0; i < n; i++) idx[i] = i;
    for (i = 1; i < n; i++)            /* sort by x then y */
        for (j = i; j > 0 && (xs[idx[j]] < xs[idx[j - 1]] || (xs[idx[j]] == xs[idx[j - 1]] && ys[idx[j]] < ys[idx[j - 1]])); j--) {
            int t = idx[j]; idx[j] = idx[j - 1]; idx[j - 1] = t;
        }
    for (i = 0; i < n; i++) {
        while (k >= 2 && cross_(hx[k - 2], hy[k - 2], hx[k - 1], hy[k - 1], xs[idx[i]], ys[idx[i]]) <= 0) k--;
        hx[k] = xs[idx[i]]; hy[k] = ys[idx[i]]; k++;
    }
    for (i = n - 2, j = k + 1; i >= 0; i--) {
        while (k >= j && cross_(hx[k - 2], hy[k - 2], hx[k - 1], hy[k - 1], xs[idx[i]], ys[idx[i]]) <= 0) k--;
        hx[k] = xs[idx[i]]; hy[k] = ys[idx[i]]; k++;
    }
    k--;
    if (k < 1) return;
    for (i = 0; i < k && i < 16; i++) { px[i] = (int16_t)hx[i]; py[i] = (int16_t)hy[i]; }
    if (k < 3) { line(cv, hx[0], hy[0], hx[k > 1 ? 1 : 0], hy[k > 1 ? 1 : 0], col, 1); return; }
    g_poly_sub(cv, px, py, k < 16 ? k : 16, col);
}

/* a capsule wall from a to b, radius r, h mm high: its sides, then its top */
static void wall3d(Canvas *cv, fix ax, fix ay, fix bx, fix by, fix r, int h, int side, int top)
{
    fix ex = bx - ax, ey = by - ay, len = fhypot(ex, ey), nx, ny, H = FROMI(h);
    int xs[8], ys[8], i;
    if (len < FONE / 4) { nx = r; ny = 0; }
    else { nx = FMUL(-FDIV(ey, len), r); ny = FMUL(FDIV(ex, len), r); }
    /* long walls in pieces (the span filler does 256 rows) */
    if (len > FROMI(300)) {
        fix mx = (ax + bx) / 2, my = (ay + by) / 2;
        wall3d(cv, ax, ay, mx, my, r, h, side, top);
        wall3d(cv, mx, my, bx, by, r, h, side, top);
        return;
    }
    {
        fix cx[4], cy[4];
        cx[0] = ax + nx; cy[0] = ay + ny; cx[1] = ax - nx; cy[1] = ay - ny;
        cx[2] = bx + nx; cy[2] = by + ny; cx[3] = bx - nx; cy[3] = by - ny;
        for (i = 0; i < 4; i++) {
            xs[i] = px_(cx[i]); ys[i] = py_(cy[i], 0);
            xs[4 + i] = xs[i]; ys[4 + i] = py_(cy[i], H);
        }
    }
    hull_fill(cv, xs, ys, 8, side);
    ellipse(cv, px_(ax), py_(ay, 0), rx_(r), ry_(r), side);
    ellipse(cv, px_(bx), py_(by, 0), rx_(r), ry_(r), side);
    hull_fill(cv, xs + 4, ys + 4, 4, top);
    ellipse(cv, px_(ax), py_(ay, H), rx_(r), ry_(r), top);
    ellipse(cv, px_(bx), py_(by, H), rx_(r), ry_(r), top);
}

/* a cylinder: posts, bumpers */
static void cylinder(Canvas *cv, fix x, fix y, fix r, int h, int side, int top)
{
    int cx = px_(x), y0 = py_(y, 0), y1 = py_(y, FROMI(h)), rx = rx_(r), ry = ry_(r);
    ellipse(cv, cx, y0, rx, ry, side);
    rect(cv, cx - rx, y1, cx + rx, y0, side);
    ellipse(cv, cx, y1, rx, ry, top);
}

/* ---------------------------------------------------------------- the Boing ball */

#define BR      (7 * S)                 /* its radius in the drawing's pixels */
#define BFRAMES 8                       /* one checker square's turn */
typedef struct { int8_t x0, x1; uint8_t col; } Run;
static Run ball_runs[BFRAMES][2 * 7 * 2 + 1][16];
static uint8_t ball_nruns[BFRAMES][2 * 7 * 2 + 1];

static void make_ball(void)
{
    int f, i, j;
    const int c20 = 962, s20 = 350;     /* cos, sin of 20 degrees * 1024: the classic tilt */
    for (f = 0; f < BFRAMES; f++)
        for (j = -BR; j <= BR; j++) {
            int n = 0, prev = -1, start = 0;
            for (i = -BR; i <= BR + 1; i++) {
                int col = -1;
                if (i <= BR && i * i + j * j <= BR * BR + BR / 2) {
                    int32_t nx = i * 1024 / BR, ny = j * 1024 / BR, nz2 = 1024 * 1024 - nx * nx - ny * ny, nz, xr, yr, lon, lat, lit;
                    if (nz2 < 0) nz2 = 0;
                    nz = (int32_t)a3_isqrt((uint32_t)nz2);
                    xr = (nx * c20 - ny * s20) / 1024; yr = (nx * s20 + ny * c20) / 1024;
                    lon = a3_atan2(xr, nz) + f * (8192 / BFRAMES);
                    lat = a3_atan2(yr, (int32_t)a3_isqrt((uint32_t)(1024 * 1024 - (yr * yr < 1024 * 1024 ? yr * yr : 1024 * 1024))));
                    lat = (lat + 32768 + 16384) & 0xFFFF;
                    /* light from the upper left */
                    lit = (-nx * 480 - ny * 560 + nz * 680) / 1024;
                    col = ((((lon & 0xFFFF) >> 13) + (lat >> 13)) & 1) ? P_RUBBER : P_RED;
                    if (lit < 250 || i * i + j * j > BR * BR - 4) col = col == P_RED ? P_RED_D : P_RUBBER_D;
                    if (lit > 960) col = P_TEXT;
                }
                if (col != prev) {
                    if (prev >= 0 && n < 16) {
                        ball_runs[f][j + BR][n].x0 = (int8_t)start; ball_runs[f][j + BR][n].x1 = (int8_t)(i - 1);
                        ball_runs[f][j + BR][n].col = (uint8_t)prev; n++;
                    }
                    prev = col; start = i;
                }
            }
            ball_nruns[f][j + BR] = (uint8_t)n;
        }
}

static void draw_ball(Canvas *cv, int cx, int cy, int frame)
{
    int j, k;
    frame &= BFRAMES - 1;
    for (j = 0; j <= 2 * BR; j++) {
        int y = cy - BR + j;
        for (k = 0; k < ball_nruns[frame][j]; k++) {
            const Run *r = &ball_runs[frame][j][k];
            g_span_sub(cv, y, cx + r->x0, cx + r->x1, r->col);
        }
    }
}

/* ---------------------------------------------------------------- the picture */

/* the playfield's left and right edges at y (mm) */
static void field_edges(int y, int *xl, int *xr)
{
    if (y > 780) {
        int d = y - 780, w = d < 260 ? (int)a3_isqrt((uint32_t)(260 * 260 - d * d)) : 0;
        *xl = 260 - w; *xr = 260 + w;
        return;
    }
    *xr = 520;
    if (y <= 380) *xl = 20;
    else if (y <= 470) *xl = 20 - (y - 380) * 20 / 90;
    else *xl = 0;
}

static void fill_field(Canvas *cv)
{
    int row;
    for (row = 0; row < PIC_H * S; row++) {
        /* the table y at this row (height 0) */
        int ymm = (int)(((int32_t)(OY * S - row) << 16) / (KY * S)), xl, xr;
        if (ymm < -60 || ymm > 1040) continue;
        field_edges(ymm, &xl, &xr);
        if (xl >= xr) continue;
        g_span_sub(cv, row, PXI(xl), PXI(xr), P_FIELD1);
        /* a darker band down the middle of the lanes, for depth */
        if (ymm > 380 && ymm < 760 && (row & 1)) g_span_sub(cv, row, PXI(xl + 30), PXI(xr < 470 ? xr - 30 : 440), P_FIELD2);
    }
}

static void dither_poly(Canvas *cv, const int16_t *xs, const int16_t *ys, int n, int col)
{
    static int16_t sl[SPAN_MAX], sr[SPAN_MAX];
    int ymin, i, x, rows = poly_spans(xs, ys, n, sl, sr, &ymin);
    for (i = 0; i < rows; i++) {
        int y = ymin + i;
        if (sl[i] > sr[i]) continue;
        (void)x;
        g_blend_sub(cv, y, sl[i], sr[i], col, 96);     /* tinted clear plastic */
    }
}

/* Catmull-Rom through a ramp's points, 4 steps a segment */
static int ramp_curve(const Ramp *r, int16_t out[][3], int max)
{
    int i, k, c, n = 0;
    for (i = 0; i + 1 < r->n; i++)
        for (k = 0; k < 4; k++) {
            const int16_t *p0 = r->pts[i > 0 ? i - 1 : 0], *p1 = r->pts[i], *p2 = r->pts[i + 1], *p3 = r->pts[i + 2 < r->n ? i + 2 : r->n - 1];
            int32_t t = k * 16, t2 = t * t, t3 = t2 * t;        /* t in 1/64 */
            if (n >= max) return n;
            for (c = 0; c < 3; c++) {
                int32_t v = 2 * p1[c] * 262144 + (-p0[c] + p2[c]) * t * 4096 + (2 * p0[c] - 5 * p1[c] + 4 * p2[c] - p3[c]) * t2 * 64 +
                            (-p0[c] + 3 * p1[c] - 3 * p2[c] + p3[c]) * t3;
                out[n][c] = (int16_t)(v / 524288);
            }
            n++;
        }
    if (n < max) { out[n][0] = r->pts[r->n - 1][0]; out[n][1] = r->pts[r->n - 1][1]; out[n][2] = r->pts[r->n - 1][2]; n++; }
    return n;
}

static void draw_ramp(Canvas *cv, const Ramp *r)
{
    int16_t pts[80][3];
    int n = ramp_curve(r, pts, 80), i, s;
    const int half = 22, rail = 14;
    /* the posts holding it up */
    for (i = 4; i < n - 2; i += 6)
        if (pts[i][2] > 12) line(cv, PXI(pts[i][0]), PYI(pts[i][1], 0), PXI(pts[i][0]), PYI(pts[i][1], pts[i][2]), P_METAL_D, 2);
    for (i = 0; i + 1 < n; i++) {
        int dx = pts[i + 1][0] - pts[i][0], dy = pts[i + 1][1] - pts[i][1], len = a3_hypot(dx, dy);
        int nx, ny;
        int16_t xs[4], ys[4];
        if (len == 0) continue;
        nx = -dy * half / len; ny = dx * half / len;
        xs[0] = (int16_t)PXI(pts[i][0] + nx); ys[0] = (int16_t)PYI(pts[i][1] + ny, pts[i][2]);
        xs[1] = (int16_t)PXI(pts[i + 1][0] + nx); ys[1] = (int16_t)PYI(pts[i + 1][1] + ny, pts[i + 1][2]);
        xs[2] = (int16_t)PXI(pts[i + 1][0] - nx); ys[2] = (int16_t)PYI(pts[i + 1][1] - ny, pts[i + 1][2]);
        xs[3] = (int16_t)PXI(pts[i][0] - nx); ys[3] = (int16_t)PYI(pts[i][1] - ny, pts[i][2]);
        dither_poly(cv, xs, ys, 4, P_GLASS);                    /* clear plastic: the table shows through */
        for (s = -1; s <= 1; s += 2)
            line(cv, PXI(pts[i][0] + s * nx), PYI(pts[i][1] + s * ny, pts[i][2] + rail),
                 PXI(pts[i + 1][0] + s * nx), PYI(pts[i + 1][1] + s * ny, pts[i + 1][2] + rail), P_METAL_L, 2);
    }
}

typedef struct { int16_t key, kind, idx; } Item;

static int seg_side(const Seg *s) { return s->kind == SK_OUTER ? P_CAB_D : s->kind == SK_SLING ? P_RUBBER_D : P_METAL_D; }
static int seg_top(const Seg *s) { return s->kind == SK_OUTER ? P_CAB_L : s->kind == SK_SLING ? P_RUBBER : P_METAL_L; }

static void theme_art(Canvas *cv, int theme)
{
    uint32_t r = 12345 + (uint32_t)theme;
    int i;
    for (i = 0; i < 160; i++) {
        int x = (int)(a3_rand(&r) % 500) + 10, y = (int)(a3_rand(&r) % 1060) - 40, xl, xr;
        field_edges(y, &xl, &xr);
        if (x < xl + 6 || x > (xr < 476 ? xr - 6 : 476)) continue;
        if (theme == TB_COMET) rect(cv, PXI(x), PYI(y, 0), PXI(x) + S / 2, PYI(y, 0) + S / 2, (i & 3) ? P_ART3 : P_TEXT);
        else if (theme == TB_DEEP) { if (i & 1) ellipse(cv, PXI(x), PYI(y, 0), S, S, P_ART2); }
        else if (!(i & 3)) rect(cv, PXI(x), PYI(y, 0), PXI(x) + 2 * S - 1, PYI(y, 0) + S - 1, P_ART2);
    }
}

static void draw_deco(Canvas *cv, const Deco *d)
{
    int i;
    switch (d->kind) {
    case DK_DISC: ellipse(cv, PXI(d->x), PYI(d->y, 0), rx_(FROMI(d->a)), ry_(FROMI(d->b)), d->col); break;
    case DK_RING:
        for (i = 0; i < 48; i++) {
            int a0 = i * 65536 / 48, a1 = (i + 1) * 65536 / 48;
            line(cv, PXI(d->x) + (int)((rx_(FROMI(d->a)) * a3_cos(a0)) >> 16), PYI(d->y, 0) + (int)((ry_(FROMI(d->b)) * a3_sin(a0)) >> 16),
                 PXI(d->x) + (int)((rx_(FROMI(d->a)) * a3_cos(a1)) >> 16), PYI(d->y, 0) + (int)((ry_(FROMI(d->b)) * a3_sin(a1)) >> 16), d->col, 2);
        }
        break;
    case DK_QUAD: {
        int16_t xs[4], ys[4];
        for (i = 0; i < 4; i++) { xs[i] = (int16_t)PXI(d->px[i]); ys[i] = (int16_t)PYI(d->py[i], 0); }
        g_poly_sub(cv, xs, ys, 4, d->col);
        break;
    }
    case DK_TEXT:
        if (d->text) {
            int w = (int)strlen(d->text) * 16;
            g_text_big(cv, PXI(PB_MID) / S - w / 2, PYI(d->y, 0) / S - 8, P_ART2, d->text, 2);
            g_text_big(cv, PXI(PB_MID) / S - w / 2 - 1, PYI(d->y, 0) / S - 9, d->col, d->text, 2);
        }
        break;
    }
}

/* The painted playfield (AmigaOS 4): a top-down picture of the table from
 * x 0..520 and y -60..1040 mm, made by tools/make_image.py at 572 x 968
 * (the view is a stretch: 0.55 across, 0.44 up the table, at twice the
 * resolution), drawn inside the cabinet's outline. */
static const char *const playfield_file[TB_COUNT] = {
    "PROGDIR:playfield_comet.img", "PROGDIR:playfield_deep.img", "PROGDIR:playfield_foundry.img",
};

static int paint_field(Canvas *cv, int theme)
{
    A3Image *im = g_image_load(playfield_file[theme]);
    int row, x0 = PXI(0), y0 = PYI(1040, 0), h, w;
    if (!im) return 0;
    w = g_image_w(im); h = g_image_h(im);
    for (row = 0; row < h; row++) {
        int py = y0 + row, ymm = (int)(((int32_t)(OY * S - py) << 16) / (KY * S)), xl, xr;
        if (ymm < -60 || ymm > 1040) continue;
        field_edges(ymm, &xl, &xr);
        g_image_row(cv, im, row, PXI(xl) - x0, PXI(xr) - x0 < w - 1 ? PXI(xr) - x0 : w - 1, x0, py);
    }
    g_image_free(im);
    return 1;
}

static void build_picture(Canvas *cv)
{
    static Item items[PB_MAX_SEG + PB_MAX_CIRC + 4];
    int n = 0, i, j, painted;
    const Table *t = &table;

    g_rect(cv, 0, 0, SCREEN_W - 1, PIC_H - 1, P_WOOD);
    painted = paint_field(cv, t->theme);
    if (!painted) {                             /* the classic Amiga, or no painting: the drawn art */
        fill_field(cv);
        theme_art(cv, t->theme);
        for (i = 0; i < t->ndeco; i++) draw_deco(cv, &t->deco[i]);
    }
    /* the inserts (lamps), unlit: on a painting, tinted see-through plastic */
    for (i = 0; i < L_COUNT; i++) {
        const Lamp *l = &t->lamp[i];
        int x = PXI(l->x), y = PYI(l->y, 0), rx = rx_(FROMI(l->r)), ry = ry_(FROMI(l->r));
        if (!l->r) continue;
        if (painted) {
            blend_ellipse(cv, x, y, rx + S, ry + S, P_BLACK, 150);
            blend_ellipse(cv, x, y, rx, ry, lamp_on[l->col], 110);
            continue;
        }
        ellipse(cv, x, y, rx + S, ry + S, P_BLACK);
        ellipse(cv, x, y, rx, ry, lamp_off[l->col]);
    }
    /* the saucer */
    ellipse(cv, px_(t->saucer_x), py_(t->saucer_y, 0), rx_(t->saucer_r + FROMI(4)), ry_(t->saucer_r + FROMI(4)), P_METAL_L);
    ellipse(cv, px_(t->saucer_x), py_(t->saucer_y, 0), rx_(t->saucer_r), ry_(t->saucer_r), P_BLACK);
    /* the drop targets' slots */
    for (i = 0; i < t->ndrop; i++) {
        const Seg *s = &t->seg[t->drop_seg[i]];
        line(cv, px_(s->ax), py_(s->ay, 0), px_(s->bx), py_(s->by, 0), P_BLACK, 2);
    }
    /* the walls, posts and bumpers, far to near */
    for (i = 0; i < t->nseg; i++) {
        const Seg *s = &t->seg[i];
        if (s->kind == SK_DROP || s->kind == SK_GATE) continue;
        items[n].key = (int16_t)(s->kind == SK_OUTER ? 30000 : FINT(s->ay < s->by ? s->ay : s->by));
        items[n].kind = 0; items[n].idx = (int16_t)i; n++;
    }
    for (i = 0; i < t->ncirc; i++) { items[n].key = (int16_t)FINT(t->circ[i].cy); items[n].kind = 1; items[n].idx = (int16_t)i; n++; }
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && items[j].key > items[j - 1].key; j--) { Item tmp = items[j]; items[j] = items[j - 1]; items[j - 1] = tmp; }
    for (i = 0; i < n; i++) {
        if (items[i].kind == 0) {
            const Seg *s = &t->seg[items[i].idx];
            wall3d(cv, s->ax, s->ay, s->bx, s->by, s->r, s->h, seg_side(s), seg_top(s));
        } else {
            const Circ *c = &t->circ[items[i].idx];
            if (c->bumper) {
                ellipse(cv, px_(c->cx), py_(c->cy, 0), rx_(c->r + FROMI(3)), ry_(c->r + FROMI(3)), P_RUBBER);
                cylinder(cv, c->cx, c->cy, c->r - FROMI(2), c->h - 8, P_METAL_D, P_RUBBER_D);
                cylinder(cv, c->cx, c->cy + FROMI(1), FMUL(c->r, FX(0.9)), c->h, P_RED_D, P_RED);
                ellipse(cv, px_(c->cx) - 3 * S, py_(c->cy, FROMI(c->h)) - S, rx_(c->r) / 3, ry_(c->r) / 3, P_LRED);
            } else cylinder(cv, c->cx, c->cy, c->r, c->h, P_RUBBER_D, P_RUBBER);
        }
    }
    /* the gates: a wire on two posts */
    for (i = 0; i < t->nseg; i++) {
        const Seg *s = &t->seg[i];
        if (s->kind != SK_GATE) continue;
        line(cv, px_(s->ax), py_(s->ay, FROMI(s->h)), px_(s->bx), py_(s->by, FROMI(s->h)), P_METAL_L, 1);
        line(cv, px_(s->ax), py_(s->ay, 0), px_(s->ax), py_(s->ay, FROMI(s->h)), P_METAL_D, 1);
        line(cv, px_(s->bx), py_(s->by, 0), px_(s->bx), py_(s->by, FROMI(s->h)), P_METAL_D, 1);
    }
    /* the spinners' brackets */
    for (i = 0; i < t->nsens; i++) {
        const Sensor *s = &t->sens[i];
        if (s->kind != SN_SPIN) continue;
        line(cv, px_(s->ax) + 2 * S, py_(s->ay, 0), px_(s->ax) + 2 * S, py_(s->ay, FROMI(30)), P_METAL_D, 2);
        line(cv, px_(s->bx) - 2 * S, py_(s->by, 0), px_(s->bx) - 2 * S, py_(s->by, FROMI(30)), P_METAL_D, 2);
    }
    /* the apron over the drain */
    {
        int16_t xs[4], ys[4];
        xs[0] = (int16_t)PXI(20); ys[0] = (int16_t)PYI(22, 20); xs[1] = (int16_t)PXI(474); ys[1] = (int16_t)PYI(22, 20);
        xs[2] = (int16_t)PXI(474); ys[2] = (int16_t)PYI(-60, 0); xs[3] = (int16_t)PXI(20); ys[3] = (int16_t)PYI(-60, 0);
        g_poly_sub(cv, xs, ys, 4, P_CAB_D);
        line(cv, PXI(20), PYI(22, 20), PXI(474), PYI(22, 20), P_CAB_L, 2);
        g_text_shadow(cv, PXI(PB_MID) / S - 52, PYI(-10, 20) / S, P_ART3, "BOING PINBALL");
    }
    /* the ramps, over everything */
    for (i = 0; i < t->nramp; i++) draw_ramp(cv, &t->ramp[i]);
}

int render_build(int which)
{
    Canvas *pic;
    static int made;
    if (!made) { make_ball(); made = 1; }
    pb_build_table(&table, which);
    pin.table = which;
    render_palette(table.theme);
    pic = g_pic_alloc(PIC_H);
    if (!pic) return 0;
    oy = 0;
    g_clip_y0 = 0; g_clip_y1 = PIC_H - 1;
    build_picture(pic);
    g_sync();
    g_clip_y0 = 0; g_clip_y1 = SCREEN_H - 1;
    return 1;
}

/* ---------------------------------------------------------------- the view */

static int ball_row(const Ball *b) { int o = oy, r; oy = 0; r = py_(b->y, PB_R + b->z) / S; oy = o; return r; }

void render_follow(int snap)
{
    int i, target = CAM_MAX, best = 1 << 30, d, step;
    for (i = 0; i < game.w.nballs; i++) {
        const Ball *b = &game.w.balls[i];
        if (b->state == BS_DRAINED || b->state == BS_LANE) continue;
        if (b->y < best) { best = b->y; target = ball_row(b) - VIEW_H / 2 - (b->vy > FROMI(1500) ? 40 : 0); }
    }
    if (target < 0) target = 0;
    if (target > CAM_MAX) target = CAM_MAX;
    d = target - pin.cam;
    if (snap) { pin.cam = target; return; }
    step = d / 2;
    if (step == 0) step = d;
    pin.cam += step;
}

/* ---------------------------------------------------------------- the dot-matrix display */

static int dmd_width(const char *s, int sx) { return (int)strlen(s) * 8 * sx; }

static void dmd_text(Canvas *cv, const char *s, int y0, int sx, int sy, int col)
{
    int x0 = (SCREEN_W - dmd_width(s, sx)) / 2, r, b, k;
#if A3_SUB > 1
    /* at the finer resolution: a round dot in each cell, a gap round it */
    {
        int cw = sx * S, ch = sy * S, rd = (cw < ch ? cw : ch) / 2 - 1;
        for (; *s; s++, x0 += 8 * sx) {
            const uint8_t *g = g_glyph_bits((unsigned char)*s);
            for (r = 0; r < 8; r++)
                for (b = 0; b < 8; b++) {
                    int cx = x0 * S + b * cw + cw / 2, cy = y0 * S + r * ch + ch / 2;
                    if (!(g[r] & (0x80 >> b))) continue;
                    if (rd >= 2) ellipse(cv, cx, cy, rd, rd, col);
                    else rect(cv, x0 * S + b * cw, y0 * S + r * ch, x0 * S + b * cw + cw - (cw >= 3 ? 2 : 1), y0 * S + r * ch + ch - 2, col);
                }
        }
        (void)k;
        return;
    }
#endif
    for (; *s; s++, x0 += 8 * sx) {
        const uint8_t *g = g_glyph_bits((unsigned char)*s);
        for (r = 0; r < 8; r++) {
            uint8_t bits = g[r];
            for (b = 0; b < 8; b++) {
                int e = b;
                if (!(bits & (0x80 >> b))) continue;
                while (e < 7 && (bits & (0x80 >> (e + 1)))) e++;
                for (k = 0; k < sy; k += 2)
                    g_hline(cv, x0 + b * sx, x0 + e * sx + sx - 1, y0 + r * sy + k, col);
                b = e;
            }
        }
    }
}

static void draw_dmd(Canvas *cv, const char *big, const char *small, int flash)
{
    int col = flash && (pin.frame & 4) ? P_DMD_MID : P_DMD_ON;
    g_clip_y0 = 0; g_clip_y1 = VIEW_Y - 1;
    g_fill(cv, 0, 0, SCREEN_W - 1, VIEW_Y - 1, P_DMD_BG);
    g_sync();
    if (!small || !small[0]) {
        if (dmd_width(big, 4) <= SCREEN_W) dmd_text(cv, big, 0, 4, 4, col);
        else if (dmd_width(big, 2) <= SCREEN_W) dmd_text(cv, big, 0, 2, 4, col);
        else dmd_text(cv, big, 9, 1, 2, col);
    } else {
        dmd_text(cv, big, 1, dmd_width(big, 2) <= SCREEN_W ? 2 : 1, 2, col);
        dmd_text(cv, small, 17, 1, 2, col);
    }
    g_clip_y0 = 0; g_clip_y1 = SCREEN_H - 1;
}

/* ---------------------------------------------------------------- what moves */

static void draw_flipper(Canvas *cv, const Flipper *f)
{
    fix tx, ty, c = fcos(f->ang), s = fsin(f->ang);
    int lvl;
    static const int z[3] = { 0, 11, 22 };
    static const uint8_t col[3] = { P_RED_D, P_RED, P_RUBBER };
    pb_flipper_tip(f, &tx, &ty);
    for (lvl = 0; lvl < 3; lvl++) {
        fix Z = FROMI(z[lvl]);
        fix n1x = FMUL(-s, f->d.r1), n1y = FMUL(c, f->d.r1), n2x = FMUL(-s, f->d.r2), n2y = FMUL(c, f->d.r2);
        int16_t xs[4], ys[4];
        xs[0] = (int16_t)px_(f->d.px + n1x); ys[0] = (int16_t)py_(f->d.py + n1y, Z);
        xs[1] = (int16_t)px_(tx + n2x); ys[1] = (int16_t)py_(ty + n2y, Z);
        xs[2] = (int16_t)px_(tx - n2x); ys[2] = (int16_t)py_(ty - n2y, Z);
        xs[3] = (int16_t)px_(f->d.px - n1x); ys[3] = (int16_t)py_(f->d.py - n1y, Z);
        g_poly_sub(cv, xs, ys, 4, col[lvl]);
        ellipse(cv, px_(f->d.px), py_(f->d.py, Z), rx_(f->d.r1), ry_(f->d.r1), col[lvl]);
        ellipse(cv, px_(tx), py_(ty, Z), rx_(f->d.r2), ry_(f->d.r2), col[lvl]);
    }
    ellipse(cv, px_(f->d.px), py_(f->d.py, FROMI(22)), S, S, P_METAL_D);   /* the pivot's screw */
}

static void draw_moving(Canvas *cv)
{
    const Table *t = &table;
    uint8_t on[L_COUNT];
    int i, top = pin.cam - 24, bot = pin.cam + VIEW_H + 24, blink = (pin.frame / 5) & 1;
    #define VISIBLE(yrow) ((yrow) / S - oy >= top && (yrow) / S - oy <= bot)

    /* the lit lamps */
    pb_lamps(&game, blink, pin.frame / 4, on);
    for (i = 0; i < L_COUNT; i++) {
        const Lamp *l = &t->lamp[i];
        int y;
        if (!l->r || !on[i]) continue;
        y = PYI(l->y, 0);
        if (!VISIBLE(y)) continue;
        blend_ellipse(cv, PXI(l->x), y, rx_(FROMI(l->r)) * 2, ry_(FROMI(l->r)) * 2, lamp_on[l->col], 70);   /* its glow */
        ellipse(cv, PXI(l->x), y, rx_(FROMI(l->r)), ry_(FROMI(l->r)), lamp_on[l->col]);
        ellipse(cv, PXI(l->x) - S, y - S, rx_(FROMI(l->r)) / 2, ry_(FROMI(l->r)) / 2, P_TEXT);
    }
    /* standing drop targets */
    for (i = 0; i < t->ndrop; i++) {
        const Seg *s = &t->seg[t->drop_seg[i]];
        if (!s->up || !VISIBLE(py_(s->ay, 0))) continue;
        wall3d(cv, s->ax, s->ay, s->bx, s->by, FROMI(3), 26, P_YEL_D, P_ORANGE);
    }
    /* flashing bumpers and slings */
    for (i = 0; i < t->ncirc; i++) {
        const Circ *c = &t->circ[i];
        if (!c->bumper || pin.bumper_flash[i] <= 0) continue;
        blend_ellipse(cv, px_(c->cx), py_(c->cy, FROMI(c->h)), rx_(c->r) * 2, ry_(c->r) * 2, P_FLASH, 80);
        ellipse(cv, px_(c->cx), py_(c->cy + FROMI(1), FROMI(c->h)), rx_(FMUL(c->r, FX(0.9))), ry_(FMUL(c->r, FX(0.9))), P_FLASH);
    }
    for (i = 0; i < t->nseg; i++) {
        const Seg *s = &t->seg[i];
        if (s->kind != SK_SLING || pin.sling_flash[s->id & 1] <= 0) continue;
        line(cv, px_(s->ax), py_(s->ay, FROMI(s->h)), px_(s->bx), py_(s->by, FROMI(s->h)), P_FLASH, 3);
    }
    /* the spinners */
    for (i = 0; i < t->nsens; i++) {
        const Sensor *s = &t->sens[i];
        int a, h, yt;
        if (s->kind != SN_SPIN) continue;
        yt = py_(s->ay, FROMI(30));
        if (!VISIBLE(yt)) continue;
        a = a3_angle(game.w.spin[s->idx].ang);
        h = (int)((22 * a3_cos(a)) >> 16);
        rect(cv, px_(s->ax) + 4 * S, yt, px_(s->bx) - 4 * S, py_(s->ay, FROMI(30 - (h < 0 ? -h : h))), h >= 0 ? P_METAL_L : P_METAL_D);
        line(cv, px_(s->ax) + 2 * S, yt, px_(s->bx) - 2 * S, yt, P_METAL_L, 1);
    }
    /* the plunger */
    {
        fix top_y = FROMI(30) - FMUL(game.pull, FROMI(22));
        int yk = py_(top_y - FROMI(6), FROMI(8));
        if (VISIBLE(yk)) {
            line(cv, px_(t->plunger_x), yk, px_(t->plunger_x), py_(FROMI(-40), FROMI(8)), P_METAL_D, 3);
            ellipse(cv, px_(t->plunger_x), yk, 5 * S, 3 * S, P_METAL_L);
        }
    }
    /* the flippers */
    for (i = 0; i < game.w.nflip; i++) {
        const Flipper *f = &game.w.flip[i];
        if (VISIBLE(py_(f->d.py, 0))) draw_flipper(cv, f);
    }
    /* the balls: shadows, then the balls far to near (balls on a ramp last) */
    {
        int order[PB_MAX_BALLS], n = 0, j;
        for (i = 0; i < game.w.nballs; i++) {
            const Ball *b = &game.w.balls[i];
            if (b->state == BS_DRAINED) continue;
            if (b->y < FROMI(22) && b->x < FROMI(474)) continue;          /* under the apron */
            order[n++] = i;
        }
        for (i = 1; i < n; i++)
            for (j = i; j > 0; j--) {
                const Ball *a = &game.w.balls[order[j - 1]], *b = &game.w.balls[order[j]];
                int ka = a->state == BS_RAMP ? -1 : 0, kb = b->state == BS_RAMP ? -1 : 0;
                if (ka < kb || (ka == kb && a->y >= b->y)) break;
                { int tmp = order[j]; order[j] = order[j - 1]; order[j - 1] = tmp; }
            }
        for (i = 0; i < n; i++) {
            const Ball *b = &game.w.balls[order[i]];
            int x = px_(b->x), y = py_(b->y, 0);
            if (VISIBLE(y)) blend_ellipse(cv, x + 2 * S, y + S, BR, BR - 2 * S, P_BLACK, 120);
        }
        for (i = n - 1; i >= 0; i--) {
            const Ball *b = &game.w.balls[order[i]];
            int x = px_(b->x), y = py_(b->y, PB_R + b->z), id = b->id & 7;
            /* the spin: one frame per 1.3 mm rolled */
            fix dx = b->x - pin.ball_px[id], dy = b->y - pin.ball_py[id];
            int moved = FINT((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
            if (moved < 200) pin.ball_phase[id] += (b->vx + b->vy >= 0 ? 1 : -1) * ((moved * 3) / 4 + (moved ? 1 : 0));
            pin.ball_px[id] = b->x; pin.ball_py[id] = b->y;
            if (VISIBLE(y)) draw_ball(cv, x, y, pin.ball_phase[id] & 0x7FFF);
        }
    }
    #undef VISIBLE
}

static void centre_box(Canvas *cv, int y0, int y1)
{
    g_box(cv, 24, y0, SCREEN_W - 25, y1, P_BLACK, P_ART3);
}

void render_game(Canvas *cv)
{
    Dmd d;
    oy = VIEW_Y - pin.cam;
    g_pic_blit(cv, pin.cam, VIEW_Y, VIEW_H);
    g_clip_y0 = VIEW_Y; g_clip_y1 = SCREEN_H - 1;
    draw_moving(cv);
    g_clip_y0 = 0; g_clip_y1 = SCREEN_H - 1;
    pb_dmd(&game, &d);
    if (pin.state == PS_OVER) draw_dmd(cv, "GAME OVER", d.big ? d.small : "", 0);
    else draw_dmd(cv, d.big, d.small, d.flash);
    if (pin.state == PS_OVER) {
        char s[48], n[16];
        centre_box(cv, 96, 170);
        g_text_big(cv, (SCREEN_W - 9 * 16) / 2, 104, P_DMD_ON, "GAME OVER", 2);
        pb_fmt(n, game.score);
        sprintf(s, "SCORE %s", n);
        g_text_centre(cv, 136, P_TEXT, s);
        pb_fmt(n, pin.best[pin.table]);
        sprintf(s, "BEST  %s", n);
        g_text_centre(cv, 150, game.score >= pin.best[pin.table] && game.score ? P_YEL : P_RUBBER_D, s);
#ifdef AMIGA3DO
        if (!pin.state_timer) g_text_centre(cv, 164, P_BLUE, "A: THE TITLE");
#else
        if (!pin.state_timer) g_text_centre(cv, 164, P_BLUE, "FIRE: THE TITLE");
#endif
    }
    if (pin.paused == 1) { centre_box(cv, 120, 140); g_text_centre(cv, 134, P_TEXT, "PAUSED  (P)"); }
}

/* The backglass (AmigaOS 4): the title's attract page, each table's
 * painting with the logo (`make art`), shown for a while, then the bot's
 * demo on the table, in turn; a new table starts on its backglass. */
static const char *const backglass_file[TB_COUNT] = {
    "PROGDIR:title_comet.img", "PROGDIR:title_deep.img", "PROGDIR:title_foundry.img",
};
static A3Image *backglass[TB_COUNT];
static int backglass_loaded, title_frame0;
#define TITLE_ART_FRAMES  250           /* 10 s */
#define TITLE_DEMO_FRAMES 375

void render_title_start(void) { title_frame0 = pin.frame; }

static void text_big_shadow(Canvas *cv, int y, int col, const char *s)
{
    int x = (SCREEN_W - (int)strlen(s) * 16) / 2;
    g_text_big(cv, x + 1, y + 1, P_BLACK, s, 2);
    g_text_big(cv, x, y, col, s, 2);
}

static int title_art(Canvas *cv)
{
    char s[48], n[16];
    int i;
    if (!backglass_loaded) {
        for (i = 0; i < TB_COUNT; i++) backglass[i] = g_image_load(backglass_file[i]);
        backglass_loaded = 1;
    }
    if (!backglass[pin.table] || (pin.frame - title_frame0) % (TITLE_ART_FRAMES + TITLE_DEMO_FRAMES) >= TITLE_ART_FRAMES)
        return 0;
    g_image_draw(cv, backglass[pin.table], 0, 0);
    sprintf(s, "< %s >", table.name);
    text_big_shadow(cv, 186, P_DMD_ON, s);
    g_text_centre(cv, 220, P_TEXT, table.blurb);
    pb_fmt(n, pin.best[pin.table]);
    sprintf(s, "BEST %s", n);
    g_text_centre(cv, 234, P_YEL, s);
#ifdef AMIGA3DO
    g_text_centre(cv, 249, P_BLUE, (pin.frame / 25) & 1 ? "PRESS A TO PLAY" : "L/R: TABLE   C: RULES");
#else
    g_text_centre(cv, 249, P_BLUE, (pin.frame / 25) & 1 ? "PRESS FIRE TO PLAY" : "LEFT/RIGHT: TABLE   HELP: RULES");
#endif
    return 1;
}

void render_title(Canvas *cv)
{
    char s[48], n[16];
    if (title_art(cv)) return;
    oy = VIEW_Y - pin.cam;
    g_pic_blit(cv, pin.cam, VIEW_Y, VIEW_H);
    g_clip_y0 = VIEW_Y; g_clip_y1 = SCREEN_H - 1;
    draw_moving(cv);
    g_clip_y0 = 0; g_clip_y1 = SCREEN_H - 1;
    if (backglass[pin.table]) {                 /* the backglass has the chooser; the table plays clear */
#ifdef AMIGA3DO
        draw_dmd(cv, table.name, (pin.frame / 50) & 1 ? "PRESS A TO PLAY" : "L/R: TABLE", 0);
#else
        draw_dmd(cv, table.name, (pin.frame / 50) & 1 ? "PRESS FIRE TO PLAY" : "LEFT/RIGHT: TABLE", 0);
#endif
        return;
    }
#ifdef AMIGA3DO
    draw_dmd(cv, "BOING PINBALL", (pin.frame / 50) & 1 ? "PRESS A TO PLAY" : "THREE TABLES  C FOR THE RULES", 0);
#else
    draw_dmd(cv, "BOING PINBALL", (pin.frame / 50) & 1 ? "PRESS FIRE TO PLAY" : "THREE TABLES  HELP FOR THE RULES", 0);
#endif
    centre_box(cv, 44, 118);
    sprintf(s, "< %s >", table.name);
    g_text_big(cv, (SCREEN_W - (int)strlen(s) * 16) / 2, 52, P_DMD_ON, s, 2);
    g_text_centre(cv, 82, P_RUBBER, table.blurb);
    pb_fmt(n, pin.best[pin.table]);
    sprintf(s, "BEST %s", n);
    g_text_centre(cv, 96, P_YEL, s);
#ifdef AMIGA3DO
    g_text_centre(cv, 110, P_BLUE, "A: PLAY   L/R: TABLE");
#else
    g_text_centre(cv, 110, P_BLUE, "FIRE: PLAY   LEFT/RIGHT: TABLE");
#endif
}

void render_help(Canvas *cv, int page)
{
    static const char *const p1[] = {
        "HOW TO PLAY",
        "",
#ifdef AMIGA3DO
        "L, LEFT           LEFT FLIPPERS",
#else
        "LEFT SHIFT  Z     LEFT FLIPPERS",
#endif
#ifdef AMIGA3DO
        "R, RIGHT          RIGHT FLIPPERS",
#else
        "RIGHT SHIFT /     RIGHT FLIPPERS",
#endif
#ifdef AMIGA3DO
        "A, DOWN           PLUNGER: HOLD TO",
#else
        "SPACE  DOWN       PLUNGER: HOLD TO",
#endif
        "                  PULL, LET GO",
#ifdef AMIGA3DO
        "UP                NUDGE THE TABLE",
#else
        "LEFT RIGHT UP     NUDGE THE TABLE",
#endif
#ifdef AMIGA3DO
        "P  PAUSE          X  THE MENU",
#else
        "P  PAUSE          ESC  THE TITLE",
#endif
        "",
#ifdef AMIGA3DO
        "B: THE BOT PLAYS ON / OFF",
#else
        "JOYSTICK: LEFT AND RIGHT FLIP,",
#endif
#ifdef AMIGA3DO
        "",
#else
        "FIRE OR DOWN: THE PLUNGER, UP: NUDGE",
#endif
        "",
        "NUDGE TOO OFTEN: TWO WARNINGS,",
        "THEN TILT - THE FLIPPERS GO DEAD",
        "AND THE BALL SCORES NO BONUS.",
        "",
#ifdef AMIGA3DO
        "C: THE RULES",
#else
        "HELP: THE RULES",
#endif
    };
    static const char *const p2[] = {
        "THE RULES",
        "",
        "SKILL SHOT: THE FLASHING TOP LANE",
        "  ON THE PLUNGE - 10,000",
        "TOP LANES: LIGHT ALL THREE FOR A",
        "  BONUS MULTIPLIER (FLIPPERS MOVE",
        "  THE LIT LANES ROUND)",
        "RAMP: THREE LOCK MULTIBALL; IN",
        "  MULTIBALL EACH RAMP IS A JACKPOT",
        "SAUCER (LIT RED): STARTS A MISSION",
        "  - BUMPERS, TARGET BANKS, SPINS,",
        "  RAMPS OR LANE SETS IN 40 SECONDS",
        "ALL FIVE MISSIONS: SUPERNOVA,",
        "  EVERYTHING SCORES 3X",
        "BALL SAVE: THE FIRST 10 SECONDS",
        "",
#ifdef AMIGA3DO
        "C: CLOSE",
#else
        "HELP: CLOSE",
#endif
    };
    const char *const *p = page == 2 ? p2 : p1;
    int i, n = page == 2 ? (int)(sizeof p2 / sizeof p2[0]) : (int)(sizeof p1 / sizeof p1[0]);
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, P_BLACK);
    g_sync();
    g_box(cv, 8, 8, SCREEN_W - 9, SCREEN_H - 9, P_SHADOW, P_ART3);
    g_text_big(cv, (SCREEN_W - (int)strlen(p[0]) * 16) / 2, 18, P_DMD_ON, p[0], 2);
    for (i = 2; i < n; i++) g_text(cv, 18, 44 + (i - 2) * 12, i == n - 1 ? P_BLUE : P_TEXT, p[i]);
}
