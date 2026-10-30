/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

/*
 * Miner Meteor renderer - 3DO port of render.cpp.
 *
 * Same picture as the Amiga version (tile grid from the baked 16x16
 * chunky tile sheet, guardians, player, HUD strip), drawn differently
 * because the ARM60 has no blitter or cache:
 *
 *  - Tiles are copied straight into the layer's 8-bit pen buffer
 *    (gfx_pixels8) instead of one RectFill per same-pen run.
 *  - The pen buffer persists between frames, so each grid cell
 *    remembers which sheet tile it holds and is only redrawn when that
 *    changes (a key taken, the exit blinking, a collapsing floor) or
 *    when a sprite was drawn over it the frame before.
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

/* 16-entry palette in (r,g,b) 8-bit-per-channel. */
static const UBYTE PALETTE[16][3] = {
    /* 0  bg          */ {   0,   0,   0 },
    /* 1  white       */ { 240, 240, 240 },
    /* 2  brick       */ { 168,  48,  48 },
    /* 3  brown       */ { 128,  88,  40 },
    /* 4  cyan        */ {  64, 208, 224 },
    /* 5  magenta     */ { 224,  80, 192 },
    /* 6  yellow      */ { 240, 224,  64 },
    /* 7  green       */ {  64, 200,  64 },
    /* 8  dark grey   */ {  72,  72,  80 },
    /* 9  bright red  */ { 240,  64,  64 },
    /* 10 orange      */ { 240, 160,  64 },
    /* 11 blue        */ {  80, 128, 240 },
    /* 12 grey        */ { 128, 128, 128 },
    /* 13 light green */ { 168, 224, 128 },
    /* 14 pink        */ { 240, 168, 200 },
    /* 15 dim         */ {  40,  40,  40 },
};

/* what each grid cell of the pen buffer holds (row * 32 + col of the
 * sheet, CELL_DIMMED for the darkened collapse tile) */
#define CELL_DIRTY   0xFFFF
#define CELL_DIMMED  0x1000
static UWORD cell_have[GRID_H][GRID_W];

/* Chunky staging buffer for tiles that need per-pixel recolouring
 * (collapse tile dim-down). */
static UBYTE scratch_tile[TILE_H][TILE_W];

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
 * did) to x,y, clipped to the play area. */
static void blit_tile(LONG x, LONG y, const UBYTE *src)
{
    LONG x0 = x, y0 = y, x1 = x + TILE_W, y1 = y + TILE_H, row, w;
    UBYTE *d;
    const UBYTE *s;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PLAY_W) x1 = PLAY_W;
    if (y1 > PLAY_H) y1 = PLAY_H;
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

/* A sprite covers these cells: redraw them next frame. */
static void dirty_cells(LONG x, LONG y)
{
    LONG c0 = x / TILE_W, c1 = (x + TILE_W - 1) / TILE_W;
    LONG r0 = y / TILE_H, r1 = (y + TILE_H - 1) / TILE_H;
    LONG r, c;
    if (x < 0) c0 = 0;
    if (y < 0) r0 = 0;
    for (r = r0; r <= r1 && r < GRID_H; r++)
        for (c = c0; c <= c1 && c < GRID_W; c++)
            cell_have[r][c] = CELL_DIRTY;
}

/* Sheet layout - must match tools/bake_tilesheet.py. */
enum {
    SR_TERRAIN     = 0,
    SR_DECOR       = 1,
    SR_PLAYER_R    = 2,
    SR_PLAYER_L    = 3,
    SR_PLAYER_POSE = 4,
    SR_GUARDIAN_A  = 5,
    SR_GUARDIAN_B  = 6,
    SR_GUARDIAN_C  = 7
};
enum {
    SC_EMPTY       = 0,
    SC_SOLID_A     = 1,
    SC_SOLID_B     = 2,
    SC_COLLAPSE    = 3,
    SC_CONV_L      = 4,
    SC_CONV_R      = 5,
    SC_HAZARD      = 6,
    SC_KEY         = 7,
    SC_EXIT_LOCKED = 0,
    SC_EXIT_OPEN   = 1,
    SC_POSE_JUMP   = 0,
    SC_POSE_FALL   = 1,
    SC_POSE_DEATH  = 2,
    SC_POSE_CLIMB  = 3
};

#define SHEET(r, c) (&tilesheet_data[r][c][0][0])

/* The sheet tile render.cpp's draw_tile() picks, as row * 32 + col. */
static UWORD tile_choice(LONG col, LONG row, const TileState *t, ULONG tick,
                         LONG keys_remaining)
{
    switch (t->kind) {
        case T_EMPTY:    return SR_TERRAIN * 32 + SC_EMPTY;
        case T_SOLID:    return SR_TERRAIN * 32 + (((col + row) & 1) ? SC_SOLID_A : SC_SOLID_B);
        case T_KEY:      return SR_TERRAIN * 32 + SC_KEY;
        case T_EXIT:
            if (keys_remaining == 0)
                return SR_DECOR * 32 + (((tick / 6) & 1) ? SC_EXIT_OPEN : SC_EXIT_LOCKED);
            return SR_DECOR * 32 + SC_EXIT_LOCKED;
        case T_COLLAPSE:
            if (t->timer > 0 && t->timer <= COLLAPSE_FRAMES / 2)
                return CELL_DIMMED | (SR_TERRAIN * 32 + SC_COLLAPSE);
            return SR_TERRAIN * 32 + SC_COLLAPSE;
        case T_CONV_L:   return SR_TERRAIN * 32 + SC_CONV_L;
        case T_CONV_R:   return SR_TERRAIN * 32 + SC_CONV_R;
        case T_HAZARD:   return SR_TERRAIN * 32 + SC_HAZARD;
        default:         return SR_TERRAIN * 32 + SC_EMPTY;
    }
}

static void draw_cell(LONG col, LONG row, UWORD t)
{
    LONG x = col * TILE_W, y = row * TILE_H;
    if (t & CELL_DIMMED) {
        /* Recolour to darker on the second half of the collapse timer -
         * swap palette index 3 (brown) with 15 (dim). */
        LONG px, py;
        memcpy(scratch_tile, SHEET(SR_TERRAIN, SC_COLLAPSE), sizeof(scratch_tile));
        for (py = 0; py < TILE_H; py++)
            for (px = 0; px < TILE_W; px++)
                if (scratch_tile[py][px] == 3) scratch_tile[py][px] = 15;
        blit_tile(x, y, &scratch_tile[0][0]);
    } else {
        blit_tile(x, y, SHEET(t >> 5, t & 31));
    }
}

static void draw_player(const Player *p)
{
    LONG row, col;
    if (!p->on_ground) {
        row = SR_PLAYER_POSE;
        col = (p->vy < 0) ? SC_POSE_JUMP : SC_POSE_FALL;
    } else {
        row = p->facing ? SR_PLAYER_R : SR_PLAYER_L;
        col = (LONG)p->frame & 7;
    }
    blit_tile(p->x, p->y, SHEET(row, col));
    dirty_cells(p->x, p->y);
}

static void draw_guardian(const Guardian *G)
{
    LONG row, col;
    if (!G->alive) return;
    switch (G->colour) {
        case 5: row = SR_GUARDIAN_B; break;
        case 6: row = SR_GUARDIAN_C; break;
        default: row = SR_GUARDIAN_A; break;
    }
    col = ((LONG)G->frame >> 2) & 7;   /* animation phase */
    blit_tile(G->x, G->y, SHEET(row, col));
    dirty_cells(G->x, G->y);
}

static void draw_hud(const GameState *gs)
{
    char buf[64];
    LONG bar_w = 120, bar_x = 100, fill;
    UBYTE pen;

    fill_rect(0, PLAY_H, SCREEN_W, HUD_H, 15);

    sprintf(buf, "SCORE %06ld", (long)gs->score);
    render_text(4, PLAY_H + 12, 1, buf);

    /* Air bar. */
    fill_rect(bar_x, PLAY_H + 4, bar_w, 8, 8);
    fill = (gs->air * bar_w) / AIR_MAX;
    if (fill < 0) fill = 0;
    if (fill > bar_w) fill = bar_w;
    pen = (gs->air < AIR_MAX / 4) ? 9 : 7;
    fill_rect(bar_x, PLAY_H + 4, fill, 8, pen);

    sprintf(buf, "LV%ld K%ld/%ld L%ld",
        (long)(gs->level_index + 1),
        (long)(gs->keys_total - gs->keys_remaining), (long)gs->keys_total,
        (long)gs->lives);
    render_text(230, PLAY_H + 12, 1, buf);
}

void render_frame(const GameState *gs)
{
    LONG row, col, i;

    for (row = 0; row < GRID_H; row++) {
        for (col = 0; col < GRID_W; col++) {
            UWORD t = tile_choice(col, row, &gs->grid[row][col], gs->tick,
                                  gs->keys_remaining);
            if (cell_have[row][col] != t) {
                draw_cell(col, row, t);
                cell_have[row][col] = t;
            }
        }
    }

    for (i = 0; i < gs->num_guards; i++)
        draw_guardian(&gs->guards[i]);

    /* Player last so it sits on top. */
    draw_player(&gs->player);

    draw_hud(gs);
}

void render_title(const GameState *gs)
{
    invalidate_cells();
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    fill_rect(0, 40, SCREEN_W, 40, 11);
    render_text(60, 66, 1, "MINER  METEOR");

    render_text(48, 120, 6, "A CLEAN-ROOM PLATFORMER");
    render_text(56, 138, 1, "INSPIRED BY 8-BIT CLASSICS");

    render_text(48, 176, 7, "LEFT / RIGHT :  MOVE");
    render_text(48, 190, 7, "A, B OR UP   :  JUMP");
    render_text(48, 204, 7, "X            :  QUIT TO MENU");
    render_text(48, 218, 7, "C            :  DEMO OF LEVEL 1");   /* port addition */

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
        render_text(96, 100, 0, "ALL CAVERNS CLEARED!");
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
