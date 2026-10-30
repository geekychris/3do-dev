// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ld_core.c - Lander 3D's simulation (see ld.h), from the browser's
 * lander-3d-core.js, in 16.16 fixed point.
 */
#include <string.h>
#include "a3d.h"
#include "a3d_fix.h"
#include "ld.h"
#ifdef AMIGA3DO
#include "a3d_i64.h"
#endif

/* ------------------------------------------------------------ maths */
static fix fclamp(fix v, fix lo, fix hi) { return v < lo ? lo : v > hi ? hi : v; }
static fix fabsx(fix v) { return v < 0 ? -v : v; }
static fix smooth(fix t) { return FMUL(FMUL(t, t), FX(3) - 2 * t); }

/* mulberry32, as the browser's rng(); ld_rf: [0, 1) as fix */
uint32_t ld_rng(uint32_t *a)
{
    uint32_t t;
    *a += 0x6D2B79F5UL;
    t = *a;
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + (t ^ (t >> 7)) * (t | 61);
    return t ^ (t >> 14);
}
static fix ld_rf(uint32_t *a) { return (fix)(ld_rng(a) >> 16); }

/* ------------------------------------------------------------ worlds */
const LdWorld ld_worlds[W_COUNT] = {
    /* name        g          amax      size  n   hills     rough      craters max wind     drag       curve       dunes  vs        hs        tilt (rad)    fuel */
    { "MOON",     FX(1.62), FX(4.4), 1200, 97, FX(38), FX(0.5),  34, 90, 0,       0,         0,          0,     FX(2.5), FX(1.5), FX(0.17453), FX(62) },
    { "MARS",     FX(3.71), FX(9.0), 1200, 97, FX(55), FX(0.45), 10, 110, FX(3),  FX(0.04),  0,          FX(9), FX(3.0), FX(1.8), FX(0.17453), FX(66) },
    { "ASTEROID", FX(0.35), FX(1.5), 640,  65, FX(26), FX(0.62), 26, 55, 0,       0,         FX(900),    0,     FX(1.5), FX(1.0), FX(0.20944), FX(85) },
};
const char *const ld_outcome_names[5] = { "flying", "landed", "offpad", "crashed", "lost" };

void ld_level_params(int world, int level, LevelParams *p)
{
    const LdWorld *W = &ld_worlds[world];
    int L = level < 1 ? 1 : level, k = L - 1 > 8 ? 8 : L - 1;
    fix f = W->fuel - FX(3.5) * k, f2 = FMUL(W->fuel, FX(0.55));
    p->fuel = f > f2 ? f : f2;
    p->start_alt = FROMI(world == W_ASTEROID ? 70 : 115) + FROMI(8) * k;
    p->start_h = world == W_ASTEROID ? FX(1.2) + FX(0.5) * k : FX(3) + FX(1.6) * k;
    p->start_dist = FROMI(world == W_ASTEROID ? 110 + 18 * k : 180 + 28 * k);
    p->pad_scale = FONE - FX(0.05) * k < FX(0.62) ? FX(0.62) : FONE - FX(0.05) * k;
    p->wind = W->wind ? (W->wind + FONE * k > FROMI(11) ? FROMI(11) : W->wind + FONE * k) : 0;
}

/* ------------------------------------------------------------ terrain */
#define MAX_CELLS 41
static fix lattice[4][(MAX_CELLS + 1) * (MAX_CELLS + 1)];
static const int oct_cells[4] = { 4, 9, 19, 41 };

static fix noise(int o, fix u, fix v)
{
    int cells = oct_cells[o], w = cells + 1, i, j;
    fix x = u * cells, y = v * cells, fx, fy, a, b, c, d;
    i = FINT(x); if (i > cells - 1) i = cells - 1;
    j = FINT(y); if (j > cells - 1) j = cells - 1;
    fx = smooth(x - FROMI(i));
    fy = smooth(y - FROMI(j));
    a = lattice[o][j * w + i]; b = lattice[o][j * w + i + 1];
    c = lattice[o][(j + 1) * w + i]; d = lattice[o][(j + 1) * w + i + 1];
    return a + FMUL(b - a, fx) + FMUL(c - a, fy) + FMUL(FMUL(a - b - c + d, fx), fy);
}

fix ld_height(const Terrain *T, fix x, fix z)
{
    int n = T->n, i, j;
    fix fx = fclamp(FMUL(x + T->half, T->inv_cell), 0, FROMI(n - 1) - 1);
    fix fz = fclamp(FMUL(z + T->half, T->inv_cell), 0, FROMI(n - 1) - 1);
    fix tx, tz, a, b, c, d;
    i = FINT(fx); j = FINT(fz);
    tx = fx & 0xFFFF; tz = fz & 0xFFFF;
    a = T->h[j * n + i]; b = T->h[j * n + i + 1]; c = T->h[(j + 1) * n + i]; d = T->h[(j + 1) * n + i + 1];
    return a + FMUL(b - a, tx) + FMUL(c - a, tz) + FMUL(FMUL(a - b - c + d, tx), tz);
}

fix ld_slope_cos(const Terrain *T, fix x, fix z)
{
    fix e = T->cell / 2;
    fix dx = ld_height(T, x + e, z) - ld_height(T, x - e, z), dz = ld_height(T, x, z + e) - ld_height(T, x, z - e);
    return FDIV(2 * e, fhypot3(dx, 2 * e, dz));
}

static fix roughness(const Terrain *T, fix x, fix z, fix rad)
{
    fix lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    int a, f;
    for (a = 0; a < 12; a++)
        for (f = 1; f <= 2; f++) {
            fix r = rad * f / 2;
            fix y = ld_height(T, x + FMUL(a3_cos(a * 65536 / 12), r), z + FMUL(a3_sin(a * 65536 / 12), r));
            if (y < lo) lo = y;
            if (y > hi) hi = y;
        }
    return hi - lo;
}

static void flatten(Terrain *T, Pad *p)
{
    int n = T->n, i, j, cnt = 0, i0, i1, j0, j1;
    fix inner = p->r + FMUL(T->cell, FX(1.5)), outer = inner + FMUL(T->cell, FX(2.5)) + FMUL(p->r, FX(0.6));
#ifdef AMIGA3DO                         /* the 3DO: no int64_t (a3d_i64.h) */
    a3_i64 sum = a3_i64_from(0);
#else
    int64_t sum = 0;
#endif
    i0 = FINT(FDIV(p->x - outer + T->half, T->cell)) - 1; i1 = FINT(FDIV(p->x + outer + T->half, T->cell)) + 2;
    j0 = FINT(FDIV(p->z - outer + T->half, T->cell)) - 1; j1 = FINT(FDIV(p->z + outer + T->half, T->cell)) + 2;
    if (i0 < 0) i0 = 0;
    if (j0 < 0) j0 = 0;
    if (i1 > n - 1) i1 = n - 1;
    if (j1 > n - 1) j1 = n - 1;
    for (j = j0; j <= j1; j++)
        for (i = i0; i <= i1; i++) {
            fix x = -T->half + T->cell * i, z = -T->half + T->cell * j;
#ifdef AMIGA3DO
            if (fhypot(x - p->x, z - p->z) <= inner) { sum = a3_i64_add(sum, a3_i64_from(T->h[j * n + i])); cnt++; }
#else
            if (fhypot(x - p->x, z - p->z) <= inner) { sum += T->h[j * n + i]; cnt++; }
#endif
        }
#ifdef AMIGA3DO
    p->y = cnt ? (fix)a3_i64_div(sum, cnt) : ld_height(T, p->x, p->z);
#else
    p->y = cnt ? (fix)(sum / cnt) : ld_height(T, p->x, p->z);
#endif
    for (j = j0; j <= j1; j++)
        for (i = i0; i <= i1; i++) {
            fix x = -T->half + T->cell * i, z = -T->half + T->cell * j, d = fhypot(x - p->x, z - p->z);
            if (d <= inner) T->h[j * n + i] = p->y;
            else if (d < outer) {
                fix t = smooth(FDIV(d - inner, outer - inner));
                T->h[j * n + i] = p->y + FMUL(T->h[j * n + i] - p->y, t);
            }
        }
}

static const struct { int mult; fix r; } mults[3] = { { 1, FX(17) }, { 2, FX(12) }, { 5, FX(8.5) } };

void ld_make_terrain(Terrain *T, int world, uint32_t seed, const LevelParams *params)
{
    const LdWorld *W = &ld_worlds[world];
    uint32_t r = seed ? seed : 0x9e3779b9UL;
    int n = W->n, i, j, o, c, k;
    fix amps[4], dune_dir, dune_k = FX(6.2831853 / 70);

    memset(T, 0, sizeof(*T));
    T->world = world;
    T->n = n;
    T->size = W->size;
    T->cell = FDIV(FROMI(W->size), FROMI(n - 1));
    T->inv_cell = FDIV(FONE, T->cell);
    T->half = FROMI(W->size / 2);
    for (o = 0; o < 4; o++) {
        int w = oct_cells[o] + 1;
        for (k = 0; k < w * w; k++) lattice[o][k] = ld_rf(&r) * 2 - FONE;
    }
    amps[0] = FONE;
    amps[1] = W->rough;
    amps[2] = FMUL(W->rough, W->rough);
    amps[3] = FMUL(FMUL(amps[2], W->rough), FX(0.8));
    dune_dir = FMUL(ld_rf(&r), FX(3.14159265));
    for (j = 0; j < n; j++)
        for (i = 0; i < n; i++) {
            fix u = FROMI(i) / (n - 1), v = FROMI(j) / (n - 1), y = 0;
            fix x = -T->half + T->cell * i, z = -T->half + T->cell * j;
            for (o = 0; o < 4; o++) y += FMUL(noise(o, u, v), amps[o]);
            y = FMUL(y, W->hills);
            if (W->dunes) {
                fix arg = FMUL(FMUL(x, fcos(dune_dir)) + FMUL(z, fsin(dune_dir)), dune_k) + 2 * noise(1, u, v);
                fix s = fabsx(fsin(arg));
                y += FMUL(W->dunes, FMUL(s, fsqrt(s)));          /* |sin|^1.5, near the browser's ^1.6 */
            }
            if (W->curve) { fix d = fhypot(x, z); y -= FMUL(d, FDIV(d, 2 * W->curve)); }
            T->h[j * n + i] = y;
        }
    /* craters: a bowl with a raised rim */
    for (c = 0; c < W->craters; c++) {
        fix rr = ld_rf(&r), R = FROMI(10) + FMUL(FMUL(rr, rr), FROMI(W->crater_max));
        fix cx = FMUL(ld_rf(&r) - FX(0.5), FMUL(FROMI(W->size), FX(0.95)));
        fix cz = FMUL(ld_rf(&r) - FX(0.5), FMUL(FROMI(W->size), FX(0.95)));
        fix depth = FMUL(R, FX(0.18) + FMUL(ld_rf(&r), FX(0.12))), rim = FMUL(R, FX(0.06) + FMUL(ld_rf(&r), FX(0.05)));
        fix reach = FMUL(R, FX(1.6));
        int i0 = FINT(FDIV(cx - reach + T->half, T->cell)), i1 = FINT(FDIV(cx + reach + T->half, T->cell)) + 1;
        int j0 = FINT(FDIV(cz - reach + T->half, T->cell)), j1 = FINT(FDIV(cz + reach + T->half, T->cell)) + 1;
        if (i0 < 0) i0 = 0;
        if (j0 < 0) j0 = 0;
        if (i1 > n - 1) i1 = n - 1;
        if (j1 > n - 1) j1 = n - 1;
        for (j = j0; j <= j1; j++)
            for (i = i0; i <= i1; i++) {
                fix x = -T->half + T->cell * i, z = -T->half + T->cell * j;
                fix d = FDIV(fhypot(x - cx, z - cz), R), dy = 0, q;
                if (d < FONE) dy -= FMUL(depth, FONE - FMUL(d, d));
                q = FDIV(d - FONE, FX(0.22));
                dy += FMUL(rim, fexp_neg(FMUL(q, q)));
                T->h[j * n + i] += dy;
            }
    }
    /* pads: the x1 on the smoothest ground, the x5 on the roughest that is
     * still well apart from the others */
    {
        struct { fix x, z, rough; } cands[80], t;
        fix margin = FMUL(FROMI(W->size), FX(0.12)), span = T->half - margin, min_sep = FMUL(FROMI(W->size), FX(0.26));
        int chosen[3], m, a, b;
        for (k = 0; k < 80; k++) {
            cands[k].x = FMUL(ld_rf(&r) * 2 - FONE, span);
            cands[k].z = FMUL(ld_rf(&r) * 2 - FONE, span);
            cands[k].rough = roughness(T, cands[k].x, cands[k].z, FROMI(30));
        }
        for (a = 1; a < 80; a++)                                  /* sort by roughness */
            for (b = a; b > 0 && cands[b - 1].rough > cands[b].rough; b--) { t = cands[b]; cands[b] = cands[b - 1]; cands[b - 1] = t; }
        for (m = 0; m < 3; m++) {
            int pick = -1;
            for (k = 0; k < 80 && pick < 0; k++) {
                int idx = m == 0 ? k : m == 1 ? 79 - k : 80 / 3 + k, ok = 1, q;
                if (idx >= 80) break;
                for (q = 0; q < m; q++)
                    if (fhypot(cands[chosen[q]].x - cands[idx].x, cands[chosen[q]].z - cands[idx].z) <= min_sep) ok = 0;
                if (ok) pick = idx;
            }
            if (pick < 0) pick = m == 0 ? m : m == 1 ? 79 - m : 80 / 3 + m;
            chosen[m] = pick;
        }
        for (m = 0; m < 3; m++) {
            T->pads[m].x = cands[chosen[m]].x;
            T->pads[m].z = cands[chosen[m]].z;
            T->pads[m].r = FMUL(mults[m].r, params->pad_scale);
            T->pads[m].mult = mults[m].mult;
            flatten(T, &T->pads[m]);
        }
    }
}

/* ------------------------------------------------------------ the craft */
static const fix FEET[4][3] = { { FX(2.3), FX(-2), FX(2.3) }, { FX(-2.3), FX(-2), FX(2.3) },
                                { FX(2.3), FX(-2), FX(-2.3) }, { FX(-2.3), FX(-2), FX(-2.3) } };
static const fix BELL[3] = { 0, FX(-1.5), 0 };

/* a body vector rotated by pitch (about x) after roll (about z) */
static void rot(fix pitch, fix roll, const fix v[3], fix out[3])
{
    fix cr = fcos(roll), sr = fsin(roll), cp = fcos(pitch), sp = fsin(pitch);
    fix x1 = FMUL(v[0], cr) - FMUL(v[1], sr), y1 = FMUL(v[0], sr) + FMUL(v[1], cr), z1 = v[2];
    out[0] = x1;
    out[1] = FMUL(y1, cp) - FMUL(z1, sp);
    out[2] = FMUL(y1, sp) + FMUL(z1, cp);
}

void ld_up(const Craft *c, fix u[3])
{
    static const fix up[3] = { 0, FONE, 0 };
    rot(c->pitch, c->roll, up, u);
}

fix ld_tilt(const Craft *c)
{
    fix u[3];
    ld_up(c, u);
    return fatan2(fhypot(u[0], u[2]), u[1]);
}

void ld_feet(const Craft *c, fix out[5][3])
{
    int k, d;
    for (k = 0; k < 4; k++) {
        rot(c->pitch, c->roll, FEET[k], out[k]);
        for (d = 0; d < 3; d++) out[k][d] += c->p[d];
    }
    rot(c->pitch, c->roll, BELL, out[4]);
    for (d = 0; d < 3; d++) out[4][d] += c->p[d];
}

fix ld_agl(const Level *L) { return L->c.p[1] - FX(2) - ld_height(&L->T, L->c.p[0], L->c.p[2]); }

static void push_ev(Level *L, int e) { if (L->nev < 8) L->events[L->nev++] = e; }
int ld_pop_event(Level *L)
{
    int e;
    if (!L->nev) return LDE_NONE;
    e = L->events[0];
    memmove(&L->events[0], &L->events[1], (L->nev - 1) * sizeof(int));
    L->nev--;
    return e;
}

static void wind_at(Level *L, fix t)
{
    fix s, a;
    if (!L->params.wind) { L->wind[0] = L->wind[1] = 0; return; }
    s = FMUL(L->params.wind, FX(0.65) + FMUL(FX(0.25), fsin(FMUL(t, FX(0.21)) + L->wind_ph)) +
             FMUL(FMUL(FX(0.18), fsin(FMUL(t, FX(1.3)) + 2 * L->wind_ph)), fsin(FMUL(t, FX(0.37)))));
    a = L->wind_dir + FMUL(FX(0.35), fsin(FMUL(t, FX(0.13)) + L->wind_ph));
    L->wind[0] = FMUL(fcos(a), s);
    L->wind[1] = FMUL(fsin(a), s);
}

static void finish(Level *L, int outcome)
{
    L->outcome = outcome;
    if (outcome != OUT_CRASHED && outcome != OUT_LOST) {
        L->c.v[0] = L->c.v[1] = L->c.v[2] = 0;
        L->c.wp = L->c.wr = 0;
    }
    L->c.throttle = 0;
    push_ev(L, outcome == OUT_LANDED ? LDE_LANDED : outcome == OUT_OFFPAD ? LDE_OFFPAD :
               outcome == OUT_CRASHED ? LDE_CRASHED : LDE_LOST);
}

/* touchdown: any foot or the bell at or below the ground decides it */
static int contact(Level *L)
{
    Craft *c = &L->c;
    const LdWorld *W = &ld_worlds[L->world];
    fix fs[5][3], pen = 0, vs, hs, tilt;
    int k, touching = 0, pad = -1;
    LdResult *res = &L->result;
    ld_feet(c, fs);
    for (k = 0; k < 5; k++) {
        fix g = ld_height(&L->T, fs[k][0], fs[k][2]);
        if (fs[k][1] <= g) { touching = 1; if (g - fs[k][1] > pen) pen = g - fs[k][1]; }
    }
    if (!touching) return 0;
    c->p[1] += pen;
    vs = -c->v[1];
    hs = fhypot(c->v[0], c->v[2]);
    tilt = ld_tilt(c);
    for (k = 0; k < 3 && pad < 0; k++) {
        const Pad *p = &L->T.pads[k];
        int f, in = fhypot(c->p[0] - p->x, c->p[2] - p->z) <= p->r - FX(1.2);
        for (f = 0; f < 4 && in; f++) if (fhypot(fs[f][0] - p->x, fs[f][2] - p->z) > p->r + FX(0.2)) in = 0;
        if (in) pad = k;
    }
    memset(res, 0, sizeof(*res));
    res->vs = vs; res->hs = hs; res->tilt = tilt;
    res->vs_ok = vs <= W->lim_vs;
    res->hs_ok = hs <= W->lim_hs;
    res->tilt_ok = tilt <= W->lim_tilt;
    res->pad = pad >= 0 ? L->T.pads[pad].mult : 0;
    res->slope = ld_slope_cos(&L->T, c->p[0], c->p[2]);
    if (res->vs_ok && res->hs_ok && res->tilt_ok && pad >= 0) {
        fix soft = 60 * (FONE - FDIV(vs, W->lim_vs)) + 30 * (FONE - FDIV(tilt, W->lim_tilt)) + 20 * (FONE - FDIV(hs, W->lim_hs));
        res->soft = (soft + FONE / 2) >> 16;
        res->fuel_bonus = (int32_t)((FMUL(FDIV(L->fuel, L->fuel_max), FROMI(150)) + FONE / 2) >> 16);
        res->score = (100 + res->soft + res->fuel_bonus) * res->pad;
        finish(L, OUT_LANDED);
    } else if (res->vs_ok && res->hs_ok && res->tilt_ok && res->slope >= FX(0.97030)) {   /* <= 14 degrees */
        res->score = 25;
        finish(L, OUT_OFFPAD);
    } else {
        finish(L, OUT_CRASHED);
    }
    return 1;
}

#define RATE      FX(1.1)
#define ALPHA     FX(2.4)
#define RCS_FUEL  FX(0.06)
#define MAX_TILT  FX(1.30900)           /* 75 degrees */

static fix axis(Level *L, fix w, fix cmd, int k)
{
    if (L->fuel <= 0) return w;
    if (L->assist) {
        fix err = FMUL(cmd, RATE) - w, a = fclamp(err * 6, -ALPHA, ALPHA);
        if (fabsx(a) > FX(0.2)) L->c.rcs[k] = a > 0 ? 1 : -1;
        return w + a / LD_STEP_HZ;
    }
    if (cmd) L->c.rcs[k] = cmd > 0 ? 1 : -1;
    return w + FMUL(cmd, ALPHA) / LD_STEP_HZ;
}

void ld_step(Level *L, const LdInput *in)
{
    Craft *c = &L->c;
    const LdWorld *W = &ld_worlds[L->world];
    fix thr, u[3], a[3], speed, burn;
    int subs, s;

    L->t++;
    if (L->outcome) return;
    thr = fclamp(in->throttle, 0, FONE);
    if (L->fuel <= 0) thr = 0;
    c->rcs[0] = c->rcs[1] = 0;
    c->wp = axis(L, c->wp, fclamp(in->pitch, -FONE, FONE), 0);
    c->wr = axis(L, c->wr, fclamp(in->roll, -FONE, FONE), 1);
    c->pitch = fclamp(c->pitch + c->wp / LD_STEP_HZ, -MAX_TILT, MAX_TILT);
    c->roll = fclamp(c->roll + c->wr / LD_STEP_HZ, -MAX_TILT, MAX_TILT);
    if (fabsx(c->pitch) >= MAX_TILT) c->wp = 0;
    if (fabsx(c->roll) >= MAX_TILT) c->wr = 0;
    burn = thr / LD_STEP_HZ + (fix)((c->rcs[0] != 0) + (c->rcs[1] != 0)) * RCS_FUEL / LD_STEP_HZ;
    if (L->fuel > 0) {
        L->fuel -= burn;
        if (L->fuel <= 0) { L->fuel = 0; push_ev(L, LDE_FUEL_OUT); }
    }
    c->throttle = thr;
    ld_up(c, u);
    a[0] = FMUL(FMUL(u[0], W->amax), thr);
    a[1] = FMUL(FMUL(u[1], W->amax), thr) - W->g;
    a[2] = FMUL(FMUL(u[2], W->amax), thr);
    wind_at(L, (fix)L->t * LD_DT);
    if (W->drag) {
        a[0] += FMUL(W->drag, L->wind[0] - c->v[0]);
        a[2] += FMUL(W->drag, L->wind[1] - c->v[2]);
        a[1] -= FMUL(FMUL(W->drag, FX(0.3)), c->v[1]);
    }
    /* sub-steps keep the touchdown check honest at any speed */
    speed = fhypot3(c->v[0], c->v[1], c->v[2]) + W->amax / LD_STEP_HZ;
    subs = (int)((speed / LD_STEP_HZ + FX(0.25) - 1) / FX(0.25));
    if (subs < 1) subs = 1;
    if (subs > 64) subs = 64;
    for (s = 0; s < subs; s++) {
        int d;
        for (d = 0; d < 3; d++) c->v[d] += a[d] / (LD_STEP_HZ * subs);
        for (d = 0; d < 3; d++) c->p[d] += c->v[d] / (LD_STEP_HZ * subs);
        if (contact(L)) return;
    }
    if (fabsx(c->p[0]) > FMUL(L->T.half, FX(1.15)) || fabsx(c->p[2]) > FMUL(L->T.half, FX(1.15)) || c->p[1] > FROMI(6000))
        finish(L, OUT_LOST);
}

int ld_nearest_pad(const Level *L, int easiest)
{
    int k, pick = 0;
    fix best = 0x7FFFFFFF;
    for (k = 0; k < 3; k++) {
        const Pad *p = &L->T.pads[k];
        fix d = fhypot(p->x - L->c.p[0], p->z - L->c.p[2]), s = easiest ? FDIV(d, p->r) : d;
        if (s < best) { best = s; pick = k; }
    }
    return pick;
}

/* ------------------------------------------------------------ the autopilot */
void ld_autopilot(Level *L, LdInput *out)
{
    Craft *c = &L->c;
    const LdWorld *W = &ld_worlds[L->world];
    const Terrain *T = &L->T;
    const Pad *pad;
    fix dx, dz, dist, h, hi = -0x7FFFFFFF, clearance, vmax, aH, vh, vdes[2], kh, ax, ay, az, vy_des, amag, ux, uy, uz, maxT;
    fix roll_t, pitch_t;
    int k;

    if (L->ap_target < 0) L->ap_target = ld_nearest_pad(L, 1);
    pad = &T->pads[L->ap_target];
    dx = pad->x - c->p[0];
    dz = pad->z - c->p[2];
    dist = fhypot(dx, dz);
    h = c->p[1] - FX(2) - pad->y;
    for (k = 0; k <= 8; k++) {
        fix lim = dist > FROMI(140) ? FDIV(FROMI(140), dist) : FONE;
        fix t = FMUL(FROMI(k) / 8, lim);
        fix g = ld_height(T, c->p[0] + FMUL(dx, t), c->p[2] + FMUL(dz, t));
        if (g > hi) hi = g;
    }
    clearance = c->p[1] - FX(2) - hi;
    vmax = FMUL(W->amax, FX(3.2));
    if (FROMI(4) + FMUL(dist, FX(0.14)) < vmax) vmax = FROMI(4) + FMUL(dist, FX(0.14));
    aH = FMUL(W->amax, FX(0.40674));                 /* sin 24 degrees */
    vh = vmax;
    if (FMUL(dist, FX(0.28)) < vh) vh = FMUL(dist, FX(0.28));
    if (fsqrt(FMUL(FMUL(FX(0.6), aH), dist)) < vh) vh = fsqrt(FMUL(FMUL(FX(0.6), aH), dist));
    if (dist > FX(0.01)) { vdes[0] = FMUL(FDIV(dx, dist), vh); vdes[1] = FMUL(FDIV(dz, dist), vh); }
    else vdes[0] = vdes[1] = 0;
    if (L->ap_down) {
        fix cap = FMUL(W->lim_hs, FX(0.35)), m = fhypot(vdes[0], vdes[1]);
        if (m > cap) { vdes[0] = FMUL(vdes[0], FDIV(cap, m)); vdes[1] = FMUL(vdes[1], FDIV(cap, m)); }
    }
    kh = L->ap_down ? FX(1.4) : FX(0.9);
    ax = FMUL(vdes[0] - c->v[0], kh);
    az = FMUL(vdes[1] - c->v[2], kh);
    /* hold a cruise height while far; descend once over the pad (with
     * hysteresis, so gusts don't send it back up) */
    {
        fix six = FROMI(6), half_r = FMUL(pad->r, FX(0.45));
        if (dist < (six < half_r ? six : half_r)) L->ap_down = 1;
        else if (dist > FMUL(pad->r, FX(0.85))) L->ap_down = 0;
    }
    if (!L->ap_down) {
        fix cruise = dist < FROMI(60) ? FROMI(14) : (FMUL(dist, FX(0.08)) > FROMI(25) ? FMUL(dist, FX(0.08)) : FROMI(25));
        vy_des = fclamp(FMUL(cruise - clearance, FX(0.35)), FROMI(-4), FROMI(6));
    } else {
        fix hi_lim = FMUL(W->lim_vs, FX(0.55)) + FMUL(h, FX(0.05));
        vy_des = -fclamp(FMUL(h, FX(0.12)) + FX(0.6), FX(0.6), hi_lim);
        if (h < FROMI(6)) {
            fix v1 = FMUL(W->lim_vs, FX(0.45)), v2 = FX(0.4) + FMUL(h, FX(0.15));
            vy_des = -(v1 < v2 ? v1 : v2);
        }
    }
    if (L->ap_down && h < FROMI(8) && fhypot(c->v[0], c->v[2]) > FMUL(W->lim_hs, FX(0.5)) && vy_des < 0) vy_des = 0;
    if (L->ap_down && h < FROMI(10) && dist > FMUL(pad->r, FX(0.5)) && vy_des < 0) vy_des = 0;
    ay = FMUL(vy_des - c->v[1], FX(1.6)) + W->g;
    amag = fhypot3(ax, ay, az);
    if (amag < 1) amag = 1;
    ux = FDIV(ax, amag); uy = FDIV(ay, amag); uz = FDIV(az, amag);
    maxT = h < FX(1.5) ? FX(0.05236) : h < FROMI(6) ? (FX(0.15708) < W->lim_tilt - FX(0.01745) ? FX(0.15708) : W->lim_tilt - FX(0.01745))
                                     : FX(0.48869);     /* 3, 9 (or the limit - 1), 28 degrees */
    if (uy < fcos(maxT)) {
        fix hh = fhypot(ux, uz), s = fsin(maxT);
        if (hh < 1) hh = 1;
        ux = FMUL(FDIV(ux, hh), s); uz = FMUL(FDIV(uz, hh), s); uy = fcos(maxT);
    }
    {
        fix den = FMUL(W->amax, uy);
        out->throttle = den > 0 ? fclamp(FDIV(ay > 0 ? ay : 0, den), 0, FONE) : FONE;
    }
    /* the attitude that points the thrust that way: u = [-sin r, cos r cos p, cos r sin p] */
    roll_t = fasin(fclamp(-ux, -FONE, FONE));
    pitch_t = fatan2(uz, uy);
    out->pitch = fclamp(FMUL(pitch_t - c->pitch, FX(2.8)) - FMUL(c->wp, FX(0.4)), -FONE, FONE);
    out->roll = fclamp(FMUL(roll_t - c->roll, FX(2.8)) - FMUL(c->wr, FX(0.4)), -FONE, FONE);
}

/* ------------------------------------------------------------ levels */
static Level probe;                     /* the autopilot's test flight (big: not on the stack) */
#define PROBE_FUEL FROMI(10000)

void ld_probe_begin(const Level *L)
{
    memcpy(&probe, L, sizeof(probe));
    probe.fuel = probe.fuel_max = PROBE_FUEL;
}

int ld_probe_run(Level *L, int steps)
{
    LdInput in;
    while (steps-- > 0 && !probe.outcome && probe.t < LD_STEP_HZ * 400) {
        ld_autopilot(&probe, &in);
        ld_step(&probe, &in);
    }
    if (!probe.outcome && probe.t < LD_STEP_HZ * 400) return 0;
    L->probe_used = PROBE_FUEL - probe.fuel;
    L->probe_outcome = probe.outcome;
    if (probe.outcome == OUT_LANDED) {
        fix need = (FMUL(L->probe_used, FX(1.3)) + FONE - 1) & ~0xFFFF;
        if (need > L->fuel_max) {               /* top up by the difference */
            L->fuel += need - L->fuel_max;
            L->fuel_max = need;
        }
    }
    return 1;
}

void ld_new_level(Level *L, int world, int level, uint32_t seed, int do_probe)
{
    LevelParams *P = &L->params;
    uint32_t r = seed ^ 0x5bd1e995UL;
    fix sx = 0, sz = 0, best = -0x7FFFFFFF, dir, ground = -0x7FFFFFFF, y, lim;
    const Pad *p1;
    int t, k;

    memset(L, 0, sizeof(*L));
    L->world = world;
    L->level = level;
    L->seed = seed;
    L->assist = 1;
    L->ap_target = -1;
    ld_level_params(world, level, P);
    ld_make_terrain(&L->T, world, seed, P);
    p1 = &L->T.pads[0];
    lim = FMUL(L->T.half, FX(0.8));
    for (t = 0; t < 24; t++) {
        fix a = FMUL(ld_rf(&r), FX(6.2831853));
        fix x = p1->x + FMUL(fcos(a), P->start_dist), z = p1->z + FMUL(fsin(a), P->start_dist);
        fix score = ((fabsx(x) < lim && fabsx(z) < lim) ? FROMI(1000) : 0) - FMUL(fhypot(x, z), FX(0.1)) + ld_rf(&r);
        if (score > best) { best = score; sx = x; sz = z; }
    }
    sx = fclamp(sx, -FMUL(L->T.half, FX(0.85)), FMUL(L->T.half, FX(0.85)));
    sz = fclamp(sz, -FMUL(L->T.half, FX(0.85)), FMUL(L->T.half, FX(0.85)));
    dir = fatan2(p1->z - sz, p1->x - sx) + FMUL(ld_rf(&r) - FX(0.5), FX(1.2));
    for (k = 0; k <= 10; k++) {
        fix g = ld_height(&L->T, sx + (p1->x - sx) / 10 * k, sz + (p1->z - sz) / 10 * k);
        if (g > ground) ground = g;
    }
    y = ld_height(&L->T, sx, sz);
    if (ground > y) y = ground;
    L->c.p[0] = sx;
    L->c.p[1] = y + P->start_alt;
    L->c.p[2] = sz;
    L->c.v[0] = FMUL(fcos(dir), P->start_h);
    L->c.v[1] = FX(-1.5);
    L->c.v[2] = FMUL(fsin(dir), P->start_h);
    L->fuel = L->fuel_max = P->fuel;
    L->wind_dir = FMUL(ld_rf(&r), FX(6.2831853));
    L->wind_ph = FMUL(ld_rf(&r), FROMI(10));
    /* every level can be landed: fly it once with the autopilot and enough
     * fuel, and give the tank 30 % more than that flight used */
    if (do_probe) {
        ld_probe_begin(L);
        while (!ld_probe_run(L, 1000))
            ;
    }
}
