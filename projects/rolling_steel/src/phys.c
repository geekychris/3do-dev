/*
 * The marble, and everything it can bump into, at 50 steps/s.
 *
 * Unity does this with PhysX: a sphere Rigidbody against mesh colliders,
 * friction combined by multiplying, bounce by taking the larger, nothing
 * bounces below 2 m/s. Here the same model, small: a sphere with a real
 * spin against the course triangles (one-sided, so you can't be pushed
 * down through a deck from underneath), with Coulomb friction at the
 * contact point. That is what makes the marble roll rather than slide on
 * deck, slide on ice, and bite on sand - and arriving on sand still
 * spinning for ice costs you speed, as it does in the Unity game.
 *
 * MarbleController.cs: input pushes at 34 m/s^2 in the camera's frame (30%
 * in the air), an extra 12 m/s^2 of gravity, flat speed capped at 13 m/s.
 * Obstacles.cs, Hazards.cs: sweepers, crushers, fans, crumbling deck, steel
 * chasers and green blobs.
 */
#include "rs.h"

World Wd;
int phys_sound = -1;

#define STEP(v)      (((v) * 1311L) >> 16)         /* per second -> per step (/50) */
#define ACCEL        2785                          /* 34 m/s^2, per step */
#define MAXSPEED     (13L * FX)
#define BOUNCE_MIN   (2L * FX)                     /* PhysX bounce threshold, 2 m/s */
#define GROUND_NY    1843                          /* contact normal.y > 0.45 */

/* PhysX materials of what you roll on (dynamic friction, bounciness), Q12 */
static const fix surf_fric[S_COUNT] = { 2253, 4506, 205, 1229, 1024 };   /* deck .55 rough 1.1 ice .05 rail .3 bouncy .25 */
static const fix surf_bounce[S_COUNT] = { 492, 82, 205, 1434, 2662 };    /* .12 .02 .05 .35 .65 */

/* ---- maths ---- */

static const short qsin[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384
};

long isin(long a)
{
    int q, i, f;
    long v0, v1, v;
    a &= 0xFFFF;
    q = (int)(a >> 14);
    i = (int)((a >> 8) & 63);
    f = (int)(a & 255);
    if (q & 1) { v0 = qsin[64 - i]; v1 = qsin[63 - i]; }
    else { v0 = qsin[i]; v1 = qsin[i + 1]; }
    v = v0 + (((v1 - v0) * f) >> 8);
    return (q & 2) ? -v : v;
}
long icos(long a) { return isin(a + 16384); }

unsigned long isqrt(unsigned long v)
{
    unsigned long r = 0, b = 1UL << 30;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

/* Q12 vector . Q12 direction -> Q12, safe for velocities up to ~100 u/s */
static fix dotn(V3 a, long nx, long ny, long nz)
{
    return ((a.x >> 2) * nx + (a.y >> 2) * ny + (a.z >> 2) * nz) >> 10;
}

fix vlen(V3 a)
{
    long x = a.x >> 4, y = a.y >> 4, z = a.z >> 4;
    return (fix)isqrt((unsigned long)(x * x + y * y + z * z)) << 4;
}

/* ---- contacts ---- */

static int contact_ground;

/* push the ball out along n (Q12 unit) by pen, then the PhysX-like
 * response against a surface moving at vs */
static void contact(Ball *b, long nx, long ny, long nz, fix pen, int cls, V3 vs, fix surf_mu_scale)
{
    V3 rel, vc, vt, J;
    fix vn, jn, e, mu, mj;
    long jl;
    b->p.x += (nx * pen) >> 12;
    b->p.y += (ny * pen) >> 12;
    b->p.z += (nz * pen) >> 12;
    if (ny > GROUND_NY) contact_ground = 1;
    rel.x = b->v.x - vs.x; rel.y = b->v.y - vs.y; rel.z = b->v.z - vs.z;
    vn = dotn(rel, nx, ny, nz);
    if (vn >= 0) return;
    e = surf_bounce[cls] > b->bounce ? surf_bounce[cls] : b->bounce;
    if (-vn < BOUNCE_MIN) e = 0;
    jn = -vn + ((-vn * e) >> 12);
    b->v.x += (nx * jn) >> 12;
    b->v.y += (ny * jn) >> 12;
    b->v.z += (nz * jn) >> 12;
    /* friction at the contact point, r = -n * radius */
    {
        long rx = -((nx * b->r) >> 12), ry = -((ny * b->r) >> 12), rz = -((nz * b->r) >> 12);
        vc.x = b->v.x - vs.x + ((b->w.y * rz - b->w.z * ry) >> 12);
        vc.y = b->v.y - vs.y + ((b->w.z * rx - b->w.x * rz) >> 12);
        vc.z = b->v.z - vs.z + ((b->w.x * ry - b->w.y * rx) >> 12);
    }
    vn = dotn(vc, nx, ny, nz);
    vt.x = vc.x - ((nx * vn) >> 12);
    vt.y = vc.y - ((ny * vn) >> 12);
    vt.z = vc.z - ((nz * vn) >> 12);
    /* the impulse that stops the slip: a solid sphere's contact point
     * answers 3.5x as fast as its centre */
    J.x = -((vt.x * 1170) >> 12);
    J.y = -((vt.y * 1170) >> 12);
    J.z = -((vt.z * 1170) >> 12);
    mu = (b->fric * ((surf_fric[cls] * surf_mu_scale) >> 12)) >> 12;
    mj = (mu * jn) >> 12;
    jl = vlen(J);
    if (jl > mj && jl > 0) {
        J.x = J.x * (mj >> 4) / (jl >> 4 ? jl >> 4 : 1);
        J.y = J.y * (mj >> 4) / (jl >> 4 ? jl >> 4 : 1);
        J.z = J.z * (mj >> 4) / (jl >> 4 ? jl >> 4 : 1);
    }
    b->v.x += J.x; b->v.y += J.y; b->v.z += J.z;
    /* spin: dw = -(n x J) / (0.4 r) */
    {
        long cx = (ny * J.z - nz * J.y) >> 12, cy = (nz * J.x - nx * J.z) >> 12, cz = (nx * J.y - ny * J.x) >> 12;
        b->w.x -= (cx * b->rot_k) >> 12;
        b->w.y -= (cy * b->rot_k) >> 12;
        b->w.z -= (cz * b->rot_k) >> 12;
    }
}

static const V3 still = { 0, 0, 0 };

/* closest point to p on segment a-b; returns |p - q|^2 (Q12 squared, >> 8) */
static long seg_closest(V3 p, const V3 *a, const V3 *b, V3 *q)
{
    long ex = b->x - a->x, ey = b->y - a->y, ez = b->z - a->z;
    long px = p.x - a->x, py = p.y - a->y, pz = p.z - a->z;
    long den = ((ex >> 2) * (ex >> 2) + (ey >> 2) * (ey >> 2) + (ez >> 2) * (ez >> 2)) >> 8;   /* Q12 squared >> 12 */
    long num = ((px >> 2) * (ex >> 2) + (py >> 2) * (ey >> 2) + (pz >> 2) * (ez >> 2)) >> 8;
    long t, dx, dy, dz;
    if (den <= 0) t = 0;
    else {
        t = num <= 0 ? 0 : num >= den ? FX : (num << 12) / den;
    }
    q->x = a->x + ((ex * t) >> 12);
    q->y = a->y + ((ey * t) >> 12);
    q->z = a->z + ((ez * t) >> 12);
    dx = (p.x - q->x) >> 4; dy = (p.y - q->y) >> 4; dz = (p.z - q->z) >> 4;
    return dx * dx + dy * dy + dz * dz;
}

static long cross2(long eu, long ev, long fu, long fv) { return (eu >> 2) * (fv >> 2) - (ev >> 2) * (fu >> 2); }

static int tile_touched;

/* the ball against one course triangle */
static void hit_tri(Ball *b, long ti)
{
    const CTri *t = &C.t[ti];
    const V3 *a = &C.v[t->v[0]], *bb = &C.v[t->v[1]], *c = &C.v[t->v[2]];
    long nx = t->n[0], ny = t->n[1], nz = t->n[2];
    V3 d;
    fix dist;
    int group = (int)(t->flags >> 8), cls = (int)(t->flags & 255);
    long anx, any, anz, s1, s2, s3, pu, pv, au, av, bu, bv, cu, cv;
    if (group && Wd.crumble[group - 1].state == 2) return;
    if (C.tsphere) {
        const w32 *sp = C.tsphere + ti * 4;
        long dx = (b->p.x - sp[0]) >> 4, dy = (b->p.y - sp[1]) >> 4, dz = (b->p.z - sp[2]) >> 4, rr = (sp[3] + b->r) >> 4;
        if (dx > rr || dx < -rr || dy > rr || dy < -rr || dz > rr || dz < -rr) return;
        if (dx * dx + dy * dy + dz * dz > rr * rr) return;
    }
    d.x = b->p.x - a->x; d.y = b->p.y - a->y; d.z = b->p.z - a->z;
    dist = dotn(d, nx, ny, nz);
    if (dist > b->r || dist < -(b->r >> 1)) return;
    /* inside the triangle? edge functions in the plane it faces most */
    anx = nx < 0 ? -nx : nx; any = ny < 0 ? -ny : ny; anz = nz < 0 ? -nz : nz;
    if (any >= anx && any >= anz) { pu = b->p.x; pv = b->p.z; au = a->x; av = a->z; bu = bb->x; bv = bb->z; cu = c->x; cv = c->z; }
    else if (anx >= anz) { pu = b->p.y; pv = b->p.z; au = a->y; av = a->z; bu = bb->y; bv = bb->z; cu = c->y; cv = c->z; }
    else { pu = b->p.x; pv = b->p.y; au = a->x; av = a->y; bu = bb->x; bv = bb->y; cu = c->x; cv = c->y; }
    s1 = cross2(bu - au, bv - av, pu - au, pv - av);
    s2 = cross2(cu - bu, cv - bv, pu - bu, pv - bv);
    s3 = cross2(au - cu, av - cv, pu - cu, pv - cv);
    if ((s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0)) {
        contact(b, nx, ny, nz, b->r - dist, cls, still, FX);
    } else {
        V3 q, best;
        long d2, bd = 0x7FFFFFFF, r2 = (b->r >> 4) * (b->r >> 4);
        d2 = seg_closest(b->p, a, bb, &q); if (d2 < bd) { bd = d2; best = q; }
        d2 = seg_closest(b->p, bb, c, &q); if (d2 < bd) { bd = d2; best = q; }
        d2 = seg_closest(b->p, c, a, &q); if (d2 < bd) { bd = d2; best = q; }
        if (bd >= r2 || bd <= 0) return;
        {
            fix len = (fix)isqrt((unsigned long)bd) << 4;
            long ex = ((b->p.x - best.x) << 12) / len, ey = ((b->p.y - best.y) << 12) / len, ez = ((b->p.z - best.z) << 12) / len;
            contact(b, ex, ey, ez, b->r - len, cls, still, FX);
        }
    }
    if (group && Wd.crumble[group - 1].state == 0) tile_touched = group;
}

static void hit_course(Ball *b)
{
    const Grid *g = &C.cgrid;
    long cx = (b->p.x - g->x0) >> 13, cz = (b->p.z - g->z0) >> 13, k, cell;   /* 2-unit cells */
    if (b->p.x < g->x0 || b->p.z < g->z0 || cx >= g->nx || cz >= g->nz) return;
    cell = cz * g->nx + cx;
    for (k = g->off[cell]; k < g->off[cell + 1]; k++) hit_tri(b, g->idx[k]);
}

/* a yaw-only box: world point -> box space */
static void to_box(const OBB *o, V3 p, long *lx, long *ly, long *lz)
{
    long dx = p.x - o->cx, dz = p.z - o->cz;
    *lx = ((dx >> 2) * o->cs - (dz >> 2) * o->sn) >> 12;
    *lz = ((dx >> 2) * o->sn + (dz >> 2) * o->cs) >> 12;
    *ly = p.y - o->cy;
}

static fix clampf(fix v, fix lo, fix hi) { return v < lo ? lo : v > hi ? hi : v; }

int phys_overlap_obb(const OBB *o, V3 p, fix r)
{
    long lx, ly, lz, dx, dy, dz;
    to_box(o, p, &lx, &ly, &lz);
    dx = (lx - clampf(lx, -o->hx, o->hx)) >> 4;
    dy = (ly - clampf(ly, -o->hy, o->hy)) >> 4;
    dz = (lz - clampf(lz, -o->hz, o->hz)) >> 4;
    return dx * dx + dy * dy + dz * dz < (r >> 4) * (r >> 4);
}

/* the ball against a solid yaw-only box moving at vs; returns 1 if touching */
static int hit_box(Ball *b, const OBB *o, V3 vs, int cls)
{
    long lx, ly, lz, qx, qy, qz, dx, dy, dz, d2, nx, ny, nz, len;
    to_box(o, b->p, &lx, &ly, &lz);
    qx = clampf(lx, -o->hx, o->hx); qy = clampf(ly, -o->hy, o->hy); qz = clampf(lz, -o->hz, o->hz);
    dx = lx - qx; dy = ly - qy; dz = lz - qz;
    d2 = (dx >> 4) * (dx >> 4) + (dy >> 4) * (dy >> 4) + (dz >> 4) * (dz >> 4);
    if (d2 >= (b->r >> 4) * (b->r >> 4)) return 0;
    if (d2 > 0) {
        len = (fix)isqrt((unsigned long)d2) << 4;
        if (len < 16) len = 16;
        nx = (dx << 12) / len; ny = (dy << 12) / len; nz = (dz << 12) / len;
        len = b->r - len;
    } else {
        /* centre inside: out through the nearest face */
        long px = o->hx - (lx < 0 ? -lx : lx), py = o->hy - (ly < 0 ? -ly : ly), pz = o->hz - (lz < 0 ? -lz : lz);
        nx = ny = nz = 0;
        if (py <= px && py <= pz) { ny = ly < 0 ? -FX : FX; len = py + b->r; }
        else if (px <= pz) { nx = lx < 0 ? -FX : FX; len = px + b->r; }
        else { nz = lz < 0 ? -FX : FX; len = pz + b->r; }
    }
    /* back to world: X = (cs, 0, -sn), Z = (sn, 0, cs) */
    {
        long wx = ((nx * o->cs) + (nz * o->sn)) >> 14, wz = ((-nx * o->sn) + (nz * o->cs)) >> 14;
        contact(b, wx, ny, wz, len, cls, vs, FX);
    }
    return 1;
}

/* ---- the moving world ---- */

static OBB sweeper_box(int i)
{
    const Sweeper *s = &C.sweep[i];
    long a = s->yaw * 2 / 45 + (Wd.sweep_angle[i] >> 8);        /* degrees Q12 -> 65536 per turn */
    OBB o;
    o.cx = s->x; o.cy = s->y; o.cz = s->z;
    o.hx = s->len / 2; o.hy = 717; o.hz = 922;              /* 0.35 x 0.45 arm */
    /* the arm's yaw is a; a box of that yaw has cs = cos, sn = sin */
    o.cs = icos(a); o.sn = isin(a);
    return o;
}

static OBB crusher_box(int i)
{
    const Crusher *c = &C.crush[i];
    OBB o;
    o.cx = c->x; o.cz = c->z;
    o.cy = c->y + ((c->lift * Wd.crush_h[i]) >> 12);
    o.hx = c->w / 2; o.hy = 3686; o.hz = c->w / 2;           /* 1.8 tall */
    o.cs = c->cs; o.sn = c->sn;
    return o;
}

static void ball_init(Ball *b, V3 p, fix r, fix inv_mass, fix fric, fix bounce, fix gravity)
{
    b->p = p;
    b->v.x = b->v.y = b->v.z = 0;
    b->w = b->v;
    b->r = r;
    b->inv_mass = inv_mass;
    b->fric = fric;
    b->bounce = bounce;
    b->gravity = gravity;
    b->rot_k = (fix)((FX * FX) / ((r * 1638) >> 12));        /* 1 / (0.4 r) */
    b->grounded = b->frozen = 0;
    b->live = 1;
    b->last_ground = p;
    b->touching_enemy = b->touch_sweeper = b->touch_tile = 0;
}

void phys_reset_enemies(void)
{
    int i;
    for (i = 0; i < Wd.nenemy; i++) {
        Enemy *e = &Wd.enemy[i];
        /* steel marbles: mass 3, the deck's material; blobs glide */
        ball_init(&e->b, e->home, e->kind ? 2253 : 2867, 1365, 2253, 492, 804);
        e->phase = 0;
    }
}

void phys_reset(void)
{
    int i;
    long n;
    for (i = 0; i < MAXCRUMBLE; i++) {
        Wd.crumble[i].state = 0;
        Wd.crumble[i].timer = 0;
        Wd.crumble[i].fall = 0;
    }
    n = C.nenemy > MAXENEMY ? MAXENEMY : C.nenemy;
    Wd.nenemy = (int)n;
    for (i = 0; i < n; i++) {
        Enemy *e = &Wd.enemy[i];
        e->kind = (int)C.enemy[i].kind;
        e->home.x = C.enemy[i].x; e->home.y = C.enemy[i].y; e->home.z = C.enemy[i].z;
        e->range = C.enemy[i].range;
        e->speed = C.enemy[i].speed;
        e->ax = C.enemy[i].ax; e->az = C.enemy[i].az;
    }
    phys_reset_enemies();
    for (i = 0; i < 8; i++) {
        Wd.sweep_angle[i] = 0;
        Wd.crush_slammed[i] = 0;
        Wd.crush_h[i] = 0;
        Wd.crush_clock[i] = i < C.ncrush ? (C.crush[i].phase * C.crush[i].period) >> 12 : 0;
    }
    Wd.clock = 0;
}

/* obstacles and enemies; marbles are stepped separately */
void phys_world_step(void)
{
    int i, k;
    Wd.clock++;
    for (i = 0; i < C.nsweep && i < 8; i++)
        Wd.sweep_angle[i] += (C.sweep[i].speed * 233L) >> 10;      /* deg/s -> per step, 8 fraction bits */
    for (i = 0; i < C.ncrush && i < 8; i++) {
        const Crusher *c = &C.crush[i];
        fix t, h;
        int low;
        Wd.crush_clock[i] += 82;                             /* 1/50 s */
        while (Wd.crush_clock[i] >= c->period) Wd.crush_clock[i] -= c->period;
        t = (Wd.crush_clock[i] << 12) / c->period;
        /* slow lift for three quarters of the cycle, then a fast drop */
        if (t < 3072) {
            fix x = (t << 12) / 3072;
            h = (((x * x) >> 12) * (3 * FX - 2 * x)) >> 12;
        } else {
            fix x = ((t - 3072) << 12) / 1024;
            h = FX - ((x * x) >> 12);
        }
        Wd.crush_h[i] = h;
        low = h < 492;
        if (low && !Wd.crush_slammed[i]) {
            int got = 0;
            Wd.crush_slammed[i] = 1;
            for (k = 0; k < phys_nmarbles; k++) {
                Ball *m = phys_marbles[k];
                fix dy = m->p.y - c->y, dx = (m->p.x - c->x) >> 4, dz = (m->p.z - c->z) >> 4, hw = (c->w / 2 + 1638) >> 4;
                if (!m->live || m->frozen) continue;
                if ((dy < 0 ? -dy : dy) < 6554 && dx * dx + dz * dz < hw * hw) {
                    m->kill = "CRUSHED";
                    got = 1;
                }
            }
            if (!got) phys_sound = SFX_THUD;
        } else if (!low) Wd.crush_slammed[i] = 0;
    }
    for (i = 0; i < C.ncrumble; i++) {
        Crumble *cr = &Wd.crumble[i];
        if (cr->state == 1 && --cr->timer <= 0) {
            cr->state = 2;
            cr->timer = 175;                                 /* back in 3.5 s */
            cr->fall = 0;
            phys_sound = SFX_THUD;
        } else if (cr->state == 2) {
            cr->fall += (175 - cr->timer) * 9;               /* drops away */
            if (--cr->timer <= 0) cr->state = 0;
        }
    }
    /* enemies */
    for (i = 0; i < Wd.nenemy; i++) {
        Enemy *e = &Wd.enemy[i];
        Ball *m = 0;
        long bestd = 0x7FFFFFFF;
        for (k = 0; k < phys_nmarbles; k++) {
            Ball *mk = phys_marbles[k];
            long dx = (mk->p.x - e->b.p.x) >> 6, dz = (mk->p.z - e->b.p.z) >> 6, dy = (mk->p.y - e->b.p.y) >> 6;
            long d = dx * dx + dy * dy + dz * dz;
            if (!mk->live || mk->frozen) continue;
            if (d < bestd) { bestd = d; m = mk; }
        }
        if (e->kind == 1) {
            /* blob: slides across the course, whoever is about */
            if (m) e->phase += STEP((e->speed * 1434) >> 12);    /* 0.35 x speed, radians */
            {
                long a = (e->phase * 10430L) >> 12;              /* radians -> 65536 per turn */
                fix s = (isin(a) * 13107L) >> 14;                /* 3.2 u */
                e->b.p.x = e->home.x + ((e->ax * s) >> 14);
                e->b.p.z = e->home.z + ((e->az * s) >> 14);
                e->b.p.y = e->home.y;
            }
            for (k = 0; k < phys_nmarbles; k++) {
                Ball *mk = phys_marbles[k];
                long dx = (mk->p.x - e->b.p.x) >> 4, dy = (mk->p.y - e->b.p.y) >> 4, dz = (mk->p.z - e->b.p.z) >> 4;
                long rr = (e->b.r + mk->r) >> 4;
                if (mk->live && !mk->frozen && dx * dx + dy * dy + dz * dz < rr * rr) mk->kill = "EATEN";
            }
            continue;
        }
        /* steel marble: rolls at you if you're near, home otherwise; asleep
         * (as PhysX would have it) when nobody's about and it's at rest */
        if (!m || bestd > (40L * 64) * (40L * 64)) {
            V3 vv = e->b.v;
            if (e->b.grounded && vlen(vv) < 410) continue;
        }
        if (m) {
            V3 target = (bestd >> 0) <= ((e->range >> 6) * (e->range >> 6)) ? m->p : e->home;
            long tx = target.x - e->b.p.x, tz = target.z - e->b.p.z;
            V3 t2;
            fix l;
            t2.x = tx; t2.y = 0; t2.z = tz;
            l = vlen(t2);
            if (l > 819) {
                e->b.v.x += STEP(e->speed) * (tx >> 4) / (l >> 4);
                e->b.v.z += STEP(e->speed) * (tz >> 4) / (l >> 4);
            }
        }
        e->b.v.x -= e->b.v.x / 250;                          /* drag 0.2 */
        e->b.v.z -= e->b.v.z / 250;
        e->b.v.y -= e->b.gravity;
        e->b.p.x += STEP(e->b.v.x); e->b.p.y += STEP(e->b.v.y); e->b.p.z += STEP(e->b.v.z);
        contact_ground = 0;
        hit_course(&e->b);
        hit_course(&e->b);
        e->b.grounded = contact_ground;
        if (e->b.p.y < C.kill_y + 6 * FX) {
            e->b.p = e->home;
            e->b.v.x = e->b.v.y = e->b.v.z = 0;
            e->b.w = e->b.v;
        }
    }
}

Ball *phys_marbles[2];
int phys_nmarbles;

/* marble against a steel marble: a proper collision, plus the shove */
static void hit_enemy(Ball *m, Enemy *e)
{
    long dx = m->p.x - e->b.p.x, dy = m->p.y - e->b.p.y, dz = m->p.z - e->b.p.z, nx, ny, nz;
    V3 d;
    fix len, rr = m->r + e->b.r, vn, j;
    d.x = dx; d.y = dy; d.z = dz;
    len = vlen(d);
    if (len >= rr || len < 16) { m->touching_enemy = 0; return; }
    nx = (dx << 12) / len; ny = (dy << 12) / len; nz = (dz << 12) / len;
    /* separate by inverse mass: the marble (1) moves 3x as far as the steel (3) */
    m->p.x += (nx * ((rr - len) * 3 / 4)) >> 12; m->p.y += (ny * ((rr - len) * 3 / 4)) >> 12; m->p.z += (nz * ((rr - len) * 3 / 4)) >> 12;
    e->b.p.x -= (nx * ((rr - len) / 4)) >> 12; e->b.p.z -= (nz * ((rr - len) / 4)) >> 12;
    d.x = m->v.x - e->b.v.x; d.y = m->v.y - e->b.v.y; d.z = m->v.z - e->b.v.z;
    vn = dotn(d, nx, ny, nz);
    if (vn < 0) {
        fix e_ = -vn > BOUNCE_MIN ? 614 : 0;                 /* max(0.15, 0.12) */
        j = (-vn - ((vn * e_) >> 12)) * 3 / 4;               /* / (1/1 + 1/3) */
        m->v.x += (nx * j) >> 12; m->v.y += (ny * j) >> 12; m->v.z += (nz * j) >> 12;
        e->b.v.x -= (nx * j / 3) >> 12; e->b.v.y -= (ny * j / 3) >> 12; e->b.v.z -= (nz * j / 3) >> 12;
    }
    if (!m->touching_enemy) {
        /* OnCollisionEnter: a proper shove, so a hit near an edge costs you */
        V3 h;
        fix hl;
        h.x = dx; h.y = 0; h.z = dz;
        hl = vlen(h);
        if (hl > 16) {
            m->v.x += 5 * ((dx << 12) / hl);
            m->v.z += 5 * ((dz << 12) / hl);
        }
        phys_sound = SFX_CLACK;
    }
    m->touching_enemy = 1;
}

static void collide(Ball *b)
{
    int i, iter, swept = 0;
    contact_ground = 0;
    tile_touched = 0;
    for (iter = 0; iter < 2; iter++) {
        hit_course(b);
        for (i = 0; i < C.npillar; i++) {
            const Pillar *pl = &C.pillar[i];
            long dx = b->p.x - pl->x, dz = b->p.z - pl->z, d2, rr = pl->r + b->r;
            if (b->p.y < pl->y - b->r || b->p.y > pl->y + pl->h + b->r) continue;
            d2 = (dx >> 4) * (dx >> 4) + (dz >> 4) * (dz >> 4);
            if (d2 >= (rr >> 4) * (rr >> 4)) continue;
            if (b->p.y > pl->y + pl->h && d2 < (pl->r >> 4) * (pl->r >> 4))
                contact(b, 0, FX, 0, pl->y + pl->h + b->r - b->p.y, S_BOUNCY, still, FX);
            else {
                fix l = (fix)isqrt((unsigned long)d2) << 4;
                if (l < 16) l = 16;
                contact(b, (dx << 12) / l, 0, (dz << 12) / l, rr - l, S_BOUNCY, still, FX);
            }
        }
        for (i = 0; i < C.nsweep && i < 8; i++) {
            OBB o = sweeper_box(i);
            V3 vs;
            /* the arm's surface moves: omega x r, about +y */
            long om = (C.sweep[i].speed * 71L) >> 12;            /* deg/s -> rad/s, Q12 */
            vs.x = (om * ((b->p.z - o.cz) >> 4)) >> 8;
            vs.y = 0;
            vs.z = -((om * ((b->p.x - o.cx) >> 4)) >> 8);
            if (hit_box(b, &o, vs, S_BOUNCY)) {
                swept = 1;
                if (!b->touch_sweeper) {
                    V3 h;
                    fix hl;
                    h.x = b->p.x - o.cx; h.y = 0; h.z = b->p.z - o.cz;
                    hl = vlen(h);
                    if (hl > 16) {
                        b->v.x += 7 * ((h.x << 12) / hl);
                        b->v.z += 7 * ((h.z << 12) / hl);
                    }
                    phys_sound = SFX_CLACK;
                    b->touch_sweeper = 1;
                }
            }
        }
        for (i = 0; i < C.ncrush && i < 8; i++) {
            OBB o = crusher_box(i);
            V3 vs;
            vs.x = vs.z = 0;
            vs.y = 0;
            hit_box(b, &o, vs, S_BOUNCY);
        }
    }
    if (!swept) b->touch_sweeper = 0;
    for (i = 0; i < Wd.nenemy; i++)
        if (Wd.enemy[i].kind == 0) hit_enemy(b, &Wd.enemy[i]);
    b->grounded = contact_ground;
    if (tile_touched) {
        Crumble *cr = &Wd.crumble[tile_touched - 1];
        cr->state = 1;
        cr->timer = 28;                                     /* 0.55 s, then it goes */
    }
}

void phys_marble_step(Ball *b, fix wx, fix wz)
{
    int i, sub;
    if (b->frozen) {
        b->v.x = b->v.y = b->v.z = 0;
        b->w = b->v;
        return;
    }
    /* input: camera-relative force, much weaker in the air */
    if (wx || wz) {
        fix a = b->grounded ? ACCEL : (ACCEL * 1229) >> 12;
        b->v.x += (wx * a) >> 12;
        b->v.z += (wz * a) >> 12;
    }
    b->v.y -= b->gravity;
    /* fans and boost strips */
    for (i = 0; i < C.nzone; i++)
        if (phys_overlap_obb(&C.zone[i].box, b->p, b->r)) {
            fix pw = STEP(C.zone[i].power);
            b->v.x += (C.zone[i].dir[0] * pw) >> 14;
            b->v.y += (C.zone[i].dir[1] * pw) >> 14;
            b->v.z += (C.zone[i].dir[2] * pw) >> 14;
        }
    /* cap the flat speed only; falling is as fast as it likes */
    {
        V3 f;
        fix l;
        f.x = b->v.x; f.y = 0; f.z = b->v.z;
        l = vlen(f);
        if (l > MAXSPEED) {
            b->v.x = (b->v.x >> 4) * (MAXSPEED >> 4) / (l >> 4) << 4;
            b->v.z = (b->v.z >> 4) * (MAXSPEED >> 4) / (l >> 4) << 4;
        }
    }
    b->w.x -= b->w.x / 1000;                                 /* angular damping 0.05 */
    b->w.y -= b->w.y / 1000;
    b->w.z -= b->w.z / 1000;
    /* move, in two halves when fast so thin kerbs can't be skipped */
    sub = vlen(b->v) > 12 * FX ? 2 : 1;
    for (i = 0; i < sub; i++) {
        b->p.x += STEP(b->v.x) / sub;
        b->p.y += STEP(b->v.y) / sub;
        b->p.z += STEP(b->v.z) / sub;
        collide(b);
    }
    if (b->grounded) b->last_ground = b->p;
    /* triggers */
    for (i = 0; i < C.nacid; i++)
        if (phys_overlap_obb(&C.acid[i], b->p, b->r)) b->kill = "DISSOLVED";
    if (phys_overlap_obb(&C.goal, b->p, b->r)) b->goal = 1;
}

/* the deck under a point (for the marble's shadow) */
int phys_ground_below(V3 p, fix *y)
{
    const Grid *g = &C.cgrid;
    long cx = (p.x - g->x0) >> 13, cz = (p.z - g->z0) >> 13, k, cell;
    int found = 0;
    fix best = 0;
    if (p.x < g->x0 || p.z < g->z0 || cx >= g->nx || cz >= g->nz) return 0;
    cell = cz * g->nx + cx;
    for (k = g->off[cell]; k < g->off[cell + 1]; k++) {
        const CTri *t = &C.t[g->idx[k]];
        const V3 *a = &C.v[t->v[0]], *b = &C.v[t->v[1]], *c = &C.v[t->v[2]];
        long s1, s2, s3;
        fix h;
        int group = (int)(t->flags >> 8);
        if (t->n[1] < 1024) continue;                        /* only things you can stand on */
        if (group && Wd.crumble[group - 1].state == 2) continue;
        s1 = cross2(b->x - a->x, b->z - a->z, p.x - a->x, p.z - a->z);
        s2 = cross2(c->x - b->x, c->z - b->z, p.x - b->x, p.z - b->z);
        s3 = cross2(a->x - c->x, a->z - c->z, p.x - c->x, p.z - c->z);
        if (!((s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0))) continue;
        h = a->y - ((t->n[0] * ((p.x - a->x) >> 2) + t->n[2] * ((p.z - a->z) >> 2)) / t->n[1] << 2);
        if (h <= p.y + 256 && (!found || h > best)) { best = h; found = 1; }
    }
    *y = best;
    return found;
}
