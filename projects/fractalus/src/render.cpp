// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "render.h"
#include "terrain.h"
#include "game.h"
#include "pilots.h"
#include "combat.h"

#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <graphics/displayinfo.h>
#include <graphics/text.h>
#include <intuition/intuition.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <string.h>

extern "C" {
#include "bridge_client.h"
}
#ifdef AMIGA3DO
#include "amiga3do.h"
#endif

#ifndef __PPC__
extern struct IntuitionBase *IntuitionBase;
extern struct GfxBase       *GfxBase;
#endif

/* ------------------------------------------------------------------
 * Terrain / rect fill primitives.
 *
 * 68k (AGA): direct-to-bitplane. graphics.library RectFill for a
 * 4-pixel-wide vertical stripe costs ~700us per call; the actual
 * blitter work is a fraction of that, the rest is graphics.library
 * overhead per call (SetAPen + dispatch). Per-frame render was ~700
 * calls x 700us = 490ms. Our stripes are 8 pixels wide (COL_STEP=8)
 * and byte-aligned (R_VIEW_X = 16). At 8bpp planar that's exactly
 * one byte per plane per row, no masking — writing the bytes
 * ourselves is ~250ns per stripe row, 100-200x faster than RectFill.
 * Safety: only ever called while drawing to the OFF-screen buffer,
 * so display DMA can't race us. A WaitBlit() at the start of
 * draw_terrain fences any in-flight blitter ops from draw_sky.
 *
 * PPC (OS4 / sam460ex): no chip RAM, no accessible bitplanes on the
 * RTG bitmap. Fall back to RectFill via graphics.library — slower
 * but portable. `snapshot_planes` stashes the RastPort so fill_*
 * can issue commands against it without a taller interface change.
 * ------------------------------------------------------------------ */
#if !defined(__PPC__) && !defined(AMIGA3DO)   /* 3DO port: no bitplanes either */
static UBYTE *g_plane[8];
static int    g_plane_bpr;

static void snapshot_planes(struct RastPort *rp)
{
    struct BitMap *bm = rp->BitMap;
    int p;
    for (p = 0; p < 8 && p < bm->Depth; p++) g_plane[p] = bm->Planes[p];
    g_plane_bpr = bm->BytesPerRow;
}

/* Fill an 8-pixel-wide column band from y0..y1 (inclusive) with pen.
 * x MUST be 8-aligned. y0 <= y1 assumed. */
static inline void fill_strip8(int x, int y0, int y1, UBYTE pen)
{
    int byte_off = x >> 3;
    int rows = y1 - y0 + 1;
    int p;
    /* Iterate plane-by-plane: writes stay in the same chip-RAM page,
     * which is dramatically friendlier than 8-plane-per-row hopping. */
    for (p = 0; p < 8; p++) {
        UBYTE v = ((pen >> p) & 1) ? 0xFF : 0x00;
        UBYTE *dst = g_plane[p] + y0 * g_plane_bpr + byte_off;
        int n = rows;
        while (n-- > 0) {
            *dst = v;
            dst += g_plane_bpr;
        }
    }
}

/* Fill an axis-aligned rectangle covering whole bytes horizontally.
 * x1..x2 in pixel coords, both 8-aligned (x2+1 must be multiple of 8).
 * y1..y2 inclusive. memset per row so gcc uses word/long stores. */
static inline void fill_rect_bytes(int x1, int x2, int y1, int y2, UBYTE pen)
{
    int byte_x = x1 >> 3;
    int byte_w = ((x2 + 1) >> 3) - byte_x;
    int rows   = y2 - y1 + 1;
    int p;
    for (p = 0; p < 8; p++) {
        UBYTE v = ((pen >> p) & 1) ? 0xFF : 0x00;
        UBYTE *row = g_plane[p] + y1 * g_plane_bpr + byte_x;
        int n = rows;
        while (n-- > 0) {
            memset(row, v, byte_w);
            row += g_plane_bpr;
        }
    }
}
#else  /* __PPC__ */
/* graphics.library RectFill fallback for OS4/RTG. Same interface as the
 * 68k bitplane path so all callers stay portable. One micro-optimisation
 * — SetAPen elision — caches the last pen written to the RastPort and
 * skips redundant re-sets. Even on OS4 that's a real per-call save
 * because graphics.library still does pen bookkeeping every time. */
static struct RastPort *g_fill_rp = 0;
static LONG g_last_pen = -1;

/* Adjacent-column batching was tried here and hung the game on OS4 —
 * likely a state ordering bug between phases. Left out until we can
 * profile it in isolation. Naive one-RectFill-per-strip is what runs
 * in production. */
static inline void ppc_batch_flush(void) { /* no-op stub */ }

static void snapshot_planes(struct RastPort *rp)
{
    g_fill_rp = rp;
    g_last_pen = -1;   /* pen state is per-RastPort — reset on rebind. */
}

static inline void fill_strip8(int x, int y0, int y1, UBYTE pen)
{
#ifdef AMIGA3DO
    /* 3DO port: 8 pixels at an 8-aligned x in the layer's 16-bit buffer
     * are four aligned 32-bit stores per row (no RectFill per strip) */
    UWORD *pix = gfx_pixels16();
    if (pix && g_fill_rp && x >= g_fill_rp->clip_x0 && x + 7 <= g_fill_rp->clip_x1 &&
        y0 >= g_fill_rp->clip_y0 && y1 <= g_fill_rp->clip_y1 && !(x & 7)) {
        ULONG c = gfx_pen_rgb16(pen), cc = (c << 16) | c;
        ULONG *d = (ULONG *)(pix + (LONG)y0 * GFX_WIDTH + x);
        int n = y1 - y0 + 1;
        while (n-- > 0) {
            d[0] = cc; d[1] = cc; d[2] = cc; d[3] = cc;
            d += GFX_WIDTH / 2;
        }
        return;
    }
#endif
    if (!g_fill_rp) return;
    if (g_last_pen != (LONG)pen) {
        SetAPen(g_fill_rp, (ULONG)pen);
        g_last_pen = pen;
    }
    RectFill(g_fill_rp, x, y0, x + 7, y1);
}

static inline void fill_rect_bytes(int x1, int x2, int y1, int y2, UBYTE pen)
{
    if (!g_fill_rp) return;
    if (g_last_pen != (LONG)pen) {
        SetAPen(g_fill_rp, (ULONG)pen);
        g_last_pen = pen;
    }
    RectFill(g_fill_rp, x1, y1, x2, y2);
}
#endif

/*
 * Runtime bench mask — bit toggles let us disable individual render
 * phases from the bridge without recompiling. Points a stopwatch at
 * each phase in isolation so we know where to optimise instead of
 * guessing.
 *
 *   bit 0  skip terrain (replace with one flat RectFill)
 *   bit 1  skip sprites (saucers + bullets)
 *   bit 2  skip cockpit frame
 *   bit 3  skip HUD (bars, text, radar)
 *   bit 4  skip sky gradient (replace with one flat RectFill)
 *   bit 5  skip flip (no ChangeScreenBuffer, no WaitTOF)
 */
LONG g_bench_mask = 0;
#define BENCH_TERRAIN  0x01
#define BENCH_SPRITES  0x02
#define BENCH_COCKPIT  0x04
#define BENCH_HUD      0x08
#define BENCH_SKY      0x10
#define BENCH_FLIP     0x20

/* ------------------------------------------------------------------ */
/* Shared integer sin/cos — TRIG_ONE scaled, ANGLE_FULL entries.        */
LONG sin_table[ANGLE_FULL];

LONG isin(LONG a) { return sin_table[a & ANGLE_MASK]; }
LONG icos(LONG a) { return sin_table[(a + ANGLE_QUART) & ANGLE_MASK]; }

/*
 * Bhaskara I sine approximation, accurate to ~0.16%:
 *   sin(x) ≈ 16 x (π - x) / (5π² - 4 x (π - x))    for x in [0, π]
 *
 * In our units x is [0, ANGLE_HALF] representing [0, π]. Result is
 * scaled by TRIG_ONE (4096). Denominator is pre-divided by TRIG_ONE
 * so the final division stays in 32-bit even without long-long.
 *
 * (Earlier version had a factor-of-4 bug in the numerator — every
 * rotation in the game was 1/4 of intended, which showed up first as
 * saucer spawn distances collapsing to a quarter of the requested
 * range. Fixed here.)
 */
void render_init_math()
{
    for (LONG i = 0; i < ANGLE_FULL; i++) {
        LONG a = i;
        LONG sign = 1;
        if (a >= ANGLE_HALF) { a -= ANGLE_HALF; sign = -1; }
        LONG x = a;
        LONG y = ANGLE_HALF - a;
        LONG xy  = x * y;                        /* 0..1M */
        LONG num = 16L * xy;                     /* 0..16.7M */
        LONG den = (5L * ANGLE_HALF * ANGLE_HALF - 4L * xy) / TRIG_ONE;
        if (den == 0) den = 1;
        LONG s = num / den;                      /* TRIG_ONE-scaled 0..4096 */
        sin_table[i] = (LONG)(s * sign);
    }
}

/* ------------------------------------------------------------------ */
/* Palette generator.                                                   */

static void put_rgb(struct ViewPort *vp, UWORD pen, int r, int g, int b)
{
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;
#ifdef AMIGA3DO
    (void)vp;
    gfx_set_rgb24(pen, ((ULONG)r << 16) | ((ULONG)g << 8) | (ULONG)b);
#else
    SetRGB32(vp, pen, (ULONG)r << 24, (ULONG)g << 24, (ULONG)b << 24);
#endif
}

void Renderer::install_palette()
{
#ifdef AMIGA3DO
    struct ViewPort *vp = 0;        /* put_rgb() sets the layer's palette */
#else
    struct ViewPort *vp = &screen->ViewPort;
#endif
    int i;

    /* Sky: deep indigo at top → dusty orange at horizon. Fractalus's
     * planet was mostly seen at twilight; this palette mirrors that. */
    for (i = 0; i < PAL_SKY_COUNT; i++) {
        int t = i;
        int r =  20 + (t * 220) / (PAL_SKY_COUNT - 1);
        int g =  10 + (t * 100) / (PAL_SKY_COUNT - 1);
        int b =  90 - (t *  70) / (PAL_SKY_COUNT - 1);
        put_rgb(vp, PAL_SKY_BASE + i, r, g, b);
    }

    /* Terrain: 8 height bins × 8 fog bins.
     *   height ramp:  dark green (low) → sandy tan (peaks)
     *   fog: mix with horizon sky colour as distance grows */
    static const struct { int r, g, b; } height_ramp[8] = {
        {  20,  40,  16 },   /* deep valley */
        {  30,  70,  22 },
        {  45, 100,  30 },
        {  60, 130,  36 },
        {  95, 150,  44 },
        { 140, 160,  52 },
        { 180, 155,  70 },
        { 210, 175,  95 },   /* sun-lit peak */
    };
    int horizon_r = 240, horizon_g = 110, horizon_b = 20;
    for (int h = 0; h < 8; h++) {
        for (int d = 0; d < 8; d++) {
            /* d=0 near (crisp), d=7 far (heavily fogged) */
            int mix = d * 32;   /* 0..224 */
            int r = height_ramp[h].r + ((horizon_r - height_ramp[h].r) * mix) / 256;
            int g = height_ramp[h].g + ((horizon_g - height_ramp[h].g) * mix) / 256;
            int b = height_ramp[h].b + ((horizon_b - height_ramp[h].b) * mix) / 256;
            put_rgb(vp, PAL_TERRAIN_BASE + h * 8 + d, r, g, b);
        }
    }

    /* Cockpit: dull metallic browns/greys. */
    for (i = 0; i < PAL_COCKPIT_COUNT; i++) {
        int v = 40 + i * 10;
        put_rgb(vp, PAL_COCKPIT_BASE + i, v, v - 5, v - 15);
    }

    /* HUD: classic phosphor greens. */
    for (i = 0; i < PAL_HUD_COUNT; i++) {
        int v = 30 + i * 15;
        put_rgb(vp, PAL_HUD_BASE + i, v / 4, v, v / 3);
    }

    /* MISC pens for FX (jumpscare flash, teeth, blood tones, etc). */
    put_rgb(vp, PAL_MISC_BASE + 0, 30, 30, 30);          /* dark grey  */
    put_rgb(vp, PAL_MISC_BASE + 1, 220, 220, 220);       /* light grey */
    put_rgb(vp, PAL_MISC_BASE + 2, 140, 20, 10);         /* dark red   */
    put_rgb(vp, PAL_MISC_BASE + 3, 220, 60, 30);         /* mid red    */
    put_rgb(vp, PAL_MISC_BASE + 4, 250, 200, 40);        /* bright     */
    put_rgb(vp, PAL_MISC_BASE + 5, 20, 200, 40);         /* laser grn  */
    put_rgb(vp, PAL_MISC_BASE + 6, 250, 250, 220);       /* pilot suit */
    put_rgb(vp, PAL_MISC_BASE + 7, 255, 255, 210);       /* teeth      */
}

/* ------------------------------------------------------------------ */
/* Display setup.                                                       */

#ifdef AMIGA3DO
/* 3DO port: 256-colour screen = the layer's 16-bit mode (256-entry palette) */
int Renderer::open_display()
{
    screen = 0;
    window = 0;
    sbuf[0] = sbuf[1] = 0;
    safe_port = 0;
    if (!gfx_init_mode(GFX_RGB16, R_SCREEN_H, 0, 0))
        return 1;
    install_palette();
    cur_buf = 1;
    first_flip = 1;
    return 0;
}

void Renderer::close_display() { gfx_exit(); }

struct MsgPort *Renderer::user_port() const { return 0; }
#else
int Renderer::open_display()
{
    memset(&rp_buf[0], 0, sizeof(rp_buf[0]));
    memset(&rp_buf[1], 0, sizeof(rp_buf[1]));

    screen = OpenScreenTags(NULL,
        SA_Width,     R_SCREEN_W,
        SA_Height,    R_SCREEN_H,
        SA_Depth,     8,
        SA_DisplayID, LORES_KEY,
        SA_Title,     (ULONG)"Fractalus",
        SA_ShowTitle, FALSE,
        SA_Quiet,     TRUE,
        SA_Type,      CUSTOMSCREEN,
        TAG_DONE);
    if (!screen) return 1;

    window = OpenWindowTags(NULL,
        WA_CustomScreen, (ULONG)screen,
        WA_Left, 0, WA_Top, 0,
        WA_Width, R_SCREEN_W, WA_Height, R_SCREEN_H,
        WA_Borderless, TRUE, WA_Backdrop, TRUE,
        WA_Activate, TRUE,
        WA_IDCMP, IDCMP_RAWKEY,
        TAG_DONE);
    if (!window) { CloseScreen(screen); screen = NULL; return 2; }

    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) {
        /* close_display handles null-guarded release of any of
         * sbuf[0/1] + window + screen that we managed to open. */
        close_display();
        return 3;
    }

    /* Historically we set dbi_SafeMessage.mn_ReplyPort on both buffers
     * intending to use safe-message handshake for the double-buffer
     * swap. flip() never GetMsg()'d the port though, so on OS4 the
     * queued safe-messages appear to jam the RTG buffer swap and
     * produce a corrupted display (uninitialised memory shown as
     * random pixels; every RectFill lands invisibly on a not-yet-
     * swapped bitmap). terrain_test and void_trader — both dual-target
     * with the same AllocScreenBuffer pattern — skip this setup and
     * render cleanly on both arches, so we do too. safe_port field
     * kept for future proper implementation; NULL means "no safe
     * message needed" per exec docs. */
    safe_port = NULL;

    InitRastPort(&rp_buf[0]); rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]); rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    install_palette();
    /* Start with cur_buf=1 so frame 1 draws to sbuf[1] (allocated
     * off-screen), then flips to it — visible bitmap sbuf[0] is
     * untouched until frame 2 draws to it (now off-screen after the
     * flip). Prevents the visible left-to-right build on frame 1. */
    cur_buf = 1;
    first_flip = 1;
    return 0;
}

void Renderer::close_display()
{
    if (sbuf[0]) {
        int t = 0;
        while (!ChangeScreenBuffer(screen, sbuf[0]) && ++t < 5) WaitTOF();
        WaitTOF(); WaitTOF();
    }
    if (sbuf[1]) { FreeScreenBuffer(screen, sbuf[1]); sbuf[1] = NULL; }
    if (sbuf[0]) { FreeScreenBuffer(screen, sbuf[0]); sbuf[0] = NULL; }
    if (safe_port) { DeleteMsgPort(safe_port); safe_port = NULL; }
    if (window) { CloseWindow(window); window = NULL; }
    if (screen) { CloseScreen(screen); screen = NULL; }
}

struct MsgPort *Renderer::user_port() const
{
    return window ? window->UserPort : NULL;
}
#endif

/* ------------------------------------------------------------------ */
/* Frame render.                                                        */

void Renderer::draw_sky(struct RastPort *rp)
{
    /* 32 horizontal bands across the flying viewport. Direct-plane
     * fills — same trick as draw_terrain. Viewport spans x=16..303
     * = bytes 2..37 (36 bytes wide) which is 8-aligned both ends,
     * so memset-per-row nails it. Saves ~20 ms/frame vs 32 RectFills. */
    WaitBlit();
    snapshot_planes(rp);
    for (int i = 0; i < PAL_SKY_COUNT; i++) {
        int y0 = R_VIEW_Y + (i * R_VIEW_H) / PAL_SKY_COUNT;
        int y1 = R_VIEW_Y + ((i + 1) * R_VIEW_H) / PAL_SKY_COUNT - 1;
        fill_rect_bytes(R_VIEW_X, R_VIEW_X2, y0, y1,
                        (UBYTE)(PAL_SKY_BASE + i));
    }
}

/*
 * Column-by-column heightfield renderer. For each visible column of the
 * viewport we cast a ray in the XZ plane, sample the terrain at each
 * step, project to screen Y, and fill the newly-exposed pixels above
 * whatever we've drawn already. Painter's algorithm, front-to-back.
 */
#ifdef AMIGA3DO
/* 3DO port: terrain helpers for draw_terrain (see fracmarch.s) */
#if R_PROJ != 220 || TRIG_SHIFT != 12 || TERRAIN_CELL_SHIFT != 6 || \
    TERRAIN_BITS != 7 || TERRAIN_HEIGHT_SHIFT != 1
#error fracmarch.s hard-codes these
#endif
struct FracMarch {
    LONG ax, az, sx, sz, thr, mst, count;
    const UBYTE *map;
    LONG h;
};
extern "C" int frac_march(struct FracMarch *m);

static UBYTE s_map[(TERRAIN_SIZE + 1) * (TERRAIN_SIZE + 1)];
static ULONG s_map_seed;
static int   s_map_valid;
static LONG  s_dbin[256];
static UBYTE s_hbin[TERRAIN_MAX_HEIGHT + 1];

/* frac_march's bilinear sample, in C (for a ray's first sample) */
static LONG map_height(const UBYTE *map, LONG ax, LONG az)
{
    LONG fx = (ax >> 12) & 63, fz = (az >> 12) & 63;
    const UBYTE *c = map + ((az >> 18) & 127) * 129 + ((ax >> 18) & 127);
    LONG a = (c[0] << 1) + (((c[1] - c[0]) * fx) >> 5);
    LONG b = (c[129] << 1) + (((c[130] - c[129]) * fx) >> 5);
    return a + (((b - a) * fz) >> 6);
}

/* threshold multiplier: draws iff n >= mult * dist (+1 when T < 0) */
static LONG march_mult(LONG T) { return T >= 0 ? T + 1 : T; }

/* Draw the strip for a sample known to draw; returns the new column top.
 * q = trunc(n / dist) clamped to [q_lo, q_hi] without a divide: "q >= k"
 * is n >= k*dist (k > 0) or n > (k-1)*dist (k <= 0), raised a row at a
 * time (the total is the column's drawn height). */
static int voxel_strip(int cx1, int y_top, LONG n, LONG dist, LONG horizon_y,
                       LONG h, LONG dbin)
{
    LONG q_hi = horizon_y - R_VIEW_Y, k, acc;
    int h_bin;
    if (y_top <= R_VIEW_Y2)
        k = horizon_y - y_top + 1;          /* known: q >= k */
    else
        k = horizon_y - R_VIEW_Y2;          /* first strip: clamp */
    acc = k * dist;
    while (k < q_hi && (k + 1 > 0 ? n >= acc + dist : n > acc)) {
        k++;
        acc += dist;
    }
    h_bin = (h < 0) ? 0 : (h > TERRAIN_MAX_HEIGHT) ? 7 : s_hbin[h];
    fill_strip8(cx1, (int)(horizon_y - k), y_top - 1,
                (UBYTE)(PAL_TERRAIN_BASE + h_bin * 8 + dbin));
    return (int)(horizon_y - k);
}
#endif

void Renderer::draw_terrain(struct RastPort *rp, const GameState &gs,
                            const Terrain &world)
{
    const ShipState &s = gs.ship;

    /* FOV: ~60° across R_VIEW_W. Pre-compute the per-column yaw offset. */
    const LONG fov_span = 512;   /* ~45° (out of 4096 = 360°) */

    LONG cam_yaw   = s.yaw;
    LONG cam_y     = s.y;
    LONG cam_x_int = FX16_TOINT(s.x);
    LONG cam_z_int = FX16_TOINT(s.z);

    /* Pitch shift: nose down = look further down = horizon moves up on
     * screen. 380 pitch units ≈ 33°, translates to ~+40 pixels. */
    LONG horizon_shift = -(s.pitch * 40) / SHIP_PITCH_MAX;
    LONG horizon_y = R_HORIZON_Y + horizon_shift;
    if (horizon_y < R_VIEW_Y) horizon_y = R_VIEW_Y;
    if (horizon_y > R_VIEW_Y2) horizon_y = R_VIEW_Y2;

    /* Match terrain_test's proven config exactly — same PROJ so
     * sprite billboarding lines up, same FAR_DIST so ground samples
     * actually reach the horizon on screen. */
    const LONG PROJ    = R_PROJ;
    const LONG MAX_DIST = R_FAR_DIST;

    /* Sky already fills above horizon; below-horizon starts empty and
     * gets painted by the terrain loop. Anything the ray-march didn't
     * reach we backstop with the horizon-fog terrain colour. */

    /* --- Voxel-space column renderer -----------------------------------
     * Front-to-back painter's algorithm. For each 4-pixel column band,
     * ray-march the heightfield; every sample that pokes higher on
     * screen than the previously-drawn top paints a stripe from
     * screen_y down to the old top with a pen picked by (height,
     * distance). Multiple stripes per column give the vertical
     * gradient that reads as depth (near=crisp, far=orange fog); the
     * silhouette of the topmost stripe is the mountain skyline.
     *
     * COL_STEP=4 caps blitter ops around ~600-800 per frame while
     * keeping the horizontal resolution readable at LORES.
     * -----------------------------------------------------------------*/

    /* Voxel-space raycaster. Fills are direct byte writes into the
     * bitplanes via fill_strip8 — see the header comment above for
     * why this beats RectFill 100:1 for narrow stripes.
     *
     * WaitBlit fences the sky's in-flight blitter ops so we don't
     * race the blitter writing to the same plane bytes. */
    WaitBlit();
    snapshot_planes(rp);

#ifdef AMIGA3DO
    if (!s_map_valid || s_map_seed != gs.seed) {
        /* 129 x 129 copy of the wrapping heightmap: the bilinear
         * neighbours of any cell are then +1 / +129 / +130 */
        const UBYTE *src = world.raw();
        int x, z;
        for (z = 0; z <= TERRAIN_SIZE; z++)
            for (x = 0; x <= TERRAIN_SIZE; x++)
                s_map[z * (TERRAIN_SIZE + 1) + x] =
                    src[(z & TERRAIN_MASK) * TERRAIN_SIZE + (x & TERRAIN_MASK)];
        s_map_seed = gs.seed;
        s_map_valid = 1;
    }
#endif
    for (int col = 0; col < R_VIEW_W; col += R_COL_STEP) {
        LONG dcol    = (LONG)col - (R_VIEW_W >> 1);
        LONG ray_yaw = (cam_yaw + (dcol * fov_span) / R_VIEW_W) & ANGLE_MASK;
        LONG rdx     = isin(ray_yaw);
        LONG rdz     = icos(ray_yaw);

        int y_top = R_VIEW_Y2 + 1;              /* nothing drawn yet */
        int cx1   = R_VIEW_X + col;
        int cx2   = cx1 + R_COL_STEP - 1;
        if (cx2 > R_VIEW_X2) cx2 = R_VIEW_X2;

#ifdef AMIGA3DO
        /* 3DO port: the ray distances are the same for every column, so
         * they and the height/distance colour bins are tables. */
        static LONG s_dist[256];
        static LONG s_seg_step[16];
        static int s_seg_count[16], s_seg_first[16], s_nseg = 0;
        static int s_nsteps = 0;
        if (!s_nsteps) {
            LONG d = R_NEAR_DIST, st = 2;
            int k;
            while (d < MAX_DIST && s_nsteps < 256) {
                int b = (int)((d * 8) / MAX_DIST);
                s_dist[s_nsteps] = d;
                s_dbin[s_nsteps] = b < 0 ? 0 : b > 7 ? 7 : b;
                s_nsteps++;
                d += st;
                if (d >   40) st = 4;
                if (d >  120) st = 10;
                if (d >  400) st = 30;
                if (d > 1200) st = 80;
                if (d > 3000) st = 200;
            }
            /* samples 1.. as runs of equal step */
            for (k = 1; k < s_nsteps; k++) {
                LONG stp = s_dist[k] - s_dist[k - 1];
                if (s_nseg && s_seg_step[s_nseg - 1] == stp)
                    s_seg_count[s_nseg - 1]++;
                else {
                    s_seg_step[s_nseg] = stp;
                    s_seg_count[s_nseg] = 1;
                    s_seg_first[s_nseg] = k;
                    s_nseg++;
                }
            }
            for (k = 0; k <= TERRAIN_MAX_HEIGHT; k++) {
                int hb = (int)(((LONG)k * 8) / (TERRAIN_MAX_HEIGHT + 1));
                s_hbin[k] = (UBYTE)(hb < 0 ? 0 : hb > 7 ? 7 : hb);
            }
        }
        {
            /* Front to back along the ray. With q = trunc(n / dist),
             * n = (h - cam_y) * PROJ and T = horizon_y - y_top, a sample
             * draws iff q > T: n >= (T+1)*dist (T >= 0) or n >= T*dist + 1
             * (T < 0). That threshold follows dist by addition, so
             * frac_march (fracmarch.s) tests hidden samples with one
             * compare; strips are drawn here. The ray walks the segments
             * of constant step from sample 1; sample 0 always draws. */
            struct FracMarch fm;
            const LONG cyp = cam_y * PROJ;
            LONG mm, h;
            int g;
            fm.ax = ((cam_x_int & 8191) << TRIG_SHIFT) + rdx * s_dist[0];
            fm.az = ((cam_z_int & 8191) << TRIG_SHIFT) + rdz * s_dist[0];
            fm.map = s_map;
            h = map_height(s_map, fm.ax, fm.az);
            y_top = voxel_strip(cx1, y_top, (h - cam_y) * PROJ, s_dist[0], horizon_y, h, s_dbin[0]);
            mm = march_mult(horizon_y - y_top);
            fm.thr = mm * s_dist[0] + (horizon_y - y_top < 0) + cyp;
            for (g = 0; g < s_nseg && y_top > R_VIEW_Y; g++) {
                const LONG st = s_seg_step[g];
                fm.sx = rdx * st;
                fm.sz = rdz * st;
                fm.mst = mm * st;
                fm.count = s_seg_count[g];
                while (frac_march(&fm)) {
                    int si = s_seg_first[g] + s_seg_count[g] - (int)fm.count - 1;
                    LONG dist = s_dist[si];
                    y_top = voxel_strip(cx1, y_top, (fm.h - cam_y) * PROJ, dist,
                                        horizon_y, fm.h, s_dbin[si]);
                    if (y_top <= R_VIEW_Y)
                        break;
                    mm = march_mult(horizon_y - y_top);
                    fm.thr = mm * dist + (horizon_y - y_top < 0) + cyp;
                    fm.mst = mm * st;
                    if (!fm.count)
                        break;
                }
            }
        }
#else
        LONG dist = R_NEAR_DIST;
        LONG step = 2;

        while (dist < MAX_DIST && y_top > R_VIEW_Y) {
            LONG wx = cam_x_int + ((rdx * dist) >> TRIG_SHIFT);
            LONG wz = cam_z_int + ((rdz * dist) >> TRIG_SHIFT);
            LONG h  = world.height_at_world(wx, wz);

            LONG dy        = h - cam_y;
            LONG projected = horizon_y - (dy * PROJ) / dist;
            if (projected < R_VIEW_Y)  projected = R_VIEW_Y;
            if (projected > R_VIEW_Y2) projected = R_VIEW_Y2;

            if (projected < y_top) {
                int h_bin = (int)((h * 8) / (TERRAIN_MAX_HEIGHT + 1));
                int d_bin = (int)((dist * 8) / MAX_DIST);
                if (h_bin < 0) h_bin = 0; else if (h_bin > 7) h_bin = 7;
                if (d_bin < 0) d_bin = 0; else if (d_bin > 7) d_bin = 7;
                fill_strip8(cx1, (int)projected, y_top - 1,
                            (UBYTE)(PAL_TERRAIN_BASE + h_bin * 8 + d_bin));
                y_top = (int)projected;
            }

            /* Log-ish adaptive step (from terrain_test). */
            dist += step;
            if (dist >   40) step = 4;
            if (dist >  120) step = 10;
            if (dist >  400) step = 30;
            if (dist > 1200) step = 80;
            if (dist > 3000) step = 200;
        }
#endif
    }
}

/*
 * Static cockpit frame. Two dark bands on either side of the flying
 * viewport plus a chunky dashboard beneath. Later phases hang gauges
 * and pilot indicators off this.
 */
void Renderer::draw_cockpit(struct RastPort *rp, const GameState &)
{
    /* Four large frame fills go direct-to-plane; only the 1-pixel
     * highlight border still uses the blitter Line mode via
     * Move/Draw where the byte-alignment win doesn't matter. */
    WaitBlit();
    snapshot_planes(rp);
    /* Left pillar (0..15) and right pillar (304..319) — 2 bytes wide each. */
    fill_rect_bytes(0,             R_VIEW_X - 1,   0, R_SCREEN_H - 1,
                    (UBYTE)(PAL_COCKPIT_BASE + 3));
    fill_rect_bytes(R_VIEW_X2 + 1, R_SCREEN_W - 1, 0, R_SCREEN_H - 1,
                    (UBYTE)(PAL_COCKPIT_BASE + 3));
    /* Top strip above the viewport. */
    fill_rect_bytes(R_VIEW_X, R_VIEW_X2, 0, R_VIEW_Y - 1,
                    (UBYTE)(PAL_COCKPIT_BASE + 3));
    /* Bottom dashboard — full-screen width. */
    fill_rect_bytes(0, R_SCREEN_W - 1, R_VIEW_Y2 + 1, R_SCREEN_H - 1,
                    (UBYTE)(PAL_COCKPIT_BASE + 2));

    /* Highlight border: 4 short lines, cheap via graphics.library. */
    SetAPen(rp, PAL_COCKPIT_BASE + 8);
    Move(rp, R_VIEW_X - 1, R_VIEW_Y - 1);
    Draw(rp, R_VIEW_X2 + 1, R_VIEW_Y - 1);
    Draw(rp, R_VIEW_X2 + 1, R_VIEW_Y2 + 1);
    Draw(rp, R_VIEW_X - 1, R_VIEW_Y2 + 1);
    Draw(rp, R_VIEW_X - 1, R_VIEW_Y - 1);
}

static void draw_bar(struct RastPort *rp, int x, int y, int w, int h,
                     int filled_w, UBYTE bg, UBYTE fg)
{
    SetAPen(rp, bg);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
    if (filled_w > 0) {
        SetAPen(rp, fg);
        RectFill(rp, x, y, x + filled_w - 1, y + h - 1);
    }
    /* Border */
    SetAPen(rp, PAL_HUD_BASE + 14);
    Move(rp, x - 1, y - 1);
    Draw(rp, x + w, y - 1);
    Draw(rp, x + w, y + h);
    Draw(rp, x - 1, y + h);
    Draw(rp, x - 1, y - 1);
}

/* Compass arrow to the nearest active pilot. Drawn as a small
 * triangle at (cx, cy) pointing along (nx, nz) — where nx/nz is the
 * unit vector from the ship to the pilot in world space, ROTATED into
 * the ship's local yaw frame so "up" on screen means "ahead of us". */
static void draw_arrow(struct RastPort *rp, int cx, int cy,
                       LONG local_dx, LONG local_dz, UBYTE pen)
{
    /* Normalise to a fixed radius so all arrows look the same size. */
    LONG len2 = local_dx * local_dx + local_dz * local_dz;
    if (len2 == 0) return;
    LONG len = 0, r = 0, bit = 1L << 30, v = len2;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    len = r ? r : 1;
    const LONG R = 12;
    LONG ux = (local_dx * R) / len;
    LONG uz = (local_dz * R) / len;
    LONG px = -uz;              /* perpendicular for the tail width */
    LONG pz = ux;
    SetAPen(rp, pen);
    /* Head at (cx+ux, cy-uz)  — screen +y = world -z (looking up on
     * the radar means "ahead"). */
    Move(rp, cx + (int)(px / 2), cy - (int)(pz / 2));
    Draw(rp, cx + (int)ux,       cy - (int)uz);
    Draw(rp, cx - (int)(px / 2), cy + (int)(pz / 2));
}

void Renderer::draw_hud(struct RastPort *rp, const GameState &gs,
                        const PilotList &pilots)
{
    /* Dashboard occupies y = R_VIEW_Y2+2 .. R_SCREEN_H-1 (~70 tall). */
    int dash_y = R_VIEW_Y2 + 6;

    /* Fuel bar */
    {
        int filled = (int)((gs.fuel * 60) / 1000);
        draw_bar(rp, 40, dash_y, 60, 8, filled,
                 PAL_HUD_BASE + 2, PAL_HUD_BASE + 12);
    }
    /* Shield bar */
    {
        int filled = (int)((gs.shield * 60) / 1000);
        draw_bar(rp, 40, dash_y + 16, 60, 8, filled,
                 PAL_HUD_BASE + 2, PAL_HUD_BASE + 10);
    }

    /* Text labels + numeric readouts */
    SetAPen(rp, PAL_HUD_BASE + 15);
    SetDrMd(rp, JAM1);
    Move(rp, 8, dash_y + 7);       Text(rp, (STRPTR)"FUEL", 4);
    Move(rp, 8, dash_y + 23);      Text(rp, (STRPTR)"SHLD", 4);

    /* Compass / heading readout */
    char buf[32];
    LONG deg = ((gs.ship.yaw * 360) / ANGLE_FULL);
    if (deg < 0) deg += 360;
    sprintf(buf, "HDG %03ld", (long)deg);
    Move(rp, 200, dash_y + 7);     Text(rp, (STRPTR)buf, 7);

    LONG alt_units = gs.ship.y;
    sprintf(buf, "ALT %04ld", (long)alt_units);
    Move(rp, 200, dash_y + 23);    Text(rp, (STRPTR)buf, 8);

    sprintf(buf, "SPD %02ld", (long)gs.ship.speed);
    Move(rp, 260, dash_y + 39);    Text(rp, (STRPTR)buf, 6);

    sprintf(buf, "SCORE %05ld", (long)gs.score);
    Move(rp, 8, dash_y + 39);      Text(rp, (STRPTR)buf, 11);

    sprintf(buf, "SAVED %ld/%ld",
            (long)gs.pilots_rescued, (long)MAX_PILOTS);
    Move(rp, 108, dash_y + 39);    Text(rp, (STRPTR)buf, 10);

    /* Keys hint bottom-right. */
    SetAPen(rp, PAL_HUD_BASE + 9);
#ifdef AMIGA3DO   /* 3DO port: pad labels */
    Move(rp, 200, dash_y + 55); Text(rp, (STRPTR)"UP/DN SPEED", 11);
#else
    Move(rp, 200, dash_y + 55); Text(rp, (STRPTR)"W/S THRUST", 10);
#endif
#ifdef AMIGA3DO   /* 3DO port: pad labels */
    Move(rp, 200, dash_y + 65); Text(rp, (STRPTR)"B LAND A FIRE", 13);
#else
    Move(rp, 200, dash_y + 65); Text(rp, (STRPTR)"A/D TURN  L", 11);
#endif

    /* Radar dot in the middle of the dashboard, arrow to nearest pilot. */
    int rx = 155, ry = dash_y + 25;
    SetAPen(rp, PAL_HUD_BASE + 4);
    RectFill(rp, rx - 14, ry - 14, rx + 14, ry + 14);
    SetAPen(rp, PAL_HUD_BASE + 14);
    Move(rp, rx - 15, ry - 15); Draw(rp, rx + 15, ry - 15);
    Draw(rp, rx + 15, ry + 15); Draw(rp, rx - 15, ry + 15);
    Draw(rp, rx - 15, ry - 15);

    /* Radar shows the nearest ACTIVE pilot regardless of distance —
     * on this map some spawns are 3-4000 units out, and the previous
     * 4000-unit cutoff made the arrow blink out when the nearest one
     * drifted out of range. Terrain wraps at 8192 so anything farther
     * than that is aliased-in via another wrap anyway. */
    LONG pdist = 0;
    LONG pi = pilots.find_nearest(FX16_TOINT(gs.ship.x),
                                  FX16_TOINT(gs.ship.z),
                                  99999, &pdist);
    if (pi >= 0) {
        /* World delta */
        LONG wdx = pilots[pi].x - FX16_TOINT(gs.ship.x);
        LONG wdz = pilots[pi].z - FX16_TOINT(gs.ship.z);
        /* Rotate into ship frame — yaw 0 = +Z ahead, so local_z
         * component wants to be the sin/cos rotation of the world vec. */
        LONG cy = icos(gs.ship.yaw);
        LONG sy = isin(gs.ship.yaw);
        LONG local_x = (wdx * cy - wdz * sy) >> TRIG_SHIFT;
        LONG local_z = (wdx * sy + wdz * cy) >> TRIG_SHIFT;
        draw_arrow(rp, rx, ry, local_x, local_z, PAL_HUD_BASE + 12);
    }
}

/* Project a world point (wx, wy, wz) into the flying viewport using
 * the same math the terrain raycaster uses. Returns 0 if the point is
 * behind the camera or off-screen. If in-view, fills sx/sy and sz
 * (depth for size scaling). */
static int project(LONG wx, LONG wy, LONG wz,
                   LONG cam_x, LONG cam_y, LONG cam_z, LONG cam_yaw,
                   LONG horizon_y, LONG proj,
                   int *sx, int *sy, LONG *sz_out)
{
    LONG dx = wx - cam_x;
    LONG dz = wz - cam_z;
    LONG sy_yaw = isin(cam_yaw);
    LONG cy_yaw = icos(cam_yaw);
    LONG local_z = ((dx * sy_yaw + dz * cy_yaw) >> TRIG_SHIFT);
    if (local_z < 16) return 0;   /* behind or too close */
    LONG local_x = ((dx * cy_yaw - dz * sy_yaw) >> TRIG_SHIFT);
    LONG dy = wy - cam_y;
    LONG screen_x = R_VIEW_X + (R_VIEW_W >> 1) + (local_x * proj) / local_z;
    LONG screen_y = horizon_y - (dy * proj) / local_z;
    /* Only cull sprites that are wildly off-screen; anything within
     * ~150px overhang still gets a partially-clipped draw so mid-
     * altitude enemies close to the horizon read as approaching. */
    if (screen_x < R_VIEW_X - 150 || screen_x > R_VIEW_X2 + 150) return 0;
    if (screen_y < R_VIEW_Y - 150 || screen_y > R_VIEW_Y2 + 150) return 0;
    *sx = (int)screen_x;
    *sy = (int)screen_y;
    *sz_out = local_z;
    return 1;
}

static void draw_saucer(struct RastPort *rp, int sx, int sy, int size,
                        UBYTE dying)
{
    /* Saucer body is an oval-ish 3-band shape drawn with RectFills.
     * Size scales with distance; kept clipped so nothing writes off
     * the viewport bounds. */
    if (size < 3) size = 3;
    if (size > 60) size = 60;
    int hw = size;             /* half-width */
    int hh = size >> 1;        /* half-height */
    UBYTE body = dying ? PAL_MISC_BASE + 4 : PAL_MISC_BASE + 3;   /* yellow / red */
    UBYTE dark = dying ? PAL_MISC_BASE + 3 : PAL_MISC_BASE + 2;
    UBYTE glass = dying ? PAL_MISC_BASE + 7 : PAL_MISC_BASE + 5;  /* teeth-white / laser-green */

    int x0 = sx - hw, x1 = sx + hw;
    int y0 = sy - hh, y1 = sy + hh;
    if (x0 < R_VIEW_X) x0 = R_VIEW_X;
    if (x1 > R_VIEW_X2) x1 = R_VIEW_X2;
    if (y0 < R_VIEW_Y) y0 = R_VIEW_Y;
    if (y1 > R_VIEW_Y2) y1 = R_VIEW_Y2;
    if (x0 >= x1 || y0 >= y1) return;

    /* Wide middle band. */
    SetAPen(rp, body);
    RectFill(rp, x0, sy - 1, x1, sy + 1);
    /* Thinner top/bottom rims. */
    int rim = hw - (hw >> 2);
    SetAPen(rp, dark);
    int rx0 = sx - rim, rx1 = sx + rim;
    if (rx0 < R_VIEW_X) rx0 = R_VIEW_X;
    if (rx1 > R_VIEW_X2) rx1 = R_VIEW_X2;
    if (rx0 < rx1) {
        RectFill(rp, rx0, sy - 3, rx1, sy - 2);
        RectFill(rp, rx0, sy + 2, rx1, sy + 3);
    }
    /* Cockpit blister on top. */
    int dome = hw >> 2;
    if (dome < 2) dome = 2;
    int dx0 = sx - dome, dx1 = sx + dome;
    if (dx0 < R_VIEW_X) dx0 = R_VIEW_X;
    if (dx1 > R_VIEW_X2) dx1 = R_VIEW_X2;
    if (dx0 < dx1) {
        SetAPen(rp, glass);
        RectFill(rp, dx0, sy - 5, dx1, sy - 4);
    }
}

static void draw_bullet_dot(struct RastPort *rp, int sx, int sy,
                            UBYTE owner)
{
    if (sx < R_VIEW_X + 1 || sx > R_VIEW_X2 - 1) return;
    if (sy < R_VIEW_Y + 1 || sy > R_VIEW_Y2 - 1) return;
    UBYTE pen = (owner == BO_PLAYER) ? PAL_MISC_BASE + 5   /* laser green */
                                     : PAL_MISC_BASE + 3;  /* enemy red */
    SetAPen(rp, pen);
    RectFill(rp, sx - 1, sy - 1, sx + 1, sy + 1);
}

static void draw_pilot_sprite(struct RastPort *rp, int sx, int sy,
                              int size, UBYTE in_range, ULONG tick)
{
    /* Recognisable little figure — round-ish head + wider body + two
     * dot arms. Scales with distance. When in landing range, the
     * whole sprite flashes bright yellow so the player knows they
     * can commit. */
    if (size < 3)  size = 3;
    if (size > 20) size = 20;
    int head_r = size / 3;   if (head_r < 1) head_r = 1;
    int body_h = size;
    int body_hw = (size >> 2) + 1;
    int y0 = sy - body_h - head_r * 2;
    int y1 = sy;
    if (sx - head_r < R_VIEW_X)   return;
    if (sx + head_r > R_VIEW_X2)  return;
    if (y1 < R_VIEW_Y || y0 > R_VIEW_Y2) return;

    UBYTE suit_pen  = PAL_MISC_BASE + 6;   /* white */
    UBYTE head_pen  = PAL_MISC_BASE + 4;   /* yellow */
    if (in_range && (tick & 4)) {
        /* Blink to bright accent while in range. */
        suit_pen = PAL_MISC_BASE + 7;
        head_pen = PAL_MISC_BASE + 5;      /* laser green */
    }

    /* Body: wider rect from below head to feet */
    int bx0 = sx - body_hw;
    int bx1 = sx + body_hw;
    int by0 = y0 + head_r * 2;
    if (by0 < R_VIEW_Y)  by0 = R_VIEW_Y;
    int by1 = y1;
    if (by1 > R_VIEW_Y2) by1 = R_VIEW_Y2;
    if (bx0 < R_VIEW_X)  bx0 = R_VIEW_X;
    if (bx1 > R_VIEW_X2) bx1 = R_VIEW_X2;
    if (by0 <= by1) {
        SetAPen(rp, suit_pen);
        RectFill(rp, bx0, by0, bx1, by1);
    }
    /* Head: square-ish above body */
    int hy0 = y0;
    if (hy0 < R_VIEW_Y)  hy0 = R_VIEW_Y;
    int hy1 = y0 + head_r * 2 - 1;
    if (hy1 > R_VIEW_Y2) hy1 = R_VIEW_Y2;
    if (hy0 <= hy1) {
        SetAPen(rp, head_pen);
        RectFill(rp, sx - head_r, hy0, sx + head_r, hy1);
    }
    /* Two arm dots waving */
    SetAPen(rp, suit_pen);
    int arm_y = y0 + head_r * 2 + (body_h >> 2);
    if (arm_y >= R_VIEW_Y && arm_y <= R_VIEW_Y2) {
        int lx = sx - body_hw - 2;
        int rx = sx + body_hw + 2;
        if (lx >= R_VIEW_X)  RectFill(rp, lx, arm_y - 1, lx, arm_y);
        if (rx <= R_VIEW_X2) RectFill(rp, rx, arm_y - 1, rx, arm_y);
    }
}

void Renderer::draw_sprites(struct RastPort *rp, const GameState &gs,
                            const Combat &combat, const PilotList &pilots)
{
    /* Share the projection constant with draw_terrain — using a
     * different PROJ here would place sprites at the wrong scale
     * and altitude relative to the terrain silhouette. */
    const LONG PROJ = R_PROJ;
    LONG cam_x = FX16_TOINT(gs.ship.x);
    LONG cam_z = FX16_TOINT(gs.ship.z);
    LONG cam_y = gs.ship.y;
    LONG horizon_y = R_HORIZON_Y
                   + (-(gs.ship.pitch * 40) / SHIP_PITCH_MAX);
    if (horizon_y < R_VIEW_Y) horizon_y = R_VIEW_Y;
    if (horizon_y > R_VIEW_Y2) horizon_y = R_VIEW_Y2;

    /* Saucers. Size scales inverse to distance. */
    for (LONG i = 0; i < combat.saucer_count(); i++) {
        const Saucer &s = combat.saucer(i);
        if (s.state == SS_INACTIVE) continue;
        int sx, sy; LONG sz;
        if (!project(s.x, s.y, s.z, cam_x, cam_y, cam_z, gs.ship.yaw,
                     horizon_y, PROJ, &sx, &sy, &sz)) continue;
        /* Saucer projected size — grows dramatically as it closes.
         * Clamped so a very close one doesn't consume the viewport. */
        int size = (int)(8000 / (sz + 40));
        if (size < 6) size = 6;
        UBYTE dying = (s.state == SS_DYING) ? 1 : 0;
        draw_saucer(rp, sx, sy, size, dying);
    }

    /* Pilots on the ground — figures that scale with distance so the
     * player can visually spot rescue targets in addition to the
     * radar arrow. Flashes green/white when within LAND_RADIUS to
     * signal "press L now". */
    for (LONG i = 0; i < pilots.count(); i++) {
        const Pilot &p = pilots[i];
        if (p.state != PILOT_ACTIVE) continue;
        int sx, sy; LONG sz;
        if (!project(p.x, p.y, p.z, cam_x, cam_y, cam_z, gs.ship.yaw,
                     horizon_y, PROJ, &sx, &sy, &sz)) continue;
        /* Bigger constant so close pilots read as a person. */
        int size = (int)(2400 / (sz + 40));
        LONG dx = p.x - cam_x, dz = p.z - cam_z;
        LONG d2 = dx * dx + dz * dz;
        UBYTE in_range = (d2 < (LONG)LAND_RADIUS * (LONG)LAND_RADIUS) ? 1 : 0;
        draw_pilot_sprite(rp, sx, sy, size, in_range, gs.tick);
    }

    /* Bullets — draw last so they sit on top of everything. */
    for (LONG i = 0; i < combat.bullet_count(); i++) {
        const Bullet &b = combat.bullet(i);
        if (b.owner == BO_INACTIVE) continue;
        int sx, sy; LONG sz;
        if (!project(b.x, b.y, b.z, cam_x, cam_y, cam_z, gs.ship.yaw,
                     horizon_y, PROJ, &sx, &sy, &sz)) continue;
        draw_bullet_dot(rp, sx, sy, b.owner);
    }
}

/* Title screen — briefing before the first mission and after each
 * WIN/LOSE reset (via TITLE-mode entry from a fresh game boot). */
static void draw_title_screen(struct RastPort *rp, const GameState &gs)
{
    /* Solid dark sky backdrop. */
    SetAPen(rp, PAL_SKY_BASE + 2);
    RectFill(rp, R_VIEW_X, R_VIEW_Y, R_VIEW_X2, R_VIEW_Y2);

    /* Big title text — Amiga topaz is 8x8 so we hand-position with
     * some spacing. */
    SetDrMd(rp, JAM1);
    SetAPen(rp, PAL_TERRAIN_BASE + 7 * 8);  /* sunny highlight yellow */
    Move(rp, R_VIEW_X + 60, R_VIEW_Y + 30);
    Text(rp, (STRPTR)" F R A C T A L U S ", 19);

    SetAPen(rp, PAL_HUD_BASE + 12);
    Move(rp, R_VIEW_X + 92, R_VIEW_Y + 46);
    Text(rp, (STRPTR)"planet rescue", 13);

    /* Briefing */
    SetAPen(rp, PAL_MISC_BASE + 6);         /* pilot-suit white */
    int y = R_VIEW_Y + 70;
    Move(rp, R_VIEW_X + 24, y); Text(rp, (STRPTR)"Downed pilots need extraction.", 30); y += 10;
    Move(rp, R_VIEW_X + 24, y); Text(rp, (STRPTR)"Some are Jaggis in disguise.",   28); y += 10;
    Move(rp, R_VIEW_X + 24, y); Text(rp, (STRPTR)"Rescue 5 of 12 to complete.",     27); y += 14;

    SetAPen(rp, PAL_HUD_BASE + 15);
#ifdef AMIGA3DO   /* 3DO port: pad labels */
    Move(rp, R_VIEW_X + 24, y); Text(rp, (STRPTR)"D-PAD fly    A      fire", 24); y += 10;
#else
    Move(rp, R_VIEW_X + 24, y); Text(rp, (STRPTR)"WASD  fly    SPACE  fire", 24); y += 10;
#endif
#ifdef AMIGA3DO   /* 3DO port: pad labels */
    Move(rp, R_VIEW_X + 24, y); Text(rp, (STRPTR)"B     land   L/R    pitch",  25); y += 16;
#else
    Move(rp, R_VIEW_X + 24, y); Text(rp, (STRPTR)"L     land   Q/Z    pitch",  25); y += 16;
#endif

    /* Blinking "PRESS SPACE" prompt at the bottom. */
    if (((gs.state_timer >> 3) & 1) == 0) {
        SetAPen(rp, PAL_MISC_BASE + 4);     /* bright yellow */
        Move(rp, R_VIEW_X + 68, R_VIEW_Y2 - 10);
#ifdef AMIGA3DO   /* 3DO port: pad labels */
        Text(rp, (STRPTR)"  PRESS A TO LAUNCH  ", 21);
#else
        Text(rp, (STRPTR)"PRESS SPACE TO LAUNCH", 21);
#endif
    }
}

/* Full-viewport end-of-mission screen (WIN or LOSE). */
static void draw_end_screen(struct RastPort *rp, const GameState &gs)
{
    /* Solid backdrop over the viewport — no terrain behind. */
    UBYTE bg = (gs.mode == GM_WIN) ? PAL_TERRAIN_BASE + 3   /* deep green */
                                   : PAL_MISC_BASE + 2;     /* dark red */
    SetAPen(rp, bg);
    RectFill(rp, R_VIEW_X, R_VIEW_Y, R_VIEW_X2, R_VIEW_Y2);

    /* Central banner — sized for title + 4 stat lines + restart hint */
    int by  = R_HORIZON_Y - 40;
    int by2 = R_HORIZON_Y + 50;
    SetAPen(rp, 0);
    RectFill(rp, R_VIEW_X + 10, by, R_VIEW_X2 - 10, by2);
    SetAPen(rp, PAL_HUD_BASE + 14);
    Move(rp, R_VIEW_X + 10, by);       Draw(rp, R_VIEW_X2 - 10, by);
    Draw(rp, R_VIEW_X2 - 10, by2);     Draw(rp, R_VIEW_X + 10, by2);
    Draw(rp, R_VIEW_X + 10, by);

    SetDrMd(rp, JAM1);
    SetAPen(rp, (gs.mode == GM_WIN) ? PAL_HUD_BASE + 15
                                     : PAL_MISC_BASE + 4);
    int tx = R_VIEW_X + 40;
    if (gs.mode == GM_WIN) {
        Move(rp, tx + 20, by + 20); Text(rp, (STRPTR)"MISSION COMPLETE", 16);
    } else {
        /* Distinguish shield failure vs fuel exhaustion by which
         * gauge is zero. */
        if (gs.fuel <= 0) {
            Move(rp, tx + 24, by + 20); Text(rp, (STRPTR)"OUT OF FUEL", 11);
        } else {
            Move(rp, tx + 8, by + 20);  Text(rp, (STRPTR)"SHIELDS DESTROYED", 17);
        }
    }
    /* Stats — rescued, jaggis encountered (shield damage), score */
    SetAPen(rp, PAL_HUD_BASE + 12);
    char buf[40];
    sprintf(buf, "RESCUED %ld / %ld",
            (long)gs.pilots_rescued, (long)MISSION_WIN_PILOTS);
    Move(rp, tx + 20, by + 32); Text(rp, (STRPTR)buf, strlen(buf));
    SetAPen(rp, PAL_MISC_BASE + 3);
    sprintf(buf, "JAGGIS SPRUNG %ld", (long)gs.pilots_lost);
    Move(rp, tx + 20, by + 44); Text(rp, (STRPTR)buf, strlen(buf));
    SetAPen(rp, PAL_HUD_BASE + 12);
    sprintf(buf, "SHIELD LEFT %ld", (long)gs.shield);
    Move(rp, tx + 20, by + 56); Text(rp, (STRPTR)buf, strlen(buf));
    SetAPen(rp, PAL_HUD_BASE + 15);
    sprintf(buf, "SCORE %05ld", (long)gs.score);
    Move(rp, tx + 20, by + 68); Text(rp, (STRPTR)buf, strlen(buf));

    /* Restart hint — blinks every ~30 frames so it draws attention. */
    if (((gs.state_timer >> 3) & 1) == 0) {
        SetAPen(rp, PAL_HUD_BASE + 15);
#ifdef AMIGA3DO   /* 3DO port: pad labels */
        Move(rp, tx + 8, by + 82); Text(rp, (STRPTR)"  P = NEW MISSION   ", 20);
#else
        Move(rp, tx + 8, by + 82); Text(rp, (STRPTR)"RETURN = NEW MISSION", 20);
#endif
    }
}

/* -------------------------------------------------------------
 * Full-body jaggi slamming the cockpit glass.
 *
 * The jaggi is a lumpy green thing with a wide torso, dome head
 * with two red eyes and a fanged mouth, and two arms extending
 * outward that pound rhythmically on the "glass". Cracks radiate
 * from each fist during the strike phase.
 *
 * Animation:
 *   BUILD   0..N/3     alien grows from small centre out
 *   STRIKE  N/3..2N/3  full size, arms slam up/down each frame
 *   FADE    2N/3..N    alien recedes, cracks fade
 * ------------------------------------------------------------- */
static void draw_v_crack(struct RastPort *rp, int x, int y,
                         int dir, int len, UBYTE pen)
{
    /* Radiating impact crack — three lines from (x,y) fanning out
     * in the direction of `dir` (-1 = leftward, +1 = rightward). */
    SetAPen(rp, pen);
    Move(rp, x, y); Draw(rp, x + dir * len, y - len);
    Move(rp, x, y); Draw(rp, x + dir * len, y);
    Move(rp, x, y); Draw(rp, x + dir * len, y + len);
}

static void draw_jaggi_slam(struct RastPort *rp, const GameState &gs)
{
    int prog = JUMPSCARE_FRAMES - (int)gs.state_timer;
    if (prog < 0) prog = 0;
    if (prog > JUMPSCARE_FRAMES) prog = JUMPSCARE_FRAMES;

    int build_end  = JUMPSCARE_FRAMES / 3;
    int strike_end = (JUMPSCARE_FRAMES * 2) / 3;

    /* Scale ramps up in BUILD, holds at STRIKE, ramps down in FADE. */
    int scale;
    if (prog <= build_end) {
        scale = 40 + (prog * 200) / (build_end + 1);
    } else if (prog <= strike_end) {
        scale = 240;
    } else {
        int fade = prog - strike_end;
        int fade_len = JUMPSCARE_FRAMES - strike_end + 1;
        scale = 240 - (fade * 220) / fade_len;
    }
    if (scale < 20) scale = 20;

    /* Screen shake — biggest on impact frames of STRIKE, decays. */
    int shake = 6;
    if (prog > strike_end) shake = 6 - (prog - strike_end);
    if (shake < 0) shake = 0;
    int sx_off = (int)((gs.tick * 7 + prog * 13) & 7) - 4;
    int sy_off = (int)((gs.tick * 11 + prog * 5) & 7) - 4;
    sx_off = (sx_off * shake) / 4;
    sy_off = (sy_off * shake) / 4;

    /* Full-viewport red backdrop, pulses between two shades. */
    SetAPen(rp, (UBYTE)((prog & 1) ? PAL_MISC_BASE + 3
                                    : PAL_MISC_BASE + 2));
    RectFill(rp, R_VIEW_X, R_VIEW_Y, R_VIEW_X2, R_VIEW_Y2);

    /* Anchor. cy is a bit above centre so the arms have room to slam
     * near the viewport edges. */
    int cx = R_VIEW_X + R_VIEW_W / 2 + sx_off;
    int cy = R_VIEW_Y + R_VIEW_H * 5 / 12 + sy_off;

    /* Slam offset — arms pulse UP on even STRIKE frames, DOWN on
     * odd, creating a percussive rhythm. Zero outside STRIKE. */
    int slam = 0;
    if (prog >= build_end && prog <= strike_end)
        slam = (prog & 1) ? 6 : -6;

    /* Palette pens. */
    UBYTE body_pen  = (UBYTE)(PAL_TERRAIN_BASE + 3 * 8);   /* dark green */
    UBYTE hi_pen    = (UBYTE)(PAL_TERRAIN_BASE + 5 * 8);   /* mid-bright */
    UBYTE head_pen  = (UBYTE)(PAL_TERRAIN_BASE + 4 * 8);   /* mid green */
    UBYTE eye_pen   = (UBYTE)(PAL_MISC_BASE + 3);          /* red */
    UBYTE eye_flash = (UBYTE)(PAL_MISC_BASE + 4);          /* yellow */
    UBYTE tooth_pen = (UBYTE)(PAL_MISC_BASE + 7);          /* off-white */
    UBYTE claw_pen  = (UBYTE)(PAL_MISC_BASE + 7);
    UBYTE crack_pen = (UBYTE)(PAL_MISC_BASE + 1);          /* light grey */

    /* --- Torso (rounded via stacked rects) --- */
    int tw = (60 * scale) / 200;      /* half-width */
    int th = (50 * scale) / 200;      /* half-height */
    SetAPen(rp, body_pen);
    /* Wide middle band + thinner top/bottom for a slightly ovoid look. */
    int by0 = cy;
    int by1 = cy + th;
    if (by0 < R_VIEW_Y)  by0 = R_VIEW_Y;
    if (by1 > R_VIEW_Y2) by1 = R_VIEW_Y2;
    int bx0 = cx - tw, bx1 = cx + tw;
    if (bx0 < R_VIEW_X)  bx0 = R_VIEW_X;
    if (bx1 > R_VIEW_X2) bx1 = R_VIEW_X2;
    if (bx0 < bx1 && by0 < by1) RectFill(rp, bx0, by0, bx1, by1);
    /* Highlight strip down the centre — vertebra look. */
    SetAPen(rp, hi_pen);
    int spinew = 4;
    if (cx - spinew >= R_VIEW_X && cx + spinew <= R_VIEW_X2 && by0 < by1)
        RectFill(rp, cx - spinew, by0, cx + spinew, by1);

    /* --- Arms + fists slamming the glass ---
     * Each arm is a horizontal rectangle from torso edge out to the
     * viewport edge, ending in a fist that overlaps the "glass". */
    int arm_h = (18 * scale) / 200; if (arm_h < 3) arm_h = 3;
    int arm_y = cy + (th / 4) + slam;
    /* Left arm — from torso left edge out to viewport left. */
    SetAPen(rp, body_pen);
    int lax1 = cx - tw;
    int lax0 = R_VIEW_X + 4;
    if (lax0 < lax1) {
        int ay0 = arm_y - arm_h / 2;
        int ay1 = arm_y + arm_h / 2;
        if (ay0 < R_VIEW_Y)  ay0 = R_VIEW_Y;
        if (ay1 > R_VIEW_Y2) ay1 = R_VIEW_Y2;
        if (ay0 < ay1) RectFill(rp, lax0, ay0, lax1, ay1);
    }
    /* Right arm. */
    int rax0 = cx + tw;
    int rax1 = R_VIEW_X2 - 4;
    if (rax1 > rax0) {
        int ay0 = arm_y - arm_h / 2;
        int ay1 = arm_y + arm_h / 2;
        if (ay0 < R_VIEW_Y)  ay0 = R_VIEW_Y;
        if (ay1 > R_VIEW_Y2) ay1 = R_VIEW_Y2;
        if (ay0 < ay1) RectFill(rp, rax0, ay0, rax1, ay1);
    }

    /* Fists — bigger blob at each arm's end. */
    SetAPen(rp, hi_pen);
    int fist = (22 * scale) / 200; if (fist < 5) fist = 5;
    /* Left fist */
    int lfx0 = R_VIEW_X + 2;
    int lfx1 = lfx0 + fist;
    int lfy0 = arm_y - fist / 2;
    int lfy1 = arm_y + fist / 2;
    if (lfy0 < R_VIEW_Y)  lfy0 = R_VIEW_Y;
    if (lfy1 > R_VIEW_Y2) lfy1 = R_VIEW_Y2;
    if (lfx1 > R_VIEW_X2) lfx1 = R_VIEW_X2;
    if (lfx0 < lfx1 && lfy0 < lfy1) RectFill(rp, lfx0, lfy0, lfx1, lfy1);
    /* Right fist */
    int rfx1 = R_VIEW_X2 - 2;
    int rfx0 = rfx1 - fist;
    int rfy0 = arm_y - fist / 2;
    int rfy1 = arm_y + fist / 2;
    if (rfy0 < R_VIEW_Y)  rfy0 = R_VIEW_Y;
    if (rfy1 > R_VIEW_Y2) rfy1 = R_VIEW_Y2;
    if (rfx0 < R_VIEW_X)  rfx0 = R_VIEW_X;
    if (rfx0 < rfx1 && rfy0 < rfy1) RectFill(rp, rfx0, rfy0, rfx1, rfy1);

    /* Claws — 3 small pointy bits protruding forward from each fist. */
    if (prog >= build_end / 2) {
        SetAPen(rp, claw_pen);
        int cw = fist / 4; if (cw < 2) cw = 2;
        int ch = fist / 3; if (ch < 3) ch = 3;
        for (int k = 0; k < 3; k++) {
            int cy0 = arm_y - fist / 2 + k * (fist / 3);
            int cy1 = cy0 + ch;
            if (cy0 < R_VIEW_Y || cy1 > R_VIEW_Y2) continue;
            /* Left claws point right (into cockpit) */
            int lcx0 = lfx1;
            int lcx1 = lfx1 + cw;
            if (lcx1 <= R_VIEW_X2) RectFill(rp, lcx0, cy0, lcx1, cy1);
            /* Right claws point left */
            int rcx1 = rfx0;
            int rcx0 = rcx1 - cw;
            if (rcx0 >= R_VIEW_X) RectFill(rp, rcx0, cy0, rcx1, cy1);
        }
    }

    /* --- Glass cracks radiating from each fist during STRIKE --- */
    if (prog >= build_end && prog <= strike_end + 3) {
        int cl = 20 + shake * 2;
        draw_v_crack(rp, lfx1, arm_y, +1, cl, crack_pen);
        draw_v_crack(rp, rfx0, arm_y, -1, cl, crack_pen);
    }

    /* --- Head sitting on top of torso --- */
    int hw = (40 * scale) / 200;
    int hh = (32 * scale) / 200;
    int hy1 = cy - 2;
    int hy0 = hy1 - hh * 2;
    int hx0 = cx - hw, hx1 = cx + hw;
    if (hx0 < R_VIEW_X)  hx0 = R_VIEW_X;
    if (hx1 > R_VIEW_X2) hx1 = R_VIEW_X2;
    if (hy0 < R_VIEW_Y)  hy0 = R_VIEW_Y;
    if (hy1 > R_VIEW_Y2) hy1 = R_VIEW_Y2;
    SetAPen(rp, head_pen);
    if (hx0 < hx1 && hy0 < hy1) RectFill(rp, hx0, hy0, hx1, hy1);

    /* --- Eyes — glowing red, pulse yellow on STRIKE beats --- */
    if (hw > 4 && hh > 2) {
        int ew = (14 * scale) / 200; if (ew < 3) ew = 3;
        int eh = (10 * scale) / 200; if (eh < 3) eh = 3;
        int ey_mid = hy0 + (hy1 - hy0) / 3;
        SetAPen(rp, (prog >= build_end && (prog & 1)) ? eye_flash : eye_pen);
        int lex0 = cx - (hw * 2) / 3, lex1 = lex0 + ew;
        int rex1 = cx + (hw * 2) / 3, rex0 = rex1 - ew;
        int ey0 = ey_mid - eh / 2, ey1 = ey_mid + eh / 2;
        if (lex0 >= R_VIEW_X && lex1 <= R_VIEW_X2 && ey0 >= R_VIEW_Y && ey1 <= R_VIEW_Y2)
            RectFill(rp, lex0, ey0, lex1, ey1);
        if (rex0 >= R_VIEW_X && rex1 <= R_VIEW_X2 && ey0 >= R_VIEW_Y && ey1 <= R_VIEW_Y2)
            RectFill(rp, rex0, ey0, rex1, ey1);
    }

    /* --- Mouth: dark cavity + teeth. Opens up during STRIKE. --- */
    if (hw > 6) {
        int mw = (hw * 3) / 4;
        int mh = ((6 + (slam > 0 ? slam : 0)) * scale) / 200;
        if (mh < 3) mh = 3;
        int my0 = hy1 - mh - 2;
        int my1 = hy1 - 2;
        if (my0 >= R_VIEW_Y && my1 <= R_VIEW_Y2) {
            SetAPen(rp, PAL_MISC_BASE + 0);   /* dark grey = mouth cavity */
            RectFill(rp, cx - mw, my0, cx + mw, my1);
            /* Teeth: alternating up/down triangles rendered as narrow rects. */
            SetAPen(rp, tooth_pen);
            int nteeth = 6;
            int space = (mw * 2) / nteeth;
            for (int i = 0; i < nteeth; i++) {
                int tx0 = cx - mw + i * space;
                int tx1 = tx0 + space / 2;
                if (tx1 > R_VIEW_X2) break;
                if (i & 1) {
                    RectFill(rp, tx0, my0, tx1, my0 + mh / 2);
                } else {
                    RectFill(rp, tx0, my1 - mh / 2, tx1, my1);
                }
            }
        }
    }

    /* --- "JAGGI!!" text at bottom, only during STRIKE + FADE --- */
    if (prog >= build_end) {
        SetAPen(rp, PAL_HUD_BASE + 15);
        SetDrMd(rp, JAM1);
        int text_y = R_VIEW_Y2 - 12;
        Move(rp, cx - 32, text_y);
        Text(rp, (STRPTR)"JAGGI!!", 7);
    }
}

/* Overlays drawn on top of the terrain during the rescue sequence. */
void Renderer::draw_overlay(struct RastPort *rp, const GameState &gs)
{
    /* Title screen and end screens win over any in-progress rescue. */
    if (gs.mode == GM_TITLE) { draw_title_screen(rp, gs); return; }
    /* GM_ATTRACT renders like GM_PLAYING (in-flight view, no overlay).
     * Without this gate the "mode != PLAYING" fall-through drew the
     * end-screen box during a demo — which showed "SHIELDS DESTROYED"
     * even with shield=1000 because end_screen defaults to the LOSE
     * variant for any non-WIN mode. */
    if (gs.mode != GM_PLAYING && gs.mode != GM_ATTRACT) {
        draw_end_screen(rp, gs); return;
    }

    if (gs.rescue_state == RS_FLYING) return;

    /* Semi-opaque banner across the middle of the viewport. */
    int by = R_HORIZON_Y - 12;
    int by2 = R_HORIZON_Y + 20;
    SetAPen(rp, PAL_COCKPIT_BASE + 1);
    RectFill(rp, R_VIEW_X + 20, by, R_VIEW_X2 - 20, by2);
    SetAPen(rp, PAL_HUD_BASE + 14);
    Move(rp, R_VIEW_X + 20, by); Draw(rp, R_VIEW_X2 - 20, by);
    Draw(rp, R_VIEW_X2 - 20, by2); Draw(rp, R_VIEW_X + 20, by2);
    Draw(rp, R_VIEW_X + 20, by);

    SetAPen(rp, PAL_HUD_BASE + 15);
    SetDrMd(rp, JAM1);
    int tx = R_VIEW_X + 40;
    int ty = by + 20;
    switch (gs.rescue_state) {
    case RS_LANDING:
        Move(rp, tx, ty); Text(rp, (STRPTR)"LANDING...", 10);
        break;
    case RS_AIRLOCK:
        Move(rp, tx, ty); Text(rp, (STRPTR)"AIRLOCK CYCLING", 15);
        break;
    case RS_REVEAL:
        SetAPen(rp, PAL_HUD_BASE + 12);
        Move(rp, tx, ty); Text(rp, (STRPTR)"PILOT ABOARD!", 13);
        break;
    case RS_TAKEOFF:
        Move(rp, tx, ty); Text(rp, (STRPTR)"LIFTING OFF", 11);
        break;
    default: break;
    }

    if (gs.rescue_state == RS_JUMPSCARE)
        draw_jaggi_slam(rp, gs);
}

void Renderer::flip()
{
    /* Wait for the blitter to settle before swapping so the display
     * doesn't chase a half-written bitplane. Render time is currently
     * far longer than a vblank so the previously-displayed buffer is
     * guaranteed safe to reuse — no safe-message dance needed. */
#ifdef AMIGA3DO
    gfx_swap();             /* 3DO port: the layer swaps and paces */
    first_flip = 0;
#else
#ifdef __PPC__
    /* PPC fill_strip8 batches consecutive same-pen strips; drain the
     * pending batch before we swap so the last drawn column actually
     * lands on the buffer we're about to display. */
    ppc_batch_flush();
#endif
    WaitBlit();
    if (ChangeScreenBuffer(screen, sbuf[cur_buf])) {
        WaitTOF();
        cur_buf ^= 1;      /* next frame draws to what is now off-screen */
        first_flip = 0;
    } else {
        WaitTOF();         /* rejected — try again next frame */
    }

    /* Frame-rate cap intentionally NOT applied here.
     *
     * We tried Delay(2) — even though CLAUDE.md's warning was
     * specifically about Delay(1), the tighter cap wedges the game
     * on OS4 too (confirmed: game freezes on the frame after the
     * first Delay(2) call; no DSI logged but the main loop stops
     * ticking). WaitTOF() on OS4/RTG returns immediately so a
     * WaitTOF loop can't pace either. DateStamp cap was defeated
     * by finer-than-20ms tick resolution on OS4.
     *
     * Result: PPC runs at 150-200 fps and gameplay is fast.
     * Follow-up: timer.device IORequest with an explicit 40 ms
     * TR_ADDREQUEST is the reliable path — schedule that. */
#endif
}

void Renderer::render(const GameState &gs, const Terrain &world,
                      const PilotList &pilots, const Combat &combat)
{
#ifdef AMIGA3DO
    struct RastPort *rp = gfx_back();
#else
    struct RastPort *rp = &rp_buf[cur_buf];
    rp->BitMap = sbuf[cur_buf]->sb_BitMap;
#endif

    /* Per-section ab_perf_* calls removed — each one round-tripped a
     * bridge message inside the render loop, which on PPC (RectFill
     * fallback) pushed frame time from render+draw into render+draw+bridge
     * and pinned FPS to ~1. Re-add locally when profiling. */
    if (g_bench_mask & BENCH_SKY) {
        SetAPen(rp, 0);
        RectFill(rp, 0, 0, R_SCREEN_W - 1, R_SCREEN_H - 1);
    } else {
        draw_sky(rp);
    }

    if (gs.rescue_state == RS_FLYING) {
        if (g_bench_mask & BENCH_TERRAIN) {
            SetAPen(rp, (UBYTE)(PAL_TERRAIN_BASE + 4 * 8));
            RectFill(rp, R_VIEW_X, R_HORIZON_Y, R_VIEW_X2, R_VIEW_Y2);
        } else {
            draw_terrain(rp, gs, world);
        }

        if (!(g_bench_mask & BENCH_SPRITES))
            draw_sprites(rp, gs, combat, pilots);
    } else {
        SetAPen(rp, (UBYTE)(PAL_TERRAIN_BASE + 7));
        if (R_HORIZON_Y + 1 <= R_VIEW_Y2)
            RectFill(rp, R_VIEW_X, R_HORIZON_Y + 1,
                         R_VIEW_X2, R_VIEW_Y2);
    }

    draw_overlay(rp, gs);
    if (!(g_bench_mask & BENCH_COCKPIT)) draw_cockpit(rp, gs);
    if (!(g_bench_mask & BENCH_HUD))     draw_hud(rp, gs, pilots);

    if (!(g_bench_mask & BENCH_FLIP)) flip();
    else                              WaitTOF();
}
