// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>

#include "draw.h"
#ifdef AMIGA3DO
#include <exec/memory.h>
#include "amiga3do.h"
#endif
#include "game.h"

/* 16-color palette: 4-bit RGB values */
const UWORD palette[NUM_COLORS] = {
    0x112,  /*  0: dark blue-black background */
    0xE4A,  /*  1: rose - I gem */
    0x3BB,  /*  2: teal - O gem */
    0xDA3,  /*  3: ochre - T gem */
    0x96F,  /*  4: lavender - S gem */
    0x6D6,  /*  5: jade - Z gem */
    0x5AF,  /*  6: sky - L gem */
    0xF75,  /*  7: coral - J gem */
    0x888,  /*  8: gray - field border */
    0xFFF,  /*  9: white - text, gem glint */
    0x667,  /* 10: dim - landing marks */
    0x112,  /* 11: gem shadow edge */
    0xFF0,  /* 12: bright yellow - flash */
    0xC85,  /* 13: copper - title, well walls */
    0xFD8,  /* 14: pale gold - score */
    0x223   /* 15: very dark - field background (alternate columns are 0) */
};

/*
 * Minimal 5x7 bitmap font.
 * Each character is 5 bytes (rows 0-6, skipping row 7 which is blank).
 * Wait -- we need 7 rows, so 7 bytes per character.
 * Each byte: bits 7..3 represent pixels (5 wide), bits 2..0 unused.
 * Bit 7 = leftmost pixel.
 */
#define FONT_W 5
#define FONT_H 7
#define FONT_FIRST '!'
#define FONT_LAST  'Z'
#define FONT_CHARS (FONT_LAST - FONT_FIRST + 1)

/* We store chars '!' through 'Z' (ASCII 33-90).
 * Space (32) is handled as a special case (blank).
 * Each row is a byte, top 5 bits used. */
static const UBYTE font_data[FONT_CHARS][FONT_H] = {
    /* ! (33) */
    { 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x20 },
    /* " (34) */
    { 0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* # (35) */
    { 0x50, 0xF8, 0x50, 0x50, 0xF8, 0x50, 0x00 },
    /* $ (36) */
    { 0x20, 0x78, 0xA0, 0x70, 0x28, 0xF0, 0x20 },
    /* % (37) */
    { 0xC8, 0xC8, 0x10, 0x20, 0x40, 0x98, 0x98 },
    /* & (38) */
    { 0x40, 0xA0, 0xA0, 0x40, 0xA8, 0x90, 0x68 },
    /* ' (39) */
    { 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 },
    /* ( (40) */
    { 0x10, 0x20, 0x40, 0x40, 0x40, 0x20, 0x10 },
    /* ) (41) */
    { 0x40, 0x20, 0x10, 0x10, 0x10, 0x20, 0x40 },
    /* * (42) */
    { 0x00, 0x50, 0x20, 0xF8, 0x20, 0x50, 0x00 },
    /* + (43) */
    { 0x00, 0x20, 0x20, 0xF8, 0x20, 0x20, 0x00 },
    /* , (44) */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x40 },
    /* - (45) */
    { 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00 },
    /* . (46) */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20 },
    /* / (47) */
    { 0x08, 0x08, 0x10, 0x20, 0x40, 0x80, 0x80 },
    /* 0 (48) */
    { 0x70, 0x88, 0x98, 0xA8, 0xC8, 0x88, 0x70 },
    /* 1 (49) */
    { 0x20, 0x60, 0x20, 0x20, 0x20, 0x20, 0x70 },
    /* 2 (50) */
    { 0x70, 0x88, 0x08, 0x10, 0x20, 0x40, 0xF8 },
    /* 3 (51) */
    { 0x70, 0x88, 0x08, 0x30, 0x08, 0x88, 0x70 },
    /* 4 (52) */
    { 0x10, 0x30, 0x50, 0x90, 0xF8, 0x10, 0x10 },
    /* 5 (53) */
    { 0xF8, 0x80, 0xF0, 0x08, 0x08, 0x88, 0x70 },
    /* 6 (54) */
    { 0x30, 0x40, 0x80, 0xF0, 0x88, 0x88, 0x70 },
    /* 7 (55) */
    { 0xF8, 0x08, 0x10, 0x20, 0x40, 0x40, 0x40 },
    /* 8 (56) */
    { 0x70, 0x88, 0x88, 0x70, 0x88, 0x88, 0x70 },
    /* 9 (57) */
    { 0x70, 0x88, 0x88, 0x78, 0x08, 0x10, 0x60 },
    /* : (58) */
    { 0x00, 0x00, 0x20, 0x00, 0x00, 0x20, 0x00 },
    /* ; (59) */
    { 0x00, 0x00, 0x20, 0x00, 0x00, 0x20, 0x40 },
    /* < (60) */
    { 0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08 },
    /* = (61) */
    { 0x00, 0x00, 0xF8, 0x00, 0xF8, 0x00, 0x00 },
    /* > (62) */
    { 0x80, 0x40, 0x20, 0x10, 0x20, 0x40, 0x80 },
    /* ? (63) */
    { 0x70, 0x88, 0x08, 0x10, 0x20, 0x00, 0x20 },
    /* @ (64) */
    { 0x70, 0x88, 0xB8, 0xB8, 0x80, 0x88, 0x70 },
    /* A (65) */
    { 0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88 },
    /* B (66) */
    { 0xF0, 0x88, 0x88, 0xF0, 0x88, 0x88, 0xF0 },
    /* C (67) */
    { 0x70, 0x88, 0x80, 0x80, 0x80, 0x88, 0x70 },
    /* D (68) */
    { 0xF0, 0x88, 0x88, 0x88, 0x88, 0x88, 0xF0 },
    /* E (69) */
    { 0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0xF8 },
    /* F (70) */
    { 0xF8, 0x80, 0x80, 0xF0, 0x80, 0x80, 0x80 },
    /* G (71) */
    { 0x70, 0x88, 0x80, 0xB8, 0x88, 0x88, 0x70 },
    /* H (72) */
    { 0x88, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88 },
    /* I (73) */
    { 0x70, 0x20, 0x20, 0x20, 0x20, 0x20, 0x70 },
    /* J (74) */
    { 0x38, 0x10, 0x10, 0x10, 0x10, 0x90, 0x60 },
    /* K (75) */
    { 0x88, 0x90, 0xA0, 0xC0, 0xA0, 0x90, 0x88 },
    /* L (76) */
    { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xF8 },
    /* M (77) */
    { 0x88, 0xD8, 0xA8, 0x88, 0x88, 0x88, 0x88 },
    /* N (78) */
    { 0x88, 0xC8, 0xA8, 0x98, 0x88, 0x88, 0x88 },
    /* O (79) */
    { 0x70, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70 },
    /* P (80) */
    { 0xF0, 0x88, 0x88, 0xF0, 0x80, 0x80, 0x80 },
    /* Q (81) */
    { 0x70, 0x88, 0x88, 0x88, 0xA8, 0x90, 0x68 },
    /* R (82) */
    { 0xF0, 0x88, 0x88, 0xF0, 0xA0, 0x90, 0x88 },
    /* S (83) */
    { 0x70, 0x88, 0x80, 0x70, 0x08, 0x88, 0x70 },
    /* T (84) */
    { 0xF8, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 },
    /* U (85) */
    { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x70 },
    /* V (86) */
    { 0x88, 0x88, 0x88, 0x88, 0x50, 0x50, 0x20 },
    /* W (87) */
    { 0x88, 0x88, 0x88, 0x88, 0xA8, 0xD8, 0x88 },
    /* X (88) */
    { 0x88, 0x88, 0x50, 0x20, 0x50, 0x88, 0x88 },
    /* Y (89) */
    { 0x88, 0x88, 0x50, 0x20, 0x20, 0x20, 0x20 },
    /* Z (90) */
    { 0xF8, 0x08, 0x10, 0x20, 0x40, 0x80, 0xF8 }
};

#ifdef AMIGA3DO
/* 3DO port: each glyph is rendered once per scale (1..3) into a run-length
 * sprite and drawn with gfx_sprite_draw(); one WritePixel per font pixel
 * is too slow on the ARM60. Same pixels on screen. */
static GfxSprite *glyph_cache[3][FONT_CHARS];

static const GfxSprite *glyph_sprite(int idx, int scale)
{
    GfxSprite **slot = &glyph_cache[scale - 1][idx];
    if (!*slot) {
        int w = FONT_W * scale, h = FONT_H * scale, row, col;
        UBYTE *m = (UBYTE *)AllocMem(w * h, MEMF_CLEAR);
        if (!m) return 0;
        for (row = 0; row < h; row++)
            for (col = 0; col < w; col++)
                if (font_data[idx][row / scale] & (0x80 >> (col / scale)))
                    m[row * w + col] = 1;
        *slot = gfx_sprite_make(m, w, h);
        FreeMem(m, w * h);
    }
    return *slot;
}
#endif

static void draw_char(struct RastPort *rp, int x, int y, char c, int color, int scale)
{
    const UBYTE *glyph;
    int row, col;

    /* Space is blank */
    if (c == ' ') return;

    /* Convert lowercase to uppercase */
    if (c >= 'a' && c <= 'z') c -= 32;

    /* Range check */
    if (c < FONT_FIRST || c > FONT_LAST) return;

    glyph = font_data[c - FONT_FIRST];
#ifdef AMIGA3DO
    if (scale >= 1 && scale <= 3) {
        const GfxSprite *g = glyph_sprite(c - FONT_FIRST, scale);
        if (g) {
            gfx_sprite_draw(rp, g, x, y, color);
            return;
        }
    }
#endif
    SetAPen(rp, color);

    for (row = 0; row < FONT_H; row++) {
        UBYTE bits = glyph[row];
        for (col = 0; col < FONT_W; col++) {
            if (bits & (0x80 >> col)) {
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

static void draw_text(struct RastPort *rp, int x, int y, const char *str, int color, int scale)
{
    int cx = x;
    int spacing = (FONT_W + 1) * scale;

    while (*str) {
        draw_char(rp, cx, y, *str, color, scale);
        cx += spacing;
        str++;
    }
}

/* Returns pixel width of a string at given scale */
static int text_width(const char *str, int scale)
{
    int len = strlen(str);
    if (len == 0) return 0;
    return len * (FONT_W + 1) * scale - scale; /* subtract trailing gap */
}

/* A gem: rounded body, dark lower edge, a white glint top-left */
static void draw_cell(struct RastPort *rp, int x, int y, int color, BOOL bevel)
{
    int e = CELL_SIZE - 2;
#ifdef AMIGA3DO
    /* 3DO port: each gem is pre-rendered once as a multi-colour sprite */
    static GfxSprite *gem[NUM_COLORS];
    if (bevel && color > 0 && color < NUM_COLORS) {
        if (!gem[color]) {
            UBYTE m[(CELL_SIZE - 1) * (CELL_SIZE - 1)];
            int w = CELL_SIZE - 1, r, c;
            for (r = 0; r <= e; r++)
                for (c = 0; c <= e; c++) {
                    UBYTE v = (UBYTE)color;
                    if ((r == 0 || r == e) && (c == 0 || c == e)) v = 0;
                    else if (r == e - 1 && c >= 2 && c <= e - 2) v = 11;
                    else if (r >= 2 && r <= 3 && c >= 2 && c <= 3) v = 9;
                    m[r * w + c] = v;
                }
            gem[color] = gfx_sprite_make(m, w, w);
        }
        if (gem[color]) {
            gfx_sprite_draw(rp, gem[color], x, y, -1);
            return;
        }
    }
#endif

    SetAPen(rp, color);
    RectFill(rp, x + 1, y, x + e - 1, y + e);
    RectFill(rp, x, y + 1, x + e, y + e - 1);

    if (bevel) {
        SetAPen(rp, 11);
        RectFill(rp, x + 2, y + e - 1, x + e - 2, y + e - 1);
        SetAPen(rp, 9);
        RectFill(rp, x + 2, y + 2, x + 3, y + 3);
    }
}

void draw_clear(struct RastPort *rp)
{
    SetRast(rp, 0);
}

void draw_field(struct RastPort *rp, const GameState *gs)
{
    int x, y, px, py;
    int fw = FIELD_W * CELL_SIZE;
    int fh = FIELD_H * CELL_SIZE;

    /* Field background: shaded column stripes instead of a grid (the
     * screen is already cleared to pen 0 by draw_clear()) */
    SetAPen(rp, 15);
    for (x = 0; x < FIELD_W; x += 2) {
        px = FIELD_X + x * CELL_SIZE;
        RectFill(rp, px, FIELD_Y, px + CELL_SIZE - 1, FIELD_Y + fh - 1);
    }

    /* Filled cells */
    for (y = 0; y < FIELD_H; y++) {
        for (x = 0; x < FIELD_W; x++) {
            UBYTE cell = gs->field[y][x];
            if (cell) {
                px = FIELD_X + x * CELL_SIZE;
                py = FIELD_Y + y * CELL_SIZE;
                draw_cell(rp, px, py, (int)cell, TRUE);
            }
        }
    }

    /* Open-topped well: thick sides and floor, no lid */
    SetAPen(rp, 13);
    RectFill(rp, FIELD_X - 4, FIELD_Y, FIELD_X - 2, FIELD_Y + fh + 3);
    RectFill(rp, FIELD_X + fw + 1, FIELD_Y, FIELD_X + fw + 3, FIELD_Y + fh + 3);
    RectFill(rp, FIELD_X - 4, FIELD_Y + fh + 1, FIELD_X + fw + 3, FIELD_Y + fh + 3);
}

void draw_current_piece(struct RastPort *rp, const GameState *gs)
{
    const PieceShape *shape;
    int row, col, px, py;
    int color = gs->current.type + 1;

    shape = piece_get_shape(gs->current.type, gs->current.rotation);

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            if (shape->cells[row][col]) {
                int fy = gs->current.y + row;
                if (fy < 0) continue; /* Above field */
                px = FIELD_X + (gs->current.x + col) * CELL_SIZE;
                py = FIELD_Y + fy * CELL_SIZE;
                draw_cell(rp, px, py, color, TRUE);
            }
        }
    }
}

void draw_ghost_piece(struct RastPort *rp, const GameState *gs)
{
    const PieceShape *shape;
    int row, col, px, py;
    int ghost_y = game_ghost_y(gs);

    /* Don't draw ghost if it overlaps the current piece */
    if (ghost_y == gs->current.y) return;

    shape = piece_get_shape(gs->current.type, gs->current.rotation);
    SetAPen(rp, 10);

    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            if (shape->cells[row][col]) {
                int fy = ghost_y + row;
                if (fy < 0) continue;
                px = FIELD_X + (gs->current.x + col) * CELL_SIZE;
                py = FIELD_Y + fy * CELL_SIZE;

                /* Landing marks: a small dot in each cell */
                RectFill(rp, px + 4, py + 4, px + 6, py + 6);
            }
        }
    }
}

void draw_next_piece(struct RastPort *rp, const GameState *gs)
{
    const PieceShape *shape;
    int row, col, px, py;
    int color = gs->next_type + 1;
    int box_x = 30;
    int box_y = 150;

    /* "Coming up" tray at the bottom of the left panel, no frame */
    draw_text(rp, box_x, box_y - 14, "COMING UP", 10, 1);
    SetAPen(rp, 13);
    RectFill(rp, box_x - 6, box_y + 4 * CELL_SIZE + 2,
             box_x + 4 * CELL_SIZE + 52, box_y + 4 * CELL_SIZE + 3);

    shape = piece_get_shape(gs->next_type, 0);
    for (row = 0; row < 4; row++) {
        for (col = 0; col < 4; col++) {
            if (shape->cells[row][col]) {
                px = box_x + 20 + col * CELL_SIZE;
                py = box_y + row * CELL_SIZE;
                draw_cell(rp, px, py, color, TRUE);
            }
        }
    }
}

void draw_hud(struct RastPort *rp, const GameState *gs)
{
    char buf[16];
    int hx = 24;

    /* Name plate */
    draw_text(rp, hx, 14, "STAKATTACK", 13, 1);

    /* Big score, then rows and stage on one line */
    sprintf(buf, "%ld", (long)gs->score);
    draw_text(rp, hx, 40, "POINTS", 10, 1);
    draw_text(rp, hx, 52, buf, 14, 2);

    draw_text(rp, hx, 84, "ROWS", 10, 1);
    sprintf(buf, "%ld", (long)gs->lines);
    draw_text(rp, hx, 96, buf, 9, 1);

    draw_text(rp, hx + 60, 84, "STAGE", 10, 1);
    sprintf(buf, "%ld", (long)gs->level);
    draw_text(rp, hx + 60, 96, buf, 9, 1);
}

void draw_title(struct RastPort *rp)
{
    int cx;
    const char *title = "STAKATTACK";
    const char *prompt = "PRESS A TO START";

    /* Title in large text, centered */
    cx = (320 - text_width(title, 3)) / 2;
    draw_text(rp, cx, 60, title, 13, 3);

    /* A row of the seven gems under the title */
    {
        int i;
        for (i = 0; i < NUM_PIECES; i++)
            draw_cell(rp, 160 - (NUM_PIECES * 16) / 2 + i * 16 + 2, 96, i + 1, TRUE);
    }

    /* Prompt */
    cx = (320 - text_width(prompt, 1)) / 2;
    draw_text(rp, cx, 160, prompt, 9, 1);
#ifdef AMIGA3DO
    /* 3DO port: describe the control pad */
    {
        const char *pad = "A ROTATE  B DROP  P PAUSE";
        cx = (320 - text_width(pad, 1)) / 2;
        draw_text(rp, cx, 180, pad, 9, 1);
    }
#endif
}

void draw_gameover(struct RastPort *rp, const GameState *gs)
{
    char buf[16];
    int cx;
    const char *go_text = "GAME OVER";
    const char *prompt = "PRESS A";

    /* Semi-dark overlay on playfield */
    SetAPen(rp, 0);
    RectFill(rp, FIELD_X + 2, FIELD_Y + 70, FIELD_X + FIELD_W * CELL_SIZE - 3, FIELD_Y + 170);

    /* GAME OVER in large red */
    cx = FIELD_X + (FIELD_W * CELL_SIZE - text_width(go_text, 2)) / 2;
    draw_text(rp, cx, 90, go_text, 1, 2);

    /* Final score */
    sprintf(buf, "%ld", (long)gs->score);
    cx = FIELD_X + (FIELD_W * CELL_SIZE - text_width(buf, 2)) / 2;
    draw_text(rp, cx, 120, buf, 9, 2);

    /* Prompt */
    cx = FIELD_X + (FIELD_W * CELL_SIZE - text_width(prompt, 1)) / 2;
    draw_text(rp, cx, 150, prompt, 9, 1);
}

void draw_line_clear_flash(struct RastPort *rp, const GameState *gs)
{
    int i, px_y;
    int flash_color;

    if (gs->clear_timer <= 0) return;

    /* Alternate between white and copper */
    flash_color = (gs->clear_timer & 2) ? 9 : 13;

    for (i = 0; i < gs->clear_count; i++) {
        int line = gs->clear_lines[i];
        if (line < 0 || line >= FIELD_H) continue;

        px_y = FIELD_Y + line * CELL_SIZE;
        SetAPen(rp, flash_color);
        RectFill(rp, FIELD_X, px_y,
                 FIELD_X + FIELD_W * CELL_SIZE - 1, px_y + CELL_SIZE - 1);
    }
}

void draw_paused(struct RastPort *rp)
{
    const char *text = "PAUSED";
    int cx;

    /* Dark overlay on playfield center */
    SetAPen(rp, 0);
    RectFill(rp, FIELD_X + 2, FIELD_Y + 100, FIELD_X + FIELD_W * CELL_SIZE - 3, FIELD_Y + 140);

    /* PAUSED text centered on playfield */
    cx = FIELD_X + (FIELD_W * CELL_SIZE - text_width(text, 2)) / 2;
    draw_text(rp, cx, FIELD_Y + 110, text, 9, 2);
}
