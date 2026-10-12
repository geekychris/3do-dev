// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * DOT_CHASE - Rendering (dirty-rect approach)
 *
 * Static maze drawn once per buffer at level start.
 * Per-frame: only erase/redraw sprite areas (~20 RectFill/frame).
 */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/graphics.h>
#include <string.h>
#include "draw.h"
#include "game.h"

/* Clipped RectFill */
static void safe_rect(struct RastPort *rp, WORD x1, WORD y1, WORD x2, WORD y2)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    if (y2 >= SCREEN_H) y2 = SCREEN_H - 1;
    if (x1 > x2 || y1 > y2) return;
    RectFill(rp, x1, y1, x2, y2);
}

/* Previous sprite positions for erasing */
static WORD prev_hero_x, prev_hero_y;
static WORD prev_spider_x[NUM_SPIDERS], prev_spider_y[NUM_SPIDERS];
static WORD prev_bonus_active;
static WORD prev_bonus_x, prev_bonus_y;
static LONG prev_score;
static WORD prev_lives;
static WORD maze_dirty;  /* flag to force full maze redraw */

/* Prior visibility of transient overlays (popup / READY / LEVEL CLEAR).
 * When one goes inactive we need to restore the maze pixels beneath it,
 * so mark both back- and front-buffer full redraws pending. */
static WORD prev_popup_active;
static WORD prev_ready_visible;
static WORD prev_level_done_visible;

/* 5x7 font */
static const UBYTE font_data[36][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

static void draw_char(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                       char ch, WORD scale)
{
    int idx, row, col;
    if (ch >= '0' && ch <= '9') idx = ch - '0';
    else if (ch >= 'A' && ch <= 'Z') idx = ch - 'A' + 10;
    else if (ch >= 'a' && ch <= 'z') idx = ch - 'a' + 10;
    else return;

    SetAPen(rp, color);
    for (row = 0; row < 7; row++) {
        UBYTE bits = font_data[idx][row];
        col = 0;
        while (col < 5) {
            if (bits & (0x10 >> col)) {
                WORD start = col;
                while (col < 5 && (bits & (0x10 >> col))) col++;
                safe_rect(rp, x + start * scale, y + row * scale,
                          x + col * scale - 1, y + row * scale + scale - 1);
            } else {
                col++;
            }
        }
    }
}

static void draw_text(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                      const char *text, WORD scale)
{
    while (*text) {
        if (*text == ' ') x += 6 * scale;
        else { draw_char(rp, x, y, color, *text, scale); x += 6 * scale; }
        text++;
    }
}

static void draw_text_centered(struct RastPort *rp, WORD y, UBYTE color,
                                const char *text, WORD scale)
{
    int len = 0;
    const char *p = text;
    WORD w;
    while (*p++) len++;
    w = len * 6 * scale;
    draw_text(rp, (SCREEN_W - w) / 2, y, color, text, scale);
}

static void format_number(char *buf, LONG val, int field_width)
{
    char tmp[12];
    int i = 0, j;
    LONG v = val;
    if (field_width <= 0) { if (buf) buf[0] = '\0'; return; }
    if (v < 0) v = 0;
    if (v == 0) { tmp[i++] = '0'; }
    else {
        while (v > 0 && i < (int)sizeof(tmp)) {
            tmp[i++] = '0' + (char)(v % 10);
            v /= 10;
        }
    }
    if (i > field_width) {
        /* Value doesn't fit -- emit an overflow marker instead of
         * writing negative buffer indices or truncating digits from the
         * high end. */
        for (j = 0; j < field_width; j++) buf[j] = '*';
        buf[field_width] = '\0';
        return;
    }
    for (j = 0; j < field_width - i; j++) buf[j] = ' ';
    for (j = 0; j < i; j++) buf[field_width - 1 - j] = tmp[j];
    buf[field_width] = '\0';
}

void draw_set_dirty(void)
{
    maze_dirty = 2; /* both buffers need redraw */
}

/* --- Sprites are drawn facing right in (u, v) and rotated to dir --- */

static void rot_rect(struct RastPort *rp, WORD cx, WORD cy, WORD dir,
                     WORD u1, WORD v1, WORD u2, WORD v2)
{
    WORD x1, y1, x2, y2, t;
    switch (dir) {
    case DIR_LEFT: x1 = -u1; x2 = -u2; y1 = v1; y2 = v2; break;
    case DIR_DOWN: x1 = v1;  x2 = v2;  y1 = u1; y2 = u2; break;
    case DIR_UP:   x1 = v1;  x2 = v2;  y1 = -u1; y2 = -u2; break;
    default:       x1 = u1;  x2 = u2;  y1 = v1; y2 = v2; break;
    }
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    safe_rect(rp, cx + x1, cy + y1, cx + x2, cy + y2);
}

/* --- Maze tiles --- */

static void draw_flower(struct RastPort *rp, WORD sx, WORD sy, WORD bright)
{
    SetAPen(rp, bright ? COL_WHITE : COL_PLUM);
    safe_rect(rp, sx + 3, sy + 1, sx + 4, sy + 6);
    safe_rect(rp, sx + 1, sy + 3, sx + 6, sy + 4);
    SetAPen(rp, COL_GOLD);
    safe_rect(rp, sx + 3, sy + 3, sx + 4, sy + 4);
}

static void draw_seed(struct RastPort *rp, WORD sx, WORD sy)
{
    SetAPen(rp, COL_SEED);
    safe_rect(rp, sx + 3, sy + 4, sx + 4, sy + 4);
    safe_rect(rp, sx + 4, sy + 3, sx + 4, sy + 3);
}

static void draw_tile(struct RastPort *rp, GameState *gs, WORD tx, WORD ty)
{
    WORD sx = MAZE_OX + tx * TILE_SIZE;
    WORD sy = MAZE_OY + ty * TILE_SIZE;
    UBYTE tile = gs->maze[ty][tx];

    if (tile == T_WALL) {
        /* hedge: leafy speckles, a shadow where it meets open ground */
        WORD h = (WORD)((tx * 7 + ty * 13) & 3);
        SetAPen(rp, COL_HEDGE);
        safe_rect(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
        SetAPen(rp, COL_HEDGE_LT);
        safe_rect(rp, sx + 1 + h, sy + 1, sx + 1 + h, sy + 1);
        safe_rect(rp, sx + 5 - h, sy + 4, sx + 6 - h, sy + 4);
        SetAPen(rp, COL_HEDGE_DK);
        safe_rect(rp, sx + 2 + (h ^ 1), sy + 3 + (h & 1), sx + 2 + (h ^ 1), sy + 3 + (h & 1));
        if (ty + 1 < MAZE_H && gs->maze[ty + 1][tx] != T_WALL)
            safe_rect(rp, sx, sy + TILE_SIZE - 1, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
        return;
    }

    SetAPen(rp, COL_BG);
    safe_rect(rp, sx, sy, sx + TILE_SIZE - 1, sy + TILE_SIZE - 1);
    if (tile == T_DOT) {
        draw_seed(rp, sx, sy);
    } else if (tile == T_POWER) {
        draw_flower(rp, sx, sy, 1);
    } else if (tile == T_SPIDER_DOOR) {
        /* a little wooden gate */
        SetAPen(rp, COL_SP_BROWN);
        safe_rect(rp, sx, sy + 2, sx + 7, sy + 2);
        safe_rect(rp, sx, sy + 5, sx + 7, sy + 5);
        safe_rect(rp, sx + 1, sy + 1, sx + 1, sy + 6);
        safe_rect(rp, sx + 6, sy + 1, sx + 6, sy + 6);
    }
}

/* --- Full maze draw (called once per buffer at level start) --- */

static void draw_hud_labels(struct RastPort *rp)
{
    draw_text(rp, 2, 24, COL_LEAF, "SCORE", 1);
    draw_text(rp, MAZE_OX + MAZE_W * TILE_SIZE + 6, 24, COL_LEAF, "BEST", 1);
    draw_text(rp, 2, 188, COL_LEAF, "BUGS", 1);
}

void draw_maze_full(struct RastPort *rp, GameState *gs)
{
    WORD ty, tx;

    SetRast(rp, COL_BG);
    for (ty = 0; ty < MAZE_H; ty++)
        for (tx = 0; tx < MAZE_W; tx++)
            draw_tile(rp, gs, tx, ty);
    draw_hud_labels(rp);
}

/* --- Restore tiles under a sprite area --- */

static void restore_area(struct RastPort *rp, GameState *gs, WORD px, WORD py)
{
    /* px, py are pixel coords in maze space. Restore the 2x2 tile area */
    WORD tx_start, ty_start, tx_end, ty_end, tx, ty;

    /* Expand area slightly for sprite overhang */
    tx_start = (px - 1) / TILE_SIZE;
    ty_start = (py - 1) / TILE_SIZE;
    tx_end = (px + TILE_SIZE + 1) / TILE_SIZE;
    ty_end = (py + TILE_SIZE + 1) / TILE_SIZE;

    if (tx_start < 0) tx_start = 0;
    if (ty_start < 0) ty_start = 0;
    if (tx_end >= MAZE_W) tx_end = MAZE_W - 1;
    if (ty_end >= MAZE_H) ty_end = MAZE_H - 1;

    for (ty = ty_start; ty <= ty_end; ty++)
        for (tx = tx_start; tx <= tx_end; tx++)
            draw_tile(rp, gs, tx, ty);
}

/* --- The ladybug --- */

static void draw_ladybug(struct RastPort *rp, WORD cx, WORD cy, WORD dir, WORD step)
{
    SetAPen(rp, COL_BLACK);
    if (step) {
        rot_rect(rp, cx, cy, dir, -1, -4, -1, -4);
        rot_rect(rp, cx, cy, dir,  1,  4,  1,  4);
    } else {
        rot_rect(rp, cx, cy, dir,  1, -4,  1, -4);
        rot_rect(rp, cx, cy, dir, -1,  4, -1,  4);
    }
    SetAPen(rp, COL_HERO);
    rot_rect(rp, cx, cy, dir, -3, -2, 2, 2);
    rot_rect(rp, cx, cy, dir, -2, -3, 1, 3);
    SetAPen(rp, COL_BLACK);
    rot_rect(rp, cx, cy, dir, 2, -1, 3, 1);      /* head */
    rot_rect(rp, cx, cy, dir, -3, 0, 1, 0);      /* wing split */
    rot_rect(rp, cx, cy, dir, 0, -2, 0, -2);     /* spots */
    rot_rect(rp, cx, cy, dir, 0, 2, 0, 2);
    rot_rect(rp, cx, cy, dir, -2, -2, -2, -2);
    rot_rect(rp, cx, cy, dir, -2, 2, -2, 2);
    SetAPen(rp, COL_WHITE);
    rot_rect(rp, cx, cy, dir, 3, -1, 3, -1);     /* eyes */
    rot_rect(rp, cx, cy, dir, 3, 1, 3, 1);
}

static void draw_hero(struct RastPort *rp, GameState *gs)
{
    WORD dir = gs->hero.dir;
    if (dir == DIR_NONE) dir = DIR_LEFT;
    draw_ladybug(rp, MAZE_OX + gs->hero.pixel_x + 3, MAZE_OY + gs->hero.pixel_y + 3,
                 dir, gs->hero.leg_step);
}

/* --- Spiders --- */

static const UBYTE spider_colors[NUM_SPIDERS] = {
    COL_SP_PURPLE, COL_SP_STEEL, COL_SP_BROWN, COL_SP_BONE
};

static void draw_spider(struct RastPort *rp, Spider *g, WORD id, GameState *gs)
{
    WORD cx = MAZE_OX + g->pixel_x + 3;
    WORD cy = MAZE_OY + g->pixel_y + 3;
    WORD dir = g->dir;
    UBYTE body_col, eye_col;
    WORD k;

    if (dir == DIR_NONE) dir = DIR_LEFT;

    if (g->state == SPIDER_EATEN) {
        /* a curled-up husk scuttling back to the nest */
        SetAPen(rp, COL_GRAY);
        safe_rect(rp, cx - 1, cy - 1, cx + 1, cy + 1);
        SetAPen(rp, COL_GOLD);
        rot_rect(rp, cx, cy, dir, 1, 0, 1, 0);
        return;
    }

    if (g->state == SPIDER_FRIGHT) {
        if (g->fright_timer < FRIGHT_BLINK && (g->fright_timer >> 3) & 1) {
            body_col = COL_WHITE; eye_col = COL_PLUM;
        } else {
            body_col = COL_GRAY; eye_col = COL_WHITE;
        }
    } else {
        body_col = spider_colors[id]; eye_col = COL_GOLD;
    }

    /* eight legs, scuttling */
    SetAPen(rp, body_col);
    if ((gs->frame >> 2) & 1) {
        for (k = -3; k <= 3; k += 2) {
            safe_rect(rp, cx - 3, cy + k, cx - 3, cy + k);
            safe_rect(rp, cx + 3, cy + k, cx + 3, cy + k);
        }
    } else {
        for (k = -2; k <= 2; k += 2) {
            safe_rect(rp, cx - 3, cy + k, cx - 3, cy + k);
            safe_rect(rp, cx + 3, cy + k, cx + 3, cy + k);
        }
        safe_rect(rp, cx - 2, cy + 3, cx - 2, cy + 3);
        safe_rect(rp, cx + 2, cy + 3, cx + 2, cy + 3);
    }
    /* round body */
    safe_rect(rp, cx - 2, cy - 1, cx + 2, cy + 1);
    safe_rect(rp, cx - 1, cy - 2, cx + 1, cy + 2);
    /* glowing eyes on the side it faces */
    SetAPen(rp, eye_col);
    rot_rect(rp, cx, cy, dir, 1, -1, 1, -1);
    rot_rect(rp, cx, cy, dir, 1, 1, 1, 1);
}

/* --- Garden bonuses --- */

static void bonus_px(struct RastPort *rp, WORD px, WORD py, UBYTE col,
                     WORD x1, WORD y1, WORD x2, WORD y2)
{
    SetAPen(rp, col);
    safe_rect(rp, px + x1, py + y1, px + x2, py + y2);
}

static void draw_bonus_icon(struct RastPort *rp, WORD type, WORD px, WORD py)
{
    switch (type) {
    case BONUS_BERRY:
        bonus_px(rp, px, py, COL_PLUM, 1, 3, 2, 4);
        bonus_px(rp, px, py, COL_PLUM, 4, 3, 5, 4);
        bonus_px(rp, px, py, COL_PLUM, 2, 5, 4, 6);
        bonus_px(rp, px, py, COL_LEAF, 3, 0, 4, 2);
        break;
    case BONUS_MUSHROOM:
        bonus_px(rp, px, py, COL_HERO, 2, 1, 5, 1);
        bonus_px(rp, px, py, COL_HERO, 1, 2, 6, 3);
        bonus_px(rp, px, py, COL_WHITE, 2, 2, 2, 2);
        bonus_px(rp, px, py, COL_WHITE, 5, 2, 5, 2);
        bonus_px(rp, px, py, COL_SEED, 3, 4, 4, 6);
        break;
    case BONUS_CARROT:
        bonus_px(rp, px, py, COL_LEAF, 2, 0, 5, 1);
        bonus_px(rp, px, py, COL_GOLD, 2, 2, 5, 3);
        bonus_px(rp, px, py, COL_GOLD, 3, 4, 4, 5);
        bonus_px(rp, px, py, COL_GOLD, 3, 6, 3, 6);
        break;
    case BONUS_PEAR:
        bonus_px(rp, px, py, COL_SP_BROWN, 4, 0, 4, 1);
        bonus_px(rp, px, py, COL_LEAF, 3, 2, 4, 3);
        bonus_px(rp, px, py, COL_LEAF, 2, 4, 5, 6);
        break;
    case BONUS_ACORN:
        bonus_px(rp, px, py, COL_SP_BROWN, 3, 0, 3, 1);
        bonus_px(rp, px, py, COL_SP_BROWN, 1, 2, 6, 3);
        bonus_px(rp, px, py, COL_SEED, 2, 4, 5, 5);
        bonus_px(rp, px, py, COL_SEED, 3, 6, 4, 6);
        break;
    case BONUS_HONEY:
        bonus_px(rp, px, py, COL_GOLD, 2, 1, 5, 2);
        bonus_px(rp, px, py, COL_SP_BROWN, 1, 3, 6, 6);
        bonus_px(rp, px, py, COL_GOLD, 2, 4, 5, 4);
        break;
    default: /* BONUS_SUNFLOWER */
        bonus_px(rp, px, py, COL_GOLD, 2, 0, 5, 5);
        bonus_px(rp, px, py, COL_GOLD, 1, 1, 6, 4);
        bonus_px(rp, px, py, COL_SP_BROWN, 3, 2, 4, 3);
        bonus_px(rp, px, py, COL_LEAF, 3, 6, 4, 7);
        break;
    }
}

static void draw_bonus(struct RastPort *rp, GameState *gs)
{
    draw_bonus_icon(rp, gs->bonus_type, MAZE_OX + gs->bonus_x, MAZE_OY + gs->bonus_y);
}

/* --- Losing a life: the ladybug flips over and curls up --- */
static void draw_dying(struct RastPort *rp, GameState *gs)
{
    WORD cx = MAZE_OX + gs->hero.pixel_x + 3;
    WORD cy = MAZE_OY + gs->hero.pixel_y + 3;
    WORD phase = (DYING_TIME - gs->state_timer) / 8;

    if (phase < 3) {
        draw_ladybug(rp, cx, cy, (gs->hero.dir + phase) & 3, phase & 1);
    } else if (phase < 7) {
        WORD s = 6 - phase;
        if (s < 0) s = 0;
        SetAPen(rp, COL_BLACK);
        safe_rect(rp, cx - s - 1, cy - s - 1, cx + s + 1, cy + s + 1);
        SetAPen(rp, phase & 1 ? COL_GRAY : COL_HERO);
        safe_rect(rp, cx - s, cy - s, cx + s, cy + s);
    }
}

/* --- HUD (partial update): side columns beside the maze --- */
#define HUD_RX  (MAZE_OX + MAZE_W * TILE_SIZE + 4)

static void draw_hud_score(struct RastPort *rp, GameState *gs)
{
    char buf[12];
    SetAPen(rp, COL_BG);
    safe_rect(rp, 0, 34, MAZE_OX - 1, 42);
    format_number(buf, gs->score, 7);
    draw_text(rp, 2, 34, COL_WHITE, buf, 1);

    SetAPen(rp, COL_BG);
    safe_rect(rp, HUD_RX, 34, SCREEN_W - 1, 42);
    format_number(buf, gs->hiscore, 7);
    draw_text(rp, HUD_RX, 34, COL_WHITE, buf, 1);
}

static void draw_hud_lives(struct RastPort *rp, GameState *gs)
{
    WORD i;
    SetAPen(rp, COL_BG);
    safe_rect(rp, 0, 198, MAZE_OX - 1, 236);

    for (i = 0; i < gs->lives - 1 && i < 6; i++)
        draw_ladybug(rp, 8 + (i % 3) * 13, 204 + (i / 3) * 12, DIR_UP, 0);

    SetAPen(rp, COL_BG);
    safe_rect(rp, HUD_RX, 198, SCREEN_W - 1, 236);
    draw_bonus_icon(rp, (gs->level - 1) % MAX_BONUS_TYPE, HUD_RX + 16, 204);
}

/* --- Main per-frame draw --- */

void draw_game(struct RastPort *rp, GameState *gs)
{
    WORD i;

    /* If maze is dirty, do full redraw */
    if (maze_dirty > 0) {
        draw_maze_full(rp, gs);
        draw_hud_score(rp, gs);
        draw_hud_lives(rp, gs);
        maze_dirty--;

        /* Init previous positions */
        prev_hero_x = gs->hero.pixel_x;
        prev_hero_y = gs->hero.pixel_y;
        for (i = 0; i < NUM_SPIDERS; i++) {
            prev_spider_x[i] = gs->spiders[i].pixel_x;
            prev_spider_y[i] = gs->spiders[i].pixel_y;
        }
        prev_score = gs->score;
        prev_lives = gs->lives;
        prev_bonus_active = gs->bonus_active;
    }

    /* Step 1: Erase previous sprite positions by restoring tiles */
    restore_area(rp, gs, prev_hero_x, prev_hero_y);
    for (i = 0; i < NUM_SPIDERS; i++) {
        restore_area(rp, gs, prev_spider_x[i], prev_spider_y[i]);
    }
    if (prev_bonus_active && !gs->bonus_active) {
        restore_area(rp, gs, prev_bonus_x, prev_bonus_y);
    }

    /* Step 2: the flowers pulse between white and plum petals */
    {
        WORD ty, tx;
        for (ty = 0; ty < MAZE_H; ty++) {
            for (tx = 0; tx < MAZE_W; tx++) {
                if (gs->maze[ty][tx] == T_POWER)
                    draw_flower(rp, MAZE_OX + tx * TILE_SIZE, MAZE_OY + ty * TILE_SIZE,
                                (WORD)((gs->frame >> 3) & 1));
            }
        }
    }

    /* Step 3: Draw sprites at new positions */
    if (gs->bonus_active) draw_bonus(rp, gs);

    if (gs->state == STATE_DYING) {
        draw_dying(rp, gs);
    } else {
        draw_hero(rp, gs);
    }

    for (i = 0; i < NUM_SPIDERS; i++) {
        draw_spider(rp, &gs->spiders[i], i, gs);
    }

    /* Step 4: Update HUD only when changed */
    if (gs->score != prev_score) {
        draw_hud_score(rp, gs);
    }
    if (gs->lives != prev_lives) {
        draw_hud_lives(rp, gs);
    }

    /* Popup text */
    {
        WORD popup_now = gs->popup_active ? 1 : 0;
        WORD ready_now = (gs->state == STATE_READY) ? 1 : 0;
        WORD level_done_now = (gs->state == STATE_LEVEL_DONE &&
                               ((gs->state_timer >> 3) & 1)) ? 1 : 0;

        if (popup_now) {
            char buf[8];
            format_number(buf, gs->popup_score, 4);
            draw_text(rp, MAZE_OX + gs->popup_x - 8, MAZE_OY + gs->popup_y, COL_GOLD, buf, 1);
        }

        if (ready_now) {
            draw_text_centered(rp, MAZE_OY + 17 * TILE_SIZE, COL_GOLD, "GO BUG GO", 1);
        }

        if (level_done_now) {
            draw_text_centered(rp, MAZE_OY + 17 * TILE_SIZE, COL_WHITE, "GARDEN CLEAR", 1);
        }

        /* If any overlay just turned off, its pixels sit on top of the
         * maze in both buffers. Mark maze dirty so the next two frames
         * repaint the underlying maze cleanly. */
        if ((prev_popup_active && !popup_now) ||
            (prev_ready_visible && !ready_now) ||
            (prev_level_done_visible && !level_done_now)) {
            draw_set_dirty();
        }

        prev_popup_active = popup_now;
        prev_ready_visible = ready_now;
        prev_level_done_visible = level_done_now;
    }

    /* Save current positions for next frame */
    prev_hero_x = gs->hero.pixel_x;
    prev_hero_y = gs->hero.pixel_y;
    for (i = 0; i < NUM_SPIDERS; i++) {
        prev_spider_x[i] = gs->spiders[i].pixel_x;
        prev_spider_y[i] = gs->spiders[i].pixel_y;
    }
    prev_score = gs->score;
    prev_lives = gs->lives;
    prev_bonus_active = gs->bonus_active;
    if (gs->bonus_active) {
        prev_bonus_x = gs->bonus_x;
        prev_bonus_y = gs->bonus_y;
    }
}

void draw_title(struct RastPort *rp, GameState *gs)
{
    static const char *names[NUM_SPIDERS] = { "WEBSTER", "SILK", "TANGLE", "NIBS" };
    static const char *habits[NUM_SPIDERS] = {
        "HUNTS YOU", "CUTS YOU OFF", "TEAMS UP", "LOSES INTEREST"
    };
    WORD i;

    SetRast(rp, COL_BG);
    draw_text_centered(rp, 16, COL_HERO, "DOT CHASE", 3);
    draw_text_centered(rp, 42, COL_LEAF, "A LADYBUG IN THE HEDGE MAZE", 1);

    {
        /* the ladybug trundles along a row of seeds */
        WORD anim_x = (gs->title_blink * 2) % (SCREEN_W + 40) - 20;
        SetAPen(rp, COL_HEDGE);
        safe_rect(rp, 0, 72, SCREEN_W - 1, 73);
        for (i = 0; i < 12; i++) {
            WORD dx = anim_x + 16 + i * 14;
            if (dx > 0 && dx < SCREEN_W - 8) draw_seed(rp, dx, 61);
        }
        draw_ladybug(rp, anim_x + 3, 65, DIR_RIGHT, (WORD)((gs->title_blink >> 2) & 1));
    }

    draw_text(rp, 70, 86, COL_WHITE, "THE SPIDERS", 1);

    {
        Spider tmp;
        memset(&tmp, 0, sizeof(tmp));
        tmp.pixel_x = 52 - MAZE_OX;
        tmp.dir = DIR_RIGHT;
        tmp.state = SPIDER_CHASE;
        for (i = 0; i < NUM_SPIDERS; i++) {
            WORD y = 102 + i * 16;
            tmp.pixel_y = y - MAZE_OY;
            draw_spider(rp, &tmp, i, gs);
            draw_text(rp, 70, y + 1, spider_colors[i], names[i], 1);
            draw_text(rp, 160, y + 1, COL_GRAY, habits[i], 1);
        }
    }

    draw_seed(rp, 94, 172);
    draw_text(rp, 110, 174, COL_WHITE, "10 PTS", 1);
    draw_flower(rp, 94, 186, 1);
    draw_text(rp, 110, 188, COL_WHITE, "50 PTS  SCARES SPIDERS", 1);

    draw_text_centered(rp, 210, COL_GRAY, "DPAD MOVES", 1);
    if ((gs->title_blink >> 3) & 1) {
        draw_text_centered(rp, 228, COL_WHITE, "PRESS A TO START", 1);
    }
    draw_text_centered(rp, SCREEN_H - 10, COL_GRAY, "X QUITS", 1);
}

void draw_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[12];
    SetRast(rp, COL_BG);
    draw_text_centered(rp, 60, COL_HERO, "GAME OVER", 3);
    draw_text_centered(rp, 110, COL_WHITE, "SCORE", 2);
    format_number(buf, gs->score, 7);
    draw_text_centered(rp, 135, COL_GOLD, buf, 2);
    draw_text_centered(rp, 170, COL_GRAY, "GARDEN", 1);
    format_number(buf, (LONG)gs->level, 3);
    draw_text_centered(rp, 182, COL_WHITE, buf, 1);
    if (gs->score >= gs->hiscore) draw_text_centered(rp, 200, COL_LEAF, "NEW BEST SCORE", 1);
    if ((gs->title_blink >> 3) & 1) draw_text_centered(rp, 225, COL_WHITE, "PRESS A", 1);
}
