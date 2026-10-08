/*
 * Spectral Keep - 3DO version of geekychris/spectral-keep (Unity, C#).
 *
 * World units: one floor tile = 1.0 = 4096 (Q12). Velocities are Q12 per
 * logic step (50 steps/s), so the Unity per-second constants appear here
 * divided by 50: walk 3.1 u/s = 254, jump 7.4 u/s = 606, gravity 22 u/s^2
 * = 36 per step per step.
 *
 * No 3DO or Amiga headers here: scene.c includes the 3DO ones, main_3do.c
 * the Amiga layer's.
 */
#ifndef KEEP_H
#define KEEP_H

typedef long fix;
#define FX        4096L
#define FXF(v)    ((fix)((v) * 4096.0))
#define MAXH      6                 /* RoomData.MaxHeight */
#define MAXW      10                /* RoomData.MaxSize */
#define TICKS(s)  ((long)((s) * 50.0 + 0.5))

/* ---- level data (levels.c, generated from the levels' JSON files) ---- */
enum { AT_GUARD, AT_HOUND, AT_GHOST, AT_BOUNCER, AT_SAGE };
typedef struct {
    signed char type, x, y, z, alongz;
    const char *text;
} KActor;
typedef struct {
    const char *id, *name;
    signed char gx, gy, w, d, ink;
    const char *seal;
    signed char wall, floor;        /* wall: 0 brick 1 rock; floor: 0 slab 1 wood 2 cobble */
    const char *cells;              /* [y][z][x], z = 0 the front row */
    const KActor *actors;
    int nactors;
} KRoom;
typedef struct {
    const char *name;
    int relics, start, sx, sy, sz;
    const KRoom *rooms;
    int nrooms;
} KLevel;
extern const KLevel keep_levels[];
extern const int keep_nlevels;

#define CELL(r, x, y, z) ((r)->cells[((y) * (r)->d + (z)) * (r)->w + (x)])
enum { SIDE_N, SIDE_E, SIDE_S, SIDE_W };
int  lv_room_at(const KLevel *l, int gx, int gy);              /* index or -1 */
int  lv_neighbour(const KLevel *l, int room, int side);
int  lv_has_door(const KLevel *l, int room, int side);
void door_span(int len, int *lo, int *hi);
int  lv_count(const KLevel *l, int c);

/* ---- physics (world.c, from Physics.cs) ---- */
typedef struct { fix x0, y0, z0, x1, y1, z1; } Box;
typedef struct Body {
    fix px, py, pz;                 /* centre of the bottom face */
    fix sx, sy, sz;                 /* size */
    fix vx, vy, vz;                 /* Q12 per step */
    unsigned char used, solid, dynamic, pushable, collides, player, npcbar, grounded;
    unsigned char harm;             /* a monster: it and the player don't block each other */
    struct Body *ground;
    int owner;                      /* actor index, -1 */
} Body;
#define MAXBODIES 64
extern Body w_body[MAXBODIES];
extern Body w_gridbody;             /* "standing on a block" */
void  w_reset(int w, int d);
void  w_set_block(int x, int y, int z);
int   w_block(int x, int y, int z);
Body *w_add_static(fix x0, fix y0, fix z0, fix x1, fix y1, fix z1);
Body *w_add(fix px, fix py, fix pz, fix sx, fix sy, fix sz);
void  w_remove(Body *b);
void  body_box(const Body *b, Box *o);
int   box_overlap(const Box *a, const Box *b, fix eps);
void  box_shrink(Box *b, int pct);                             /* scale about the centre */
void  box_grow(Box *b, fix g);
void  w_step(Body *b, int canpush, fix *moved);                 /* gravity + move */
void  w_move(Body *b, fix dx, fix dy, fix dz, int canpush, fix *moved);
void  w_move_kinematic(Body *lift, fix dy);
int   w_overlapping_solid(const Box *box, const Body *except);
fix   w_ground_below(fix x, fix y, fix z);                     /* top of what's under a point */

/* ---- rooms and actors (room.c, from RoomView.cs and Actors.cs) ---- */
enum { K_PLAYER, K_GUARD, K_HOUND, K_GHOST, K_BOUNCER, K_SAGE, K_CRATE };
typedef struct {
    int kind;
    Body *body;
    fix dx, dz;                     /* guard / bouncer heading, Q12 */
    int awake;
    long bob, last_talk;
    const char *text;
    int face;                       /* 0..7: 0 = +z, 2 = +x, 4 = -z, 6 = -x */
    unsigned int phase;             /* walk cycle, 256 = one stride pair */
    int roll;                       /* bouncer spin, 0..7 */
    long invuln;                    /* player: ticks */
    long coyote, jumpbuf;
    int was_grounded, visible;
} Actor;
typedef struct { Body *body; fix base; int dir; long wait; } Lift;
typedef struct { int kind, x, y, z, cell; Box box; } Pickup;
typedef struct { Body *body; int x, y, z, rot; } Gate;
#define MAXACT   24
#define MAXLIFT  6
#define MAXPICK  12
#define MAXHAZ   48
#define MAXGATE  6
typedef struct {
    const KLevel *level;
    const KRoom *room;
    int ridx, host;                 /* host = 0: title preview (no player, no pickups taken) */
    Actor act[MAXACT];
    int nact, player;               /* player: index or -1 */
    Lift lift[MAXLIFT];
    int nlift;
    Pickup pk[MAXPICK];
    int npk;
    Box haz[MAXHAZ];
    int nhaz;
    Box throne;
    int has_throne, tx, ty, tz;
    Gate gate[MAXGATE];
    int ngate;
    long clock;
    int exited;
} RoomState;
extern RoomState R;
void room_build(const KLevel *l, int ridx, int host);
void room_spawn_player(fix x, fix y, fix z);
void room_tick(fix mx, fix mz, int jump);    /* move: world direction, |m| <= 4096 */
void room_entry_point(const KRoom *from, const KRoom *to, int side, fix *x, fix *y, fix *z);

/* what a room tells the game (game.c) */
int  host_taken(int ridx, int cell);
int  host_gate_open(int ridx);
void host_pickup(int kind, int ridx, int cell);
int  host_try_key(int ridx);
void host_death(const char *cause);
void host_exit(int side, fix x, fix y, fix z);
void host_throne(void);
void host_talk(const char *text);
void host_sound(int sfx);

/* ---- the game (game.c, from Game.cs) ---- */
enum { GS_TITLE, GS_PLAYING, GS_DYING, GS_LEVELDONE, GS_GAMEOVER, GS_VICTORY };
#define INK_BLACK 0
#define INK_BLUE 1
#define INK_RED 2
#define INK_MAGENTA 3
#define INK_GREEN 4
#define INK_CYAN 5
#define INK_YELLOW 6
#define INK_WHITE 7
typedef struct {
    int state;
    long timer, clock;
    int selected, level_index;
    const KLevel *level;
    int lives, keys, relics;
    char message[96];
    int msg_ink;
    long msg_until, last_nag;
    unsigned char visited[16];
    int nvisited;
    int grid_controls, paused, music_on;
    fix entry_x, entry_y, entry_z;
    int flash_ink;
    long flash_until, flash_len;
    int room_changed;               /* the scene must rebuild */
    int trace;                      /* log the player's position (tests) */
    int tour, tour_level, tour_room;  /* the title's room tour (L+R) */
} Game;
extern Game G;
void game_init(void);
void game_step(unsigned long held, unsigned long pressed);
int  game_message_visible(void);
void keep_log(const char *fmt, ...);          /* main_3do.c */

/* ---- sound (sound.c) ---- */
enum { SFX_JUMP, SFX_LAND, SFX_PICKUP, SFX_RELIC, SFX_KEY, SFX_GATE, SFX_DEATH, SFX_DOOR,
       SFX_TALK, SFX_BARK, SFX_LEVELDONE, SFX_START, SFX_BLIP, SFX_COUNT };
enum { T_TITLE, T_GATEHOUSE, T_CRYPT, T_TOWER, T_NONE };
int  snd_init(void);
void snd_play(int sfx);
void snd_music(int track);          /* T_NONE stops */
void snd_duck(long ticks);
void snd_music_enable(int on);

/* ---- software voxel renderer (vox.c): Models.cs drawn into images ---- */
typedef struct {
    short w, h;                     /* w even */
    short ax, ay;                   /* where the model origin lands in the image */
    unsigned short *pix;            /* 3DO 15-bit RGB, 0 = transparent */
    void *ccb;                      /* scene.c's cel for it, made on first use */
} Sprite;
/* the iso projection, in pixels per unit: x -> (+14, -7), z -> (-14, -7), y -> (0, -17) */
#define ISO_X 14
#define ISO_Z 7
#define ISO_Y 17
enum { M_EXPLORER, M_GUARD, M_HOUND, M_GHOST, M_BOUNCER, M_SAGE, M_CRATE, M_SPIKES, M_GATE,
       M_LIFT, M_PISTON, M_RELIC, M_POTION, M_KEY, M_THRONE, M_POST, M_LINTEL, M_COUNT };
int  vox_init(void);
/* rendered at startup: characters [model][face 0..7][frame 0..2], pickups [face] */
extern Sprite spr_walker[3][8][3];  /* explorer, guard, hound */
extern Sprite spr_sage[8], spr_ghost[8], spr_bouncer[8], spr_crate, spr_spikes, spr_gate[2],
              spr_lift, spr_piston[9], spr_relic[16], spr_potion, spr_key[16], spr_throne,
              spr_shadow[3];
/* per room: blocks (stone '#' / brick 'B'), shade variants */
#define BLOCK_VARIANTS 3
extern Sprite spr_block[2][BLOCK_VARIANTS];
extern Sprite spr_arch[2];          /* front doorway arches: south, west */
void vox_room(const KRoom *r);      /* block and arch sprites for this room */
/* the room's floor tiles, wall bricks and door steps: a few shades each */
#define VARIANTS 4
extern Sprite spr_tile[VARIANTS], spr_brick[VARIANTS], spr_step[2];
int  tile_variant(int x, int z);
int  brick_variant(int along, int y, int east);
int  block_variant(int x, int y, int z);
unsigned short ink_rgb15(int ink, int bright);

#endif
