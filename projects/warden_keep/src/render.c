/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

/*
 * warden_keep renderer - isometric painter's-algorithm scene draw.
 *
 * 3DO port of render.cpp: the drawing code below is the original's
 * (RectFill scanlines + Draw lines through the sdk/amiga layer),
 * converted to C. The screen set-up is the layer's, and since the
 * layer's pen buffer persists between frames, the room is only redrawn
 * when something in it changed (the game is turn-based: nothing moves
 * between steps), which leaves the ARM60 idle most of the time.
 *
 * Palette follows the Fairlight "monochrome yellow keep" aesthetic:
 *   0 black background
 *   1 dark yellow      (floor / left face shading)
 *   2 mid yellow       (top face)
 *   3 bright yellow    (right face highlight, jewels, outlines)
 *   4 dim floor stroke
 *   5 door
 *   6 white (HUD text)
 *   7 red   (player accent)
 */

#include "render.h"
#include "game.h"
#include "amiga3do.h"

#include <graphics/rastport.h>
#include <proto/graphics.h>

#include <stdio.h>
#include <string.h>

static struct RastPort *rp;

static const UBYTE PALETTE[8][3] = {
    /* 0 */ {   0,   0,   0 },
    /* 1 */ { 128, 112,  32 },
    /* 2 */ { 208, 176,  40 },
    /* 3 */ { 248, 232,  80 },
    /* 4 */ {  56,  48,  16 },
    /* 5 */ {  64,  56, 168 },
    /* 6 */ { 240, 240, 240 },
    /* 7 */ { 240,  72,  56 },
};

/* The scene the pen buffer holds (render_frame redraws when it differs). */
typedef struct SceneKey {
    LONG   valid;
    LONG   score, jewels_left, num_objects;
    Player player;
    Prop   objects[MAX_OBJECTS];
    UBYTE  floor[WORLD_H][WORLD_W];
} SceneKey;
static SceneKey shown, want;

LONG render_open(void)
{
    LONG i;
    if (!gfx_init(NULL, 0)) return 1;
    for (i = 0; i < 8; i++)
        gfx_set_rgb24((int)i, ((ULONG)PALETTE[i][0] << 16) |
                              ((ULONG)PALETTE[i][1] << 8) | PALETTE[i][2]);
    rp = gfx_back();
    /* Port fix: text in JAM1. The Amiga RastPort default is JAM2 with
     * background pen 0, so the title / end-screen captions (pen 0 on a
     * coloured bar) come out as solid black boxes there. */
    SetDrMd(rp, JAM1);
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    shown.valid = 0;
    return 0;
}

void render_close(void)
{
    gfx_exit();
}

void render_flip(void)
{
    gfx_swap();
}

void render_text(LONG x, LONG y, UBYTE pen, const char *text)
{
    SetAPen(rp, pen);
    Move(rp, x, y);
    Text(rp, (STRPTR)text, (LONG)strlen(text));
}

/* World -> screen isometric projection. wz is world height (0 = floor). */
static void iso_project(LONG wx, LONG wy, LONG wz, LONG *sx, LONG *sy)
{
    *sx = (wx - wy) * (ISO_TILE_W / 2) + ORG_X;
    *sy = (wx + wy) * (ISO_TILE_H / 2) - wz * ISO_CUBE_H + ORG_Y;
}

/* Fill a horizontal-scanline "quad" - no arbitrary polygon fill in
 * plain graphics.library, so we approximate the diamond / parallelogram
 * shapes with a series of RectFill scanlines. Fast enough for a
 * pixel-count budget of a few hundred quads. */
static void fill_top_diamond(LONG cx, LONG cy, UBYTE pen)
{
    LONG half_w;
    LONG half_h;
    LONG i;
    /* A tile top-face diamond of full width ISO_TILE_W and height
     * ISO_TILE_H, centred on (cx, cy). Draw as pairs of scanlines
     * expanding from centre outward. */
    SetAPen(rp, pen);
    half_w = ISO_TILE_W / 2;
    half_h = ISO_TILE_H / 2;
    for (i = 0; i < half_h; i++) {
        LONG w = ((i + 1) * half_w) / half_h;
        RectFill(rp, cx - w, cy - i, cx + w - 1, cy - i);
        RectFill(rp, cx - w, cy + i, cx + w - 1, cy + i);
    }
}

/* Left face - the SW-facing parallelogram of a cube whose top-face
 * centre is at (cx, cy_top_center). Sits below the top diamond,
 * slanting down-right along the west-to-south edge.
 *
 * Top edge (from top diamond's W corner to S corner):
 *   (cx - half_w, cy_top_center) -> (cx, cy_top_center + half_h)
 * Bottom edge (same, shifted down by ISO_CUBE_H * stack_h):
 *   (cx - half_w, cy_top_center + face_h) -> (cx, cy_top_center + half_h + face_h)
 *
 * Rendered as one-pixel-wide vertical RectFills per column so the
 * parallelogram is completely solid - the earlier WritePixel-with-mask
 * approach was leaving gaps. */
static void fill_left_face(LONG cx, LONG cy_top_center, LONG face_h, UBYTE pen)
{
    LONG half_w;
    LONG half_h;
    LONG col;
    SetAPen(rp, pen);
    half_w = ISO_TILE_W / 2;
    half_h = ISO_TILE_H / 2;
    for (col = 0; col < half_w; col++) {
        LONG x  = cx - half_w + col;
        LONG y0 = cy_top_center + (col * half_h) / half_w;
        LONG y1 = y0 + face_h - 1;
        RectFill(rp, x, y0, x, y1);
    }
}

/* Right face - the SE-facing parallelogram, mirror of left. */
static void fill_right_face(LONG cx, LONG cy_top_center, LONG face_h, UBYTE pen)
{
    LONG half_w;
    LONG half_h;
    LONG col;
    SetAPen(rp, pen);
    half_w = ISO_TILE_W / 2;
    half_h = ISO_TILE_H / 2;
    for (col = 0; col < half_w; col++) {
        LONG x  = cx + col;
        /* Right face's top edge starts at S corner and rises to E
         * corner as we move right, so y decreases with col. */
        LONG y0 = cy_top_center + half_h - (col * half_h) / half_w;
        LONG y1 = y0 + face_h - 1;
        RectFill(rp, x, y0, x, y1);
    }
}

/* Outline a cube's silhouette in a dark pen so faces read as edges,
 * not blobs. Draws the 6 visible edges of a wireframe cube seen from
 * the front-right. */
static void outline_cube(LONG cx, LONG cy_top_center, LONG face_h, UBYTE pen)
{
    LONG half_w;
    LONG half_h;
    SetAPen(rp, pen);
    half_w = ISO_TILE_W / 2;
    half_h = ISO_TILE_H / 2;
    /* Top diamond edges. */
    Move(rp, cx - half_w, cy_top_center);         Draw(rp, cx, cy_top_center - half_h);
    Move(rp, cx, cy_top_center - half_h);         Draw(rp, cx + half_w, cy_top_center);
    Move(rp, cx + half_w, cy_top_center);         Draw(rp, cx, cy_top_center + half_h);
    Move(rp, cx, cy_top_center + half_h);         Draw(rp, cx - half_w, cy_top_center);
    /* Three vertical drops from the visible top-front corners. */
    Move(rp, cx - half_w, cy_top_center);         Draw(rp, cx - half_w, cy_top_center + face_h);
    Move(rp, cx, cy_top_center + half_h);         Draw(rp, cx, cy_top_center + half_h + face_h);
    Move(rp, cx + half_w, cy_top_center);         Draw(rp, cx + half_w, cy_top_center + face_h);
    /* Bottom-front edges of the two visible faces. */
    Move(rp, cx - half_w, cy_top_center + face_h);Draw(rp, cx, cy_top_center + half_h + face_h);
    Move(rp, cx, cy_top_center + half_h + face_h);Draw(rp, cx + half_w, cy_top_center + face_h);
}

/* Draw a stacked cube (stack = height in cube units) at world
 * (wx, wy, wz=0). Faces + top + outline. */
static void draw_stacked_cube(LONG wx, LONG wy, LONG stack,
                              UBYTE pen_top, UBYTE pen_left, UBYTE pen_right,
                              UBYTE pen_outline)
{
    LONG sx, sy;
    LONG face_h;
    iso_project(wx, wy, stack, &sx, &sy);   /* top face centre */
    face_h = stack * ISO_CUBE_H;
    fill_left_face(sx, sy, face_h, pen_left);
    fill_right_face(sx, sy, face_h, pen_right);
    fill_top_diamond(sx, sy, pen_top);
    outline_cube(sx, sy, face_h, pen_outline);
}

/* Draw a single unit cube. */
static void draw_cube(LONG wx, LONG wy,
                      UBYTE pen_top, UBYTE pen_left, UBYTE pen_right,
                      UBYTE pen_outline)
{
    draw_stacked_cube(wx, wy, 1, pen_top, pen_left, pen_right, pen_outline);
}

/* Draw a floor tile at wz=0 (flat diamond only). */
static void draw_floor_tile(LONG wx, LONG wy, UBYTE kind)
{
    LONG sx, sy;
    UBYTE pen;
    LONG half_w;
    LONG half_h;
    iso_project(wx, wy, 0, &sx, &sy);
    pen = (kind == F_DOOR) ? 5 : 1;
    fill_top_diamond(sx, sy, pen);
    /* Faint darker outline for grout. */
    SetAPen(rp, 4);
    half_w = ISO_TILE_W / 2;
    half_h = ISO_TILE_H / 2;
    /* Top-left, top-right, bottom-left, bottom-right edges of the diamond. */
    Move(rp, sx - half_w, sy);       Draw(rp, sx, sy - half_h);
    Move(rp, sx, sy - half_h);       Draw(rp, sx + half_w, sy);
    Move(rp, sx + half_w, sy);       Draw(rp, sx, sy + half_h);
    Move(rp, sx, sy + half_h);       Draw(rp, sx - half_w, sy);
}

/* Draw a small "player" - a red pillar so they're immediately visible
 * against the yellow walls / floor. Facing indicator = a bright white
 * pixel on top diamond in the facing direction. */
static void draw_player(const Player *p)
{
    LONG sx, sy;
    /* Two-cube tall pillar, red body with lit right shoulder for
     * strong 3D reading. */
    draw_stacked_cube(p->wx, p->wy, 2, /*top*/6, /*left*/7, /*right*/3, /*outline*/4);
    iso_project(p->wx, p->wy, 2, &sx, &sy);
    SetAPen(rp, 0);
    switch (p->facing) {
        case 0: RectFill(rp, sx - 3, sy - 6, sx + 2, sy - 4); break;
        case 1: RectFill(rp, sx + 3, sy - 2, sx + 7, sy + 1); break;
        case 2: RectFill(rp, sx - 3, sy + 3, sx + 2, sy + 5); break;
        case 3: RectFill(rp, sx - 7, sy - 2, sx - 3, sy + 1); break;
    }
}

static void draw_jewel(const Prop *o)
{
    LONG sx, sy;
    iso_project(o->wx, o->wy, 0, &sx, &sy);
    /* Small bright rhombus above the floor. */
    SetAPen(rp, 3);
    RectFill(rp, sx - 2, sy - 6, sx + 1, sy - 3);
    RectFill(rp, sx - 3, sy - 5, sx + 2, sy - 4);
    SetAPen(rp, 6);
    WritePixel(rp, sx, sy - 6);
}

/* Sort a small array of object indices by (wx+wy) ascending - plain
 * insertion sort, enough for <=MAX_OBJECTS entries. */
static void sort_objects(const GameState *gs, LONG *idx, LONG n)
{
    LONG i;
    for (i = 1; i < n; i++) {
        LONG cur = idx[i];
        LONG ck  = (LONG)gs->objects[cur].wx + (LONG)gs->objects[cur].wy;
        LONG j = i;
        while (j > 0) {
            LONG pk = (LONG)gs->objects[idx[j-1]].wx + (LONG)gs->objects[idx[j-1]].wy;
            if (pk <= ck) break;
            idx[j] = idx[j-1];
            j--;
        }
        idx[j] = cur;
    }
}

static void draw_hud(const GameState *gs)
{
    char buf[64];
    SetAPen(rp, 4);
    RectFill(rp, 0, PLAY_H, SCREEN_W - 1, SCREEN_H - 1);
    sprintf(buf, "SCORE %06ld", (long)gs->score);
    render_text(4, PLAY_H + 12, 6, buf);
    sprintf(buf, "JEWELS %ld", (long)gs->jewels_left);
    render_text(230, PLAY_H + 12, 6, buf);
}

void render_frame(const GameState *gs)
{
    LONG wy;
    LONG wx;
    LONG idx[MAX_OBJECTS + 1];
    LONG n;
    LONG i;
    LONG pk;
    LONG drew_player;

    /* Port: skip the redraw when the room on screen is this one. */
    memset(&want, 0, sizeof(want));
    want.valid = 1;
    want.score = gs->score;
    want.jewels_left = gs->jewels_left;
    want.num_objects = gs->num_objects;
    memcpy(&want.player, &gs->player, sizeof(want.player));
    memcpy(want.objects, gs->objects, sizeof(want.objects));
    memcpy(want.floor, gs->floor, sizeof(want.floor));
    if (shown.valid && memcmp(&want, &shown, sizeof(want)) == 0)
        return;
    memcpy(&shown, &want, sizeof(shown));

    /* Clear play area to bg. */
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, PLAY_H - 1);

    /* 1. Floor tiles - always at wz=0, drawn back-to-front by row+col
     *    so overlaps are visually consistent. */
    for (wy = 0; wy < WORLD_H; wy++)
        for (wx = 0; wx < WORLD_W; wx++)
            draw_floor_tile(wx, wy, gs->floor[wy][wx]);

    /* 2. Objects + player - combined painter-sorted pass. Player slot
     *    is index gs->num_objects (sentinel). */
    n = 0;
    for (i = 0; i < gs->num_objects; i++) {
        if (gs->objects[i].kind == O_NONE) continue;
        idx[n++] = i;
    }
    /* Sort objects by (wx+wy). */
    sort_objects(gs, idx, n);

    /* Merge the player into the draw pass by its own (wx+wy) key. */
    pk = gs->player.wx + gs->player.wy;
    drew_player = 0;
    for (i = 0; i < n; i++) {
        LONG ok = (LONG)gs->objects[idx[i]].wx + (LONG)gs->objects[idx[i]].wy;
        const Prop *o;
        if (!drew_player && pk < ok) {
            draw_player(&gs->player);
            drew_player = 1;
        }
        o = &gs->objects[idx[i]];
        switch (o->kind) {
            case O_WALL:
                draw_stacked_cube(o->wx, o->wy, 3, /*top*/2, /*left*/1, /*right*/3, /*outline*/4);
                break;
            case O_BARREL:
                draw_cube(o->wx, o->wy, /*top*/3, /*left*/2, /*right*/1, /*outline*/4);
                break;
            case O_JEWEL:
                draw_jewel(o);
                break;
            default: break;
        }
    }
    if (!drew_player) draw_player(&gs->player);

    draw_hud(gs);
}

void render_title(const GameState *gs)
{
    shown.valid = 0;
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    SetAPen(rp, 2);
    RectFill(rp, 0, 40, SCREEN_W - 1, 79);
    render_text(96, 66, 0, "WARDEN  KEEP");

    render_text(48, 120, 3, "AN ISOMETRIC KEEP TO PLUNDER");
    render_text(56, 138, 6, "PUSH BARRELS, GRAB JEWELS");

    render_text(56, 176, 3, "D-PAD       :  MOVE");
    render_text(56, 190, 3, "A OR P      :  BEGIN");
    render_text(56, 204, 3, "X           :  QUIT TO MENU");

    if ((gs->tick / 15) & 1)
        render_text(88, 232, 7, "PRESS  A  TO  BEGIN");
}

void render_endscreen(const GameState *gs)
{
    char buf[64];
    shown.valid = 0;
    SetAPen(rp, 0);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);
    SetAPen(rp, 3);
    RectFill(rp, 0, 80, SCREEN_W - 1, 109);
    render_text(64, 100, 0, "EVERY JEWEL CLAIMED");

    sprintf(buf, "FINAL SCORE  %06ld", (long)gs->score);
    render_text(88, 156, 6, buf);

    if (gs->state_timer == 0 && ((gs->tick / 15) & 1))
        render_text(88, 208, 2, "PRESS A TO RETURN");
}
