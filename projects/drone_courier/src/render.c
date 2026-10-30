// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Drone Courier's picture: the sky and the ground, the towers
 * near the drone and ahead of the camera (picked by a cull over the city's
 * grid cells), the pads and the beacon over the next one, the drone with
 * its rotors and parcel and its shadow, and the HUD: the battery, the
 * clock, an arrow to the next pad, height and speed, the wind.
 *
 * Culling by grid cell (proposed for the engine): the city is a grid, so
 * instead of transforming every tower each frame, only the cells in the
 * square round the camera out to the view range are visited, and of
 * those only the ones ahead of it (a dot product with the camera's
 * heading) are drawn. On a 68020 the range is 560: about 25 towers of the
 * 144; on AmigaOS 4 it is 1400.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "dc.h"
#include "courier.h"

/* 0 black, 1 white, 2 panel, 3 grey, 4 sky (gradient), 5 ground, 6 park,
 * 7-12 towers in three tones (lit, shaded), 13 roofs, 14 depot pad,
 * 15 customer pad, 16 beacon, 17-18 the drone, 19 rotors, 20 parcel,
 * 21 fragile parcel, 22 shadow, 26 red, 27 yellow, 28 green, 29 cyan */
const uint16_t courier_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0x7AE, 0x667, 0x5A5, 0xCBA, 0x987, 0x9AB, 0x678, 0xBA9, 0x876,
    0x556, 0x39F, 0x3E6, 0xEF8, 0xEEE, 0x999, 0x444, 0xB85, 0xE44, 0x333, 0x000, 0x000, 0x000,
    0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000
};
#define PEN_SKY    4
#define PEN_GROUND 5
#define PEN_PARK   6
#define PEN_TOWER  7
#define PEN_ROOF   13
#define PEN_DEPOT  14
#define PEN_CUST   15
#define PEN_BEACON 16
#define PEN_DRONE  17
#define PEN_ROTOR  19
#define PEN_PARCEL 20
#define PEN_FRAGILE 21
#define PEN_SHADOW 22
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29

int32_t view_range = A3_SUB > 1 ? 1400 : 560;
int32_t towers_drawn;
static Cam cam;

static void quad(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                 int32_t x2, int32_t y2, int32_t z2, int32_t x3, int32_t y3, int32_t z3, int32_t bias, int col)
{
    V3 a, b, c, d;
    cam_xform(&cam, x0, y0, z0, &a); cam_xform(&cam, x1, y1, z1, &b);
    cam_xform(&cam, x2, y2, z2, &c); cam_xform(&cam, x3, y3, z3, &d);
    if (a.z < NEAR_Z && b.z < NEAR_Z && c.z < NEAR_Z && d.z < NEAR_Z) return;
    a3_quad(&cam, ((a.z + b.z + c.z + d.z) >> 2) + bias, col, a, b, c, d);
}

static void flat(int32_t x0, int32_t x1, int32_t z0, int32_t z1, int32_t y, int32_t bias, int col)
{
    quad(x0, y, z0, x1, y, z0, x1, y, z1, x0, y, z1, bias, col);
}

/* a tower: its roof and the (at most two) walls facing the camera */
static void tower(int32_t cx, int32_t cz, int32_t h, int tone)
{
    int32_t x0 = cx - DC_FOOT, x1 = cx + DC_FOOT, z0 = cz - DC_FOOT, z1 = cz + DC_FOOT;
    int lit = PEN_TOWER + 2 * tone, sh = lit + 1;
    flat(x0, x1, z0, z1, h, 0, PEN_ROOF);
    if (cam.z < z0) quad(x0, 0, z0, x1, 0, z0, x1, h, z0, x0, h, z0, 0, lit);
    else if (cam.z > z1) quad(x0, 0, z1, x1, 0, z1, x1, h, z1, x0, h, z1, 0, lit);
    if (cam.x < x0) quad(x0, 0, z0, x0, 0, z1, x0, h, z1, x0, h, z0, 0, sh);
    else if (cam.x > x1) quad(x1, 0, z0, x1, 0, z1, x1, h, z1, x1, h, z0, 0, sh);
}

static void city(const DCGame *g)
{
    int32_t fx = a3_sin(cam.yaw), fz = a3_cos(cam.yaw);
    int c0 = (int)((cam.x - view_range + DC_HALF) / DC_PITCH), c1 = (int)((cam.x + view_range + DC_HALF) / DC_PITCH);
    int r0 = (int)((cam.z - view_range + DC_HALF) / DC_PITCH), r1 = (int)((cam.z + view_range + DC_HALF) / DC_PITCH);
    int r, c;
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 >= DC_N) c1 = DC_N - 1;
    if (r1 >= DC_N) r1 = DC_N - 1;
    towers_drawn = 0;
    for (r = r0; r <= r1; r++)
        for (c = c0; c <= c1; c++) {
            int32_t cx = dc_block_x(c) >> 16, cz = dc_block_z(r) >> 16, dx = cx - cam.x, dz = cz - cam.z, h = g->height[r][c];
            int b = r * DC_N + c;
            /* behind the camera, or out of range: skip it */
            if (((dx * fx + dz * fz) >> 16) < -DC_PITCH) continue;
            if (dx > view_range || dx < -view_range || dz > view_range || dz < -view_range) continue;
            if (h == 0) { flat(cx - DC_FOOT, cx + DC_FOOT, cz - DC_FOOT, cz + DC_FOOT, 1, 60, PEN_PARK); continue; }
            tower(cx, cz, h, (r * 7 + c * 3) % 3);
            towers_drawn++;
            if (b == g->depot || b == dc_goal(g)) {
                int32_t pr = g->pad_r;
                flat(cx - pr, cx + pr, cz - pr, cz + pr, h + 1, -4, b == g->depot ? PEN_DEPOT : PEN_CUST);
                flat(cx - pr + 6, cx + pr - 6, cz - pr + 6, cz + pr - 6, h + 2, -5, PEN_ROOF);
                flat(cx - pr + 12, cx + pr - 12, cz - pr + 12, cz + pr - 12, h + 3, -6, b == g->depot ? PEN_DEPOT : PEN_CUST);
            }
        }
}

/* the beacon over the next pad: seen from anywhere ahead */
static void beacon(const DCGame *g)
{
    int b = dc_goal(g);
    int32_t cx = dc_block_x(b % DC_N) >> 16, cz = dc_block_z(b / DC_N) >> 16, h = g->height[b / DC_N][b % DC_N];
    int32_t w = 3 + (g->tick & 2);
    quad(cx - w, h, cz, cx + w, h, cz, cx + w, h + 260, cz, cx - w, h + 260, cz, -20, g->carrying ? PEN_CUST : PEN_DEPOT);
    quad(cx, h, cz - w, cx, h, cz + w, cx, h + 260, cz + w, cx, h + 260, cz - w, -20, PEN_BEACON);
}

static void drone(const DCGame *g)
{
    int32_t x = g->x >> 16, y = g->y >> 16, z = g->z >> 16, gy = dc_ground(g, g->x, g->z) >> 16;
    int32_t fx = a3_sin(g->yaw), fz = a3_cos(g->yaw), rx = fz, rz = -fx;
    int k;
    /* the shadow, straight down */
    flat(x - 14, x + 14, z - 14, z + 14, gy + 1, 30, PEN_SHADOW);
    /* the body, its four arms and rotors, the parcel */
#define P(a, b, yy) (x + ((fx * (a) + rx * (b)) >> 16)), (yy), (z + ((fz * (a) + rz * (b)) >> 16))
    quad(P(-10, -8, y + 4), P(-10, 8, y + 4), P(10, 8, y + 4), P(10, -8, y + 4), -2, PEN_DRONE);
    quad(P(10, -8, y), P(10, 8, y), P(10, 8, y + 4), P(10, -8, y + 4), -1, PEN_DRONE + 1);
    quad(P(-10, -8, y), P(-10, 8, y), P(-10, 8, y + 4), P(-10, -8, y + 4), -1, PEN_DRONE + 1);
    for (k = 0; k < 4; k++) {
        int32_t a = (k & 1) ? 16 : -16, bb = (k & 2) ? 16 : -16, s = 7 + (int32_t)((g->tick + k) & 1);
        quad(P(a - s, bb - s, y + 6), P(a - s, bb + s, y + 6), P(a + s, bb + s, y + 6), P(a + s, bb - s, y + 6), -3, PEN_ROTOR);
    }
    if (g->carrying) {
        int col = g->fragile ? PEN_FRAGILE : PEN_PARCEL;
        quad(P(-6, -6, y - 9), P(-6, 6, y - 9), P(6, 6, y - 9), P(6, -6, y - 9), -1, col);
        quad(P(-6, -6, y - 9), P(-6, 6, y - 9), P(-6, 6, y), P(-6, -6, y), 0, col);
        quad(P(6, -6, y - 9), P(6, 6, y - 9), P(6, 6, y), P(6, -6, y), 0, col);
    }
#undef P
}

static void scene(Canvas *cv, const DCGame *g)
{
    int32_t fx = a3_sin(co.cam_yaw), fz = a3_cos(co.cam_yaw), gy;
    int hz;
    cam.x = (g->x >> 16) - ((fx * 170) >> 16);
    cam.z = (g->z >> 16) - ((fz * 170) >> 16);
    cam.y = (g->y >> 16) + 70;
    gy = (dc_ground(g, (fix)cam.x << 16, (fix)cam.z << 16) >> 16) + 20;
    if (cam.y < gy) cam.y = gy;
    cam.yaw = co.cam_yaw;
    /* looking down at the ground under the drone (a little beyond it), so
     * the roof it is over stays in view however high it flies */
    cam.pitch = a3_atan2(cam.y - (int32_t)(dc_ground(g, g->x, g->z) >> 16), 300);
    if (cam.pitch < 1200) cam.pitch = 1200;
    if (cam.pitch > 9000) cam.pitch = 9000;
    cam_view(&cam, SCREEN_W, SCREEN_H, SCREEN_W / 2, 110, 200);
    cam_setup(&cam);
    hz = cam_horizon(&cam);
    if (hz < 1) hz = 1;
    if (hz > SCREEN_H - 1) hz = SCREEN_H - 1;
    g_fill(cv, 0, 0, SCREEN_W - 1, hz - 1, PEN_SKY);
    g_fill(cv, 0, hz, SCREEN_W - 1, SCREEN_H - 1, PEN_GROUND);
    g_sync();
    a3_begin(4000);
    city(g);
    beacon(g);
    drone(g);
    a3_flush(cv);
}

static void hud(Canvas *cv, const DCGame *g)
{
    char tmp[40];
    int k, w, b = dc_goal(g), rel;
    fix dx = dc_block_x(b % DC_N) - g->x, dz = dc_block_z(b / DC_N) - g->z;
    sprintf(tmp, "DAY %ld  %ld/%ld", (long)g->day, (long)g->jobs_done, (long)g->jobs);
    g_text_shadow(cv, 4, 10, PEN_YELLOW, tmp);
    for (k = 0; k < g->fails; k++) g_text_shadow(cv, 4 + k * 9, 22, PEN_RED, "X");
    sprintf(tmp, "%ld", (long)(g->job_time / 25));
    g_text_big(cv, 4, 26, g->job_time < 250 && (g->tick & 8) ? PEN_RED : 1, tmp, 2);
    sprintf(tmp, "%ld", (long)g->score);
    g_text_shadow(cv, SCREEN_W - 8 - (int)strlen(tmp) * 8, 10, 3, tmp);
    /* the arrow to the next pad, turned with the view */
    rel = a3_atan2(dx >> 8, dz >> 8) - co.cam_yaw;
    {
        int cx = SCREEN_W / 2, cy = 22;
        int16_t xs[3], ys[3];
        xs[0] = (int16_t)(cx + ((a3_sin(rel) * 14) >> 16)); ys[0] = (int16_t)(cy - ((a3_cos(rel) * 14) >> 16));
        xs[1] = (int16_t)(cx + ((a3_sin(rel + 26000) * 9) >> 16)); ys[1] = (int16_t)(cy - ((a3_cos(rel + 26000) * 9) >> 16));
        xs[2] = (int16_t)(cx + ((a3_sin(rel - 26000) * 9) >> 16)); ys[2] = (int16_t)(cy - ((a3_cos(rel - 26000) * 9) >> 16));
        g_poly(cv, xs, ys, 3, g->carrying ? PEN_CUST : PEN_DEPOT, 0, SCREEN_H - 1);
        sprintf(tmp, "%ld", (long)(fhypot(dx, dz) >> 16));
        g_text_shadow(cv, cx - (int)strlen(tmp) * 4, cy + 22, 1, tmp);
    }
    g_text_shadow(cv, SCREEN_W / 2 - 40, 58, g->carrying ? PEN_CUST : PEN_DEPOT, g->carrying ? (g->fragile ? "FRAGILE: DELIVER" : "DELIVER") : "COLLECT");
    /* the battery */
    g_text_shadow(cv, 4, SCREEN_H - 4, 3, "BATT");
    g_box(cv, 40, SCREEN_H - 12, 122, SCREEN_H - 4, 0, 3);
    w = (int)((g->battery >> 16) * 80 / 100);
    if (w > 0) g_rect(cv, 41, SCREEN_H - 11, 41 + w, SCREEN_H - 5, w > 20 ? PEN_GREEN : PEN_RED);
    sprintf(tmp, "ALT %ld", (long)(g->y >> 16));
    g_text_shadow(cv, 140, SCREEN_H - 4, 3, tmp);
    if (g->wind_x || g->wind_z || g->gust_x) {
        int wx = (int)((g->wind_x + g->gust_x) >> 9), wz = (int)((g->wind_z + g->gust_z) >> 9);
        g_text_shadow(cv, 230, SCREEN_H - 4, 3, "WIND");
        g_rect(cv, 290, SCREEN_H - 9, 290, SCREEN_H - 9, 1);
        g_rect(cv, 290 + (wx > 0 ? 0 : wx), SCREEN_H - 9, 290 + (wx > 0 ? wx : 0), SCREEN_H - 8, PEN_CYAN);
        g_rect(cv, 289, SCREEN_H - 9 - (wz > 0 ? wz : 0), 290, SCREEN_H - 9 - (wz > 0 ? 0 : wz), PEN_CYAN);
    }
    if (co.autoplay) g_text_shadow(cv, SCREEN_W - 40, 22, PEN_GREEN, "AUTO");
}

void render_game(Canvas *cv, const DCGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (g->between) {
        g_text_big(cv, 80, 80, PEN_GREEN, "DAY'S DONE", 2);
        if (g->day < DC_DAYS) { sprintf(tmp, "DAY %ld NEXT", (long)g->day + 1); g_text_centre(cv, 110, 1, tmp); }
    }
    if (co.state == CS_OVER) {
        g_box(cv, 50, 80, 270, 160, 2, g->won ? PEN_GREEN : PEN_RED);
        g_text_big(cv, g->won ? 60 : 76, 88, g->won ? PEN_GREEN : PEN_RED, g->won ? "WEEK'S DONE" : "GAME OVER", 2);
        sprintf(tmp, "DAY %ld  %ld FAILED", (long)g->day, (long)g->fails);
        g_text_centre(cv, 122, PEN_YELLOW, g->battery == 0 ? "THE BATTERY WENT FLAT" : tmp);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 136, 1, tmp);
#ifdef AMIGA3DO
        if (co.state_timer == 0) g_text_centre(cv, 152, PEN_YELLOW, "PRESS A");
#else
        if (co.state_timer == 0) g_text_centre(cv, 152, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (co.paused == 1) g_text_centre(cv, 180, 1, "PAUSED");
    if (co.flash_timer > 0 && co.flash[0]) g_text_centre(cv, 76, co.flash_pen, co.flash);
}

void render_title(Canvas *cv, const DCGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot works behind the title */
    g_text_big(cv, 66, 26, 0, "DRONE", 4);
    g_text_big(cv, 64, 24, PEN_YELLOW, "DRONE", 4);
    g_text_big(cv, 34, 62, 0, "COURIER", 4);
    g_text_big(cv, 32, 60, PEN_CUST, "COURIER", 4);
    g_box(cv, 40, 196, 280, 252, 2, 3);
    sprintf(tmp, "START ON DAY %ld (UP/DOWN)", (long)co.start_day);
    g_text_centre(cv, 210, PEN_GREEN, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO FLY");
#else
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO FLY");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld   C: HOW TO PLAY", (long)co.best);
#else
    sprintf(tmp, "BEST %ld   HELP: HOW TO PLAY", (long)co.best);
#endif
    g_text_centre(cv, 242, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "COLLECT EACH PARCEL FROM THE DEPOT'S",
      "BLUE PAD AND LAND IT ON THE CUSTOMER'S",
      "GREEN PAD. THE ARROW AND THE BEACON",
      "SHOW THE WAY.",
      "",
      "TOUCH DOWN GENTLY ON THE PAD. FRAGILE",
      "PARCELS (RED) BREAK IF YOU LAND HARD",
      "OR KNOCK A TOWER.",
      "",
      "THE BATTERY DRAINS - FASTER CLIMBING -",
      "AND FILLS AT THE DEPOT. WIND FROM DAY 2.",
      "3 FAILED JOBS END THE GAME. 5 DAYS." },
    { "UP / DOWN        FORWARD / BACK",
      "LEFT / RIGHT     TURN",
#ifdef AMIGA3DO
      "A                CLIMB (LET GO: SINK)",
#else
      "SPACE / FIRE     CLIMB (LET GO: SINK)",
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
      "DELIVERY 200 + 5 A SECOND LEFT,",
      "+150 FRAGILE. A DAY DONE: 500 x DAY.",
      "",
      "" },
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
