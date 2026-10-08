/*
 * One room, built from its level data and simulated while the player is in
 * it (RoomView.cs), and everything that moves in it (Actors.cs). Leaving and
 * re-entering rebuilds the room, so crates and enemies reset, as in the
 * 8-bit originals.
 */
#include "keep.h"

RoomState R;

#define WALK_SPEED   254           /* 3.1 u/s */
#define JUMP_SPEED   606           /* 7.4 u/s */
#define LIFT_SPEED   106           /* 1.3 u/s */
#define LIFT_PAUSE   45            /* 0.9 s */
#define LIFT_TRAVEL  (2 * FX)
#define TALL         (30 * FX)

/* ---- small maths ---- */

static unsigned long isqrt(unsigned long v)
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

static fix flat_len(fix x, fix z)          /* |(x, z)|, Q12 in and out */
{
    long a = x >> 2, b = z >> 2;
    return (fix)isqrt((unsigned long)(a * a + b * b)) << 2;
}

/* sin of a 256-per-circle angle, Q14 */
static const short qsin[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384
};
long isin256(int a)
{
    int q = (a >> 6) & 3, i = a & 63;
    long v = (q & 1) ? qsin[64 - i] : qsin[i];
    return (q & 2) ? -v : v;
}

static int octant(fix vx, fix vz)
{
    fix ax = vx < 0 ? -vx : vx, az = vz < 0 ? -vz : vz;
    if (ax * 10 < az * 4) return vz > 0 ? 0 : 4;      /* within 22 degrees of an axis */
    if (az * 10 < ax * 4) return vx > 0 ? 2 : 6;
    if (vx > 0) return vz > 0 ? 1 : 3;
    return vz > 0 ? 7 : 5;
}

/* ---- level helpers (LevelData.cs) ---- */

int lv_room_at(const KLevel *l, int gx, int gy)
{
    int i;
    for (i = 0; i < l->nrooms; i++)
        if (l->rooms[i].gx == gx && l->rooms[i].gy == gy) return i;
    return -1;
}

static const int side_dx[4] = { 0, 1, 0, -1 }, side_dy[4] = { 1, 0, -1, 0 };
static const char side_ch[4] = { 'N', 'E', 'S', 'W' };

int lv_neighbour(const KLevel *l, int room, int side)
{
    return lv_room_at(l, l->rooms[room].gx + side_dx[side], l->rooms[room].gy + side_dy[side]);
}

static int sealed(const KRoom *r, int side)
{
    const char *s;
    for (s = r->seal; s && *s; s++)
        if (*s == side_ch[side]) return 1;
    return 0;
}

int lv_has_door(const KLevel *l, int room, int side)
{
    int n = lv_neighbour(l, room, side);
    return n >= 0 && !sealed(&l->rooms[room], side) && !sealed(&l->rooms[n], (side + 2) & 3);
}

void door_span(int len, int *lo, int *hi)
{
    *lo = len / 2 - 1;
    *hi = *lo + 2;
}

int lv_count(const KLevel *l, int c)
{
    int i, k, n = 0;
    for (i = 0; i < l->nrooms; i++) {
        const KRoom *r = &l->rooms[i];
        for (k = 0; k < MAXH * r->w * r->d; k++)
            if (r->cells[k] == c) n++;
    }
    return n;
}

/* ---- construction ---- */

static Actor *new_actor(int kind, fix x, fix y, fix z, fix sx, fix sy, fix sz)
{
    Actor *a = &R.act[R.nact];
    a->kind = kind;
    a->body = w_add(x, y, z, sx, sy, sz);
    a->body->dynamic = 1;
    a->body->owner = R.nact;
    a->dx = a->dz = 0;
    a->awake = 0;
    a->bob = a->last_talk = 0;
    a->text = 0;
    a->face = 4;                          /* Vector3.back */
    a->phase = 0;
    a->roll = 0;
    a->invuln = a->coyote = a->jumpbuf = 0;
    a->was_grounded = 1;
    a->visible = 1;
    R.nact++;
    return a;
}

/* collision for one wall: columns either side of a doorway, the lintel and
 * an invisible top so nobody hops over; front walls are not drawn */
static void wall(int side)
{
    const KRoom *r = R.room;
    int alongx = side == SIDE_N || side == SIDE_S, len = alongx ? r->w : r->d, lo, hi, door;
    fix W = (fix)r->w << 12, D = (fix)r->d << 12;
    fix a[4], b[4], y0[4], y1[4];
    int n, i;
    door_span(len, &lo, &hi);
    door = lv_has_door(R.level, R.ridx, side);
    if (door) {
        a[0] = -FX;            b[0] = (fix)lo << 12;          y0[0] = 0;      y1[0] = TALL;
        a[1] = (fix)hi << 12;  b[1] = (fix)(len + 1) << 12;   y0[1] = 0;      y1[1] = TALL;
        a[2] = (fix)lo << 12;  b[2] = (fix)hi << 12;          y0[2] = 2 * FX; y1[2] = TALL;
        a[3] = (fix)lo << 12;  b[3] = (fix)hi << 12;          y0[3] = 0;      y1[3] = 2 * FX;
        n = 4;
    } else {
        a[0] = -FX; b[0] = (fix)(len + 1) << 12; y0[0] = 0; y1[0] = TALL;
        n = 1;
    }
    for (i = 0; i < n; i++) {
        Body *o;
        switch (side) {
        case SIDE_N: o = w_add_static(a[i], y0[i], D, b[i], y1[i], D + FX); break;
        case SIDE_E: o = w_add_static(W, y0[i], a[i], W + FX, y1[i], b[i]); break;
        case SIDE_S: o = w_add_static(a[i], y0[i], -FX, b[i], y1[i], 0); break;
        default:     o = w_add_static(-FX, y0[i], a[i], 0, y1[i], b[i]); break;
        }
        if (i == 3) o->npcbar = 1;
    }
}

void room_build(const KLevel *l, int ridx, int host)
{
    const KRoom *r = &l->rooms[ridx];
    int x, y, z, i, gate_open;
    R.level = l;
    R.room = r;
    R.ridx = ridx;
    R.host = host;
    R.nact = 0;
    R.player = -1;
    R.nlift = R.npk = R.nhaz = R.ngate = 0;
    R.has_throne = 0;
    R.clock = 0;
    R.exited = 0;
    w_reset(r->w, r->d);

    /* the shell */
    w_add_static(-2 * FX, -FX, -2 * FX, (fix)(r->w + 2) << 12, 0, (fix)(r->d + 2) << 12);
    wall(SIDE_N);
    wall(SIDE_E);
    wall(SIDE_S);
    wall(SIDE_W);
    w_add_static((fix)r->w << 12, 0, (fix)r->d << 12, (fix)(r->w + 1) << 12, TALL, (fix)(r->d + 1) << 12);

    /* the cells */
    gate_open = host && host_gate_open(ridx);
    for (y = 0; y < MAXH; y++)
        for (z = 0; z < r->d; z++)
            for (x = 0; x < r->w; x++) {
                int c = CELL(r, x, y, z), cell = (y * r->d + z) * r->w + x;
                fix bx = ((fix)x << 12) + FX / 2, by = (fix)y << 12, bz = ((fix)z << 12) + FX / 2;
                switch (c) {
                case '#':
                case 'B':
                    w_set_block(x, y, z);
                    break;
                case 'C':
                    if (R.nact < MAXACT) {
                        Actor *a = new_actor(K_CRATE, bx, by, bz, 3932, 3932, 3932);   /* 0.96 */
                        a->body->pushable = 1;
                    }
                    break;
                case '^':
                    if (R.nhaz < MAXHAZ) {
                        Box *h = &R.haz[R.nhaz++];
                        h->x0 = ((fix)x << 12) + 410; h->x1 = ((fix)x << 12) + 3686;
                        h->y0 = by;                   h->y1 = by + 1638;
                        h->z0 = ((fix)z << 12) + 410; h->z1 = ((fix)z << 12) + 3686;
                    }
                    break;
                case 'L':
                    if (R.nlift < MAXLIFT) {
                        Lift *lf = &R.lift[R.nlift++];
                        lf->body = w_add(bx, by, bz, 4014, FX / 4, 4014);   /* 0.98 x 0.25 */
                        lf->base = by;
                        lf->dir = 1;
                        lf->wait = LIFT_PAUSE;
                    }
                    break;
                case 'G':
                    if (!gate_open && R.ngate < MAXGATE) {
                        Gate *g = &R.gate[R.ngate++];
                        g->body = w_add_static((fix)x << 12, by, (fix)z << 12, (fix)(x + 1) << 12,
                                               by + 2 * FX, (fix)(z + 1) << 12);
                        g->x = x; g->y = y; g->z = z;
                        g->rot = x == 0 || x == r->w - 1;     /* face along the passage */
                    }
                    break;
                case 'R':
                case 'P':
                case 'K':
                    if ((host && host_taken(ridx, cell)) || R.npk >= MAXPICK) break;
                    {
                        Pickup *p = &R.pk[R.npk++];
                        p->kind = c;
                        p->x = x; p->y = y; p->z = z;
                        p->cell = cell;
                        p->box.x0 = bx - 1434; p->box.x1 = bx + 1434;   /* 0.35 */
                        p->box.y0 = by;        p->box.y1 = by + 3686;   /* 0.9 */
                        p->box.z0 = bz - 1434; p->box.z1 = bz + 1434;
                    }
                    break;
                case 'T':
                    R.has_throne = 1;
                    R.tx = x; R.ty = y; R.tz = z;
                    R.throne.x0 = ((fix)x << 12) - 205; R.throne.x1 = ((fix)(x + 1) << 12) + 205;
                    R.throne.y0 = by;                   R.throne.y1 = by + 4915;     /* 1.2 */
                    R.throne.z0 = ((fix)z << 12) - 205; R.throne.z1 = ((fix)(z + 1) << 12) + 205;
                    w_add_static(((fix)x << 12) + 410, by, ((fix)z << 12) + 410,
                                 ((fix)x << 12) + 3686, by + 1638, ((fix)z << 12) + 3686);
                    break;
                }
            }

    /* the actors */
    for (i = 0; i < r->nactors && R.nact < MAXACT; i++) {
        const KActor *k = &r->actors[i];
        fix px = ((fix)k->x << 12) + FX / 2, py = (fix)k->y << 12, pz = ((fix)k->z << 12) + FX / 2;
        Actor *a;
        switch (k->type) {
        case AT_GUARD:
            a = new_actor(K_GUARD, px, py, pz, 2458, 3891, 2458);      /* 0.6 x 0.95 */
            a->body->harm = 1;
            if (k->alongz) a->dz = FX; else a->dx = FX;
            break;
        case AT_HOUND:
            new_actor(K_HOUND, px, py, pz, 2048, 2867, 2048)->body->harm = 1;   /* 0.5 x 0.7 */
            break;
        case AT_GHOST:
            a = new_actor(K_GHOST, px, py, pz, 2253, 3277, 2253);      /* 0.55 x 0.8 */
            a->body->collides = a->body->solid = a->body->dynamic = 0;
            a->bob = (long)k->x * 50 + 25;                             /* bob = pos.x seconds */
            break;
        case AT_BOUNCER:
            a = new_actor(K_BOUNCER, px, py, pz, 2458, 2540, 2458);    /* 0.6 x 0.62 */
            a->body->harm = 1;
            if (k->alongz) { a->dx = -2107; a->dz = 3512; }            /* (-0.6, 1) normalised */
            else { a->dx = 3512; a->dz = 2107; }
            break;
        case AT_SAGE:
            a = new_actor(K_SAGE, px, py, pz, 2458, 4506, 2458);       /* 0.6 x 1.1 */
            a->text = k->text && k->text[0] ? k->text : "HELLO TRAVELLER.";
            a->last_talk = -99 * 50;
            break;
        }
    }
}

void room_spawn_player(fix x, fix y, fix z)
{
    Actor *a;
    if (R.nact >= MAXACT) return;
    a = new_actor(K_PLAYER, x, y, z, 2294, 3686, 2294);                 /* 0.56 x 0.9 */
    a->body->player = 1;
    R.player = R.nact - 1;
    R.exited = 0;
}

/* ---- simulation ---- */

static void pose(Actor *a, fix vx, fix vz, int walks)    /* v in u/s, Q12 */
{
    fix speed = flat_len(vx, vz);
    if (speed > 410) a->face = octant(vx, vz);           /* sqrMagnitude > 0.01 */
    if (!walks) return;
    if (speed > 410) a->phase += (unsigned int)((speed * 939) >> 12);   /* 4.5 rad per unit */
    else a->phase = 0;
}

static void walk(Actor *a, fix wx, fix wz, fix speed, int push, fix *moved)
{
    a->body->vx = (wx * speed) >> 12;
    a->body->vz = (wz * speed) >> 12;
    w_step(a->body, push, moved);
}

static void player_pos(const Actor *self, fix *x, fix *y, fix *z)
{
    const Body *b = R.player >= 0 ? R.act[R.player].body : self->body;
    *x = b->px; *y = b->py; *z = b->pz;
}

static int ground_ahead(Actor *a)
{
    Box b, ahead;
    fix ox = (a->dx * 1843) >> 12, oz = (a->dz * 1843) >> 12;     /* 0.45 */
    body_box(a->body, &b);
    ahead.x0 = b.x0 + ox; ahead.x1 = b.x1 + ox;
    ahead.z0 = b.z0 + oz; ahead.z1 = b.z1 + oz;
    ahead.y0 = b.y0 - 614;                                        /* 0.15 */
    ahead.y1 = b.y0 - 41;
    return w_overlapping_solid(&ahead, a->body);
}

static void tick_actor(Actor *a)
{
    fix moved[3], px, py, pz, tx, tz, len;
    switch (a->kind) {
    case K_GUARD:
        if (a->body->grounded && !ground_ahead(a)) { a->dx = -a->dx; a->dz = -a->dz; }
        walk(a, a->dx, a->dz, 131, 0, moved);                      /* 1.6 u/s */
        {
            fix along = (moved[0] * a->dx + moved[2] * a->dz) >> 12;
            if (along < 0) along = -along;
            if (along < 65) { a->dx = -a->dx; a->dz = -a->dz; }
        }
        pose(a, a->dx, a->dz, 1);
        break;
    case K_HOUND:
        player_pos(a, &px, &py, &pz);
        tx = px - a->body->px;
        tz = pz - a->body->pz;
        len = flat_len(tx, tz);
        if (!a->awake && R.player >= 0 && len < 14336) {           /* 3.5 */
            a->awake = 1;
            if (R.host) host_sound(SFX_BARK);
        }
        if (a->awake && len > 410) { tx = tx * FX / len; tz = tz * FX / len; }
        else tx = tz = 0;
        walk(a, tx, tz, 119, 0, moved);                            /* 1.45 u/s */
        pose(a, tx, tz, 1);
        break;
    case K_GHOST:
        a->bob++;
        player_pos(a, &px, &py, &pz);
        tx = px - a->body->px;
        tz = pz - a->body->pz;
        len = flat_len(tx, tz);
        if (len > 819) { tx = tx * 74 / len; tz = tz * 74 / len; }   /* 0.9 u/s */
        else tx = tz = 0;
        {
            Body *b = a->body;
            fix lo = 1229, hix = ((fix)R.room->w << 12) - 1229, hiz = ((fix)R.room->d << 12) - 1229;
            b->px += tx;
            b->pz += tz;
            if (b->px < lo) b->px = lo;
            if (b->px > hix) b->px = hix;
            if (b->pz < lo) b->pz = lo;
            if (b->pz > hiz) b->pz = hiz;
            b->py = (py > 0 ? py / 2 : 0) + 1024 + ((isin256((int)((a->bob * 2037) / 1000)) * 819) >> 14);
        }
        pose(a, tx * 50, tz * 50, 0);
        break;
    case K_BOUNCER:
        walk(a, a->dx, a->dz, 172, 0, moved);                      /* 2.1 u/s */
        {
            fix wx = (a->dx * 172) >> 12, wz = (a->dz * 172) >> 12;
            if ((moved[0] < 0 ? -moved[0] : moved[0]) * 2 < (wx < 0 ? -wx : wx)) a->dx = -a->dx;
            if ((moved[2] < 0 ? -moved[2] : moved[2]) * 2 < (wz < 0 ? -wz : wz)) a->dz = -a->dz;
        }
        if (a->body->grounded) a->body->vy = 508;                  /* 6.2 u/s */
        a->phase += 136;                                           /* 300 degrees/s, frames 11.25 apart */
        a->roll = (int)(a->phase >> 8) & 7;
        break;
    case K_SAGE:
        w_step(a->body, 0, moved);
        player_pos(a, &px, &py, &pz);
        pose(a, (px - a->body->px) / 20, (pz - a->body->pz) / 20, 0);
        break;
    case K_CRATE:
        a->body->vx = a->body->vz = 0;
        w_step(a->body, 0, moved);
        break;
    }
}

static void drive_player(Actor *a, fix mx, fix mz, int jump)
{
    fix moved[3], len = flat_len(mx, mz);
    if (a->invuln > 0) a->invuln--;
    a->coyote = a->body->grounded ? 5 : a->coyote - 1;
    a->jumpbuf = jump ? 7 : a->jumpbuf - 1;
    if (a->jumpbuf > 0 && a->coyote > 0) {
        a->body->vy = JUMP_SPEED;
        a->jumpbuf = a->coyote = 0;
        host_sound(SFX_JUMP);
    }
    if (len > FX) { mx = mx * FX / len; mz = mz * FX / len; }
    walk(a, mx, mz, WALK_SPEED, 1, moved);
    if (a->body->grounded && !a->was_grounded) host_sound(SFX_LAND);
    a->was_grounded = a->body->grounded;
    pose(a, a->body->vx * 50, a->body->vz * 50, 1);
    a->visible = a->invuln <= 0 || (a->invuln % 10) < 6;
}

static void tick_lift(Lift *l)
{
    fix target, y, step;
    if (l->wait > 0) { l->wait--; return; }
    target = l->base + (l->dir > 0 ? LIFT_TRAVEL : 0);
    y = l->body->py;
    step = target - y;
    if (step > LIFT_SPEED) step = LIFT_SPEED;
    if (step < -LIFT_SPEED) step = -LIFT_SPEED;
    w_move_kinematic(l->body, step);
    if (l->body->py == target) { l->dir = -l->dir; l->wait = LIFT_PAUSE; }
}

static int check_exit(void)
{
    const Body *b = R.act[R.player].body;
    int side = -1;
    if (b->px > ((fix)R.room->w << 12) + 82) side = SIDE_E;
    else if (b->px < -82) side = SIDE_W;
    else if (b->pz > ((fix)R.room->d << 12) + 82) side = SIDE_N;
    else if (b->pz < -82) side = SIDE_S;
    if (side < 0) return 0;
    R.exited = 1;
    host_exit(side, b->px, b->py, b->pz);
    return 1;
}

static const char *actor_name(int kind)
{
    switch (kind) {
    case K_GUARD: return "A GUARD";
    case K_HOUND: return "THE HOUND";
    case K_GHOST: return "A GHOST";
    case K_BOUNCER: return "A BOUNCER";
    }
    return "";
}

void room_tick(fix mx, fix mz, int jump)
{
    Box pb, hurt, ab, reach;
    Actor *p;
    int i;
    R.clock++;
    for (i = 0; i < R.nlift; i++) tick_lift(&R.lift[i]);
    if (R.player >= 0) drive_player(&R.act[R.player], mx, mz, jump);
    for (i = 0; i < R.nact; i++)
        if (i != R.player) tick_actor(&R.act[i]);
    if (R.player < 0 || R.exited || !R.host) return;
    p = &R.act[R.player];
    body_box(p->body, &pb);
    if (check_exit()) return;
    for (i = R.npk - 1; i >= 0; i--) {
        Pickup k;
        if (!box_overlap(&R.pk[i].box, &pb, 4)) continue;
        k = R.pk[i];
        R.pk[i] = R.pk[--R.npk];
        host_pickup(k.kind, R.ridx, k.cell);
    }
    if (R.has_throne && box_overlap(&R.throne, &pb, 4)) host_throne();
    if (R.ngate > 0) {
        reach = pb;
        box_grow(&reach, 328);                                     /* 0.08 */
        for (i = 0; i < R.ngate; i++) {
            Box gb;
            body_box(R.gate[i].body, &gb);
            if (box_overlap(&reach, &gb, 4) && host_try_key(R.ridx)) {
                int k;
                for (k = 0; k < R.ngate; k++) w_remove(R.gate[k].body);
                R.ngate = 0;
                break;
            }
        }
    }
    if (p->invuln > 0) return;
    hurt = pb;
    box_shrink(&hurt, 80);
    for (i = 0; i < R.nhaz; i++)
        if (box_overlap(&hurt, &R.haz[i], 4)) { host_death("SPIKES"); return; }
    for (i = 0; i < R.nact; i++) {
        Actor *a = &R.act[i];
        if (a->kind < K_GUARD || a->kind > K_BOUNCER) continue;
        body_box(a->body, &ab);
        box_shrink(&ab, 80);
        if (!box_overlap(&hurt, &ab, 4)) continue;
        host_death(actor_name(a->kind));
        return;
    }
    reach = pb;
    box_grow(&reach, 1229);                                        /* 0.3 */
    for (i = 0; i < R.nact; i++) {
        Actor *a = &R.act[i];
        if (a->kind != K_SAGE) continue;
        body_box(a->body, &ab);
        if (box_overlap(&reach, &ab, 4) && R.clock - a->last_talk >= 200) {   /* 4 s */
            a->last_talk = R.clock;
            host_talk(a->text);
            keep_log("talk %s\n", R.room->id);
        }
    }
}

/* where the player appears entering `to` through a side, given where they
 * left `from` (RoomView.EntryPoint) */
void room_entry_point(const KRoom *from, const KRoom *to, int side, fix *x, fix *y, fix *z)
{
    const fix inset = 1638;                                        /* 0.4 */
    fix px = *x, pz = *z;
    int lo, hi, alongx = side == SIDE_N || side == SIDE_S;
    switch (side) {
    case SIDE_E: px = inset; pz = *z - ((fix)from->d << 11) + ((fix)to->d << 11); break;
    case SIDE_W: px = ((fix)to->w << 12) - inset; pz = *z - ((fix)from->d << 11) + ((fix)to->d << 11); break;
    case SIDE_N: pz = inset; px = *x - ((fix)from->w << 11) + ((fix)to->w << 11); break;
    default:     pz = ((fix)to->d << 12) - inset; px = *x - ((fix)from->w << 11) + ((fix)to->w << 11); break;
    }
    door_span(alongx ? to->w : to->d, &lo, &hi);
    if (alongx) {
        if (px < ((fix)lo << 12) + 1434) px = ((fix)lo << 12) + 1434;
        if (px > ((fix)hi << 12) - 1434) px = ((fix)hi << 12) - 1434;
    } else {
        if (pz < ((fix)lo << 12) + 1434) pz = ((fix)lo << 12) + 1434;
        if (pz > ((fix)hi << 12) - 1434) pz = ((fix)hi << 12) - 1434;
    }
    *x = px;
    *z = pz;
    if (*y < 0) *y = 0;
}
