/* Planet Chomp game logic (from GameDirector.cs, Ghost.cs, Chomper.cs,
 * Mover.cs): one call per 50 Hz step. */
#ifndef GAME_H
#define GAME_H
#include "pc.h"

#define TICKS(s) ((long)((s) * 50))

enum { GS_TITLE, GS_READY, GS_PLAYING, GS_DYING, GS_CLEAR, GS_OVER };
enum { GM_NEST, GM_ACTIVE, GM_EATEN };
enum { GK_CHASER, GK_AMBUSHER, GK_FLANKER, GK_DRIFTER };

typedef struct {
    int  from, to, prev, exitk, moving;
    long t;                         /* Q16 along the edge */
    V3   heading;                   /* Q14 */
} Mover;

typedef struct {
    Mover mv;
    int   mode, fright, kind, slot;
    V3    scatter;
    long  release_at;               /* life ticks */
    unsigned long rng;
} Ghost;

typedef struct { V3 world; int pts; int age; } Popup;

typedef struct {
    int   state, paused;
    long  timer, life, clock;       /* ticks in state, in this life, total */
    long  score, hiscore;
    int   lives, level, extra_given;
    int   crumbs_left, crumbs_total;
    long  power_left, power_total;
    int   scatter, mode_index, combo;
    long  mode_timer;
    fix   player_speed, ghost_speed;   /* Q8 units per second */
    Mover player;
    int   powered;
    Ghost ghost[4];
    unsigned char crumb[CELLS], key[CELLS];
    short nest_dist[CELLS];
    Popup popup[8];
    Camera cam;
    int   overview;
    long  mouth;                    /* mouth phase, ticks of movement */
    int   demo;                     /* attract mode: the autopilot plays */
} Game;

extern Game g;
extern const char *ghost_name[4];

void game_init(void);
void game_step(unsigned long held, unsigned long pressed);
V3   mover_dir(const Mover *m);
int  mover_nearest(const Mover *m);
int  ghost_dangerous(const Ghost *gh);
V3   ghost_world(const Ghost *gh);  /* sprite centre, world Q8 */
V3   player_world(void);
#endif
