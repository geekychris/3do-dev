// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - World/room data and procedural generation
 */
#include <string.h>
#include "game.h"

/* World room tile data: [row][col][tile_y][tile_x] */
UBYTE world_rooms[WORLD_H][WORLD_W][ROOM_H][ROOM_W];

/* Item spawn table */
ItemSpawn g_items[MAX_ITEMS];
WORD g_num_items = 0;

/* Room type identifiers for generation */
#define RTYPE_SOLID      0
#define RTYPE_CAVERN     1
#define RTYPE_MAGMA      2
#define RTYPE_CORRIDOR   3
#define RTYPE_BOSS_BEAST 4
#define RTYPE_BOSS_DRAKE 5
#define RTYPE_CITADEL    6
#define RTYPE_ROLL_AREA  7
#define RTYPE_START      8

/* World layout map: which type each room is */
static UBYTE room_types[WORLD_H][WORLD_W] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 1, 1, 1, 3, 2, 2, 0 },
    { 0, 1, 8, 1, 3, 2, 2, 0 },
    { 0, 1, 1, 3, 4, 2, 5, 0 },
    { 0, 7, 1, 6, 6, 6, 0, 0 },
    { 0, 0, 0, 0, 0, 0, 0, 0 }
};

/* Deterministic hash for room-specific randomness */
static ULONG room_hash(WORD rx, WORD ry, WORD seed)
{
    ULONG h = (ULONG)rx * 7919 + (ULONG)ry * 6271 + (ULONG)seed * 1301;
    h ^= h >> 11;
    h *= 2654435761UL;
    h ^= h >> 16;
    return h;
}

/* Check if room edge should have a doorway (connected to non-solid neighbor) */
static WORD has_exit_right(WORD rx, WORD ry)
{
    if (rx + 1 >= WORLD_W) return 0;
    return (room_types[ry][rx + 1] != RTYPE_SOLID) ? 1 : 0;
}

static WORD has_exit_left(WORD rx, WORD ry)
{
    if (rx - 1 < 0) return 0;
    return (room_types[ry][rx - 1] != RTYPE_SOLID) ? 1 : 0;
}

static WORD has_exit_down(WORD rx, WORD ry)
{
    if (ry + 1 >= WORLD_H) return 0;
    return (room_types[ry + 1][rx] != RTYPE_SOLID) ? 1 : 0;
}

static WORD has_exit_up(WORD rx, WORD ry)
{
    if (ry - 1 < 0) return 0;
    return (room_types[ry - 1][rx] != RTYPE_SOLID) ? 1 : 0;
}

/* Fill a room with a base tile */
static void fill_room(WORD rx, WORD ry, UBYTE tile)
{
    WORD x, y;
    for (y = 0; y < ROOM_H; y++)
        for (x = 0; x < ROOM_W; x++)
            world_rooms[ry][rx][y][x] = tile;
}

/* Add floor, ceiling, and walls to a room */
static void add_shell(WORD rx, WORD ry, UBYTE wall_tile,
                      WORD floor_rows, WORD ceil_rows)
{
    WORD x, y;

    /* Floor */
    for (y = ROOM_H - floor_rows; y < ROOM_H; y++)
        for (x = 0; x < ROOM_W; x++)
            world_rooms[ry][rx][y][x] = wall_tile;

    /* Ceiling */
    for (y = 0; y < ceil_rows; y++)
        for (x = 0; x < ROOM_W; x++)
            world_rooms[ry][rx][y][x] = wall_tile;

    /* Left wall (if no exit left) */
    if (!has_exit_left(rx, ry)) {
        for (y = 0; y < ROOM_H; y++)
            world_rooms[ry][rx][y][0] = wall_tile;
    } else {
        /* Doorway: clear middle of left edge */
        for (y = ROOM_H / 2 - 2; y < ROOM_H / 2 + 2; y++) {
            if (y >= ceil_rows && y < ROOM_H - floor_rows)
                world_rooms[ry][rx][y][0] = TILE_EMPTY;
        }
    }

    /* Right wall */
    if (!has_exit_right(rx, ry)) {
        for (y = 0; y < ROOM_H; y++)
            world_rooms[ry][rx][y][ROOM_W - 1] = wall_tile;
    } else {
        for (y = ROOM_H / 2 - 2; y < ROOM_H / 2 + 2; y++) {
            if (y >= ceil_rows && y < ROOM_H - floor_rows)
                world_rooms[ry][rx][y][ROOM_W - 1] = TILE_EMPTY;
        }
    }

    /* Top exit */
    if (has_exit_up(rx, ry)) {
        for (x = ROOM_W / 2 - 2; x < ROOM_W / 2 + 2; x++)
            for (y = 0; y < ceil_rows; y++)
                world_rooms[ry][rx][y][x] = TILE_EMPTY;
    }

    /* Bottom exit */
    if (has_exit_down(rx, ry)) {
        for (x = ROOM_W / 2 - 2; x < ROOM_W / 2 + 2; x++)
            for (y = ROOM_H - floor_rows; y < ROOM_H; y++)
                world_rooms[ry][rx][y][x] = TILE_EMPTY;
    }
}

/* Add platforms using deterministic hash */
static void add_platforms(WORD rx, WORD ry, UBYTE plat_tile, WORD count)
{
    WORD i;
    for (i = 0; i < count; i++) {
        ULONG h = room_hash(rx, ry, i * 37);
        WORD px = 3 + (WORD)(h % (ROOM_W - 6));
        WORD py = 4 + (WORD)((h >> 8) % (ROOM_H - 8));
        WORD len = 2 + (WORD)((h >> 16) % 3);
        WORD j;
        for (j = 0; j < len && px + j < ROOM_W - 1; j++)
            world_rooms[ry][rx][py][px + j] = plat_tile;
    }
}

/* Generate a cavern-style room */
static void gen_cavern(WORD rx, WORD ry)
{
    ULONG h = room_hash(rx, ry, 0);

    fill_room(rx, ry, TILE_EMPTY);
    add_shell(rx, ry, TILE_ROCK, 2, 1);

    /* Stalactites from ceiling */
    {
        WORD i;
        for (i = 0; i < 4; i++) {
            WORD sx = 2 + (WORD)((room_hash(rx, ry, 100 + i)) % (ROOM_W - 4));
            world_rooms[ry][rx][1][sx] = TILE_ROCK;
            if ((h >> (i * 3)) & 1)
                world_rooms[ry][rx][2][sx] = TILE_ROCK;
        }
    }

    /* Floor formations */
    {
        WORD i;
        for (i = 0; i < 3; i++) {
            WORD fx = 3 + (WORD)((room_hash(rx, ry, 200 + i)) % (ROOM_W - 6));
            world_rooms[ry][rx][ROOM_H - 3][fx] = TILE_ROCK;
            world_rooms[ry][rx][ROOM_H - 3][fx + 1] = TILE_ROCK;
        }
    }

    /* Platforms */
    add_platforms(rx, ry, TILE_PLATFORM, 2 + (WORD)(h % 3));
}

/* Generate a magma-style room */
static void gen_magma(WORD rx, WORD ry)
{
    ULONG h = room_hash(rx, ry, 0);

    fill_room(rx, ry, TILE_EMPTY);
    add_shell(rx, ry, TILE_METAL, 2, 1);

    /* Lava pools on floor */
    {
        WORD i;
        for (i = 0; i < 3; i++) {
            WORD lx = 2 + (WORD)((room_hash(rx, ry, 300 + i)) % (ROOM_W - 6));
            WORD len = 2 + (WORD)((room_hash(rx, ry, 310 + i)) % 3);
            WORD j;
            for (j = 0; j < len && lx + j < ROOM_W - 1; j++)
                world_rooms[ry][rx][ROOM_H - 2][lx + j] = TILE_LAVA;
        }
    }

    /* Metal platforms above lava */
    add_platforms(rx, ry, TILE_METAL, 3 + (WORD)(h % 2));

    /* Some brick detail */
    {
        WORD bx = 5 + (WORD)(h % 8);
        world_rooms[ry][rx][ROOM_H - 3][bx] = TILE_BRICK;
        world_rooms[ry][rx][ROOM_H - 3][bx + 1] = TILE_BRICK;
    }
}

/* Generate a corridor room */
static void gen_corridor(WORD rx, WORD ry)
{
    fill_room(rx, ry, TILE_EMPTY);
    add_shell(rx, ry, TILE_BRICK, 2, 2);

    /* Simple flat corridor with a platform or two */
    add_platforms(rx, ry, TILE_PLATFORM, 1);
}

/* Generate boss arena: Beast */
static void gen_boss_beast(WORD rx, WORD ry)
{
    fill_room(rx, ry, TILE_EMPTY);
    add_shell(rx, ry, TILE_METAL, 2, 1);

    /* Platform ledges for dodging */
    {
        WORD x;
        for (x = 3; x < 7; x++)
            world_rooms[ry][rx][8][x] = TILE_PLATFORM;
        for (x = 13; x < 17; x++)
            world_rooms[ry][rx][6][x] = TILE_PLATFORM;
        for (x = 8; x < 12; x++)
            world_rooms[ry][rx][10][x] = TILE_PLATFORM;
    }

    /* Locked door on entry side - fill entire doorway carved by add_shell */
    if (has_exit_left(rx, ry)) {
        WORD y;
        for (y = ROOM_H / 2 - 2; y < ROOM_H / 2 + 2; y++)
            world_rooms[ry][rx][y][0] = TILE_DOOR_LOCKED;
    }
}

/* Generate boss arena: Drake */
static void gen_boss_drake(WORD rx, WORD ry)
{
    fill_room(rx, ry, TILE_EMPTY);
    add_shell(rx, ry, TILE_METAL, 2, 1);

    /* Open arena with high ledges */
    {
        WORD x;
        for (x = 2; x < 5; x++)
            world_rooms[ry][rx][5][x] = TILE_PLATFORM;
        for (x = 15; x < 18; x++)
            world_rooms[ry][rx][5][x] = TILE_PLATFORM;
        for (x = 8; x < 12; x++)
            world_rooms[ry][rx][9][x] = TILE_PLATFORM;
    }

    /* Some lava at bottom */
    {
        WORD x;
        for (x = 4; x < 16; x++)
            world_rooms[ry][rx][ROOM_H - 2][x] = TILE_LAVA;
    }
}

/* Generate citadel room */
static void gen_citadel(WORD rx, WORD ry)
{
    ULONG h = room_hash(rx, ry, 0);

    fill_room(rx, ry, TILE_EMPTY);
    add_shell(rx, ry, TILE_METAL, 3, 2);

    /* Tight metallic corridors */
    {
        WORD wx = 5 + (WORD)(h % 6);
        WORD y;
        for (y = 2; y < ROOM_H - 5; y++)
            world_rooms[ry][rx][y][wx] = TILE_METAL;
    }

    /* Platform stepping stones */
    add_platforms(rx, ry, TILE_METAL, 2);

    /* A locked door somewhere - fill entire doorway carved by add_shell */
    if (has_exit_right(rx, ry) && (h & 1)) {
        WORD y;
        for (y = ROOM_H / 2 - 2; y < ROOM_H / 2 + 2; y++)
            world_rooms[ry][rx][y][ROOM_W - 1] = TILE_DOOR_LOCKED;
    }
}

/* Generate roll form area: low ceilings */
static void gen_roll_area(WORD rx, WORD ry)
{
    WORD x, y;

    fill_room(rx, ry, TILE_EMPTY);
    add_shell(rx, ry, TILE_ROCK, 2, 1);

    /* Low ceiling passage in middle section */
    for (y = 3; y < 5; y++)
        for (x = 2; x < ROOM_W - 2; x++)
            world_rooms[ry][rx][y][x] = TILE_ROCK;

    /* Open crawl space below the low ceiling */
    for (y = 5; y < 10; y++)
        for (x = 2; x < ROOM_W - 2; x++)
            world_rooms[ry][rx][y][x] = TILE_EMPTY;

    /* Low ceiling forces roll form */
    for (x = 4; x < ROOM_W - 4; x++)
        world_rooms[ry][rx][8][x] = TILE_ROCK;

    /* Small gap to crawl through */
    for (x = 6; x < 14; x++) {
        world_rooms[ry][rx][8][x] = TILE_EMPTY;
        world_rooms[ry][rx][9][x] = TILE_EMPTY;
    }

    /* Ensure exits are clear */
    if (has_exit_left(rx, ry)) {
        for (y = 5; y < 8; y++)
            world_rooms[ry][rx][y][0] = TILE_EMPTY;
    }
    if (has_exit_up(rx, ry)) {
        for (x = ROOM_W / 2 - 2; x < ROOM_W / 2 + 2; x++)
            for (y = 0; y < 3; y++)
                world_rooms[ry][rx][y][x] = TILE_EMPTY;
    }
}

/* Generate start room */
static void gen_start_room(WORD rx, WORD ry)
{
    fill_room(rx, ry, TILE_EMPTY);
    add_shell(rx, ry, TILE_ROCK, 2, 1);

    /* Save point */
    world_rooms[ry][rx][ROOM_H - 3][3] = TILE_SAVE_POINT;

    /* A couple platforms */
    {
        WORD x;
        for (x = 6; x < 10; x++)
            world_rooms[ry][rx][10][x] = TILE_PLATFORM;
        for (x = 12; x < 16; x++)
            world_rooms[ry][rx][7][x] = TILE_PLATFORM;
    }
}

/* --- Item placement --- */

static void place_items(void)
{
    WORD i = 0;

    /* Roll Form - room (1,4) roll area, accessible early */
    g_items[i].room_x = 1; g_items[i].room_y = 4;
    g_items[i].tile_x = 10; g_items[i].tile_y = 6;
    g_items[i].type = ITEM_ROLL_FORM;
    g_items[i].collected = 0; i++;

    /* Missiles - room (2,1) cavern */
    g_items[i].room_x = 2; g_items[i].room_y = 1;
    g_items[i].tile_x = 10; g_items[i].tile_y = 12;
    g_items[i].type = ITEM_MISSILE_PACK;
    g_items[i].collected = 0; i++;

    /* Bombs - room (3,2) requires roll form */
    g_items[i].room_x = 3; g_items[i].room_y = 2;
    g_items[i].tile_x = 15; g_items[i].tile_y = 12;
    g_items[i].type = ITEM_BOMBS;
    g_items[i].collected = 0; i++;

    /* Long Beam - room (5,1) magma */
    g_items[i].room_x = 5; g_items[i].room_y = 1;
    g_items[i].tile_x = 10; g_items[i].tile_y = 11;
    g_items[i].type = ITEM_LONG_BEAM;
    g_items[i].collected = 0; i++;

    /* Ice Beam - room (6,2) magma */
    g_items[i].room_x = 6; g_items[i].room_y = 2;
    g_items[i].tile_x = 10; g_items[i].tile_y = 11;
    g_items[i].type = ITEM_ICE_BEAM;
    g_items[i].collected = 0; i++;

    /* High Jump - room (3,3) near Beast */
    g_items[i].room_x = 3; g_items[i].room_y = 3;
    g_items[i].tile_x = 5; g_items[i].tile_y = 12;
    g_items[i].type = ITEM_HIGH_JUMP;
    g_items[i].collected = 0; i++;

    /* Heat Suit - room (5,2) magma */
    g_items[i].room_x = 5; g_items[i].room_y = 2;
    g_items[i].tile_x = 15; g_items[i].tile_y = 11;
    g_items[i].type = ITEM_HEAT_SUIT;
    g_items[i].collected = 0; i++;

    /* Spin Strike - room (6,3) after Drake */
    g_items[i].room_x = 6; g_items[i].room_y = 3;
    g_items[i].tile_x = 16; g_items[i].tile_y = 12;
    g_items[i].type = ITEM_SPIN_STRIKE;
    g_items[i].collected = 0; i++;

    /* Energy Tank 1 - room (1,1) cavern */
    g_items[i].room_x = 1; g_items[i].room_y = 1;
    g_items[i].tile_x = 10; g_items[i].tile_y = 5;
    g_items[i].type = ITEM_ENERGY_TANK;
    g_items[i].collected = 0; i++;

    /* Energy Tank 2 - room (6,1) magma */
    g_items[i].room_x = 6; g_items[i].room_y = 1;
    g_items[i].tile_x = 5; g_items[i].tile_y = 11;
    g_items[i].type = ITEM_ENERGY_TANK;
    g_items[i].collected = 0; i++;

    /* Energy Tank 3 - room (4,4) citadel */
    g_items[i].room_x = 4; g_items[i].room_y = 4;
    g_items[i].tile_x = 10; g_items[i].tile_y = 10;
    g_items[i].type = ITEM_ENERGY_TANK;
    g_items[i].collected = 0; i++;

    /* Extra missile packs */
    g_items[i].room_x = 1; g_items[i].room_y = 3;
    g_items[i].tile_x = 5; g_items[i].tile_y = 12;
    g_items[i].type = ITEM_MISSILE_PACK;
    g_items[i].collected = 0; i++;

    g_items[i].room_x = 5; g_items[i].room_y = 3;
    g_items[i].tile_x = 15; g_items[i].tile_y = 11;
    g_items[i].type = ITEM_MISSILE_PACK;
    g_items[i].collected = 0; i++;

    g_num_items = i;
}

/* --- Main init --- */

void levels_init(void)
{
    WORD rx, ry;

    /* Generate all rooms */
    for (ry = 0; ry < WORLD_H; ry++) {
        for (rx = 0; rx < WORLD_W; rx++) {
            switch (room_types[ry][rx]) {
                case RTYPE_SOLID:
                    fill_room(rx, ry, TILE_ROCK);
                    break;
                case RTYPE_CAVERN:
                    gen_cavern(rx, ry);
                    break;
                case RTYPE_MAGMA:
                    gen_magma(rx, ry);
                    break;
                case RTYPE_CORRIDOR:
                    gen_corridor(rx, ry);
                    break;
                case RTYPE_BOSS_BEAST:
                    gen_boss_beast(rx, ry);
                    break;
                case RTYPE_BOSS_DRAKE:
                    gen_boss_drake(rx, ry);
                    break;
                case RTYPE_CITADEL:
                    gen_citadel(rx, ry);
                    break;
                case RTYPE_ROLL_AREA:
                    gen_roll_area(rx, ry);
                    break;
                case RTYPE_START:
                    gen_start_room(rx, ry);
                    break;
            }
        }
    }

    /* Place items */
    place_items();
}
