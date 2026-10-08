// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Split-screen pitch renderer.
 *
 * Each call renders the full pane (sky + horizon + checkered ground
 * grid + goal beams + ball) from ONE camera viewpoint. Coordinates
 * are all 16.16 fixed-point, but screen math falls back to integer
 * pixels once we divide by depth.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/graphics.h>

#include "ballblazer.h"
#include <string.h>

/* ---- integer sin / cos --------------------------------------------- */
static LONG sintab[360];

static LONG isin_deg(LONG d)
{
    LONG sign = 1, t;
    d %= 360; if (d < 0) d += 360;
    if (d >= 180) { d -= 180; sign = -1; }
    t = d * (180 - d);
    return sign * ((4 * t * ONE) / (40500 - t));
}

void math_init(void)
{
    LONG i;
    for (i = 0; i < 360; i++) sintab[i] = isin_deg(i);
}

LONG math_sin(LONG deg) { deg %= 360; if (deg < 0) deg += 360; return sintab[deg]; }
LONG math_cos(LONG deg) { return math_sin(deg + 90); }

/* ---- projection ---------------------------------------------------- */
#define FOCAL       180L        /* pixels, ~85° horizontal FOV */
/* Near-clip below 1 world unit so the carried ball (2 units in front)
 * always projects — a NEAR_CLIP of 2*ONE was rejecting the carried
 * ball exactly at the clip plane, hiding it from the carrier's view. */
#define NEAR_CLIP   (1L * ONE / 2)

/* Project a world point (wx, wy=optional, wz) into pane coords.
 * Returns 0 if behind near plane; else fills *sx, *sy. */
static int project(LONG wx, LONG wy_offset,   /* wy_offset = wy - HOVER_H */
                   LONG wz,
                   LONG cx, LONG cz,
                   LONG cos_a, LONG sin_a,
                   int pane_y0,
                   WORD *sx, WORD *sy)
{
    LONG dx = wx - cx;
    LONG dz = wz - cz;
    /* z_cam (forward) = dx*cos + dz*sin  — dot with camera forward */
    LONG zc = ((dx >> 8) * (cos_a >> 8) + (dz >> 8) * (sin_a >> 8));
    LONG xc;
    LONG pane_cx;
    LONG rx;
    LONG ry;
    LONG sxp;
    LONG syp;
    if (zc < NEAR_CLIP) return 0;
    /* x_cam (right)   = dx*sin - dz*cos */
    xc = ((dx >> 8) * (sin_a >> 8) - (dz >> 8) * (cos_a >> 8));

    pane_cx = SCR_W / 2;
    /* Screen X: pane_cx + F * xc / zc — shift xc back into full FP for /zc */
    rx = (xc * FOCAL) / (zc >> FP); /* xc still FP; result in pixels */
    ry = (wy_offset * FOCAL) / (zc >> FP);
    sxp = pane_cx + (rx >> FP);
    syp = pane_y0 + HORIZON_Y - (ry >> FP);

    /* Safety clamp to the pane rect; caller can further clip if needed. */
    if (sxp < 0)              sxp = 0;
    if (sxp > SCR_W - 1)      sxp = SCR_W - 1;
    if (syp < pane_y0)              syp = pane_y0;
    if (syp > pane_y0 + PANE_H - 1) syp = pane_y0 + PANE_H - 1;

    *sx = (WORD)sxp; *sy = (WORD)syp;
    return 1;
}

/* Line-draw a world-space segment. If either endpoint is behind the
 * near plane, walk toward the visible end in small steps until it
 * becomes projectable. Crude but avoids per-segment clip algebra. */
static void world_line(struct RastPort *rp,
                       LONG x0, LONG z0, LONG x1, LONG z1,
                       LONG cx, LONG cz, LONG ca, LONG sa,
                       int pane_y0)
{
    WORD sx0, sy0, sx1, sy1;
    int a_ok = project(x0, -HOVER_H, z0, cx, cz, ca, sa, pane_y0, &sx0, &sy0);
    int b_ok = project(x1, -HOVER_H, z1, cx, cz, ca, sa, pane_y0, &sx1, &sy1);
    if (!a_ok && !b_ok) return;

    /* If one end is offscreen-behind, pull it toward the visible end
     * along the segment until project() succeeds. */
    if (!a_ok || !b_ok) {
        LONG *fx = a_ok ? &x1 : &x0;
        LONG *fz = a_ok ? &z1 : &z0;
        LONG  gx = a_ok ? x0  : x1;
        LONG  gz = a_ok ? z0  : z1;
        int tries = 8;
        while (tries--) {
            WORD tx, ty;
            *fx = (*fx + gx) / 2;
            *fz = (*fz + gz) / 2;
            if (project(*fx, -HOVER_H, *fz, cx, cz, ca, sa, pane_y0, &tx, &ty)) {
                if (!a_ok) { sx0 = tx; sy0 = ty; }
                else       { sx1 = tx; sy1 = ty; }
                break;
            }
        }
    }
    Move(rp, sx0, sy0);
    Draw(rp, sx1, sy1);
}

/* Fill an axis-aligned rectangle band with a solid pen. */
static void band(struct RastPort *rp, int y0, int y1, UBYTE pen)
{
    SetAPen(rp, pen);
    RectFill(rp, 0, y0, SCR_W - 1, y1);
}

/* ---- chequered floor ---------------------------------------------- */
/*
 * Direct-to-bitplane per-row checker fill. The prior implementation
 * used graphics.library Move+Draw per cell band (10-30 calls per
 * scanline × 85 rows × 2 panes = ~4000 calls/frame) which pays the
 * gfx.library setup cost on every call. That single change dropped
 * the frame rate to single digits.
 *
 * Here we compute one 40-byte mask per row (bit = 1 means checker
 * cell A) and blast it into 5 bitplanes directly. Pen A=3=0b00011
 * so planes 0/1 = mask, plane 2 = ~mask; pen B=4=0b00100 fills the
 * inverse cells. Planes 3/4 are already zeroed for these two pens.
 *
 * Cost per frame: ~85 rows × 5 plane byte-writes × 40 bytes × 2
 * panes = ~34 KB memory writes. Trivial on 68020.
 *
 * Camera restriction: only the two Ballblazer angles (0 / 180) so
 * we can hard-code the fwd_sign instead of running a full 2D rotate
 * per pixel.
 */
#ifdef AMIGA3DO
/* 3DO port: the layer has no bitplanes, and the original's two software
 * divisions per pixel are far too slow on the ARM60. Same picture: each
 * row is a run of cells (A = pen 3, B = pen 4, what the plane bits
 * m/m/~m/0/0 make). Per row (dy) we tabulate q(a) = floor(a*zdist/FOCAL)
 * for a = |px - 160| and its inverse, then walk cell boundaries and fill
 * spans. worldz = cam_z_i - fwd_sign * sign(dx) * q, cells are floor(/5). */
#include "amiga3do.h"
#define FLOOR_ROWS (PANE_H - HORIZON_Y)
#define HALF_W     (SCR_W / 2)
/* int, not WORD: ARMv3 has no halfword loads */
static int q_tab[FLOOR_ROWS][HALF_W + 1];      /* q at |dx| = a */
static int inv_tab[FLOOR_ROWS][2 * HALF_W + 4]; /* first a with q >= v */
static int q_max[FLOOR_ROWS];
static LONG zdist_tab[FLOOR_ROWS];
static int floor_ready = 0;

static void floor_tables(void)
{
    LONG hover_i = HOVER_H >> FP;
    int dy, a, v;
    for (dy = 1; dy < FLOOR_ROWS; dy++) {
        LONG zdist = (hover_i * FOCAL) / dy;
        if (zdist < 1) zdist = 1;
        zdist_tab[dy] = zdist;
        for (a = 0; a <= HALF_W; a++)
            q_tab[dy][a] = (int)(((LONG)a * zdist) / FOCAL);
        q_max[dy] = q_tab[dy][HALF_W];
        a = 0;
        for (v = 0; v <= q_max[dy] + 1 && v < 2 * HALF_W + 4; v++) {
            while (a <= HALF_W && q_tab[dy][a] < v) a++;
            inv_tab[dy][v] = a;                 /* HALF_W + 1 = never */
        }
    }
    floor_ready = 1;
}

static LONG floor_div5(LONG w, LONG g)
{
    /* g is GRID 5: x / 5 == (x * 0xCCCD) >> 18 exactly for 0 <= x < 81920 */
    if (g == 5 && w > -81915 && w < 81920) {
        if (w >= 0) return (LONG)(((ULONG)w * 0xCCCDUL) >> 18);
        return -(LONG)(((ULONG)(-w + 4) * 0xCCCDUL) >> 18);
    }
    return (w >= 0) ? (w / g) : -((-w + g - 1) / g);
}

/* fill one half row: a runs from a0 (inclusive) to a1 (exclusive), pixel
 * px = base + dir * a; worldz = C + t * q(a) (t = +1 or -1) */
static void floor_half(UBYTE *row, int dy, LONG C, int t, LONG cellx, int a0, int a1, int base, int dir)
{
    LONG g = GRID_STEP >> FP;
    int a = a0;
    LONG q = q_tab[dy][a0];
    LONG w = C + t * q;
    LONG cellz = floor_div5(w, g);
    while (a < a1) {
        LONG q_next, a_next;
        UBYTE pen = (((cellz + cellx) & 1) == 0) ? 3 : 4;
        /* the q at which worldz leaves cell cellz */
        if (t > 0) q_next = g * (cellz + 1) - C;   /* w increasing */
        else       q_next = C - g * cellz + 1;     /* w decreasing */
        if (q_next > q_max[dy]) a_next = a1;
        else {
            a_next = inv_tab[dy][q_next];
            if (a_next > a1) a_next = a1;
        }
        {
            UBYTE *d = (dir > 0) ? row + base + a : row + base - (a_next - 1);
            LONG n = a_next - a;
            if (n >= 16) memset(d, pen, n);
            else while (n-- > 0) *d++ = pen;
        }
        a = (int)a_next;
        /* q grows by at most 2 per pixel (zdist/FOCAL <= 2) and a cell is
         * 5 wide, so crossing a boundary always lands in the next cell */
        cellz += t;
    }
}

#include "floor_cels.h"

/* x of the cell edge at world z = z16 on the line dy16 below the horizon
 * centre (16.16 px): 160 + fwd * (cam_z - z) * dy / hover */
#define HOVER_I (HOVER_H >> FP)            /* 2: divisions by it are shifts */
static LONG floor_edge_x(LONG camz16, LONG z16, LONG dy16, int fwd)
{
    LONG off = (((camz16 - z16) >> 8) * (dy16 >> 10) * 4) / HOVER_I;
    return ((LONG)HALF_W << 16) + (fwd > 0 ? off : -off);
}

/* The floor as cels (floor_cels.c): rows with the same cellx form a band,
 * and each band is one cel whose texels are the cells along z. The frame
 * itself is left in the transparent pen there (see main.c). */
static void draw_checker_floor(struct RastPort *rp, int pane_y0,
                               LONG cam_x, LONG cam_z, LONG cam_angle)
{
    int fwd = (cam_angle == 0) ? +1 : -1;
    LONG grid_i  = GRID_STEP >> FP;
    LONG cam_x_i = cam_x    >> FP;
    int hy = pane_y0 + HORIZON_Y;            /* horizon row */
    int dy, dy_a;
    LONG cell_a;
    if (!floor_ready) floor_tables();
    SetAPen(rp, PEN_FLOOR_HW);
    RectFill(rp, 0, hy + 1, SCR_W - 1, pane_y0 + PANE_H - 1);
    dy_a = 1;
    cell_a = floor_div5(cam_x_i + fwd * zdist_tab[1], grid_i);
    for (dy = 2; dy <= FLOOR_ROWS; dy++) {
        LONG cell = (dy < FLOOR_ROWS) ? floor_div5(cam_x_i + fwd * zdist_tab[dy], grid_i) : cell_a + 1;
        if (cell != cell_a) {
            /* band: rows dy_a .. dy-1; edges at dy_a - 1/2 and dy - 1/2 */
            LONG dt = ((LONG)dy_a << 16) - 0x8000, db = ((LONG)dy << 16) - 0x8000;
            /* visible half width at the band's far edge, world units 16.16 */
            LONG half_w = ((160L * HOVER_I) << 20) / (dt >> 12);
            LONG zmin = cam_z - half_w - (grid_i << 16);
            LONG m0 = floor_div5(zmin >> 16, grid_i);
            int texels = (int)((((ULONG)(2 * half_w >> 16)) * 0xCCCDUL) >> 18) + 4;   /* / 5 */
            LONG z0, xt, xb, ht, hb, yt, yb;
            if ((m0 + cell_a) & 1) { m0--; texels++; }
            z0 = m0 * (grid_i << 16);
            xt = floor_edge_x(cam_z, z0, dt, fwd);
            xb = floor_edge_x(cam_z, z0, db, fwd);
            ht = (-fwd * grid_i * dt / HOVER_I) << 4;
            hb = (-fwd * grid_i * db / HOVER_I) << 4;
            yt = gfx_display_y((LONG)(hy + dy_a) << 16);
            yb = gfx_display_y((LONG)(hy + dy) << 16);
            fc_band(xt, yt, ht, xb - xt, yb - yt, hb - ht, texels);
            dy_a = dy;
            cell_a = cell;
        }
    }
}

static void draw_checker_floor_sw(struct RastPort *rp, int pane_y0,
                               LONG cam_x, LONG cam_z, LONG cam_angle)
{
    UBYTE *pix = gfx_pixels8();
    int fwd_sign = (cam_angle == 0) ? +1 : -1;
    LONG grid_i  = GRID_STEP >> FP;
    LONG hover_i = HOVER_H  >> FP;
    LONG cam_x_i = cam_x    >> FP;
    LONG cam_z_i = cam_z    >> FP;
    int y;
    (void)rp;
    if (!pix) return;
    if (!floor_ready) floor_tables();
    for (y = pane_y0 + HORIZON_Y + 1; y < pane_y0 + PANE_H; y++) {
        int dy = y - (pane_y0 + HORIZON_Y);
        LONG zdist = zdist_tab[dy];     /* (hover_i * FOCAL) / dy, >= 1 */
        LONG worldx, cellx;
        UBYTE *row = pix + (LONG)y * GFX_WIDTH;
        worldx = cam_x_i + fwd_sign * zdist;
        cellx  = floor_div5(worldx, grid_i);
        /* dx >= 0 (px 160..319): worldz = cam_z - fwd_sign * q */
        floor_half(row, dy, cam_z_i, -fwd_sign, cellx, 0, HALF_W, HALF_W, +1);
        /* dx < 0 (px 159..0, a = 1..160): worldz = cam_z + fwd_sign * q */
        floor_half(row, dy, cam_z_i, fwd_sign, cellx, 1, HALF_W + 1, HALF_W, -1);
    }
}
#else
static void draw_checker_floor(struct RastPort *rp, int pane_y0,
                               LONG cam_x, LONG cam_z, LONG cam_angle)
{
    struct BitMap *bm = rp->BitMap;
    LONG bpr;
    UBYTE *plane0;
    UBYTE *plane1;
    UBYTE *plane2;
    UBYTE *plane3;
    UBYTE *plane4;
    int fwd_sign;
    LONG grid_i;
    LONG hover_i;
    LONG cam_x_i;
    LONG cam_z_i;
    int y;
    if (!bm) return;
    bpr = bm->BytesPerRow;
    plane0 = (UBYTE *)bm->Planes[0];
    plane1 = (UBYTE *)bm->Planes[1];
    plane2 = (UBYTE *)bm->Planes[2];
    plane3 = (SCR_DEPTH > 3) ? (UBYTE *)bm->Planes[3] : NULL;
    plane4 = (SCR_DEPTH > 4) ? (UBYTE *)bm->Planes[4] : NULL;

    fwd_sign = (cam_angle == 0) ? +1 : -1;
    grid_i = GRID_STEP >> FP; /* 5 */
    hover_i = HOVER_H  >> FP; /* 2 */
    cam_x_i = cam_x    >> FP;
    cam_z_i = cam_z    >> FP;

    /* Row-scratch mask: 40 bytes = 320 pixels. */
    UBYTE mask[40];

    for (y = pane_y0 + HORIZON_Y + 1; y < pane_y0 + PANE_H; y++) {
        int dy = y - (pane_y0 + HORIZON_Y);
        LONG zdist = (hover_i * FOCAL) / dy;
        LONG worldx;
        int row_parity;
        int px;
        UBYTE cur;
        UBYTE *r0;
        UBYTE *r1;
        UBYTE *r2;
        UBYTE *r3;
        UBYTE *r4;
        int b;
        if (zdist < 1) zdist = 1;
        worldx = cam_x_i + fwd_sign * zdist;
        LONG cellx  = (worldx >= 0) ? (worldx / grid_i)
                                    : -((-worldx + grid_i - 1) / grid_i);
        row_parity = ((int)(cellx & 1));

        /* Build mask: bit set = pixel is in an A cell.
         * worldz(px) = cam_z_i - fwd_sign * (px - pane_cx) * zdist / FOCAL */
        cur = 0;
        for (px = 0; px < SCR_W; px++) {
            LONG dx = px - SCR_W / 2;
            LONG worldz_num = (LONG)dx * zdist;
            LONG worldz = cam_z_i - fwd_sign * (worldz_num / FOCAL);
            int is_a;
            int bit;
            LONG cellz = (worldz >= 0) ? (worldz / grid_i)
                                       : -((-worldz + grid_i - 1) / grid_i);
            is_a = (((int)((cellz + cellx) & 1)) == 0); /* even = A */
            (void)row_parity;
            /* pack MSB-first across the byte */
            bit = 7 - (px & 7);
            if (bit == 7) cur = 0;
            if (is_a) cur |= (1 << bit);
            if (bit == 0) mask[px >> 3] = cur;
        }

        /* Blast to 5 bitplanes at this Y. */
        r0 = plane0 + y * bpr;
        r1 = plane1 + y * bpr;
        r2 = plane2 + y * bpr;
        r3 = plane3 ? plane3 + y * bpr : NULL;
        r4 = plane4 ? plane4 + y * bpr : NULL;
        for (b = 0; b < 40; b++) {
            UBYTE m = mask[b];
            r0[b] = m;
            r1[b] = m;
            r2[b] = (UBYTE)~m;
            if (r3) r3[b] = 0;
            if (r4) r4[b] = 0;
        }
    }
}

#endif

/* Horizontal span. 3DO port: a one-row RectFill skips Draw's line set-up
 * and clipping (hundreds of spans a frame for the ball and rotofoils). */
#ifdef AMIGA3DO
#define HLINE(rp, x0, x1, y) RectFill(rp, x0, y, x1, y)
#else
#define HLINE(rp, x0, x1, y) (Move(rp, x0, y), Draw(rp, x1, y))
#endif

/* ---- shaded ball --------------------------------------------------- */
static void draw_ball_sphere(struct RastPort *rp, int pane_y0,
                             int cx, int cy, int r)
{
    int hi_dx, hi_dy;
    int r2;
    int mr;
    int mr2;
    int hr;
    int hr2;
    int dy;
#ifdef AMIGA3DO
    int w_out_prev = 0, w_mid_prev = 0, w_hi_prev = 0;
#endif
    if (r < 1) r = 1;
    /* Three-tone shading: dark outer, mid, bright inner-offset. */
    hi_dx = -r / 3; hi_dy = -r / 3; /* highlight upper-left */
    r2 = r * r;
    mr = (r * 5) / 6; /* mid radius */
    mr2 = mr * mr;
    hr = r / 3; /* highlight radius */
    hr2 = hr * hr;
    for (dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        int hw_out;
        int hw_mid;
        int w_out;
        int w_mid;
        int xL, xR;
        int hdy;
        int hw_hi;
        if (y < pane_y0 || y >= pane_y0 + PANE_H) continue;
        hw_out = r2 - dy * dy; if (hw_out <= 0) continue;
        hw_mid = mr2 - dy * dy;
#ifdef AMIGA3DO
        /* floor(sqrt()) stepped from the previous row's value (the
         * widths change by little per row) instead of counting from 0 */
        w_out = w_out_prev;
        while ((w_out+1)*(w_out+1) <= hw_out) w_out++;
        while (w_out > 0 && w_out*w_out > hw_out) w_out--;
        w_out_prev = w_out;
        w_mid = 0;
        if (hw_mid > 0) {
            w_mid = w_mid_prev;
            while ((w_mid+1)*(w_mid+1) <= hw_mid) w_mid++;
            while (w_mid > 0 && w_mid*w_mid > hw_mid) w_mid--;
            w_mid_prev = w_mid;
        }
#else
        w_out = 0; while ((w_out+1)*(w_out+1) <= hw_out) w_out++;
        w_mid = 0; if (hw_mid > 0) while ((w_mid+1)*(w_mid+1) <= hw_mid) w_mid++;
#endif
        /* Outer ring: PEN_BALL_DARK */
        SetAPen(rp, PEN_BALL_DARK);
        xL = cx - w_out; xR = cx + w_out;
        if (xL < 0)         xL = 0;
        if (xR > SCR_W - 1) xR = SCR_W - 1;
        HLINE(rp, xL, xR, y);
        /* Mid: PEN_BALL */
        if (w_mid > 0) {
            int mL, mR;
            SetAPen(rp, PEN_BALL);
            mL = cx - w_mid; mR = cx + w_mid;
            if (mL < 0) mL = 0; if (mR > SCR_W - 1) mR = SCR_W - 1;
            HLINE(rp, mL, mR, y);
        }
        /* Highlight: PEN_BALL_HI, offset up-left */
        hdy = dy - hi_dy;
        hw_hi = hr2 - hdy * hdy;
        if (hw_hi > 0) {
            int w_hi = 0, hL, hR;
#ifdef AMIGA3DO
            w_hi = w_hi_prev;
            while (w_hi > 0 && w_hi*w_hi > hw_hi) w_hi--;
#endif
            while ((w_hi+1)*(w_hi+1) <= hw_hi) w_hi++;
#ifdef AMIGA3DO
            w_hi_prev = w_hi;
#endif
            SetAPen(rp, PEN_BALL_HI);
            hL = cx + hi_dx - w_hi; hR = cx + hi_dx + w_hi;
            if (hL < 0) hL = 0; if (hR > SCR_W - 1) hR = SCR_W - 1;
            HLINE(rp, hL, hR, y);
        }
    }
}

/* ---- rotofoil sprite ---------------------------------------------- */
/*
 * Draw the opponent as a hovering "spinner": a filled triangle (body)
 * with a horizontal base bar (hover skirt) at the ground point. Scales
 * with 1/depth like the ball. Body pen distinguishes P1 (cyan)
 * from P2 (amber). */
static void draw_rotofoil(struct RastPort *rp, int pane_y0,
                          int cx_ground, int cy_ground, int scale, UBYTE body_pen)
{
    int apex_y;
    int base_l;
    int base_r;
    int span_h;
    int dy;
    if (scale < 6) scale = 6;   /* keep visible even at distance */
    /* Triangle body ~3× tall as wide so it reads from far away. */
    apex_y = cy_ground - scale * 3;
    base_l = cx_ground - scale;
    base_r = cx_ground + scale;
    if (apex_y < pane_y0) apex_y = pane_y0;
    if (base_l < 0)         base_l = 0;
    if (base_r > SCR_W - 1) base_r = SCR_W - 1;

    span_h = cy_ground - apex_y;
    if (span_h < 1) return;

    /* Dark outline first (one-pixel outer silhouette), then body fill
     * so the darker edge remains visible where body doesn't overwrite. */
    SetAPen(rp, PEN_ROTO_EDGE);
#ifdef AMIGA3DO
    {   /* half_w = ((dy + 1) * scale) / span_h, stepped without dividing */
        int q = scale / span_h, rem = scale % span_h;
        for (dy = 0; dy <= span_h; dy++) {
            int half_w = q;
            int y = apex_y + dy;
            int L, R;
            rem += scale;
            while (rem >= span_h) { rem -= span_h; q++; }
            if (y < pane_y0 || y >= pane_y0 + PANE_H) continue;
            L = cx_ground - half_w - 1;
            R = cx_ground + half_w + 1;
            if (L < 0)         L = 0;
            if (R > SCR_W - 1) R = SCR_W - 1;
            HLINE(rp, L, R, y);
        }
    }
    SetAPen(rp, body_pen);
    {   /* half_w = (dy * scale) / span_h */
        int q = scale / span_h, rem = scale % span_h;
        for (dy = 1; dy <= span_h; dy++) {
            int half_w = q;
            int y = apex_y + dy;
            int L, R;
            rem += scale;
            while (rem >= span_h) { rem -= span_h; q++; }
            if (y < pane_y0 || y >= pane_y0 + PANE_H) continue;
            L = cx_ground - half_w;
            R = cx_ground + half_w;
            if (L < 0)         L = 0;
            if (R > SCR_W - 1) R = SCR_W - 1;
            HLINE(rp, L, R, y);
        }
    }
#else
    for (dy = 0; dy <= span_h; dy++) {
        int half_w = ((dy + 1) * scale) / span_h;
        int y = apex_y + dy;
        int L;
        int R;
        if (y < pane_y0 || y >= pane_y0 + PANE_H) continue;
        L = cx_ground - half_w - 1;
        R = cx_ground + half_w + 1;
        if (L < 0)         L = 0;
        if (R > SCR_W - 1) R = SCR_W - 1;
        HLINE(rp, L, R, y);
    }
    /* Body fill inside outline. */
    SetAPen(rp, body_pen);
    for (dy = 1; dy <= span_h; dy++) {
        int half_w = (dy * scale) / span_h;
        int y = apex_y + dy;
        int L;
        int R;
        if (y < pane_y0 || y >= pane_y0 + PANE_H) continue;
        L = cx_ground - half_w;
        R = cx_ground + half_w;
        if (L < 0)         L = 0;
        if (R > SCR_W - 1) R = SCR_W - 1;
        HLINE(rp, L, R, y);
    }
#endif
    /* Hover skirt: two-pixel dark bar centred on ground point. */
    SetAPen(rp, PEN_ROTO_EDGE);
    if (cy_ground < pane_y0 + PANE_H) {
        Move(rp, base_l - 1, cy_ground);
        Draw(rp, base_r + 1, cy_ground);
    }
    if (cy_ground + 1 < pane_y0 + PANE_H) {
        Move(rp, base_l + 1, cy_ground + 1);
        Draw(rp, base_r - 1, cy_ground + 1);
    }
}

/* ---- public entry point ------------------------------------------- */
void pitch_render(struct RastPort *rp, int pane_y0,
                  LONG cam_x, LONG cam_z, LONG cam_angle,
                  const Ball *ball,
                  LONG other_x, LONG other_z, UBYTE other_pen)
{
    LONG ca = math_cos(cam_angle);
    LONG sa = math_sin(cam_angle);

    /* Sky above horizon. */
    UBYTE sky_pen = (pane_y0 == PANE_P1_Y0) ? PEN_SKY_BOT : PEN_SKY_TOP;
    LONG z;
    band(rp, pane_y0, pane_y0 + HORIZON_Y - 1, sky_pen);

    /* Chequered ground. */
    draw_checker_floor(rp, pane_y0, cam_x, cam_z, cam_angle);

    /* Horizon divider — one-pixel high black. */
    SetAPen(rp, PEN_BLACK);
    Move(rp, 0, pane_y0 + HORIZON_Y);
    Draw(rp, SCR_W - 1, pane_y0 + HORIZON_Y);

    /* Goal beams: two posts at each end of the pitch. */
    for (z = -PITCH_WIDTH; z <= PITCH_WIDTH; z += PITCH_WIDTH) {
        WORD sx, sy_bot, sy_top;
        SetAPen(rp, PEN_GOAL_P2);
        if (project(PITCH_LENGTH, -HOVER_H, z, cam_x, cam_z, ca, sa, pane_y0, &sx, &sy_bot)) {
            WORD sxt; int ok_top = project(PITCH_LENGTH, HOVER_H * 4, z,
                                           cam_x, cam_z, ca, sa, pane_y0, &sxt, &sy_top);
            if (ok_top) { Move(rp, sx, sy_bot); Draw(rp, sxt, sy_top); }
        }
        SetAPen(rp, PEN_GOAL_P1);
        if (project(-PITCH_LENGTH, -HOVER_H, z, cam_x, cam_z, ca, sa, pane_y0, &sx, &sy_bot)) {
            WORD sxt; int ok_top = project(-PITCH_LENGTH, HOVER_H * 4, z,
                                           cam_x, cam_z, ca, sa, pane_y0, &sxt, &sy_top);
            if (ok_top) { Move(rp, sx, sy_bot); Draw(rp, sxt, sy_top); }
        }
    }

    /* Opponent rotofoil. Depth-cull first, then project onto ground
     * point and scale the sprite by 1/depth. */
    {
        LONG dx = other_x - cam_x;
        LONG dz = other_z - cam_z;
        LONG zc = ((dx >> 8) * (ca >> 8) + (dz >> 8) * (sa >> 8));
        if (zc > NEAR_CLIP) {
            WORD osx, osy;
            if (project(other_x, -HOVER_H, other_z, cam_x, cam_z, ca, sa, pane_y0, &osx, &osy)) {
                LONG s_px = (2L * ONE * FOCAL) / (zc >> FP);
                int  s    = (int)(s_px >> FP);
                if (s < 4)  s = 4;
                if (s > 40) s = 40;
                draw_rotofoil(rp, pane_y0, osx, osy, s, other_pen);
            }
        }
    }

    /* Ball as a shaded sphere. When the camera is carrying the ball
     * (zc very small or even at NEAR_CLIP), still draw it dead-centre
     * at maximum size so the player sees they've got possession. */
    if (ball) {
        LONG dx = ball->x - cam_x;
        LONG dz = ball->z - cam_z;
        LONG zc = ((dx >> 8) * (ca >> 8) + (dz >> 8) * (sa >> 8));
        if (zc > 0) {
            WORD bsx, bsy;
            if (zc < NEAR_CLIP) {
                /* Too close to project safely — clamp at pane centre
                 * with a big fixed radius. That's the "I've got the
                 * ball" visual. */
                bsx = SCR_W / 2;
                bsy = pane_y0 + HORIZON_Y;
                draw_ball_sphere(rp, pane_y0, bsx, bsy, 32);
            } else if (project(ball->x, 0, ball->z, cam_x, cam_z, ca, sa, pane_y0, &bsx, &bsy)) {
                LONG r_px = (BALL_RADIUS * FOCAL) / (zc >> FP);
                int  r    = (int)(r_px >> FP);
                if (r < 3)  r = 3;
                if (r > 32) r = 32;
                draw_ball_sphere(rp, pane_y0, bsx, bsy, r);
            }
        }
    }
}
