// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Block Breaker 3D's picture: the tunnel (striped along its
 * length so its depth reads), the bricks (only the faces you can see:
 * the front, and the sides and tops that face the camera with no brick
 * against them), the ball with its shadow and a ring round the tunnel at
 * its depth, the capsules, the bits of broken bricks, the paddle's frame,
 * and the score line, title, help and game-over screens.
 *
 * The camera sits behind the paddle and leans a little with it.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "bb.h"
#include "breaker.h"

/* palette: 0 black, 1 white, 2 shadow, 3 grey, 4 the back wall, 5-6 the
 * floor's stripes, 7-8 the ceiling's, 9-10 the side walls', 11-22 the
 * coloured bricks (lit, shaded), 23-24 tough, 25-26 steel, 27 the ball,
 * 28 the paddle, 29 capsules and bonus bricks, 30 the depth ring, 31 yellow */
const uint16_t breaker_palette[32] = {
    0x000, 0xFFF, 0x102, 0x99A, 0x214, 0x325, 0x436, 0x213, 0x324, 0x224, 0x335,
    0xF33, 0xA12, 0xF83, 0xB52, 0xFE4, 0xBA2, 0x5E4, 0x2A3, 0x3DF, 0x19A, 0x56F, 0x23B,
    0xCCD, 0x889, 0x99A, 0x556, 0xFFE, 0x4FD, 0xF4E, 0x6F6, 0xFD3
};
#define PEN_BACK   4
#define PEN_FLOOR  5
#define PEN_CEIL   7
#define PEN_SIDE   9
#define PEN_BRICK  11
#define PEN_TOUGH  23
#define PEN_STEEL  25
#define PEN_BALL   27
#define PEN_PADDLE 28
#define PEN_CAP    29
#define PEN_RING   30
#define PEN_YELLOW 31
#define PEN_RED    11
#define PEN_GREEN  17
#define PEN_CYAN   19

#define WALL_Z0  (-200)                 /* the tunnel starts behind the paddle */
#define SEG      100                    /* stripes along it */

static Cam cam;

int brick_pen(int type, int hits)
{
    if (type >= BK_RED && type <= BK_BLUE) return PEN_BRICK + 2 * (type - BK_RED);
    if (type == BK_TOUGH) return hits > 1 ? PEN_TOUGH : PEN_TOUGH + 1;
    if (type == BK_STEEL) return PEN_STEEL;
    return PEN_CAP;
}
static int brick_shade(int type, int hits)
{
    if (type == BK_TOUGH && hits <= 1) return 3;
    if (type == BK_BONUS) return PEN_BRICK + 11;
    return brick_pen(type, hits) + 1;
}

static void quad(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                 int32_t x2, int32_t y2, int32_t z2, int32_t x3, int32_t y3, int32_t z3, int32_t depth, int col)
{
    V3 a, b, c, d;
    cam_xform(&cam, x0, y0, z0, &a);
    cam_xform(&cam, x1, y1, z1, &b);
    cam_xform(&cam, x2, y2, z2, &c);
    cam_xform(&cam, x3, y3, z3, &d);
    a3_quad(&cam, depth - cam.z, col, a, b, c, d);
}

/* a flat rectangle facing the camera at depth z */
static void card(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t z, int32_t depth, int col)
{
    quad(x0, y0, z, x1, y0, z, x1, y1, z, x0, y1, z, depth, col);
}

static void tunnel(Canvas *cv)
{
    int32_t z;
    int k = 0;
    a3_begin(1400);
    for (z = WALL_Z0; z < BB_D; z += SEG, k++) {
        int32_t z1 = z + SEG > BB_D ? BB_D : z + SEG, d = z + SEG / 2;
        int s = k & 1;
        quad(-BB_W, -BB_H, z, BB_W, -BB_H, z, BB_W, -BB_H, z1, -BB_W, -BB_H, z1, d, PEN_FLOOR + s);
        quad(-BB_W, BB_H, z, BB_W, BB_H, z, BB_W, BB_H, z1, -BB_W, BB_H, z1, d, PEN_CEIL + s);
        quad(-BB_W, -BB_H, z, -BB_W, BB_H, z, -BB_W, BB_H, z1, -BB_W, -BB_H, z1, d, PEN_SIDE + s);
        quad(BB_W, -BB_H, z, BB_W, BB_H, z, BB_W, BB_H, z1, BB_W, -BB_H, z1, d, PEN_SIDE + s);
    }
    card(-BB_W, -BB_H, BB_W, BB_H, BB_D, BB_D + 50, PEN_BACK);
    a3_flush(cv);
}

static int empty_at(const BBGame *g, int c, int r, int l)
{
    if (c < 0 || c >= BB_COLS || r < 0 || r >= BB_ROWS || l < 0 || l >= BB_LAYERS) return 1;
    return g->cell[bb_index(c, r, l)] == BK_EMPTY;
}

static void bricks(const BBGame *g)
{
    int i;
    for (i = 0; i < BB_CELLS; i++) {
        int t = g->cell[i], c = i % BB_COLS, r = (i / BB_COLS) % BB_ROWS, l, lit, sh;
        int32_t x0, y0, z0, x1, y1, z1, mz;
        if (t == BK_EMPTY) continue;
        l = bb_cell_box(i, &x0, &y0, &z0);
        x1 = x0 + BB_CW; y1 = y0 + BB_CH; z1 = z0 + BB_CD; mz = z0 + BB_CD / 2;
        lit = brick_pen(t, g->hits[i]);
        sh = brick_shade(t, g->hits[i]);
        if (empty_at(g, c, r, l - 1)) {
            card(x0, y0, x1, y1, z0, z0, sh);                         /* the front: a rim */
            card(x0 + 5, y0 + 5, x1 - 5, y1 - 5, z0 - 1, z0 - 1, lit);     /* and its face */
        }
        if (x0 > cam.x && empty_at(g, c - 1, r, l)) quad(x0, y0, z0, x0, y1, z0, x0, y1, z1, x0, y0, z1, mz, sh);
        if (x1 < cam.x && empty_at(g, c + 1, r, l)) quad(x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1, mz, sh);
        if (y1 < cam.y && empty_at(g, c, r + 1, l)) quad(x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1, mz, lit);
        if (y0 > cam.y && empty_at(g, c, r - 1, l)) quad(x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1, mz, sh);
    }
}

static void ball(const BBGame *g)
{
    static const int8_t oct[8][2] = { {10,0},{7,7},{0,10},{-7,7},{-10,0},{-7,-7},{0,-10},{7,-7} };
    int32_t x = g->bx / BB_FX, y = g->by / BB_FX, z = g->bz / BB_FX;
    V3 p[8];
    int k;
    /* where it is along the tunnel: a ring round the walls, and its shadow */
    quad(-BB_W, -BB_H + 1, z - 3, BB_W, -BB_H + 1, z - 3, BB_W, -BB_H + 1, z + 3, -BB_W, -BB_H + 1, z + 3, z + 40, PEN_RING);
    quad(-BB_W, BB_H - 1, z - 3, BB_W, BB_H - 1, z - 3, BB_W, BB_H - 1, z + 3, -BB_W, BB_H - 1, z + 3, z + 40, PEN_RING);
    quad(-BB_W + 1, -BB_H, z - 3, -BB_W + 1, BB_H, z - 3, -BB_W + 1, BB_H, z + 3, -BB_W + 1, -BB_H, z + 3, z + 40, PEN_RING);
    quad(BB_W - 1, -BB_H, z - 3, BB_W - 1, BB_H, z - 3, BB_W - 1, BB_H, z + 3, BB_W - 1, -BB_H, z + 3, z + 40, PEN_RING);
    quad(x - 9, -BB_H + 2, z - 9, x + 9, -BB_H + 2, z - 9, x + 9, -BB_H + 2, z + 9, x - 9, -BB_H + 2, z + 9, z + 30, 2);
    for (k = 0; k < 8; k++) cam_xform(&cam, x + oct[k][0], y + oct[k][1], z, &p[k]);
    a3_poly(&cam, z - cam.z - 2, g->plough_t ? PEN_RED : PEN_BALL, p, 8);
    for (k = 0; k < 8; k++) cam_xform(&cam, x - 3 + oct[k][0] / 3, y + 3 + oct[k][1] / 3, z - 1, &p[k]);
    a3_poly(&cam, z - cam.z - 3, 1, p, 8);
}

static void capsules(const BBGame *g)
{
    int k;
    for (k = 0; k < BB_MAX_CAPS; k++) {
        const Capsule *c = &g->cap[k];
        int32_t x, y, z;
        if (!c->active) continue;
        x = c->x / BB_FX; y = c->y / BB_FX; z = c->z / BB_FX;
        card(x - 16, y - 10, x + 16, y + 10, z, z, PEN_CAP);
        card(x - 11, y - 5, x + 11, y + 5, z - 1, z - 1, c->type == CAP_LIFE ? PEN_GREEN : c->type == CAP_PLOUGH ? PEN_RED :
             c->type == CAP_SLOW ? PEN_CYAN : PEN_YELLOW);
    }
}

static void parts(void)
{
    int k;
    for (k = 0; k < MAX_PARTS; k++) {
        const Part *p = &br.part[k];
        int32_t x, y, z;
        if (p->life <= 0) continue;
        x = p->x >> 4; y = p->y >> 4; z = p->z >> 4;
        card(x - 4, y - 4, x + 4, y + 4, z, z - 2, p->col);
    }
}

static void paddle(const BBGame *g)
{
    int32_t x = g->px / BB_FX, y = g->py / BB_FX, w = bb_pad_w(g), h = bb_pad_h(g), t = 5;
    int col = g->plough_t && (g->tick & 2) ? PEN_RED : PEN_PADDLE;
    card(x - w, y + h - t, x + w, y + h, 0, -10, col);
    card(x - w, y - h, x + w, y - h + t, 0, -10, col);
    card(x - w, y - h, x - w + t, y + h, 0, -10, col);
    card(x + w - t, y - h, x + w, y + h, 0, -10, col);
    card(x - 6, y - 1, x + 6, y + 1, 0, -10, col);
    card(x - 1, y - 6, x + 1, y + 6, 0, -10, col);
}

static void capsule_letters(Canvas *cv, const BBGame *g)
{
    static const char *const letter[CAP_COUNT] = { "W", "S", "L", "P" };
    int k;
    for (k = 0; k < BB_MAX_CAPS; k++) {
        const Capsule *c = &g->cap[k];
        V3 p;
        int32_t sx, sy;
        if (!c->active) continue;
        cam_xform(&cam, c->x / BB_FX, c->y / BB_FX, c->z / BB_FX, &p);
        if (p.z < 60) continue;
        cam_project(&cam, &p, &sx, &sy);
        g_text(cv, (int)sx - 3, (int)sy + 3, 0, letter[c->type]);
    }
}

static void scene(Canvas *cv, const BBGame *g)
{
    cam.x = g->px / BB_FX / 5;
    cam.y = g->py / BB_FX / 5;
    cam.z = -320;
    cam.yaw = 0;
    cam.pitch = 0;
    cam_view(&cam, SCREEN_W, SCREEN_H, SCREEN_W / 2, SCREEN_H / 2, 270);
    cam_setup(&cam);
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, 0);
    g_sync();
    tunnel(cv);
    a3_begin(1400);
    bricks(g);
    capsules(g);
    parts();
    if (!(g->between && g->stuck)) ball(g);
    paddle(g);
    a3_flush(cv);
    capsule_letters(cv, g);
}

static void hud(Canvas *cv, const BBGame *g)
{
    char tmp[48];
    int k, x = 210;
    sprintf(tmp, "SCORE %ld", (long)g->score);
    g_text_shadow(cv, 4, 10, 1, tmp);
    sprintf(tmp, "LEVEL %ld", (long)g->level);
    g_text_shadow(cv, 128, 10, PEN_YELLOW, tmp);
    for (k = 0; k < g->lives && k < 8; k++, x += 13) g_circle(cv, x + 4, 6, 4, PEN_BALL);
    x = 4;
    if (g->wide_t) { g_text_shadow(cv, x, 250, PEN_YELLOW, "WIDE"); x += 44; }
    if (g->slow_t) { g_text_shadow(cv, x, 250, PEN_CYAN, "SLOW"); x += 44; }
    if (g->plough_t) { g_text_shadow(cv, x, 250, PEN_RED, "PLOUGH"); x += 60; }
    if (br.autoplay) g_text_shadow(cv, 250, 250, PEN_GREEN, "AUTO");
}

void render_game(Canvas *cv, const BBGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (g->stuck && !g->between && br.state == BS_PLAY) {
        sprintf(tmp, "LEVEL %ld: %s", (long)g->level, bb_level_names[g->level - 1]);
        g_text_centre(cv, 92, PEN_YELLOW, tmp);
        g_text_centre(cv, 172, 1, "FIRE TO SERVE");
    }
    if (g->between) g_text_big(cv, 72, 100, PEN_GREEN, "CLEARED!", 3);
    if (br.state == BS_OVER) {
        g_box(cv, 60, 90, 260, 170, 2, g->won ? PEN_GREEN : PEN_RED);
        g_text_big(cv, g->won ? 92 : 76, 98, g->won ? PEN_GREEN : PEN_RED, g->won ? "YOU WIN" : "GAME OVER", 2);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 140, 1, tmp);
#ifdef AMIGA3DO
        if (br.state_timer == 0) g_text_centre(cv, 158, PEN_YELLOW, "PRESS A");
#else
        if (br.state_timer == 0) g_text_centre(cv, 158, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (br.paused == 1) g_text_centre(cv, 128, 1, "PAUSED");
    if (br.flash_timer > 0 && br.flash[0]) g_text_centre(cv, 40, PEN_YELLOW, br.flash);
}

void render_title(Canvas *cv, const BBGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot plays behind the title */
    g_text_big(cv, 26, 30, 0, "BLOCK BREAKER", 2);
    g_text_big(cv, 24, 28, PEN_YELLOW, "BLOCK BREAKER", 2);
    g_text_big(cv, 128, 58, 0, "3D", 4);
    g_text_big(cv, 126, 56, PEN_CYAN, "3D", 4);
    sprintf(tmp, "START AT LEVEL %ld  (UP/DOWN)", (long)br.start_level);
    g_text_centre(cv, 186, PEN_GREEN, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 204, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO PLAY");
#else
    g_text_centre(cv, 204, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO PLAY");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld     C: HOW TO PLAY", (long)br.best);
#else
    sprintf(tmp, "BEST %ld     HELP: HOW TO PLAY", (long)br.best);
#endif
    g_text_centre(cv, 222, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "THE BALL FLIES DOWN THE TUNNEL TO THE",
      "BRICKS AT THE FAR END AND BACK. MOVE THE",
      "PADDLE ANY WAY TO MEET IT: WHERE IT HITS",
      "THE PADDLE STEERS IT. THE GREEN RING ON",
      "THE WALLS SHOWS HOW FAR AWAY IT IS.",
      "",
      "SILVER BRICKS TAKE SEVERAL HITS. GREY",
      "STEEL NEVER BREAKS. PINK BRICKS DROP A",
      "CAPSULE - CATCH IT WITH THE PADDLE:",
      " W WIDE PADDLE    S SLOW BALL",
      " L EXTRA LIFE     P PLOUGH THROUGH",
      "CLEAR ALL 8 LEVELS TO WIN." },
#ifdef AMIGA3DO
    { "D-PAD                     MOVE",
#else
    { "ARROWS / WASD / JOYSTICK  MOVE",
#endif
#ifdef AMIGA3DO
      "A                         SERVE",
#else
      "SPACE / FIRE              SERVE",
#endif
      "P                         PAUSE",
      "B                         AUTOPILOT",
#ifdef AMIGA3DO
      "C                         THESE PAGES",
#else
      "HELP / H                  THESE PAGES",
#endif
#ifdef AMIGA3DO
      "L / X                     TITLE / MENU",
#else
      "ESC                       TITLE / QUIT",
#endif
      "",
      "BRICKS 10, TOUGH 30, BONUS 50,",
      "+5 FOR EACH LAYER DEEPER.",
      "CAPSULE 100. LEVEL CLEAR 250 x LEVEL.",
      "",
      "THE BALL SPEEDS UP AS YOU RALLY." },
};

void render_help(Canvas *cv, int page)
{
    int k;
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, 0);
    g_sync();
    g_text_big(cv, page == 1 ? 72 : 96, 14, PEN_CYAN, page == 1 ? "HOW TO PLAY" : "CONTROLS", 2);
    for (k = 0; k < 12; k++) g_text(cv, 8, 58 + k * 14, 1, help_lines[page - 1][k]);
    g_text(cv, 8, 242, 3, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2");
#ifdef AMIGA3DO
    g_text(cv, 156, 242, PEN_YELLOW, "C: NEXT  A: PLAY");
#else
    g_text(cv, 156, 242, PEN_YELLOW, "HELP: NEXT  FIRE: PLAY");
#endif
}
