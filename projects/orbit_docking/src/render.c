// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Orbit Docking's picture, from the cockpit: the stars, the
 * spinning station (its hub, the docking slot with its lights, four arms
 * and a ring), the approach markers along its axis, debris, and the
 * instruments: range, closing rate, where the axis is, the roll against
 * the slot and the fuel.
 *
 * The view rolls with the craft. The engine's camera turns about two
 * axes (yaw and pitch); here a third, the roll, turns everything about
 * the middle of the view after the camera has done its part (cam_roll).
 * Rolled to match the slot, the station stands still.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "a3d_i64.h"
#include "od.h"
#include "dock.h"

/* 0 black, 1 white, 2 panel, 3 grey, 4 space, 5 stars, 6-7 the hub,
 * 8 its face, 9 the slot, 10 green lights, 11 red lights, 12-13 arms,
 * 14-15 the ring, 16 markers, 17 debris, 18-19 solar panels, 26 red,
 * 27 yellow, 28 green, 29 cyan */
const uint16_t dock_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0x002, 0xCCD, 0x99A, 0x667, 0xBBC, 0x111, 0x3F5, 0xF33,
    0x889, 0x556, 0xAAB, 0x778, 0x0A8, 0x986, 0x35A, 0x238, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000
};
#define PEN_SPACE  4
#define PEN_STAR   5
#define PEN_HUB    6
#define PEN_FACE   8
#define PEN_SLOT   9
#define PEN_LIGHT  10
#define PEN_RLIGHT 11
#define PEN_ARM    12
#define PEN_RING   14
#define PEN_MARK   16
#define PEN_DEBRIS 17
#define PEN_PANEL  18
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29

static Cam cam;
static int32_t rc, rs;                  /* the craft's roll (16.16 cos, sin) */
static int32_t sc, ss;                  /* the station's turn */

/* ---------- the camera, with roll ---------- */
/* proposed for the engine: a3d's Cam has yaw and pitch; a roll after
 * them, about the line of sight, is a turn of camera space's x and y */
static void cam_roll(V3 *p)
{
    int32_t x = p->x, y = p->y;
    p->x = (x * rc - y * rs) >> 16;
    p->y = (x * rs + y * rc) >> 16;
}

static void wpt(int32_t x, int32_t y, int32_t z, V3 *out)
{
    cam_xform(&cam, x, y, z, out);
    cam_roll(out);
}

/* a point on the station: turned with it about the z axis */
static void spt(int32_t x, int32_t y, int32_t z, V3 *out)
{
    wpt((x * sc - y * ss) >> 16, (x * ss + y * sc) >> 16, z, out);
}

static void squad(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                  int32_t z0, int32_t z1, int32_t bias, int col)
{
    V3 a, b, c, d;
    spt(x0, y0, z0, &a); spt(x1, y1, z0, &b); spt(x2, y2, z1, &c); spt(x3, y3, z1, &d);
    a3_quad(&cam, ((a.z + b.z + c.z + d.z) >> 2) + bias, col, a, b, c, d);
}

static void station(void)
{
    int k;
    V3 face[8];
    /* the hub: eight sides and its face */
    for (k = 0; k < 8; k++) {
        int a0 = k * 8192 + 4096, a1 = a0 + 8192;
        int32_t x0 = (a3_cos(a0) * OD_HUB_R) >> 16, y0 = (a3_sin(a0) * OD_HUB_R) >> 16;
        int32_t x1 = (a3_cos(a1) * OD_HUB_R) >> 16, y1 = (a3_sin(a1) * OD_HUB_R) >> 16;
        squad(x0, y0, x1, y1, x1, y1, x0, y0, OD_PORT_Z, -300, 0, PEN_HUB + (k & 1));
        spt(x0, y0, OD_PORT_Z, &face[k]);
    }
    a3_poly(&cam, face[0].z - 4, PEN_FACE, face, 8);
    /* the slot and its lights */
    squad(-24, -7, 24, -7, 24, 7, -24, 7, OD_PORT_Z + 1, OD_PORT_Z + 1, -8, PEN_SLOT);
    squad(-34, -4, -28, -4, -28, 4, -34, 4, OD_PORT_Z + 1, OD_PORT_Z + 1, -9, PEN_LIGHT);
    squad(28, -4, 34, -4, 34, 4, 28, 4, OD_PORT_Z + 1, OD_PORT_Z + 1, -9, PEN_LIGHT);
    squad(-3, 30, 3, 30, 3, 36, -3, 36, OD_PORT_Z + 1, OD_PORT_Z + 1, -9, PEN_RLIGHT);
    /* four arms, out to the ring */
    for (k = 0; k < 4; k++) {
        int32_t c = a3_cos(k * 16384), s = a3_sin(k * 16384);
        int32_t ax = (c * 60) >> 16, ay = (s * 60) >> 16, bx = (c * 260) >> 16, by = (s * 260) >> 16;
        int32_t px = (-s * 10) >> 16, py = (c * 10) >> 16;
        squad(ax + px, ay + py, bx + px, by + py, bx - px, by - py, ax - px, ay - py, -60, -60, 0, PEN_ARM);
        squad(ax + px, ay + py, bx + px, by + py, bx + px, by + py, ax + px, ay + py, -60, -100, 2, PEN_ARM + 1);
    }
    /* the ring */
    for (k = 0; k < 16; k++) {
        int a0 = k * 4096, a1 = a0 + 4096;
        int32_t c0 = a3_cos(a0), s0 = a3_sin(a0), c1 = a3_cos(a1), s1 = a3_sin(a1);
        squad((c0 * 260) >> 16, (s0 * 260) >> 16, (c1 * 260) >> 16, (s1 * 260) >> 16,
              (c1 * 300) >> 16, (s1 * 300) >> 16, (c0 * 300) >> 16, (s0 * 300) >> 16, -60, -60, 0, PEN_RING + (k & 1));
        squad((c0 * 300) >> 16, (s0 * 300) >> 16, (c1 * 300) >> 16, (s1 * 300) >> 16,
              (c1 * 300) >> 16, (s1 * 300) >> 16, (c0 * 300) >> 16, (s0 * 300) >> 16, -60, -120, 2, PEN_RING + 1);
    }
    /* solar panels beyond the ring */
    for (k = 0; k < 2; k++) {
        int32_t sg = k ? -1 : 1;
        squad(sg * 310, -40, sg * 470, -40, sg * 470, 40, sg * 310, 40, -90, -90, 0, PEN_PANEL + k);
    }
}

/* the approach markers: squares round the axis, standing still */
static void markers(const ODGame *g)
{
    static const int16_t zs[3] = { 420, 260, 150 };
    int k, s;
    for (k = 0; k < 3; k++) {
        int32_t z = zs[k], h = 34, t = 2;
        V3 a, b, c, d;
        if (FROMI(z) >= g->z - FROMI(20)) continue;
        for (s = 0; s < 4; s++) {
            int32_t x0 = s == 1 ? h - t : -h, x1 = s == 3 ? -h + t : h, y0 = s == 0 ? h - t : -h, y1 = s == 2 ? -h + t : h;
            wpt(x0, y0, z, &a); wpt(x1, y0, z, &b); wpt(x1, y1, z, &c); wpt(x0, y1, z, &d);
            a3_quad(&cam, a.z - 2, PEN_MARK, a, b, c, d);
        }
    }
}

static void debris(const ODGame *g)
{
    int k, j;
    for (k = 0; k < g->ndebris; k++) {
        const Debris *d = &g->debris[k];
        int32_t x = od_debris_x(g, k) >> 16, ang = g->level_ticks * 900 + k * 9000;
        V3 c, p[6];
        wpt(x, d->y, d->z, &c);
        if (c.z < NEAR_Z + 30) continue;
        for (j = 0; j < 6; j++) {
            int a = (int)(ang + j * 10923) & 0xFFFF;
            int32_t r = (j & 1) ? 14 : 24;
            p[j].x = c.x + ((a3_cos(a) * r) >> 16);
            p[j].y = c.y + ((a3_sin(a) * r) >> 16);
            p[j].z = c.z;
        }
        a3_poly(&cam, c.z, PEN_DEBRIS, p, 6);
    }
}

static void stars(Canvas *cv)
{
    int k, cx = SCREEN_W / 2, cy = SCREEN_H / 2;
    for (k = 0; k < 60; k++) {
        uint32_t h = a3_hash((uint32_t)k * 2654435761u);
        int32_t sx = (int32_t)(h % 640) - 320, sy = (int32_t)((h >> 10) % 640) - 320;
        int x = cx + (int)((sx * rc - sy * rs) >> 16), y = cy - (int)((sx * rs + sy * rc) >> 16);
        if (x >= 0 && y >= 0 && x < SCREEN_W && y < SCREEN_H) g_rect(cv, x, y, x, y, (k & 3) ? PEN_STAR : 1);
    }
}

static void scene(Canvas *cv, const ODGame *g)
{
    int ra = (int)((g->roll >> 8) & 0xFFFF), sa = (int)((g->st_angle >> 8) & 0xFFFF);
    rc = a3_cos(ra); rs = a3_sin(ra);
    sc = a3_cos(sa); ss = a3_sin(sa);
    cam.x = g->x >> 16;
    cam.y = g->y >> 16;
    cam.z = g->z >> 16;
    cam.yaw = 0x8000;                   /* looking back down the axis at the station */
    cam.pitch = 0;
    cam_view(&cam, SCREEN_W, SCREEN_H, SCREEN_W / 2, SCREEN_H / 2, 230);
    cam_setup(&cam);
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_SPACE);
    g_sync();
    stars(cv);
    a3_begin(2500);
    station();
    markers(g);
    debris(g);
    a3_flush(cv);
}

/* ---------- the instruments ---------- */
static void fixed_text(char *out, fix v)
{
    int32_t t = (int32_t)((v * 10) >> 16);
    if (t < 0) t = -t;
    sprintf(out, "%s%ld.%ld", v < 0 ? "-" : "", (long)(t / 10), (long)(t % 10));
}

static void hud(Canvas *cv, const ODGame *g)
{
    char tmp[40], num[16];
    int k, err = od_roll_error(g), x, y, ok;
    int32_t range = (g->z >> 16) - (OD_PORT_Z + OD_NOSE), spin = (g->roll_rate - g->st_spin) >> 8;
    sprintf(tmp, "STATION %ld", (long)g->level);
    g_text_shadow(cv, 4, 10, PEN_YELLOW, tmp);
    for (k = 0; k < g->lives; k++) g_rect(cv, 4 + k * 10, 15, 10 + k * 10, 19, PEN_CYAN);
    sprintf(tmp, "RANGE %ld", (long)range);
    g_text_shadow(cv, 4, 34, 1, tmp);
    fixed_text(num, -g->vz);
    sprintf(tmp, "CLOSE %s", num);
    g_text_shadow(cv, 4, 46, range < 150 && -g->vz > OD_TOL_CLOSE ? PEN_RED : range < 150 ? PEN_GREEN : 1, tmp);
    sprintf(tmp, "%ld", (long)g->score);
    g_text_shadow(cv, 4, 58, 3, tmp);
    /* where the axis is: the craft's offset from it, and its drift */
    g_box(cv, SCREEN_W - 52, 4, SCREEN_W - 4, 52, 2, 3);
    g_rect(cv, SCREEN_W - 28, 6, SCREEN_W - 28, 50, 3);
    g_rect(cv, SCREEN_W - 50, 28, SCREEN_W - 6, 28, 3);
    x = SCREEN_W - 28 + (int)((g->x >> 16) / 6);
    y = 28 - (int)((g->y >> 16) / 6);
    ok = fhypot(g->x, g->y) < FROMI(OD_TOL_AXIS);
    if (x > SCREEN_W - 50 && x < SCREEN_W - 6 && y > 6 && y < 50) g_rect(cv, x - 2, y - 2, x + 2, y + 2, ok ? PEN_GREEN : PEN_RED);
    g_text_shadow(cv, SCREEN_W - 50, 64, 3, "AXIS");
    /* the roll against the slot: a needle, and how the turn matches */
    {
        int cx = SCREEN_W / 2, cy = SCREEN_H - 30, r = 22;
        int16_t xs[3], ys[3];
        int a = err * 2;                /* the slot repeats every half turn: show it a full turn */
        g_circle(cv, cx, cy, r + 2, 3);
        g_circle(cv, cx, cy, r, 2);
        g_rect(cv, cx - 1, cy - r, cx + 1, cy - r + 5, PEN_GREEN);
        xs[0] = (int16_t)(cx + ((a3_sin(a) * r) >> 16)); ys[0] = (int16_t)(cy - ((a3_cos(a) * r) >> 16));
        xs[1] = (int16_t)(cx + ((a3_sin(a + 16384) * 3) >> 16)); ys[1] = (int16_t)(cy - ((a3_cos(a + 16384) * 3) >> 16));
        xs[2] = (int16_t)(cx - ((a3_sin(a + 16384) * 3) >> 16)); ys[2] = (int16_t)(cy + ((a3_cos(a + 16384) * 3) >> 16));
        g_poly(cv, xs, ys, 3, (err < OD_TOL_ROLL && err > -OD_TOL_ROLL) ? PEN_GREEN : PEN_YELLOW, 0, SCREEN_H - 1);
        g_text_shadow(cv, cx + 30, cy + 4, (spin < 20 && spin > -20) ? PEN_GREEN : PEN_RED,
                      spin < -20 ? "<< TURN" : spin > 20 ? "TURN >>" : "MATCHED");
    }
    /* fuel */
    g_text_shadow(cv, 4, SCREEN_H - 4, 3, "FUEL");
    g_box(cv, 40, SCREEN_H - 12, 120, SCREEN_H - 4, 0, 3);
    k = (int)(g->fuel_max > 0 ? a3_muldiv(g->fuel, 78, g->fuel_max) : 0);   /* 3DO: no int64_t */
    if (k > 0) g_rect(cv, 41, SCREEN_H - 11, 41 + k, SCREEN_H - 5, k > 20 ? PEN_CYAN : PEN_RED);
    /* the sight */
    g_rect(cv, SCREEN_W / 2 - 12, SCREEN_H / 2, SCREEN_W / 2 - 5, SCREEN_H / 2, PEN_YELLOW);
    g_rect(cv, SCREEN_W / 2 + 5, SCREEN_H / 2, SCREEN_W / 2 + 12, SCREEN_H / 2, PEN_YELLOW);
    g_rect(cv, SCREEN_W / 2, SCREEN_H / 2 - 12, SCREEN_W / 2, SCREEN_H / 2 - 5, PEN_YELLOW);
    if (dk.autoplay) g_text_shadow(cv, SCREEN_W - 40, SCREEN_H - 4, PEN_GREEN, "AUTO");
}

void render_game(Canvas *cv, const ODGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (g->between) {
        g_text_big(cv, 96, 80, PEN_GREEN, "DOCKED", 3);
        if (g->level < OD_LEVELS) { sprintf(tmp, "STATION %ld NEXT", (long)g->level + 1); g_text_centre(cv, 120, 1, tmp); }
    }
    if (dk.state == DS_PLAY && g->level_ticks < 60 && !g->between) {
        sprintf(tmp, "STATION %ld: DOCK WITH THE SLOT", (long)g->level);
        g_text_centre(cv, 80, PEN_YELLOW, tmp);
    }
    if (dk.state == DS_OVER) {
        g_box(cv, 50, 80, 270, 160, 2, g->won ? PEN_GREEN : PEN_RED);
        g_text_big(cv, g->won ? 66 : 76, 88, g->won ? PEN_GREEN : PEN_RED, g->won ? "ALL DOCKED" : "GAME OVER", 2);
        sprintf(tmp, "%ld DOCKED  %ld CRASHED", (long)g->docks, (long)g->crashes);
        g_text_centre(cv, 122, PEN_YELLOW, tmp);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 136, 1, tmp);
#ifdef AMIGA3DO
        if (dk.state_timer == 0) g_text_centre(cv, 152, PEN_YELLOW, "PRESS A");
#else
        if (dk.state_timer == 0) g_text_centre(cv, 152, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (dk.paused == 1) g_text_centre(cv, 160, 1, "PAUSED");
    if (dk.flash_timer > 0 && dk.flash[0]) g_text_centre(cv, 100, dk.flash_pen, dk.flash);
}

void render_title(Canvas *cv, const ODGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot docks behind the title */
    g_text_big(cv, 66, 18, 0, "ORBIT", 4);
    g_text_big(cv, 64, 16, PEN_YELLOW, "ORBIT", 4);
    g_text_big(cv, 34, 54, 0, "DOCKING", 4);
    g_text_big(cv, 32, 52, PEN_CYAN, "DOCKING", 4);
    g_box(cv, 40, 196, 280, 252, 2, 3);
    sprintf(tmp, "START AT STATION %ld (UP/DOWN)", (long)dk.start_level);
    g_text_centre(cv, 210, PEN_GREEN, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO FLY");
#else
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO FLY");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld", (long)dk.best);
#else
    sprintf(tmp, "BEST %ld   HELP: HOW TO PLAY", (long)dk.best);
#endif
    g_text_centre(cv, 242, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "DOCK WITH THE SLOT IN THE STATION'S HUB.",
      "IT SPINS: ROLL TO MATCH IT. WHEN YOU",
      "TURN WITH IT, THE STATION STANDS STILL.",
      "",
      "COME IN ALONG THE AXIS (THE BOX AT THE",
      "TOP RIGHT), SLOWLY: UNDER 1.6 AT CONTACT.",
      "THE DIAL: THE SLOT'S ROLL. GREEN IS GOOD.",
      "",
      "NO FRICTION IN SPACE: A PUSH LASTS UNTIL",
      "YOU PUSH BACK. FUEL IS LIMITED.",
      "FROM STATION 4, DEBRIS DRIFTS ACROSS.",
      "THREE CRASHES AND IT'S OVER." },
#ifdef AMIGA3DO
    { "D-PAD               MOVE ACROSS",
#else
    { "ARROWS / JOYSTICK   MOVE ACROSS",
#endif
#ifdef AMIGA3DO
      "A                   FORWARD (IN)",
#else
      "W / FIRE + UP       FORWARD (IN)",
#endif
#ifdef AMIGA3DO
      "C                   BACK (OUT)",
#else
      "S / FIRE + DOWN     BACK (OUT)",
#endif
#ifdef AMIGA3DO
      "L / R               ROLL",
#else
      "A / D               ROLL",
#endif
#ifdef AMIGA3DO
      "",
#else
      "FIRE + LEFT/RIGHT   ROLL",
#endif
      "P                   PAUSE",
      "B                   AUTOPILOT",
#ifdef AMIGA3DO
      "",
#else
      "HELP / H            THESE PAGES",
#endif
#ifdef AMIGA3DO
      "X                   THE MENU",
#else
      "ESC                 TITLE / QUIT",
#endif
      "",
      "DOCKING: 1000 + 500 x STATION, 2 FOR",
      "EACH UNIT OF FUEL LEFT, LESS TIME." },
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
    g_text(cv, 156, 242, PEN_YELLOW, "A: PLAY");
#else
    g_text(cv, 156, 242, PEN_YELLOW, "HELP: NEXT  FIRE: PLAY");
#endif
}
