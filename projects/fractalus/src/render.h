// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef FRACTALUS_RENDER_H
#define FRACTALUS_RENDER_H

#include <exec/types.h>
#include <intuition/screens.h>
#include <graphics/rastport.h>
#include "fixed.h"

class Terrain;
struct ShipState;
struct GameState;

/* Screen geometry — LORES AGA 320x256, 8bpp (256-colour). */
#define R_SCREEN_W    320
#define R_SCREEN_H    256

/* Flying viewport (inside the cockpit frame). */
#define R_VIEW_X      16
#define R_VIEW_Y      16
#define R_VIEW_W      288
#define R_VIEW_H      160
#define R_VIEW_X2     (R_VIEW_X + R_VIEW_W - 1)
#define R_VIEW_Y2     (R_VIEW_Y + R_VIEW_H - 1)
#define R_HORIZON_Y   (R_VIEW_Y + (R_VIEW_H / 2))

/* Render params — matched to terrain_test's proven config. Sprite
 * projection must use the same values as the terrain raycaster or
 * saucers won't sit correctly against the terrain. */
#define R_PROJ           220
#define R_FOV_SPAN       900        /* out of 4096 = ~79° horizontal */
#define R_NEAR_DIST      6
#define R_FAR_DIST       6000
#define R_COL_STEP       8         /* must match direct-plane fill in render.cpp */

/* Palette layout — 256 pens total. */
#define PAL_SKY_BASE       0
#define PAL_SKY_COUNT      32     /* pens 0..31 : deep→orange gradient */
#define PAL_TERRAIN_BASE   32
#define PAL_TERRAIN_COUNT  64     /* pens 32..95 : 8 heights × 8 fog bins */
#define PAL_COCKPIT_BASE   96
#define PAL_COCKPIT_COUNT  16
#define PAL_HUD_BASE       112
#define PAL_HUD_COUNT      16
#define PAL_MISC_BASE      128    /* reserved for pilots/enemies/fx */

/* Shared sin/cos table (angle in ANGLE_FULL units). */
extern LONG sin_table[ANGLE_FULL];
LONG isin(LONG a);
LONG icos(LONG a);

class PilotList;
class Combat;

class Renderer {
public:
    /* Set up screen + double buffer + palette. Returns 0 on success. */
    int open_display();
    void close_display();

    /* Draw one frame given the current game/ship state. */
    void render(const GameState &gs, const Terrain &world,
                const PilotList &pilots, const Combat &combat);

    /* IDCMP UserPort for input. */
    struct MsgPort *user_port() const;

private:
    struct Screen       *screen;
    struct Window       *window;
    struct ScreenBuffer *sbuf[2];
    struct RastPort      rp_buf[2];
    struct MsgPort      *safe_port;
    UWORD                cur_buf;
    UWORD                first_flip;

    /* Reusable per-frame scratch. */
    UBYTE                col_top[R_VIEW_W];   /* smallest y drawn in each col */

    void install_palette();
    void draw_sky(struct RastPort *rp);
    void draw_terrain(struct RastPort *rp, const GameState &gs,
                      const Terrain &world);
    void draw_sprites(struct RastPort *rp, const GameState &gs,
                      const Combat &combat, const PilotList &pilots);
    void draw_cockpit(struct RastPort *rp, const GameState &gs);
    void draw_hud(struct RastPort *rp, const GameState &gs,
                  const PilotList &pilots);
    void draw_overlay(struct RastPort *rp, const GameState &gs);

    void flip();
};

/* Build the shared sin/cos table (called once from main). */
void render_init_math();

#endif
