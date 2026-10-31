// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Tank Duel's picture, from the turret: the sky and the arena's
 * floor with its grid, the walls, the cover (pillars, pyramids, crates),
 * the enemy tanks, shells, mortar rounds in flight with the red rings
 * where they will land, explosions, and the HUD: the gun, the sight, the
 * armour and reload bars, and a radar turned with the tank.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "td.h"
#include "duel.h"

/* 0 black, 1 white, 2 panel, 3 grey, 4 sky (gradient), 5 floor, 6 grid,
 * 7-8 walls, 9-10 pillars, 11-12 pyramids, 13-14 crates, 15 a cracked
 * crate, 16-21 scouts, mortars, bulwarks (lit, shaded), 22 the shield,
 * 23 your shells, 24 theirs, 25 the mortar ring, 26 red, 27 yellow,
 * 28 green, 29 cyan, 30 fire, 31 the radar */
const uint16_t duel_palette[32] = {
    0x000, 0xFFF, 0x122, 0x99A, 0x58A, 0x353, 0x575, 0x887, 0x554, 0xAAB, 0x667, 0xCA6, 0x864,
    0xB83, 0x751, 0x642, 0x6D4, 0x372, 0xE92, 0x952, 0x98B, 0x546, 0x6EF, 0xFF6, 0xF73, 0xE22,
    0xF33, 0xFE4, 0x6F6, 0x5EF, 0xFA3, 0x021
};
#define PEN_SKY    4
#define PEN_FLOOR  5
#define PEN_GRID   6
#define PEN_WALL   7
#define PEN_SHIELD 22
#define PEN_SHELL  23
#define PEN_ESHELL 24
#define PEN_RING   25
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29
#define PEN_FIRE   30
#define PEN_RADAR  31

#define EYE_Y    34
#define VIEW_H   SCREEN_H

static Cam cam;
static Model m_pillar, m_pyramid, m_crate, m_tank[TK_COUNT];
static const uint8_t crate_body[2][2] = { { 15, 14 }, { 13, 14 } };

void render_init(void)
{
    Model *m;
    mdl_begin(&m_pillar);
    mdl_prism(&m_pillar, 9, 10, 8, 26, 0, 130);
    mdl_begin(&m_pyramid);
    mdl_pyramid(&m_pyramid, 11, 12, -50, 50, 0, -50, 50, 110);
    mdl_begin(&m_crate);
    mdl_box(&m_crate, COL_BODY, COL_BODY + 1, -34, 34, 0, 56, -34, 34);
    /* the scout: a low hull, a little turret and a long barrel */
    m = &m_tank[TK_SCOUT];
    mdl_begin(m);
    mdl_box(m, 16, 17, -18, 18, 2, 16, -26, 24);
    mdl_box(m, 17, 17, -22, 22, 0, 8, -24, 22);
    mdl_box(m, 16, 17, -9, 9, 16, 26, -10, 8);
    mdl_box(m, 3, 3, -2, 2, 19, 23, 8, 40);
    /* the mortar: a box with a short tube pointing up */
    m = &m_tank[TK_MORTAR];
    mdl_begin(m);
    mdl_box(m, 18, 19, -22, 22, 0, 22, -26, 26);
    mdl_box(m, 19, 19, -12, 12, 22, 30, -14, 10);
    mdl_box(m, 3, 2, -6, 6, 30, 58, -8, 4);
    /* the bulwark: a heavy hull and the shield across its front */
    m = &m_tank[TK_BULWARK];
    mdl_begin(m);
    mdl_box(m, 20, 21, -26, 26, 0, 26, -28, 22);
    mdl_box(m, 20, 21, -13, 13, 26, 38, -14, 10);
    mdl_box(m, 3, 3, -3, 3, 30, 34, 10, 38);
    mdl_box(m, PEN_SHIELD, 29, -32, 32, 2, 40, 26, 32);
}

/* a square facing the camera, half size r, at world (x, y, z) */
static void spot(int32_t x, int32_t y, int32_t z, int32_t r, int col, int32_t bias)
{
    V3 c, p[4];
    cam_xform(&cam, x, y, z, &c);
    r *= CAM_U(&cam);                       /* camera space may be in sixteenths */
    if (c.z < CAM_NEAR(&cam) + r) return;
    p[0].x = c.x - r; p[0].y = c.y - r; p[0].z = c.z;
    p[1].x = c.x + r; p[1].y = c.y - r; p[1].z = c.z;
    p[2].x = c.x + r; p[2].y = c.y + r; p[2].z = c.z;
    p[3].x = c.x - r; p[3].y = c.y + r; p[3].z = c.z;
    a3_poly(&cam, c.z + bias * CAM_U(&cam), col, p, 4);
}

/* an explosion: a ragged star facing the camera, turning as it fades */
static void burst(int32_t x, int32_t y, int32_t z, int32_t r, int col, int t)
{
    V3 c, p[8];
    int k;
    cam_xform(&cam, x, y, z, &c);
    r *= CAM_U(&cam);
    if (c.z < CAM_NEAR(&cam) + r) return;
    for (k = 0; k < 8; k++) {
        int a = k * 8192 + t * 1500;
        int32_t rr = (k & 1) ? r / 2 : r;
        p[k].x = c.x + ((a3_sin(a) * rr) >> 16);
        p[k].y = c.y + ((a3_cos(a) * rr) >> 16);
        p[k].z = c.z;
    }
    a3_poly(&cam, c.z - 8 * CAM_U(&cam), col, p, 8);
}

static void ring(int32_t x, int32_t z, int32_t r, int col)
{
    int k;
    for (k = 0; k < 12; k++) {
        int a0 = k * 5461, a1 = a0 + 5461;
        V3 q[4];
        int32_t x0 = x + ((a3_sin(a0) * r) >> 16), z0 = z + ((a3_cos(a0) * r) >> 16);
        int32_t x1 = x + ((a3_sin(a1) * r) >> 16), z1 = z + ((a3_cos(a1) * r) >> 16);
        int32_t x2 = x + ((a3_sin(a1) * (r - 8)) >> 16), z2 = z + ((a3_cos(a1) * (r - 8)) >> 16);
        int32_t x3 = x + ((a3_sin(a0) * (r - 8)) >> 16), z3 = z + ((a3_cos(a0) * (r - 8)) >> 16);
        cam_xform(&cam, x0, 1, z0, &q[0]); cam_xform(&cam, x1, 1, z1, &q[1]);
        cam_xform(&cam, x2, 1, z2, &q[2]); cam_xform(&cam, x3, 1, z3, &q[3]);
        a3_quad(&cam, ((q[0].z + q[2].z) >> 1) + 30 * CAM_U(&cam), col, q[0], q[1], q[2], q[3]);
    }
}

static void floor_grid(Canvas *cv)
{
    int32_t k;
    a3_begin(3000);
    for (k = -TD_ARENA; k <= TD_ARENA; k += 200) {
        V3 a, b, c, d;
        cam_xform(&cam, k - 3, 0, -TD_ARENA, &a); cam_xform(&cam, k + 3, 0, -TD_ARENA, &b);
        cam_xform(&cam, k + 3, 0, TD_ARENA, &c); cam_xform(&cam, k - 3, 0, TD_ARENA, &d);
        a3_quad(&cam, 2000 * CAM_U(&cam), PEN_GRID, a, b, c, d);
        cam_xform(&cam, -TD_ARENA, 0, k - 3, &a); cam_xform(&cam, TD_ARENA, 0, k - 3, &b);
        cam_xform(&cam, TD_ARENA, 0, k + 3, &c); cam_xform(&cam, -TD_ARENA, 0, k + 3, &d);
        a3_quad(&cam, 2000 * CAM_U(&cam), PEN_GRID, a, b, c, d);
    }
    a3_flush(cv);
}

static void walls(void)
{
    int side, k;
    for (side = 0; side < 4; side++)
        for (k = 0; k < 5; k++) {
            int32_t u0 = -TD_ARENA + k * 400, u1 = u0 + 400, h = 70, s = side < 2 ? TD_ARENA : -TD_ARENA;
            V3 a, b, c, d;
            if (side & 1) { cam_xform(&cam, s, 0, u0, &a); cam_xform(&cam, s, 0, u1, &b); cam_xform(&cam, s, h, u1, &c); cam_xform(&cam, s, h, u0, &d); }
            else { cam_xform(&cam, u0, 0, s, &a); cam_xform(&cam, u1, 0, s, &b); cam_xform(&cam, u1, h, s, &c); cam_xform(&cam, u0, h, s, &d); }
            a3_quad(&cam, (a.z + c.z) >> 1, PEN_WALL + (k & 1), a, b, c, d);
        }
}

static void obstacles(const TDGame *g)
{
    int k;
    for (k = 0; k < g->nobs; k++) {
        const Obstacle *o = &g->ob[k];
        if (o->kind == OB_CRATE && o->hp <= 0) continue;
        a3_model(&cam, o->kind == OB_PILLAR ? &m_pillar : o->kind == OB_PYRAMID ? &m_pyramid : &m_crate,
                 o->x >> 16, 0, o->z >> 16, NULL, crate_body[o->hp >= 2], 0, 0);
    }
}

static void tanks(const TDGame *g)
{
    int k;
    int32_t m[9];
    for (k = 1; k < g->ntanks; k++) {
        const Tank *t = &g->tk[k];
        Cam tc;
        if (!t->alive) continue;
        cam_for(&cam, t->x, 0, t->z, &tc);  /* drawn where it is, not on whole units */
        a3_rot_yaw(m, t->yaw);
        a3_model(&tc, &m_tank[t->type], t->x >> 16, 0, t->z >> 16, m, NULL, 0, 0);
    }
}

static void missiles(const TDGame *g)
{
    int k;
    for (k = 0; k < TD_MAX_SHELLS; k++) {
        const Shell *s = &g->sh[k];
        if (s->alive) spot(s->x >> 16, 20, s->z >> 16, 5, s->owner ? PEN_ESHELL : PEN_SHELL, -4);
    }
    for (k = 0; k < TD_MAX_MORTARS; k++) {
        const Mortar *m = &g->mo[k];
        int32_t f, x, z, y;
        if (!m->alive) continue;
        f = (int32_t)m->t * 256 / m->flight;          /* 0..256 of the way */
        x = (int32_t)((m->x0 >> 16) + (((m->tx - m->x0) >> 16) * f >> 8));
        z = (int32_t)((m->z0 >> 16) + (((m->tz - m->z0) >> 16) * f >> 8));
        y = 20 + 4 * 280 * f * (256 - f) / 65536;
        spot(x, y, z, 7, PEN_ESHELL, -4);
        ring(m->tx >> 16, m->tz >> 16, TD_SPLASH_R, (g->tick & 4) ? PEN_RING : PEN_RED);
    }
    for (k = 0; k < MAX_BLASTS; k++) {
        const Blast *b = &du.blast[k];
        if (b->t > 0) burst(b->x, b->y + (10 - b->t) * 2, b->z, b->size * (4 + b->t) / 14,
                            b->t > 7 ? PEN_YELLOW : b->t > 3 ? PEN_FIRE : PEN_RED, b->t);
    }
}

static void scene(Canvas *cv, const TDGame *g)
{
    const Tank *p = &g->tk[0];
    int hz;
    /* placed to a sixteenth of a unit: a camera on whole units steps
     * unevenly as the tank drives, which shows as judder */
    cam_place(&cam, p->x, EYE_Y << 16, p->z);
    cam.yaw = p->yaw;
    cam.pitch = 300;
    cam_view(&cam, SCREEN_W, VIEW_H, SCREEN_W / 2, 118, 220);
    cam_setup(&cam);
    hz = cam_horizon(&cam);
    if (hz < 1) hz = 1;
    if (hz > VIEW_H - 1) hz = VIEW_H - 1;
    g_fill(cv, 0, 0, SCREEN_W - 1, hz - 1, PEN_SKY);
    g_fill(cv, 0, hz, SCREEN_W - 1, VIEW_H - 1, PEN_FLOOR);
    g_sync();
    floor_grid(cv);
    a3_begin(3000);
    walls();
    obstacles(g);
    tanks(g);
    missiles(g);
    a3_flush(cv);
}

/* ---------- the HUD ---------- */
static void radar(Canvas *cv, const TDGame *g)
{
    const Tank *p = &g->tk[0];
    int cx = SCREEN_W - 36, cy = 36, k, R = 30;
    int32_t c = a3_cos(p->yaw), s = a3_sin(p->yaw);
    g_circle(cv, cx, cy, R + 2, 3);
    g_circle(cv, cx, cy, R, PEN_RADAR);
    /* everything turned so ahead is up; 1 pixel = 40 units */
#define BLIP(wx, wz, col, sz) do { \
        int32_t dx = ((wx) - p->x) >> 16, dz = ((wz) - p->z) >> 16; \
        int32_t rx = (dx * c - dz * s) >> 16, rz = (dx * s + dz * c) >> 16; \
        int bx = cx + (int)(rx / 40), by = cy - (int)(rz / 40); \
        if ((bx - cx) * (bx - cx) + (by - cy) * (by - cy) < R * R) g_rect(cv, bx - (sz), by - (sz), bx + (sz), by + (sz), col); \
    } while (0)
    for (k = 0; k < g->nobs; k++) if (g->ob[k].kind != OB_CRATE || g->ob[k].hp > 0) BLIP(g->ob[k].x, g->ob[k].z, 3, 0);
    for (k = 0; k < TD_MAX_MORTARS; k++) if (g->mo[k].alive) BLIP(g->mo[k].tx, g->mo[k].tz, PEN_RED, 1);
    for (k = 1; k < g->ntanks; k++) if (g->tk[k].alive) BLIP(g->tk[k].x, g->tk[k].z, g->tk[k].type == TK_SCOUT ? PEN_GREEN : g->tk[k].type == TK_MORTAR ? PEN_FIRE : PEN_CYAN, 1);
#undef BLIP
    g_rect(cv, cx - 1, cy - 1, cx + 1, cy + 1, 1);
    g_rect(cv, cx, cy - 5, cx, cy - 2, 1);
}

static void gun(Canvas *cv, const TDGame *g)
{
    int16_t xs[4] = { 146, 174, 186, 134 }, ys[4] = { 206, 206, SCREEN_H - 1, SCREEN_H - 1 };
    int kick = g->tk[0].reload > TD_RELOAD - 4 ? 6 : 0;
    ys[0] += kick; ys[1] += kick;
    g_poly(cv, xs, ys, 4, 3, 0, SCREEN_H - 1);
    g_rect(cv, 154, 206 + kick, 166, 210 + kick, 0);
    /* the sight */
    g_rect(cv, 159, 108, 160, 113, PEN_GREEN);
    g_rect(cv, 159, 123, 160, 128, PEN_GREEN);
    g_rect(cv, 148, 117, 153, 118, PEN_GREEN);
    g_rect(cv, 166, 117, 171, 118, PEN_GREEN);
}

static void hud(Canvas *cv, const TDGame *g)
{
    char tmp[40];
    const Tank *p = &g->tk[0];
    int w = p->hp * 80 / TD_HP;
    gun(cv, g);
    radar(cv, g);
    sprintf(tmp, "ROUND %ld", (long)g->round);
    g_text_shadow(cv, 4, 10, PEN_YELLOW, tmp);
    sprintf(tmp, "ENEMIES %ld", (long)td_enemies_left(g));
    g_text_shadow(cv, 4, 22, 1, tmp);
    sprintf(tmp, "%ld", (long)g->score);
    g_text_shadow(cv, 4, 34, 3, tmp);
    g_text_shadow(cv, 4, 242, 3, "ARMOUR");
    g_box(cv, 56, 235, 138, 243, 0, 3);
    if (w > 0) g_rect(cv, 57, 236, 57 + w, 242, p->hp > 40 ? PEN_GREEN : PEN_RED);
    g_text_shadow(cv, 196, 242, 3, "GUN");
    w = (TD_RELOAD - p->reload) * 80 / TD_RELOAD;
    g_box(cv, 228, 235, 310, 243, 0, 3);
    g_rect(cv, 229, 236, 229 + w, 242, p->reload ? PEN_FIRE : PEN_YELLOW);
    if (du.autoplay) g_text_shadow(cv, 4, 228, PEN_GREEN, "AUTO");
    if (du.hurt_timer > 0) {
        g_rect(cv, 0, 0, SCREEN_W - 1, 3, PEN_RED);
        g_rect(cv, 0, SCREEN_H - 4, SCREEN_W - 1, SCREEN_H - 1, PEN_RED);
        g_rect(cv, 0, 0, 3, SCREEN_H - 1, PEN_RED);
        g_rect(cv, SCREEN_W - 4, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_RED);
    }
}

void render_game(Canvas *cv, const TDGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (g->between) {
        g_text_big(cv, 60, 70, PEN_GREEN, "ROUND CLEAR", 2);
        if (g->round < TD_ROUNDS) { sprintf(tmp, "ROUND %ld NEXT", (long)g->round + 1); g_text_centre(cv, 100, 1, tmp); }
    }
    if (du.state == DS_PLAY && g->tick < 50 && !g->between) {
        sprintf(tmp, "ROUND %ld: %ld ENEMIES", (long)g->round, (long)td_enemies_left(g));
        g_text_centre(cv, 70, PEN_YELLOW, tmp);
    }
    if (du.state == DS_OVER) {
        g_box(cv, 50, 70, 270, 150, 2, g->won ? PEN_GREEN : PEN_RED);
        g_text_big(cv, g->won ? 92 : 76, 78, g->won ? PEN_GREEN : PEN_RED, g->won ? "VICTORY" : "GAME OVER", 2);
        sprintf(tmp, "%ld KILLS  %ld%% HITS", (long)g->kills, (long)(g->shots ? g->hits * 100 / g->shots : 0));
        g_text_centre(cv, 112, PEN_YELLOW, tmp);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 126, 1, tmp);
#ifdef AMIGA3DO
        if (du.state_timer == 0) g_text_centre(cv, 142, PEN_YELLOW, "PRESS A");
#else
        if (du.state_timer == 0) g_text_centre(cv, 142, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (du.paused == 1) g_text_centre(cv, 160, 1, "PAUSED");
    if (du.flash_timer > 0 && du.flash[0]) g_text_centre(cv, 60, du.flash_pen, du.flash);
}

void render_title(Canvas *cv, const TDGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot fights behind the title */
    g_text_big(cv, 66, 26, 0, "TANK", 4);
    g_text_big(cv, 64, 24, PEN_YELLOW, "TANK", 4);
    g_text_big(cv, 98, 62, 0, "DUEL", 4);
    g_text_big(cv, 96, 60, PEN_FIRE, "DUEL", 4);
    g_box(cv, 40, 196, 280, 252, 2, 3);
    sprintf(tmp, "START AT ROUND %ld (UP/DOWN)", (long)du.start_round);
    g_text_centre(cv, 210, PEN_GREEN, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO FIGHT");
#else
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO FIGHT");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld   C: HOW TO PLAY", (long)du.best);
#else
    sprintf(tmp, "BEST %ld   HELP: HOW TO PLAY", (long)du.best);
#endif
    g_text_centre(cv, 242, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "DESTROY EVERY ENEMY TANK IN THE ARENA.",
      "SHELLS BOUNCE ONCE OFF THE WALLS: BANK",
      "A SHOT ROUND COVER. CRATES BREAK.",
      "",
      " SCOUT    QUICK, CIRCLES AND SNIPES",
      " MORTAR   LOBS SHELLS AT WHERE YOU ARE",
      "          - GET OUT OF THE RED RING",
      " BULWARK  SHIELDED IN FRONT: SHOTS",
      "          GLANCE OFF. IT TURNS SLOWLY,",
      "          SO GET ROUND TO ITS SIDE.",
      "",
      "8 ROUNDS. +30 ARMOUR BETWEEN THEM." },
    { "UP / DOWN        FORWARD / BACK",
      "LEFT / RIGHT     TURN",
#ifdef AMIGA3DO
      "A                FIRE",
#else
      "SPACE / FIRE     FIRE",
#endif
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
      "SCOUT 100, MORTAR 200, BULWARK 300,",
      "CRATE 20. ROUND 500 x ROUND, AND 5",
      "FOR EACH POINT OF ARMOUR LEFT.",
      "THE RADAR: AHEAD IS UP." },
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
