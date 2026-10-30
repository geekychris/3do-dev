// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Harbour Crane's picture: the sky and the far shore, the sea,
 * the quay with the containers waiting on it, the gantry crane with its
 * trolley, cable and hook, the ship (rolled by her heel and lifted by the
 * swell) with what is loaded on her, the container on the hook (swung
 * with the cable) or on its way down, and the score line with the list
 * gauge and the wind.
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "a3d_i64.h"
#include "hc.h"
#include "crane.h"

/* palette: 0 black, 1 white, 2 panel, 3 grey, 4 sky (gradient), 5-6 the
 * sea, 7 the far shore, 8-9 the quay, 10-11 the crane, 12-13 the hull,
 * 14 the deck, 15 the bridge, 16 the cable, 17-24 containers by weight
 * (lit, shaded), 25 windows, 26 red, 27 yellow, 28 green, 29 cyan,
 * 30 shadow, 31 foam */
const uint16_t crane_palette[32] = {
    0x000, 0xFFF, 0x123, 0x99A, 0x7AD, 0x236, 0x358, 0x576, 0xBBA, 0x776,
    0xFC2, 0xA81, 0x23A, 0x125, 0x733, 0xEED, 0x333,
    0x5C5, 0x383, 0x48E, 0x259, 0xF93, 0xB52, 0xE44, 0x922,
    0x6CE, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x112, 0xDEF
};
#define PEN_SKY    4
#define PEN_SEA    5
#define PEN_SHORE  7
#define PEN_QUAY   8
#define PEN_CRANE  10
#define PEN_HULL   12
#define PEN_DECK   14
#define PEN_BRIDGE 15
#define PEN_CABLE  16
#define PEN_BOX    17
#define PEN_WINDOW 25
#define PEN_RED    26
#define PEN_YELLOW 27
#define PEN_GREEN  28
#define PEN_CYAN   29
#define PEN_SHADOW 30
#define PEN_FOAM   31

#define BOX_Z    20                     /* containers: z -20..20 */

static Cam cam;

/* how a part of the scene is placed: 0 the world, 1 the ship (rolled
 * about the pivot by heel and lifted by bob), 2 swung about (px, py) by th */
static int xf_mode;
static int32_t xf_c, xf_s, xf_px, xf_py, xf_bob;

static void xf_world(void) { xf_mode = 0; }

static void xf_ship(const HCGame *g)
{
    int a = a3_angle(g->heel);
    xf_mode = 1;
    xf_c = a3_cos(a);
    xf_s = a3_sin(a);
    xf_px = HC_PIVOT_X;
    xf_py = HC_PIVOT_Y;
    xf_bob = g->bob >> 16;
}

static void xf_swing(int32_t px, int32_t py, fix th)
{
    int a = a3_angle(th);
    xf_mode = 2;
    xf_c = a3_cos(a);
    xf_s = a3_sin(a);
    xf_px = px;
    xf_py = py;
    xf_bob = 0;
}

static void pt(int32_t x, int32_t y, int32_t z, V3 *out)
{
    if (xf_mode) {
        int32_t dx = x - xf_px, dy = y - xf_py;
        if (xf_mode == 1) {                 /* heel: the right side down */
            x = xf_px + ((dx * xf_c + dy * xf_s) >> 16);
            y = xf_py + ((dy * xf_c - dx * xf_s) >> 16) + xf_bob;
        } else {                            /* th: the bottom swings to +x */
            x = xf_px + ((dx * xf_c - dy * xf_s) >> 16);
            y = xf_py + ((dy * xf_c + dx * xf_s) >> 16);
        }
    }
    cam_xform(&cam, x, y, z, out);
}

static void quad(int32_t x0, int32_t y0, int32_t z0, int32_t x1, int32_t y1, int32_t z1,
                 int32_t x2, int32_t y2, int32_t z2, int32_t x3, int32_t y3, int32_t z3, int32_t bias, int col)
{
    V3 a, b, c, d;
    pt(x0, y0, z0, &a); pt(x1, y1, z1, &b); pt(x2, y2, z2, &c); pt(x3, y3, z3, &d);
    a3_quad(&cam, ((a.z + b.z + c.z + d.z) >> 2) + bias, col, a, b, c, d);
}

/* a box: its front, its top, and the end facing the camera */
static void box(int32_t x0, int32_t x1, int32_t y0, int32_t y1, int32_t z0, int32_t z1, int lit, int shade, int32_t bias)
{
    quad(x0, y0, z0, x1, y0, z0, x1, y1, z0, x0, y1, z0, bias, lit);
    quad(x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1, bias + 1, shade);
    if (x1 < cam.x) quad(x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1, bias + 1, shade);
    else if (x0 > cam.x) quad(x0, y0, z0, x0, y1, z0, x0, y1, z1, x0, y0, z1, bias + 1, shade);
}

/* a container: coloured by weight, with a darker band top and bottom so
 * stacked ones read apart */
static void container(int32_t x0, int32_t y0, int w, int weight, int32_t bias)
{
    int lit = PEN_BOX + 2 * (weight - 1), sh = lit + 1;
    box(x0, x0 + w, y0, y0 + HC_BOX_H, -BOX_Z, BOX_Z, sh, sh, bias);
    quad(x0 + 2, y0 + 3, -BOX_Z - 1, x0 + w - 2, y0 + 3, -BOX_Z - 1, x0 + w - 2, y0 + HC_BOX_H - 3, -BOX_Z - 1,
         x0 + 2, y0 + HC_BOX_H - 3, -BOX_Z - 1, bias - 1, lit);
    quad(x0 + 2, y0 + HC_BOX_H, -BOX_Z, x0 + w - 2, y0 + HC_BOX_H, -BOX_Z, x0 + w - 2, y0 + HC_BOX_H, BOX_Z,
         x0 + 2, y0 + HC_BOX_H, BOX_Z, bias, lit);
}

static void background(Canvas *cv, const HCGame *g)
{
    int k;
    int32_t z;
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, PEN_SKY);
    g_sync();
    a3_begin(4000);
    /* the far shore: low hills and a few buildings */
    quad(-2400, HC_WATER_Y, 2600, 2600, HC_WATER_Y, 2600, 2600, 40, 2600, -2400, 40, 2600, 0, PEN_SHORE);
    for (k = 0; k < 9; k++) {
        int32_t x = -1700 + k * 420 + (k * 97 % 150), h = 70 + (k * 53 % 110);
        quad(x, 30, 2590, x + 140, 30, 2590, x + 140, 30 + h, 2590, x, 30 + h, 2590, 0, PEN_SHORE + 2 * (k & 1));
    }
    /* the sea, in bands that drift with the swell */
    for (z = -400, k = 0; z < 2600; z += 300, k++) {
        int32_t zz = z + (int32_t)((g->phase >> 12) * 300 / 102);
        quad(-2400, HC_WATER_Y, zz, 2600, HC_WATER_Y, zz, 2600, HC_WATER_Y, zz + 300, -2400, HC_WATER_Y, zz + 300, 0, PEN_SEA + (k & 1));
    }
    a3_flush(cv);
}

static void quay(const HCGame *g)
{
    int k;
    box(-700, HC_QUAY_X1, HC_WATER_Y - 30, 0, -120, 260, PEN_QUAY + 1, PEN_QUAY + 1, 0);
    quad(-700, 0, -120, HC_QUAY_X1, 0, -120, HC_QUAY_X1, 0, 260, -700, 0, 260, 2, PEN_QUAY);
    /* the line painted where the next container waits */
    quad(HC_PICK_X - 30, 1, -26, HC_PICK_X + 30, 1, -26, HC_PICK_X + 30, 1, 26, HC_PICK_X - 30, 1, 26, -2, PEN_YELLOW);
    quad(HC_PICK_X - 27, 1, -23, HC_PICK_X + 27, 1, -23, HC_PICK_X + 27, 1, 23, HC_PICK_X - 27, 1, 23, -3, PEN_QUAY);
    /* the next one, and those behind it */
    if (g->carrying < 0 && g->falling < 0 && g->next < g->nbox) {
        int w = hc_box_w(g, g->next);
        container(HC_PICK_X - w / 2, 0, w, g->box[g->next].weight, 0);
    }
    for (k = 1; k <= 4 && g->next + k < g->nbox; k++) {
        const HCBox *b = &g->box[g->next + k];
        int w = b->len * HC_SLOT_W - 2;
        int lit = PEN_BOX + 2 * (b->weight - 1);
        int32_t x0 = HC_PICK_X - 60 - k * 70 - w / 2;
        box(x0, x0 + w, 0, HC_BOX_H, 120, 160, lit, lit + 1, 0);
    }
}

static void crane(const HCGame *g)
{
    int32_t tx = g->tx >> 16, hx = hc_hook_x(g) >> 16, hy = hc_hook_y(g) >> 16, top = HC_GANTRY_Y;
    static const int16_t legs[2] = { -300, -40 };
    int k;
    xf_world();
    for (k = 0; k < 2; k++) {
        box(legs[k] - 8, legs[k] + 8, 0, top + 25, -52, -40, PEN_CRANE, PEN_CRANE + 1, 0);
        box(legs[k] - 8, legs[k] + 8, 0, top + 25, 40, 52, PEN_CRANE, PEN_CRANE + 1, 0);
        box(legs[k] - 8, legs[k] + 8, top + 25, top + 37, -52, 52, PEN_CRANE, PEN_CRANE + 1, 0);
    }
    box(-320, 460, top + 10, top + 25, -52, -40, PEN_CRANE, PEN_CRANE + 1, 0);     /* the rails */
    box(-320, 460, top + 10, top + 25, 40, 52, PEN_CRANE, PEN_CRANE + 1, 0);
    box(-300, -40, 160, 172, -52, -40, PEN_CRANE, PEN_CRANE + 1, 0);               /* a brace */
    box(-330, -280, top + 25, top + 70, -30, 30, PEN_CRANE + 1, PEN_CRANE + 1, 0); /* the machinery house */
    box(tx - 18, tx + 18, top, top + 14, -48, 48, PEN_CRANE + 1, PEN_CRANE + 1, -2);   /* the trolley */
    /* the cable, from the trolley to the hook */
    {
        V3 a, b, c, d;
        int32_t dx = 2;
        pt(tx - dx, top, -2, &a); pt(tx + dx, top, -2, &b); pt(hx + dx, hy, -2, &c); pt(hx - dx, hy, -2, &d);
        a3_quad(&cam, ((a.z + c.z) >> 1) - 25, PEN_CABLE, a, b, c, d);
    }
    if (g->carrying < 0) box(hx - 6, hx + 6, hy - 4, hy + 4, -6, 6, PEN_CABLE, PEN_CABLE, -26);
}

static void ship(const HCGame *g)
{
    int k;
    xf_ship(g);
    /* the hull: its side, with a bow, and the deck */
    {
        V3 p[5];
        pt(HC_HULL_X0 - 25, HC_DECK_Y + 6, -60, &p[0]);
        pt(HC_HULL_X1 + 10, HC_DECK_Y + 6, -60, &p[1]);
        pt(HC_HULL_X1 - 5, HC_WATER_Y - 30, -60, &p[2]);
        pt(HC_HULL_X0 + 15, HC_WATER_Y - 30, -60, &p[3]);
        pt(HC_HULL_X0 - 10, HC_WATER_Y + 10, -60, &p[4]);
        a3_poly(&cam, p[0].z + 20, PEN_HULL, p, 5);
    }
    quad(HC_HULL_X0 - 25, HC_DECK_Y + 6, -60, HC_HULL_X1 + 10, HC_DECK_Y + 6, -60, HC_HULL_X1 + 10, HC_DECK_Y, -60,
         HC_HULL_X0 - 25, HC_DECK_Y, -60, 18, PEN_FOAM);
    quad(HC_HULL_X0 - 25, HC_DECK_Y, -60, HC_HULL_X1 + 10, HC_DECK_Y, -60, HC_HULL_X1 + 10, HC_DECK_Y, 60,
         HC_HULL_X0 - 25, HC_DECK_Y, 60, 40, PEN_DECK);
    /* the slots' edges on the deck */
    for (k = 0; k <= HC_SLOTS; k++) {
        int32_t x = HC_SHIP_X0 + k * HC_SLOT_W;
        quad(x - 1, HC_DECK_Y + 1, -30, x + 1, HC_DECK_Y + 1, -30, x + 1, HC_DECK_Y + 1, 30, x - 1, HC_DECK_Y + 1, 30, 38, PEN_YELLOW);
    }
    /* the bridge at the stern */
    box(HC_SHIP_X0 + HC_SLOTS * HC_SLOT_W + 6, HC_HULL_X1 + 2, HC_DECK_Y, HC_DECK_Y + 80, -40, 40, PEN_BRIDGE, PEN_BRIDGE - 12, 0);
    box(HC_SHIP_X0 + HC_SLOTS * HC_SLOT_W + 10, HC_HULL_X1 - 2, HC_DECK_Y + 58, HC_DECK_Y + 70, -41, -40, PEN_WINDOW, PEN_WINDOW, -1);
    box(HC_SHIP_X0 + HC_SLOTS * HC_SLOT_W + 4, HC_HULL_X1 + 6, HC_DECK_Y + 80, HC_DECK_Y + 86, -44, 44, PEN_HULL, PEN_HULL + 1, 0);
    /* the load */
    for (k = 0; k < g->nplaced; k++) {
        const HCPlaced *p = &g->placed[k];
        int b = p->box;
        container(HC_SHIP_X0 + p->slot * HC_SLOT_W + 1, HC_DECK_Y + p->tier * HC_BOX_H, hc_box_w(g, b), g->box[b].weight, -p->tier);
    }
    xf_world();
}

static void load(const HCGame *g)
{
    if (g->carrying >= 0) {
        int32_t hx = hc_hook_x(g) >> 16, hy = hc_hook_y(g) >> 16;
        int w = hc_box_w(g, g->carrying);
        xf_swing(hx, hy, g->th);
        box(hx - w / 2 - 4, hx + w / 2 + 4, hy, hy + 5, -24, 24, PEN_CABLE, PEN_CABLE, -30);      /* the spreader */
        container(hx - w / 2, hy - HC_BOX_H, w, g->box[g->carrying].weight, -30);
        xf_world();
    }
    if (g->falling >= 0)
        container(g->fx >> 16, g->fy >> 16, hc_box_w(g, g->falling), g->box[g->falling].weight, -30);
}

static void scene(Canvas *cv, const HCGame *g)
{
    cam.x = 60 + (int32_t)((g->tx >> 16) - 100) / 6;
    cam.y = 150;
    cam.z = -760;
    cam.yaw = 0;
    cam.pitch = 1100;
    cam_view(&cam, SCREEN_W, SCREEN_H, SCREEN_W / 2, SCREEN_H / 2 + 8, 290);
    cam_setup(&cam);
    xf_world();
    background(cv, g);
    a3_begin(2000);
    quay(g);
    ship(g);
    crane(g);
    load(g);
    if (cr.splash_timer > 0) {
        int32_t x = cr.splash_x, r = 6 + (20 - cr.splash_timer) * 3;
        quad(x - r, HC_WATER_Y + 1, -r / 2, x + r, HC_WATER_Y + 1, -r / 2, x + r, HC_WATER_Y + 1, r / 2, x - r, HC_WATER_Y + 1, r / 2, -40, PEN_FOAM);
    }
    a3_flush(cv);
}

/* the list gauge: the heel against the capsize marks */
static void gauge(Canvas *cv, const HCGame *g, int x, int y)
{
    int w = 100, n = (int)a3_muldiv(g->heel, w / 2, HC_CAPSIZE), l = (int)a3_muldiv(g->list, w / 2, HC_CAPSIZE);   /* 3DO: no int64_t */
    g_box(cv, x, y, x + w, y + 8, 0, 3);
    g_rect(cv, x + 1, y + 1, x + 12, y + 7, PEN_RED);
    g_rect(cv, x + w - 12, y + 1, x + w - 1, y + 7, PEN_RED);
    if (n < -w / 2 + 1) n = -w / 2 + 1;
    if (n > w / 2 - 1) n = w / 2 - 1;
    if (l < -w / 2 + 1) l = -w / 2 + 1;
    if (l > w / 2 - 1) l = w / 2 - 1;
    g_rect(cv, x + w / 2 + l - 1, y + 2, x + w / 2 + l + 1, y + 6, PEN_YELLOW);
    g_rect(cv, x + w / 2 + n, y - 1, x + w / 2 + n, y + 9, 1);
}

static void hud(Canvas *cv, const HCGame *g)
{
    char tmp[48];
    int k, wind = (int)((g->wind + g->gust) / 64);
    g_fill(cv, 0, 0, SCREEN_W - 1, 11, 2);
    g_sync();
    sprintf(tmp, "SCORE %ld", (long)g->score);
    g_text(cv, 4, 9, 1, tmp);
    sprintf(tmp, "LEVEL %ld", (long)g->level);
    g_text(cv, 110, 9, PEN_YELLOW, tmp);
    sprintf(tmp, "%ld/%ld", (long)g->loaded, (long)g->nbox);
    g_text(cv, 186, 9, PEN_GREEN, tmp);
    sprintf(tmp, "%ld:%02ld", (long)(g->time_left / 1500), (long)((g->time_left / 25) % 60));
    g_text(cv, 276, 9, g->time_left < 750 && (g->tick & 8) ? PEN_RED : 1, tmp);
    for (k = 0; k < g->lost; k++) g_text(cv, 240 + k * 9, 9, PEN_RED, "X");
    g_text_shadow(cv, 4, 250, 3, "LIST");
    gauge(cv, g, 40, 243);
    if (wind) {
        int x0 = 220, len = wind > 0 ? wind : -wind;
        if (len > 40) len = 40;
        g_text_shadow(cv, 160, 250, 3, "WIND");
        g_rect(cv, x0, 246, x0 + len, 247, PEN_CYAN);
        g_rect(cv, wind > 0 ? x0 + len - 2 : x0, 244, wind > 0 ? x0 + len : x0 + 2, 249, PEN_CYAN);
    }
    if (cr.autoplay) g_text_shadow(cv, 276, 250, PEN_GREEN, "AUTO");
}

void render_game(Canvas *cv, const HCGame *g)
{
    char tmp[48];
    scene(cv, g);
    hud(cv, g);
    if (g->between) {
        g_text_big(cv, 56, 60, PEN_GREEN, "SHIP LOADED", 2);
        sprintf(tmp, "LEVEL %ld NEXT", (long)g->level + 1);
        if (g->level < HC_LEVELS) g_text_centre(cv, 96, 1, tmp);
    }
    if (cr.state == CS_PLAY && g->tick < 75 && !g->between) {
        sprintf(tmp, "LEVEL %ld: LOAD %ld CONTAINERS", (long)g->level, (long)g->nbox);
        g_text_centre(cv, 60, PEN_YELLOW, tmp);
    }
    if (cr.state == CS_OVER) {
        const char *why = g->won ? "ALL SHIPS LOADED" : g->capsized ? "SHE CAPSIZED" : g->lost >= HC_MAX_LOST ? "THREE LOST" : "TIME UP";
        g_box(cv, 50, 70, 270, 150, 2, g->won ? PEN_GREEN : PEN_RED);
        g_text_big(cv, g->won ? 92 : 76, 78, g->won ? PEN_GREEN : PEN_RED, g->won ? "YOU WIN" : "GAME OVER", 2);
        g_text_centre(cv, 112, PEN_YELLOW, why);
        sprintf(tmp, "SCORE %ld", (long)g->score);
        g_text_centre(cv, 126, 1, tmp);
#ifdef AMIGA3DO
        if (cr.state_timer == 0) g_text_centre(cv, 142, PEN_YELLOW, "PRESS A");
#else
        if (cr.state_timer == 0) g_text_centre(cv, 142, PEN_YELLOW, "PRESS FIRE");
#endif
    }
    if (cr.paused == 1) g_text_centre(cv, 128, 1, "PAUSED");
    if (cr.flash_timer > 0 && cr.flash[0]) g_text_centre(cv, 30, cr.flash_pen, cr.flash);
}

void render_title(Canvas *cv, const HCGame *g)
{
    char tmp[48];
    scene(cv, g);                        /* the autopilot works behind the title */
    g_text_big(cv, 34, 22, 0, "HARBOUR", 3);
    g_text_big(cv, 32, 20, PEN_YELLOW, "HARBOUR", 3);
    g_text_big(cv, 82, 52, 0, "CRANE", 3);
    g_text_big(cv, 80, 50, PEN_CRANE, "CRANE", 3);
    g_box(cv, 40, 196, 280, 252, 2, 3);
    sprintf(tmp, "START AT LEVEL %ld (UP/DOWN)", (long)cr.start_level);
    g_text_centre(cv, 210, PEN_GREEN, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS A TO PLAY");
#else
    g_text_centre(cv, 226, (g->tick / 12) & 1 ? PEN_YELLOW : 1, "PRESS FIRE TO PLAY");
#endif
#ifdef AMIGA3DO
    sprintf(tmp, "BEST %ld   C: HOW TO PLAY", (long)cr.best);
#else
    sprintf(tmp, "BEST %ld   HELP: HOW TO PLAY", (long)cr.best);
#endif
    g_text_centre(cv, 242, 3, tmp);
}

static const char *const help_lines[2][12] = {
    { "LIFT EACH CONTAINER OFF THE QUAY AND",
      "LAND IT ON THE SHIP. WHAT HANGS ON THE",
      "HOOK SWINGS: START AND STOP SMOOTHLY,",
      "AND LET THE SWING DIE BEFORE YOU LAND.",
      "LET GO LOW: DROPPED FROM HIGH IT DENTS.",
      "LAND SQUARE IN A SLOT; LONG ONES NEED",
      "TWO STACKS THE SAME HEIGHT. FOUR HIGH.",
      "",
      "KEEP HER LEVEL: HEAVY BOXES ON ONE SIDE",
      "LIST THE SHIP, HIGH STACKS ROLL HER",
      "MORE. TOO FAR AND SHE CAPSIZES.",
      "GREEN 1T  BLUE 2T  ORANGE 3T  RED 4T" },
    { "LEFT / RIGHT     MOVE THE TROLLEY",
      "UP / DOWN        WINCH IN / OUT",
#ifdef AMIGA3DO
      "A                GRAB / LET GO",
#else
      "SPACE / FIRE     GRAB / LET GO",
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
      "50 x WEIGHT A CONTAINER, -40 DENTED,",
      "-100 LOST (3 LOST ENDS THE GAME).",
      "BONUS FOR TIME LEFT, AND 200 FOR",
      "SENDING HER OFF ON AN EVEN KEEL." },
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
