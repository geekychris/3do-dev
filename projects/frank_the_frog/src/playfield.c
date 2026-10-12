// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Frank the Frog - Playfield drawing
 */

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

#include "game.h"
#include "playfield.h"
#include "lanes.h"

/* Draw a row-width filled rectangle */
static void fill_row(struct RastPort *rp, int row, UBYTE color)
{
    SetAPen(rp, (long)color);
    RectFill(rp, 0, row * TILE_H,
             SCREEN_W - 1, (row + 1) * TILE_H - 1);
}

/* Draw road lane divider dashes between two road rows */
static void draw_road_divider(struct RastPort *rp, int row)
{
    int col;
    int y_mid = row * TILE_H + TILE_H / 2;

    /* Dashed center line */
    SetAPen(rp, (long)COL_ROAD_LINE);
    for (col = 0; col < SCREEN_W; col += 24) {
        int x2 = col + 10;
        if (x2 > SCREEN_W - 1) x2 = SCREEN_W - 1;
        RectFill(rp, col, y_mid, x2, y_mid + 1);
    }
}

/* Draw the home bank: grass with reeds and a few ponds to hop into */
static void draw_home_row(struct RastPort *rp)
{
    int slot, x;
    int y0 = ROW_HOME * TILE_H;

    fill_row(rp, ROW_HOME, COL_GRASS);

    /* reeds along the bank */
    SetAPen(rp, (long)COL_FROG_DARK);
    for (x = 6; x < SCREEN_W; x += 11) {
        Move(rp, x, y0 + 14);
        Draw(rp, x + ((x & 2) ? 1 : -1), y0 + 5 + (x % 5));
    }

    /* ponds: rounded water patches in a muddy rim */
    for (slot = 0; slot < NUM_HOMES; slot++) {
        int cx = lanes_home_cx(slot);
        SetAPen(rp, (long)COL_LOG);
        RectFill(rp, cx - 14, y0 + 2, cx + 14, y0 + 14);
        SetAPen(rp, (long)COL_WATER);
        RectFill(rp, cx - 11, y0 + 3, cx + 11, y0 + 13);
        RectFill(rp, cx - 13, y0 + 5, cx + 13, y0 + 11);
    }
}

void playfield_draw(struct RastPort *rp)
{
    int row;

    /* Home bases */
    draw_home_row(rp);

    /* River lanes */
    for (row = ROW_RIVER_5; row <= ROW_RIVER_1; row++) {
        fill_row(rp, row, COL_WATER);
    }

    /* Median (safe zone) */
    fill_row(rp, ROW_MEDIAN, COL_GRASS);

    /* Road lanes */
    for (row = ROW_ROAD_5; row <= ROW_ROAD_1; row++) {
        fill_row(rp, row, COL_ROAD);
    }
    /* Dividers between road lanes (not on top/bottom edges) */
    for (row = ROW_ROAD_5; row < ROW_ROAD_1; row++) {
        draw_road_divider(rp, row);
    }

    /* Start area */
    fill_row(rp, ROW_START, COL_GRASS);

    /* HUD area */
    SetAPen(rp, (long)COL_HUD_BG);
    RectFill(rp, 0, HUD_Y, SCREEN_W - 1, SCREEN_H - 1);
}
