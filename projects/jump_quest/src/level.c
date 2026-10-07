// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Jump Quest - Level System
 * Level data, tile rendering, collision
 */
#include "game.h"
#include <string.h>
#include "levels/level1.h"
#include "levels/level2.h"
#include "levels/level3.h"

static LevelDef levels[] = {
    { LEVEL1_W, 14, (const UBYTE *)level1_tiles, level1_ents, 0x059C, 0x047A, "Office Park" },
    { LEVEL2_W, 14, (const UBYTE *)level2_tiles, level2_ents, 0x0337, 0x0225, "Server Room" },
    { LEVEL3_W, 14, (const UBYTE *)level3_tiles, level3_ents, 0x0D83, 0x0741, "Rooftop" },
};

#define NUM_LEVELS 3

static int cur_level = 0;
static UBYTE tile_buf[200 * 14]; /* mutable copy of tile data */

void level_load(int level_num) {
    const LevelDef *ld;
    ULONG size;

    if (level_num < 0 || level_num >= NUM_LEVELS)
        level_num = 0;

    cur_level = level_num;
    ld = &levels[cur_level];
    size = (ULONG)ld->width * (ULONG)ld->height;
    if (size > sizeof(tile_buf)) size = sizeof(tile_buf);
    memcpy(tile_buf, ld->tiles, size);
}

const LevelDef *level_current(void) {
    return &levels[cur_level];
}

UBYTE level_get_tile(int tx, int ty) {
    const LevelDef *ld = &levels[cur_level];
    if (tx < 0 || tx >= (int)ld->width || ty < 0 || ty >= (int)ld->height)
        return TILE_EMPTY;
    return tile_buf[ty * ld->width + tx];
}

void level_set_tile(int tx, int ty, UBYTE tile) {
    const LevelDef *ld = &levels[cur_level];
    if (tx < 0 || tx >= (int)ld->width || ty < 0 || ty >= (int)ld->height)
        return;
    tile_buf[ty * ld->width + tx] = tile;
}

BOOL level_is_solid(int tx, int ty) {
    UBYTE t = level_get_tile(tx, ty);
    switch (t) {
    case TILE_GROUND:
    case TILE_GRASS:
    case TILE_BRICK:
    case TILE_QBLOCK:
    case TILE_QBLOCK_HIT:
    case TILE_STONE:
    case TILE_PIPE_TL:
    case TILE_PIPE_TR:
    case TILE_PIPE_BL:
    case TILE_PIPE_BR:
        return TRUE;
    default:
        return FALSE;
    }
}

BOOL level_is_platform(int tx, int ty) {
    return level_get_tile(tx, ty) == TILE_PLATFORM;
}

int level_width_pixels(void) {
    return levels[cur_level].width * TILE_SIZE;
}

#ifdef AMIGA3DO
/* 3DO port: tiles sit on whole 16 pixel positions, so the picture only
 * depends on which tile ids are visible. Draw it once per change and copy
 * it from a cache otherwise (the ARM60 can't redraw ~280 tiles per frame).
 * Positions the original leaves undrawn (level edge) bypass the cache. */
extern UBYTE *gfx_pixels8(void);
static void level_draw_tiles(struct RastPort *rp, int cam_x);

void level_draw(struct RastPort *rp, int cam_x) {
    static UBYTE *cache = 0;
    static int cached[TILES_Y][TILES_X], valid = 0;
    int ids[TILES_Y][TILES_X];
    int start_tx = cam_x / TILE_SIZE, tx, ty, complete = 1;
    const LevelDef *ld = &levels[cur_level];
    long bytes = (long)SCREEN_W * (TILES_Y * TILE_SIZE);
    UBYTE *screen = gfx_pixels8();

    for (ty = 0; ty < TILES_Y; ty++)
        for (tx = 0; tx < TILES_X; tx++) {
            if (start_tx + tx > (int)ld->width - 1) { complete = 0; ids[ty][tx] = -1; }
            else ids[ty][tx] = level_get_tile(start_tx + tx, ty);
        }
    if (!complete || !screen) {
        level_draw_tiles(rp, cam_x);
        valid = 0;
        return;
    }
    if (!cache)
        cache = (UBYTE *)AllocMem(bytes, 0);
    if (cache && valid && memcmp(ids, cached, sizeof(ids)) == 0) {
        memcpy(screen, cache, bytes);
        return;
    }
    level_draw_tiles(rp, cam_x);
    if (cache) {
        memcpy(cache, screen, bytes);
        memcpy(cached, ids, sizeof(ids));
        valid = 1;
    }
}

static void level_draw_tiles(struct RastPort *rp, int cam_x) {
#else
void level_draw(struct RastPort *rp, int cam_x) {
#endif
    int start_tx = cam_x / TILE_SIZE;
    int tx, ty, sx, sy;
    int end_tx = start_tx + TILES_X;
    const LevelDef *ld = &levels[cur_level];

    if (end_tx >= (int)ld->width) end_tx = (int)ld->width - 1;

    for (ty = 0; ty < TILES_Y; ty++) {
        sy = ty * TILE_SIZE;
        for (tx = start_tx; tx <= end_tx; tx++) {
            sx = (tx - start_tx) * TILE_SIZE;
            if (sx >= SCREEN_W) break;
            gfx_draw_tile(rp, level_get_tile(tx, ty), sx, sy);
        }
    }
}
