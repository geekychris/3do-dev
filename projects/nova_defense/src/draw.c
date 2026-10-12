// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include <exec/types.h>
#include <graphics/rastport.h>
#include <graphics/gfxmacros.h>
#include <proto/graphics.h>
#include <string.h>
#include "draw.h"
#include "game.h"
#ifdef AMIGA3DO
#include <exec/memory.h>
#include "amiga3do.h"
#endif

/* 5x7 bitmap font - each character is 7 bytes, bits 4-0 = columns L-R */
static const UBYTE font_data[36][7] = {
    /* 0 */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* 1 */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* 2 */ {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
    /* 3 */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    /* 4 */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* 5 */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* 6 */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* 7 */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    /* 8 */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* 9 */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* A */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* B */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* C */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* D */ {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    /* E */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* F */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* G */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    /* H */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* I */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* J */ {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    /* K */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* L */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* M */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* N */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* O */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* P */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* Q */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* R */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* S */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    /* T */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* U */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* V */ {0x11,0x11,0x11,0x11,0x0A,0x0A,0x04},
    /* W */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* X */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* Y */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* Z */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
};

#ifdef AMIGA3DO
/* 3DO port: each glyph is rendered once per scale (1..3) into a run-length
 * sprite (gfx_sprite_make); the ARM60 can't afford the original's one
 * RectFill per font pixel. Same pixels on screen. */
static GfxSprite *glyph_cache[3][36];

static const GfxSprite *glyph(int idx, int scale)
{
    GfxSprite **slot = &glyph_cache[scale - 1][idx];
    if (!*slot) {
        int w = 5 * scale, h = 7 * scale, row, col;
        UBYTE *m = (UBYTE *)AllocMem(w * h, MEMF_CLEAR);
        if (!m) return 0;
        for (row = 0; row < h; row++)
            for (col = 0; col < w; col++)
                if (font_data[idx][row / scale] & (0x10 >> (col / scale)))
                    m[row * w + col] = 1;
        *slot = gfx_sprite_make(m, w, h);
        FreeMem(m, w * h);
    }
    return *slot;
}
#endif

static void draw_char(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                       char ch, WORD scale)
{
    int idx, row, col;
    if (ch >= '0' && ch <= '9') idx = ch - '0';
    else if (ch >= 'A' && ch <= 'Z') idx = ch - 'A' + 10;
    else if (ch >= 'a' && ch <= 'z') idx = ch - 'a' + 10;
    else return;
#ifdef AMIGA3DO
    if (scale >= 1 && scale <= 3) {
        const GfxSprite *g = glyph(idx, scale);
        if (g) {
            gfx_sprite_draw(rp, g, x, y, color);
            return;
        }
    }
#endif

    SetAPen(rp, color);
    for (row = 0; row < 7; row++) {
        UBYTE bits = font_data[idx][row];
        for (col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                RectFill(rp, x + col * scale, y + row * scale,
                         x + col * scale + scale - 1,
                         y + row * scale + scale - 1);
            }
        }
    }
}

void draw_text_scaled(struct RastPort *rp, WORD x, WORD y, UBYTE color,
                      const char *text, WORD scale)
{
    while (*text) {
        if (*text == ' ')
            x += 6 * scale;
        else {
            draw_char(rp, x, y, color, *text, scale);
            x += 6 * scale;
        }
        text++;
    }
}

/* Center text at y position */
static void draw_text_centered(struct RastPort *rp, WORD y, UBYTE color,
                                const char *text, WORD scale)
{
    int len = 0;
    const char *p = text;
    WORD w;
    while (*p++) len++;
    w = len * 6 * scale;
    draw_text_scaled(rp, (SCREEN_W - w) / 2, y, color, text, scale);
}

/* Format a number into a string buffer, right-aligned in field_width chars */
static void format_number(char *buf, LONG val, int field_width)
{
    char tmp[12];
    int i = 0, j;
    LONG v = val;

    if (field_width <= 0) {
        if (buf) buf[0] = '\0';
        return;
    }

    if (v < 0) v = 0;

    if (v == 0) {
        tmp[i++] = '0';
    } else {
        while (v > 0 && i < (int)sizeof(tmp)) {
            tmp[i++] = '0' + (char)(v % 10);
            v /= 10;
        }
    }

    /* Clamp displayed digits to field_width so we never write before buf */
    if (i > field_width) i = field_width;

    /* Pad with spaces */
    for (j = 0; j < field_width - i; j++) buf[j] = ' ';
    /* Reverse digits (only the ones that fit) */
    for (j = 0; j < i; j++) buf[field_width - 1 - j] = tmp[j];
    buf[field_width] = '\0';
}

/*
 * Sprites as character art: '#' is the body colour, 'o' the accent
 * colour, anything else is transparent. All rows of a sprite are the
 * same width.
 */
#define ALIEN_ROWS_PX 10

/* Type C (rows 0-1, 30 pts): eye pod trailing tendrils */
static const char *const art_c[2][ALIEN_ROWS_PX] = {
    { "....######....",
      "..##########..",
      ".####oooo####.",
      "####oo..oo####",
      ".####oooo####.",
      "..##########..",
      "...#..##..#...",
      "..#...##...#..",
      ".#....##....#.",
      "..#........#.." },
    { "....######....",
      "..##########..",
      ".####oooo####.",
      "####o..oo#####",
      ".####oooo####.",
      "..##########..",
      "...#..##..#...",
      "....#.##.#....",
      "...#..##..#...",
      "....#....#...." }
};
/* Type B (rows 2-3, 20 pts): ray beating its wings */
static const char *const art_b[2][ALIEN_ROWS_PX] = {
    { "#............#",
      "##....##....##",
      "###..####..###",
      "##############",
      ".####o##o####.",
      "..##########..",
      "....##..##....",
      "...##....##...",
      "..............",
      ".............." },
    { "..............",
      "..............",
      "......##......",
      "#....####....#",
      "##############",
      ".####o##o####.",
      "..##########..",
      "....##..##....",
      "....#....#....",
      ".............." }
};
/* Type A (rows 4-5, 10 pts): spore crawler */
static const char *const art_a[2][ALIEN_ROWS_PX] = {
    { "......##......",
      ".....####.....",
      "....##oo##....",
      "...########...",
      "..##.####.##..",
      ".##..####..##.",
      "##...#..#...##",
      "....##..##....",
      "...##....##...",
      ".............." },
    { "......##......",
      ".....####.....",
      "....##oo##....",
      "...########...",
      "..##.####.##..",
      "..##.####.##..",
      "..#..#..#..#..",
      "...#.#..#.#...",
      "....#....#....",
      ".............." }
};
/* Player: delta fighter with a cockpit */
static const char *const art_player[PLAYER_H] = {
    ".......#.......",
    "......###......",
    "......#o#......",
    "..#..#####..#..",
    "..#.#######.#..",
    ".#############.",
    "###############",
    "##.##.....##.##"
};
/* Comet: the bonus target crossing the top */
static const char *const art_ufo[UFO_H] = {
    "........oo......",
    "......oooooo....",
    "####ooooooooo...",
    "..##############",
    "####ooooooooo...",
    "......oooooo....",
    "........oo......"
};

/* Colours per alien type: body, accent */
static const UBYTE alien_colors[3][2] = {
    { COL_MAGENTA, COL_YELLOW },   /* Type C */
    { COL_BRGREEN, COL_WHITE },    /* Type B */
    { COL_LTBLUE,  COL_ORANGE }    /* Type A */
};

/* Draw a character-art sprite: '#' in body, 'o' in accent */
static void draw_art(struct RastPort *rp, const char *const *art, int w, int h,
                     WORD x, WORD y, UBYTE body, UBYTE accent)
{
    int row, col;
#ifdef AMIGA3DO
    {
        /* 3DO port: one pre-rendered two-colour run-length sprite per art
         * (pen -1 draws each run in its own colour) */
        static const char *const *keys[16];
        static GfxSprite *cache[16];
        int i;
        for (i = 0; i < 16 && keys[i]; i++)
            if (keys[i] == art) break;
        if (i < 16) {
            if (!keys[i]) {
                UBYTE *m = (UBYTE *)AllocMem(w * h, MEMF_CLEAR);
                if (m) {
                    for (row = 0; row < h; row++)
                        for (col = 0; col < w; col++)
                            m[row * w + col] = art[row][col] == '#' ? body
                                             : art[row][col] == 'o' ? accent : 0;
                    cache[i] = gfx_sprite_make(m, w, h);
                    FreeMem(m, w * h);
                }
                keys[i] = art;
            }
            if (cache[i]) {
                gfx_sprite_draw(rp, cache[i], x, y, -1);
                return;
            }
        }
    }
#endif
    for (row = 0; row < h; row++) {
        col = 0;
        while (col < w) {
            char ch = art[row][col];
            if (ch == '#' || ch == 'o') {
                WORD start = col;
                while (col < w && art[row][col] == ch) col++;
                SetAPen(rp, ch == '#' ? body : accent);
                RectFill(rp, x + start, y + row, x + col - 1, y + row);
            } else {
                col++;
            }
        }
    }
}

static void draw_alien(struct RastPort *rp, WORD x, WORD y, WORD type,
                        WORD frame)
{
    const char *const *art;
    if (type == ALIEN_TYPE_C) art = art_c[frame & 1];
    else if (type == ALIEN_TYPE_B) art = art_b[frame & 1];
    else art = art_a[frame & 1];
    draw_art(rp, art, ALIEN_W, ALIEN_ROWS_PX, x, y,
             alien_colors[type][0], alien_colors[type][1]);
}

static void draw_player(struct RastPort *rp, WORD x, WORD y)
{
    draw_art(rp, art_player, PLAYER_W, PLAYER_H, x, y, COL_YELLOW, COL_CYAN);
}

static void draw_ufo(struct RastPort *rp, WORD x, WORD y)
{
    draw_art(rp, art_ufo, UFO_W, UFO_H, x, y, COL_DKORANGE, COL_WHITE);
}

static void draw_shields(struct RastPort *rp, GameState *gs)
{
    int s, y, x;
    for (s = 0; s < SHIELD_COUNT; s++) {
        Shield *sh = &gs->shields[s];
        SetAPen(rp, COL_CYAN);
        for (y = 0; y < SHIELD_H; y++) {
            x = 0;
            while (x < SHIELD_W) {
                WORD start;
                while (x < SHIELD_W && !sh->pixels[y][x]) x++;
                if (x >= SHIELD_W) break;
                start = x;
                while (x < SHIELD_W && sh->pixels[y][x]) x++;
                RectFill(rp, sh->x + start, sh->y + y,
                         sh->x + x - 1, sh->y + y);
            }
        }
        /* facet highlights */
        SetAPen(rp, COL_WHITE);
        for (y = 0; y < SHIELD_H; y++)
            for (x = 0; x < SHIELD_W; x++)
                if (sh->pixels[y][x] == 2)
                    WritePixel(rp, sh->x + x, sh->y + y);
    }
}

static void draw_explosions(struct RastPort *rp, GameState *gs)
{
    int i;
    for (i = 0; i < MAX_EXPLOSIONS; i++) {
        Explosion *ex = &gs->explosions[i];
        WORD size, cx, cy;
        if (!ex->active) continue;

        size = 10 - ex->timer;
        SetAPen(rp, ex->color);

        /* Expanding cross pattern */
        cx = ex->x; cy = ex->y;
        if (size > 0) {
            RectFill(rp, cx - size, cy - 1, cx + size, cy + 1);
            RectFill(rp, cx - 1, cy - size, cx + 1, cy + size);
        }
        /* Dots at diagonals */
        if (size > 2) {
            WORD d = size - 1;
            WritePixel(rp, cx - d, cy - d);
            WritePixel(rp, cx + d, cy - d);
            WritePixel(rp, cx - d, cy + d);
            WritePixel(rp, cx + d, cy + d);
        }
    }
}

static void draw_hud(struct RastPort *rp, GameState *gs)
{
    char buf[20];
    int i;

    /* Wave, top right */
    draw_text_scaled(rp, 262, 4, COL_GRAY, "WAVE", 1);
    format_number(buf, (LONG)gs->wave, 2);
    draw_text_scaled(rp, 290, 4, COL_WHITE, buf, 1);

    /* Bottom bar under the ground line: points, best, reserve ships */
    SetAPen(rp, COL_CYAN);
    RectFill(rp, 0, 242, SCREEN_W - 1, 242);

    draw_text_scaled(rp, 6, 246, COL_GRAY, "PTS", 1);
    format_number(buf, gs->score, 6);
    draw_text_scaled(rp, 26, 246, COL_YELLOW, buf, 1);

    draw_text_scaled(rp, 116, 246, COL_GRAY, "BEST", 1);
    format_number(buf, gs->hiscore, 6);
    draw_text_scaled(rp, 142, 246, COL_YELLOW, buf, 1);

    for (i = 0; i < gs->lives - 1 && i < 4; i++) {
        WORD lx = SCREEN_W - 20 - i * 18;
        draw_player(rp, lx, 246);
    }
}

void draw_game(struct RastPort *rp, GameState *gs)
{
    int r, c, i;

    /* Clear screen */
    SetRast(rp, 0);

    /* Draw HUD */
    draw_hud(rp, gs);

    /* Draw shields */
    draw_shields(rp, gs);

    /* Draw aliens */
    for (r = 0; r < ALIEN_ROWS; r++) {
        WORD type;
        if (r <= 1) type = ALIEN_TYPE_C;
        else if (r <= 3) type = ALIEN_TYPE_B;
        else type = ALIEN_TYPE_A;

        for (c = 0; c < ALIEN_COLS; c++) {
            if (!gs->swarm.alive[r][c]) continue;
            /* neighbouring columns flap out of step */
            draw_alien(rp, ALIEN_X(gs, r, c), ALIEN_Y(gs, r), type,
                       gs->swarm.anim_frame ^ (c & 1));
        }
    }

    /* Draw UFO */
    if (gs->ufo.active) {
        draw_ufo(rp, gs->ufo.x, UFO_Y);
    }

    /* Draw player (unless dying and blinking) */
    if (gs->state != STATE_DYING || (gs->state_timer & 4)) {
        draw_player(rp, gs->player_x, PLAYER_Y);
    }

    /* Draw player bullet */
    if (gs->player_bullet.active) {
        SetAPen(rp, COL_CYAN);
        RectFill(rp, gs->player_bullet.x - 1, gs->player_bullet.y,
                 gs->player_bullet.x, gs->player_bullet.y + 5);
        SetAPen(rp, COL_WHITE);
        RectFill(rp, gs->player_bullet.x - 1, gs->player_bullet.y,
                 gs->player_bullet.x, gs->player_bullet.y + 1);
    }

    /* Draw alien bullets */
    SetAPen(rp, COL_MAGENTA);
    for (i = 0; i < MAX_ALIEN_BULLETS; i++) {
        WORD bx, by;
        if (!gs->alien_bullets[i].active) continue;
        bx = gs->alien_bullets[i].x;
        by = gs->alien_bullets[i].y;
        /* Spinning spore: alternates a plus and an x */
        if (by & 4) {
            RectFill(rp, bx - 1, by, bx + 1, by + 2);
            WritePixel(rp, bx, by - 1);
            WritePixel(rp, bx, by + 3);
        } else {
            WritePixel(rp, bx - 1, by);
            WritePixel(rp, bx + 1, by);
            WritePixel(rp, bx, by + 1);
            WritePixel(rp, bx - 1, by + 2);
            WritePixel(rp, bx + 1, by + 2);
        }
    }

    /* Draw explosions */
    draw_explosions(rp, gs);

    /* Wave clear message */
    if (gs->state == STATE_WAVE_CLEAR) {
        draw_text_centered(rp, 120, COL_YELLOW, "WAVE CLEAR", 2);
    }
}

void draw_title(struct RastPort *rp, GameState *gs)
{
    SetRast(rp, 0);

    /* Title */
    draw_text_centered(rp, 24, COL_MAGENTA, "NOVA", 3);
    draw_text_centered(rp, 54, COL_CYAN, "DEFENSE", 3);

    /* The invaders in a line, with their points underneath */
    {
        WORD f = (gs->title_blink >> 4) & 1;
        draw_text_centered(rp, 92, COL_GRAY, "KNOW YOUR ENEMY", 1);
        draw_alien(rp, 57, 110, ALIEN_TYPE_C, f);
        draw_text_scaled(rp, 55, 126, COL_WHITE, "30", 1);
        draw_alien(rp, 117, 110, ALIEN_TYPE_B, f);
        draw_text_scaled(rp, 115, 126, COL_WHITE, "20", 1);
        draw_alien(rp, 177, 110, ALIEN_TYPE_A, f);
        draw_text_scaled(rp, 175, 126, COL_WHITE, "10", 1);
        draw_ufo(rp, 236, 112);
        draw_text_scaled(rp, 232, 126, COL_WHITE, "COMET", 1);
        draw_player(rp, SCREEN_W / 2 - PLAYER_W / 2, 152);
    }

    /* Hi-score */
    if (gs->hiscore > 0) {
        char buf[20];
        draw_text_centered(rp, 182, COL_GRAY, "BEST", 1);
        format_number(buf, gs->hiscore, 6);
        draw_text_centered(rp, 192, COL_YELLOW, buf, 1);
    }

    /* Blinking "PRESS FIRE" */
    if ((gs->title_blink >> 3) & 1) {
        draw_text_centered(rp, 215, COL_WHITE, "PRESS A TO START", 1);
    }

    /* Credits */
    draw_text_centered(rp, 240, COL_GRAY, "DPAD MOVE  A FIRE  X QUIT", 1);
}

void draw_gameover(struct RastPort *rp, GameState *gs)
{
    char buf[20];

    SetRast(rp, 0);

    draw_text_centered(rp, 80, COL_RED, "GAME OVER", 3);

    draw_text_centered(rp, 130, COL_WHITE, "SCORE", 2);
    format_number(buf, gs->score, 6);
    draw_text_centered(rp, 155, COL_YELLOW, buf, 2);

    draw_text_centered(rp, 185, COL_GRAY, "WAVE", 1);
    format_number(buf, (LONG)gs->wave, 2);
    draw_text_centered(rp, 195, COL_WHITE, buf, 1);

    if ((gs->state_timer & 8)) {
        draw_text_centered(rp, 225, COL_WHITE, "PRESS A", 1);
    }
}
