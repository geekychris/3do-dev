// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Terra Strike renderer.
 *
 * Each frame the visible block of terrain tiles and every model are
 * transformed into camera space, clipped (near plane, then the screen
 * rectangle) and dropped into depth buckets. The buckets are drawn far to
 * near, so nearer hills cover farther ones - a painter's algorithm, as
 * Zarch did it.
 *
 * Models are keyed to the depth of the tile they stand over, so they draw
 * just after their own tile and before anything nearer.
 *
 * The drawing primitives at the top are the only part that differs between
 * the classic Amiga (spans written straight into the five bitplanes,
 * graphics.library for rectangles and text) and AmigaOS 4 (a buffer of
 * 8-bit pens, with the topaz 8 glyphs read out of the system font).
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/text.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <string.h>
#include <stdio.h>

#include "ts.h"
#include "models.h"
#include "render.h"
#include "title_image.h"
#ifdef AMIGA3DO
#include "cels_3do.h"
#endif

#if defined(__PPC__) || defined(AMIGA3DO)
/* ---------- drawing primitives: 8-bit pens (AmigaOS 4, and the 3DO) ---------- */
static uint8_t glyph[256][8];           /* topaz 8, one byte per row, bit 7 left */
static int glyph_base = 6;              /* baseline row, as Text() uses it */
static uint8_t title_pix[TITLE_W * TITLE_H];

#ifdef AMIGA3DO
/* the 3DO has no topaz: sdk/amiga's 8x8 font (bit 0 its left column) */
#include "font8x8_basic.h"
static void load_font(void)
{
    int c, r, b;
    for (c = 0; c < 128; c++)
        for (r = 0; r < 8; r++) {
            uint8_t bits = 0;
            for (b = 0; b < 8; b++) if (font8x8_basic[c][r] & (1 << b)) bits |= (uint8_t)(0x80 >> b);
            glyph[c][r] = bits;
        }
}
#else
/* the topaz 8 bitmap out of the ROM font, so the text matches the 68k game */
static void load_font(void)
{
    struct TextAttr ta = { (STRPTR)"topaz.font", 8, 0, 0 };
    struct TextFont *tf = OpenFont(&ta);
    int c, r, b;
    if (!tf) return;
    glyph_base = tf->tf_Baseline;
    for (c = tf->tf_LoChar; c <= tf->tf_HiChar && c < 256; c++) {
        const ULONG *loc = (const ULONG *)tf->tf_CharLoc + (c - tf->tf_LoChar);
        int off = (int)(*loc >> 16), w = (int)(*loc & 0xFFFF);
        const UBYTE *data = (const UBYTE *)tf->tf_CharData;
        if (w > 8) w = 8;
        for (r = 0; r < 8 && r < tf->tf_YSize; r++) {
            uint8_t bits = 0;
            for (b = 0; b < w; b++) {
                int bit = off + b;
                if (data[r * tf->tf_Modulo + (bit >> 3)] & (0x80 >> (bit & 7))) bits |= 0x80 >> b;
            }
            glyph[c][r] = bits;
        }
    }
    CloseFont(tf);
}
#endif

/* the artwork's five planes as pens, once */
static void decode_title(void)
{
    int y, x, p;
    for (y = 0; y < TITLE_H; y++)
        for (x = 0; x < TITLE_W; x++) {
            int pen = 0, o = y * (TITLE_W / 8) + (x >> 3), m = 0x80 >> (x & 7);
            for (p = 0; p < TITLE_DEPTH; p++)
                if (title_planes[p][o] & m) pen |= 1 << p;
            title_pix[y * TITLE_W + x] = (uint8_t)pen;
        }
}

int render_init(void)
{
    load_font();
    decode_title();
    models_init();
    return 1;
}

void render_cleanup(void) {}

static void g_rect(Canvas *cv, int x0, int y0, int x1, int y1, int col)
{
    int y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W - 1) x1 = SCREEN_W - 1;
    if (y1 > SCREEN_H - 1) y1 = SCREEN_H - 1;
    if (x0 > x1) return;
    for (y = y0; y <= y1; y++) memset(cv->pix + y * SCREEN_W + x0, col, x1 - x0 + 1);
}

static void g_span(Canvas *cv, int y, int xl, int xr, int col)
{
    memset(cv->pix + y * SCREEN_W + xl, col, xr - xl + 1);
}

/* like Move(x, y) + Text(): y is the baseline, JAM1 */
static void g_text(Canvas *cv, int x, int y, int col, const char *s)
{
    int r, b;
    y -= glyph_base;
    for (; *s; s++, x += 8)
        for (r = 0; r < 8; r++) {
            int yy = y + r;
            uint8_t bits = glyph[(unsigned char)*s][r];
            if (!bits || yy < 0 || yy >= SCREEN_H) continue;
            for (b = 0; b < 8; b++)
                if ((bits & (0x80 >> b)) && x + b >= 0 && x + b < SCREEN_W)
                    cv->pix[yy * SCREEN_W + x + b] = (uint8_t)col;
        }
}

static void g_begin(Canvas *cv) { (void)cv; }

static void g_title(Canvas *cv)
{
    int i;
#ifdef AMIGA3DO
    (void)i;
    memcpy(cv->pix, title_pix, TITLE_W * TITLE_H);     /* the 3DO's libc memcpy is fine (and STM) */
#else
    /* a plain loop: newlib's memcpy is unreliable for big copies on OS4 */
    for (i = 0; i < TITLE_W * TITLE_H; i++) cv->pix[i] = title_pix[i];
#endif
}

#else
/* ---------- drawing primitives: bitplanes and graphics.library ---------- */
#define AREA_MAXV 40
static WORD area_buf[(AREA_MAXV * 5 + 1) / 2];
static struct AreaInfo area_info;
static struct TmpRas tmp_ras;
static PLANEPTR tmp_plane;

int render_init(void)
{
    tmp_plane = AllocRaster(320, 256);
    if (!tmp_plane) return 0;
    InitTmpRas(&tmp_ras, tmp_plane, RASSIZE(320, 256));
    InitArea(&area_info, area_buf, AREA_MAXV);
    models_init();
    return 1;
}

void render_attach(struct RastPort *rp)
{
    rp->TmpRas = &tmp_ras;
    rp->AreaInfo = &area_info;
}

void render_cleanup(void)
{
    if (tmp_plane) { FreeRaster(tmp_plane, 320, 256); tmp_plane = NULL; }
}

static void g_rect(Canvas *rp, int x0, int y0, int x1, int y1, int col)
{
    SetAPen(rp, col);
    RectFill(rp, x0, y0, x1, y1);
}

/* Fill one row segment xl..xr with pen col straight into the bitplanes.
 * Works for interleaved and plain bitmaps (BytesPerRow covers both). */
static void g_span(Canvas *rp, int y, int xl, int xr, int col)
{
    PLANEPTR *planes = rp->BitMap->Planes;
    LONG bpr = rp->BitMap->BytesPerRow;
    int wl = xl >> 4, wr = xr >> 4, p, k, n = wr - wl;
    UWORD lm = 0xFFFF >> (xl & 15), rm = 0xFFFF << (15 - (xr & 15));
    LONG off = y * bpr + wl * 2;

    if (n == 0) lm &= rm;
    for (p = 0; p < 5; p++, col >>= 1) {
        UWORD *d = (UWORD *)(planes[p] + off);
        if (col & 1) {
            d[0] |= lm;
            if (n) { for (k = 1; k < n; k++) d[k] = 0xFFFF; d[n] |= rm; }
        } else {
            d[0] &= ~lm;
            if (n) { for (k = 1; k < n; k++) d[k] = 0; d[n] &= ~rm; }
        }
    }
}

static void g_text(Canvas *rp, int x, int y, int col, const char *s)
{
    SetAPen(rp, col);
    Move(rp, x, y);
    Text(rp, (STRPTR)s, strlen(s));
}

/* the CPU writes the planes next: let the blitter finish first */
static void g_begin(Canvas *rp)
{
    SetDrMd(rp, JAM1);
    WaitBlit();
}

/* the converted artwork, copied straight into the bitplanes */
static void g_title(Canvas *rp)
{
    struct BitMap *bm = rp->BitMap;
    LONG bpr = bm->BytesPerRow;
    int p, y;

    WaitBlit();
    for (p = 0; p < TITLE_DEPTH; p++) {
        const UBYTE *src = title_planes[p];
        UBYTE *dst = (UBYTE *)bm->Planes[p];
        for (y = 0; y < TITLE_H; y++, src += TITLE_W / 8, dst += bpr)
            memcpy(dst, src, TITLE_W / 8);
    }
    SetDrMd(rp, JAM1);
}
#endif

/* ---------- draw list ---------- */
typedef struct DPoly {
    struct DPoly *next;
    WORD n;             /* 0 = rectangle x[0],y[0]-x[1],y[1] */
    UBYTE col;
    int16_t x[MAX_PV], y[MAX_PV];
} DPoly;

#define MAX_DPOLY 640
#define NBUCKET   256
static DPoly pool[MAX_DPOLY];
static int npoly;
static DPoly *bhead[NBUCKET], *btail[NBUCKET];
RenderStats render_stats;

static int bucket_of(int32_t depth)
{
    int32_t b = (depth * NBUCKET) / FAR_Z;
    if (b < 0) b = 0;
    if (b >= NBUCKET) b = NBUCKET - 1;
    return (int)b;
}

static DPoly *alloc_poly(int32_t depth)
{
    DPoly *p;
    int b;
    if (npoly >= MAX_DPOLY) { render_stats.dropped++; return NULL; }
    p = &pool[npoly++];
    p->next = NULL;
    b = bucket_of(depth);
    if (btail[b]) btail[b]->next = p; else bhead[b] = p;
    btail[b] = p;
    return p;
}

static void add_poly(int32_t depth, int col, V3 *cv, int n)
{
    V3 cl[MAX_PV];
    int32_t xs[MAX_PV], ys[MAX_PV];
    DPoly *p;
    int i;

    n = clip_near(cv, n, cl);
    if (n < 3) return;
#ifdef AMIGA3DO                         /* the 3DO: a reciprocal, not two software divides (cels_3do.c) */
    for (i = 0; i < n; i++) {
        xs[i] = VIEW_CX + ts3_div(cl[i].x * FOV, cl[i].z);
        ys[i] = VIEW_CY - ts3_div(cl[i].y * FOV, cl[i].z);
    }
#else
    for (i = 0; i < n; i++) cam_project(&cl[i], &xs[i], &ys[i]);
#endif
    n = clip_rect(xs, ys, n, 0, 0, VIEW_W - 1, VIEW_H - 1);
    if (n < 3) return;
    p = alloc_poly(depth);
    if (!p) return;
    p->n = (WORD)n;
    p->col = (UBYTE)col;
    for (i = 0; i < n; i++) { p->x[i] = (WORD)xs[i]; p->y[i] = (WORD)ys[i]; }
}

static void add_rect(int32_t depth, int col, int32_t x, int32_t y, int size)
{
    DPoly *p;
    if (x + size < 0 || y + size < 0 || x >= VIEW_W || y >= VIEW_H) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    p = alloc_poly(depth);
    if (!p) return;
    p->n = 0;
    p->col = (UBYTE)col;
    p->x[0] = (WORD)x; p->y[0] = (WORD)y;
    p->x[1] = (WORD)(x + size > VIEW_W - 1 ? VIEW_W - 1 : x + size);
    p->y[1] = (WORD)(y + size > VIEW_H - 1 ? VIEW_H - 1 : y + size);
}

/* ---------- camera ---------- */
void camera_follow(Cam *c, const World *w)
{
    int32_t tx, ty, tz, dist, up, g;
    if (w->state == GS_TITLE) {
        pad_centre(w, &tx, &tz);
        ty = w->pad_h;
        c->yaw = (w->frame >> 1) & 255;
        c->pitch = 18;
        dist = 1300;
        up = 700;
    } else {
        const Craft *p = &w->p;
        int d = ((p->yaw - c->yaw + 128) & 255) - 128;
        tx = p->x >> FBITS; ty = p->y >> FBITS; tz = p->z >> FBITS;
        c->yaw = (c->yaw + (d / 3 ? d / 3 : d)) & 255;
        c->pitch = 15;
        dist = 560;
        up = 250;
    }
    c->x = ts_wrap(tx - ((TS_SIN(c->yaw) * dist) >> 16));
    c->z = ts_wrap(tz - ((TS_COS(c->yaw) * dist) >> 16));
    c->y = ty + up;
    g = ground_height(w, c->x, c->z) + 60;
    if (c->y < g) c->y = g;
    cam_setup(c);
}

/* ---------- terrain ---------- */
#define GR  7                   /* tiles each side of the grid centre */
#define GN  (2 * GR + 2)        /* vertices per side */
static V3 gv[GN * GN];
static int16_t graw[GN * GN];
static int32_t tdepth[(GN - 1) * (GN - 1)];
static int gx0, gz0;            /* tile coords of the grid's corner */

static void draw_terrain(const World *w, const Cam *c)
{
    int i, j;
    int32_t fx = c->x + ((TS_SIN(c->yaw) * (5 * TILE)) >> 16);
    int32_t fz = c->z + ((TS_COS(c->yaw) * (5 * TILE)) >> 16);

    gx0 = (int)(ts_wrap(fx) >> TILE_BITS) - GR;
    gz0 = (int)(ts_wrap(fz) >> TILE_BITS) - GR;

    for (j = 0; j < GN; j++)
        for (i = 0; i < GN; i++) {
            int tx = gx0 + i, tz = gz0 + j;
            int32_t h = terrain_vertex(w, tx, tz);
            graw[j * GN + i] = (int16_t)h;
            cam_xform(c, (int32_t)tx << TILE_BITS, h < SEA_LEVEL ? SEA_LEVEL : h,
                      (int32_t)tz << TILE_BITS, &gv[j * GN + i]);
        }

    for (j = 0; j < GN - 1; j++)
        for (i = 0; i < GN - 1; i++) {
            int ia = j * GN + i, ib = ia + 1, ic = ia + GN + 1, id = ia + GN;
            V3 q[4];
            int32_t depth;
            int pen, chk = (gx0 + i + gz0 + j) & 1;
            int32_t ha = graw[ia], hb = graw[ib], hc = graw[ic], hd = graw[id];

            q[0] = gv[ia]; q[1] = gv[ib]; q[2] = gv[ic]; q[3] = gv[id];
            depth = (q[0].z + q[1].z + q[2].z + q[3].z) >> 2;
            tdepth[j * (GN - 1) + i] = depth;
            if (q[0].z < NEAR_Z && q[1].z < NEAR_Z && q[2].z < NEAR_Z && q[3].z < NEAR_Z) continue;
            if (q[0].z > FAR_Z && q[1].z > FAR_Z && q[2].z > FAR_Z && q[3].z > FAR_Z) continue;
            if (q[0].x > q[0].z && q[1].x > q[1].z && q[2].x > q[2].z && q[3].x > q[3].z) continue;
            if (q[0].x < -q[0].z && q[1].x < -q[1].z && q[2].x < -q[2].z && q[3].x < -q[3].z) continue;

            if (on_pad(w, (int32_t)(gx0 + i) << TILE_BITS | 1, (int32_t)(gz0 + j) << TILE_BITS | 1)) {
                pen = chk ? PEN_GREY : PEN_YELLOW;
            } else if (ha <= SEA_LEVEL && hb <= SEA_LEVEL && hc <= SEA_LEVEL && hd <= SEA_LEVEL) {
                pen = PEN_TERRAIN + chk;
            } else {
                int32_t avg = (ha + hb + hc + hd) >> 2, s = hb - hd;
                int band = terrain_type(avg <= SEA_LEVEL ? SEA_LEVEL + 1 : avg);
                int shade = s > 30 ? 1 : s < -30 ? 0 : chk;
                pen = PEN_TERRAIN + band * 2 + shade;
            }
            add_poly(depth, pen, q, 4);
            render_stats.tiles++;
        }
}

/* Depth key for something standing over world (x, z): its tile's depth,
 * nudged nearer, or its own depth when outside the terrain grid. */
static int32_t key_for(int32_t x, int32_t z, int32_t own)
{
    int i = ((int)(ts_wrap(x) >> TILE_BITS) - gx0) & (MAP_N - 1);
    int j = ((int)(ts_wrap(z) >> TILE_BITS) - gz0) & (MAP_N - 1);
    if (i < GN - 1 && j < GN - 1) return tdepth[j * (GN - 1) + i] - 6;
    return own;
}

/* ---------- models ---------- */
static void add_model(const Cam *c, int mdl, int32_t wx, int32_t wy, int32_t wz,
                      int yaw, int tilt, int bank, int flat_y, int32_t key_adj)
{
    const Model *m = &models[mdl];
    V3 cv[MDL_MAXV], ctr;
    int32_t co = TS_COS(yaw) >> 4, si = TS_SIN(yaw) >> 4, key;
    int i, k;

    cam_xform(c, wx, wy, wz, &ctr);
    if (ctr.z < -200 || ctr.z > FAR_Z) return;
    if (ctr.x > ctr.z + 250 || ctr.x < -ctr.z - 250) return;
    key = key_for(wx, wz, ctr.z) - key_adj;

    for (i = 0; i < m->nv; i++) {
        int32_t x = m->v[i][0], y = m->v[i][1], z = m->v[i][2];
        int32_t rx = (x * co + z * si) >> 12;
        int32_t rz = (z * co - x * si) >> 12;
        if (flat_y) y = 0;
        else {
            y += (z * tilt) >> 6;           /* nose up when climbing */
            y += (x * bank) >> 3;           /* dip a wing in turns */
        }
        cam_xform(c, wx + rx, wy + y, wz + rz, &cv[i]);
    }
    for (k = 0; k < m->nf; k++) {
        const Face *f = &m->f[k];
        V3 q[4];
        int32_t ux, uy, uz, vx, vy, vz, nx, ny, nz;
        q[0] = cv[f->idx[0]]; q[1] = cv[f->idx[1]]; q[2] = cv[f->idx[2]];
        if (f->n == 4) q[3] = cv[f->idx[3]];
        /* back-face cull in camera space (the camera is at the origin) */
        ux = q[1].x - q[0].x; uy = q[1].y - q[0].y; uz = q[1].z - q[0].z;
        vx = q[2].x - q[0].x; vy = q[2].y - q[0].y; vz = q[2].z - q[0].z;
        nx = uy * vz - uz * vy; ny = uz * vx - ux * vz; nz = ux * vy - uy * vx;
        if (nx * q[0].x + ny * q[0].y + nz * q[0].z >= 0) continue;
        add_poly(key, f->col, q, f->n);
    }
    render_stats.models++;
}

static void add_point(const Cam *c, int32_t x, int32_t y, int32_t z, int col, int big)
{
    V3 v;
    int32_t sx, sy;
    int size;
    cam_xform(c, x, y, z, &v);
    if (v.z < NEAR_Z || v.z > FAR_Z) return;
    cam_project(&v, &sx, &sy);
    size = (int)((big * 300) / v.z);
    if (size < 1) size = 1;
    if (size > 5) size = 5;
    add_rect(key_for(x, z, v.z) - 8, col, sx - size / 2, sy - size / 2, size);
}

static int ent_model(const Ent *e, int idx)
{
    switch (e->type) {
    case ENT_TANK: return MDL_TANK;
    case ENT_BUILDING: return (idx & 1) ? MDL_TOWER : MDL_BUILDING;
    case ENT_SHIP: return MDL_SHIP;
    case ENT_TREE: return MDL_TREE;
    }
    return -1;
}

static int16_t span_l[SPAN_MAX], span_r[SPAN_MAX];

static void flush(Canvas *cv)
{
    int b, i, y;

    for (b = NBUCKET - 1; b >= 0; b--) {
        DPoly *p;
        for (p = bhead[b]; p; p = p->next) {
            if (p->n == 0) {
                for (y = p->y[0]; y <= p->y[1]; y++)
                    g_span(cv, y, p->x[0], p->x[1], p->col);
            } else {
                int ymin, rows = poly_spans(p->x, p->y, p->n, span_l, span_r, &ymin);
                for (i = 0; i < rows; i++) {
                    int xl = span_l[i], xr = span_r[i];
                    y = ymin + i;
                    if (xl > xr || y < 0 || y >= VIEW_H) continue;
                    if (xl < 0) xl = 0;
                    if (xr > VIEW_W - 1) xr = VIEW_W - 1;
                    g_span(cv, y, xl, xr, p->col);
                }
            }
        }
    }
}

void render_build(const World *w, Cam *c)
{
    int i;

    npoly = 0;
    memset(bhead, 0, sizeof(bhead));
    memset(btail, 0, sizeof(btail));
    render_stats.tiles = render_stats.models = 0;

    draw_terrain(w, c);

    for (i = 0; i < MAX_ENTS; i++) {
        const Ent *e = &w->ents[i];
        int m = ent_model(e, i);
        if (m < 0) continue;
        add_model(c, m, e->x >> FBITS, e->y >> FBITS, e->z >> FBITS, e->yaw, 0, 0, 0, 0);
    }

    if (w->state != GS_TITLE && w->p.state != PS_DEAD) {
        const Craft *p = &w->p;
        int32_t px = p->x >> FBITS, py = p->y >> FBITS, pz = p->z >> FBITS;
        int tilt = (int)(p->vy / 8);
        if (tilt > 6) tilt = 6;
        if (tilt < -6) tilt = -6;
        add_model(c, MDL_SHADOW, px, ground_height(w, px, pz) + 2, pz, p->yaw, 0, 0, 1, 0);
        add_model(c, MDL_CRAFT, px, py, pz, p->yaw, tilt, p->bank, 0, 4);
    }

    for (i = 0; i < MAX_SHOTS; i++)
        if (w->shots[i].active)
            add_point(c, w->shots[i].x >> FBITS, w->shots[i].y >> FBITS, w->shots[i].z >> FBITS,
                      PEN_YELLOW, 8);
    for (i = 0; i < MAX_SHELLS; i++)
        if (w->shells[i].active)
            add_point(c, w->shells[i].x >> FBITS, w->shells[i].y >> FBITS, w->shells[i].z >> FBITS,
                      PEN_RED, 10);
    for (i = 0; i < MAX_PARTS; i++)
        if (w->parts[i].life > 0)
            add_point(c, w->parts[i].x >> FBITS, w->parts[i].y >> FBITS, w->parts[i].z >> FBITS,
                      w->parts[i].col, 10);

    render_stats.polys = npoly;
}

#ifdef AMIGA3DO
/* the 3DO: the cel engine fills the polygons (cels_3do.c), in a layer
 * under the frame; the frame's view area is cleared to the shadow pen,
 * which main_3do.c makes transparent (it is only ever drawn in 3D), so
 * the HUD and the text drawn over the view stay on top */
#include "cels_3do.h"
void render_draw(Canvas *cv)
{
    int b;
    g_rect(cv, 0, 0, VIEW_W - 1, VIEW_H - 1, PEN_SHADOW);
    ts3_begin(PEN_BLACK, VIEW_W, VIEW_H);
    for (b = NBUCKET - 1; b >= 0; b--) {
        DPoly *p;
        for (p = bhead[b]; p; p = p->next) {
            if (p->n == 0) ts3_rect(p->x[0], p->y[0], p->x[1], p->y[1], p->col);
            else ts3_poly(p->x, p->y, p->n, p->col);
        }
    }
    ts3_end();
    (void)flush;
}
#else
void render_draw(Canvas *cv)
{
    g_rect(cv, 0, 0, VIEW_W - 1, VIEW_H - 1, PEN_BLACK);
    g_begin(cv);
    flush(cv);
}
#endif

/* ---------- HUD ---------- */
static void text_at(Canvas *rp, int x, int y, int col, const char *s)
{
    g_text(rp, x, y, col, s);
}

static void text_centre(Canvas *rp, int y, int col, const char *s)
{
    int x = (VIEW_W - (int)strlen(s) * 8) / 2;
    text_at(rp, x + 1, y + 1, PEN_BLACK, s);
    text_at(rp, x, y, col, s);
}

static void bar(Canvas *rp, int x, int y, int w, int val, int max, int col)
{
    int fill = (val * w) / max;
    if (fill < 0) fill = 0;
    if (fill > w) fill = w;
    g_rect(rp, x, y, x + w, y + 4, PEN_BLACK);
    if (fill > 0) g_rect(rp, x, y, x + fill, y + 4, col);
}

static void radar(Canvas *rp, const World *w)
{
    const int x0 = 250, y0 = VIEW_H + 3, x1 = 316, y1 = 253;
    const int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
    int32_t px = w->p.x >> FBITS, pz = w->p.z >> FBITS;
    int32_t padx, padz;
    int i, rx, ry;

    g_rect(rp, x0, y0, x1, y1, PEN_BLACK);
    g_rect(rp, x0, y0, x1, y0, PEN_GREY);           /* the frame */
    g_rect(rp, x0, y1, x1, y1, PEN_GREY);
    g_rect(rp, x0, y0, x0, y1, PEN_GREY);
    g_rect(rp, x1, y0, x1, y1, PEN_GREY);

    pad_centre(w, &padx, &padz);
    radar_map(ts_wrap_delta(padx, px), ts_wrap_delta(padz, pz), w->p.yaw, 6, &rx, &ry);
    if (cx + rx > x0 + 1 && cx + rx < x1 - 2 && cy + ry > y0 + 1 && cy + ry < y1 - 2) {
        g_rect(rp, cx + rx - 1, cy + ry - 1, cx + rx + 1, cy + ry + 1, PEN_YELLOW);
    }
    for (i = 0; i < MAX_ENTS; i++) {
        const Ent *e = &w->ents[i];
        int col;
        if (e->type == ENT_NONE || e->type == ENT_TREE) continue;
        radar_map(ts_wrap_delta(e->x >> FBITS, px), ts_wrap_delta(e->z >> FBITS, pz),
                  w->p.yaw, 6, &rx, &ry);
        if (cx + rx <= x0 || cx + rx >= x1 - 1 || cy + ry <= y0 || cy + ry >= y1 - 1) continue;
        col = e->type == ENT_TANK ? PEN_RED : e->type == ENT_SHIP ? PEN_CRAFT : PEN_WHITE;
        g_rect(rp, cx + rx, cy + ry, cx + rx + 1, cy + ry + 1, col);
    }
    g_rect(rp, cx, cy - 2, cx, cy + 1, PEN_WHITE);
}

/* HUD text is cached per screen buffer: a line is redrawn only when its
 * text changed since that buffer last showed it (Text() is the slow part). */
static char hud_cache[2][6][24];
static int hud_valid[2];

static void hud_line(Canvas *rp, int buf, int slot, int x, int y, int w,
                     int col, const char *s)
{
    if (hud_valid[buf] && strcmp(hud_cache[buf][slot], s) == 0) return;
    strncpy(hud_cache[buf][slot], s, 23);
    g_rect(rp, x, y - 7, x + w, y + 1, PEN_PANEL);
    text_at(rp, x, y, col, s);
}

void render_hud_reset(void) { hud_valid[0] = hud_valid[1] = 0; }

void render_hud(Canvas *rp, int buf, const World *w, int fps, int paused)
{
    char tmp[48];
    const Craft *p = &w->p;
    int32_t alt = (p->y >> FBITS) - ground_height(w, p->x >> FBITS, p->z >> FBITS);

    if (!hud_valid[buf]) {
        g_rect(rp, 0, VIEW_H, 319, 255, PEN_PANEL);
        g_rect(rp, 0, VIEW_H, 319, VIEW_H, PEN_GREY);
        text_at(rp, 134, VIEW_H + 12, PEN_GREY, "FUEL");
        text_at(rp, 134, VIEW_H + 24, PEN_GREY, "SHLD");
    }

    sprintf(tmp, "SCORE %06ld", (long)w->score);
    hud_line(rp, buf, 0, 6, VIEW_H + 12, 120, PEN_WHITE, tmp);
    sprintf(tmp, "LIVES %ld  LV %ld", (long)w->lives, (long)w->level);
    hud_line(rp, buf, 1, 6, VIEW_H + 24, 120, PEN_GREY, tmp);
    sprintf(tmp, "TARGETS %ld", (long)w->targets_left);
    hud_line(rp, buf, 2, 6, VIEW_H + 36, 120, PEN_RED, tmp);
    sprintf(tmp, "ALT%4ld SPD%3ld", (long)(alt < 0 ? 0 : alt), (long)(p->speed >> FBITS));
    hud_line(rp, buf, 3, 134, VIEW_H + 36, 112, PEN_GREY, tmp);
    hud_valid[buf] = 1;

    bar(rp, 172, VIEW_H + 6, 68, p->fuel, 1000, p->fuel < 200 ? PEN_RED : PEN_YELLOW);
    bar(rp, 172, VIEW_H + 18, 68, p->shield, 100, PEN_CRAFT);
    radar(rp, w);

    if (fps > 0) {
        sprintf(tmp, "%ld", (long)fps);
        text_at(rp, 302, 9, PEN_GREY, tmp);
    }

    switch (w->state) {
    case GS_PLAY:
        if (p->state == PS_LANDED)
            text_centre(rp, 30, PEN_YELLOW, "LANDED - UP TO LIFT OFF");
        else if (p->state == PS_DEAD && w->lives > 0)
            text_centre(rp, 60, PEN_RED, "CRAFT LOST");
        else if (p->fuel < 150 && !w->tune.inf_fuel && (w->frame & 16))
            text_centre(rp, 30, PEN_RED, "LOW FUEL - RETURN TO PAD");
        break;
    case GS_LEVELCLEAR:
        sprintf(tmp, "LEVEL %ld CLEARED", (long)w->level);
        text_centre(rp, 60, PEN_YELLOW, tmp);
        sprintf(tmp, "BONUS %ld", (long)(500 * w->level));
        text_centre(rp, 76, PEN_WHITE, tmp);
        break;
    case GS_GAMEOVER:
        text_centre(rp, 70, PEN_RED, "GAME OVER");
#ifdef AMIGA3DO
        if (w->state_timer == 0) text_centre(rp, 90, PEN_WHITE, "PRESS A");
#else
        if (w->state_timer == 0) text_centre(rp, 90, PEN_WHITE, "PRESS FIRE");
#endif
        break;
    }
    if (paused) text_centre(rp, 100, PEN_WHITE, "PAUSED");
}

void render_title(Canvas *rp, const World *w)
{
    text_centre(rp, 40, PEN_YELLOW, "T E R R A   S T R I K E");
    text_centre(rp, 56, PEN_GREY, "an Amiga DevBench demo");
#ifdef AMIGA3DO
    text_centre(rp, 150, PEN_WHITE, "D-PAD  FLY      B / C  THROTTLE");
#else
    text_centre(rp, 150, PEN_WHITE, "ARROWS  FLY      A / Z  THROTTLE");
#endif
#ifdef AMIGA3DO
    text_centre(rp, 162, PEN_WHITE, "A  FIRE      P  START / PAUSE");
#else
    text_centre(rp, 162, PEN_WHITE, "SPACE  FIRE      P  PAUSE");
#endif
#ifdef AMIGA3DO
    if (w->frame & 32) text_centre(rp, 186, PEN_YELLOW, "PRESS A TO START");
#else
    if (w->frame & 32) text_centre(rp, 186, PEN_YELLOW, "PRESS FIRE TO START");
#endif
}

/* Main screen: the converted ChatGPT artwork (tools/make_title.py), with
 * the prompt text drawn over the dark sea at the bottom in the image's own
 * palette. */
void render_title_image(Canvas *rp, int frame)
{
    g_title(rp);
    {
#ifdef AMIGA3DO
        const char *keys = "D-PAD FLY  B/C SPEED  A FIRE";
#else
        const char *keys = "ARROWS FLY  A/Z SPEED  SPACE FIRE";
#endif
        int x = (320 - (int)strlen(keys) * 8) / 2;
        text_at(rp, x + 1, 227, TITLE_PEN_SHADOW, keys);
        text_at(rp, x, 226, TITLE_PEN_WHITE, keys);
    }
    {
        /* Alternate yellow/white rather than blink on/off: the prompt is in
         * every frame, so devbench's screenshot (which reads the screen row
         * by row over ~5 s) and the tests always see it. */
#ifdef AMIGA3DO
        const char *go = "PRESS A TO START";
#else
        const char *go = "PRESS FIRE TO START";
#endif
        int x = (320 - (int)strlen(go) * 8) / 2;
        text_at(rp, x + 1, 245, TITLE_PEN_SHADOW, go);
        text_at(rp, x, 244, (frame & 16) ? TITLE_PEN_TEXT : TITLE_PEN_WHITE, go);
    }
}
