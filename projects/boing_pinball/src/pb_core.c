// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * pb_core.c - Boing Pinball's physics and rules (see pb.h), the browser's
 * boing-pinball-core.js in 16.16 fixed point. Differences: the tables are
 * data (pb_tables.c) with up to three flippers, two ramps, two spinners
 * and two drop-target banks; the ball looks up what is near it in a grid
 * rather than testing every wall; flippers only add to the substep count
 * while they move; time is counted in 1/120 s steps.
 */
#include <string.h>
#include "a3d.h"
#include "a3d_fix.h"
#include "pb.h"

#define FABS(a)   ((a) < 0 ? -(a) : (a))
#define FPI       205887                /* pi */
#define SLOW      FROMI(60)             /* below this a touch doesn't bounce or scrub */

const char *const pb_event_names[EV_COUNT] = {
    "wall", "sling", "drop", "bumper", "flipperHit", "flipperStop", "spinnerHit", "spin",
    "sensor", "rampEnter", "ramp", "rampFail", "saucer", "kick", "laneRest", "drain", "plunge",
    "score", "skill", "lanesDone", "bank", "bankReset", "lock", "multiball", "jackpot",
    "missionStart", "missionDone", "missionFail", "supernova", "save", "endBall", "gameOver",
    "tiltWarn", "tilt", "nudge", "outlane",
};

const MissionDef pb_missions[5] = {
    { "POP BUMPERS", 25, MI_BUMPER, 40000 },
    { "TARGET BANKS", 2, MI_BANK,   50000 },
    { "SPINS",       40, MI_SPIN,   45000 },
    { "RAMP SHOTS",   3, MI_RAMP,   60000 },
    { "LANE SETS",    2, MI_LANES,  55000 },
};

static fix fclamp(fix v, fix lo, fix hi) { return v < lo ? lo : v > hi ? hi : v; }
static int fsign(fix v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

/* ================================================================ collisions */

void pb_seg_init(Seg *s, fix ax, fix ay, fix bx, fix by, fix r, fix e, int kind)
{
    fix ex = bx - ax, ey = by - ay, grow = r + PB_R + FROMI(4);
    memset(s, 0, sizeof *s);
    s->ax = ax; s->ay = ay; s->bx = bx; s->by = by; s->r = r; s->e = e; s->kind = (int8_t)kind;
    s->len = fhypot(ex, ey);
    if (s->len > 0) { s->ux = FDIV(ex, s->len); s->uy = FDIV(ey, s->len); }
    s->bb[0] = (int16_t)FINT((ax < bx ? ax : bx) - grow);
    s->bb[1] = (int16_t)FINT((ay < by ? ay : by) - grow);
    s->bb[2] = (int16_t)FINT((ax > bx ? ax : bx) + grow) + 1;
    s->bb[3] = (int16_t)FINT((ay > by ? ay : by) + grow) + 1;
    s->up = 1;
}

static void bounce(Ball *b, fix nx, fix ny, fix vn, fix e, fix fric)
{
    fix tx = b->vx - FMUL(vn, nx), ty = b->vy - FMUL(vn, ny);
    int slow = -vn < SLOW;
    fix ee = slow ? 0 : e, k = slow ? FONE : FONE - fric;
    fix en = FMUL(ee, vn);
    b->vx = FMUL(tx, k) - FMUL(en, nx);
    b->vy = FMUL(ty, k) - FMUL(en, ny);
}

/* the unit vector of (dx, dy), length d (> 0) */
static void unit(fix dx, fix dy, fix d, fix *nx, fix *ny) { *nx = FDIV(dx, d); *ny = FDIV(dy, d); }

/* Circle (the ball) against a capsule segment: resolves the overlap and
 * returns the approach speed, or -1. */
fix pb_hit_segment(Ball *b, const Seg *s)
{
    fix dx = b->x - s->ax, dy = b->y - s->ay, t, qx, qy, rr = PB_R + s->r, d2, d, nx, ny, vn;
    t = fclamp(FMUL(dx, s->ux) + FMUL(dy, s->uy), 0, s->len);
    qx = s->ax + FMUL(s->ux, t);
    qy = s->ay + FMUL(s->uy, t);
    dx = b->x - qx; dy = b->y - qy;
    if (FABS(dx) >= rr || FABS(dy) >= rr) return -1;
    d2 = FMUL(dx, dx) + FMUL(dy, dy);
    if (d2 >= FMUL(rr, rr)) return -1;
    d = fsqrt(d2);
    if (d < 4) {                        /* dead centre: out on the side it came from */
        nx = -s->uy; ny = s->ux;
        if (FMUL(nx, b->vx) + FMUL(ny, b->vy) > 0) { nx = -nx; ny = -ny; }
    } else unit(dx, dy, d, &nx, &ny);
    vn = FMUL(b->vx, nx) + FMUL(b->vy, ny);
    if ((s->ow_x || s->ow_y) && (nx * s->ow_x + ny * s->ow_y <= 0 || vn >= 0)) return -1;
    b->x = qx + FMUL(nx, rr);
    b->y = qy + FMUL(ny, rr);
    if (vn < 0) bounce(b, nx, ny, vn, s->e, FX(0.03));
    return vn < 0 ? -vn : 0;
}

fix pb_hit_circle(Ball *b, const Circ *c, fix *pnx, fix *pny)
{
    fix dx = b->x - c->cx, dy = b->y - c->cy, rr = PB_R + c->r, d2, d, nx, ny, vn;
    if (FABS(dx) >= rr || FABS(dy) >= rr) return -1;
    d2 = FMUL(dx, dx) + FMUL(dy, dy);
    if (d2 >= FMUL(rr, rr)) return -1;
    d = fsqrt(d2);
    if (d < 4) { nx = 0; ny = FONE; } else unit(dx, dy, d, &nx, &ny);
    b->x = c->cx + FMUL(nx, rr);
    b->y = c->cy + FMUL(ny, rr);
    vn = FMUL(b->vx, nx) + FMUL(b->vy, ny);
    if (vn < 0) bounce(b, nx, ny, vn, c->e, FX(0.02));
    *pnx = nx; *pny = ny;
    return vn < 0 ? -vn : 0;
}

void pb_flipper_tip(const Flipper *f, fix *x, fix *y)
{
    *x = f->d.px + FMUL(f->d.len, fcos(f->ang));
    *y = f->d.py + FMUL(f->d.len, fsin(f->ang));
}

/* Ball against a (moving) flipper, a tapered capsule; the surface's
 * velocity at the contact is w x r. */
fix pb_hit_flipper(Ball *b, const Flipper *f)
{
    fix tx, ty, ex, ey, t, qx, qy, rad, dx, dy, rr, d2, d, nx, ny, cx, cy, vcx, vcy, rvx, rvy, vn;
    pb_flipper_tip(f, &tx, &ty);
    ex = tx - f->d.px; ey = ty - f->d.py;
    t = fclamp(FDIV(FMUL(b->x - f->d.px, ex) + FMUL(b->y - f->d.py, ey), FMUL(f->d.len, f->d.len)), 0, FONE);
    qx = f->d.px + FMUL(t, ex);
    qy = f->d.py + FMUL(t, ey);
    rad = f->d.r1 + FMUL(f->d.r2 - f->d.r1, t);
    dx = b->x - qx; dy = b->y - qy;
    rr = PB_R + rad;
    if (FABS(dx) >= rr || FABS(dy) >= rr) return -1;
    d2 = FMUL(dx, dx) + FMUL(dy, dy);
    if (d2 >= FMUL(rr, rr)) return -1;
    d = fsqrt(d2);
    if (d < 4) {                        /* dead centre: out on the side the flipper moves towards */
        nx = -FDIV(ey, f->d.len); ny = FDIV(ex, f->d.len);
        if (f->w < 0) { nx = -nx; ny = -ny; }
    } else unit(dx, dy, d, &nx, &ny);
    cx = qx + FMUL(nx, rad);
    cy = qy + FMUL(ny, rad);
    vcx = -FMUL(f->w, cy - f->d.py);    /* the contact point's velocity */
    vcy = FMUL(f->w, cx - f->d.px);
    b->x = qx + FMUL(nx, rr);
    b->y = qy + FMUL(ny, rr);
    rvx = b->vx - vcx; rvy = b->vy - vcy;
    vn = FMUL(rvx, nx) + FMUL(rvy, ny);
    if (vn < 0) {
        fix e = FABS(f->w) > FONE ? FX(0.55) : FX(0.3);
        int slow = -vn < SLOW;
        fix ee = slow ? 0 : e, k = slow ? FONE : FX(0.97), en = FMUL(ee, vn);
        fix sx = rvx - FMUL(vn, nx), sy = rvy - FMUL(vn, ny);
        b->vx = vcx + FMUL(sx, k) - FMUL(en, nx);
        b->vy = vcy + FMUL(sy, k) - FMUL(en, ny);
        return -vn;
    }
    return 0;
}

int pb_hit_balls(Ball *a, Ball *b)
{
    fix dx = b->x - a->x, dy = b->y - a->y, rr = 2 * PB_R, d2, d, nx, ny, pen, rel;
    if (FABS(dx) >= rr || FABS(dy) >= rr) return 0;
    d2 = FMUL(dx, dx) + FMUL(dy, dy);
    if (d2 >= FMUL(rr, rr) || d2 == 0) return 0;
    d = fsqrt(d2);
    if (d < 4) return 0;
    unit(dx, dy, d, &nx, &ny);
    pen = (rr - d) / 2;
    a->x -= FMUL(nx, pen); a->y -= FMUL(ny, pen);
    b->x += FMUL(nx, pen); b->y += FMUL(ny, pen);
    rel = FMUL(b->vx - a->vx, nx) + FMUL(b->vy - a->vy, ny);
    if (rel < 0) {
        fix j = FMUL(rel, FX(0.95));
        a->vx += FMUL(j, nx); a->vy += FMUL(j, ny);
        b->vx -= FMUL(j, nx); b->vy -= FMUL(j, ny);
    }
    return 1;
}

/* Did the move p0 -> p1 cross the sensor line? +1 / -1 (the side crossed
 * to), or 0. The sensors are short and the ball near, so the products fit. */
int pb_crossed(fix x0, fix y0, fix x1, fix y1, const Sensor *s)
{
    fix ex = s->bx - s->ax, ey = s->by - s->ay;
    fix d0 = FMUL(ex, y0 - s->ay) - FMUL(ey, x0 - s->ax);
    fix d1 = FMUL(ex, y1 - s->ay) - FMUL(ey, x1 - s->ax);
    fix t, px, py, u, l2;
    if ((d0 > 0) == (d1 > 0) || d0 == d1) return 0;
    t = FDIV(d0, d0 - d1);
    px = x0 + FMUL(x1 - x0, t);
    py = y0 + FMUL(y1 - y0, t);
    l2 = FMUL(ex, ex) + FMUL(ey, ey);
    u = FDIV(FMUL(px - s->ax, ex) + FMUL(py - s->ay, ey), l2);
    if (u < 0 || u > FONE) return 0;
    return d1 > 0 ? 1 : -1;
}

/* ================================================================ the ramps */

void pb_ramp_at(const Ramp *r, fix s, fix *x, fix *y, fix *z, fix *slope, fix *dx, fix *dy)
{
    fix L = r->prof[r->n - 1], t, seglen;
    const int16_t *a, *b;
    int i = 1;
    s = fclamp(s, 0, L);
    while (i < r->n - 1 && r->prof[i] < s) i++;
    seglen = r->prof[i] - r->prof[i - 1];
    if (seglen <= 0) seglen = FONE;
    t = FDIV(s - r->prof[i - 1], seglen);
    a = r->pts[i - 1]; b = r->pts[i];
    *x = FROMI(a[0]) + FMUL(FROMI(b[0] - a[0]), t);
    *y = FROMI(a[1]) + FMUL(FROMI(b[1] - a[1]), t);
    *z = FROMI(a[2]) + FMUL(FROMI(b[2] - a[2]), t);
    if (slope) *slope = FDIV(FROMI(b[2] - a[2]), seglen);
    if (dx) *dx = FDIV(FROMI(b[0] - a[0]), seglen);
    if (dy) *dy = FDIV(FROMI(b[1] - a[1]), seglen);
}

/* ================================================================ the world */

static void emit(World *w, int type, fix x, fix y, int id, int up, int32_t val, int ball)
{
    Event *e;
    if (w->nev >= PB_MAX_EV) return;
    e = &w->ev[w->nev++];
    e->type = (int8_t)type; e->id = (int8_t)id; e->kind = 0; e->up = (int8_t)up;
    e->x = (int16_t)FINT(x); e->y = (int16_t)FINT(y); e->val = val; e->ball = (int16_t)ball;
}

void pb_world_init(World *w, Table *t)
{
    int i;
    memset(w, 0, sizeof *w);
    w->t = t;
    w->nflip = t->nflip;
    for (i = 0; i < t->nflip; i++) { w->flip[i].d = t->flip[i]; w->flip[i].ang = t->flip[i].rest; }
    for (i = 0; i < t->nseg; i++) if (t->seg[i].kind == SK_DROP) t->seg[i].up = 1;
    w->next_id = 1;
    w->flipper_power = w->active_targets = 1;
    w->substeps = 1;
}

Ball *pb_add_ball(World *w, fix x, fix y, fix vx, fix vy, int state)
{
    Ball *b;
    if (w->nballs >= PB_MAX_BALLS) return NULL;
    b = &w->balls[w->nballs++];
    memset(b, 0, sizeof *b);
    b->id = w->next_id++;
    b->x = x; b->y = y; b->vx = vx; b->vy = vy; b->state = state; b->autol = -1;
    return b;
}

void pb_remove_drained(World *w)
{
    int i, n = 0;
    for (i = 0; i < w->nballs; i++) if (w->balls[i].state != BS_DRAINED) w->balls[n++] = w->balls[i];
    w->nballs = n;
}

static void flipper_step(World *w, Flipper *f, fix h)
{
    int want = f->held && w->flipper_power;
    fix lo = f->d.rest < f->d.up ? f->d.rest : f->d.up, hi = f->d.rest < f->d.up ? f->d.up : f->d.rest;
    fix target = want ? f->d.up : f->d.rest;
    int sgn = fsign(target - f->ang);
    fix wmax = want ? FROMI(42) : FROMI(26);
    int32_t acc = want ? 9000 : 5000;
    if (sgn == 0) f->w = 0;
    else {
        f->w += sgn * acc * h;
        if (FABS(f->w) > wmax) f->w = sgn * wmax;
        if (fsign(f->w) != sgn) { fix m = acc * h; f->w = sgn * (FABS(f->w) < m ? FABS(f->w) : m); }
    }
    f->ang += FMUL(f->w, h);
    if (f->ang <= lo) {
        f->ang = lo;
        if (f->w < 0) { if (FABS(f->w) > FROMI(8)) emit(w, EV_FLIPSTOP, f->d.px, f->d.py, f->d.side, f->ang == f->d.up, 0, 0); f->w = 0; }
    }
    if (f->ang >= hi) {
        f->ang = hi;
        if (f->w > 0) { if (FABS(f->w) > FROMI(8)) emit(w, EV_FLIPSTOP, f->d.px, f->d.py, f->d.side, f->ang == f->d.up, 0, 0); f->w = 0; }
    }
}

static int flipper_moving(const World *w, const Flipper *f)
{
    fix target = (f->held && w->flipper_power) ? f->d.up : f->d.rest;
    return f->w != 0 || f->ang != target;
}

static void move_ball(World *w, Ball *b, fix dt)
{
    Table *t = w->t;
    fix x0 = b->x, y0 = b->y, sp;
    int gx, gy, k, i;

    b->vy -= PB_GRAVITY * dt;
    sp = fhypot(b->vx, b->vy);
    if (sp > PB_MAX_SPEED) { fix s = FDIV(PB_MAX_SPEED, sp); b->vx = FMUL(b->vx, s); b->vy = FMUL(b->vy, s); }
    b->x += FMUL(b->vx, dt);
    b->y += FMUL(b->vy, dt);

    gx = FINT(b->x) >> PB_CELL_SH;
    gy = (FINT(b->y) + 64) >> PB_CELL_SH;
    if (gx < 0) gx = 0;
    if (gx >= PB_GX) gx = PB_GX - 1;
    if (gy < 0) gy = 0;
    if (gy >= PB_GY) gy = PB_GY - 1;
    {
        int bx = FINT(b->x), by = FINT(b->y);
        int n = t->ngrid[gy][gx];
        const uint16_t *cell = t->grid[gy][gx];
        for (k = 0; k < n; k++) {
            int type = cell[k] >> 12, idx = cell[k] & 0xFFF;
            if (type == 0) {                                    /* a wall, gate, sling or drop target */
                Seg *s = &t->seg[idx];
                fix imp;
                if (bx < s->bb[0] || bx > s->bb[2] || by < s->bb[1] || by > s->bb[3]) continue;
                if (s->kind == SK_DROP && !s->up) continue;
                w->tests++;
                imp = pb_hit_segment(b, s);
                if (imp <= 0) continue;
                if (s->kind == SK_DROP) {
                    if (imp > FROMI(90)) { s->up = 0; emit(w, EV_DROP, b->x, b->y, s->id, 0, FINT(imp), b->id); }
                } else if (s->kind == SK_SLING) {
                    if (imp > FROMI(140) && w->active_targets) {
                        fix nx = -s->uy, ny = s->ux;
                        if (FMUL(b->x - s->ax, nx) + FMUL(b->y - s->ay, ny) < 0) { nx = -nx; ny = -ny; }
                        b->vx += 950 * nx; b->vy += 950 * ny;
                        emit(w, EV_SLING, b->x, b->y, s->id, 0, 0, b->id);
                    }
                } else if (imp > FROMI(250)) emit(w, EV_WALL, b->x, b->y, 0, 0, FINT(imp), b->id);
            } else if (type == 1) {                             /* a post or a pop bumper */
                Circ *c = &t->circ[idx];
                fix nx, ny, imp;
                w->tests++;
                imp = pb_hit_circle(b, c, &nx, &ny);
                if (imp < 0) continue;
                if (c->bumper) {
                    if (w->active_targets) {
                        fix out = FMUL(b->vx, nx) + FMUL(b->vy, ny);
                        if (out < FROMI(1050)) { fix add = FROMI(1050) - out; b->vx += FMUL(nx, add); b->vy += FMUL(ny, add); }
                        emit(w, EV_BUMPER, c->cx, c->cy, c->id, 0, 0, b->id);
                    }
                } else if (imp > FROMI(250)) emit(w, EV_WALL, b->x, b->y, 0, 0, FINT(imp), b->id);
            } else {                                            /* a sensor */
                Sensor *s = &t->sens[idx];
                int c;
                if (bx < s->bb[0] || bx > s->bb[2] || by < s->bb[1] || by > s->bb[3]) continue;
                c = pb_crossed(x0, y0, b->x, b->y, s);
                if (!c) continue;
                if (s->kind == SN_SPIN) {
                    Spinner *sp2 = &w->spin[s->idx];
                    fix add = FABS(b->vy) / 28;
                    sp2->w += add < FROMI(70) ? add : FROMI(70);
                    emit(w, EV_SPINHIT, b->x, b->y, s->idx, b->vy > 0, 0, b->id);
                    continue;
                }
                if (s->kind == SN_RAMPIN) {
                    if (b->vy > 0) {
                        b->state = BS_RAMP; b->ramp = s->idx; b->rs = 0;
                        b->rv = FMUL(fhypot(b->vx, b->vy), FX(0.92));
                        emit(w, EV_RAMPENTER, b->x, b->y, s->idx, 1, 0, b->id);
                        return;
                    }
                    continue;
                }
                if (w->nev < PB_MAX_EV) {
                    emit(w, EV_SENSOR, b->x, b->y, s->idx, b->vy > 0, 0, b->id);
                    w->ev[w->nev - 1].kind = s->kind;
                }
            }
        }
    }
    /* the flippers */
    for (i = 0; i < w->nflip; i++) {
        Flipper *f = &w->flip[i];
        fix imp, reach = f->d.len + f->d.r1 + PB_R;
        if (FABS(b->x - f->d.px) > reach || FABS(b->y - f->d.py) > reach) continue;
        w->tests++;
        imp = pb_hit_flipper(b, f);
        if (imp > FROMI(300)) emit(w, EV_FLIPHIT, b->x, b->y, f->d.side, 0, FINT(imp), b->id);
    }
    /* the saucer catches a ball that isn't moving too fast */
    if (b->nocatch > 0) b->nocatch -= dt;
    else if (FABS(b->x - t->saucer_x) < t->saucer_r && FABS(b->y - t->saucer_y) < t->saucer_r &&
             fhypot(b->x - t->saucer_x, b->y - t->saucer_y) < t->saucer_r && fhypot(b->vx, b->vy) < FROMI(1300)) {
        b->state = BS_SAUCER; b->x = t->saucer_x; b->y = t->saucer_y; b->vx = b->vy = 0;
        b->hold = PB_SECS(1.2);
        emit(w, EV_SAUCER, b->x, b->y, 0, 0, 0, b->id);
        return;
    }
    /* resting on the plunger (the browser's 3 mm misses a ball sitting on the
     * lane's floor, 3 mm above the plunger: it would stay in play there) */
    if (b->x > FROMI(478) && b->y < t->plunger_rest + FROMI(4) && fhypot(b->vx, b->vy) < FROMI(80)) {
        b->state = BS_LANE; b->x = t->plunger_x; b->y = t->plunger_rest; b->vx = b->vy = 0;
        emit(w, EV_LANEREST, b->x, b->y, 0, 0, 0, b->id);
    }
    if (b->y < FROMI(-40)) { b->state = BS_DRAINED; emit(w, EV_DRAIN, b->x, b->y, 0, 0, 0, b->id); }
}

/* A ball on a ramp: one-dimensional motion along the rail, pulled by the slope. */
static void ramp_step(World *w, Ball *b, fix h)
{
    const Ramp *r = &w->t->ramp[b->ramp];
    fix x, y, z, slope, dx, dy, L = r->prof[r->n - 1];
    pb_ramp_at(r, b->rs, &x, &y, &z, &slope, NULL, NULL);
    b->rv += FMUL(-3200 * slope - FROMI(120) * fsign(b->rv), h);
    b->rs += FMUL(b->rv, h);
    pb_ramp_at(r, b->rs, &x, &y, &z, NULL, &dx, &dy);
    b->x = x; b->y = y; b->z = z;
    b->vx = FMUL(dx, b->rv); b->vy = FMUL(dy, b->rv);
    if (b->rs <= 0 && b->rv <= 0) {     /* didn't make it: rolls back out of the entrance */
        b->state = BS_PLAY; b->z = 0;
        b->x = FROMI(r->pts[0][0]); b->y = FROMI(r->pts[0][1] - 4);
        b->vx = 0; b->vy = b->rv < FROMI(-120) ? b->rv : FROMI(-120);
        emit(w, EV_RAMPFAIL, b->x, b->y, b->ramp, 0, 0, b->id);
    } else if (b->rs >= L) {
        fix v = -FMUL(FABS(b->rv), FX(0.5));
        b->state = BS_PLAY; b->z = 0;
        b->x = r->exit_x; b->y = r->exit_y;
        b->vx = 0; b->vy = r->exit_vy < v ? r->exit_vy : v;
        emit(w, EV_RAMP, b->x, b->y, b->ramp, 0, 0, b->id);
    }
}

/* One fixed step (h, normally PB_STEP), split into substeps (see pb.h). */
void pb_world_step(World *w, fix h)
{
    fix fast = 0, tip = 0, dist;
    int32_t n, k, i, j, done = 0, live = 0;
    const fix roll = FONE - FMUL(FX(0.04), h);          /* a touch of rolling resistance */

    for (i = 0; i < w->nballs; i++) {
        Ball *b = &w->balls[i];
        if (b->state != BS_PLAY) continue;
        live++;
        b->vx = FMUL(b->vx, roll); b->vy = FMUL(b->vy, roll);
        { fix s = fhypot(b->vx, b->vy) + PB_GRAVITY * h; if (s > fast) fast = s; }
    }
    for (i = 0; i < w->nflip; i++)
        if (flipper_moving(w, &w->flip[i])) { fix tp = 42 * (w->flip[i].d.len + w->flip[i].d.r2); if (tp > tip) tip = tp; }
    dist = FMUL(fast + tip, h);
    n = live ? (dist + PB_R / 4 - 1) / (PB_R / 4) : 1;
    if (n < 1) n = 1;
    if (n > 160) n = 160;
    w->substeps = n;
    for (k = 0; k < n; k++) {
        int32_t upto = (int32_t)(((int32_t)h * (k + 1)) / n);
        fix dt = upto - done;            /* the substeps add up to h exactly */
        done = upto;
        for (i = 0; i < w->nflip; i++) flipper_step(w, &w->flip[i], dt);
        for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_PLAY) move_ball(w, &w->balls[i], dt);
        for (i = 0; i < w->nballs; i++)
            for (j = i + 1; j < w->nballs; j++)
                if (w->balls[i].state == BS_PLAY && w->balls[j].state == BS_PLAY) pb_hit_balls(&w->balls[i], &w->balls[j]);
    }
    for (i = 0; i < w->nballs; i++) {
        Ball *b = &w->balls[i];
        if (b->state == BS_RAMP) ramp_step(w, b, h);
        else if (b->state == BS_SAUCER) { b->hold--; b->vx = b->vy = 0; }
        else if (b->state == BS_LANE) { b->vx = b->vy = 0; }
    }
    /* the spinners keep turning on their own and slow down */
    for (i = 0; i < w->t->nspin; i++) {
        Spinner *sp = &w->spin[i];
        if (sp->w <= 0) continue;
        sp->w -= FMUL(FROMI(5) + FMUL(sp->w, FX(0.9)), h);
        if (sp->w < 0) sp->w = 0;
        sp->ang += FMUL(sp->w, h);
        while (sp->ang >= FPI) { sp->ang -= FPI; emit(w, EV_SPIN, w->t->spin[i].x, w->t->spin[i].y, i, 0, 0, 0); }
    }
}

void pb_kick_saucer(World *w, Ball *b, fix jitter)
{
    int right = w->t->saucer_x > FROMI(PB_MID);         /* kicks out towards the middle */
    b->state = BS_PLAY;
    b->x = w->t->saucer_x + (right ? FROMI(-4) : FROMI(4)); b->y = w->t->saucer_y - FROMI(4);
    b->vx = FROMI(520) + jitter; b->vy = FROMI(-820);
    if (right) b->vx = -b->vx;
    b->nocatch = FX(0.5);
    emit(w, EV_KICK, b->x, b->y, 0, 0, 0, b->id);
}

void pb_plunge(World *w, Ball *b, fix pull)
{
    pull = fclamp(pull, 0, FONE);
    b->state = BS_PLAY;
    b->vx = 0; b->vy = FROMI(700) + 4300 * pull;
    b->y = w->t->plunger_rest + FONE;
    emit(w, EV_PLUNGE, b->x, b->y, 0, 0, (pull * 1000) >> 16, b->id);
}

void pb_nudge(World *w, fix dx, fix dy)
{
    int i;
    for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_PLAY) { w->balls[i].vx += dx; w->balls[i].vy += dy; }
}

/* ================================================================ the rules */

#define MISSION_TIME PB_SECS(40)
#define SAVE_TIME    PB_SECS(10)

static void gev(Game *g, int type, int x, int y, int id, int32_t val)
{
    Event *e;
    if (g->nev >= PB_MAX_EV) return;
    e = &g->ev[g->nev++];
    memset(e, 0, sizeof *e);
    e->type = (int8_t)type; e->x = (int16_t)x; e->y = (int16_t)y; e->id = (int8_t)id; e->val = val;
}

void pb_fmt(char *out, int32_t n)
{
    char tmp[16];
    int len = 0, i, o = 0;
    uint32_t u = (uint32_t)(n < 0 ? -n : n);
    do { tmp[len++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (n < 0) out[o++] = '-';
    for (i = len - 1; i >= 0; i--) {
        out[o++] = tmp[i];
        if (i && i % 3 == 0) out[o++] = ',';
    }
    out[o] = 0;
}

static void copy(char *d, const char *s, int max)
{
    int i = 0;
    if (s) for (; s[i] && i < max - 1; i++) d[i] = s[i];
    d[i] = 0;
}

void pb_say(Game *g, const char *big, const char *small, int32_t steps)
{
    Msg *m;
    if (g->nmsg >= PB_MAX_MSG) { memmove(&g->msg[0], &g->msg[1], sizeof(Msg) * (PB_MAX_MSG - 1)); g->nmsg--; }
    m = &g->msg[g->nmsg++];
    copy(m->big, big, sizeof m->big);
    copy(m->small, small, sizeof m->small);
    m->until = g->time + steps;
    m->at = g->time;
}

void pb_award(Game *g, int32_t pts, int x, int y)
{
    int32_t p = g->supernova > g->time ? pts * 3 : pts;
    g->score += p;
    if (x != -9999) gev(g, EV_SCORE, x, y, 0, p);
}
#define AWARD(g, p) pb_award(g, p, -9999, 0)

static void add_timer(Game *g, int32_t steps, int kind, int arg)
{
    if (g->ntimers >= PB_MAX_TIMER) return;
    g->timers[g->ntimers].at = g->time + steps;
    g->timers[g->ntimers].kind = kind;
    g->timers[g->ntimers].arg = arg;
    g->ntimers++;
}

static void reset_rules(Game *g)
{
    int i;
    g->lanes[0] = g->lanes[1] = g->lanes[2] = 0;
    g->skill_lane = 1;
    g->bonus_x = 1;
    g->banks = 0; g->ramps = 0; g->locks = 0;
    g->missions_done = 0;
    g->mission = -1; g->mission_lit = 1;
    g->multiball = 0; g->jackpot = 0;
    g->supernova = 0; g->jackpot_lit = 0;
    g->bonus_ramps = g->bonus_targets = g->bonus_lanes = g->bonus_spins = 0;
    g->tilt_meter = 0; g->tilt_warnings = 0; g->tilted = 0;
    g->save_until = 0; g->skill_armed = 0; g->plunge_at = -1;
    g->pull = 0; g->pull_held = 0;
    g->ntimers = 0;
    for (i = 0; i < g->t->nseg; i++) if (g->t->seg[i].kind == SK_DROP) g->t->seg[i].up = 1;
}

void pb_game_init(Game *g, Table *t, uint32_t seed)
{
    memset(g, 0, sizeof *g);
    g->t = t;
    pb_world_init(&g->w, t);
    g->state = GS_ATTRACT;
    g->balls_per_game = 3;
    g->rng = seed ? seed : 1;
    reset_rules(g);
}

/* a new ball (or a multiball / saved ball) in the plunger lane */
static Ball *serve(Game *g, int autol)
{
    Ball *b;
    if (!autol) {
        g->ball_no++;
        g->tilt_meter = 0; g->tilt_warnings = 0; g->tilted = 0;
        g->w.flipper_power = 1; g->w.active_targets = 1;
        g->skill_lane = (g->skill_lane + 1) % 3;
        g->skill_armed = 1;
        g->bonus_ramps = g->bonus_targets = g->bonus_lanes = g->bonus_spins = 0;
        g->bonus_x = 1;
    }
    b = pb_add_ball(&g->w, g->t->plunger_x, g->t->plunger_rest, 0, 0, BS_LANE);
    if (b && autol) b->autol = PB_SECS(0.6);
    return b;
}

void pb_game_start(Game *g)
{
    char s[32];
    g->score = 0; g->ball_no = 0; g->state = GS_PLAY; g->time = 0;
    g->w.nballs = 0;
    g->nmsg = 0; g->nev = 0;
    reset_rules(g);
    serve(g, 0);
    copy(s, g->t->name, sizeof s);
    pb_say(g, s, "BALL 1", PB_SECS(2.5));
}

int pb_live_balls(const Game *g)
{
    int i, n = 0;
    for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].state != BS_DRAINED) n++;
    return n;
}

static void fire_plunger(Game *g, Ball *b, fix pull, int autol)
{
    pb_plunge(&g->w, b, pull);
    if (!autol) {
        if (g->plunge_at < 0 || g->save_until < g->time) g->save_until = g->time + SAVE_TIME;
        g->plunge_at = g->time;
    }
}

static void lane_change(Game *g, int dir)
{
    int any = g->lanes[0] || g->lanes[1] || g->lanes[2], all = g->lanes[0] && g->lanes[1] && g->lanes[2];
    if (any && !all) {
        int a = g->lanes[0], b = g->lanes[1], c = g->lanes[2];
        if (dir < 0) { g->lanes[0] = b; g->lanes[1] = c; g->lanes[2] = a; }
        else         { g->lanes[0] = c; g->lanes[1] = a; g->lanes[2] = b; }
    }
    if (g->skill_armed) g->skill_lane = (g->skill_lane + (dir < 0 ? 2 : 1)) % 3;
}

void pb_do_tilt(Game *g)
{
    int i;
    g->tilted = 1;
    g->w.flipper_power = 0; g->w.active_targets = 0;
    for (i = 0; i < g->w.nflip; i++) g->w.flip[i].held = 0;
    g->save_until = 0;
    g->mission = -1;
    pb_say(g, "TILT", "NO BONUS", PB_SECS(99));
    gev(g, EV_TILT, 0, 0, 0, 0);
}

static void do_nudge(Game *g, int nx, int ny)
{
    fix mag = fhypot(FROMI(nx), FROMI(ny));
    if (mag <= 0) return;
    pb_nudge(&g->w, FDIV(FROMI(nx * 240), mag), FDIV(FROMI(ny * 240), mag) + FROMI(40));
    gev(g, EV_NUDGE, nx, ny, 0, 0);
    g->tilt_meter += FONE;
    if (g->tilt_meter > FX(1.7)) {
        g->tilt_meter = FX(0.3);
        g->tilt_warnings++;
        if (g->tilt_warnings >= 3) pb_do_tilt(g);
        else {
            pb_say(g, "DANGER", g->tilt_warnings == 1 ? "WARNING 1 OF 2" : "LAST WARNING", PB_SECS(1.6));
            gev(g, EV_TILTWARN, 0, 0, 0, g->tilt_warnings);
        }
    }
}

static void start_multiball(Game *g, int extra)
{
    int i;
    g->multiball = 1; g->jackpot = 0;
    for (i = 0; i < extra; i++) add_timer(g, PB_SECS(0.4) + i * PB_SECS(1.1), TM_SERVE, 0);
    g->save_until = g->time + PB_SECS(12);
    pb_say(g, "MULTIBALL", "RAMP FOR JACKPOT", PB_SECS(2.6));
    gev(g, EV_MULTIBALL, 0, 0, 0, 0);
}

void pb_mission_hit(Game *g, int ev)
{
    const MissionDef *m;
    char s[16];
    if (g->mission < 0) return;
    m = &pb_missions[g->mission];
    if (m->ev != ev) return;
    if (++g->mission_n >= m->need) {
        AWARD(g, m->award);
        g->missions_done |= 1 << g->mission;
        pb_fmt(s, m->award);
        pb_say(g, "MISSION DONE", s, PB_SECS(2.4));
        gev(g, EV_MISSIONDONE, 0, 0, g->mission, 0);
        g->mission = -1;
        g->mission_lit = 1;
    }
}

static void sensor(Game *g, const Event *e)
{
    if (e->kind == SN_LANE) {
        int i = e->id;
        if (e->up) return;              /* lanes count on the way down */
        if (g->skill_armed && g->time - g->plunge_at < PB_SECS(6)) {
            g->skill_armed = 0;
            if (i == g->skill_lane) { pb_award(g, 10000, e->x, e->y); pb_say(g, "SKILL SHOT", "10,000", PB_SECS(2)); gev(g, EV_SKILL, 0, 0, 0, 0); }
        } else g->skill_armed = 0;
        pb_award(g, 500, e->x, e->y);
        g->bonus_lanes++;
        if (!g->lanes[i]) {
            g->lanes[i] = 1;
            if (g->lanes[0] && g->lanes[1] && g->lanes[2]) {
                g->lanes[0] = g->lanes[1] = g->lanes[2] = 0;
                if (g->bonus_x < 5) g->bonus_x++;
                AWARD(g, 2000);
                g->mission_lit = g->mission_lit || g->mission < 0;
                {
                    char big[16] = "BONUS  X";
                    big[6] = (char)('0' + g->bonus_x);
                    pb_say(g, big, "LANES COMPLETE", PB_SECS(1.8));
                }
                gev(g, EV_LANESDONE, 0, 0, 0, 0);
                pb_mission_hit(g, MI_LANES);
            }
        }
        return;
    }
    if (e->kind == SN_IN) { if (!e->up) pb_award(g, 1000, e->x, e->y); return; }
    if (e->kind == SN_OUT) { if (!e->up) { pb_award(g, 2500, e->x, e->y); gev(g, EV_OUTLANE, e->x, e->y, 0, 0); } return; }
}

static void drop_hit(Game *g, const Event *e)
{
    Table *t = g->t;
    int i, bank = t->seg[t->drop_seg[e->id]].bank, all = 1;
    pb_award(g, 750, e->x, e->y);
    g->bonus_targets++;
    for (i = 0; i < t->ndrop; i++) {
        const Seg *s = &t->seg[t->drop_seg[i]];
        if (s->bank == bank && s->up) all = 0;
    }
    if (all) {
        g->banks++;
        AWARD(g, 5000);
        pb_say(g, "BANK COMPLETE", "5,000", PB_SECS(1.5));
        gev(g, EV_BANK, 0, 0, bank, 0);
        pb_mission_hit(g, MI_BANK);
        add_timer(g, PB_SECS(1.2), TM_BANK_RESET, bank);
    }
}

static void ramp_made(Game *g, const Event *e)
{
    g->ramps++; g->bonus_ramps++;
    pb_award(g, 2500, e->x, e->y);
    pb_mission_hit(g, MI_RAMP);
    if (g->multiball) {
        int32_t pts = 50000 + 10000 * g->jackpot++, before = g->score;
        char s[16];
        AWARD(g, pts);
        pb_fmt(s, g->score - before);
        pb_say(g, "JACKPOT", s, PB_SECS(2.2));
        gev(g, EV_JACKPOT, 0, 0, 0, 0);
        return;
    }
    g->locks++;
    if (g->locks >= 3) { g->locks = 0; start_multiball(g, 2); }
    else {
        char big[16] = "BALL 0 LOCKED", small[32] = "0 MORE FOR MULTIBALL";
        big[5] = (char)('0' + g->locks);
        small[0] = (char)('0' + 3 - g->locks);
        pb_say(g, big, small, PB_SECS(1.8));
        gev(g, EV_LOCK, 0, 0, g->locks, 0);
    }
}

static Ball *find_ball(Game *g, int id)
{
    int i;
    for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].id == id) return &g->w.balls[i];
    return NULL;
}

static void saucer(Game *g, const Event *e)
{
    Ball *b = find_ball(g, e->ball);
    pb_award(g, 1000, e->x, e->y);
    if (g->mission < 0 && g->mission_lit) {
        int left[5], nl = 0, i, done = 0, m;
        char s[32];
        for (i = 0; i < 5; i++) { if (g->missions_done & (1 << i)) done++; else left[nl++] = i; }
        if (!nl) {                      /* every mission done: the wizard mode */
            g->missions_done = 0;
            g->supernova = g->time + PB_SECS(30);
            pb_say(g, "SUPERNOVA", "EVERYTHING SCORES 3X", PB_SECS(3));
            gev(g, EV_SUPERNOVA, 0, 0, 0, 0);
            if (!g->multiball) start_multiball(g, 2);
            if (b) b->hold = PB_SECS(2.5);
            return;
        }
        m = left[(done + g->ball_no) % nl];
        g->mission = m; g->mission_n = 0;
        g->mission_until = g->time + MISSION_TIME + PB_SECS(2);
        g->mission_lit = 0;
        pb_fmt(s, pb_missions[m].need);
        { int l = 0; while (s[l]) l++; s[l++] = ' '; copy(s + l, pb_missions[m].goal, (int)sizeof s - l); }
        pb_say(g, g->t->mission[m], s, PB_SECS(2.4));
        gev(g, EV_MISSIONSTART, 0, 0, m, 0);
        if (b) b->hold = PB_SECS(2.2);
    }
}

static void drain(Game *g, const Event *e)
{
    int left;
    char s[16];
    (void)e;
    pb_remove_drained(&g->w);
    left = pb_live_balls(g);
    if (!g->tilted && g->time < g->save_until) {
        pb_say(g, "BALL SAVED", "", PB_SECS(1.6));
        gev(g, EV_SAVE, 0, 0, 0, 0);
        serve(g, 1);
        return;
    }
    if (left) {
        if (g->multiball && left == 1) { g->multiball = 0; pb_say(g, "MULTIBALL OVER", "", PB_SECS(1.5)); }
        return;
    }
    if (g->ntimers && g->multiball) return;            /* a multiball ball is about to come out */
    g->multiball = 0;
    g->mission = -1; g->mission_lit = 1;
    /* end of ball: the bonus (none if tilted) */
    {
        int32_t base = g->bonus_ramps * 1000 + g->bonus_targets * 250 + g->bonus_lanes * 200 + g->bonus_spins * 20 + 1000;
        g->bonus_pts = g->tilted ? 0 : base * g->bonus_x;
    }
    g->score += g->bonus_pts;
    g->state = GS_BONUS; g->bonus_t = PB_SECS(2.2);
    pb_fmt(s, g->bonus_pts);
    if (g->tilted) pb_say(g, "TILT", s, PB_SECS(2.2));
    else { char big[16] = "BONUS  X"; big[6] = (char)('0' + g->bonus_x); pb_say(g, big, s, PB_SECS(2.2)); }
    gev(g, EV_ENDBALL, 0, 0, 0, 0);
}

static void after_bonus(Game *g)
{
    char s[16], big[16] = "BALL 0";
    g->w.flipper_power = 1; g->w.active_targets = 1;
    pb_fmt(s, g->score);
    if (g->ball_no >= g->balls_per_game + g->extra_balls) {
        g->state = GS_OVER;
        pb_say(g, "GAME OVER", s, PB_SECS(99));
        gev(g, EV_GAMEOVER, 0, 0, 0, 0);
        return;
    }
    g->state = GS_PLAY;
    serve(g, 0);
    big[5] = (char)('0' + g->ball_no);
    pb_say(g, big, s, PB_SECS(2));
}

void pb_game_handle(Game *g, const Event *e)
{
    if (g->nev < PB_MAX_EV) g->ev[g->nev++] = *e;
    if (g->tilted && e->type != EV_DRAIN && e->type != EV_LANEREST) return;
    switch (e->type) {
    case EV_BUMPER: pb_award(g, 100, e->x, e->y); pb_mission_hit(g, MI_BUMPER); break;
    case EV_SLING:  AWARD(g, 10); break;
    case EV_SPIN:   AWARD(g, 100); g->bonus_spins++; pb_mission_hit(g, MI_SPIN); break;
    case EV_DROP:   drop_hit(g, e); break;
    case EV_SENSOR: sensor(g, e); break;
    case EV_RAMP:   ramp_made(g, e); break;
    case EV_SAUCER: saucer(g, e); break;
    case EV_DRAIN:  drain(g, e); break;
    }
}

static void run_timers(Game *g)
{
    int i, n = 0;
    Timer due[PB_MAX_TIMER];
    int nd = 0;
    for (i = 0; i < g->ntimers; i++) {
        if (g->timers[i].at <= g->time) due[nd++] = g->timers[i];
        else g->timers[n++] = g->timers[i];
    }
    g->ntimers = n;
    for (i = 0; i < nd; i++) {
        if (due[i].kind == TM_BANK_RESET) {
            int k;
            for (k = 0; k < g->t->ndrop; k++) {
                Seg *s = &g->t->seg[g->t->drop_seg[k]];
                if (s->bank == due[i].arg) s->up = 1;
            }
            gev(g, EV_BANKRESET, 0, 0, due[i].arg, 0);
        } else if (due[i].kind == TM_SERVE) serve(g, 1);
    }
}

void pb_game_tick(Game *g, const PbInput *in)
{
    World *w = &g->w;
    int i, L, R, nev;
    Ball *in_lane = NULL;
    g->time++;
    if (g->state == GS_BONUS) {
        if (--g->bonus_t <= 0) after_bonus(g);
        run_timers(g);
        return;
    }
    /* the flippers (dead while tilted); pressing one also moves the lit lanes along */
    L = in->left && !g->tilted;
    R = in->right && !g->tilted;
    {
        int held_l = 0, held_r = 0;
        for (i = 0; i < w->nflip; i++) {
            if (w->flip[i].d.side == 0) held_l |= w->flip[i].held; else held_r |= w->flip[i].held;
        }
        if (L && !held_l) lane_change(g, -1);
        if (R && !held_r) lane_change(g, 1);
        for (i = 0; i < w->nflip; i++) w->flip[i].held = w->flip[i].d.side == 0 ? L : R;
    }
    /* the plunger: hold to pull back (one second to full), release to fire */
    for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_LANE && w->balls[i].autol < 0) { in_lane = &w->balls[i]; break; }
    if (!in_lane) for (i = 0; i < w->nballs; i++) if (w->balls[i].state == BS_LANE) { in_lane = &w->balls[i]; break; }
    if (in->pull_to >= 0) { g->pull = fclamp((fix)(((int32_t)in->pull_to << 16) / 1000), 0, FONE); g->pull_held = 1; }
    else if (in->plunger) { g->pull += PB_STEP; if (g->pull > FONE) g->pull = FONE; g->pull_held = 1; }
    else if (g->pull_held) {
        /* (the browser won't fire while tilted, so a ball in the lane then
         * keeps the ball alive for ever; here it fires, and drains) */
        g->pull_held = 0;
        if (in_lane) fire_plunger(g, in_lane, g->pull, 0);
        g->pull = 0;
    }
    /* automatic launches, for multiball and saved balls */
    for (i = 0; i < w->nballs; i++) {
        Ball *b = &w->balls[i];
        if (b->state == BS_LANE && b->autol >= 0 && --b->autol < 0)
            fire_plunger(g, b, FX(0.82) + (fix)(a3_rand(&g->rng) % 6554), 1);
    }
    /* nudging and the tilt bob */
    if ((in->nudge_x || in->nudge_y) && g->state == GS_PLAY && !g->tilted) do_nudge(g, in->nudge_x, in->nudge_y);
    g->tilt_meter -= FMUL(FX(0.9), PB_STEP);
    if (g->tilt_meter < 0) g->tilt_meter = 0;
    pb_world_step(w, PB_STEP);
    /* the saucer holds the ball, then kicks it out */
    /* (with a little variation the browser doesn't have: a fixed kick can set up an endless loop) */
    for (i = 0; i < w->nballs; i++)
        if (w->balls[i].state == BS_SAUCER && w->balls[i].hold <= 0)
            pb_kick_saucer(w, &w->balls[i], FROMI((int)(a3_rand(&g->rng) % 161) - 80));
    nev = w->nev; w->nev = 0;
    for (i = 0; i < nev; i++) {
        Event e = w->ev[i];
        pb_game_handle(g, &e);
    }
    /* missions time out */
    if (g->mission >= 0 && g->time > g->mission_until) {
        pb_say(g, "MISSION OVER", g->t->mission[g->mission], PB_SECS(2));
        gev(g, EV_MISSIONFAIL, 0, 0, g->mission, 0);
        g->mission = -1; g->mission_lit = 1;
    }
    run_timers(g);
}

void pb_game_update(Game *g, int32_t us, const PbInput *in)
{
    const int32_t step_us = 1000000 / PB_HZ;
    if (g->state != GS_PLAY && g->state != GS_BONUS) return;
    g->acc_us += us;
    if (g->acc_us > 250000) g->acc_us = 250000;
    while (g->acc_us >= step_us) { g->acc_us -= step_us; pb_game_tick(g, in); }
}

/* What the dot-matrix display shows now. */
void pb_dmd(Game *g, Dmd *d)
{
    static char big[24], small[40];
    Msg *m = NULL;
    int i;
    while (g->nmsg > 1 && g->msg[0].until < g->time) { memmove(&g->msg[0], &g->msg[1], sizeof(Msg) * (g->nmsg - 1)); g->nmsg--; }
    for (i = 0; i < g->nmsg; i++) if (g->msg[i].until >= g->time) m = &g->msg[i];
    d->flash = 0;
    if (g->state == GS_ATTRACT) { d->big = g->t->name; d->small = "PRESS FIRE"; return; }
    if (m) { d->big = m->big; d->small = m->small; d->flash = g->time - m->at < PB_SECS(0.6); return; }
    pb_fmt(big, g->score);
    d->big = big; d->small = small;
    {
        char *p = small;
        const char *s;
        if (g->mission >= 0) {
            int32_t left = (g->mission_until - g->time + PB_HZ - 1) / PB_HZ;
            char num[12];
            for (s = g->t->mission[g->mission]; *s && *s != ' '; s++) *p++ = *s;
            *p++ = ' ';
            pb_fmt(num, g->mission_n); for (s = num; *s; s++) *p++ = *s;
            *p++ = '/';
            pb_fmt(num, pb_missions[g->mission].need); for (s = num; *s; s++) *p++ = *s;
            *p++ = ' ';
            pb_fmt(num, left < 0 ? 0 : left); for (s = num; *s; s++) *p++ = *s;
            *p++ = 'S'; *p = 0;
        } else if (g->multiball) {
            char num[12];
            for (s = "MULTIBALL  JACKPOT "; *s; s++) *p++ = *s;
            pb_fmt(num, 50 + 10 * g->jackpot); for (s = num; *s; s++) *p++ = *s;
            *p++ = 'K'; *p = 0;
        } else {
            int lane = 0;
            for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].state == BS_LANE) lane = 1;
            for (s = "BALL "; *s; s++) *p++ = *s;
            *p++ = (char)('0' + g->ball_no % 10);
            if (lane) { for (s = "  SKILL SHOT "; *s; s++) *p++ = *s; *p++ = (char)('A' + g->skill_lane); }
            else if (g->bonus_x > 1) { for (s = "  BONUS "; *s; s++) *p++ = *s; *p++ = (char)('0' + g->bonus_x); *p++ = 'X'; }
            *p = 0;
        }
    }
}

/* Which lamps are lit (the browser page's rules); blink toggles at 2.5 Hz,
 * chase counts up in the attract mode. */
void pb_lamps(const Game *g, int blink, int chase, uint8_t on[L_COUNT])
{
    int i, lane = 0, skill;
    memset(on, 0, L_COUNT);
    if (g->state == GS_ATTRACT || g->state == GS_OVER) {
        for (i = 0; i < L_COUNT; i++) on[i] = (uint8_t)((i + chase) % 5 == 0);
        return;
    }
    for (i = 0; i < g->w.nballs; i++) if (g->w.balls[i].state == BS_LANE) lane = 1;
    skill = g->skill_armed && (g->time - g->plunge_at < PB_SECS(6) || lane) ? g->skill_lane : -1;
    for (i = 0; i < 3; i++) on[L_LANE0 + i] = (uint8_t)(g->lanes[i] || (skill == i && blink));
    for (i = 0; i < 3; i++) on[L_LOCK0 + i] = (uint8_t)(g->multiball ? blink : (i < g->locks || (i == g->locks && blink && g->state == GS_PLAY)));
    on[L_JACKPOT] = (uint8_t)(g->multiball && blink);
    for (i = 0; i < 5; i++) on[L_M0 + i] = (uint8_t)((g->missions_done & (1 << i)) || (g->mission == i && blink));
    on[L_MISSION] = (uint8_t)(g->mission_lit && g->mission < 0 && blink);
    for (i = 0; i < 4; i++) on[L_X2 + i] = (uint8_t)(g->bonus_x >= i + 2);
    on[L_SAVE] = (uint8_t)(g->time < g->save_until);
    for (i = 0; i < g->t->nspin && i < 2; i++) on[L_SPIN0 + i] = (uint8_t)(g->w.spin[i].w > FROMI(2));
    for (i = 0; i < g->t->ndrop && i < 8; i++) on[L_DROP0 + i] = g->t->seg[g->t->drop_seg[i]].up;
    on[L_SUPER] = (uint8_t)(g->supernova > g->time && blink);
}

/* The attract mode's player (and the tests'): launches, holds a flipper up
 * while a ball comes down towards it and lets go a moment after. */
void pb_bot(const Game *g, PbInput *in, Bot *bot)
{
    int i, k;
    int *hold = bot->hold;
    uint32_t *rng = &bot->rng;
    memset(in, 0, sizeof *in);
    in->pull_to = -1;
    for (k = 0; k < 2; k++) if (hold[k] > 0) { hold[k]--; if (k) in->right = 1; else in->left = 1; }
    for (i = 0; i < g->w.nballs; i++) {
        const Ball *b = &g->w.balls[i];
        if (b->state == BS_LANE) {
            if (++bot->launch > PB_SECS(0.8)) { in->pull_to = 800 + (int)(a3_rand(rng) % 200); bot->launch = 0; }
            continue;
        }
        if (b->state != BS_PLAY) continue;
        for (k = 0; k < g->w.nflip; k++) {
            const Flipper *f = &g->w.flip[k];
            fix tx, ty, dx, dy;
            pb_flipper_tip(f, &tx, &ty);
            dx = b->x - (f->d.px + tx) / 2;
            dy = b->y - (f->d.py + ty) / 2;
            /* coming down within reach: flip, with a little randomness in the timing */
            if (b->vy < FROMI(-60) && FABS(dx) < f->d.len * 3 / 4 + PB_R && dy > 0 && dy < FROMI(40) + FABS(b->vy) / 20) {
                if (a3_rand(rng) % 4) hold[f->d.side] = PB_SECS(0.25);
            }
        }
    }
    for (k = 0; k < 2; k++) if (hold[k] > 0) { if (k) in->right = 1; else in->left = 1; }
}
