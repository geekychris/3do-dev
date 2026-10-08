// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - Drawing functions
 * Tile-based room rendering, character sprites, HUD, menus
 */
#include <proto/graphics.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "draw.h"

/* Color indices (matching palette in main.c) */
#define COL_BG          0   /* dark blue-black 0x001 */
#define COL_WHITE       1   /* white */
#define COL_HUNTER      2   /* orange-red (hunter suit) 0xE40 */
#define COL_HUNTER2     3   /* yellow (suit detail) 0xFC0 */
#define COL_ROCK        4   /* dark green (cavern rock) 0x141 */
#define COL_ROCK2       5   /* medium green (cavern) 0x262 */
#define COL_METAL       6   /* grey-blue (metal/citadel) 0x668 */
#define COL_METAL2      7   /* light grey 0x99A */
#define COL_LAVA        8   /* red-orange (magma lava) 0xF30 */
#define COL_LAVA2       9   /* bright orange 0xF80 */
#define COL_MISSILE     10  /* bright yellow 0xFF0 */
#define COL_ENEMY       11  /* purple (enemy) 0xA0F */
#define COL_ENEMY2      12  /* magenta 0xF0A */
#define COL_DOOR        13  /* blue (locked door) 0x06F */
#define COL_ITEM        14  /* cyan (item orb) 0x0FF */
#define COL_BEAM        15  /* bright green (beam) 0x0F0 */

/* HUD height at top of screen */
#define HUD_H 16

/* Frame counter reference (from GameState) */
static WORD g_frame = 0;

/* 5x7 bitmap font */
static const UBYTE font_5x7[96][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* # */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* $ */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* % */
    {0x08,0x14,0x14,0x08,0x15,0x12,0x0D}, /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* ( */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ) */
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, /* * */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* + */
    {0x00,0x00,0x00,0x00,0x00,0x04,0x08}, /* , */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04}, /* . */
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* / */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* 2 */
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* 3 */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    {0x00,0x00,0x04,0x00,0x00,0x04,0x00}, /* : */
    {0x00,0x00,0x04,0x00,0x00,0x04,0x08}, /* ; */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* < */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* = */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* > */
    {0x0E,0x11,0x01,0x06,0x04,0x00,0x04}, /* ? */
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* @ */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
    {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}, /* D */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}, /* G */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
    {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}, /* V */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* W */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* [ */
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, /* \ */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* ] */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, /* _ */
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, /* a */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E}, /* b */
    {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E}, /* c */
    {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F}, /* d */
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, /* e */
    {0x06,0x08,0x1C,0x08,0x08,0x08,0x08}, /* f */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, /* g */
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x11}, /* h */
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, /* i */
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, /* j */
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, /* k */
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, /* l */
    {0x00,0x00,0x1A,0x15,0x15,0x15,0x15}, /* m */
    {0x00,0x00,0x1E,0x11,0x11,0x11,0x11}, /* n */
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, /* o */
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, /* p */
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01}, /* q */
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, /* r */
    {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E}, /* s */
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, /* t */
    {0x00,0x00,0x11,0x11,0x11,0x11,0x0F}, /* u */
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, /* v */
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, /* w */
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, /* x */
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, /* y */
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, /* z */
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, /* { */
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, /* | */
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, /* } */
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00}, /* ~ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* DEL */
};

/* --- Text drawing --- */

static void draw_char(struct RastPort *rp, WORD x, WORD y, char c, WORD scale)
{
    WORD idx, row, col;
    UBYTE bits;

    if (c < 32 || c > 127) return;
    idx = c - 32;

    for (row = 0; row < 7; row++) {
        bits = font_5x7[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                if (scale == 1) {
                    WritePixel(rp, x + col, y + row);
                } else {
                    RectFill(rp, x + col * scale, y + row * scale,
                             x + col * scale + scale - 1,
                             y + row * scale + scale - 1);
                }
            }
        }
    }
}

static void draw_string(struct RastPort *rp, WORD x, WORD y,
                         const char *s, WORD scale)
{
    while (*s) {
        draw_char(rp, x, y, *s, scale);
        x += (5 + 1) * scale;
        s++;
    }
}

static WORD string_width(const char *s, WORD scale)
{
    WORD len = 0;
    while (*s) { len++; s++; }
    if (len == 0) return 0;
    return len * 6 * scale - scale;
}

void draw_text(struct RastPort *rp, WORD x, WORD y, const char *str, WORD color)
{
    SetAPen(rp, color);
    draw_string(rp, x, y, str, 1);
}

/* --- Tile drawing --- */

static void draw_tile(struct RastPort *rp, WORD tx, WORD ty, UBYTE tile)
{
    WORD x = tx * TILE_W;
    WORD y = ty * TILE_H + HUD_H;

    switch (tile) {
        case TILE_EMPTY:
            break;

        case TILE_ROCK:
            /* Filled green rock with texture dots */
            SetAPen(rp, COL_ROCK);
            RectFill(rp, x, y, x + TILE_W - 1, y + TILE_H - 1);
            /* Texture: a few lighter pixels */
            SetAPen(rp, COL_ROCK2);
            WritePixel(rp, x + 3, y + 2);
            WritePixel(rp, x + 10, y + 5);
            WritePixel(rp, x + 6, y + 11);
            WritePixel(rp, x + 13, y + 9);
            WritePixel(rp, x + 1, y + 7);
            WritePixel(rp, x + 8, y + 14);
            WritePixel(rp, x + 14, y + 2);
            WritePixel(rp, x + 5, y + 6);
            break;

        case TILE_BRICK:
            /* Brick pattern */
            SetAPen(rp, COL_ROCK);
            RectFill(rp, x, y, x + TILE_W - 1, y + TILE_H - 1);
            /* Horizontal mortar lines */
            SetAPen(rp, COL_ROCK2);
            RectFill(rp, x, y + 3, x + TILE_W - 1, y + 3);
            RectFill(rp, x, y + 7, x + TILE_W - 1, y + 7);
            RectFill(rp, x, y + 11, x + TILE_W - 1, y + 11);
            RectFill(rp, x, y + 15, x + TILE_W - 1, y + 15);
            /* Vertical mortar lines (offset every other row) */
            RectFill(rp, x + 7, y, x + 7, y + 3);
            RectFill(rp, x + 15, y + 4, x + 15, y + 7);
            RectFill(rp, x + 7, y + 8, x + 7, y + 11);
            RectFill(rp, x + 15, y + 12, x + 15, y + 15);
            break;

        case TILE_METAL:
            /* Metal panel with grid lines */
            SetAPen(rp, COL_METAL);
            RectFill(rp, x, y, x + TILE_W - 1, y + TILE_H - 1);
            /* Panel edge lines */
            SetAPen(rp, COL_METAL2);
            RectFill(rp, x, y, x + TILE_W - 1, y);
            RectFill(rp, x, y, x, y + TILE_H - 1);
            /* Inner detail */
            SetAPen(rp, COL_BG);
            RectFill(rp, x + TILE_W - 1, y, x + TILE_W - 1, y + TILE_H - 1);
            RectFill(rp, x, y + TILE_H - 1, x + TILE_W - 1, y + TILE_H - 1);
            break;

        case TILE_PLATFORM:
            /* Thin platform at top of tile */
            SetAPen(rp, COL_ROCK);
            RectFill(rp, x, y, x + TILE_W - 1, y + 2);
            SetAPen(rp, COL_ROCK2);
            RectFill(rp, x, y + 3, x + TILE_W - 1, y + 3);
            break;

        case TILE_LAVA: {
            /* Animated lava with wave effect */
            WORD wave = (g_frame / 4) & 3;
            WORD row;
            SetAPen(rp, COL_LAVA);
            RectFill(rp, x, y + 2, x + TILE_W - 1, y + TILE_H - 1);
            /* Wavy top surface */
            for (row = 0; row < TILE_W; row++) {
                WORD wy = ((row + wave) & 3) > 1 ? 0 : 1;
                SetAPen(rp, COL_LAVA2);
                WritePixel(rp, x + row, y + wy);
                WritePixel(rp, x + row, y + wy + 1);
            }
            /* Bright spots */
            SetAPen(rp, COL_LAVA2);
            WritePixel(rp, x + ((g_frame + 3) & 0xF), y + 6);
            WritePixel(rp, x + ((g_frame + 9) & 0xF), y + 10);
            WritePixel(rp, x + ((g_frame + 5) & 0xF), y + 13);
            break;
        }

        case TILE_DOOR_LOCKED:
            /* Vertical bars */
            SetAPen(rp, COL_DOOR);
            RectFill(rp, x + 2, y, x + 4, y + TILE_H - 1);
            RectFill(rp, x + 7, y, x + 9, y + TILE_H - 1);
            RectFill(rp, x + 12, y, x + 14, y + TILE_H - 1);
            /* Cross bars */
            RectFill(rp, x + 2, y + 3, x + 14, y + 4);
            RectFill(rp, x + 2, y + 11, x + 14, y + 12);
            break;

        case TILE_DOOR_OPEN:
            /* Empty (black) - passage */
            break;

        case TILE_ITEM_BLOCK: {
            /* Glowing block with pulse */
            WORD pulse = ((g_frame / 3) & 7);
            WORD bright = pulse < 4 ? pulse : 7 - pulse;
            SetAPen(rp, bright > 1 ? COL_ITEM : COL_METAL);
            RectFill(rp, x + 1, y + 1, x + TILE_W - 2, y + TILE_H - 2);
            SetAPen(rp, COL_METAL2);
            RectFill(rp, x, y, x + TILE_W - 1, y);
            RectFill(rp, x, y + TILE_H - 1, x + TILE_W - 1, y + TILE_H - 1);
            RectFill(rp, x, y, x, y + TILE_H - 1);
            RectFill(rp, x + TILE_W - 1, y, x + TILE_W - 1, y + TILE_H - 1);
            /* Inner glow */
            if (bright > 2) {
                SetAPen(rp, COL_WHITE);
                WritePixel(rp, x + 7, y + 7);
                WritePixel(rp, x + 8, y + 7);
                WritePixel(rp, x + 7, y + 8);
                WritePixel(rp, x + 8, y + 8);
            }
            break;
        }

        case TILE_SAVE_POINT: {
            /* Flashing save marker */
            WORD flash = (g_frame / 8) & 1;
            SetAPen(rp, flash ? COL_ITEM : COL_BEAM);
            RectFill(rp, x + 4, y + 2, x + 11, y + 13);
            /* 'S' indicator */
            SetAPen(rp, COL_WHITE);
            draw_char(rp, x + 5, y + 4, 'S', 1);
            break;
        }

        default:
            break;
    }
}

/* --- Hunter (player character) drawing --- */

static void draw_hunter_standing(struct RastPort *rp, WORD x, WORD y, WORD facing)
{
    /* Standing hunter: ~12 wide, 16 tall
     * Orange suit with yellow visor and arm cannon.
     * facing: 0=right, 1=left
     */
    WORD cx = x;

    /* Helmet (top) */
    SetAPen(rp, COL_HUNTER);
    RectFill(rp, cx + 2, y, cx + 9, y + 3);
    /* Visor */
    SetAPen(rp, COL_HUNTER2);
    if (facing == 0) {
        RectFill(rp, cx + 7, y + 1, cx + 9, y + 2);
    } else {
        RectFill(rp, cx + 2, y + 1, cx + 4, y + 2);
    }

    /* Body (torso) */
    SetAPen(rp, COL_HUNTER);
    RectFill(rp, cx + 1, y + 4, cx + 10, y + 9);
    /* Chest plate detail */
    SetAPen(rp, COL_HUNTER2);
    RectFill(rp, cx + 4, y + 5, cx + 7, y + 6);

    /* Arm cannon */
    SetAPen(rp, COL_HUNTER);
    if (facing == 0) {
        RectFill(rp, cx + 9, y + 5, cx + 12, y + 7);
        /* Cannon tip */
        SetAPen(rp, COL_HUNTER2);
        RectFill(rp, cx + 12, y + 5, cx + 12, y + 7);
    } else {
        RectFill(rp, cx - 1, y + 5, cx + 2, y + 7);
        SetAPen(rp, COL_HUNTER2);
        RectFill(rp, cx - 1, y + 5, cx - 1, y + 7);
    }

    /* Legs */
    SetAPen(rp, COL_HUNTER);
    RectFill(rp, cx + 2, y + 10, cx + 4, y + 14);
    RectFill(rp, cx + 7, y + 10, cx + 9, y + 14);
    /* Boots */
    SetAPen(rp, COL_HUNTER2);
    RectFill(rp, cx + 1, y + 14, cx + 5, y + 15);
    RectFill(rp, cx + 6, y + 14, cx + 10, y + 15);
}

static void draw_hunter_rolled(struct RastPort *rp, WORD x, WORD y)
{
    /* Roll form: 8x8 ball shape */
    WORD cx = x + 2;
    WORD cy = y;

    /* Outer ball */
    SetAPen(rp, COL_HUNTER);
    RectFill(rp, cx + 1, cy, cx + 6, cy);
    RectFill(rp, cx, cy + 1, cx + 7, cy + 6);
    RectFill(rp, cx + 1, cy + 7, cx + 6, cy + 7);

    /* Inner pattern (rotates with frame) */
    SetAPen(rp, COL_HUNTER2);
    {
        WORD phase = (g_frame / 2) & 3;
        switch (phase) {
            case 0:
                RectFill(rp, cx + 3, cy + 1, cx + 4, cy + 2);
                break;
            case 1:
                RectFill(rp, cx + 5, cy + 3, cx + 6, cy + 4);
                break;
            case 2:
                RectFill(rp, cx + 3, cy + 5, cx + 4, cy + 6);
                break;
            case 3:
                RectFill(rp, cx + 1, cy + 3, cx + 2, cy + 4);
                break;
        }
    }
}

static void draw_hunter(struct RastPort *rp, GameState *gs)
{
    WORD sx, sy;
    Hunter *h = &gs->hunter;

    /* Convert fixed-point to screen coords (offset by HUD height) */
    sx = (WORD)(h->x >> 16);
    sy = (WORD)(h->y >> 16) + HUD_H;

    /* Invulnerability flash: skip drawing every other frame */
    if (h->invuln_timer > 0 && (g_frame & 1)) return;

    if (h->rolled) {
        draw_hunter_rolled(rp, sx, sy);
    } else {
        draw_hunter_standing(rp, sx, sy, h->facing);
    }
}

/* --- Enemy drawing --- */

static void draw_enemy_crawler(struct RastPort *rp, WORD x, WORD y, WORD facing)
{
    /* Crawler: 12x10, purple worm-like */
    SetAPen(rp, COL_ENEMY);
    /* Body segments */
    RectFill(rp, x + 1, y + 4, x + 10, y + 8);
    /* Head */
    if (facing == 0) {
        RectFill(rp, x + 8, y + 2, x + 11, y + 6);
    } else {
        RectFill(rp, x, y + 2, x + 3, y + 6);
    }
    /* Legs */
    SetAPen(rp, COL_ENEMY2);
    RectFill(rp, x + 2, y + 9, x + 3, y + 9);
    RectFill(rp, x + 5, y + 9, x + 6, y + 9);
    RectFill(rp, x + 8, y + 9, x + 9, y + 9);
    /* Eye */
    SetAPen(rp, COL_WHITE);
    if (facing == 0) {
        WritePixel(rp, x + 10, y + 3);
    } else {
        WritePixel(rp, x + 1, y + 3);
    }
}

static void draw_enemy_flyer(struct RastPort *rp, WORD x, WORD y)
{
    /* Flyer: 10x10, winged creature */
    WORD wing = (g_frame / 3) & 1;
    SetAPen(rp, COL_ENEMY);
    /* Body */
    RectFill(rp, x + 3, y + 3, x + 6, y + 7);
    /* Wings */
    if (wing) {
        RectFill(rp, x, y + 1, x + 3, y + 3);
        RectFill(rp, x + 6, y + 1, x + 9, y + 3);
    } else {
        RectFill(rp, x, y + 4, x + 3, y + 6);
        RectFill(rp, x + 6, y + 4, x + 9, y + 6);
    }
    /* Eyes */
    SetAPen(rp, COL_ENEMY2);
    WritePixel(rp, x + 4, y + 4);
    WritePixel(rp, x + 5, y + 4);
    /* Tail */
    SetAPen(rp, COL_ENEMY);
    RectFill(rp, x + 4, y + 8, x + 5, y + 9);
}

static void draw_enemy_hopper(struct RastPort *rp, WORD x, WORD y)
{
    /* Hopper: 10x12, frog-like */
    SetAPen(rp, COL_ENEMY);
    /* Body */
    RectFill(rp, x + 2, y + 2, x + 7, y + 7);
    /* Head */
    RectFill(rp, x + 1, y, x + 8, y + 3);
    /* Eyes */
    SetAPen(rp, COL_WHITE);
    WritePixel(rp, x + 3, y + 1);
    WritePixel(rp, x + 6, y + 1);
    /* Legs */
    SetAPen(rp, COL_ENEMY2);
    RectFill(rp, x, y + 8, x + 2, y + 11);
    RectFill(rp, x + 7, y + 8, x + 9, y + 11);
    /* Feet */
    RectFill(rp, x, y + 11, x + 3, y + 11);
    RectFill(rp, x + 6, y + 11, x + 9, y + 11);
}

static void draw_enemy_turret(struct RastPort *rp, WORD x, WORD y)
{
    /* Turret: 12x12, cannon on pedestal */
    /* Pedestal base */
    SetAPen(rp, COL_METAL);
    RectFill(rp, x + 1, y + 8, x + 10, y + 11);
    /* Pedestal column */
    RectFill(rp, x + 3, y + 5, x + 8, y + 8);
    /* Cannon barrel */
    SetAPen(rp, COL_ENEMY);
    RectFill(rp, x + 2, y + 2, x + 9, y + 5);
    /* Muzzle */
    SetAPen(rp, COL_ENEMY2);
    RectFill(rp, x + 4, y, x + 7, y + 1);
    /* Flash indicator */
    if ((g_frame / 8) & 1) {
        SetAPen(rp, COL_LAVA);
        WritePixel(rp, x + 5, y);
        WritePixel(rp, x + 6, y);
    }
}

static void draw_enemy_boss_beast(struct RastPort *rp, WORD x, WORD y)
{
    /* Boss Beast: 24x32, large beast */
    /* Main body */
    SetAPen(rp, COL_ENEMY);
    RectFill(rp, x + 4, y + 8, x + 19, y + 27);
    /* Head */
    RectFill(rp, x + 6, y + 2, x + 17, y + 9);
    /* Eyes */
    SetAPen(rp, COL_WHITE);
    RectFill(rp, x + 8, y + 4, x + 9, y + 5);
    RectFill(rp, x + 14, y + 4, x + 15, y + 5);
    /* Pupils */
    SetAPen(rp, COL_BG);
    WritePixel(rp, x + 9, y + 5);
    WritePixel(rp, x + 15, y + 5);
    /* Belly plates */
    SetAPen(rp, COL_ENEMY2);
    RectFill(rp, x + 7, y + 14, x + 16, y + 16);
    RectFill(rp, x + 7, y + 18, x + 16, y + 20);
    RectFill(rp, x + 7, y + 22, x + 16, y + 24);
    /* Arms */
    SetAPen(rp, COL_ENEMY);
    RectFill(rp, x, y + 10, x + 5, y + 14);
    RectFill(rp, x + 18, y + 10, x + 23, y + 14);
    /* Claws */
    SetAPen(rp, COL_ENEMY2);
    RectFill(rp, x, y + 14, x + 2, y + 17);
    RectFill(rp, x + 21, y + 14, x + 23, y + 17);
    /* Legs */
    SetAPen(rp, COL_ENEMY);
    RectFill(rp, x + 5, y + 28, x + 9, y + 31);
    RectFill(rp, x + 14, y + 28, x + 18, y + 31);
    /* Spikes on head */
    SetAPen(rp, COL_ENEMY2);
    RectFill(rp, x + 7, y, x + 8, y + 2);
    RectFill(rp, x + 11, y, x + 12, y + 1);
    RectFill(rp, x + 15, y, x + 16, y + 2);
}

static void draw_enemy_boss_drake(struct RastPort *rp, WORD x, WORD y)
{
    /* Boss Drake: 20x24, dragon-like */
    /* Body */
    SetAPen(rp, COL_ENEMY);
    RectFill(rp, x + 4, y + 8, x + 15, y + 19);
    /* Head */
    RectFill(rp, x + 10, y + 2, x + 18, y + 9);
    /* Snout */
    RectFill(rp, x + 16, y + 5, x + 19, y + 8);
    /* Eye */
    SetAPen(rp, COL_LAVA);
    RectFill(rp, x + 13, y + 4, x + 14, y + 5);
    /* Wings */
    SetAPen(rp, COL_ENEMY2);
    {
        WORD wingpos = (g_frame / 4) & 1;
        if (wingpos) {
            RectFill(rp, x, y + 4, x + 5, y + 8);
            RectFill(rp, x, y + 2, x + 2, y + 4);
        } else {
            RectFill(rp, x, y + 8, x + 5, y + 12);
            RectFill(rp, x, y + 12, x + 2, y + 14);
        }
    }
    /* Tail */
    SetAPen(rp, COL_ENEMY);
    RectFill(rp, x + 2, y + 18, x + 6, y + 20);
    RectFill(rp, x, y + 20, x + 4, y + 22);
    /* Tail spike */
    SetAPen(rp, COL_ENEMY2);
    RectFill(rp, x, y + 22, x + 2, y + 23);
    /* Legs */
    SetAPen(rp, COL_ENEMY);
    RectFill(rp, x + 6, y + 20, x + 8, y + 23);
    RectFill(rp, x + 12, y + 20, x + 14, y + 23);
    /* Claws */
    SetAPen(rp, COL_ENEMY2);
    RectFill(rp, x + 5, y + 23, x + 9, y + 23);
    RectFill(rp, x + 11, y + 23, x + 15, y + 23);
}

static void draw_enemies(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &gs->enemies[i];
        WORD ex, ey;
        if (!e->active) continue;

        ex = (WORD)(e->x >> 16);
        ey = (WORD)(e->y >> 16) + HUD_H;

        /* Flash when hit */
        if (e->timer > 0 && (g_frame & 1)) continue;

        switch (e->type) {
            case ENEMY_CRAWLER:
                draw_enemy_crawler(rp, ex, ey, e->facing);
                break;
            case ENEMY_FLYER:
                draw_enemy_flyer(rp, ex, ey);
                break;
            case ENEMY_HOPPER:
                draw_enemy_hopper(rp, ex, ey);
                break;
            case ENEMY_TURRET:
                draw_enemy_turret(rp, ex, ey);
                break;
            case ENEMY_BOSS_BEAST:
                draw_enemy_boss_beast(rp, ex, ey);
                break;
            case ENEMY_BOSS_DRAKE:
                draw_enemy_boss_drake(rp, ex, ey);
                break;
            default:
                /* Generic enemy fallback */
                SetAPen(rp, COL_ENEMY);
                RectFill(rp, ex, ey, ex + 9, ey + 9);
                break;
        }
    }
}

/* --- Projectiles and effects --- */

static void draw_bullets(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_BULLETS; i++) {
        Bullet *b = &gs->bullets[i];
        WORD bx, by;
        if (!b->active) continue;

        bx = (WORD)(b->x >> 16);
        by = (WORD)(b->y >> 16) + HUD_H;

        if (b->type == BULLET_TYPE_MISSILE) {
            /* Missile: yellow rectangle */
            SetAPen(rp, COL_MISSILE);
            RectFill(rp, bx, by, bx + 5, by + 2);
            /* Nose */
            SetAPen(rp, COL_WHITE);
            if (b->dx > 0) {
                WritePixel(rp, bx + 5, by + 1);
            } else {
                WritePixel(rp, bx, by + 1);
            }
        } else {
            /* Beam shot: green dot/line */
            SetAPen(rp, COL_BEAM);
            if (b->dy != 0) {
                /* Vertical shot */
                RectFill(rp, bx, by, bx + 1, by + 3);
            } else {
                /* Horizontal shot */
                RectFill(rp, bx, by, bx + 3, by + 1);
            }
        }
    }
}

static void draw_bombs(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_BOMBS; i++) {
        Bomb *b = &gs->bombs[i];
        WORD bx, by;
        if (!b->active) continue;

        bx = (WORD)(b->x >> 16);
        by = (WORD)(b->y >> 16) + HUD_H;

        if (b->timer > 10) {
            /* Unexploded bomb: small circle */
            SetAPen(rp, COL_HUNTER2);
            RectFill(rp, bx + 1, by, bx + 2, by);
            RectFill(rp, bx, by + 1, bx + 3, by + 2);
            RectFill(rp, bx + 1, by + 3, bx + 2, by + 3);
        } else {
            /* Exploding: expanding flash */
            WORD r = (10 - b->timer) + 2;
            SetAPen(rp, COL_WHITE);
            RectFill(rp, bx - r + 2, by - r + 2, bx + r, by + r);
            SetAPen(rp, COL_HUNTER2);
            if (r > 2) {
                RectFill(rp, bx - r + 3, by - r + 3, bx + r - 1, by + r - 1);
            }
        }
    }
}

static void draw_enemy_bullets(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_ENEMY_BULLETS; i++) {
        EnemyBullet *eb = &gs->enemy_bullets[i];
        WORD ex, ey;
        if (!eb->active) continue;

        ex = (WORD)(eb->x >> 16);
        ey = (WORD)(eb->y >> 16) + HUD_H;

        SetAPen(rp, COL_ENEMY2);
        RectFill(rp, ex, ey, ex + 2, ey + 2);
    }
}

static void draw_particles(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < MAX_PARTICLES; i++) {
        Particle *p = &gs->particles[i];
        WORD px, py;
        if (!p->active) continue;

        px = (WORD)(p->x >> 16);
        py = (WORD)(p->y >> 16) + HUD_H;

        SetAPen(rp, p->color);
        WritePixel(rp, px, py);
        WritePixel(rp, px + 1, py);
    }
}

/* --- Items (collectible orbs) --- */

static void draw_items(struct RastPort *rp, GameState *gs)
{
    WORD i;
    for (i = 0; i < g_num_items; i++) {
        ItemSpawn *it = &g_items[i];
        WORD ix, iy;
        WORD pulse;

        /* Skip if already collected or not in current room */
        if (gs->items_collected & (1L << i)) continue;
        if (it->room_x != gs->room_x || it->room_y != gs->room_y) continue;

        ix = it->tile_x * TILE_W;
        iy = it->tile_y * TILE_H + HUD_H;

        /* Pulsing orb */
        pulse = ((g_frame / 4) + i) & 3;
        SetAPen(rp, pulse > 1 ? COL_ITEM : COL_WHITE);
        RectFill(rp, ix + 2, iy, ix + 5, iy);
        RectFill(rp, ix + 1, iy + 1, ix + 6, iy + 1);
        RectFill(rp, ix, iy + 2, ix + 7, iy + 5);
        RectFill(rp, ix + 1, iy + 6, ix + 6, iy + 6);
        RectFill(rp, ix + 2, iy + 7, ix + 5, iy + 7);

        /* Inner highlight */
        SetAPen(rp, COL_WHITE);
        WritePixel(rp, ix + 2, iy + 2);
        WritePixel(rp, ix + 3, iy + 2);
    }
}

/* --- HUD --- */

static const char *area_names[] = {
    "CAVERN",
    "MAGMA",
    "BEAST LAIR",
    "DRAKE LAIR",
    "CITADEL",
    "SURFACE"
};

void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[32];
    WORD energy_w;
    WORD bar_color;
    Hunter *h = &gs->hunter;
    WORD area_idx;

    /* Background bar */
    SetAPen(rp, COL_BG);
    RectFill(rp, 0, 0, SCREEN_W - 1, HUD_H - 1);

    /* Energy label */
    SetAPen(rp, COL_WHITE);
    draw_string(rp, 2, 1, "EN", 1);

    /* Energy bar frame */
    SetAPen(rp, COL_METAL2);
    RectFill(rp, 16, 1, 81, 7);

    /* Energy bar fill */
    if (h->max_energy > 0) {
        energy_w = (h->health * 64) / h->max_energy;
    } else {
        energy_w = 0;
    }
    bar_color = (h->health * 4 > h->max_energy) ? COL_BEAM : COL_LAVA;
    SetAPen(rp, bar_color);
    if (energy_w > 0) {
        RectFill(rp, 17, 2, 17 + energy_w - 1, 6);
    }

    /* Numeric health */
    sprintf(buf, "%03ld", (long)h->health);
    SetAPen(rp, COL_WHITE);
    draw_string(rp, 84, 1, buf, 1);

    /* Missiles (if has_missiles) */
    if (h->has_missiles) {
        SetAPen(rp, COL_MISSILE);
        draw_string(rp, 120, 1, "M", 1);
        sprintf(buf, "%02ld", (long)h->missiles);
        SetAPen(rp, COL_WHITE);
        draw_string(rp, 128, 1, buf, 1);
    }

    /* Area name based on room_y */
    area_idx = gs->room_y;
    if (area_idx < 0) area_idx = 0;
    if (area_idx > 5) area_idx = 5;
    SetAPen(rp, COL_ITEM);
    draw_string(rp, 160, 1, area_names[area_idx], 1);

    /* Mini-map: 8x6 grid showing current room position */
    {
        WORD mx_base = SCREEN_W - 52;
        WORD my_base = 1;
        WORD mx, my;

        /* Map frame */
        SetAPen(rp, COL_METAL);
        RectFill(rp, mx_base - 1, my_base - 1,
                 mx_base + 8 * 6, my_base + 6 * 2 + 1);

        /* Room dots */
        for (my = 0; my < 6; my++) {
            for (mx = 0; mx < 8; mx++) {
                WORD px = mx_base + mx * 6;
                WORD py = my_base + my * 2;
                if (mx == gs->room_x && my == gs->room_y) {
                    /* Current room: bright flash */
                    SetAPen(rp, (g_frame & 4) ? COL_WHITE : COL_HUNTER);
                    RectFill(rp, px, py, px + 4, py + 1);
                }
            }
        }
    }

    /* Debug: show hunter Y pos */
    {
        char dbg[40];
        LONG hy = (h->y >> 16);
        LONG feet = hy + (LONG)HUNTER_H;
        snprintf(dbg, sizeof(dbg), "Y%ld F%ld R%ld,%ld",
                 hy, feet, (long)gs->room_x, (long)gs->room_y);
        SetAPen(rp, COL_WHITE);
        draw_string(rp, 170, 9, dbg, 1);
    }

    /* Bottom HUD line separator */
    SetAPen(rp, COL_METAL2);
    RectFill(rp, 0, HUD_H - 1, SCREEN_W - 1, HUD_H - 1);
}

/* --- Title screen --- */

void draw_title(struct RastPort *rp)
{
    WORD cx;

    SetRast(rp, 0);
    g_frame++;

    /* "ORB HUNTER" in large text (double-size) */
    SetAPen(rp, COL_HUNTER);
    cx = (SCREEN_W - string_width("ORB HUNTER", 3)) / 2;
    draw_string(rp, cx, 40, "ORB HUNTER", 3);

    /* Underline decoration */
    SetAPen(rp, COL_HUNTER2);
    RectFill(rp, cx, 67, cx + string_width("ORB HUNTER", 3), 68);

    /* Subtitle */
    SetAPen(rp, COL_ITEM);
    cx = (SCREEN_W - string_width("THE LOST CAVERNS", 1)) / 2;
    draw_string(rp, cx, 80, "THE LOST CAVERNS", 1);

    /* "PRESS A TO START" flashing */
    if ((g_frame / 20) & 1) {
        SetAPen(rp, COL_WHITE);
        cx = (SCREEN_W - string_width("PRESS A TO START", 1)) / 2;
        draw_string(rp, cx, 130, "PRESS A TO START", 1);
    }

    /* Controls */
    SetAPen(rp, COL_METAL2);
    cx = 60;
    draw_string(rp, cx, 160, "CONTROLS:", 1);
    draw_string(rp, cx, 172, "LEFT/RIGHT - MOVE", 1);
#ifdef AMIGA3DO
    /* 3DO port: what the buttons really do (missiles are fired with
     * UP held; P isn't used in play) */
    draw_string(rp, cx, 182, "UP/B - JUMP", 1);
    draw_string(rp, cx, 192, "DOWN - ROLL FORM", 1);
    draw_string(rp, cx, 202, "A - SHOOT (BOMB IN ROLL)", 1);
    draw_string(rp, cx, 212, "UP + A - MISSILE", 1);
#else
    draw_string(rp, cx, 182, "UP/B - JUMP / AIM UP", 1);
    draw_string(rp, cx, 192, "DOWN - ROLL FORM", 1);
    draw_string(rp, cx, 202, "A - SHOOT/BOMB", 1);
    draw_string(rp, cx, 212, "P - MISSILE MODE", 1);
#endif

    /* Version */
    SetAPen(rp, COL_ROCK2);
    draw_string(rp, 4, SCREEN_H - 10, "V1.0 - 2026", 1);
}

/* --- Item get screen --- */

void draw_item_get(struct RastPort *rp, GameState *gs)
{
    WORD bx, by, bw, bh;
    WORD cx;
    char buf[48];

    bw = 200;
    bh = 60;
    bx = (SCREEN_W - bw) / 2;
    by = (SCREEN_H - bh) / 2;

    /* Box background */
    SetAPen(rp, COL_BG);
    RectFill(rp, bx, by, bx + bw - 1, by + bh - 1);

    /* Box border */
    SetAPen(rp, COL_ITEM);
    RectFill(rp, bx, by, bx + bw - 1, by);
    RectFill(rp, bx, by + bh - 1, bx + bw - 1, by + bh - 1);
    RectFill(rp, bx, by, bx, by + bh - 1);
    RectFill(rp, bx + bw - 1, by, bx + bw - 1, by + bh - 1);

    /* "GOT [ITEM]!" */
    snprintf(buf, sizeof(buf), "GOT %s!", gs->item_name);
    cx = bx + (bw - string_width(buf, 1)) / 2;
    SetAPen(rp, COL_WHITE);
    draw_string(rp, cx, by + 14, buf, 1);

    /* "PRESS A" */
    if ((g_frame / 15) & 1) {
        cx = bx + (bw - string_width("PRESS A", 1)) / 2;
        SetAPen(rp, COL_METAL2);
        draw_string(rp, cx, by + 44, "PRESS A", 1);
    }
}

/* --- Game Over screen --- */

void draw_gameover(struct RastPort *rp)
{
    WORD cx;

    /* Advance our own frame counter so blinking prompt keeps toggling
       after the last draw_frame() call. */
    g_frame++;

    SetRast(rp, COL_BG);

    /* "GAME OVER" in large text */
    SetAPen(rp, COL_LAVA);
    cx = (SCREEN_W - string_width("GAME OVER", 3)) / 2;
    draw_string(rp, cx, 80, "GAME OVER", 3);

    /* Decorative line */
    SetAPen(rp, COL_LAVA2);
    RectFill(rp, cx, 107, cx + string_width("GAME OVER", 3), 108);

    /* "PRESS A TO CONTINUE" */
    if ((g_frame / 20) & 1) {
        SetAPen(rp, COL_WHITE);
        cx = (SCREEN_W - string_width("PRESS A TO CONTINUE", 1)) / 2;
        draw_string(rp, cx, 160, "PRESS A TO CONTINUE", 1);
    }
}

/* --- Main frame draw --- */

void draw_frame(struct RastPort *rp, GameState *gs)
{
    WORD tx, ty;
    UBYTE tile;

    g_frame = gs->frame;

    /* Clear screen */
    SetRast(rp, COL_BG);

    /* Draw all tiles for current room */
    for (ty = 0; ty < ROOM_H; ty++) {
        for (tx = 0; tx < ROOM_W; tx++) {
            tile = world_rooms[gs->room_y][gs->room_x][ty][tx];
            if (tile != TILE_EMPTY) {
                draw_tile(rp, tx, ty, tile);
            }
        }
    }

    /* Draw items */
    draw_items(rp, gs);

    /* Draw hunter */
    draw_hunter(rp, gs);

    /* Draw player bullets */
    draw_bullets(rp, gs);

    /* Draw bombs */
    draw_bombs(rp, gs);

    /* Draw enemies */
    draw_enemies(rp, gs);

    /* Draw enemy bullets */
    draw_enemy_bullets(rp, gs);

    /* Draw particles */
    draw_particles(rp, gs);

    /* Draw HUD on top */
    draw_hud(rp, gs);
}
