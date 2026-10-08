/*
 * The marble physics on the host, against the real course files: the Unity
 * game's own demo driver (GameDirector.DemoInput) rolls down each course,
 * respawning like the game does, and must reach the goal - the same check
 * as the Unity project's `make verify`.
 *
 *   cc -std=c89 -Dlong=int -Isrc -o build/sim tools/sim.c src/phys.c src/course.c && build/sim
 *
 * (-Dlong=int: the course structures map 32-bit words; the host's long is 64.)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "rs.h"

void rs_log(const char *fmt, ...) { (void)fmt; }

void *rs_load(const char *name, long *size)
{
    char path[256];
    FILE *f;
    unsigned char *buf;
    long n, i;
    sprintf(path, "takeme/rolling_steel/%s", name);
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(n);
    fread(buf, 1, n, f);
    fclose(f);
    for (i = 0; i + 3 < n; i += 4) {           /* the files are big-endian, like the 3DO */
        unsigned char t = buf[i]; buf[i] = buf[i + 3]; buf[i + 3] = t;
        t = buf[i + 1]; buf[i + 1] = buf[i + 2]; buf[i + 2] = t;
    }
    for (i = 4; i < 36; i += 4) {             /* except the name, which is bytes */
        unsigned char t = buf[i]; buf[i] = buf[i + 3]; buf[i + 3] = t;
        t = buf[i + 1]; buf[i + 1] = buf[i + 2]; buf[i + 2] = t;
    }
    *size = n;
    return buf;
}
void rs_free(void *p) { free(p); }
void *rs_alloc(long bytes) { return malloc(bytes); }


static Ball m;
static int wp;

/* GameDirector.DemoInput: chase the next waypoint at a cruising speed */
static void demo(fix *wx, fix *wz)
{
    fix tx, tz, ex, ez;
    V3 d;
    fix l;
    while (wp < C.npath - 1) {
        d.x = C.path[wp].x - m.p.x; d.y = 0; d.z = C.path[wp].z - m.p.z;
        if (vlen(d) < 2 * FX) wp++; else break;
    }
    d.x = C.path[wp].x - m.p.x; d.y = 0; d.z = C.path[wp].z - m.p.z;
    l = vlen(d);
    if (l < 16) { *wx = 0; *wz = FX; return; }
    tx = (d.x << 12) / l; tz = (d.z << 12) / l;
    ex = ((tx * 34816) >> 12) - m.v.x;                 /* 8.5 m/s */
    ez = ((tz * 34816) >> 12) - m.v.z;
    d.x = ex; d.z = ez;
    l = vlen(d);
    if (l < 64) { *wx = tx; *wz = tz; return; }
    *wx = (ex << 12) / l; *wz = (ez << 12) / l;
    if (getenv("DBG")) printf("    d %d %d t %d %d v %d %d e %d %d l %d w %d %d\n", (int)(C.path[wp].x - m.p.x), (int)(C.path[wp].z - m.p.z), (int)tx, (int)tz, (int)m.v.x, (int)m.v.z, (int)ex, (int)ez, (int)l, (int)*wx, (int)*wz);
}

static void resync(void)
{
    long i, best = 0;
    long bd = 0x7FFFFFFF;
    for (i = 0; i < C.npath; i++) {
        long dx = (C.path[i].x - m.p.x) >> 8, dy = (C.path[i].y - m.p.y) >> 8, dz = (C.path[i].z - m.p.z) >> 8;
        long dd = dx * dx + dy * dy + dz * dz;
        if (dd < bd) { bd = dd; best = i; }
    }
    wp = (int)best;
}

/* GameDirector.RespawnPointOnCourse */
static V3 respawn_point(void)
{
    long i, best = -1, bd = 0x7FFFFFFF;
    V3 p;
    for (i = 0; i < C.npath; i++) {
        long dx, dy, dz, dd;
        if (!C.path[i].safe) continue;
        dx = (C.path[i].x - m.last_ground.x) >> 8; dy = (C.path[i].y - m.last_ground.y) >> 8; dz = (C.path[i].z - m.last_ground.z) >> 8;
        dd = dx * dx + dy * dy + dz * dz;
        if (dd < bd) { bd = dd; best = i; }
    }
    if (best < 0) return C.spawn;
    for (i = best - 1; i >= 0; i--)
        if (C.path[i].safe) { best = i; break; }
    p.x = C.path[best].x; p.y = C.path[best].y + 1638; p.z = C.path[best].z;
    return p;
}

int main(int argc, char **argv)
{
    int course, fails = 0;
    for (course = 1; course <= 6; course++) {
        long step, deaths = 0, maxspeed = 0;
        const char *why = 0;
        if (argc > 1 && atoi(argv[1]) != course) continue;
        if (!course_load(course)) { printf("course %d: no file\n", course); fails++; continue; }
        phys_reset();
        memset(&m, 0, sizeof(m));
        m.p = C.spawn; m.r = 2048; m.inv_mass = FX; m.fric = 2048; m.bounce = 614; m.gravity = 1787;
        m.rot_k = 5 * FX; m.live = 1; m.last_ground = m.p;
        phys_marbles[0] = &m;
        phys_nmarbles = 1;
        wp = 0;
        for (step = 0; step < 50 * 240; step++) {
            fix wx, wz;
            V3 f;
            m.kill = 0;
            m.goal = 0;
            demo(&wx, &wz);
            phys_world_step();
            phys_marble_step(&m, wx, wz);
            f.x = m.v.x; f.y = 0; f.z = m.v.z;
            if (vlen(f) > maxspeed) maxspeed = vlen(f);
            if (!m.kill && ((!m.grounded && m.last_ground.y - m.p.y > 12 * FX) || m.p.y < C.kill_y))
                m.kill = "FELL OFF";
            if (m.kill) {
                deaths++;
                if (argc > 2) printf("  %5.1fs death %s at %.1f %.1f %.1f (wp %d)\n", step / 50.0, m.kill,
                                     m.p.x / 4096.0, m.p.y / 4096.0, m.p.z / 4096.0, wp);
                m.p = respawn_point();
                m.v.x = m.v.y = m.v.z = 0; m.w = m.v;
                m.last_ground = m.p;
                phys_reset_enemies();
                resync();
                if (deaths > 40) { why = "too many deaths"; break; }
            }
            if (argc > 2 && step % 50 == 0)
                printf("  t=%3lds wp %3d/%ld p %.1f %.1f %.1f v %.1f %.1f %.1f g%d wish %.2f %.2f\n", step / 50, wp, (long)C.npath,
                       m.p.x / 4096.0, m.p.y / 4096.0, m.p.z / 4096.0, m.v.x / 4096.0, m.v.y / 4096.0, m.v.z / 4096.0, m.grounded,
                       wx / 4096.0, wz / 4096.0);
            if (m.goal) break;
        }
        printf("course %d %-12s %s in %5.1f s, %ld falls, top speed %.1f\n", course, C.name,
               m.goal ? "GOAL" : why ? why : "TIMEOUT", step / 50.0, deaths, maxspeed / 4096.0);
        if (!m.goal) fails++;
    }
    return fails ? 1 : 0;
}
