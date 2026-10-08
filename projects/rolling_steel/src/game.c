/*
 * The game around the marble (GameDirector.cs, Player.cs, IsoCamera.cs,
 * Progress.cs, Ghost.cs, DeathFx.cs): one clock that carries over between
 * the six courses, falls that cost three seconds and put you back behind
 * where you last had contact, medals, best times, a ghost of your best run,
 * the title flyover, wipeouts in slow motion - and one or two players.
 *
 * Everything here steps at 50 Hz. Gameplay runs through a time-scale
 * accumulator so a wipeout can slow it down (single player, as in Unity).
 */
#include <string.h>
#include "game.h"

Game G;

#define B_UP    0x0001
#define B_DOWN  0x0002
#define B_LEFT  0x0004
#define B_RIGHT 0x0008
#define B_A     0x0010
#define B_B     0x0020
#define B_C     0x0040
#define B_P     0x0080
#define B_L     0x0200
#define B_R     0x0400

#define SECS(s)        ((long)((s) * 50.0 + 0.5))
#define DEG(d)         ((long)((d) * 65536.0 / 360.0))
#define DEATH_PENALTY  SECS(3)
#define DYING_HOLD     SECS(1.35)
#define CLEAR_HOLD     SECS(3.4)
#define DEFAULT_SIZE   FXF(10.5)           /* the Unity view is 13 units; 320x240 needs a closer look */

void rs_save_progress(void);               /* main_3do.c: NVRAM */
void rs_load_progress(void);

/* ---- small things ---- */

static unsigned long rnd_state = 12345;
static long rnd(long lo, long hi)          /* lo..hi-1 */
{
    rnd_state = rnd_state * 1103515245UL + 12345UL;
    return lo + (long)((rnd_state >> 8) % (unsigned long)(hi - lo));
}

static long lerp_angle(long now, long want, long k)    /* 65536 per turn, k Q12 */
{
    long d = (want - now) & 0xFFFF;
    if (d > 32768) d -= 65536;
    return now + ((d * k) >> 12);
}

int medal_for(long t)
{
    long ms = t * 20;
    if (t <= 0) return MEDAL_NONE;
    if (C.gold_ms > 0 && ms <= C.gold_ms) return MEDAL_GOLD;
    if (C.silver_ms > 0 && ms <= C.silver_ms) return MEDAL_SILVER;
    if (C.bronze_ms > 0 && ms <= C.bronze_ms) return MEDAL_BRONZE;
    return MEDAL_NONE;
}

/* ---- wipeout debris (DeathFx.Burst): shards and sparks ---- */
typedef struct { V3 p, v; fix size; int look; long life, age; } Bit;
#define MAXBITS 48
static Bit bits[MAXBITS];

static void burst(V3 at, int look)
{
    int i, n = 0;
    for (i = 0; i < MAXBITS && n < 24; i++) {
        Bit *b = &bits[i];
        int spark = n >= 16;
        fix force = spark ? 18 * FX : 12 * FX, sp;
        long a = rnd(0, 65536), e = rnd(-16384, 16384);
        if (b->life) continue;
        sp = rnd(force * 45 / 100, force);
        b->p = at;
        b->p.x += rnd(-1228, 1228); b->p.y += rnd(-1228, 1228); b->p.z += rnd(-1228, 1228);
        b->v.x = ((icos(a) >> 2) * ((icos(e) * (sp >> 4)) >> 14)) >> 8;
        b->v.z = ((isin(a) >> 2) * ((icos(e) * (sp >> 4)) >> 14)) >> 8;
        b->v.y = ((isin(e) * sp) >> 14) + rnd(force * 3 / 10, force * 8 / 10);
        b->size = spark ? rnd(328, 655) / 2 : rnd(819, 1802) / 2;
        b->look = spark ? SP_SPARK : look;
        b->life = spark ? rnd(25, 45) : rnd(60, 100);
        b->age = 0;
        n++;
    }
}

static void bits_step(void)
{
    int i;
    for (i = 0; i < MAXBITS; i++) {
        Bit *b = &bits[i];
        if (!b->life) continue;
        b->v.y -= 804;                                   /* 9.81 m/s^2 */
        b->p.x += (b->v.x * 1311L) >> 16;
        b->p.y += (b->v.y * 1311L) >> 16;
        b->p.z += (b->v.z * 1311L) >> 16;
        if (++b->age >= b->life) b->life = 0;
    }
}

/* ---- ghosts (Ghost.cs): your best run on each course, this session ---- */
#define GHOST_HZ   20
#define GHOST_MAX  1800
typedef struct { short x, y, z; } GS;
static GS ghost_rec[GHOST_MAX];
static int ghost_nrec;
static GS ghost_best[6][GHOST_MAX];
static int ghost_n[6];

static GS gs_of(V3 p) { GS g; g.x = (short)(p.x >> 6); g.y = (short)(p.y >> 6); g.z = (short)(p.z >> 6); return g; }

static int ghost_at(long course_time, V3 *out)
{
    const GS *s = ghost_best[G.level];
    long n = ghost_n[G.level], f = course_time * GHOST_HZ * 64 / 50, i = f >> 6, k = f & 63;
    if (n < 2 || i >= n - 1) return 0;
    out->x = ((long)s[i].x * (64 - k) + (long)s[i + 1].x * k);
    out->y = ((long)s[i].y * (64 - k) + (long)s[i + 1].y * k);
    out->z = ((long)s[i].z * (64 - k) + (long)s[i + 1].z * k);
    return 1;
}

/* ---- loading a course ---- */

static void cam_snap(Player *p);

static void load_level(int index, int reset_clock)
{
    int i;
    V3 across;
    if (!course_load(index + 1)) return;
    G.level = index;
    G.loaded = 1;
    phys_reset();
    ghost_nrec = 0;
    G.timescale = FX;
    G.time_acc = 0;
    across.x = 2896; across.y = 0; across.z = -2896;       /* the course's +x, yawed 45 */
    phys_nmarbles = G.nplayers;
    for (i = 0; i < G.nplayers; i++) {
        Player *p = &G.p[i];
        Ball *m = &p->m;
        p->course_time = 0;
        p->finished = p->out_of_time = p->dying = p->fall_whistle = 0;
        p->dying_timer = 0;
        p->demo_wp = 0;
        if (reset_clock) p->time_left = 0;
        p->time_left += C.time_ms / 20;
        memset(m, 0, sizeof(*m));
        m->p = C.spawn;
        if (G.nplayers > 1) {
            long s = i ? 5325 : -5325;                     /* 1.3 units either side */
            m->p.x += (across.x * s) >> 12;
            m->p.z += (across.z * s) >> 12;
        }
        m->r = FX / 2; m->inv_mass = FX; m->fric = FX / 2; m->bounce = 614; m->gravity = 1787;
        m->rot_k = 5 * FX; m->live = 1; m->last_ground = m->p;
        phys_marbles[i] = m;
        p->hold = 0;
        cam_snap(p);
    }
    rs_log("level %d %s clock=%ld\n", index + 1, C.name, G.p[0].time_left / 5);
    snd_music(G.music_on ? (int)C.music : -1);
}

static void enter(int s)
{
    if (s != ST_PLAYING) G.timescale = FX;
    G.state = s;
    G.state_timer = 0;
    rs_log("state=%d level=%d falls=%d clock=%ld\n", s, G.level + 1, G.p[0].deaths, G.p[0].time_left / 5);
}

static void freeze_all(void)
{
    int i;
    for (i = 0; i < G.nplayers; i++) G.p[i].m.frozen = 1;
}

static void start_run(int from)
{
    int i;
    G.nplayers = G.want_players;
    for (i = 0; i < G.nplayers; i++) {
        G.p[i].deaths = 0;
        G.p[i].run_time = 0;
        G.p[i].wins = 0;
        G.p[i].death_reason = "";
    }
    G.started_at = from;
    G.idle = 0;
    G.last_winner = -1;
    load_level(from, 1);
    enter(ST_PLAYING);
    snd_play(SFX_START);
}

static void to_title(void)
{
    /* after an attract demo, the next one shows the next course */
    if (G.demo) G.title_select = (G.level + 1) % course_count;
    G.demo = 0;
    G.nplayers = 1;
    enter(ST_TITLE);
    G.loaded = 0;
    snd_music(G.music_on ? 0 : -1);
}

/* ---- the camera (IsoCamera.cs) ---- */

static V3 desired(const Player *p)
{
    V3 f = p->m.p;
    /* look ahead along the flat velocity, 0.35 s */
    f.x += (p->m.v.x * 1434) >> 12;
    f.z += (p->m.v.z * 1434) >> 12;
    return f;
}

static void cam_snap(Player *p)
{
    p->cam.yaw = p->yaw;
    p->cam.pitch = p->pitch;
    p->cam.size = p->size;
    p->cam.focus = desired(p);
    p->cam_vel.x = p->cam_vel.y = p->cam_vel.z = 0;
}

static void cam_input(Player *p, unsigned long held, unsigned long pressed)
{
    /* a tap turns one 45-degree facet; holding spins freely and settles on 15 degrees */
    int spinning = 0;
    if (pressed & B_R) { p->yaw += DEG(45); p->r_hold = 0; }
    if (pressed & B_L) { p->yaw -= DEG(45); p->l_hold = 0; }
    if (held & B_R) { if (++p->r_hold > 17) { p->yaw += DEG(2); spinning = 1; } } else p->r_hold = 0;
    if (held & B_L) { if (++p->l_hold > 17) { p->yaw -= DEG(2); spinning = 1; } } else p->l_hold = 0;
    if (p->spinning && !spinning) {
        long step = DEG(15);
        p->yaw = ((p->yaw + step / 2) / step) * step;
    }
    p->spinning = spinning;
    /* C with up/down tilts; A zooms in, B out */
    if (held & B_C) {
        if (held & B_UP) p->pitch += DEG(0.9);
        if (held & B_DOWN) p->pitch -= DEG(0.9);
        if (p->pitch < DEG(15)) p->pitch = DEG(15);
        if (p->pitch > DEG(80)) p->pitch = DEG(80);
    }
    if (held & B_A) p->size -= 1147;                      /* 14 units/s */
    if (held & B_B) p->size += 1147;
    if (p->size < 7 * FX) p->size = 7 * FX;
    if (p->size > 28 * FX) p->size = 28 * FX;
}

static void cam_follow(Player *p)
{
    const long k = 874;                                   /* 1 - exp(-12 dt) */
    p->cam.yaw = lerp_angle(p->cam.yaw, p->yaw, k);
    p->cam.pitch += ((p->pitch - p->cam.pitch) * k) >> 12;
    p->cam.size += ((p->size - p->cam.size) * k) >> 12;
    if (!p->hold) {
        V3 d = desired(p);
        p->cam.focus.x += ((d.x - p->cam.focus.x) * 500) >> 12;
        p->cam.focus.y += ((d.y - p->cam.focus.y) * 500) >> 12;
        p->cam.focus.z += ((d.z - p->cam.focus.z) * 500) >> 12;
    }
}

static long cine_yaw;

static void cam_orbit(Player *p, V3 focus, long spin_deg, long pitch_deg, fix size)
{
    cine_yaw += spin_deg * 65536L / 360 / 50;
    p->cam.yaw = cine_yaw;
    p->cam.pitch += ((DEG(1) * pitch_deg - p->cam.pitch) * 278) >> 12;
    p->cam.size += ((size - p->cam.size) * 278) >> 12;
    p->cam.focus.x += ((focus.x - p->cam.focus.x) * 238) >> 12;
    p->cam.focus.y += ((focus.y - p->cam.focus.y) * 238) >> 12;
    p->cam.focus.z += ((focus.z - p->cam.focus.z) * 238) >> 12;
}

static V3 path_at(long t)                                 /* t: Q16 along the route */
{
    long f = (t >> 4) * (C.npath - 1), i = f >> 12, k = f & 4095;
    V3 a;
    if (C.npath < 2) return C.spawn;
    if (i >= C.npath - 1) { i = C.npath - 2; k = 4096; }
    a.x = C.path[i].x + (((C.path[i + 1].x - C.path[i].x) * k) >> 12);
    a.y = C.path[i].y + (((C.path[i + 1].y - C.path[i].y) * k) >> 12) + 2 * FX;
    a.z = C.path[i].z + (((C.path[i + 1].z - C.path[i].z) * k) >> 12);
    return a;
}

long course_progress(const Player *p)
{
    long i, best = 0, bd = 0x7FFFFFFF;
    for (i = 0; i < C.npath; i++) {
        long dx = (C.path[i].x - p->m.p.x) >> 8, dy = (C.path[i].y - p->m.p.y) >> 8, dz = (C.path[i].z - p->m.p.z) >> 8;
        long d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return C.npath ? C.path[best].progress : 0;
}

/* ---- the autopilot (GameDirector.DemoInput) ---- */

static void demo_input(Player *p, fix *wx, fix *wz)
{
    V3 d;
    fix l, tx, tz, ex, ez;
    while (p->demo_wp < C.npath - 1) {
        d.x = C.path[p->demo_wp].x - p->m.p.x; d.y = 0; d.z = C.path[p->demo_wp].z - p->m.p.z;
        if (vlen(d) < 2 * FX) p->demo_wp++; else break;
    }
    d.x = C.path[p->demo_wp].x - p->m.p.x; d.y = 0; d.z = C.path[p->demo_wp].z - p->m.p.z;
    l = vlen(d);
    if (l < 16) { *wx = *wz = 0; return; }
    tx = (d.x << 12) / l; tz = (d.z << 12) / l;
    ex = ((tx * 34816) >> 12) - p->m.v.x;                  /* cruise at 8.5 m/s */
    ez = ((tz * 34816) >> 12) - p->m.v.z;
    d.x = ex; d.z = ez;
    l = vlen(d);
    if (l < 64) { *wx = tx; *wz = tz; return; }
    *wx = (ex << 12) / l; *wz = (ez << 12) / l;
}

static void demo_resync(Player *p)
{
    long i, bd = 0x7FFFFFFF;
    for (i = 0; i < C.npath; i++) {
        long dx = (C.path[i].x - p->m.p.x) >> 8, dy = (C.path[i].y - p->m.p.y) >> 8, dz = (C.path[i].z - p->m.p.z) >> 8;
        long d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; p->demo_wp = (int)i; }
    }
}

/* ---- dying, respawning, finishing ---- */

static void kill(Player *p, const char *reason)
{
    if (G.state != ST_PLAYING || p->dying || p->finished || p->out_of_time) return;
    rs_log("death %s p%d course=%d at %ld %ld %ld\n", reason, (int)(p - G.p) + 1, G.level + 1,
           (long)(p->m.p.x >> 12), (long)(p->m.p.y >> 12), (long)(p->m.p.z >> 12));
    p->death_reason = reason;
    p->deaths++;
    p->time_left -= DEATH_PENALTY;
    if (p->time_left < 0) p->time_left = 0;
    p->m.frozen = 1;
    p->m.live = 0;
    p->hold = 1;
    p->shake = FXF(1.4);
    burst(p->m.p, p == G.p ? SP_MARBLE : SP_MARBLE2);
    snd_play(SFX_SHATTER);
    p->flash = 58;
    if (!strcmp(reason, "DISSOLVED")) { snd_play(SFX_SIZZLE); p->flash_r = 11; p->flash_g = 31; p->flash_b = 12; }
    else if (!strcmp(reason, "EATEN")) { snd_play(SFX_CHOMP); p->flash_r = 15; p->flash_g = 31; p->flash_b = 17; }
    else if (!strcmp(reason, "CRUSHED")) { snd_play(SFX_THUD); p->flash_r = 31; p->flash_g = 17; p->flash_b = 9; }
    else { if (!p->fall_whistle) snd_play(SFX_FALL); p->flash_r = 31; p->flash_g = 14; p->flash_b = 11; }
    if (G.nplayers == 1) G.timescale = 1229;               /* 0.3: the slow-motion beat */
    p->dying = 1;
    p->dying_timer = 0;
}

static V3 respawn_point(const Player *p)
{
    long i, best = -1, bd = 0x7FFFFFFF;
    V3 r;
    for (i = 0; i < C.npath; i++) {
        long dx, dy, dz, d;
        if (!C.path[i].safe) continue;                     /* never back out over a gap */
        dx = (C.path[i].x - p->m.last_ground.x) >> 8;
        dy = (C.path[i].y - p->m.last_ground.y) >> 8;
        dz = (C.path[i].z - p->m.last_ground.z) >> 8;
        d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return C.spawn;
    /* a step back along the route, so you come back on before whatever got you */
    for (i = best - 1; i >= 0; i--)
        if (C.path[i].safe) { best = i; break; }
    r.x = C.path[best].x; r.y = C.path[best].y + 1638; r.z = C.path[best].z;
    return r;
}

static void respawn(Player *p)
{
    G.timescale = FX;
    p->dying = 0;
    p->fall_whistle = 0;
    if (p->time_left <= 0) {
        p->out_of_time = 1;
        p->m.frozen = 1;
        return;
    }
    p->m.p = respawn_point(p);
    p->m.v.x = p->m.v.y = p->m.v.z = 0;
    p->m.w = p->m.v;
    p->m.last_ground = p->m.p;
    p->m.frozen = 0;
    p->m.live = 1;
    p->m.touching_enemy = p->m.touch_sweeper = 0;
    phys_reset_enemies();
    p->hold = 0;
    cam_snap(p);
    demo_resync(p);
}

static void reach_goal(Player *p)
{
    if (G.state != ST_PLAYING || p->finished || p->dying) return;
    p->finished = 1;
    p->finish_time = p->course_time;
    p->m.frozen = 1;
    G.last_course_time = p->course_time;
    G.last_medal = medal_for(p->course_time);
    G.last_was_best = 0;
    if (G.nplayers == 1 && !G.demo) {
        long *b = &G.best[G.level];
        if (!*b || p->course_time < *b) {
            *b = p->course_time;
            G.last_was_best = 1;
            memcpy(ghost_best[G.level], ghost_rec, ghost_nrec * sizeof(GS));
            ghost_n[G.level] = ghost_nrec;
            rs_save_progress();
        }
    } else if (G.nplayers > 1 && G.last_winner < 0) {
        G.last_winner = (int)(p - G.p);
        p->wins++;
    }
    rs_log("goal p%d course=%d time=%ld medal=%d best=%d\n", (int)(p - G.p) + 1, G.level + 1,
           p->course_time * 2, G.last_medal, G.last_was_best);
    snd_play(SFX_GOAL);
    freeze_all();
    enter(ST_CLEAR);
}

int game_bits(RBall *out, int max);

static void advance(void)
{
    if (G.demo) { to_title(); return; }
    if (G.level + 1 >= course_count) {
        if (G.started_at == 0 && G.nplayers == 1 && !G.demo &&
            (!G.best_run || G.p[0].run_time < G.best_run)) {
            G.best_run = G.p[0].run_time;
            G.best_run_falls = G.p[0].deaths;
            G.last_was_best = 1;
            rs_save_progress();
        }
        enter(ST_WON);
        freeze_all();
        return;
    }
    load_level(G.level + 1, 0);
    G.last_winner = -1;
    enter(ST_PLAYING);
    snd_play(SFX_START);
}

/* ---- one step of the race (scaled by the slow-motion) ---- */

static void race_step(unsigned long held[2])
{
    int i, all_out = 1;
    phys_world_step();
    bits_step();
    for (i = 0; i < G.nplayers; i++) {
        Player *p = &G.p[i];
        Ball *m = &p->m;
        fix wx = 0, wz = 0;
        if (p->finished) { all_out = 0; continue; }
        if (p->dying) { all_out = 0; continue; }
        if (p->out_of_time) continue;
        all_out = 0;
        if (G.demo) demo_input(p, &wx, &wz);
        else {
            /* the pad, relative to the camera: up is away from you */
            long h = 0, v = 0, sy = isin(p->cam.yaw) >> 2, cy = icos(p->cam.yaw) >> 2;
            unsigned long k = held[i];
            if (!(k & B_C)) {
                if (k & B_RIGHT) h += FX;
                if (k & B_LEFT) h -= FX;
                if (k & B_UP) v += FX;
                if (k & B_DOWN) v -= FX;
            }
            wx = (h * cy + v * sy) >> 12;
            wz = (-h * sy + v * cy) >> 12;
            if (h && v) { wx = (wx * 2896) >> 12; wz = (wz * 2896) >> 12; }
        }
        m->kill = 0;
        m->goal = 0;
        phys_marble_step(m, wx, wz);
        p->time_left--;
        p->course_time++;
        p->run_time++;
        if (p->time_left <= SECS(10) && p->time_left > 0 && G.clock - p->last_warn > 50) {
            p->last_warn = G.clock;
            snd_play(SFX_WARN);
        }
        if (p->time_left <= 0) {
            p->time_left = 0;
            p->out_of_time = 1;
            m->frozen = 1;
            snd_play(SFX_DEATH);
            continue;
        }
        if (m->kill) { kill(p, m->kill); continue; }
        if (m->goal) { reach_goal(p); continue; }
        {
            fix dropped = m->last_ground.y - m->p.y;
            int airborne = !m->grounded;
            if (airborne && dropped > FXF(4.5) && !p->fall_whistle) { p->fall_whistle = 1; snd_play(SFX_FALL); }
            if (!airborne || dropped < FX) p->fall_whistle = 0;
            if ((airborne && dropped > 12 * FX) || m->p.y < C.kill_y) kill(p, "FELL OFF");
        }
    }
    if ((G.p[0].course_time % 250) == 0 && G.p[0].course_time)
        rs_log("run course=%d t=%ld progress=%ld falls=%d\n", G.level + 1, G.p[0].course_time / 50,
               (course_progress(&G.p[0]) * 100) >> 16, G.p[0].deaths);
    /* the ghost: 20 samples a second of course time */
    if (G.nplayers == 1 && G.state == ST_PLAYING && !G.p[0].dying && !G.p[0].finished &&
        (G.p[0].course_time * GHOST_HZ) % 50 < GHOST_HZ && ghost_nrec < GHOST_MAX)
        ghost_rec[ghost_nrec++] = gs_of(G.p[0].m.p);
    if (all_out && G.state == ST_PLAYING) {
        enter(ST_GAMEOVER);
        freeze_all();
        snd_play(SFX_DEATH);
    }
}

/* ---- the 50 Hz step ---- */

void game_init(void)
{
    int i;
    memset(&G, 0, sizeof(G));
    G.want_players = G.nplayers = 1;
    G.music_on = 1;
    G.last_winner = -1;
    for (i = 0; i < 2; i++) {
        G.p[i].yaw = 0;
        G.p[i].pitch = DEG(35);
        G.p[i].size = DEFAULT_SIZE;
        G.p[i].cam = G.p[i].cam;
    }
    rs_load_progress();
    to_title();
}

void game_step(unsigned long held0, unsigned long pressed0, unsigned long held1, unsigned long pressed1)
{
    unsigned long held[2], pressed[2];
    int i;
    held[0] = held0; held[1] = held1; pressed[0] = pressed0; pressed[1] = pressed1;
    G.clock++;
    G.state_timer++;
    for (i = 0; i < 2; i++) {
        if (G.p[i].flash > 0) G.p[i].flash -= 4;
        if (G.p[i].shake > 0) G.p[i].shake -= 287;
        if (G.p[i].shake < 0) G.p[i].shake = 0;
    }
    if (G.demo && (pressed0 | pressed1)) { to_title(); return; }
    switch (G.state) {
    case ST_TITLE:
        if (!G.loaded || G.level != G.title_select) { load_level(G.title_select, 1); freeze_all(); }
        if (pressed0) G.idle = 0; else G.idle++;
        if (pressed0 & B_DOWN) { if (G.title_select < course_count - 1) G.title_select++; }
        if (pressed0 & B_UP) { if (G.title_select > 0) G.title_select--; }
        if (pressed0 & (B_LEFT | B_RIGHT)) G.want_players = G.want_players == 1 ? 2 : 1;
        if (pressed0 & B_C) { G.music_on = !G.music_on; snd_music(G.music_on ? 0 : -1); }
        if (pressed0 & (B_A | B_P)) { start_run(G.title_select); return; }
        if (G.idle > SECS(25)) {
            /* attract mode: the autopilot plays the first course */
            G.want_players = 1;
            start_run(G.title_select);
            G.demo = 1;
            rs_log("demo starts course=%d\n", G.title_select + 1);
            return;
        }
        phys_world_step();
        G.cine_t = (G.cine_t + 59) & 0xFFFF;               /* 0.045 of the course a second */
        cam_orbit(&G.p[0], path_at(G.cine_t), 9, 27, 20 * FX);
        break;
    case ST_PLAYING:
        if (pressed0 & B_P) G.paused = !G.paused;
        if (G.paused) break;
        for (i = 0; i < G.nplayers; i++) {
            Player *p = &G.p[i];
            cam_input(p, held[i], pressed[i]);
            if (p->dying) {
                p->dying_timer++;
                if (G.nplayers == 1) {
                    G.timescale = 1229 + ((FX - 1229) * p->dying_timer) / DYING_HOLD;
                    if (G.timescale > FX) G.timescale = FX;
                }
                if (p->dying_timer >= DYING_HOLD) respawn(p);
            }
        }
        G.time_acc += G.timescale;
        while (G.time_acc >= FX && G.state == ST_PLAYING) {
            G.time_acc -= FX;
            race_step(held);
        }
        for (i = 0; i < G.nplayers; i++) cam_follow(&G.p[i]);
        break;
    case ST_CLEAR:
        phys_world_step();
        bits_step();
        for (i = 0; i < G.nplayers; i++) {
            V3 f = C.goal_world;
            f.y += 7 * FX;
            if (i == 0) cam_orbit(&G.p[i], f, 34, 30, 16 * FX);
            else G.p[i].cam = G.p[0].cam;
        }
        if (G.state_timer >= CLEAR_HOLD) advance();
        break;
    case ST_GAMEOVER:
    case ST_WON:
        phys_world_step();
        bits_step();
        if (G.state == ST_WON) {
            V3 f = C.goal_world;
            f.y += 7 * FX;
            cam_orbit(&G.p[0], f, 34, 30, 16 * FX);
        }
        if (G.state_timer > SECS(1) && (pressed0 & (B_A | B_P))) to_title();
        else if (G.demo && G.state_timer > SECS(4)) to_title();
        break;
    }
    /* the world plays its sounds */
    if (phys_sound >= 0) { snd_play(phys_sound); phys_sound = -1; }
    {
        Player *p = &G.p[0];
        fix sp = 0;
        if (G.state == ST_PLAYING && !p->dying && !p->finished && p->m.grounded) {
            V3 f;
            f.x = p->m.v.x; f.y = 0; f.z = p->m.v.z;
            sp = vlen(f) * FX / (13 * FX);
            if (sp > FX) sp = FX;
        }
        snd_roll(sp);
    }
}

/* ---- the frame ---- */

int game_marks[3];

int game_draw(void)
{
    RBall b[64];
    int i, n, pl;
    int views = G.state == ST_TITLE ? 1 : G.nplayers;
    for (pl = 0; pl < views; pl++) {
        Player *me = &G.p[pl];
        Cam c = me->cam;
        game_marks[pl] = render_mark();
        /* split screen: the right half is drawn through a clip rectangle whose
         * origin also moves the cels, so both views are laid out from x = 0 */
        if (views > 1) { c.x0 = 0; c.w = 160; } else { c.x0 = 0; c.w = 320; }
        if (me->shake > 0) {
            c.focus.x += rnd(-me->shake, me->shake);
            c.focus.y += rnd(-me->shake, me->shake);
            c.focus.z += rnd(-me->shake, me->shake);
        }
        n = 0;
        if (G.state != ST_TITLE)
            for (i = 0; i < G.nplayers; i++) {
                Player *p = &G.p[i];
                if (p->dying || !p->m.live) continue;
                b[n].p = p->m.p; b[n].r = p->m.r; b[n].look = i ? SP_MARBLE2 : SP_MARBLE;
                b[n].pixc = 0; b[n].shadow = 1;
                n++;
            }
        if (G.nplayers == 1 && G.state == ST_PLAYING && !G.demo) {
            V3 gp;
            if (ghost_at(G.p[0].course_time, &gp)) {
                b[n].p = gp; b[n].r = FX / 2; b[n].look = SP_GHOST; b[n].pixc = 0x0F810F81UL; b[n].shadow = 0;
                n++;
            }
        }
        n += game_bits(b + n, 64 - n);
        render_scene(&c, b, n);
    }
    game_marks[views] = render_mark();
    return views;
}

/* the wipeout debris, as round things for render.c */
int game_bits(RBall *out, int max)
{
    int i, n = 0;
    for (i = 0; i < MAXBITS && n < max; i++) {
        const Bit *b = &bits[i];
        long k;
        if (!b->life) continue;
        k = FX - (b->age << 12) / b->life;                 /* Fader: shrink away */
        out[n].p = b->p;
        out[n].r = (b->size * k) >> 12;
        out[n].look = b->look;
        out[n].pixc = b->look == SP_SPARK ? 0x1F801F80UL : 0;
        out[n].shadow = 0;
        n++;
    }
    return n;
}
