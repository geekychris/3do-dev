/*
 * Loads a course written by tools/courses.py. The file is big-endian int32
 * throughout - the 3DO's own order - so loading is pointing structures into
 * the buffer; only the triangle normals are converted (Q14 -> Q12).
 */
#include <string.h>
#include <stdio.h>
#include "rs.h"

Course C;
int course_count = 6;

static w32 *cur;
static w32 take(void) { return *cur++; }
static w32 *take_n(long n) { w32 *p = cur; cur += n; return p; }

static void grid(Grid *g, int spheres)
{
    long n;
    g->x0 = take(); g->z0 = take(); g->cell = take(); g->nx = take(); g->nz = take();
    n = g->nx * g->nz;
    g->off = take_n(n + 1);
    g->idx = take_n(g->off[n]);
    g->sphere = spheres ? take_n(4 * n) : 0;
}

void course_free(void)
{
    if (C.tsphere) rs_free(C.tsphere);
    if (C.data) rs_free(C.data);
    memset(&C, 0, sizeof(C));
}

int course_load(int index)
{
    char name[24];
    long size = 0;
    long i, n;
    course_free();
    sprintf(name, "course%d.bin", index);
    C.data = (w32 *)rs_load(name, &size);
    if (!C.data || size < 64 || C.data[0] != 0x52534331L) {
        rs_log("can't load %s\n", name);
        return 0;
    }
    cur = C.data + 1;
    memcpy(C.name, take_n(8), 32);
    C.name[32] = 0;
    C.time_ms = take(); C.gold_ms = take(); C.silver_ms = take(); C.bronze_ms = take();
    C.decor = take(); C.music = take();
    C.spawn = *(V3 *)take_n(3);
    C.goal = *(OBB *)take_n(8);
    C.goal_world = *(V3 *)take_n(3);
    C.kill_y = take();
    C.nv = take(); C.v = (V3 *)take_n(3 * C.nv);
    C.nq = take(); C.q = (RQuad *)take_n(9 * C.nq);
    C.nt = take(); C.t = (CTri *)take_n(7 * C.nt);
    C.tsphere = (w32 *)rs_alloc(C.nt * 16);
    for (i = 0; i < C.nt; i++) {
        CTri *t = &C.t[i];
        t->n[0] >>= 2; t->n[1] >>= 2; t->n[2] >>= 2;
        if (C.tsphere) {
            /* a bounding sphere, so the marble can skip far triangles cheaply */
            const V3 *a = &C.v[t->v[0]], *b = &C.v[t->v[1]], *c = &C.v[t->v[2]];
            w32 *sp = C.tsphere + i * 4;
            long k, r = 0;
            sp[0] = (a->x + b->x + c->x) / 3; sp[1] = (a->y + b->y + c->y) / 3; sp[2] = (a->z + b->z + c->z) / 3;
            for (k = 0; k < 3; k++) {
                const V3 *v = &C.v[t->v[k]];
                long dx = (v->x - sp[0]) >> 4, dy = (v->y - sp[1]) >> 4, dz = (v->z - sp[2]) >> 4;
                long d = (long)isqrt((unsigned long)(dx * dx + dy * dy + dz * dz)) << 4;
                if (d > r) r = d;
            }
            sp[3] = r + 16;
        }
    }
    grid(&C.cgrid, 0);
    grid(&C.rgrid, 1);
    C.npath = take(); C.path = (PathPt *)take_n(5 * C.npath);
    C.nacid = take(); C.acid = (OBB *)take_n(8 * C.nacid);
    C.nzone = take(); C.zone = (Zone *)take_n(13 * C.nzone);
    C.npillar = take(); C.pillar = (Pillar *)take_n(5 * C.npillar);
    C.nsweep = take(); C.sweep = (Sweeper *)take_n(6 * C.nsweep);
    C.ncrush = take(); C.crush = (Crusher *)take_n(9 * C.ncrush);
    C.nenemy = take(); C.enemy = (EnemySpec *)take_n(8 * C.nenemy);
    C.ncrumble = take(); C.crumble = (CrumbleSpec *)take_n(4 * C.ncrumble);
    n = take();
    C.ndecor = n > MAXDECOR ? MAXDECOR : n;
    for (i = 0; i < n; i++) {
        Decor d;
        d.x = take(); d.y = take(); d.z = take(); d.scale = take(); d.phase = take();
        d.amp = take(); d.speed = take(); d.spin = take(); d.yaw = take();
        d.nv = take(); d.v = (V3 *)take_n(3 * d.nv);
        d.nq = take(); d.q = take_n(7 * d.nq);
        d.ns = take(); d.s = take_n(5 * d.ns);
        if (i < MAXDECOR) C.deco[i] = d;
    }
    if (C.ncrumble > MAXCRUMBLE) C.ncrumble = MAXCRUMBLE;
    rs_log("course %ld %s quads=%ld tris=%ld path=%ld bytes=%ld\n", (long)index, C.name, C.nq, C.nt, C.npath, (long)size);
    return 1;
}
