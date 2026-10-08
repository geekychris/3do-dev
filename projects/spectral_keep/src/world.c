/*
 * Tiny kinematic physics for one room (Physics.cs): axis-separated moves
 * against solid boxes, gravity, pushing and lift-riding. Fixed point, Q12.
 *
 * The room's stone and brick blocks are a grid rather than bodies, so a
 * move only looks at the few cells its swept box touches.
 */
#include "keep.h"

#define EPS        4                /* Box.Overlaps eps 0.001 */
#define EPS_GROUND 2                /* 0.0005 */
#define GRAVITY    36               /* 22 u/s^2 */
#define MAXFALL    1147             /* 14 u/s */

Body w_body[MAXBODIES];
Body w_gridbody;
static unsigned char grid[MAXH][MAXW][MAXW];
static int gw, gd;

void w_reset(int w, int d)
{
    int i, x, y, z;
    for (i = 0; i < MAXBODIES; i++) w_body[i].used = 0;
    for (y = 0; y < MAXH; y++)
        for (z = 0; z < MAXW; z++)
            for (x = 0; x < MAXW; x++) grid[y][z][x] = 0;
    gw = w;
    gd = d;
    w_gridbody.used = 1;
    w_gridbody.solid = 1;
    w_gridbody.owner = -1;
}

void w_set_block(int x, int y, int z) { grid[y][z][x] = 1; }

int w_block(int x, int y, int z)
{
    if (x < 0 || x >= gw || z < 0 || z >= gd || y < 0 || y >= MAXH) return 0;
    return grid[y][z][x];
}

static Body *alloc_body(void)
{
    int i;
    for (i = 0; i < MAXBODIES; i++)
        if (!w_body[i].used) {
            Body *b = &w_body[i];
            b->px = b->py = b->pz = 0;
            b->sx = b->sy = b->sz = FX;
            b->vx = b->vy = b->vz = 0;
            b->used = 1;
            b->solid = 1;
            b->dynamic = b->pushable = b->player = b->npcbar = b->grounded = b->harm = 0;
            b->collides = 1;
            b->ground = 0;
            b->owner = -1;
            return b;
        }
    return &w_body[MAXBODIES - 1];        /* never in practice: rooms are small */
}

Body *w_add(fix px, fix py, fix pz, fix sx, fix sy, fix sz)
{
    Body *b = alloc_body();
    b->px = px; b->py = py; b->pz = pz;
    b->sx = sx; b->sy = sy; b->sz = sz;
    return b;
}

Body *w_add_static(fix x0, fix y0, fix z0, fix x1, fix y1, fix z1)
{
    return w_add((x0 + x1) / 2, y0, (z0 + z1) / 2, x1 - x0, y1 - y0, z1 - z0);
}

void w_remove(Body *b)
{
    int i;
    b->used = 0;
    for (i = 0; i < MAXBODIES; i++)
        if (w_body[i].ground == b) w_body[i].ground = 0;
}

void body_box(const Body *b, Box *o)
{
    o->x0 = b->px - (b->sx >> 1); o->x1 = o->x0 + b->sx;
    o->y0 = b->py;                o->y1 = b->py + b->sy;
    o->z0 = b->pz - (b->sz >> 1); o->z1 = o->z0 + b->sz;
}

int box_overlap(const Box *a, const Box *b, fix eps)
{
    return a->x0 < b->x1 - eps && a->x1 > b->x0 + eps &&
           a->y0 < b->y1 - eps && a->y1 > b->y0 + eps &&
           a->z0 < b->z1 - eps && a->z1 > b->z0 + eps;
}

void box_shrink(Box *b, int pct)
{
    fix cx = (b->x0 + b->x1) / 2, cy = (b->y0 + b->y1) / 2, cz = (b->z0 + b->z1) / 2;
    fix hx = (b->x1 - b->x0) * pct / 200, hy = (b->y1 - b->y0) * pct / 200, hz = (b->z1 - b->z0) * pct / 200;
    b->x0 = cx - hx; b->x1 = cx + hx;
    b->y0 = cy - hy; b->y1 = cy + hy;
    b->z0 = cz - hz; b->z1 = cz + hz;
}

void box_grow(Box *b, fix g)
{
    b->x0 -= g; b->y0 -= g; b->z0 -= g;
    b->x1 += g; b->y1 += g; b->z1 += g;
}

static int ffloor(fix v) { return v >= 0 ? (int)(v >> 12) : -(int)((-v + FX - 1) >> 12); }

/* every solid thing whose box roughly meets `r`: bodies, then grid cells */
#define MAXCAND 48
static Box   cand_box[MAXCAND];
static Body *cand_body[MAXCAND];

static int gather(const Box *r, const Body *self, int skip_npcbar)
{
    int i, n = 0, x, y, z, x0, x1, y0, y1, z0, z1;
    for (i = 0; i < MAXBODIES && n < MAXCAND; i++) {
        Body *o = &w_body[i];
        if (!o->used || o == self || !o->solid || (o->npcbar && skip_npcbar)) continue;
        /* monsters are deadly to touch, so they must be able to touch you
         * (in the Unity game they block each other and only ghosts kill) */
        if (self && ((self->player && o->harm) || (self->harm && o->player))) continue;
        body_box(o, &cand_box[n]);
        if (cand_box[n].x0 > r->x1 || cand_box[n].x1 < r->x0 || cand_box[n].y0 > r->y1 ||
            cand_box[n].y1 < r->y0 || cand_box[n].z0 > r->z1 || cand_box[n].z1 < r->z0) continue;
        cand_body[n++] = o;
    }
    x0 = ffloor(r->x0); x1 = ffloor(r->x1);
    y0 = ffloor(r->y0); y1 = ffloor(r->y1);
    z0 = ffloor(r->z0); z1 = ffloor(r->z1);
    if (x0 < 0) x0 = 0;
    if (x1 > gw - 1) x1 = gw - 1;
    if (y0 < 0) y0 = 0;
    if (y1 > MAXH - 1) y1 = MAXH - 1;
    if (z0 < 0) z0 = 0;
    if (z1 > gd - 1) z1 = gd - 1;
    for (y = y0; y <= y1; y++)
        for (z = z0; z <= z1; z++)
            for (x = x0; x <= x1; x++)
                if (grid[y][z][x] && n < MAXCAND) {
                    Box *c = &cand_box[n];
                    c->x0 = (fix)x << 12; c->x1 = c->x0 + FX;
                    c->y0 = (fix)y << 12; c->y1 = c->y0 + FX;
                    c->z0 = (fix)z << 12; c->z1 = c->z0 + FX;
                    cand_body[n++] = &w_gridbody;
                }
    return n;
}

static fix *pos(Body *b, int axis) { return axis == 0 ? &b->px : axis == 1 ? &b->py : &b->pz; }
static fix lo(const Box *b, int axis) { return axis == 0 ? b->x0 : axis == 1 ? b->y0 : b->z0; }
static fix hi(const Box *b, int axis) { return axis == 0 ? b->x1 : axis == 1 ? b->y1 : b->z1; }

static void extend(Box *b, int axis, fix d)
{
    if (axis == 0) { if (d > 0) b->x1 += d; else b->x0 += d; }
    else if (axis == 1) { if (d > 0) b->y1 += d; else b->y0 += d; }
    else { if (d > 0) b->z1 += d; else b->z0 += d; }
}

static fix move_axis(Body *b, int axis, fix d, int canpush, int depth)
{
    Box start, swept, ob;
    fix allowed, limit;
    Body *hit = 0;
    int i, n;
    if (d == 0) return 0;
    if (!b->collides) { *pos(b, axis) += d; return d; }
    body_box(b, &start);
    swept = start;
    extend(&swept, axis, d);
    /* shove anything pushable in the way first */
    if (canpush && axis != 1 && depth < 3)
        for (i = 0; i < MAXBODIES; i++) {
            Body *o = &w_body[i];
            fix gap;
            if (!o->used || o == b || !o->pushable || !o->solid) continue;
            body_box(o, &ob);
            if (!box_overlap(&swept, &ob, EPS) || box_overlap(&start, &ob, EPS)) continue;
            if (ob.y0 >= start.y1 - 205 || ob.y1 <= start.y0 + 205) continue;   /* above or below us */
            gap = d > 0 ? lo(&ob, axis) - hi(&start, axis) : hi(&ob, axis) - lo(&start, axis);
            move_axis(o, axis, d - gap, 1, depth + 1);
        }
    allowed = d;
    n = gather(&swept, b, b->player);
    for (i = 0; i < n; i++) {
        Box *c = &cand_box[i];
        if (!box_overlap(&swept, c, EPS)) continue;
        if (box_overlap(&start, c, EPS)) continue;   /* already inside (spawned overlapping): let it escape */
        limit = d > 0 ? lo(c, axis) - hi(&start, axis) : hi(c, axis) - lo(&start, axis);
        if (d > 0 ? limit < allowed : limit > allowed) {
            allowed = limit;
            hit = cand_body[i];
        }
    }
    if (d > 0) { if (allowed < 0) allowed = 0; }
    else if (allowed > 0) allowed = 0;
    *pos(b, axis) += allowed;
    if (axis == 1 && hit && d < 0) b->ground = hit;
    return allowed;
}

static void probe_ground(Body *b)
{
    Box probe;
    int i, n;
    if (!b->collides) return;
    body_box(b, &probe);
    probe.y1 = probe.y0;
    probe.y0 -= 82;                       /* 0.02 */
    n = gather(&probe, b, 1);
    for (i = 0; i < n; i++)
        if (box_overlap(&probe, &cand_box[i], EPS_GROUND)) {
            b->grounded = 1;
            b->ground = cand_body[i];
            return;
        }
}

void w_move(Body *b, fix dx, fix dy, fix dz, int canpush, fix *moved)
{
    fix my;
    moved[0] = move_axis(b, 0, dx, canpush, 0);
    moved[2] = move_axis(b, 2, dz, canpush, 0);
    b->grounded = 0;
    b->ground = 0;
    my = move_axis(b, 1, dy, 0, 0);
    moved[1] = my;
    if (dy <= 0 && my > dy) b->grounded = 1;
    if (dy > 0 && my < dy && b->vy > 0) b->vy = 0;             /* bonked our head */
    if (!b->grounded && dy <= 0) probe_ground(b);
}

void w_step(Body *b, int canpush, fix *moved)
{
    b->vy -= GRAVITY;
    if (b->vy < -MAXFALL) b->vy = -MAXFALL;
    w_move(b, b->vx, b->vy, b->vz, canpush, moved);
    if (b->grounded && b->vy < 0) b->vy = 0;
}

void w_move_kinematic(Body *lift, fix dy)
{
    Body *riders[MAXBODIES];
    Box top, ob;
    fix moved[3];
    int i, n = 0;
    body_box(lift, &top);
    top.y0 = top.y1;
    top.y1 += 205;                        /* 0.05 */
    for (i = 0; i < MAXBODIES; i++) {
        Body *o = &w_body[i];
        if (!o->used || o == lift || !o->dynamic || !o->collides) continue;
        body_box(o, &ob);
        if (o->ground == lift || box_overlap(&top, &ob, EPS_GROUND)) riders[n++] = o;
    }
    if (dy > 0)
        for (i = 0; i < n; i++) w_move(riders[i], 0, dy, 0, 0, moved);
    lift->py += dy;
    if (dy <= 0)
        for (i = 0; i < n; i++) {
            w_move(riders[i], 0, dy, 0, 0, moved);
            riders[i]->grounded = 1;
            riders[i]->ground = lift;
        }
}

int w_overlapping_solid(const Box *box, const Body *except)
{
    int i, n = gather(box, except, 0);
    for (i = 0; i < n; i++)
        if (box_overlap(box, &cand_box[i], EPS)) return 1;
    return 0;
}

fix w_ground_below(fix x, fix y, fix z)
{
    fix best = 0;
    int cx = ffloor(x), cz = ffloor(z), cy, i;
    for (cy = 0; cy < MAXH; cy++)
        if (w_block(cx, cy, cz) && ((fix)(cy + 1) << 12) <= y + 410 && ((fix)(cy + 1) << 12) > best)
            best = (fix)(cy + 1) << 12;
    for (i = 0; i < MAXBODIES; i++) {
        Body *o = &w_body[i];
        Box b;
        if (!o->used || !o->solid || o->npcbar || (o->owner >= 0 && !o->pushable)) continue;
        body_box(o, &b);
        if (x > b.x0 && x < b.x1 && z > b.z0 && z < b.z1 && b.y1 <= y + 410 && b.y1 > best) best = b.y1;
    }
    return best;
}
