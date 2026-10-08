/*
 * Rolling Steel - 3DO version of geekychris/rolling_steel (Unity, C#).
 *
 * Fixed point throughout (the ARM60 has no FPU):
 *   positions, velocities  Q12 (4096 = one unit, or one unit per second)
 *   directions             Q12 at run time (the course files store Q14)
 *   angles                 65536 per turn
 * World space is the Unity game's: the course yawed 45 degrees, +y up.
 * Physics steps at 50 Hz like Unity's FixedUpdate.
 *
 * No 3DO or Amiga headers here (cels.c includes the 3DO ones, main_3do.c
 * and sound.c the Amiga layer's).
 */
#ifndef RS_H
#define RS_H

#ifdef RS_HOST
typedef int w32;                    /* tools/sim.c on a 64-bit host */
typedef unsigned int uw32;
#else
typedef long w32;                   /* the course files are 32-bit words */
typedef unsigned long uw32;
#endif
typedef w32 fix;
#define FX       4096L
#define FXF(v)   ((fix)((v) * 4096.0))
#define STEP_HZ  50
typedef struct { fix x, y, z; } V3;

/* ---- the course (course.c), loaded from takeme/rolling_steel/courseN.bin,
 * written by tools/courses.py ---- */
typedef struct { w32 v[4]; uw32 col[3]; w32 flags; w32 nrm; } RQuad;   /* col: 6 fog levels, 2 per word; flags: group<<8 | kind;
                                                                    nrm: its normal, 3 x 10 bits (x<<20 | y<<10 | z, Q9) */
typedef struct { w32 v[3]; w32 n[3]; w32 flags; } CTri;                 /* flags: group<<8 | surface class */
typedef struct { fix cx, cy, cz, hx, hy, hz; w32 cs, sn; } OBB;          /* yaw-only box; cs/sn Q14 */
typedef struct { OBB box; w32 dir[3]; fix power; w32 kind; } Zone;     /* kind 0 fan, 1 boost; dir Q14 */
typedef struct { fix x, y, z, r, h; } Pillar;
typedef struct { fix x, y, z, len, speed, yaw; } Sweeper;                 /* speed deg/s, yaw deg (Q12) */
typedef struct { fix x, y, z, w, period, phase, lift; w32 cs, sn; } Crusher;
typedef struct { w32 kind; fix x, y, z, range, speed; w32 ax, az; } EnemySpec;   /* kind 0 chaser 1 blob */
typedef struct { fix x, y, z; w32 group; } CrumbleSpec;
typedef struct { fix x, y, z, safe, progress; } PathPt;                  /* progress Q16 */
typedef struct {
    w32 x0, z0, cell, nx, nz;
    w32 *off, *idx;
    w32 *sphere;                   /* render grid only: cx, cy, cz, r per cell */
} Grid;
typedef struct {
    fix x, y, z, scale, phase, amp, speed, spin, yaw;
    w32 nv, nq, ns;
    V3 *v;
    w32 *q;                        /* per quad: v0..v3, col[3] */
    w32 *s;                        /* per sphere: x, y, z, r, material */
} Decor;
#define MAXDECOR 16
#define MAXCRUMBLE 32

typedef struct {
    char name[33];
    w32 time_ms, gold_ms, silver_ms, bronze_ms, decor, music;
    V3 spawn;
    OBB goal;
    V3 goal_world;
    fix kill_y;
    w32 nv, nq, nt;
    V3 *v;
    RQuad *q;
    CTri *t;
    w32 *tsphere;                  /* per triangle: centre x, y, z and radius (made at load) */
    Grid cgrid, rgrid;
    w32 npath;
    PathPt *path;
    w32 nacid, nzone, npillar, nsweep, ncrush, nenemy, ncrumble, ndecor;
    OBB *acid;
    Zone *zone;
    Pillar *pillar;
    Sweeper *sweep;
    Crusher *crush;
    EnemySpec *enemy;
    CrumbleSpec *crumble;
    Decor deco[MAXDECOR];           /* the floating scenery */
    w32 *data;                     /* the loaded file */
} Course;
extern Course C;
int  course_load(int index);        /* 1..6; 0 on failure */
void course_free(void);
extern int course_count;

/* surface classes (collision) */
enum { S_DECK, S_ROUGH, S_ICE, S_RAIL, S_BOUNCY, S_COUNT };

/* ---- physics (phys.c) ---- */
typedef struct {
    V3 p, v, w;                     /* position, velocity, angular velocity (rad/s, Q12) */
    fix r, inv_mass;                /* radius; 1/mass (Q12) */
    fix fric, bounce, gravity;      /* its own PhysX material and pull per step */
    fix rot_k;                      /* 1 / (0.4 r): spin from a friction impulse, Q12 */
    int grounded, frozen, live;
    V3 last_ground;
    int touching_enemy;             /* for collision-enter shoves */
    int touch_sweeper, touch_tile;
    const char *kill;               /* what got it this step, if anything */
    int goal;                       /* touched the finish pad */
} Ball;
typedef struct {
    int state;                      /* 0 solid, 1 shivering, 2 gone */
    long timer;                     /* steps */
    fix fall;                       /* how far it has dropped (visual) */
} Crumble;
typedef struct { Ball b; V3 home; int kind; fix range, speed; long ax, az; fix phase; } Enemy;
#define MAXENEMY 8
typedef struct {
    Crumble crumble[MAXCRUMBLE];
    Enemy enemy[MAXENEMY];
    int nenemy;
    long sweep_angle[8];            /* 65536 per turn, 8 fraction bits */
    fix crush_h[8];                 /* 0..1 lift of each crusher (Q12) */
    fix crush_clock[8];
    int crush_slammed[8];
    long clock;
} World;
extern World Wd;
void phys_reset(void);              /* a course was loaded */
void phys_reset_enemies(void);
void phys_world_step(void);         /* obstacles, enemies */
/* one marble: input is a world-space wish direction (x, z; |wish| <= 4096) */
void phys_marble_step(Ball *b, fix wx, fix wz);
int  phys_ground_below(V3 p, fix *y);   /* the deck under a point, for its shadow */
int  phys_overlap_obb(const OBB *o, V3 p, fix r);
extern int phys_sound;              /* SFX_* the physics wants played, or -1 */
extern Ball *phys_marbles[2];       /* the marbles the world acts on */
extern int phys_nmarbles;
long isin(long a);                  /* Q14, a: 65536 per turn */
long icos(long a);
unsigned long isqrt(unsigned long v);
fix  vlen(V3 a);

/* ---- camera and drawing (render.c) ---- */
typedef struct {
    V3 focus;                       /* what it looks at */
    long yaw, pitch;                /* 65536 per turn */
    fix size;                       /* half height of the view, units */
    long r[3], u[3], f[3];          /* basis, Q12 */
    long scale;                     /* px per unit, Q8 */
    int x0, w;                      /* viewport (two players split the screen) */
} Cam;
/* sprite looks: the Unity materials of everything round */
enum { SP_MARBLE, SP_MARBLE2, SP_STEEL, SP_BLOB, SP_GHOST, SP_CRYSTAL, SP_CANDY, SP_GLOW, SP_SHADOW, SP_SPARK, SP_COUNT };
typedef struct { V3 p; fix r; int look; unsigned long pixc; int shadow; } RBall;   /* game-owned round things */
int  render_init(void);
void cam_basis(Cam *c);
void render_begin(void);
void render_scene(Cam *c, const RBall *b, int nb);
void render_flash(int r, int g, int b);      /* a coloured hit over everything, 0..31 per channel */
void render_shade(int x0, int y0, int x1, int y1, int k);   /* darken to k/8 */
void render_bg(void);
int  render_mark(void);                       /* split screen: cut the cel list */
void *render_segment(int from, int to);
void *render_end(void);
int  render_project(const Cam *c, V3 p, long *sx, long *sy);   /* 16.16 px; 0 if behind */
extern int render_stats_quads, render_stats_cels;

/* ---- sound (sound.c) ---- */
enum { SFX_START, SFX_GOAL, SFX_DEATH, SFX_CLACK, SFX_WARN, SFX_FALL, SFX_SIZZLE, SFX_CHOMP,
       SFX_SHATTER, SFX_THUD, SFX_COUNT };
int  snd_init(void);
void snd_play(int sfx);
void snd_music(int theme);          /* 0 title, 1..6 courses, -1 off */
void snd_music_enable(int on);
void snd_roll(fix speed01);         /* rolling noise: 0 silent .. 4096 flat out */

void rs_log(const char *fmt, ...);  /* main_3do.c (tools/sim.c on the host) */
void *rs_load(const char *name, long *size);
void *rs_alloc(long bytes);
void rs_free(void *p);
#endif
