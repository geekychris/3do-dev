// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Ridge Runner's picture: the sky and far ridges, the ground
 * around and ahead of the bike (a window of the height grid, each quad
 * shaded by its slope), the track laid over it as a ribbon with kerbs,
 * the gates' pylons and banners, the finish, the bikes with their shadows
 * and boost flames, and the race's HUD.
 *
 * The camera chases the bike, its heading easing after the bike's.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "a3d_plat.h"
#include "rr.h"
#include "runner.h"

/* 0 black, 1 white, 2 panel, 3 grey, 4 sky (gradient), 5-10 ground
 * (light to dark), 11-13 the track, 14 kerb red, 15 shadow, 16-17 the
 * player's bike, 18-23 the rivals', 24 pylons, 25 banner, 26 red,
 * 27 yellow, 28 green, 29 cyan, 30 far ridges, 31 flame */
static const uint16_t themes[3][32] = {
    { 0x000, 0xFFF, 0x123, 0x99A, 0x69D, 0x7C5, 0x6B4, 0x5A3, 0x492, 0x381, 0x260,
      0x998, 0x887, 0x776, 0xE33, 0x131, 0x3CF, 0x18A, 0xF62, 0xA31, 0xFD3, 0xA81, 0xC5E, 0x737,
      0xF80, 0xFFE, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x58A, 0xFC4 },
    { 0x000, 0xFFF, 0x211, 0xA99, 0xE96, 0xDA6, 0xC95, 0xB84, 0xA73, 0x862, 0x641,
      0x887, 0x776, 0x665, 0xE33, 0x420, 0x3CF, 0x18A, 0x6E4, 0x382, 0xFD3, 0xA81, 0xC5E, 0x737,
      0xF80, 0xFFE, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0xB75, 0xFC4 },
    { 0x000, 0xFFF, 0x123, 0x99A, 0x8AC, 0xEEF, 0xDDE, 0xBCD, 0x9AC, 0x89A, 0x678,
      0x667, 0x556, 0x445, 0xE33, 0x567, 0xF62, 0xA31, 0x3CF, 0x18A, 0xFD3, 0xA81, 0xC5E, 0x737,
      0xF80, 0xFFE, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x9AB, 0xFC4 },
};
static const uint8_t track_theme[RR_TRACKS] = { 0, 1, 1, 2, 0, 2 };
static const uint16_t sky_top[3] = { 0x36B, 0xC74, 0x68A }, sky_bottom[3] = { 0xBDE, 0xFDA, 0xDEF };
#define PEN_SKY    4
#define PEN_GROUND 5
#define PEN_TRACK  11
#define PEN_KERB   14
#define PEN_SHADOW 15
#define PEN_BIKE   16
#define PEN_PYLON  24
#define PEN_BANNER 25
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29
#define PEN_RIDGE  30
#define PEN_FLAME  31

#define CELL     80
#define SEG      80                     /* the track ribbon's pieces along the course */
#define VIEW_H   SCREEN_H
int32_t ground_win = A3_SUB > 1 ? 12 : 4;  /* 68k on a 68020: 6 cells gives 19-22 fps, 4 gives 25 */
#define WIN      ground_win
#define MAXWIN   12

static Cam cam;
static Model bike_m, pylon_m;
static int theme;
static const uint8_t body_pens[4][2] = { { 16, 17 }, { 18, 19 }, { 20, 21 }, { 22, 23 } };

void render_init(void)
{
    Model *m = &bike_m;
    mdl_begin(m);
    /* a hover bike, nose along +z: the hull, a raised seat, the nose
     * cone, two side pods */
    mdl_box(m, COL_BODY, COL_BODY + 1, -9, 9, -4, 6, -18, 16);
    mdl_box(m, COL_BODY + 1, COL_BODY + 1, -6, 6, 6, 12, -14, 2);
    {
        /* the nose: a wedge forward from the hull's front to a low, narrow tip */
        int a = mdl_vtx(m, -9, -4, 16), b = mdl_vtx(m, 9, -4, 16), c = mdl_vtx(m, 9, 6, 16), d = mdl_vtx(m, -9, 6, 16);
        int e = mdl_vtx(m, -4, -4, 36), f = mdl_vtx(m, 4, -4, 36), h = mdl_vtx(m, 4, -1, 36), i = mdl_vtx(m, -4, -1, 36);
        mdl_face(m, COL_BODY, COL_BODY + 1, 4, d, c, h, i, 0, 0, 22);      /* top */
        mdl_face(m, COL_BODY, COL_BODY + 1, 4, b, c, h, f, 0, 0, 22);      /* sides */
        mdl_face(m, COL_BODY, COL_BODY + 1, 4, a, d, i, e, 0, 0, 22);
        mdl_face(m, COL_BODY, COL_BODY + 1, 4, e, f, h, i, 0, 0, 22);      /* the tip */
    }
    mdl_box(m, 3, 2, -17, -9, -6, 2, -14, 10);
    mdl_box(m, 3, 2, 9, 17, -6, 2, -14, 10);
    m = &pylon_m;
    mdl_begin(m);
    mdl_prism(m, PEN_PYLON, PEN_PYLON, 6, 9, 0, 90);
    mdl_box(m, 1, 3, -11, 11, 90, 98, -11, 11);
}

void render_theme(int track)
{
    theme = track_theme[(track - 1) % RR_TRACKS];
    plat_palette(themes[theme]);
    plat_sky(PEN_SKY, sky_top[theme], sky_bottom[theme], 150);
}

/* ---------- the ground ---------- */
static int32_t floor_div(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static int32_t hcache[(2 * MAXWIN + 2) * (2 * MAXWIN + 2)];

static void ground(const RRGame *g)
{
    int i, j, n = 2 * WIN + 2;
    int32_t fx = a3_sin(rn.cam_yaw), fz = a3_cos(rn.cam_yaw);
    /* the window centred ahead of the bike */
    int32_t ax = (g->x >> 16) + ((fx * (WIN * CELL - CELL)) >> 16), az = (g->z >> 16) + ((fz * (WIN * CELL - CELL)) >> 16);
    int ci = (int)floor_div(ax, CELL), cj = (int)floor_div(az, CELL);
    for (j = 0; j < n; j++)
        for (i = 0; i < n; i++)
            hcache[j * n + i] = rr_height(g, (ci - WIN + i) * CELL, (cj - WIN + j) * CELL);
    for (j = 0; j < n - 1; j++)
        for (i = 0; i < n - 1; i++) {
            V3 a, b, c, d;
            int32_t x0 = (ci - WIN + i) * CELL, z0 = (cj - WIN + j) * CELL;
            int32_t ha = hcache[j * n + i], hb = hcache[j * n + i + 1], hc = hcache[(j + 1) * n + i + 1], hd = hcache[(j + 1) * n + i];
            int32_t slope;
            int pen;
            cam_xform(&cam, x0, ha, z0, &a);
            cam_xform(&cam, x0 + CELL, hb, z0, &b);
            cam_xform(&cam, x0 + CELL, hc, z0 + CELL, &c);
            cam_xform(&cam, x0, hd, z0 + CELL, &d);
            if (a.z < NEAR_Z && b.z < NEAR_Z && c.z < NEAR_Z && d.z < NEAR_Z) continue;
            /* lit from the left and ahead */
            slope = ((ha + hd) - (hb + hc)) + ((hc + hd) - (ha + hb)) / 2;
            pen = 2 + slope / 24;
            if (pen < 0) pen = 0;
            if (pen > 5) pen = 5;
            a3_quad(&cam, ((a.z + b.z + c.z + d.z) >> 2) + 40, PEN_GROUND + pen, a, d, c, b);
        }
}

/* the track: a ribbon along the course, its two halves following the
 * ground across, with kerbs in alternate colours */
static void track(const RRGame *g)
{
    int32_t z0 = ((g->z >> 16) / CELL - 2) * CELL, z1 = z0 + (WIN * 2 + 2) * CELL, z;
    if (z0 < -CELL) z0 = -CELL;
    if (z1 > RR_LEN + CELL) z1 = RR_LEN + CELL;
    for (z = z0; z < z1; z += SEG) {
        int32_t za = z, zb = z + SEG, ca = rr_centre(g, za), cb = rr_centre(g, zb), k;
        int32_t xs[2][5];
        int32_t ys[2][5];
        V3 p[2][5];
        static const int16_t off[5] = { -RR_TRACK_HW - 8, -RR_TRACK_HW, 0, RR_TRACK_HW, RR_TRACK_HW + 8 };
        int stripe = (int)((z / SEG) & 1);
        for (k = 0; k < 5; k++) {
            xs[0][k] = ca + off[k]; xs[1][k] = cb + off[k];
            ys[0][k] = rr_height(g, ca + off[k] / 2, za) + 2;      /* (the valley's sides start at the edge: measure inside it) */
            ys[1][k] = rr_height(g, cb + off[k] / 2, zb) + 2;
            cam_xform(&cam, xs[0][k], ys[0][k], za, &p[0][k]);
            cam_xform(&cam, xs[1][k], ys[1][k], zb, &p[1][k]);
        }
        if (p[1][2].z < NEAR_Z) continue;
        for (k = 0; k < 4; k++) {
            int pen = (k == 0 || k == 3) ? (stripe ? PEN_KERB : 1) : PEN_TRACK + ((k + stripe) & 1);
            a3_quad(&cam, ((p[0][k].z + p[1][k + 1].z) >> 1) + 20, pen, p[0][k], p[1][k], p[1][k + 1], p[0][k + 1]);
        }
    }
}

static void gates(const RRGame *g)
{
    int k;
    int32_t m[9];
    a3_rot_yaw(m, 0);
    for (k = g->gate; k < RR_GATES && k < g->gate + 3; k++) {
        int32_t gz = (k + 1) * RR_GATE_GAP, c = rr_centre(g, gz), s;
        V3 q[4];
        if (gz - (g->z >> 16) > (WIN * 2 + 2) * CELL) break;
        for (s = -1; s <= 1; s += 2) {
            int32_t px = c + s * RR_GATE_HW;
            a3_model(&cam, &pylon_m, px, rr_height(g, c + s * (RR_TRACK_HW - 10), gz), gz, m, NULL, 0, 0);
        }
        if (k == g->gate) {                 /* the next gate's banner */
            int32_t y0 = rr_height(g, c, gz) + 70, last = k == RR_GATES - 1;
            cam_xform(&cam, c - RR_GATE_HW, y0, gz, &q[0]);
            cam_xform(&cam, c + RR_GATE_HW, y0, gz, &q[1]);
            cam_xform(&cam, c + RR_GATE_HW, y0 + 16, gz, &q[2]);
            cam_xform(&cam, c - RR_GATE_HW, y0 + 16, gz, &q[3]);
            if (q[0].z > NEAR_Z) a3_poly(&cam, q[0].z - 4, last ? 1 : PEN_BANNER, q, 4);
            if (last)                        /* the finish: chequered */
                for (s = 0; s < 6; s++) {
                    int32_t xa = c - RR_GATE_HW + s * 30 + 2;
                    cam_xform(&cam, xa, y0 + 2 + (s & 1) * 6, gz, &q[0]);
                    cam_xform(&cam, xa + 13, y0 + 2 + (s & 1) * 6, gz, &q[1]);
                    cam_xform(&cam, xa + 13, y0 + 8 + (s & 1) * 6, gz, &q[2]);
                    cam_xform(&cam, xa, y0 + 8 + (s & 1) * 6, gz, &q[3]);
                    if (q[0].z > NEAR_Z) a3_poly(&cam, q[0].z - 6, 0, q, 4);
                }
        }
    }
}

static void shadow(int32_t x, int32_t y, int32_t z, int32_t r)
{
    V3 p[6];
    int k;
    for (k = 0; k < 6; k++) {
        int a = k * 10923;
        cam_xform(&cam, x + ((a3_sin(a) * r) >> 16), y, z + ((a3_cos(a) * r * 3 / 2) >> 16), &p[k]);
    }
    if (p[0].z > NEAR_Z && p[3].z > NEAR_Z) a3_poly(&cam, ((p[0].z + p[3].z) >> 1) + 6, PEN_SHADOW, p, 6);
}

static void bike(const RRGame *g, int32_t x, int32_t y, int32_t z, int yaw, int lean, int colour, int flame, int32_t bias)
{
    int32_t m[9];
    int32_t gy = rr_height(g, x, z) + 1;
    shadow(x, gy, z, 16);
    a3_rot_ypr(m, yaw, 0, lean);
    a3_model(&cam, &bike_m, x, y, z, m, body_pens[colour], 0, bias);
    if (flame) {
        V3 q[3];
        int32_t fx = a3_sin(yaw), fz = a3_cos(yaw), len = 26 + (int32_t)(g->tick % 3) * 8;
        cam_xform(&cam, x - ((fz * 6) >> 16) - ((fx * 18) >> 16), y + 2, z + ((fx * 6) >> 16) - ((fz * 18) >> 16), &q[0]);
        cam_xform(&cam, x + ((fz * 6) >> 16) - ((fx * 18) >> 16), y + 2, z - ((fx * 6) >> 16) - ((fz * 18) >> 16), &q[1]);
        cam_xform(&cam, x - ((fx * (18 + len)) >> 16), y + 3, z - ((fz * (18 + len)) >> 16), &q[2]);
        if (q[2].z > NEAR_Z) a3_poly(&cam, q[2].z - 10, (g->tick & 1) ? PEN_FLAME : PEN_YELLOW, q, 3);
    }
}

static void bikes(const RRGame *g)
{
    int k;
    for (k = 0; k < RR_RIVALS; k++) {
        const Rival *r = &g->rv[k];
        int32_t x = r->x >> 16, z = r->z >> 16, dz = z - (g->z >> 16);
        int yaw;
        if (r->finished || dz < -300 || dz > (WIN * 2) * CELL) continue;
        yaw = a3_atan2(rr_centre(g, z + 60) - rr_centre(g, z), 60);
        bike(g, x, rr_height(g, x, z) + RR_HOVER, z, yaw, 0, k + 1, 0, 0);
    }
    bike(g, g->x >> 16, g->y >> 16, g->z >> 16, g->yaw, -g->steer * 2200, 0, g->boosting, 0);
}

static void camera(const RRGame *g)
{
    int32_t fx = a3_sin(rn.cam_yaw), fz = a3_cos(rn.cam_yaw), gy;
    cam.x = (g->x >> 16) - ((fx * 150) >> 16);
    cam.z = (g->z >> 16) - ((fz * 150) >> 16);
    cam.y = (g->y >> 16) + 62;
    gy = rr_height(g, cam.x, cam.z) + 30;
    if (cam.y < gy) cam.y = gy;
    cam.yaw = rn.cam_yaw;
    cam.pitch = 1500;
    cam_view(&cam, SCREEN_W, VIEW_H, SCREEN_W / 2, 104, 210);
    cam_setup(&cam);
}

static void sky(Canvas *cv)
{
    int hz = cam_horizon(&cam), k;
    if (hz < 4) hz = 4;
    if (hz > VIEW_H - 1) hz = VIEW_H - 1;
    g_fill(cv, 0, 0, SCREEN_W - 1, hz - 1, PEN_SKY);
    g_fill(cv, 0, hz, SCREEN_W - 1, VIEW_H - 1, PEN_GROUND + 5);
    g_sync();
    /* far ridges, drifting as the camera turns */
    for (k = 0; k < 12; k++) {
        int w = 40 + (int)(a3_hash((uint32_t)k * 31u) % 40), h = 6 + (int)(a3_hash((uint32_t)k * 77u) % 18);
        int x = ((k * 61 - (rn.cam_yaw >> 6)) % 360 + 360) % 360 - 30;
        int16_t xs[3], ys[3];           /* C89: no run-time initialisers */
        xs[0] = (int16_t)(x - w / 2); xs[1] = (int16_t)x; xs[2] = (int16_t)(x + w / 2);
        ys[0] = (int16_t)hz; ys[1] = (int16_t)(hz - h); ys[2] = (int16_t)hz;
        g_poly(cv, xs, ys, 3, PEN_RIDGE, 0, hz);
    }
}

static void scene(Canvas *cv, const RRGame *g)
{
    camera(g);
    sky(cv);
    a3_begin(3000);
    ground(g);
    track(g);
    gates(g);
    bikes(g);
    a3_flush(cv);
}

/* ---------- the HUD ---------- */
static const char *const places[5] = { "", "1ST", "2ND", "3RD", "4TH" };

static void hud(Canvas *cv, const RRGame *g)
{
    char tmp[40];
    int k, p = rr_place_now(g), w;
    g_text_big(cv, 4, 4, p <= 3 ? PEN_GREEN : PEN_RED, places[p], 2);
    sprintf(tmp, "%ld", (long)(g->time_left / 25));
    w = (int)strlen(tmp) * 16;
    g_text_big(cv, (SCREEN_W - w) / 2, 4, g->time_left < 250 && (g->tick & 8) ? PEN_RED : PEN_YELLOW, tmp, 2);
    sprintf(tmp, "GATE %ld/%ld", (long)(g->gate < RR_GATES ? g->gate + 1 : RR_GATES), (long)RR_GATES);
    g_text_shadow(cv, 236, 12, 1, tmp);
    sprintf(tmp, "%ld", (long)g->score);
    g_text_shadow(cv, 236, 24, 3, tmp);
    /* speed and boost */
    w = (int)((g->v >> 16) * 4);
    g_box(cv, 4, 238, 84, 244, 0, 3);
    g_rect(cv, 5, 239, 5 + (w > 78 ? 78 : w), 243, g->v > RR_VMAX ? PEN_FLAME : PEN_CYAN);
    w = (int)((g->energy >> 16) * 78 / 100);
    g_box(cv, 4, 247, 84, 253, 0, 3);
    if (w > 0) g_rect(cv, 5, 248, 5 + w, 252, g->energy >= RR_ENERGY / 5 ? PEN_YELLOW : PEN_RED);
    /* the course: everyone's progress */
    g_rect(cv, 100, 248, 300, 249, 3);
    for (k = 0; k < RR_RIVALS; k++) {
        int x = 100 + (int)((g->rv[k].z >> 16) * 200 / RR_LEN);
        if (x < 100) x = 100;
        if (x > 300) x = 300;
        g_rect(cv, x - 1, 245, x + 1, 252, body_pens[k + 1][0]);
    }
    k = 100 + (int)((g->z >> 16) * 200 / RR_LEN);
    if (k > 300) k = 300;
    g_rect(cv, k - 2, 244, k + 2, 253, 1);
    if (g->offtrack && (g->tick & 4)) g_text_centre(cv, 220, PEN_RED, "OFF THE TRACK");
    if (rn.autoplay) g_text_shadow(cv, 4, 232, PEN_GREEN, "AUTO");
}

void render_game(Canvas *cv, const RRGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (rn.countdown > 0) {
        tmp[0] = (char)('1' + (rn.countdown - 1) / 25);
        tmp[1] = 0;
        g_text_big(cv, 148, 90, PEN_YELLOW, tmp, 4);
        sprintf(tmp, "TRACK %ld: %s", (long)g->track, rr_track_names[g->track - 1]);
        g_text_centre(cv, 150, 1, tmp);
    }
    if (g->finished && g->between) {
        g_text_big(cv, 112, 70, g->qualified ? PEN_GREEN : PEN_RED, places[g->place], 4);
        g_text_centre(cv, 120, 1, g->qualified ? (g->track < RR_TRACKS ? "QUALIFIED - NEXT TRACK" : "CHAMPION!") : "NOT QUALIFIED");
    }
    if (rn.state == RS_OVER) {
        g_box(cv, 50, 70, 270, 150, 2, g->won ? PEN_GREEN : PEN_RED);
        g_text_big(cv, g->won ? 92 : 76, 78, g->won ? PEN_GREEN : PEN_RED, g->won ? "CHAMPION" : "GAME OVER", 2);
        g_text_centre(cv, 112, PEN_YELLOW, g->won ? "ALL SIX TRACKS" : g->finished ? "FINISHED 4TH" : "OUT OF TIME");
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 126, 1, tmp);
#ifdef AMIGA3DO
        if (rn.state_timer == 0) g_text_centre(cv, 142, PEN_YELLOW, "PRESS A");
#else
        if (rn.state_timer == 0) g_text_centre(cv, 142, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (rn.paused == 1) g_text_centre(cv, 128, 1, "PAUSED");
    if (rn.flash_timer > 0 && rn.flash[0]) g_text_centre(cv, 60, rn.flash_pen, rn.flash);
}

void render_title(Canvas *cv, const RRGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot races behind the title */
    g_text_big(cv, 66, 26, 0, "RIDGE", 4);
    g_text_big(cv, 64, 24, PEN_YELLOW, "RIDGE", 4);
    g_text_big(cv, 50, 62, 0, "RUNNER", 4);
    g_text_big(cv, 48, 60, PEN_FLAME, "RUNNER", 4);
    g_box(cv, 40, 196, 280, 252, 2, 3);
    sprintf(tmp, "TRACK %ld: %s", (long)rn.start_track, rr_track_names[rn.start_track - 1]);
    g_text_centre(cv, 210, PEN_GREEN, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO RACE");
#else
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO RACE");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld   C: HOW TO PLAY", (long)rn.best);
#else
    sprintf(tmp, "BEST %ld   HELP: HOW TO PLAY", (long)rn.best);
#endif
    g_text_centre(cv, 242, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "RACE YOUR HOVER BIKE DOWN THE VALLEY",
      "AGAINST THREE RIVALS. FINISH IN THE",
      "FIRST THREE TO GO ON TO THE NEXT TRACK.",
      "",
      "PASS BETWEEN EACH GATE'S PYLONS: A GATE",
      "ADDS TIME TO THE CLOCK. MISS ONE AND",
      "YOU LOSE THREE SECONDS.",
      "",
      "OFF THE TRACK THE ROUGH GROUND SLOWS",
      "YOU. CRESTS THROW YOU INTO THE AIR -",
      "YOU CAN'T STEER MUCH UNTIL YOU LAND.",
      "SIX TRACKS, EACH TWISTIER AND HILLIER." },
    { "LEFT / RIGHT     STEER",
      "UP               THROTTLE",
      "DOWN             BRAKE",
#ifdef AMIGA3DO
      "A                BOOST (THE YELLOW BAR)",
#else
      "SPACE / FIRE     BOOST (THE YELLOW BAR)",
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
      "GATE 100. FINISH 3000 / 2000 / 1000",
      "FOR 1ST / 2ND / 3RD, AND 10 FOR EACH",
      "SECOND LEFT ON THE CLOCK." },
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
