// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - Lander 3D's picture on the shared engine: the terrain around
 * the craft (a window of the height grid, each quad shaded by its slope),
 * the pads, the lander tilted by its pitch and roll with its exhaust and
 * its shadow, the sky (stars, or Mars's dusty gradient), and the HUD:
 * altitude, the descent and sideways speeds and the tilt coloured against
 * the world's limits, fuel, the compass to the nearest pad and the wind.
 *
 * World units for the engine: 16 to the metre (the simulation is fixed
 * point metres; >> 12 turns one into the other).
 */
#include <string.h>
#include <stdio.h>
#include "a3d.h"
#include "a3d_draw.h"
#include "a3d_plat.h"
#include "ld.h"
#include "lander.h"

#define U(f)       ((int32_t)((f) >> 12))      /* fix metres -> engine units */
#define VIEW_H     216
/* terrain cells each side of the centre (bridge: ground_win): AmigaOS 4
 * draws nearly to the horizon */
int32_t ground_win = A3_SUB > 1 ? 20 : 9;
#define WIN        ground_win

/* 0 black, 1 white, 2 panel, 3 grey, 4 sky, 5-10 ground (light to dark),
 * 11-13 pad rings x1 x2 x5, 14 pad, 15 shadow, 16-17 gold foil, 18-19 the
 * ascent stage, 20 legs, 21-22 flame, 23 red, 24 amber, 25 green, 26 cyan,
 * 27 stars, 28 HUD grey, 29 dust, 30 haze, 31 near black */
static const uint16_t palettes[W_COUNT][32] = {
    { 0x000, 0xFFF, 0x223, 0x99A, 0x000, 0xCCC, 0xAAA, 0x999, 0x777, 0x555, 0x333,
      0x3E5, 0xFE3, 0xE3E, 0x445, 0x111, 0xEB3, 0x973, 0xCCD, 0x889, 0x444, 0xFF8, 0xF82,
      0xF33, 0xFB2, 0x4E4, 0x3DF, 0x888, 0x99A, 0xBBB, 0x334, 0x111 },
    { 0x000, 0xFFF, 0x322, 0xA99, 0xC85, 0xD96, 0xC75, 0xA64, 0x853, 0x642, 0x431,
      0x3E5, 0xFE3, 0xE3E, 0x544, 0x211, 0xEB3, 0x973, 0xCCD, 0x889, 0x444, 0xFF8, 0xF82,
      0xF33, 0xFB2, 0x4E4, 0x3DF, 0xA88, 0xA99, 0xC97, 0xB86, 0x111 },
    { 0x000, 0xFFF, 0x223, 0x99A, 0x000, 0xA98, 0x987, 0x876, 0x765, 0x543, 0x332,
      0x3E5, 0xFE3, 0xE3E, 0x444, 0x111, 0xEB3, 0x973, 0xCCD, 0x889, 0x444, 0xFF8, 0xF82,
      0xF33, 0xFB2, 0x4E4, 0x3DF, 0x888, 0x99A, 0xA98, 0x333, 0x111 },
};
#define PEN_SKY    4
#define PEN_GROUND 5
#define PEN_RING   11
#define PEN_PAD    14
#define PEN_SHADOW 15
#define PEN_GOLD   16
#define PEN_STAGE  18
#define PEN_LEGS   20
#define PEN_FLAME  21
#define PEN_RED    23
#define PEN_AMBER  24
#define PEN_GREEN  25
#define PEN_CYAN   26
#define PEN_STAR   27
#define PEN_HUD    28
#define PEN_DUST   29

static Cam cam;
static Model craft;
static int32_t cam_back = 480, cam_up = 160;

void render_palette(int world)
{
    plat_palette(palettes[world]);
    if (world == W_MARS) plat_sky(PEN_SKY, 0x632, 0xC96, 110);
    else plat_sky(PEN_SKY, 0, 0, 0);
}

void render_init_models(void)
{
    Model *m = &craft;
    mdl_begin(m);
    /* the descent stage in gold foil, the ascent stage on top, four legs and
     * feet, the engine bell underneath (units: 16 to the metre) */
    mdl_box(m, PEN_GOLD, PEN_GOLD + 1, -24, 24, -14, 8, -24, 24);
    mdl_box(m, PEN_STAGE, PEN_STAGE + 1, -16, 16, 8, 30, -14, 18);
    mdl_box(m, PEN_STAGE + 1, PEN_STAGE + 1, -4, 4, 30, 36, -4, 4);
    mdl_box(m, PEN_LEGS, PEN_LEGS, 30, 34, -32, -6, 30, 34);
    mdl_box(m, PEN_LEGS, PEN_LEGS, -34, -30, -32, -6, 30, 34);
    mdl_box(m, PEN_LEGS, PEN_LEGS, 30, 34, -32, -6, -34, -30);
    mdl_box(m, PEN_LEGS, PEN_LEGS, -34, -30, -32, -6, -34, -30);
    mdl_pyramid(m, PEN_LEGS, PEN_LEGS, -12, 12, -14, -12, 12, -26);
}

/* ---------- the ground ---------- */
#if A3_SUB > 1
static int mirror(int i, int n)
{
    int p = 2 * (n - 1);
    i %= p;
    if (i < 0) i += p;
    return i > n - 1 ? p - i : i;
}
#endif

static void ground(const Level *L)
{
    const Terrain *T = &L->T;
    int n = T->n, ci, cj, i, j;
    int32_t cell = U(T->cell);
    /* centred a little ahead of the craft (the camera looks along +z) */
    ci = (int)FINT(FDIV(L->c.p[0] + T->half, T->cell));
    cj = (int)FINT(FDIV(L->c.p[2] + T->half, T->cell)) + WIN / 2;
    for (j = cj + WIN; j >= cj - WIN; j--)
        for (i = ci - WIN; i <= ci + WIN; i++) {
            V3 a, b, c, d;
            int32_t ha, hb, hc, hd, x0, z0, slope;
            int pen;
#if A3_SUB > 1
            {
                /* AmigaOS 4: beyond the map, its mirror image (no seam), so the
                 * ground goes on to the horizon */
                int i0 = mirror(i, n), i1 = mirror(i + 1, n), j0 = mirror(j, n) * n, j1 = mirror(j + 1, n) * n;
                ha = U(T->h[j0 + i0]); hb = U(T->h[j0 + i1]);
                hc = U(T->h[j1 + i1]); hd = U(T->h[j1 + i0]);
            }
#else
            if (i < 0 || j < 0 || i >= n - 1 || j >= n - 1) continue;
            ha = U(T->h[j * n + i]); hb = U(T->h[j * n + i + 1]);
            hc = U(T->h[(j + 1) * n + i + 1]); hd = U(T->h[(j + 1) * n + i]);
#endif
            x0 = U(-T->half) + i * cell;
            z0 = U(-T->half) + j * cell;
            cam_xform(&cam, x0, ha, z0, &a);
            cam_xform(&cam, x0 + cell, hb, z0, &b);
            cam_xform(&cam, x0 + cell, hc, z0 + cell, &c);
            cam_xform(&cam, x0, hd, z0 + cell, &d);
            /* lit from the left and behind: brighter facing -x and -z */
            slope = ((ha + hd) - (hb + hc)) + ((ha + hb) - (hc + hd)) / 2;
            pen = 2 - slope / (cell / 3 + 1) + ((i + j) & 1 ? 0 : 0);
            if (pen < 0) pen = 0;
            if (pen > 5) pen = 5;
            a3_quad(&cam, (a.z + b.z + c.z + d.z) >> 2, PEN_GROUND + pen, a, d, c, b);
        }
}

/* returns the depth it was sorted at; at_depth >= 0 sorts it there instead */
static int32_t octagon(const Level *L, int32_t cx, int32_t cz, int32_t r, int32_t lift, int col, int32_t bias, int32_t at_depth)
{
    V3 p[8];
    int k;
    int32_t y = U(ld_height(&L->T, (fix)cx << 12, (fix)cz << 12)) + lift, depth = 0x7FFFFFFF;
    for (k = 0; k < 8; k++) {
        int a = k * 8192 + 4096;
        cam_xform(&cam, cx + ((a3_sin(a) * r) >> 16), y, cz + ((a3_cos(a) * r) >> 16), &p[k]);
        if (p[k].z < depth) depth = p[k].z;
    }
    if (p[0].z < NEAR_Z && p[4].z < NEAR_Z) return -1;
    /* sorted at its nearest point, half a cell nearer still: lying flat on
     * the ground, it would otherwise lose its near half to the cells under
     * it (each sorted by its own centre) */
    depth = at_depth >= 0 ? at_depth : depth - U(L->T.cell) / 2 - bias;
    if (depth < 0) depth = 0;
    a3_poly(&cam, depth, col, p, 8);
    return depth;
}

static int32_t pad_depth[3];             /* where each pad sorted this frame (-1: not drawn) */

static void pads(const Level *L)
{
    int k;
    for (k = 0; k < 3; k++) {
        const Pad *p = &L->T.pads[k];
        int32_t d = octagon(L, U(p->x), U(p->z), U(p->r), 2, PEN_RING + k, 4, -1);
        pad_depth[k] = d;
        if (d >= 0) octagon(L, U(p->x), U(p->z), U(p->r) * 3 / 4, 3, PEN_PAD, 0, d > 0 ? d - 1 : 0);   /* just in front of its ring */
    }
}

/* ---------- the craft ---------- */

static void craft_draw(const Level *L)
{
    const Craft *c = &L->c;
    int32_t m[9], px = U(c->p[0]), py = U(c->p[1]), pz = U(c->p[2]);
    fix u[3];
    int k, over = -1;
    int32_t bias = 30;
    /* over a pad? The pad sorts at its nearest point, so the craft and its
     * shadow have to come in front of that */
    for (k = 0; k < 3; k++) {
        const Pad *p = &L->T.pads[k];
        int32_t dx = px - U(p->x), dz = pz - U(p->z), reach = U(p->r) + 48;
        if (pad_depth[k] >= 0 && dx > -reach && dx < reach && dz > -reach && dz < reach) over = k;
    }
    if (over >= 0) {
        V3 v;
        cam_xform(&cam, px, py, pz, &v);
        if (v.z - (pad_depth[over] - 4) > bias) bias = v.z - (pad_depth[over] - 4);
    }
    /* the shadow, straight down */
    octagon(L, px, pz, 40, 3, PEN_SHADOW, 8, over >= 0 && pad_depth[over] > 2 ? pad_depth[over] - 2 : -1);
    a3_rot_ypr(m, 0, a3_angle(c->pitch), a3_angle(c->roll));
    a3_model(&cam, &craft, px, py, pz, m, NULL, 0, bias);
    /* the exhaust: a flickering cone along -up, as long as the throttle */
    if (c->throttle > 0 && !L->outcome) {
        V3 q[3];
        int32_t len = 30 + ((c->throttle * 70) >> 16) + (int32_t)((lander.frame * 7) % 9);
        int32_t bx = px, by = py - 24, bz = pz;
        ld_up(c, u);
        cam_xform(&cam, bx - 8, by, bz, &q[0]);
        cam_xform(&cam, bx + 8, by, bz, &q[1]);
        cam_xform(&cam, bx - ((u[0] * len) >> 16), by - ((u[1] * len) >> 16), bz - ((u[2] * len) >> 16), &q[2]);
        a3_poly(&cam, q[2].z - 40, (lander.frame & 1) ? PEN_FLAME : PEN_FLAME + 1, q, 3);
    }
}

/* ---------- the sky ----------
 * AmigaOS 4 has a painted panorama for each world (`make art`), its bottom
 * edge on the horizon, drifting as the lander flies across; the classic
 * Amiga has stars, or Mars's dusty gradient. */
static const char *const sky_file[W_COUNT] = {
    "PROGDIR:sky_moon.img", "PROGDIR:sky_mars.img", "PROGDIR:sky_asteroid.img",
};
static const int sky_sink[W_COUNT] = { 48, 0, 16 };   /* panorama rows hidden below the horizon */
static const uint32_t plain_far[W_COUNT] = { 0x3A3A3E, 0xC08458, 0x3C3630 };    /* at the horizon */
static const uint32_t plain_near[W_COUNT] = { 0x8A8A8A, 0x7A4628, 0x6E6252 };
static A3Image *sky_img[W_COUNT];
static int sky_tried[W_COUNT];

static void sky(Canvas *cv, const Level *L)
{
    int k, hz = cam_horizon(&cam);
    A3Image *im;
    if (hz < 0) hz = 0;
    if (hz > VIEW_H) hz = VIEW_H;
    if (!sky_tried[L->world]) { sky_img[L->world] = g_image_load(sky_file[L->world]); sky_tried[L->world] = 1; }
    im = sky_img[L->world];
    if (im && hz > 0) g_image_wrap(cv, im, cam.x / 6, hz * A3_SUB + sky_sink[L->world] - g_image_h(im), 0, hz * A3_SUB - 1);
    else g_fill(cv, 0, 0, SCREEN_W - 1, hz - 1, L->world == W_MARS ? PEN_SKY : 0);
    /* beyond the terrain drawn: a plain to the horizon (AmigaOS 4: fading
     * into the distance) */
    if (im) g_fill_grad(cv, hz, VIEW_H - 1, plain_far[L->world], plain_near[L->world]);
    else g_fill(cv, 0, hz, SCREEN_W - 1, VIEW_H - 1, PEN_GROUND + 4);
    g_sync();
    if (L->world != W_MARS && !im)
        for (k = 0; k < 70; k++) {
            uint32_t h = a3_hash((uint32_t)k * 7919u);
            int x = (int)(h % SCREEN_W), y = (int)((h >> 12) % 120);
            if (y < hz) g_span(cv, y, x, x, (k & 3) ? PEN_STAR : 1);
        }
}

static void camera(const Level *L)
{
    fix agl = ld_agl(L);
    /* closer in as the ground comes up */
    int32_t back = agl > FROMI(40) ? 480 : 224 + (int32_t)FINT(agl) * 6;
    int32_t up = agl > FROMI(40) ? 160 : 72 + (int32_t)FINT(agl) * 2;
    if (L->outcome) { back = 300; up = 110; }
    cam_back += (back - cam_back) / 8;
    cam_up += (up - cam_up) / 8;
    cam.x = U(L->c.p[0]);
    cam.y = U(L->c.p[1]) + cam_up;
    cam.z = U(L->c.p[2]) - cam_back;
    {
        /* don't go into a hill */
        int32_t g = U(ld_height(&L->T, (fix)cam.x << 12, (fix)cam.z << 12)) + 40;
        if (cam.y < g) cam.y = g;
    }
    cam.yaw = 0;
    cam.pitch = a3_atan2(cam.y - U(L->c.p[1]) + 40, cam_back);     /* looking down at the craft */
    if (cam.pitch < 600) cam.pitch = 600;
    cam_view(&cam, SCREEN_W, VIEW_H, SCREEN_W / 2, 116, 200);    /* the craft a little low: more sky */
    cam_setup(&cam);
}

/* ---------- the HUD ---------- */
static int limit_pen(fix v, fix lim)
{
    if (v <= FMUL(lim, FX(0.7))) return PEN_GREEN;
    if (v <= lim) return PEN_AMBER;
    return PEN_RED;
}

static void compass(Canvas *cv, const Level *L)
{
    int k = ld_nearest_pad(L, 0), a, cx = SCREEN_W / 2, cy = 18;
    const Pad *p = &L->T.pads[k];
    int16_t xs[3], ys[3];
    char tmp[16];
    fix d = fhypot(p->x - L->c.p[0], p->z - L->c.p[2]);
    a = a3_atan2((p->x - L->c.p[0]) >> 4, (p->z - L->c.p[2]) >> 4);
    xs[0] = (int16_t)(cx + ((a3_sin(a) * 12) >> 16)); ys[0] = (int16_t)(cy - ((a3_cos(a) * 12) >> 16));
    xs[1] = (int16_t)(cx + ((a3_sin(a + 26000) * 7) >> 16)); ys[1] = (int16_t)(cy - ((a3_cos(a + 26000) * 7) >> 16));
    xs[2] = (int16_t)(cx + ((a3_sin(a - 26000) * 7) >> 16)); ys[2] = (int16_t)(cy - ((a3_cos(a - 26000) * 7) >> 16));
    g_poly(cv, xs, ys, 3, PEN_RING + k, 0, VIEW_H - 1);
    sprintf(tmp, "%ldM", (long)FINT(d));
    g_text_shadow(cv, cx + 16, cy + 4, PEN_RING + k, tmp);
    if (L->params.wind) {
        int wa = a3_atan2(L->wind[0] >> 4, L->wind[1] >> 4), wx = 290, wy = 18;
        g_hline(cv, wx - ((a3_sin(wa) * 10) >> 16), wx + ((a3_sin(wa) * 10) >> 16), wy, PEN_HUD);
        g_vline(cv, wx + ((a3_sin(wa) * 10) >> 16), wy - ((a3_cos(wa) * 10) >> 16), wy, PEN_HUD);
        sprintf(tmp, "%ld", (long)FINT(fhypot(L->wind[0], L->wind[1])));
        g_text_shadow(cv, wx - 12, wy + 14, PEN_HUD, "WIND");
    }
}

static void hud(Canvas *cv, const Level *L)
{
    const LdWorld *W = &ld_worlds[L->world];
    char tmp[40];
    fix vs = -L->c.v[1], hs = fhypot(L->c.v[0], L->c.v[2]), tilt = ld_tilt(&L->c), agl = ld_agl(L);
    int fuel = (int)(FDIV(L->fuel, L->fuel_max ? L->fuel_max : 1) * 100 >> 16);
    g_fill(cv, 0, VIEW_H, SCREEN_W - 1, SCREEN_H - 1, 2);
    g_sync();
    sprintf(tmp, "ALT %4ld", (long)FINT(agl < 0 ? 0 : agl));
    g_text(cv, 4, VIEW_H + 12, 1, tmp);
    sprintf(tmp, "V/S %ld.%ld", (long)FINT(vs), (long)((vs < 0 ? -vs : vs) * 10 >> 16) % 10);
    g_text(cv, 4, VIEW_H + 24, vs > 0 ? limit_pen(vs, W->lim_vs) : PEN_GREEN, tmp);
    sprintf(tmp, "H/S %ld.%ld", (long)FINT(hs), (long)(hs * 10 >> 16) % 10);
    g_text(cv, 4, VIEW_H + 36, limit_pen(hs, W->lim_hs), tmp);
    sprintf(tmp, "TILT %2ld", (long)FINT(FMUL(tilt, FX(57.29578))));
    g_text(cv, 100, VIEW_H + 12, limit_pen(tilt, W->lim_tilt), tmp);
    sprintf(tmp, "THR %3ld%%", (long)(L->c.throttle * 100 >> 16));
    g_text(cv, 100, VIEW_H + 24, PEN_HUD, tmp);
    g_text(cv, 100, VIEW_H + 36, PEN_HUD, "FUEL");
    g_rect(cv, 140, VIEW_H + 30, 190, VIEW_H + 35, 0);
    if (fuel > 0) g_rect(cv, 140, VIEW_H + 30, 140 + fuel / 2, VIEW_H + 35, fuel < 20 ? PEN_RED : PEN_AMBER);
    sprintf(tmp, "SCORE %ld", (long)lander.score);
    g_text(cv, 200, VIEW_H + 12, 1, tmp);
    sprintf(tmp, "%s %ld", W->name, (long)L->level);
    g_text(cv, 200, VIEW_H + 24, PEN_CYAN, tmp);
    sprintf(tmp, "SHIPS %ld", (long)lander.ships);
    g_text(cv, 200, VIEW_H + 36, PEN_HUD, tmp);
}

static void scene(Canvas *cv, const Level *L)
{
    camera(L);
    sky(cv, L);
    a3_begin(8000);
    ground(L);
    pads(L);
    craft_draw(L);
    a3_flush(cv);
    compass(cv, L);
}

void render_game(Canvas *cv, const Level *L)
{
    scene(cv, L);
    hud(cv, L);
    if (lander.state == LS_RESULT || lander.state == LS_OVER) {
        int col = L->outcome == OUT_LANDED ? PEN_GREEN : L->outcome == OUT_OFFPAD ? PEN_AMBER : PEN_RED;
        g_box(cv, 24, 60, 296, 140, 2, col);
        g_text_big(cv, (SCREEN_W - (int)strlen(lander.msg) * 16) / 2, 70, col, lander.msg, 2);
        g_text_centre(cv, 104, 1, lander.msg2);
        if (lander.state == LS_OVER) g_text_centre(cv, 124, PEN_RED, "GAME OVER");
        else if (L->outcome == OUT_LANDED || L->outcome == OUT_OFFPAD) {
            char tmp[32];
            sprintf(tmp, "+%ld", (long)lander.last_score);
            g_text_centre(cv, 124, PEN_AMBER, tmp);
        }
    }
    if (lander.paused == 1) g_text_centre(cv, 100, 1, "PAUSED");
}

/* The painted title (AmigaOS 4, `make art`) in turn with the autopilot's demo */
static A3Image *title_img;
static int title_tried, title_frame0;
#define TITLE_ART_FRAMES  250           /* 10 s */
#define TITLE_DEMO_FRAMES 375

void render_title_start(void) { title_frame0 = lander.frame; }

void render_title(Canvas *cv, const Level *L)
{
    static const char *const names[4] = { "MOON", "MARS", "ASTEROID", "TOUR" };
    char tmp[40];
    if (!title_tried) { title_img = g_image_load("PROGDIR:lander_title.img"); title_tried = 1; }
    if (title_img && (lander.frame - title_frame0) % (TITLE_ART_FRAMES + TITLE_DEMO_FRAMES) < TITLE_ART_FRAMES) {
        g_image_draw(cv, title_img, 0, 0);
        sprintf(tmp, "<  %s  >", names[lander.menu]);
        g_text_centre(cv, 214, PEN_AMBER, tmp);
#ifdef AMIGA3DO
        g_text_centre(cv, 230, (lander.frame & 16) ? PEN_GREEN : 1, "PRESS A TO START");
#else
        g_text_centre(cv, 230, (lander.frame & 16) ? PEN_GREEN : 1, "PRESS FIRE TO START");
#endif
#ifdef AMIGA3DO
        sprintf(tmp, "BEST %ld    R: HOW TO FLY", (long)lander.best);
#else
        sprintf(tmp, "BEST %ld    HELP: HOW TO FLY", (long)lander.best);
#endif
        g_text_centre(cv, 246, PEN_HUD, tmp);
        return;
    }
    scene(cv, L);
    g_text_big(cv, 42, 30, 0, "LANDER 3D", 3);
    g_text_big(cv, 40, 28, 1, "LANDER 3D", 3);
    sprintf(tmp, "<  %s  >", names[lander.menu]);
    g_text_centre(cv, 150, PEN_AMBER, tmp);
#ifdef AMIGA3DO
    g_text_centre(cv, 170, (lander.frame & 16) ? PEN_GREEN : 1, "PRESS A TO START");
#else
    g_text_centre(cv, 170, (lander.frame & 16) ? PEN_GREEN : 1, "PRESS FIRE TO START");
#endif
#ifdef AMIGA3DO
    g_text_centre(cv, 184, PEN_HUD, "R: HOW TO FLY");
#else
    g_text_centre(cv, 184, PEN_HUD, "HELP: HOW TO FLY");
#endif
    g_fill(cv, 0, VIEW_H, SCREEN_W - 1, SCREEN_H - 1, 2);
    g_sync();
    sprintf(tmp, "BEST %ld", (long)lander.best);
    g_text(cv, 8, VIEW_H + 16, PEN_AMBER, tmp);
    g_text(cv, 8, VIEW_H + 32, PEN_HUD, "THE AUTOPILOT IS FLYING THE DEMO");
}

static const char *const help_lines[2][12] = {
    { "THE MAIN ENGINE PUSHES ALONG THE",
      "LANDER'S OWN UP: TIP IT TO MOVE",
      "SIDEWAYS, LEVEL IT TO STOP, AND KEEP",
      "ENOUGH THRUST ON TO STOP GRAVITY.",
      "TOUCH DOWN WITH EVERY LEG ON A PAD:",
      "SLOW (GREEN V/S), NOT SLIDING (H/S)",
      "AND NEARLY LEVEL (TILT).",
      "",
      "GREEN PAD x1   YELLOW x2   MAGENTA x5",
      "SCORE (100 + SOFTNESS + FUEL) x PAD",
      "SAFE OFF A PAD: 25. ELSE: A SHIP LOST.",
      "MARS HAS WIND. THE ASTEROID: DELICATE." },
#ifdef AMIGA3DO
    { "A              FULL THRUST",
#else
    { "SPACE / FIRE   FULL THRUST",
#endif
#ifdef AMIGA3DO
      "C / B          STEADY THROTTLE - / +",
#else
      "Q / E          STEADY THROTTLE - / +",
#endif
#ifdef AMIGA3DO
      "UP             TIP FORWARD",
#else
      "UP / W         TIP FORWARD",
#endif
#ifdef AMIGA3DO
      "DOWN           TIP BACK",
#else
      "DOWN / S       TIP BACK",
#endif
#ifdef AMIGA3DO
      "LEFT           TIP LEFT",
#else
      "LEFT / A       TIP LEFT",
#endif
#ifdef AMIGA3DO
      "RIGHT          TIP RIGHT",
#else
      "RIGHT / D      TIP RIGHT",
#endif
      "P              PAUSE",
#ifdef AMIGA3DO
      "R              THESE PAGES",
#else
      "HELP / H       THESE PAGES",
#endif
#ifdef AMIGA3DO
      "L / X          TITLE / MENU",
#else
      "ESC            TITLE / QUIT",
#endif
      "",
      "THE ARROW AT THE TOP POINTS TO THE",
      "NEAREST PAD. THE SHADOW SHOWS HEIGHT." },
};

void render_help(Canvas *cv, int page)
{
    int k;
    g_fill(cv, 0, 0, SCREEN_W - 1, SCREEN_H - 1, 0);
    g_sync();
    g_text_big(cv, page == 1 ? 80 : 96, 14, PEN_AMBER, page == 1 ? "HOW TO FLY" : "CONTROLS", 2);
    for (k = 0; k < 12; k++) g_text(cv, 8, 56 + k * 14, 1, help_lines[page - 1][k]);
    g_text(cv, 8, 244, PEN_HUD, page == 1 ? "PAGE 1 OF 2" : "PAGE 2 OF 2");
#ifdef AMIGA3DO
    g_text(cv, 156, 244, PEN_GREEN, "R: NEXT  A: FLY");
#else
    g_text(cv, 156, 244, PEN_GREEN, "HELP: NEXT  FIRE: FLY");
#endif
}
