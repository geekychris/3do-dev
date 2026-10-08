/* game.c: GameDirector.cs, Player.cs, Progress.cs and Ghost.cs */
#ifndef GAME_H
#define GAME_H
#include "rs.h"

enum { ST_TITLE, ST_PLAYING, ST_CLEAR, ST_GAMEOVER, ST_WON };
enum { MEDAL_NONE, MEDAL_BRONZE, MEDAL_SILVER, MEDAL_GOLD };

typedef struct {
    Ball m;
    Cam cam;
    long yaw, pitch;                /* where the player wants the view (65536 per turn) */
    fix size;
    V3 cam_vel;
    int hold;                       /* camera holds still while you fall to your death */
    fix shake;
    long l_hold, r_hold;            /* steps the turn buttons have been held */
    int spinning;
    long time_left, course_time, run_time;   /* steps (1/50 s) */
    int deaths, wins, finished, out_of_time, dying, fall_whistle;
    long finish_time, dying_timer, last_warn;
    const char *death_reason;
    int flash;                      /* 0..64 */
    int flash_r, flash_g, flash_b;
    int demo_wp;
} Player;

typedef struct {
    int state, nplayers, want_players, level, title_select, started_at;
    long state_timer, clock;
    fix timescale, time_acc;
    Player p[2];
    int last_medal, last_was_best, last_winner;
    long last_course_time;
    long cine_t;                    /* title flyover position along the course, Q16 */
    long idle;                      /* steps without a button on the title */
    int demo;                       /* the autopilot is driving (attract mode) */
    int music_on, paused;
    long best[8], best_run, best_run_falls;   /* steps; 0 = none */
    int loaded;
} Game;
extern Game G;

void game_init(void);
void game_step(unsigned long held0, unsigned long pressed0, unsigned long held1, unsigned long pressed1);
int  game_draw(void);               /* build the frame's cels; returns players drawn */
extern int game_marks[3];           /* where each view's cels start (and the end) */
int  medal_for(long steps);
long course_progress(const Player *p);   /* Q16 */
extern int game_course_loaded;
#endif
