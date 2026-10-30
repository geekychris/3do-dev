// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Canyon Rail's picture: the sky, the canyon ahead (its floor
 * with the river down the middle, its rock walls in sloping slabs with
 * spires along the rim), the turrets on the walls, the drones, the fuel
 * pods, the bolts, the guardian and its orbiting nodes, explosions, and
 * the HUD: the sight, the tracer, the shield, the multiplier and how far
 * to the guardian.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "cr.h"
#include "rail.h"

/* 0 black, 1 white, 2 panel, 3 grey, 4 sky (gradient), 5-6 banks, 7 the
 * river, 8-11 walls (two tones, lit and shaded), 12 spires, 13-14
 * turrets, 15-16 drones, 17 pods, 18 bolts, 19 bolt glow, 20-21 the
 * guardian, 22 nodes, 23 fire, 26 red, 27 yellow, 28 green, 29 cyan */
const uint16_t rail_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0xE96, 0x975, 0x864, 0x258, 0xB64, 0x843, 0xA75, 0x754, 0x632,
    0x889, 0x556, 0x6CD, 0x38A, 0x9F4, 0xF44, 0xFA8, 0x99B, 0x667, 0xF5E, 0xFA3, 0x000, 0x000,
    0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000
};
#define PEN_SKY    4
#define PEN_BANK   5
#define PEN_RIVER  7
#define PEN_WALL   8
#define PEN_SPIRE  12
#define PEN_TURRET 13
#define PEN_DRONE  15
#define PEN_POD    17
#define PEN_BOLT   18
#define PEN_GLOW   19
#define PEN_BOSS   20
#define PEN_NODE   22
#define PEN_FIRE   23
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29

#define SEG 100
int32_t view_ahead = A3_SUB > 1 ? 2200 : 1300;
static Cam cam;
static Model m_turret, m_drone, m_core, m_node;

void render_init(void)
{
    mdl_begin(&m_turret);
    mdl_box(&m_turret, PEN_TURRET, PEN_TURRET + 1, -14, 14, -12, 6, -14, 14);
    mdl_box(&m_turret, PEN_TURRET + 1, PEN_TURRET + 1, -8, 8, 6, 14, -8, 8);
    mdl_box(&m_turret, PEN_RED, PEN_RED, -3, 3, 8, 12, 8, 30);
    mdl_begin(&m_drone);
    mdl_box(&m_drone, PEN_DRONE, PEN_DRONE + 1, -8, 8, -5, 5, -10, 12);
    mdl_box(&m_drone, PEN_DRONE + 1, PEN_DRONE + 1, -24, 24, -1, 2, -6, 4);
    mdl_box(&m_drone, PEN_RED, PEN_RED, -3, 3, -3, 3, 12, 15);
    mdl_begin(&m_core);
    mdl_box(&m_core, PEN_BOSS, PEN_BOSS + 1, -50, 50, -50, 50, -50, 50);
    mdl_box(&m_core, PEN_RED, PEN_RED + 0, -18, 18, -18, 18, -56, -50);
    mdl_begin(&m_node);
    mdl_prism(&m_node, PEN_NODE, PEN_RED, 6, 18, -18, 18);
}

/* a square facing the camera */
static void spot(int32_t x, int32_t y, int32_t z, int32_t r, int col, int32_t bias)
{
    V3 c, p[4];
    cam_xform(&cam, x, y, z, &c);
    if (c.z < NEAR_Z + r) return;
    p[0].x = c.x - r; p[0].y = c.y - r; p[0].z = c.z;
    p[1].x = c.x + r; p[1].y = c.y - r; p[1].z = c.z;
    p[2].x = c.x + r; p[2].y = c.y + r; p[2].z = c.z;
    p[3].x = c.x - r; p[3].y = c.y + r; p[3].z = c.z;
    a3_poly(&cam, c.z + bias, col, p, 4);
}

static void burst(int32_t x, int32_t y, int32_t z, int32_t r, int col, int t)
{
    V3 c, p[8];
    int k;
    cam_xform(&cam, x, y, z, &c);
    if (c.z < NEAR_Z + r) return;
    for (k = 0; k < 8; k++) {
        int a = k * 8192 + t * 1700;
        int32_t rr = (k & 1) ? r / 2 : r;
        p[k].x = c.x + ((a3_sin(a) * rr) >> 16);
        p[k].y = c.y + ((a3_cos(a) * rr) >> 16);
        p[k].z = c.z;
    }
    a3_poly(&cam, c.z - 6, col, p, 8);
}

static void quad(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                 int32_t x2, int32_t y2, int32_t z2, int32_t x3, int32_t y3, int32_t z3, int32_t bias, int col)
{
    V3 a, b, c, d;
    cam_xform(&cam, x0, y0, z0, &a); cam_xform(&cam, x1, y1, z1, &b);
    cam_xform(&cam, x2, y2, z2, &c); cam_xform(&cam, x3, y3, z3, &d);
    if (a.z < NEAR_Z && b.z < NEAR_Z && c.z < NEAR_Z && d.z < NEAR_Z) return;
    a3_quad(&cam, ((a.z + b.z + c.z + d.z) >> 2) + bias, col, a, b, c, d);
}

static void canyon(const CRGame *g)
{
    int32_t s, s0 = (g->cam_s / SEG) * SEG - SEG, s1 = g->cam_s + view_ahead;
    for (s = s0; s < s1; s += SEG) {
        int32_t sb = s + SEG, ca = cr_path_x(s), cb = cr_path_x(sb), wa = cr_half_width(s), wb = cr_half_width(sb);
        int32_t ha = 250 + (int32_t)(a3_hash((uint32_t)s * 7u) % 90), hb = 250 + (int32_t)(a3_hash((uint32_t)sb * 7u) % 90);
        int tone = (int)((s / SEG) & 1), side;
        /* the floor: the banks and the river */
        quad(ca - wa, 0, s, ca - 30, 0, s, cb - 30, 0, sb, cb - wb, 0, sb, 60, PEN_BANK + tone);
        quad(ca + 30, 0, s, ca + wa, 0, s, cb + wb, 0, sb, cb + 30, 0, sb, 60, PEN_BANK + tone);
        quad(ca - 30, 1, s, ca + 30, 1, s, cb + 30, 1, sb, cb - 30, 1, sb, 59, PEN_RIVER);
        /* the walls, sloping out as they rise, and a spire on the rim now and then */
        for (side = -1; side <= 1; side += 2) {
            int32_t xa = ca + side * wa, xb = cb + side * wb, ta = xa + side * 70, tb = xb + side * 70;
            int pen = PEN_WALL + 2 * tone + (side > 0);
            quad(xa, 0, s, xb, 0, sb, tb, hb, sb, ta, ha, s, 40, pen);
            if ((a3_hash((uint32_t)s * 13u + (uint32_t)side) & 3) == 0) {
                V3 p[3];
                cam_xform(&cam, ta, ha, s, &p[0]);
                cam_xform(&cam, tb, hb, sb, &p[1]);
                cam_xform(&cam, (ta + tb) / 2 + side * 20, (ha + hb) / 2 + 90, s + SEG / 2, &p[2]);
                a3_poly(&cam, ((p[0].z + p[1].z) >> 1) + 38, PEN_SPIRE, p, 3);
            }
        }
    }
}

static void targets(const CRGame *g)
{
    int k;
    int32_t m[9], x, y, z;
    for (k = 0; k < g->ntg; k++) {
        const Target *t = &g->tg[k];
        int32_t ahead = t->s - g->cam_s;
        if (!t->alive || ahead < 20 || ahead > view_ahead) continue;
        cr_target_pos(g, k, &x, &y, &z);
        if (t->type == TG_POD) { spot(x, y, z, 12, PEN_POD, 0); spot(x, y, z - 1, 6, 1, -1); continue; }
        a3_rot_yaw(m, a3_atan2(cam.x - x, cam.z - z));       /* facing the camera */
        a3_model(&cam, t->type == TG_TURRET ? &m_turret : &m_drone, x, y, z, m, NULL, 0, 0);
    }
    for (k = 0; k < CR_MAX_BOLTS; k++) {
        if (!g->bolt[k].alive) continue;
        cr_bolt_pos(g, k, &x, &y, &z);
        spot(x, y, z, 9, PEN_GLOW, 0);
        spot(x, y, z, 5, PEN_BOLT, -1);
    }
}

static void guardian(const CRGame *g)
{
    int k;
    int32_t m[9], bs = CR_LEN + 450, x, y, z;
    a3_rot_ypr(m, (int)(g->boss_angle / 3), 0, 0);
    a3_model(&cam, &m_core, cr_path_x(bs), cr_path_y(bs) + 90, bs, m, NULL, 0, 0);
    for (k = 0; k < CR_NODES; k++) {
        if (g->node_hp[k] <= 0) continue;
        cr_node_pos(g, k, &x, &y, &z);
        a3_rot_ypr(m, (int)g->boss_angle, 16384, 0);
        a3_model(&cam, &m_node, x, y, z, m, NULL, 0, -30);
    }
}

static void scene(Canvas *cv, const CRGame *g)
{
    int hz, k;
    cr_camera(g, &cam);
    hz = cam_horizon(&cam);
    if (hz < 1) hz = 1;
    if (hz > SCREEN_H - 1) hz = SCREEN_H - 1;
    g_fill(cv, 0, 0, SCREEN_W - 1, hz - 1, PEN_SKY);
    g_fill(cv, 0, hz, SCREEN_W - 1, SCREEN_H - 1, PEN_WALL + 1);
    g_sync();
    a3_begin(3000);
    canyon(g);
    if (g->cam_s + view_ahead > CR_LEN) guardian(g);
    targets(g);
    for (k = 0; k < MAX_BURSTS; k++) {
        const Burst *b = &rl.burst[k];
        if (b->t > 0) burst(b->x, b->y, b->z, b->size * (4 + b->t) / 14, b->t > 7 ? PEN_YELLOW : b->t > 3 ? PEN_FIRE : PEN_RED, b->t);
    }
    a3_flush(cv);
}

static void hud(Canvas *cv, const CRGame *g)
{
    char tmp[40];
    int w, k, m = 1 + g->combo / 4;
    if (m > 8) m = 8;
    /* the tracer, from the gun to the sight */
    if (rl.tracer_timer > 0) {
        int16_t xs[3], ys[3];           /* C89: no run-time initialisers */
        xs[0] = (int16_t)(SCREEN_W / 2 - 3); xs[1] = (int16_t)(SCREEN_W / 2 + 3); xs[2] = (int16_t)g->sx;
        ys[0] = (int16_t)(SCREEN_H - 1); ys[1] = (int16_t)(SCREEN_H - 1); ys[2] = (int16_t)g->sy;
        g_poly(cv, xs, ys, 3, g->last_shot_hit ? PEN_YELLOW : 3, 0, SCREEN_H - 1);
    }
    /* the sight */
    g_rect(cv, g->sx - 9, g->sy, g->sx - 4, g->sy, PEN_GREEN);
    g_rect(cv, g->sx + 4, g->sy, g->sx + 9, g->sy, PEN_GREEN);
    g_rect(cv, g->sx, g->sy - 9, g->sx, g->sy - 4, PEN_GREEN);
    g_rect(cv, g->sx, g->sy + 4, g->sx, g->sy + 9, PEN_GREEN);
    sprintf(tmp, "%ld", (long)g->score);
    g_text_shadow(cv, 4, 10, 1, tmp);
    if (m > 1) { sprintf(tmp, "x%ld", (long)m); g_text_big(cv, 4, 14, PEN_YELLOW, tmp, 2); }
    sprintf(tmp, "STAGE %ld", (long)g->stage);
    g_text_shadow(cv, SCREEN_W - 76, 10, PEN_YELLOW, tmp);
    /* how far to the guardian, or its nodes */
    if (!g->boss) {
        w = (int)(g->cam_s * 100 / CR_LEN);
        g_box(cv, SCREEN_W - 106, 14, SCREEN_W - 4, 19, 0, 3);
        g_rect(cv, SCREEN_W - 105, 15, SCREEN_W - 105 + w, 18, PEN_CYAN);
    } else
        for (k = 0; k < CR_NODES; k++) g_rect(cv, SCREEN_W - 50 + k * 11, 14, SCREEN_W - 42 + k * 11, 20, g->node_hp[k] > 0 ? PEN_NODE : 2);
    g_text_shadow(cv, 4, SCREEN_H - 4, 3, "SHIELD");
    g_box(cv, 56, SCREEN_H - 12, 138, SCREEN_H - 4, 0, 3);
    w = g->shield * 80 / CR_SHIELD;
    if (w > 0) g_rect(cv, 57, SCREEN_H - 11, 57 + w, SCREEN_H - 5, g->shield > 30 ? PEN_GREEN : PEN_RED);
    if (rl.autoplay) g_text_shadow(cv, SCREEN_W - 40, SCREEN_H - 4, PEN_GREEN, "AUTO");
    if (rl.hurt_timer > 0) {
        g_rect(cv, 0, 0, SCREEN_W - 1, 3, PEN_RED);
        g_rect(cv, 0, SCREEN_H - 4, SCREEN_W - 1, SCREEN_H - 1, PEN_RED);
        g_rect(cv, 0, 0, 3, SCREEN_H - 1, PEN_RED);
        g_rect(cv, SCREEN_W - 4, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_RED);
    }
}

void render_game(Canvas *cv, const CRGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (g->between) {
        g_text_big(cv, 56, 80, PEN_GREEN, "STAGE CLEAR", 2);
        if (g->stage < CR_STAGES) { sprintf(tmp, "STAGE %ld NEXT", (long)g->stage + 1); g_text_centre(cv, 110, 1, tmp); }
    } else if (g->boss && g->tick % 50 < 30 && g->node_hp[0] + g->node_hp[1] + g->node_hp[2] + g->node_hp[3] == 4 * (3 + g->stage))
        g_text_centre(cv, 60, PEN_RED, "GUARDIAN: SHOOT ITS NODES");
    if (rl.state == RS_OVER) {
        g_box(cv, 50, 80, 270, 160, 2, g->won ? PEN_GREEN : PEN_RED);
        g_text_big(cv, g->won ? 92 : 76, 88, g->won ? PEN_GREEN : PEN_RED, g->won ? "VICTORY" : "GAME OVER", 2);
        sprintf(tmp, "%ld%% HITS  BEST CHAIN %ld", (long)(g->shots ? g->hits * 100 / g->shots : 0), (long)g->best_combo);
        g_text_centre(cv, 122, PEN_YELLOW, tmp);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 136, 1, tmp);
#ifdef AMIGA3DO
        if (rl.state_timer == 0) g_text_centre(cv, 152, PEN_YELLOW, "PRESS A");
#else
        if (rl.state_timer == 0) g_text_centre(cv, 152, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (rl.paused == 1) g_text_centre(cv, 180, 1, "PAUSED");
    if (rl.flash_timer > 0 && rl.flash[0]) g_text_centre(cv, 44, rl.flash_pen, rl.flash);
}

void render_title(Canvas *cv, const CRGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot rides behind the title */
    g_text_big(cv, 50, 26, 0, "CANYON", 4);
    g_text_big(cv, 48, 24, PEN_YELLOW, "CANYON", 4);
    g_text_big(cv, 98, 62, 0, "RAIL", 4);
    g_text_big(cv, 96, 60, PEN_FIRE, "RAIL", 4);
    g_box(cv, 40, 196, 280, 252, 2, 3);
    sprintf(tmp, "START AT STAGE %ld (UP/DOWN)", (long)rl.start_stage);
    g_text_centre(cv, 210, PEN_GREEN, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO RIDE");
#else
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO RIDE");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld   C: HOW TO PLAY", (long)rl.best);
#else
    sprintf(tmp, "BEST %ld   HELP: HOW TO PLAY", (long)rl.best);
#endif
    g_text_centre(cv, 242, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "THE RIDE TAKES YOU DOWN THE CANYON:",
      "MOVE THE SIGHT AND FIRE.",
      "",
      " TURRETS  ON THE WALLS, TWO HITS",
      " DRONES   SWOOP ACROSS, ONE HIT",
      " BOLTS    THEIR SHOTS: SHOOT THEM DOWN",
      "          OR YOUR SHIELD TAKES THEM",
      " PODS     SHOOT THEM: +25 SHIELD",
      "",
      "HITS IN A ROW BUILD A MULTIPLIER, UP",
      "TO x8. AT THE END: THE GUARDIAN - SHOOT",
      "OUT ITS FOUR NODES. THREE STAGES." },
#ifdef AMIGA3DO
    { "D-PAD               MOVE THE SIGHT",
#else
    { "ARROWS / JOYSTICK   MOVE THE SIGHT",
#endif
#ifdef AMIGA3DO
      "A                   FIRE (HOLD: 5 A S)",
#else
      "SPACE / FIRE        FIRE (HOLD: 5 A S)",
#endif
      "P                   PAUSE",
      "B                   AUTOPILOT",
#ifdef AMIGA3DO
      "C                   THESE PAGES",
#else
      "HELP / H            THESE PAGES",
#endif
#ifdef AMIGA3DO
      "L / X               TITLE / MENU",
#else
      "ESC                 TITLE / QUIT",
#endif
      "",
      "TURRET 100, DRONE 150, BOLT 25, POD 50,",
      "NODE 500, GUARDIAN 2000 x STAGE.",
      "ALL x THE MULTIPLIER.",
      "",
      "A MISS OR A HIT TAKEN RESETS IT." },
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
