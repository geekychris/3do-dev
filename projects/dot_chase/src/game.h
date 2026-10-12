// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * DOT CHASE - a ladybug in a hedge maze, chased by spiders
 * Game definitions and structures
 */
#ifndef GAME_H
#define GAME_H

#include <exec/types.h>

/* Screen dimensions */
#define SCREEN_W    320
#define SCREEN_H    256

/* Maze dimensions: 28x31 tiles of 8x8 pixels = 224x248 */
#define MAZE_W      28
#define MAZE_H      31
#define TILE_SIZE   8

/* Maze offset to center on screen */
#define MAZE_OX     48
#define MAZE_OY     4

/* Tile types in maze */
#define T_EMPTY     0
#define T_WALL      1
#define T_DOT       2
#define T_POWER     3
#define T_SPIDER_DOOR 4

/* Directions */
#define DIR_NONE    -1
#define DIR_RIGHT   0
#define DIR_DOWN    1
#define DIR_LEFT    2
#define DIR_UP      3

/* Spider states */
#define SPIDER_SCATTER   0
#define SPIDER_CHASE     1
#define SPIDER_FRIGHT    2
#define SPIDER_EATEN     3

/* Spider IDs */
#define SPIDER_HUNTER    0
#define SPIDER_AMBUSH     1
#define SPIDER_FLANK      2
#define SPIDER_SHY     3
#define NUM_SPIDERS      4

/* Game states */
#define STATE_TITLE      0
#define STATE_READY      1
#define STATE_PLAYING    2
#define STATE_DYING      3
#define STATE_GAMEOVER   4
#define STATE_LEVEL_DONE 5
#define STATE_EAT_SPIDER  6

/* Speed: pixels per frame (fixed 8.8) */
#define HERO_SPEED     320   /* ~1.25 px/frame at 50fps */
#define SPIDER_SPEED      288   /* slightly slower */
#define SPIDER_FRIGHT_SPD 160   /* slow when frightened */
#define SPIDER_EATEN_SPD  640   /* fast returning to spider house */
#define SPIDER_TUNNEL_SPD 160   /* slow in tunnel */

/* Timing (frames at 50fps) */
#define READY_TIME       100   /* 2 seconds */
#define DYING_TIME       60
#define EAT_SPIDER_TIME   30
#define LEVEL_DONE_TIME  100
#define FRIGHT_TIME      350   /* 7 seconds */
#define FRIGHT_BLINK     200   /* start blinking at this remaining */

/* Scatter/Chase mode durations (in frames) */
#define SCATTER1_TIME    350   /* 7s */
#define CHASE1_TIME      1000  /* 20s */
#define SCATTER2_TIME    350
#define CHASE2_TIME      1000
#define SCATTER3_TIME    250   /* 5s */
#define CHASE3_TIME      1000
#define SCATTER4_TIME    250
/* After scatter4: permanent chase */

/* Scoring */
#define SCORE_DOT        10
#define SCORE_POWER      50
#define SCORE_SPIDER1     150   /* then 300, 600, 1200 */
#define SCORE_BONUS_BASE 100
#define EXTRA_LIFE_SCORE 10000

/* Bonus types: garden treats that appear mid-level */
#define BONUS_BERRY      0
#define BONUS_MUSHROOM   1
#define BONUS_CARROT     2
#define BONUS_PEAR       3
#define BONUS_ACORN      4
#define BONUS_HONEY      5
#define BONUS_SUNFLOWER  6
#define MAX_BONUS_TYPE   7

/* Bonus point values */
static const LONG bonus_scores[MAX_BONUS_TYPE] = {
    150, 250, 400, 600, 900, 1500, 2500
};

/* Sound event flags */
#define EV_SEED     1
#define EV_EAT_SPIDER 2
#define EV_DIE       4
#define EV_BONUS     8
#define EV_EXTRA     16
#define EV_POWER     32

/* Colors (palette in main_3do.c) */
#define COL_BG       0   /* dark soil */
#define COL_HERO     1   /* ladybug red */
#define COL_SP_PURPLE 2  /* the four spiders */
#define COL_SP_STEEL 3
#define COL_SP_BROWN 4
#define COL_SP_BONE  5
#define COL_HEDGE    6   /* maze walls */
#define COL_WHITE    7
#define COL_HEDGE_DK 8
#define COL_HEDGE_LT 9
#define COL_GOLD     10  /* flower centres, popups */
#define COL_BLACK    11  /* ladybug spots, spider legs */
#define COL_LEAF     12
#define COL_PLUM     13
#define COL_GRAY     14
#define COL_SEED     15

/* Spider structure */
typedef struct {
    WORD  tile_x, tile_y;    /* current tile position */
    WORD  pixel_x, pixel_y;  /* pixel position (top-left of sprite) */
    WORD  sub_x, sub_y;      /* fractional sub-pixel (8.8 fixed) */
    WORD  dir;               /* current direction */
    WORD  next_dir;          /* next direction at next intersection */
    WORD  state;             /* SPIDER_SCATTER/CHASE/FRIGHT/EATEN */
    WORD  target_x, target_y; /* target tile for AI */
    WORD  home;              /* 1 if still in spider house */
    WORD  dot_counter;       /* dots needed to leave house */
    WORD  fright_timer;      /* remaining fright frames */
} Spider;

/* Hero structure */
typedef struct {
    WORD  tile_x, tile_y;
    WORD  pixel_x, pixel_y;
    WORD  sub_x, sub_y;
    WORD  dir;
    WORD  next_dir;          /* buffered direction input */
    WORD  anim_frame;
    WORD  anim_timer;
    WORD  leg_step;        /* walking animation */
} Hero;

/* Input state */
typedef struct {
    WORD  dir;               /* direction input: DIR_* or DIR_NONE */
    WORD  start;             /* start button pressed */
    WORD  quit;              /* ESC pressed */
} InputState;

/* Game state */
typedef struct {
    UBYTE maze[MAZE_H][MAZE_W]; /* current maze state */
    Hero hero;
    Spider  spiders[NUM_SPIDERS];

    LONG   score;
    LONG   hiscore;
    LONG   score_p2;         /* player 2 score */
    WORD   lives;
    WORD   level;
    WORD   state;
    WORD   state_timer;
    WORD   frame;

    /* Dot tracking */
    WORD   dots_total;
    WORD   dots_eaten;
    WORD   dots_eaten_global; /* for spider house dot counter */

    /* Scatter/Chase mode */
    WORD   mode;             /* 0=scatter, 1=chase */
    LONG   mode_timer;
    WORD   mode_phase;       /* which scatter/chase phase we're in */

    /* Fright mode */
    WORD   fright_active;
    WORD   fright_timer;
    WORD   spiders_eaten;     /* count during current fright for scoring */

    /* Bonus */
    WORD   bonus_active;
    WORD   bonus_timer;
    WORD   bonus_type;
    WORD   bonus_x, bonus_y; /* pixel position */

    /* Score popup */
    WORD   popup_active;
    WORD   popup_timer;
    WORD   popup_x, popup_y;
    LONG   popup_score;

    /* Multiplayer */
    WORD   num_players;      /* 1 or 2 */
    WORD   current_player;   /* 0 or 1 */

    /* Extra life tracking */
    WORD   extra_life_given;

    /* Sound events */
    WORD   ev_flags;

    /* Pulse speed (increases as dots eaten) */
    WORD   pulse_speed;

    /* Title screen */
    WORD   title_blink;
} GameState;

/* Functions */
void game_init(GameState *gs);
void game_start_level(GameState *gs);
void game_start_life(GameState *gs);
void game_update(GameState *gs, InputState *input);

/* Maze data (defined in game.c) */
extern const UBYTE maze_template[MAZE_H][MAZE_W];

/* Corner each spider retreats to between hunts */
static const WORD scatter_target[NUM_SPIDERS][2] = {
    { 25, -3 },  /* hunter: top-right */
    {  2, -3 },  /* ambusher: top-left */
    { 27, 34 },  /* flanker: bottom-right */
    {  0, 34 },  /* shy one: bottom-left */
};

#endif
