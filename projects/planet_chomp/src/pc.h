/*
 * Planet Chomp - 3DO version of geekychris/planet-chomp (Unity).
 *
 * Fixed point throughout (the ARM60 has no FPU):
 *   directions / basis vectors: Q14 (16384 = 1.0)
 *   world positions:            Q8  (256 = 1 world unit; planet radius 12)
 *   screen (display) positions: 16.16 pixels on the 320x240 display
 */
#ifndef PC_H
#define PC_H

typedef long  fix;
typedef struct { fix x, y, z; } V3;

#define ONE14   16384L
#define Q8(v)   ((fix)((v) * 256))

/* ---- maze (maze.c) ---- */
#define GRID_N      9                      /* cells per cube-face edge (odd) */
#define CELLS       (6 * GRID_N * GRID_N)  /* 486 */
#define PLANET_R    Q8(12)

extern int  mz_lat[CELLS][3];               /* integer cube lattice coords */
extern int  mz_face[CELLS][3];              /* face normal */
extern V3   mz_dir[CELLS];                  /* unit direction, Q14 */
extern int  mz_nb[CELLS][4];                /* neighbour by exit */
extern int  mz_rev[CELLS][4];               /* exit at the neighbour leading back */
extern int  mz_step[CELLS][4][3];           /* lattice step of each exit */
extern unsigned char mz_open[CELLS][4];     /* passage (1) or wall (0) */
extern V3   mz_tan[CELLS][4];               /* surface tangent toward the neighbour, Q14 */
extern int  mz_start, mz_nest;

void mz_build(unsigned long seed);
int  mz_degree(int c);
V3   mz_lattice_dir(int x, int y, int z);   /* any lattice point -> sphere dir, Q14 */

/* walls: each closed edge between two lattice corners */
#define MAX_WALLS 700
typedef struct {
    V3   v[8];        /* world Q8: bottom A-, A+, B+, B-, then the same on top */
    V3   mid;         /* midpoint direction, Q14 */
    V3   midp;        /* midpoint half way up, world Q8 */
    V3   side;        /* unit normal of the + side, Q14 */
} Wall;
extern Wall mz_wall[MAX_WALLS];
extern int  mz_nwalls;
void mz_build_walls(void);

/* ---- maths (maze.c) ---- */
unsigned long isqrt32(unsigned long v);
V3   v3_norm14(V3 a);
fix  v3_dot14(V3 a, V3 b);                  /* Q14 . Q14 -> Q14 */
V3   v3_cross14(V3 a, V3 b);
V3   v3_sub(V3 a, V3 b);
V3   v3_add(V3 a, V3 b);
V3   v3_scale14(V3 a, fix s);               /* a * s / 16384 */

/* ---- rendering (render.c) ---- */
typedef struct {
    V3 focus;        /* camera focus direction, Q14 */
    V3 up;           /* screen-up tangent, Q14 */
    fix height;      /* Q8 */
} Camera;
void rd_init(void);
void rd_frame(const Camera *cam, V3 player_dir, const unsigned char *crumb);
extern int rd_stats_walls, rd_stats_cels;

/* ---- cels (cels.c, 3DO headers only) ---- */
int   pc_cels_init(void);
void  pc_cels_begin(void);
void  pc_quad(long ax, long ay, long bx, long by, long cx, long cy, long dx, long dy,
              unsigned short rgb15);
void *pc_cels_list(void);
int   pc_cels_count(void);

#define RGB15(r, g, b) ((unsigned short)((((r) >> 3) << 10) | (((g) >> 3) << 5) | ((b) >> 3)))
#endif
