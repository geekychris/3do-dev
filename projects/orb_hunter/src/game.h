// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * Orb Hunter - Game definitions
 * A exploration-platformer side-scrolling exploration game for Amiga
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen dimensions */
#define SCREEN_W    320
#define SCREEN_H    256
#define BITMAP_W    352   /* oversized for smooth scrolling */
#define TILE_W      16
#define TILE_H      16

/* Room/world dimensions */
#define ROOM_W      20    /* tiles per room width */
#define ROOM_H      16    /* tiles per room height */
#define WORLD_W     8     /* rooms across */
#define WORLD_H     6     /* rooms tall */

/* Fixed-point 16.16 */
typedef LONG Fixed;
#define FIX(x)       ((Fixed)((x) * 65536L))
#define FIXF(x)      ((Fixed)((x) * 65536.0f))
#define FIX_INT(x)   ((x) >> 16)
#define FIX_FRAC(x)  ((x) & 0xFFFF)
#define FIX_MUL(a,b) ((Fixed)(((LONG)(a) >> 8) * ((LONG)(b) >> 8)))

/* Tile types */
#define TILE_EMPTY       0
#define TILE_ROCK        1
#define TILE_BRICK       2
#define TILE_METAL       3
#define TILE_PLATFORM    4   /* one-way platform */
#define TILE_LAVA        5   /* damage tile */
#define TILE_DOOR_LOCKED 6   /* requires missile to open */
#define TILE_DOOR_OPEN   7
#define TILE_ITEM_BLOCK  8   /* contains item */
#define TILE_SAVE_POINT  9
#define NUM_TILE_TYPES   10

/* Tile properties (bit flags) */
#define TPROP_SOLID     1   /* blocks movement */
#define TPROP_PLATFORM  2   /* solid from top only */
#define TPROP_DAMAGE    4   /* hurts player */
#define TPROP_DOOR      8   /* door tile */

extern UBYTE tile_props[NUM_TILE_TYPES];

/* Hunter (player) dimensions */
#define HUNTER_W        14
#define HUNTER_H        16
#define HUNTER_ROLL_H   8    /* height when in roll form */
#define HUNTER_SPEED    FIX(2)
#define HUNTER_JUMP_VEL FIX(-4)
#define HUNTER_HIGH_JUMP FIX(-5)
#define HUNTER_GRAVITY  (65536 / 4)   /* 0.25 fixed */
#define HUNTER_MAX_FALL FIX(5)
#define GUN_COOLDOWN    8
#define MAX_ENERGY      99
#define START_ENERGY    30
#define INVULN_TIME     60

/* Hunter struct */
typedef struct {
    Fixed x, y;         /* position within current room (pixels, fixed) */
    Fixed dx, dy;
    WORD  on_ground;
    WORD  facing;       /* 0=right, 1=left */
    WORD  alive;
    LONG  health;       /* current energy 0..max_energy */
    LONG  max_energy;   /* increases with energy tanks */
    LONG  missiles;     /* missile count */
    LONG  max_missiles;
    WORD  gun_timer;
    WORD  invuln_timer;
    WORD  jump_held;
    WORD  anim_frame;
    WORD  anim_timer;
    /* Power-up flags */
    WORD  has_roll_form;
    WORD  has_bombs;
    WORD  has_missiles;
    WORD  has_long_beam;
    WORD  has_ice_beam;
    WORD  has_spin_strike;
    WORD  has_high_jump;
    WORD  has_heat_suit;
    /* Roll form state */
    WORD  rolled;        /* currently in roll form */
    WORD  bomb_timer;
} Hunter;

/* Bullet types */
#define BULLET_TYPE_NORMAL  0
#define BULLET_TYPE_MISSILE 1
#define BULLET_TYPE_ICE     2

#define MAX_BULLETS     6
#define BULLET_SPEED    FIX(5)
#define MISSILE_SPEED   FIX(4)
#define BULLET_LIFE     30

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  life;
    WORD  active;
    WORD  type;     /* BULLET_TYPE_xxx */
    WORD  power;    /* damage */
} Bullet;

/* Bombs (roll form) */
#define MAX_BOMBS       3
#define BOMB_FUSE       30   /* frames until detonation */
#define BOMB_RADIUS     24   /* pixel radius */

typedef struct {
    Fixed x, y;
    WORD  timer;
    WORD  active;
} Bomb;

/* Enemy types */
#define ENEMY_CRAWLER       0
#define ENEMY_FLYER         1
#define ENEMY_HOPPER        2
#define ENEMY_TURRET        3
#define ENEMY_BOSS_BEAST    4
#define ENEMY_BOSS_DRAKE    5

#define MAX_ENEMIES         12
#define MAX_ENEMY_BULLETS   6

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  type;
    WORD  health;
    WORD  active;
    WORD  facing;
    WORD  state;
    WORD  timer;
    WORD  anim_frame;
    WORD  fire_timer;
} Enemy;

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  life;
    WORD  active;
} EnemyBullet;

/* Particles */
#define MAX_PARTICLES   24

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  life;
    WORD  color;
    WORD  active;
} Particle;

/* Item types */
#define ITEM_ENERGY_TANK    0
#define ITEM_MISSILE_PACK   1
#define ITEM_ROLL_FORM      2
#define ITEM_BOMBS          3
#define ITEM_LONG_BEAM      4
#define ITEM_ICE_BEAM       5
#define ITEM_HIGH_JUMP      6
#define ITEM_SPIN_STRIKE    7
#define ITEM_HEAT_SUIT      8
#define ITEM_MISSILE_DOOR   9
#define MAX_ITEMS           24

typedef struct {
    WORD room_x, room_y;
    WORD tile_x, tile_y;
    WORD type;
    WORD collected;       /* already picked up? */
} ItemSpawn;

/* Game states */
#define STATE_TITLE         0
#define STATE_PLAYING       1
#define STATE_DEAD          2
#define STATE_GAMEOVER      3
#define STATE_ITEM_GET      4
#define STATE_ROOM_TRANSITION 5

/* Game state */
typedef struct {
    Hunter      hunter;
    Bullet      bullets[MAX_BULLETS];
    Bomb        bombs[MAX_BOMBS];
    Enemy       enemies[MAX_ENEMIES];
    EnemyBullet enemy_bullets[MAX_ENEMY_BULLETS];
    Particle    particles[MAX_PARTICLES];
    LONG        room_x, room_y;   /* current room in world grid */
    LONG        score;
    WORD        state;
    WORD        state_timer;
    WORD        frame;
    LONG        items_collected;  /* bitmask of collected items */
    WORD        doors_opened;     /* bitmask of opened doors */
    WORD        transition_dir;   /* 0=right,1=left,2=down,3=up */
    char        item_name[32];    /* name of last collected item for display */
} GameState;

/* World room data */
extern UBYTE world_rooms[WORLD_H][WORLD_W][ROOM_H][ROOM_W];

/* Item spawn table */
extern ItemSpawn g_items[MAX_ITEMS];
extern WORD g_num_items;

/* Core functions */
void game_init(GameState *gs);
void game_load_room(GameState *gs);
void game_update(GameState *gs, WORD inp_left, WORD inp_right,
                 WORD inp_up, WORD inp_down,
                 WORD inp_jump, WORD inp_fire);
void game_spawn_particles(GameState *gs, Fixed x, Fixed y,
                          WORD count, WORD color);

/* Level generation (levels.c) */
void levels_init(void);

/* SFX callbacks (defined in main.c) */
void sfx_shoot(void);
void sfx_hit(void);
void sfx_explode(void);
void sfx_powerup(void);
void sfx_jump(void);
void sfx_player_hit(void);
void sfx_missile(void);
void sfx_door(void);

#endif
