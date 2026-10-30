/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

/*
 * emberkeep renderer - 3DO port of render.cpp.
 *
 * Same picture as the Amiga version: the tile grid, enemies and player
 * from the baked 16x16 chunky tile sheet, then the lantern mask (four
 * black bands around the light square plus its chopped corners), then
 * the HUD strip. What changed is how it gets there, because the ARM60
 * has no blitter or cache:
 *
 *  - Tiles are copied straight into the layer's 8-bit pen buffer
 *    (gfx_pixels8) instead of one RectFill per same-pen run.
 *  - The pen buffer persists between frames, so each grid cell
 *    remembers what it holds. Cells wholly outside the light only need
 *    to be black, and are filled once when they go dark. Cells that the
 *    light square touches are redrawn every frame (tile, then sprites
 *    and mask clipped to those cells).
 *
 * Palette: the 16 colours of render.cpp, loaded with gfx_set_rgb24.
 */

#include "render.h"
#include "game.h"
#include "tilesheet_data.h"
#include "amiga3do.h"

#include <graphics/rastport.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

static struct RastPort *rp;
static UBYTE           *fb;

static const UBYTE PALETTE[16][3] = {
    /* 0  bg           */ {   0,   0,   0 },
    /* 1  white        */ { 240, 240, 240 },
    /* 2  brick        */ { 168,  48,  48 },
    /* 3  brown        */ { 128,  88,  40 },
    /* 4  cyan         */ {  64, 208, 224 },
    /* 5  magenta      */ { 224,  80, 192 },
    /* 6  yellow       */ { 240, 224,  64 },
    /* 7  green        */ {  64, 200,  64 },
    /* 8  dark grey    */ {  72,  72,  80 },
    /* 9  bright red   */ { 240,  64,  64 },
    /* 10 orange       */ { 240, 160,  64 },
    /* 11 blue         */ {  80, 128, 240 },
    /* 12 grey         */ { 128, 128, 128 },
    /* 13 light green  */ { 168, 224, 128 },
    /* 14 pink         */ { 240, 168, 200 },
    /* 15 dim          */ {  40,  40,  40 },
};

/* what each grid cell of the pen buffer holds */
#define CELL_DIRTY  0xFFFF
#define CELL_BLACK  0xFFFE
static UWORD cell_have[GRID_H][GRID_W];

static void invalidate_cells(void)
{
    memset(cell_have, 0xFF, sizeof(cell_have));
}

LONG render_open(void)
{
    LONG i;
    if (!gfx_init(NULL, 0)) return 1;
    for (i = 0; i < 16; i++)
        gfx_set_rgb24((int)i, ((ULONG)PALETTE[i][0] << 16) |
                              ((ULONG)PALETTE[i][1] << 8) | PALETTE[i][2]);
    rp = gfx_back();
    /* Port fix: text in JAM1. The Amiga RastPort default is JAM2 with
     * background pen 0, so the title / end-screen captions (pen 0 on a
     * coloured bar) come out as solid black boxes there. */
    SetDrMd(rp, JAM1);
    fb = gfx_pixels8();
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    invalidate_cells();
    return 0;
}

void render_close(void)
{
    gfx_exit();
}

void render_flip(void)
{
    gfx_swap();
}

static void fill_rect(LONG x, LONG y, LONG w, LONG h, UBYTE pen)
{
    if (w <= 0 || h <= 0) return;
    SetAPen(rp, pen);
    RectFill(rp, x, y, x + w - 1, y + h - 1);
}

void render_text(LONG x, LONG y, UBYTE pen, const char *text)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)text, (LONG)strlen(text));
}

/* Copy a 16x16 tile (opaque, every pixel - as the Amiga's run fills
 * did) to x,y, clipped to [cx0,cx1) x [cy0,cy1). */
static void blit_tile_clip(LONG x, LONG y, const UBYTE *src,
                           LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
    LONG x0 = x, y0 = y, x1 = x + TILE_W, y1 = y + TILE_H, row, w;
    UBYTE *d;
    const UBYTE *s;

    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x0 >= x1 || y0 >= y1) return;
    w = x1 - x0;
    d = fb + y0 * GFX_WIDTH + x0;
    s = src + (y0 - y) * TILE_W + (x0 - x);
    if (w == TILE_W && !(((ULONG)d | (ULONG)s) & 3)) {
        for (row = y0; row < y1; row++) {
            ULONG *dl = (ULONG *)d;
            const ULONG *sl = (const ULONG *)s;
            dl[0] = sl[0]; dl[1] = sl[1]; dl[2] = sl[2]; dl[3] = sl[3];
            d += GFX_WIDTH;
            s += TILE_W;
        }
    } else {
        for (row = y0; row < y1; row++) {
            memcpy(d, s, (size_t)w);
            d += GFX_WIDTH;
            s += TILE_W;
        }
    }
}

/* Sheet layout - mirror of tools/bake_tilesheet.py's grid. */
enum {
    SR_TERRAIN     = 0,
    SR_DECOR       = 1,
    SR_PLAYER_R    = 2,
    SR_PLAYER_L    = 3,
    SR_PLAYER_POSE = 4,
    SR_ENEMY_A     = 5,
    SR_ENEMY_B     = 6,
    SR_ENEMY_C     = 7
};
enum {
    SC_FLOOR       = 0,
    SC_WALL_A      = 1,
    SC_WALL_B      = 2,
    SC_RUNE        = 3,
    SC_OIL         = 4,
    SC_HAZARD      = 5,
    SC_PORTAL_LOCKED = 0,
    SC_PORTAL_OPEN   = 1,
    SC_POSE_UP     = 0,
    SC_POSE_RIGHT  = 1,
    SC_POSE_DOWN   = 2,
    SC_POSE_LEFT   = 3
};

#define SHEET(r, c) (&tilesheet_data[r][c][0][0])

/* The sheet tile render.cpp's draw_tile() picks, as row * 32 + col. */
static UWORD tile_choice(LONG col, LONG row, UBYTE kind,
                         LONG runes_remaining, ULONG tick)
{
    switch (kind) {
        case T_FLOOR:  return SR_TERRAIN * 32 + SC_FLOOR;
        case T_WALL:   return SR_TERRAIN * 32 + (((col + row) & 1) ? SC_WALL_A : SC_WALL_B);
        case T_RUNE:   return SR_TERRAIN * 32 + SC_RUNE;
        case T_OIL:    return SR_TERRAIN * 32 + SC_OIL;
        case T_HAZARD: return SR_TERRAIN * 32 + SC_HAZARD;
        case T_PORTAL:
            if (runes_remaining == 0)
                return SR_DECOR * 32 + (((tick / 6) & 1) ? SC_PORTAL_OPEN : SC_PORTAL_LOCKED);
            return SR_DECOR * 32 + SC_PORTAL_LOCKED;
        default:       return SR_TERRAIN * 32 + SC_FLOOR;
    }
}

static void draw_player(const Player *p, LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
    LONG col;
    switch (p->facing) {
        case 0:  col = SC_POSE_UP;    break;
        case 1:  col = SC_POSE_RIGHT; break;
        case 3:  col = SC_POSE_LEFT;  break;
        default: col = SC_POSE_DOWN;  break;
    }
    blit_tile_clip(p->x, p->y, SHEET(SR_PLAYER_POSE, col), cx0, cy0, cx1, cy1);
}

static void draw_enemy(const Enemy *E, LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
    LONG row = SR_ENEMY_A, col;
    if (!E->alive) return;
    if (E->colour == 5) row = SR_ENEMY_B;
    else if (E->colour == 6) row = SR_ENEMY_C;
    col = ((LONG)E->frame >> 2) & 7;
    blit_tile_clip(E->x, E->y, SHEET(row, col), cx0, cy0, cx1, cy1);
}

/* RectFill in pen 0 of the inclusive rectangle, clipped to [cx0,cx1) x [cy0,cy1) */
static void black_clip(LONG x0, LONG y0, LONG x1, LONG y1,
                       LONG cx0, LONG cy0, LONG cx1, LONG cy1)
{
    if (x0 < cx0) x0 = cx0;
    if (y0 < cy0) y0 = cy0;
    if (x1 > cx1 - 1) x1 = cx1 - 1;
    if (y1 > cy1 - 1) y1 = cy1 - 1;
    if (x0 > x1 || y0 > y1) return;
    RectFill(rp, x0, y0, x1, y1);
}

static void draw_hud(const GameState *gs)
{
    char buf[64];
    LONG bar_w = 120, bar_x = 100, fill;
    UBYTE pen;

    fill_rect(0, PLAY_H, SCREEN_W, HUD_H, 15);

    sprintf(buf, "SCORE %06ld", (long)gs->score);
    render_text(4, PLAY_H + 12, 1, buf);

    /* Lantern fuel bar (orange -> red as it runs low). */
    fill_rect(bar_x, PLAY_H + 4, bar_w, 8, 8);
    fill = (gs->lantern * bar_w) / LANTERN_MAX;
    if (fill < 0) fill = 0;
    if (fill > bar_w) fill = bar_w;
    pen = (gs->lantern < LANTERN_MAX / 4) ? 9 : 10;
    fill_rect(bar_x, PLAY_H + 4, fill, 8, pen);

    sprintf(buf, "LV%ld R%ld/%ld L%ld",
        (long)(gs->level_index + 1),
        (long)(gs->runes_total - gs->runes_remaining),
        (long)gs->runes_total,
        (long)gs->lives);
    render_text(230, PLAY_H + 12, 1, buf);
}

void render_frame(const GameState *gs)
{
    LONG cx = gs->player.x + 8, cy = gs->player.y + 8;
    LONG r, lx, rx, ty, by, chop;
    LONG c0, c1, r0, r1, kx0, ky0, kx1, ky1;
    LONG row, col, i;

    /* The light square, as render.cpp's apply_lantern_mask() works it out. */
    r = 8 + (LIGHT_RADIUS_PX - 8) * gs->lantern / LANTERN_MAX;
    if (r < 8) r = 8;
    lx = cx - r; if (lx < 0)      lx = 0;
    rx = cx + r; if (rx > PLAY_W) rx = PLAY_W;
    ty = cy - r; if (ty < 0)      ty = 0;
    by = cy + r; if (by > PLAY_H) by = PLAY_H;

    /* the cells it touches, and their pixel rectangle */
    c0 = lx / TILE_W; c1 = (rx - 1) / TILE_W;
    r0 = ty / TILE_H; r1 = (by - 1) / TILE_H;
    if (c1 < c0) c1 = c0;
    if (r1 < r0) r1 = r0;
    kx0 = c0 * TILE_W; kx1 = (c1 + 1) * TILE_W;
    ky0 = r0 * TILE_H; ky1 = (r1 + 1) * TILE_H;

    SetAPen(rp, 0);
    for (row = 0; row < GRID_H; row++) {
        for (col = 0; col < GRID_W; col++) {
            if (row >= r0 && row <= r1 && col >= c0 && col <= c1) {
                UWORD t = tile_choice(col, row, gs->grid[row][col],
                                      gs->runes_remaining, gs->tick);
                blit_tile_clip(col * TILE_W, row * TILE_H, SHEET(t >> 5, t & 31),
                               0, 0, PLAY_W, PLAY_H);
                cell_have[row][col] = CELL_DIRTY;    /* sprites + mask go on it */
            } else if (cell_have[row][col] != CELL_BLACK) {
                RectFill(rp, col * TILE_W, row * TILE_H,
                         col * TILE_W + TILE_W - 1, row * TILE_H + TILE_H - 1);
                cell_have[row][col] = CELL_BLACK;
            }
        }
    }

    /* Sprites, then the mask, inside the lit cells only (anything
     * outside them is black on the Amiga too). */
    for (i = 0; i < gs->num_enemies; i++)
        draw_enemy(&gs->enemies[i], kx0, ky0, kx1, ky1);
    draw_player(&gs->player, kx0, ky0, kx1, ky1);

    SetAPen(rp, 0);
    if (ty > 0)      black_clip(0,  0,  PLAY_W - 1, ty - 1,     kx0, ky0, kx1, ky1);
    if (by < PLAY_H) black_clip(0,  by, PLAY_W - 1, PLAY_H - 1, kx0, ky0, kx1, ky1);
    if (lx > 0)      black_clip(0,  ty, lx - 1,     by - 1,     kx0, ky0, kx1, ky1);
    if (rx < PLAY_W) black_clip(rx, ty, PLAY_W - 1, by - 1,     kx0, ky0, kx1, ky1);

    /* Cheap "circle" - chop the 4 corners of the light square. */
    chop = r / 3;
    if (chop > 0) {
        if (ty > 0 && lx > 0)           black_clip(lx, ty, lx + chop - 1, ty + chop - 1, kx0, ky0, kx1, ky1);
        if (ty > 0 && rx < PLAY_W)      black_clip(rx - chop, ty, rx - 1, ty + chop - 1, kx0, ky0, kx1, ky1);
        if (by < PLAY_H && lx > 0)      black_clip(lx, by - chop, lx + chop - 1, by - 1, kx0, ky0, kx1, ky1);
        if (by < PLAY_H && rx < PLAY_W) black_clip(rx - chop, by - chop, rx - 1, by - 1, kx0, ky0, kx1, ky1);
    }

    draw_hud(gs);
}

void render_title(const GameState *gs)
{
    invalidate_cells();
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    fill_rect(0, 40, SCREEN_W, 40, 11);
    render_text(96, 66, 1, "EMBERKEEP");

    render_text(40, 120, 6, "A LANTERN IN THE DEEP DARK");
    render_text(56, 138, 1, "INSPIRED BY 8-BIT CLASSICS");

    render_text(56, 176, 7, "D-PAD       :  MOVE");
    render_text(56, 190, 7, "A OR P      :  BEGIN / RESTART");
    render_text(56, 204, 7, "X           :  QUIT TO MENU");
    render_text(56, 218, 7, "C           :  DEMO OF LEVEL 1");   /* port addition */

    if ((gs->tick / 15) & 1) {
        render_text(88, 232, 9, "PRESS  A  TO  BEGIN");
    }
}

void render_endscreen(const GameState *gs)
{
    char buf[64];

    invalidate_cells();
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    if (gs->mode == GM_WIN) {
        fill_rect(0, 80, SCREEN_W, 30, 7);
        render_text(80, 100, 0, "THE DARK IS BEATEN");
    } else {
        fill_rect(0, 80, SCREEN_W, 30, 9);
        render_text(112, 100, 0, "GAME OVER");
    }

    sprintf(buf, "FINAL SCORE  %06ld", (long)gs->score);
    render_text(88, 156, 1, buf);

    if (gs->state_timer == 0 && ((gs->tick / 15) & 1)) {
        render_text(88, 208, 6, "PRESS A TO RETURN");
    }
}
