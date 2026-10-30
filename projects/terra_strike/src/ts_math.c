// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ts_math.c - fixed-point trig, world wrapping, camera transform,
 * projection and polygon clipping for Terra Strike.
 */
#include "ts.h"
#ifdef AMIGA3DO                         /* the 3DO: no int64_t; the same results in 32-bit pieces */
#include "a3d_i64.h"
#define MULDIV64(a, b, c) a3_muldiv((a), (b), (c))
#else
#define MULDIV64(a, b, c) ((int32_t)(((int64_t)(a) * (b)) / (c)))
#endif

/* sin(i * 2pi / 256) * 65536 */
const int32_t ts_sin_tab[256] = {
        0,   1608,   3216,   4821,   6424,   8022,   9616,  11204,
    12785,  14359,  15924,  17479,  19024,  20557,  22078,  23586,
    25080,  26558,  28020,  29466,  30893,  32303,  33692,  35062,
    36410,  37736,  39040,  40320,  41576,  42806,  44011,  45190,
    46341,  47464,  48559,  49624,  50660,  51665,  52639,  53581,
    54491,  55368,  56212,  57022,  57798,  58538,  59244,  59914,
    60547,  61145,  61705,  62228,  62714,  63162,  63572,  63944,
    64277,  64571,  64827,  65043,  65220,  65358,  65457,  65516,
    65536,  65516,  65457,  65358,  65220,  65043,  64827,  64571,
    64277,  63944,  63572,  63162,  62714,  62228,  61705,  61145,
    60547,  59914,  59244,  58538,  57798,  57022,  56212,  55368,
    54491,  53581,  52639,  51665,  50660,  49624,  48559,  47464,
    46341,  45190,  44011,  42806,  41576,  40320,  39040,  37736,
    36410,  35062,  33692,  32303,  30893,  29466,  28020,  26558,
    25080,  23586,  22078,  20557,  19024,  17479,  15924,  14359,
    12785,  11204,   9616,   8022,   6424,   4821,   3216,   1608,
        0,  -1608,  -3216,  -4821,  -6424,  -8022,  -9616, -11204,
   -12785, -14359, -15924, -17479, -19024, -20557, -22078, -23586,
   -25080, -26558, -28020, -29466, -30893, -32303, -33692, -35062,
   -36410, -37736, -39040, -40320, -41576, -42806, -44011, -45190,
   -46341, -47464, -48559, -49624, -50660, -51665, -52639, -53581,
   -54491, -55368, -56212, -57022, -57798, -58538, -59244, -59914,
   -60547, -61145, -61705, -62228, -62714, -63162, -63572, -63944,
   -64277, -64571, -64827, -65043, -65220, -65358, -65457, -65516,
   -65536, -65516, -65457, -65358, -65220, -65043, -64827, -64571,
   -64277, -63944, -63572, -63162, -62714, -62228, -61705, -61145,
   -60547, -59914, -59244, -58538, -57798, -57022, -56212, -55368,
   -54491, -53581, -52639, -51665, -50660, -49624, -48559, -47464,
   -46341, -45190, -44011, -42806, -41576, -40320, -39040, -37736,
   -36410, -35062, -33692, -32303, -30893, -29466, -28020, -26558,
   -25080, -23586, -22078, -20557, -19024, -17479, -15924, -14359,
   -12785, -11204,  -9616,  -8022,  -6424,  -4821,  -3216,  -1608
};

/* atan(t/32) in 1/256ths of a turn, t = 0..32 */
static const unsigned char atan_tab[33] = {
    0,1,3,4,5,6,8,9,10,11,12,13,15,16,17,18,19,20,21,22,23,24,25,25,
    26,27,28,29,29,30,31,31,32
};

int32_t ts_wrap(int32_t a)
{
    return a & WORLD_MASK;
}

int32_t ts_wrap_delta(int32_t a, int32_t b)
{
    int32_t d = (a - b) & WORLD_MASK;
    if (d >= WORLD_SIZE / 2) d -= WORLD_SIZE;
    return d;
}

uint32_t ts_rand(uint32_t *state)
{
    *state = *state * 1103515245UL + 12345UL;
    return (*state >> 16) & 0x7FFF;
}

int ts_atan2(int32_t dx, int32_t dz)
{
    int32_t ax = dx < 0 ? -dx : dx;
    int32_t az = dz < 0 ? -dz : dz;
    int t, a;

    if (ax == 0 && az == 0) return 0;
    if (ax <= az) {
        t = atan_tab[(ax * 32) / az];
        if (dz >= 0) a = dx >= 0 ? t : -t;
        else         a = dx >= 0 ? 128 - t : 128 + t;
    } else {
        t = atan_tab[(az * 32) / ax];
        if (dx >= 0) a = dz >= 0 ? 64 - t : 64 + t;
        else         a = dz >= 0 ? 192 + t : 192 - t;
    }
    return a & 255;
}

void cam_setup(Cam *c)
{
    c->cy = TS_COS(c->yaw);
    c->sy = TS_SIN(c->yaw);
    c->cp = TS_COS(c->pitch);
    c->sp = TS_SIN(c->pitch);
}

/* World point -> camera space. Deltas wrap, so the camera always sees the
 * nearest copy of the tiled world. Products stay below 2^31: |delta| is at
 * most 8192 and the trig values at most 65536. */
void cam_xform(const Cam *c, int32_t wx, int32_t wy, int32_t wz, V3 *out)
{
    int32_t dx = ts_wrap_delta(wx, c->x);
    int32_t dz = ts_wrap_delta(wz, c->z);
    int32_t dy = wy - c->y;
    int32_t rx, rz;

    rx = (dx * (c->cy >> 4) - dz * (c->sy >> 4)) >> 12;
    rz = (dx * (c->sy >> 4) + dz * (c->cy >> 4)) >> 12;
    out->x = rx;
    out->y = (dy * (c->cp >> 4) + rz * (c->sp >> 4)) >> 12;
    out->z = (rz * (c->cp >> 4) - dy * (c->sp >> 4)) >> 12;
}

void cam_project(const V3 *p, int32_t *sx, int32_t *sy)
{
    *sx = VIEW_CX + (p->x * FOV) / p->z;
    *sy = VIEW_CY - (p->y * FOV) / p->z;
}

int clip_near(V3 *in, int n, V3 *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        V3 *a = &in[i], *b = &in[(i + 1) % n];
        int ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
        if (ain && m < MAX_PV) out[m++] = *a;
        if (ain != bin && m < MAX_PV) {
            /* interpolate from the inside end, so a shared edge clips to the
             * same point in both polygons whichever way they wind it */
            V3 *p = ain ? a : b, *q = ain ? b : a;
            int32_t dz = q->z - p->z;
            int32_t t = NEAR_Z - p->z;
            out[m].x = p->x + MULDIV64(q->x - p->x, t, dz);
            out[m].y = p->y + MULDIV64(q->y - p->y, t, dz);
            out[m].z = NEAR_Z;
            m++;
        }
    }
    return m;
}

/* One Sutherland-Hodgman pass against an axis-aligned edge.
 * axis 0 = x, 1 = y; keep values >= lim when ge, <= lim otherwise. */
static int clip_edge(int32_t *xs, int32_t *ys, int n, int32_t *ox, int32_t *oy,
                     int axis, int32_t lim, int ge)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int32_t av = axis ? ys[i] : xs[i];
        int32_t bv = axis ? ys[j] : xs[j];
        int ain = ge ? av >= lim : av <= lim;
        int bin = ge ? bv >= lim : bv <= lim;
        if (ain && m < MAX_PV) { ox[m] = xs[i]; oy[m] = ys[i]; m++; }
        if (ain != bin && m < MAX_PV) {
            /* interpolate from the inside end (see clip_near) */
            int p = ain ? i : j, q = ain ? j : i;
            int32_t pv = axis ? ys[p] : xs[p], qv = axis ? ys[q] : xs[q];
            int32_t t = lim - pv, d = qv - pv;     /* (int64_t on the Amiga; the products below are 64-bit either way) */
            if (axis) {
                ox[m] = xs[p] + MULDIV64(xs[q] - xs[p], t, d);
                oy[m] = lim;
            } else {
                ox[m] = lim;
                oy[m] = ys[p] + MULDIV64(ys[q] - ys[p], t, d);
            }
            m++;
        }
    }
    return m;
}

int clip_rect(int32_t *xs, int32_t *ys, int n, int32_t x0, int32_t y0,
              int32_t x1, int32_t y1)
{
    int32_t tx[MAX_PV], ty[MAX_PV];
    int i, inside = 1;

    for (i = 0; i < n; i++)
        if (xs[i] < x0 || xs[i] > x1 || ys[i] < y0 || ys[i] > y1) { inside = 0; break; }
    if (inside) return n;

    n = clip_edge(xs, ys, n, tx, ty, 0, x0, 1); if (n < 3) return 0;
    n = clip_edge(tx, ty, n, xs, ys, 0, x1, 0); if (n < 3) return 0;
    n = clip_edge(xs, ys, n, tx, ty, 1, y0, 1); if (n < 3) return 0;
    n = clip_edge(tx, ty, n, xs, ys, 1, y1, 0); if (n < 3) return 0;
    return n;
}

void radar_map(int32_t dx, int32_t dz, int yaw, int scale_shift, int *rx, int *ry)
{
    int32_t c = TS_COS(yaw) >> 4, s = TS_SIN(yaw) >> 4;
    int32_t x = (dx * c - dz * s) >> 12;
    int32_t z = (dx * s + dz * c) >> 12;
    *rx = (int)(x >> scale_shift);
    *ry = (int)(-(z >> scale_shift));
}

/* Scan-convert a convex polygon into per-row spans. Rows ymin..ymax get
 * xl[y - ymin]..xr[y - ymin] (inclusive). Returns the row count. Edges are
 * stepped in 16.16 fixed point; both end rows of each edge are included so
 * neighbouring polygons never leave a gap. */
int poly_spans(const int16_t *xs, const int16_t *ys, int n,
               int16_t *xl, int16_t *xr, int *ymin_out)
{
    int i, y, ymin = ys[0], ymax = ys[0], rows;

    for (i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    rows = ymax - ymin + 1;
    if (rows > SPAN_MAX) rows = SPAN_MAX;
    for (y = 0; y < rows; y++) { xl[y] = 32767; xr[y] = -32768; }

    for (i = 0; i < n; i++) {
        int x0 = xs[i], y0 = ys[i], x1 = xs[(i + 1) % n], y1 = ys[(i + 1) % n];
        int32_t x, dx;
        if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        x = (int32_t)x0 << 16;
        dx = y1 > y0 ? (((int32_t)(x1 - x0)) << 16) / (y1 - y0) : 0;
        for (y = y0; y <= y1; y++, x += dx) {
            int r = y - ymin, px = (int)((x + 0x8000) >> 16);
            if (y == y1) px = x1;
            if (r < 0 || r >= rows) continue;
            if (px < xl[r]) xl[r] = (int16_t)px;
            if (px > xr[r]) xr[r] = (int16_t)px;
        }
    }
    *ymin_out = ymin;
    return rows;
}
