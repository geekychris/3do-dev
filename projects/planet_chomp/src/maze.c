/*
 * The sphere maze (SphereMaze.cs): a cube sphere of 6 x N x N cells on an
 * integer lattice of side 2N, so neighbour lookups are exact across cube
 * edges; carved with a recursive backtracker, braided (no dead ends), and
 * opened up with ~7% extra passages. Integer and fixed-point maths only.
 */
#include "pc.h"

int  mz_lat[CELLS][3];
int  mz_face[CELLS][3];
V3   mz_dir[CELLS];
int  mz_nb[CELLS][4];
int  mz_rev[CELLS][4];
int  mz_step[CELLS][4][3];
unsigned char mz_open[CELLS][4];
V3   mz_tan[CELLS][4];
int  mz_start, mz_nest, mz_keys[4];
fix  mz_len[CELLS][4];
fix  mz_cell_len;
Wall mz_wall[MAX_WALLS];
int  mz_nwalls;

#define L    (GRID_N + 1)                   /* lattice coords run -N..N */
static short cell_at[2 * L + 1][2 * L + 1][2 * L + 1];

/* ---- maths ---- */

unsigned long isqrt32(unsigned long v)
{
    unsigned long r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

V3 v3_norm14(V3 a)
{
    /* scale down big vectors first so the squares fit 32 bits */
    V3 r;
    unsigned long len;
    int sh = 0;
    while (a.x > 30000 || a.x < -30000 || a.y > 30000 || a.y < -30000 ||
           a.z > 30000 || a.z < -30000) {
        a.x >>= 1; a.y >>= 1; a.z >>= 1; sh++;
    }
    while (a.x < 4096 && a.x > -4096 && a.y < 4096 && a.y > -4096 &&
           a.z < 4096 && a.z > -4096 && (a.x | a.y | a.z) != 0 && sh > -8) {
        a.x <<= 1; a.y <<= 1; a.z <<= 1; sh--;
    }
    len = isqrt32((unsigned long)(a.x * a.x + a.y * a.y + a.z * a.z));
    if (len == 0) { r.x = 0; r.y = ONE14; r.z = 0; return r; }
    {
        /* one divide, not three: components are < 2^15 and len >= 2^12 */
        long inv = (1L << 28) / (long)len;              /* 2^28 / len */
        r.x = (a.x * inv) >> 14;
        r.y = (a.y * inv) >> 14;
        r.z = (a.z * inv) >> 14;
    }
    return r;
}

fix v3_dot14(V3 a, V3 b) { return (a.x * b.x + a.y * b.y + a.z * b.z) >> 14; }

V3 v3_cross14(V3 a, V3 b)
{
    V3 r;
    r.x = (a.y * b.z - a.z * b.y) >> 14;
    r.y = (a.z * b.x - a.x * b.z) >> 14;
    r.z = (a.x * b.y - a.y * b.x) >> 14;
    return r;
}

V3 v3_sub(V3 a, V3 b) { V3 r; r.x = a.x - b.x; r.y = a.y - b.y; r.z = a.z - b.z; return r; }
V3 v3_add(V3 a, V3 b) { V3 r; r.x = a.x + b.x; r.y = a.y + b.y; r.z = a.z + b.z; return r; }
V3 v3_scale14(V3 a, fix s) { V3 r; r.x = (a.x * s) >> 14; r.y = (a.y * s) >> 14; r.z = (a.z * s) >> 14; return r; }

/* Equal-area-ish cube-to-sphere map (Spherify), p = lattice / N in Q14 */
V3 mz_lattice_dir(int lx, int ly, int lz)
{
    fix x = ((fix)lx << 14) / GRID_N, y = ((fix)ly << 14) / GRID_N, z = ((fix)lz << 14) / GRID_N;
    fix x2 = (x * x) >> 14, y2 = (y * y) >> 14, z2 = (z * z) >> 14;
    fix tx = ONE14 - y2 / 2 - z2 / 2 + ((y2 * z2) >> 14) / 3;
    fix ty = ONE14 - z2 / 2 - x2 / 2 + ((z2 * x2) >> 14) / 3;
    fix tz = ONE14 - x2 / 2 - y2 / 2 + ((x2 * y2) >> 14) / 3;
    V3 r;
    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tz < 0) tz = 0;
    r.x = (x * (fix)isqrt32((unsigned long)tx << 14)) >> 14;
    r.y = (y * (fix)isqrt32((unsigned long)ty << 14)) >> 14;
    r.z = (z * (fix)isqrt32((unsigned long)tz << 14)) >> 14;
    return v3_norm14(r);
}

/* ---- grid ---- */

static const int axes[6][3] = { {1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1} };

static void face_axes(const int *n, int *a, int *b)
{
    a[0] = a[1] = a[2] = b[0] = b[1] = b[2] = 0;
    if (n[0]) { a[1] = 1; b[2] = 1; }        /* up, forward */
    else if (n[1]) { a[2] = 1; b[0] = 1; }   /* forward, right */
    else { a[0] = 1; b[1] = 1; }             /* right, up */
}

static int lookup(int x, int y, int z)
{
    if (x < -L || x > L || y < -L || y > L || z < -L || z > L) return -1;
    return cell_at[x + L][y + L][z + L];
}

static unsigned long rng;
static int rnd(int n) { rng = rng * 1103515245UL + 12345UL; return (int)((rng >> 16) % (unsigned long)n); }

int mz_degree(int c) { return mz_open[c][0] + mz_open[c][1] + mz_open[c][2] + mz_open[c][3]; }

static void set_open(int c, int k, int open)
{
    mz_open[c][k] = (unsigned char)open;
    mz_open[mz_nb[c][k]][mz_rev[c][k]] = (unsigned char)open;
}

static void build_grid(void)
{
    int f, i, j, c = 0, k, x, y, z;
    for (x = 0; x < 2 * L + 1; x++)
        for (y = 0; y < 2 * L + 1; y++)
            for (z = 0; z < 2 * L + 1; z++)
                cell_at[x][y][z] = -1;
    for (f = 0; f < 6; f++) {
        int a[3], b[3];
        face_axes(axes[f], a, b);
        for (i = 0; i < GRID_N; i++)
            for (j = 0; j < GRID_N; j++) {
                int s = -GRID_N + 1 + 2 * i, t = -GRID_N + 1 + 2 * j, d;
                for (d = 0; d < 3; d++) {
                    mz_lat[c][d] = axes[f][d] * GRID_N + a[d] * s + b[d] * t;
                    mz_face[c][d] = axes[f][d];
                }
                cell_at[mz_lat[c][0] + L][mz_lat[c][1] + L][mz_lat[c][2] + L] = (short)c;
                c++;
            }
    }
    for (c = 0; c < CELLS; c++)
        mz_dir[c] = mz_lattice_dir(mz_lat[c][0], mz_lat[c][1], mz_lat[c][2]);
    for (c = 0; c < CELLS; c++) {
        int a[3], b[3], steps[4][3], d;
        face_axes(mz_face[c], a, b);
        for (d = 0; d < 3; d++) {
            steps[0][d] = a[d]; steps[1][d] = b[d]; steps[2][d] = -a[d]; steps[3][d] = -b[d];
        }
        for (k = 0; k < 4; k++) {
            int q[3], dot;
            for (d = 0; d < 3; d++) q[d] = mz_lat[c][d] + 2 * steps[k][d];
            dot = q[0] * steps[k][0] + q[1] * steps[k][1] + q[2] * steps[k][2];
            if (dot > GRID_N || dot < -GRID_N)   /* off the face: wrap onto the next */
                for (d = 0; d < 3; d++) q[d] = mz_lat[c][d] + steps[k][d] - mz_face[c][d];
            mz_nb[c][k] = lookup(q[0], q[1], q[2]);
            for (d = 0; d < 3; d++) mz_step[c][k][d] = steps[k][d];
        }
    }
    for (c = 0; c < CELLS; c++)
        for (k = 0; k < 4; k++) {
            int nb = mz_nb[c][k], k2;
            V3 t;
            fix dd;
            mz_rev[c][k] = -1;
            for (k2 = 0; k2 < 4; k2++) if (mz_nb[nb][k2] == c) mz_rev[c][k] = k2;
            t = v3_sub(mz_dir[nb], mz_dir[c]);
            dd = v3_dot14(t, mz_dir[c]);
            t = v3_sub(t, v3_scale14(mz_dir[c], dd));
            mz_tan[c][k] = v3_norm14(t);
        }
    mz_start = lookup(0, GRID_N, 0);    /* north pole */
    mz_nest = lookup(0, -GRID_N, 0);    /* south pole */
    mz_keys[0] = lookup(GRID_N, 0, 0);  /* four keys round the equator */
    mz_keys[1] = lookup(0, 0, GRID_N);
    mz_keys[2] = lookup(-GRID_N, 0, 0);
    mz_keys[3] = lookup(0, 0, -GRID_N);
    {
        /* edge lengths: the chord, which for these short hops is within
         * 0.1% of the arc */
        long total = 0;
        for (c = 0; c < CELLS; c++)
            for (k = 0; k < 4; k++) {
                V3 d = v3_sub(mz_dir[mz_nb[c][k]], mz_dir[c]);
                fix chord = (fix)isqrt32((unsigned long)(d.x * d.x + d.y * d.y + d.z * d.z));  /* Q14 */
                mz_len[c][k] = (chord * PLANET_R) >> 14;
                total += mz_len[c][k];
            }
        mz_cell_len = total / (CELLS * 4);
    }
}

static void carve(void)
{
    static short stack[CELLS], order[CELLS];
    static unsigned char seen[CELLS];
    int sp = 0, c, k, i, n;
    for (c = 0; c < CELLS; c++) { seen[c] = 0; for (k = 0; k < 4; k++) mz_open[c][k] = 0; }
    stack[sp++] = (short)mz_start;
    seen[mz_start] = 1;
    while (sp > 0) {
        int choices[4];
        c = stack[sp - 1];
        n = 0;
        for (k = 0; k < 4; k++) if (!seen[mz_nb[c][k]]) choices[n++] = k;
        if (!n) { sp--; continue; }
        k = choices[rnd(n)];
        set_open(c, k, 1);
        seen[mz_nb[c][k]] = 1;
        stack[sp++] = (short)mz_nb[c][k];
    }
    /* braid: no dead ends (joining two dead ends fixes both) */
    for (i = 0; i < CELLS; i++) order[i] = (short)i;
    for (i = CELLS - 1; i > 0; i--) { int j = rnd(i + 1); short t = order[i]; order[i] = order[j]; order[j] = t; }
    for (i = 0; i < CELLS; i++) {
        int choices[4], pref = -1;
        c = order[i];
        if (mz_degree(c) != 1) continue;
        n = 0;
        for (k = 0; k < 4; k++) {
            if (mz_open[c][k]) continue;
            choices[n++] = k;
            if (mz_degree(mz_nb[c][k]) == 1) pref = k;
        }
        set_open(c, pref >= 0 ? pref : choices[rnd(n)], 1);
    }
    /* a few extra loops */
    for (c = 0; c < CELLS; c++)
        for (k = 0; k < 4; k++)
            if (!mz_open[c][k] && mz_nb[c][k] > c && rnd(100) < 7) set_open(c, k, 1);
    for (k = 0; k < 4; k++) { set_open(mz_start, k, 1); set_open(mz_nest, k, 1); }
}

void mz_bfs(int src, short *dist)
{
    static short q[CELLS];
    int head = 0, tail = 0, c, k;
    for (c = 0; c < CELLS; c++) dist[c] = 32767;
    dist[src] = 0;
    q[tail++] = (short)src;
    while (head < tail) {
        c = q[head++];
        for (k = 0; k < 4; k++) {
            int nb = mz_nb[c][k];
            if (!mz_open[c][k] || dist[nb] != 32767) continue;
            dist[nb] = (short)(dist[c] + 1);
            q[tail++] = (short)nb;
        }
    }
}

void mz_build(unsigned long seed)
{
    rng = seed;
    build_grid();
    carve();
}

/* each closed edge: a thin box from lattice corner A to B, standing up */
#define WALL_H     Q8(0.75)
#define WALL_HALFT Q8(0.13)

void mz_build_walls(void)
{
    int c, k, i;
    mz_nwalls = 0;
    for (c = 0; c < CELLS && mz_nwalls < MAX_WALLS; c++)
        for (k = 0; k < 4; k++) {
            int mid[3], e[3], *n = mz_face[c], *d = mz_step[c][k];
            V3 da, db, side, off;
            Wall *w;
            if (mz_open[c][k] || mz_nb[c][k] < c) continue;
            for (i = 0; i < 3; i++) mid[i] = mz_lat[c][i] + d[i];
            e[0] = n[1] * d[2] - n[2] * d[1];
            e[1] = n[2] * d[0] - n[0] * d[2];
            e[2] = n[0] * d[1] - n[1] * d[0];
            da = mz_lattice_dir(mid[0] - e[0], mid[1] - e[1], mid[2] - e[2]);
            db = mz_lattice_dir(mid[0] + e[0], mid[1] + e[1], mid[2] + e[2]);
            w = &mz_wall[mz_nwalls++];
            w->mid = v3_norm14(v3_add(da, db));
            w->midp = v3_scale14(w->mid, PLANET_R + WALL_H / 2);
            side = v3_norm14(v3_cross14(w->mid, v3_sub(db, da)));
            w->side = side;
            off = v3_scale14(side, WALL_HALFT);
            /* bottom: A-, A+, B+, B- ; top: same at R + H */
            {
                V3 pa = v3_scale14(da, PLANET_R), pb = v3_scale14(db, PLANET_R);
                V3 ta = v3_scale14(da, PLANET_R + WALL_H), tb = v3_scale14(db, PLANET_R + WALL_H);
                w->v[0] = v3_sub(pa, off); w->v[1] = v3_add(pa, off);
                w->v[2] = v3_add(pb, off); w->v[3] = v3_sub(pb, off);
                w->v[4] = v3_sub(ta, off); w->v[5] = v3_add(ta, off);
                w->v[6] = v3_add(tb, off); w->v[7] = v3_sub(tb, off);
            }
        }
}
