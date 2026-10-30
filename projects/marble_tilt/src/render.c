// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Marble Tilt's picture: the board floating over a dark void,
 * tilted with the stick (shown at twice the tilt, so a few degrees read),
 * its floor in runs a row at a time, the walls as blocks (runs again),
 * the gates rising and sinking, the bumpers, the exit and checkpoints,
 * the marble with its shadow (dropping away through a hole when it
 * falls), and the clock, the level and a tilt gauge.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "mt.h"
#include "tilt.h"

/* 0 black, 1 white, 2 panel, 3 grey, 4 the void (gradient), 5-6 floor
 * (alternate rows), 7 the slab's edge, 8-9 walls (top, side), 10 sand,
 * 11 checkpoint, 12 exit, 13-14 gates (shut, open), 15-16 bumpers,
 * 17-18 the marble (body, shine), 19 shadow, 20 the start, 26 red,
 * 27 yellow, 28 green, 29 cyan */
const uint16_t tilt_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0x113, 0x9AB, 0x89A, 0x445, 0x6A8, 0x386, 0xCA6, 0x48E, 0x3D5,
    0xE44, 0x844, 0xF53, 0xA32, 0xDDE, 0xFFF, 0x334, 0xCC8, 0x000, 0x000, 0x000, 0x000, 0x000,
    0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000
};
#define PEN_VOID   4
#define PEN_FLOOR  5
#define PEN_EDGE   7
#define PEN_WTOP   8
#define PEN_WSIDE  9
#define PEN_SAND   10
#define PEN_CP     11
#define PEN_EXIT   12
#define PEN_GATE   13
#define PEN_BUMPER 15
#define PEN_MARBLE 17
#define PEN_SHINE  18
#define PEN_SHADOW 19
#define PEN_START  20
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29

#define WALL_H 22
#define HALF   (MT_N * MT_CELL / 2)

static Cam cam;
static int32_t cr, sr, cp, sp;          /* the board's tilt (16.16 cos and sin) */

/* a point on the board to the camera: rolled, then pitched */
static void bpt(int32_t x, int32_t y, int32_t z, V3 *out)
{
    int32_t x1 = (x * cr + y * sr) >> 16, y1 = (y * cr - x * sr) >> 16;
    int32_t z2 = (z * cp + y1 * sp) >> 16, y2 = (y1 * cp - z * sp) >> 16;
    cam_xform(&cam, x1, y2, z2, out);
}

static void bquad(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                  int32_t x2, int32_t y2, int32_t z2, int32_t x3, int32_t y3, int32_t z3, int32_t bias, int col)
{
    V3 a, b, c, d;
    bpt(x0, y0, z0, &a); bpt(x1, y1, z1, &b); bpt(x2, y2, z2, &c); bpt(x3, y3, z3, &d);
    a3_quad(&cam, ((a.z + b.z + c.z + d.z) >> 2) + bias, col, a, b, c, d);
}

/* a flat rectangle on the board at height y (x0 < x1, z0 < z1) */
static void tile(int32_t x0, int32_t x1, int32_t z0, int32_t z1, int32_t y, int col, int32_t bias)
{
    bquad(x0, y, z0, x1, y, z0, x1, y, z1, x0, y, z1, bias, col);
}

/* a block: its top, its near side (-z), and the end facing the camera */
static void block(int32_t x0, int32_t x1, int32_t z0, int32_t z1, int32_t h, int top, int side, int near_side)
{
    tile(x0, x1, z0, z1, h, top, -2);
    if (near_side) bquad(x0, 0, z0, x1, 0, z0, x1, h, z0, x0, h, z0, -1, side);
    if (x1 < 0) bquad(x1, 0, z0, x1, 0, z1, x1, h, z1, x1, h, z0, -1, side);
    if (x0 > 0) bquad(x0, 0, z0, x0, 0, z1, x0, h, z1, x0, h, z0, -1, side);
}

static int floor_pen(char ch, int row)
{
    switch (ch) {
    case 'Z': return PEN_SAND;
    case 'C': return PEN_CP;
    case 'E': return PEN_EXIT;
    case 'S': return PEN_START;
    case ' ': case '#': return -1;
    }
    return PEN_FLOOR + (row & 1);
}

static void board(Canvas *cv, const MTGame *g)
{
    int r, c;
    /* the floor, in runs along each row; then the slab's front edge */
    a3_begin(2000);
    for (r = 0; r < MT_N; r++) {
        int32_t z1 = (mt_cell_z(r) >> 16) + MT_CELL / 2, z0 = z1 - MT_CELL;
        for (c = 0; c < MT_N;) {
            int pen = floor_pen(g->cell[r][c], r), e = c + 1;
            if (pen < 0) { c++; continue; }
            while (e < MT_N && floor_pen(g->cell[r][e], r) == pen) e++;
            tile(-HALF + c * MT_CELL, -HALF + e * MT_CELL, z0, z1, 0, pen, 40);
            c = e;
        }
    }
    bquad(-HALF, 0, -HALF, HALF, 0, -HALF, HALF, -16, -HALF, -HALF, -16, -HALF, 30, PEN_EDGE);
    a3_flush(cv);
}

static void blocks(const MTGame *g)
{
    int r, c;
    for (r = 0; r < MT_N; r++) {
        int32_t z1 = (mt_cell_z(r) >> 16) + MT_CELL / 2, z0 = z1 - MT_CELL;
        for (c = 0; c < MT_N;) {
            int e = c + 1, near_side;
            int32_t x0 = -HALF + c * MT_CELL;
            char ch = g->cell[r][c];
            if (ch == 'G') {
                block(x0, x0 + MT_CELL, z0, z1, g->gates_open ? 4 : WALL_H, g->gates_open ? PEN_GATE + 1 : PEN_GATE, PEN_GATE + 1, 1);
                c++;
                continue;
            }
            if (ch == 'B') {
                V3 p[8], q[8];
                int k;
                int32_t cx = x0 + MT_CELL / 2, cz = z0 + MT_CELL / 2;
                for (k = 0; k < 8; k++) {
                    int a = k * 8192 + 4096;
                    int32_t px = cx + ((a3_sin(a) * 13) >> 16), pz = cz + ((a3_cos(a) * 13) >> 16);
                    bpt(px, 18, pz, &p[k]);
                    bpt(px, 0, pz, &q[k]);
                }
                for (k = 0; k < 8; k++) {      /* its sides (the top, drawn last, covers the far ones) */
                    int j = (k + 1) & 7;
                    V3 s[4];
                    s[0] = q[k]; s[1] = q[j]; s[2] = p[j]; s[3] = p[k];
                    a3_poly(&cam, ((q[k].z + q[j].z) >> 1), PEN_BUMPER + 1, s, 4);
                }
                a3_poly(&cam, ((p[0].z + p[4].z) >> 1) - 6, PEN_BUMPER, p, 8);
                c++;
                continue;
            }
            if (ch != '#') { c++; continue; }
            while (e < MT_N && g->cell[r][e] == '#') e++;
            /* its near side shows unless a wall is in front of all of it */
            near_side = r == MT_N - 1;
            {
                int k;
                for (k = c; k < e && !near_side; k++) if (g->cell[r + 1][k] != '#') near_side = 1;
            }
            block(x0, -HALF + e * MT_CELL, z0, z1, WALL_H, PEN_WTOP, PEN_WSIDE, near_side);
            c = e;
        }
    }
}

static void marble(const MTGame *g)
{
    static const int8_t oct[8][2] = { { 9, 0 }, { 6, 6 }, { 0, 9 }, { -6, 6 }, { -9, 0 }, { -6, -6 }, { 0, -9 }, { 6, -6 } };
    int32_t x = g->x >> 16, z = g->z >> 16, y = MT_R, r = 9;
    V3 c, p[8];
    int k;
    if (g->falling) {                    /* dropping away below the board */
        int32_t t = MT_FALL_T - g->falling;
        y = MT_R - t * t;
        r = 9 - t / 4;
        if (r < 2) r = 2;
    } else {
        V3 s[8];
        for (k = 0; k < 8; k++) bpt(x + oct[k][0] + 3, 1, z + oct[k][1] - 3, &s[k]);
        a3_poly(&cam, s[0].z + 30, PEN_SHADOW, s, 8);
    }
    bpt(x, y, z, &c);
    if (c.z < NEAR_Z + 10) return;
    for (k = 0; k < 8; k++) { p[k].x = c.x + oct[k][0] * r / 9; p[k].y = c.y + oct[k][1] * r / 9; p[k].z = c.z; }
    a3_poly(&cam, c.z - 10, PEN_MARBLE, p, 8);
    for (k = 0; k < 8; k++) { p[k].x = c.x - 3 + oct[k][0] * r / 30; p[k].y = c.y + 3 + oct[k][1] * r / 30; p[k].z = c.z; }
    a3_poly(&cam, c.z - 11, PEN_SHINE, p, 8);
}

static void scene(Canvas *cv, const MTGame *g)
{
    int ar = a3_angle(g->roll * 2), ap = a3_angle(g->pitch * 2);
    cr = a3_cos(ar); sr = a3_sin(ar);
    cp = a3_cos(ap); sp = a3_sin(ap);
    cam.x = 0;
    cam.y = 470;
    cam.z = -430;
    cam.yaw = 0;
    cam.pitch = 7600;
    cam_view(&cam, SCREEN_W, SCREEN_H, SCREEN_W / 2, SCREEN_H / 2 + 6, 266);
    cam_setup(&cam);
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_VOID);
    g_sync();
    board(cv, g);
    a3_begin(2000);
    blocks(g);
    marble(g);
    a3_flush(cv);
}

static void hud(Canvas *cv, const MTGame *g)
{
    char tmp[48];
    int gx = SCREEN_W - 26, gy = 26;
    sprintf(tmp, "%ld %s", (long)g->level, mt_level_names[g->level - 1]);
    g_text_shadow(cv, 4, 10, PEN_YELLOW, tmp);
    sprintf(tmp, "%ld", (long)(g->time_left / 25));
    g_text_big(cv, 4, 16, g->time_left < 250 && (g->tick & 8) ? PEN_RED : 1, tmp, 2);
    sprintf(tmp, "SCORE %ld", (long)g->score);
    g_text_shadow(cv, 4, 250, 3, tmp);
    if (g->falls) { sprintf(tmp, "FALLS %ld", (long)g->falls); g_text_shadow(cv, 120, 250, 3, tmp); }
    /* the tilt gauge */
    g_circle(cv, gx, gy, 18, 2);
    g_rect(cv, gx - 18, gy, gx + 18, gy, 3);
    g_rect(cv, gx, gy - 18, gx, gy + 18, 3);
    g_circle(cv, gx + (int)(g->roll * 15 / MT_TILT_MAX), gy - (int)(g->pitch * 15 / MT_TILT_MAX), 3, PEN_YELLOW);
    if (!g->gates_open) g_text_shadow(cv, 230, 250, PEN_RED, "GATES SHUT");
    if (tl.autoplay) g_text_shadow(cv, 230, 238, PEN_GREEN, "AUTO");
}

void render_game(Canvas *cv, const MTGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (g->between) {
        g_text_big(cv, 88, 90, PEN_GREEN, "THROUGH!", 2);
        if (g->level < MT_LEVELS) { sprintf(tmp, "NEXT: %s", mt_level_names[g->level]); g_text_centre(cv, 120, 1, tmp); }
    }
    if (tl.state == TS_PLAY && g->level_ticks < 50 && !g->between) {
        sprintf(tmp, "LEVEL %ld: %s", (long)g->level, mt_level_names[g->level - 1]);
        g_text_centre(cv, 120, PEN_YELLOW, tmp);
    }
    if (tl.state == TS_OVER) {
        g_box(cv, 50, 80, 270, 160, 2, g->won ? PEN_GREEN : PEN_RED);
        g_text_big(cv, g->won ? 66 : 76, 88, g->won ? PEN_GREEN : PEN_RED, g->won ? "ALL THROUGH" : "TIME UP", 2);
        sprintf(tmp, "LEVEL %ld  FALLS %ld", (long)g->level, (long)g->falls);
        g_text_centre(cv, 122, PEN_YELLOW, tmp);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 136, 1, tmp);
#ifdef AMIGA3DO
        if (tl.state_timer == 0) g_text_centre(cv, 152, PEN_YELLOW, "PRESS A");
#else
        if (tl.state_timer == 0) g_text_centre(cv, 152, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (tl.paused == 1) g_text_centre(cv, 128, 1, "PAUSED");
    if (tl.flash_timer > 0 && tl.flash[0]) g_text_centre(cv, 60, tl.flash_pen, tl.flash);
}

void render_title(Canvas *cv, const MTGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot rolls behind the title */
    g_text_big(cv, 50, 14, 0, "MARBLE", 4);
    g_text_big(cv, 48, 12, PEN_YELLOW, "MARBLE", 4);
    g_text_big(cv, 98, 50, 0, "TILT", 4);
    g_text_big(cv, 96, 48, PEN_CYAN, "TILT", 4);
    g_box(cv, 40, 200, 280, 254, 2, 3);
    sprintf(tmp, "LEVEL %ld: %s", (long)tl.start_level, mt_level_names[tl.start_level - 1]);
    g_text_centre(cv, 214, PEN_GREEN, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 230, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO PLAY");
#else
    g_text_centre(cv, 230, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO PLAY");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld   C: HOW TO PLAY", (long)tl.best);
#else
    sprintf(tmp, "BEST %ld   HELP: HOW TO PLAY", (long)tl.best);
#endif
    g_text_centre(cv, 246, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "TILT THE BOARD TO ROLL THE MARBLE TO",
      "THE GREEN EXIT BEFORE THE CLOCK RUNS",
      "OUT. IT KEEPS ROLLING: TILT BACK TO",
      "SLOW IT DOWN.",
      "",
      " HOLES    FALL IN: BACK TO THE LAST",
      "          CHECKPOINT (BLUE), -3 S",
      " RED POSTS   BUMPERS: THEY KICK IT",
      " RED BLOCKS  GATES: OPEN 3 S, SHUT 3 S",
      " TAN       SAND: IT DRAGS",
      "",
      "EIGHT LEVELS. SCORE: 10 A SECOND LEFT." },
#ifdef AMIGA3DO
    { "D-PAD                     TILT",
#else
    { "ARROWS / WASD / JOYSTICK  TILT",
#endif
      "UP               ROLLS IT AWAY",
      "DOWN             ROLLS IT TOWARDS YOU",
      "P                PAUSE",
      "B                AUTOPILOT",
#ifdef AMIGA3DO
      "C                THESE PAGES",
#else
      "HELP / H         THESE PAGES",
#endif
#ifdef AMIGA3DO
      "L / X            TITLE / MENU",
#else
      "ESC              TITLE / QUIT",
#endif
      "",
      "THE GAUGE AT THE TOP RIGHT SHOWS",
      "THE TILT. EACH LEVEL DONE SCORES",
      "100 x THE LEVEL AND 10 FOR EACH",
      "SECOND LEFT." },
};

void render_help(Canvas *cv, int page)
{
    int k;
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, 2);
    g_sync();
    g_text_big(cv, page == 1 ? 72 : 96, 14, PEN_YELLOW, page == 1 ? "HOW TO PLAY" : "CONTROLS", 2);
    for (k = 0; k < 12; k++) g_text(cv, 8, 58 + k * 14, 1, help_lines[page - 1][k]);
    g_text(cv, 8, 242, 3, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2");
#ifdef AMIGA3DO
    g_text(cv, 156, 242, PEN_YELLOW, "C: NEXT  A: PLAY");
#else
    g_text(cv, 156, 242, PEN_YELLOW, "HELP: NEXT  FIRE: PLAY");
#endif
}
