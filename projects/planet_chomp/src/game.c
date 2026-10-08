/*
 * Planet Chomp rules, from the Unity sources: GameDirector.cs (states,
 * scoring, keys, levels, scatter/chase clock), Ghost.cs (the arcade ghost
 * brain on a sphere), Chomper.cs (buffered screen-relative steering) and
 * Mover.cs (rail movement). Times are 50 Hz ticks; positions fixed point.
 */
#include "game.h"
#include "amiga3do.h"
#include "sfx.h"
#include "bridge_client.h"

Game g;
const char *ghost_name[4] = { "BLAZE", "PETAL", "FROST", "EMBER" };

#define START_LIVES   3
#define EXTRA_LIFE_AT 10000
#define CATCH_COS     16330          /* about 1 world unit apart (cos 0.08 rad), Q14 */

static const long mode_ticks[8] = { 350, 1000, 350, 1000, 250, 1000, 250, 0x7FFFFFFFL };

/* ---- movement on rails (Mover.cs) ---- */

typedef int (*Chooser)(void *ctx, int cell, V3 heading, int cruising);
typedef void (*Arrived)(void *ctx, int cell);

static void mover_place(Mover *m, int cell, V3 facing)
{
    fix d = v3_dot14(facing, mz_dir[cell]);
    m->from = cell; m->to = -1; m->prev = -1; m->t = 0; m->moving = 0; m->exitk = -1;
    m->heading = v3_norm14(v3_sub(facing, v3_scale14(mz_dir[cell], d)));
}

V3 mover_dir(const Mover *m)
{
    V3 p;
    long t;
    if (!m->moving) return mz_dir[m->from];
    t = m->t >> 2;                                       /* Q14 */
    p.x = mz_dir[m->from].x + (((mz_dir[m->to].x - mz_dir[m->from].x) * t) >> 14);
    p.y = mz_dir[m->from].y + (((mz_dir[m->to].y - mz_dir[m->from].y) * t) >> 14);
    p.z = mz_dir[m->from].z + (((mz_dir[m->to].z - mz_dir[m->from].z) * t) >> 14);
    return v3_norm14(p);
}

int mover_nearest(const Mover *m) { return m->moving && m->t > 32768 ? m->to : m->from; }

static void mover_reverse(Mover *m)
{
    int back, f;
    if (!m->moving) return;
    back = mz_rev[m->from][m->exitk];
    f = m->from; m->from = m->to; m->to = f;
    m->exitk = back;
    m->t = 65536 - m->t;
    m->prev = m->from;
    m->heading = v3_scale14(m->heading, -ONE14);
}

static void mover_advance(Mover *m, fix dist, Chooser choose, Arrived arrived, void *ctx)
{
    int cruising = m->moving, guard;
    for (guard = 0; dist > 0 && guard < 8; guard++) {
        fix len, remain;
        if (!m->moving) {
            int k = choose(ctx, m->from, m->heading, cruising);
            if (k < 0) break;
            m->to = mz_nb[m->from][k];
            m->exitk = k;
            m->t = 0;
            m->moving = 1;
        }
        len = mz_len[m->from][m->exitk];
        remain = ((65536 - m->t) * len) >> 16;
        if (dist < remain) {
            m->t += (dist << 16) / len;
            dist = 0;
        } else {
            int back = mz_rev[m->from][m->exitk];
            dist -= remain;
            m->prev = m->from;
            m->from = m->to;
            m->to = -1;
            m->t = 0;
            m->moving = 0;
            m->heading = v3_scale14(mz_tan[m->from][back], -ONE14);
            cruising = 1;
            if (arrived) arrived(ctx, m->from);
        }
    }
    if (m->moving)
        m->heading = mz_tan[m->from][m->exitk];
}

/* exit at c best matching a wanted direction (projected onto the surface) */
static int best_exit(int c, V3 want, fix min_dot)
{
    int k, best = -1;
    fix bd = min_dot, d = v3_dot14(want, mz_dir[c]);
    if (!(want.x | want.y | want.z)) return -1;
    want = v3_norm14(v3_sub(want, v3_scale14(mz_dir[c], d)));
    for (k = 0; k < 4; k++) {
        fix dot;
        if (!mz_open[c][k]) continue;
        dot = v3_dot14(mz_tan[c][k], want);
        if (dot > bd) { bd = dot; best = k; }
    }
    return best;
}

/* ---- the player (Chomper.cs) ---- */

static int pend_x, pend_y, last_x, last_y;     /* screen direction, -1/0/1 */
static unsigned long held_now;

static int held_dir(int x, int y)
{
    return (x > 0 && (held_now & PAD_RIGHT)) || (x < 0 && (held_now & PAD_LEFT)) ||
           (y > 0 && (held_now & PAD_UP)) || (y < 0 && (held_now & PAD_DOWN));
}

static void consume_pending(void)
{
    if (!held_dir(pend_x, pend_y)) pend_x = pend_y = 0;
}

/* screen direction -> surface tangent (camera right/up on the planet) */
static V3 wish(void)
{
    V3 r = v3_cross14(g.cam.focus, g.cam.up), w;
    w.x = r.x * pend_x + g.cam.up.x * pend_y;
    w.y = r.y * pend_x + g.cam.up.y * pend_y;
    w.z = r.z * pend_x + g.cam.up.z * pend_y;
    return w;
}

/* ---- autopilot (GameDirector.BotChoose / BotDodge): eats the nearest
 * crumb it can reach without passing next to a spook, grabs a key that's
 * close, hunts frightened spooks; otherwise heads away from the spooks ---- */

static int bot_choose(int c)
{
    static unsigned char danger[CELLS];
    static short dist[CELLS], parent[CELLS], q[CELLS];
    int i, k, head = 0, tail = 0, target = -1, best = 0x7FFF;
    for (i = 0; i < CELLS; i++) { danger[i] = 0; dist[i] = 0x7FFF; parent[i] = -1; }
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        int gc;
        if (!ghost_dangerous(gh) || gh->mode == GM_NEST) continue;
        gc = mover_nearest(&gh->mv);
        danger[gc] = 1;
        for (k = 0; k < 4; k++) if (mz_open[gc][k]) danger[mz_nb[gc][k]] = 1;
        if (gh->mv.moving) danger[gh->mv.from == gc ? gh->mv.to : gh->mv.from] = 1;
    }
    dist[c] = 0;
    q[tail++] = (short)c;
    while (head < tail) {
        int x = q[head++];
        for (k = 0; k < 4; k++) {
            int nb = mz_nb[x][k];
            if (!mz_open[x][k] || dist[nb] != 0x7FFF || danger[nb]) continue;
            dist[nb] = (short)(dist[x] + 1);
            parent[nb] = (short)x;
            q[tail++] = (short)nb;
        }
    }
    for (i = 1; i < tail; i++) {
        int n = q[i], want = g.crumb[n] || (g.key[n] && dist[n] < 6);
        if (g.power_left > TICKS(1.5)) {
            for (k = 0; k < 4; k++)
                if (g.ghost[k].fright && g.ghost[k].mode == GM_ACTIVE && mover_nearest(&g.ghost[k].mv) == n)
                    want = dist[n] < 12;
        }
        if (want && dist[n] < best) { best = dist[n]; target = n; }
    }
    if (target >= 0) {
        int step = target;
        while (parent[step] != c) step = parent[step];
        for (k = 0; k < 4; k++) if (mz_nb[c][k] == step && mz_open[c][k]) return k;
    }
    {
        /* nothing safe to eat: the exit farthest from the nearest spook */
        int bk = -1;
        fix bscore = -0x7FFFFFFFL;
        for (k = 0; k < 4; k++) {
            fix worst = -ONE14;
            if (!mz_open[c][k]) continue;
            for (i = 0; i < 4; i++)
                if (ghost_dangerous(&g.ghost[i])) {
                    fix d = v3_dot14(mz_dir[mz_nb[c][k]], mover_dir(&g.ghost[i].mv));
                    if (d > worst) worst = d;
                }
            if (-worst > bscore) { bscore = -worst; bk = k; }
        }
        return bk;
    }
}

static void bot_dodge(void)
{
    int i;
    V3 p;
    if (!g.player.moving) return;
    p = mover_dir(&g.player);
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        V3 gd, t;
        fix d;
        if (!ghost_dangerous(gh) || gh->mode == GM_NEST) continue;
        gd = mover_dir(&gh->mv);
        d = v3_dot14(gd, p);
        if (d < 15960) continue;                          /* farther than ~1.3 cells */
        t = v3_sub(gd, v3_scale14(p, d));
        if (!(t.x | t.y | t.z)) continue;
        if (v3_dot14(v3_norm14(t), g.player.heading) > 8192) {
            mover_reverse(&g.player);
            return;
        }
    }
}

static int player_choose(void *ctx, int c, V3 heading, int cruising)
{
    (void)ctx;
    if (g.demo) return bot_choose(c);
    if (pend_x || pend_y) {
        int k = best_exit(c, wish(), 7373);          /* 0.45 */
        if (k >= 0) { consume_pending(); return k; }
    }
    return cruising ? best_exit(c, heading, 8192) : -1;   /* 0.5 */
}

static void start_power(void);
static void add_score(long pts);
static void set_state(int s);

static void player_arrived(void *ctx, int c)
{
    (void)ctx;
    if (g.crumb[c]) {
        g.crumb[c] = 0;
        g.crumbs_left--;
        add_score(10);
        sfx_waka();
        if (g.crumbs_left % 10 == 0)
            AB_I("chomp score=%ld left=%ld", g.score, (long)g.crumbs_left);
        if (g.crumbs_left == 0) {
            sfx_ambience(0);
            sfx_play(SFX_CLEAR);
            g.power_left = 0;
            g.powered = 0;
            set_state(GS_CLEAR);
            AB_I("level clear level=%ld score=%ld", (long)g.level, g.score);
        }
    } else if (g.key[c]) {
        g.key[c] = 0;
        add_score(50);
        sfx_play(SFX_KEY);
        start_power();
    }
}

static void player_tick(void)
{
    if (g.player.moving && (pend_x || pend_y)) {
        V3 w = wish();
        fix d = v3_dot14(v3_norm14(w), g.player.heading);
        if ((w.x | w.y | w.z) && d < -8192) {
            mover_reverse(&g.player);
            consume_pending();
        }
    }
    mover_advance(&g.player, g.player_speed / 50, player_choose, player_arrived, 0);
    if (g.player.moving) g.mouth++;
}

/* ---- the spooks (Ghost.cs) ---- */

static int grnd(Ghost *gh, int n)
{
    gh->rng = gh->rng * 1103515245UL + 12345UL;
    return (int)((gh->rng >> 16) % (unsigned long)n);
}

int ghost_dangerous(const Ghost *gh) { return gh->mode != GM_EATEN && !gh->fright; }

/* point `cells` cells ahead of dir along heading: dir cos a + heading sin a */
static V3 ahead(V3 dir, V3 heading, int cells)
{
    /* one cell is ~0.175 rad: cos/sin of 2 and 4 cells, Q14 */
    fix cs = cells == 2 ? 15385 : 12531, sn = cells == 2 ? 5632 : 10555;
    return v3_norm14(v3_add(v3_scale14(dir, cs), v3_scale14(heading, sn)));
}

static V3 ghost_target(Ghost *gh)
{
    V3 pd = mover_dir(&g.player);
    if (g.scatter) return gh->scatter;
    switch (gh->kind) {
    case GK_AMBUSHER:
        return ahead(pd, g.player.heading, 4);
    case GK_FLANKER: {
        V3 pivot = ahead(pd, g.player.heading, 2), chaser = mover_dir(&g.ghost[0].mv);
        V3 t = v3_sub(v3_add(pivot, pivot), chaser);
        return (t.x | t.y | t.z) ? v3_norm14(t) : pd;
    }
    case GK_DRIFTER:
        /* chase when more than 7 cells away (cos 1.22 rad = 0.34) */
        return v3_dot14(mover_dir(&gh->mv), pd) < 5570 ? pd : gh->scatter;
    default:
        return pd;
    }
}

static int ghost_choose(void *ctx, int c, V3 heading, int cruising)
{
    Ghost *gh = (Ghost *)ctx;
    int back = gh->mv.prev, options = 0, allow_back, k, best = -1;
    fix bd = -0x7FFF;
    V3 target;
    (void)heading; (void)cruising;
    for (k = 0; k < 4; k++) if (mz_open[c][k] && mz_nb[c][k] != back) options++;
    allow_back = options == 0;
    if (gh->mode == GM_EATEN) {
        int bk = -1, bdist = 0x7FFF;
        for (k = 0; k < 4; k++) {
            if (!mz_open[c][k]) continue;
            if (g.nest_dist[mz_nb[c][k]] < bdist) { bdist = g.nest_dist[mz_nb[c][k]]; bk = k; }
        }
        return bk;
    }
    if (gh->fright) {
        int pick = grnd(gh, options > 0 ? options : 1);
        for (k = 0; k < 4; k++) {
            if (!mz_open[c][k] || (!allow_back && mz_nb[c][k] == back)) continue;
            if (pick-- == 0) return k;
        }
    }
    target = ghost_target(gh);
    for (k = 0; k < 4; k++) {
        fix d;
        if (!mz_open[c][k] || (!allow_back && mz_nb[c][k] == back)) continue;
        d = v3_dot14(mz_dir[mz_nb[c][k]], target);      /* closest by angle = largest dot */
        if (d > bd) { bd = d; best = k; }
    }
    return best;
}

static void ghost_arrived(void *ctx, int c)
{
    Ghost *gh = (Ghost *)ctx;
    if (gh->mode == GM_EATEN && c == mz_nest) {
        gh->mode = GM_NEST;                 /* home: regrow, out again shortly */
        gh->release_at = g.life + TICKS(1.2);
    }
}

static void ghost_tick(Ghost *gh)
{
    fix speed = g.ghost_speed;
    if (gh->mode == GM_NEST) {
        if (g.life >= gh->release_at) gh->mode = GM_ACTIVE;
        else return;
    }
    if (gh->fright) speed = (speed * 55) / 100;
    if (gh->mode == GM_EATEN) speed = (speed * 24) / 10;
    mover_advance(&gh->mv, speed / 50, ghost_choose, ghost_arrived, gh);
}

V3 ghost_world(const Ghost *gh)
{
    V3 n = mover_dir(&gh->mv), p = v3_scale14(n, PLANET_R + Q8(0.52));
    if (gh->mode == GM_NEST) {
        /* huddle round the nest, bobbing */
        static const short bob[16] = { 0, 12, 22, 28, 30, 28, 22, 12, 0, -12, -22, -28, -30, -28, -22, -12 };
        fix lift = Q8(0.15) + bob[((g.clock >> 2) + gh->slot * 4) & 15];
        p = v3_add(p, v3_scale14(mz_tan[mz_nest][gh->slot], Q8(0.55)));
        p = v3_add(p, v3_scale14(n, lift));
    }
    return p;
}

V3 player_world(void) { return v3_scale14(mover_dir(&g.player), PLANET_R + Q8(0.55)); }

/* ---- the director (GameDirector.cs) ---- */

static void set_state(int s)
{
    g.state = s;
    g.timer = 0;
    AB_I("state=%ld score=%ld lives=%ld level=%ld crumbs=%ld", (long)s, g.score, (long)g.lives,
         (long)g.level, (long)g.crumbs_left);
}

static void add_score(long pts)
{
    g.score += pts;
    if (g.score > g.hiscore && !g.demo) g.hiscore = g.score;   /* demos don't count */
    if (!g.extra_given && g.score >= EXTRA_LIFE_AT) {
        g.extra_given = 1;
        g.lives++;
        sfx_play(SFX_EXTRA);
    }
}

static void build_level(void)
{
    int c, i, lv = g.level - 1;
    long ps;
    mz_build(2026UL + 7919UL * (unsigned long)g.level);
    mz_build_walls();
    mz_bfs(mz_nest, g.nest_dist);
    g.crumbs_total = 0;
    for (c = 0; c < CELLS; c++) { g.crumb[c] = 0; g.key[c] = 0; }
    for (c = 0; c < CELLS; c++) {
        if (c == mz_start || c == mz_nest) continue;
        for (i = 0; i < 4; i++) if (mz_keys[i] == c) break;
        if (i < 4) { g.key[c] = 1; continue; }
        g.crumb[c] = 1;
        g.crumbs_total++;
    }
    g.crumbs_left = g.crumbs_total;
    ps = 1485 + 77 * lv;                           /* (5.8 + 0.3 lv) * 256 */
    if (ps > Q8(7.4)) ps = Q8(7.4);
    g.player_speed = ps;
    g.ghost_speed = (ps * (84 + 35 * lv / 10 > 97 ? 97 : 84 + 35 * lv / 10)) / 100;
    AB_I("level %ld seed=%ld crumbs=%ld walls=%ld", (long)g.level,
         (long)(2026 + 7919 * g.level), (long)g.crumbs_total, (long)mz_nwalls);
}

static void reset_actors(void)
{
    static const int release[4] = { 25, 150, 300, 450 };    /* 0.5, 3, 6, 9 s */
    int i, s = mz_start, denom = 6 + 4 * g.level;            /* (0.6 + 0.4 L) * 10 */
    V3 facing = mz_tan[s][0];
    if (denom < 10) denom = 10;
    mover_place(&g.player, s, facing);
    g.powered = 0;
    pend_x = pend_y = last_x = last_y = 0;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        mover_place(&gh->mv, mz_nest, mz_tan[mz_nest][i]);
        gh->mode = GM_NEST;
        gh->fright = 0;
        gh->release_at = (long)release[i] * 10 / denom;
    }
    g.power_left = 0;
    g.mode_index = 0;
    g.mode_timer = 0;
    g.scatter = 1;
    g.life = 0;
    for (i = 0; i < 8; i++) g.popup[i].age = -1;
    g.cam.focus = mz_dir[s];
    g.cam.up = v3_norm14(v3_sub(facing, v3_scale14(mz_dir[s], v3_dot14(facing, mz_dir[s]))));
    sfx_ambience(0);
}

static void start_game(void)
{
    g.score = 0;
    g.lives = START_LIVES;
    g.level = 1;
    g.extra_given = 0;
    build_level();
    reset_actors();
    set_state(GS_READY);
    sfx_play(SFX_START);
}

static void start_power(void)
{
    int i;
    long t = TICKS(9) - TICKS(1) * (g.level - 1);
    if (t < TICKS(3)) t = TICKS(3);
    g.power_total = g.power_left = t;
    g.combo = 0;
    g.powered = 1;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        if (gh->mode == GM_EATEN) continue;
        if (gh->mode == GM_ACTIVE) mover_reverse(&gh->mv);
        gh->fright = 1;
    }
    sfx_ambience(2);
    AB_I("key: invincible for %ld ticks", t);
}

static void end_power(void)
{
    int i;
    g.power_left = 0;
    g.powered = 0;
    for (i = 0; i < 4; i++) if (g.ghost[i].mode != GM_EATEN) g.ghost[i].fright = 0;
    sfx_ambience(1);
}

static void popup(V3 world, int pts)
{
    int i;
    for (i = 0; i < 8; i++)
        if (g.popup[i].age < 0) {
            g.popup[i].world = world;
            g.popup[i].pts = pts;
            g.popup[i].age = 0;
            return;
        }
}

static void check_catches(void)
{
    V3 pd = mover_dir(&g.player);
    int i;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        if (gh->mode == GM_EATEN) continue;
        if (v3_dot14(mover_dir(&gh->mv), pd) < CATCH_COS) continue;
        if (gh->fright) {
            int pts = 200 << (g.combo < 3 ? g.combo : 3);
            g.combo++;
            add_score(pts);
            gh->fright = 0;
            gh->mode = GM_EATEN;
            if (!gh->mv.moving && gh->mv.from == mz_nest) ghost_arrived(gh, mz_nest);
            popup(ghost_world(gh), pts);
            sfx_play(SFX_EAT);
            AB_I("eat %s +%ld", ghost_name[i], (long)pts);
        } else {
            AB_I("death caught by %s", ghost_name[i]);
            sfx_ambience(0);
            sfx_play(SFX_DEATH);
            g.power_left = 0;
            g.powered = 0;
            set_state(GS_DYING);
            return;
        }
    }
}

static void tick_playing(void)
{
    int i;
    g.life++;
    if (g.power_left > 0) {
        if (--g.power_left <= 0) end_power();
    } else if (++g.mode_timer >= mode_ticks[g.mode_index < 7 ? g.mode_index : 7]) {
        g.mode_timer = 0;
        g.mode_index++;
        g.scatter = (g.mode_index % 2) == 0;
        for (i = 0; i < 4; i++) if (g.ghost[i].mode == GM_ACTIVE) mover_reverse(&g.ghost[i].mv);
    }
    if (g.demo) bot_dodge();
    player_tick();
    if (g.state != GS_PLAYING) return;          /* the last crumb ends the level mid-tick */
    for (i = 0; i < 4; i++) ghost_tick(&g.ghost[i]);
    check_catches();
}

/* ---- camera (PlanetCamera.cs) ---- */

static void cam_transport(V3 f)
{
    fix d = v3_dot14(g.cam.up, f);
    V3 u = v3_sub(g.cam.up, v3_scale14(f, d));
    if (u.x | u.y | u.z) g.cam.up = v3_norm14(u);
    g.cam.focus = f;
}

static void cam_height(fix target, int div)
{
    g.cam.height += (target - g.cam.height) / div;
}

static void cam_follow(V3 target, unsigned long held)
{
    cam_transport(v3_norm14(v3_add(g.cam.focus, v3_scale14(v3_sub(target, g.cam.focus), 2700))));
    if (held & (PAD_L | PAD_R)) {               /* spin the view, ~110 deg/s */
        V3 side = v3_cross14(g.cam.focus, g.cam.up);
        fix s = (held & PAD_L) ? -629 : 629;
        g.cam.up = v3_norm14(v3_add(v3_scale14(g.cam.up, 16372), v3_scale14(side, s)));
    }
    cam_height(g.overview ? Q8(42) : Q8(13), 9);
}

static void cam_orbit(fix height)
{
    static V3 axis;
    if (!axis.z) { axis.x = 4729; axis.y = 3153; axis.z = 15766; }   /* (0.3, 0.2, 1) normalised */
    /* rotate the focus about the axis (~14 deg/s) and up about the focus */
    cam_transport(v3_norm14(v3_add(g.cam.focus, v3_scale14(v3_cross14(axis, g.cam.focus), 80))));
    g.cam.up = v3_norm14(v3_add(g.cam.up, v3_scale14(v3_cross14(g.cam.focus, g.cam.up), 34)));
    cam_height(height, 25);
}

/* ---- top level ---- */

void game_init(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        Ghost *gh = &g.ghost[i];
        V3 corner;
        corner.x = (i % 2 == 0) ? ONE14 : -ONE14;
        corner.y = (i < 2) ? ONE14 : -ONE14;
        corner.z = (i == 1 || i == 2) ? ONE14 : -ONE14;
        gh->scatter = v3_norm14(corner);
        gh->kind = i;
        gh->slot = i;
        gh->rng = 1234 + i;
    }
    g.level = 1;
    g.cam.height = Q8(30);
    build_level();
    reset_actors();
    set_state(GS_TITLE);
}

void game_step(unsigned long held, unsigned long pressed)
{
    int i;
    if (g.demo && g.state != GS_TITLE && (pressed & (PAD_A | PAD_B | PAD_C | PAD_P))) {
        g.demo = 0;
        sfx_ambience(0);
        set_state(GS_TITLE);
        return;
    }
    held_now = held;
    g.clock++;
    g.timer++;
    for (i = 0; i < 8; i++) if (g.popup[i].age >= 0 && ++g.popup[i].age > 60) g.popup[i].age = -1;

    /* buffered steering: a press is remembered until a junction allows it */
    if (pressed & PAD_UP)    { pend_x = 0;  pend_y = 1;  last_x = 0;  last_y = 1; }
    if (pressed & PAD_DOWN)  { pend_x = 0;  pend_y = -1; last_x = 0;  last_y = -1; }
    if (pressed & PAD_LEFT)  { pend_x = -1; pend_y = 0;  last_x = -1; last_y = 0; }
    if (pressed & PAD_RIGHT) { pend_x = 1;  pend_y = 0;  last_x = 1;  last_y = 0; }
    if ((last_x || last_y) && held_dir(last_x, last_y)) { pend_x = last_x; pend_y = last_y; }
    if (pressed & PAD_C) g.overview = !g.overview;

    switch (g.state) {
    case GS_TITLE:
    case GS_OVER:
        if (g.state == GS_OVER && g.demo && g.timer > TICKS(3)) {
            g.demo = 0;                       /* demo over: back to the title */
            set_state(GS_TITLE);
            break;
        }
        if ((pressed & (PAD_A | PAD_P)) && g.timer > TICKS(0.5)) { g.demo = 0; start_game(); break; }
        if (g.state == GS_TITLE && g.timer > TICKS(15)) {
            g.demo = 1;                       /* attract mode */
            AB_I("demo starts");
            start_game();
            break;
        }
        cam_orbit(Q8(30));
        if (g.state == GS_TITLE) {
            g.life++;
            for (i = 0; i < 4; i++) ghost_tick(&g.ghost[i]);
        }
        break;
    case GS_READY:
        cam_follow(mover_dir(&g.player), held);
        if (g.timer >= ((g.level == 1 && g.lives == START_LIVES) ? TICKS(2.2) : TICKS(1.6))) {
            set_state(GS_PLAYING);
            sfx_ambience(1);
        }
        break;
    case GS_PLAYING:
        if (pressed & PAD_P) g.paused = !g.paused;
        if (g.paused) break;
        tick_playing();
        cam_follow(mover_dir(&g.player), held);
        break;
    case GS_DYING:
        cam_follow(mover_dir(&g.player), held);
        if (g.timer >= TICKS(2.2)) {
            g.lives--;
            if (g.lives > 0) {
                reset_actors();
                set_state(GS_READY);
            } else {
                if (g.score >= g.hiscore && !g.demo) g.hiscore = g.score;
                set_state(GS_OVER);
            }
        }
        break;
    case GS_CLEAR:
        cam_follow(mover_dir(&g.player), held);
        if (g.timer >= TICKS(2.6)) {
            g.level++;
            build_level();
            reset_actors();
            set_state(GS_READY);
        }
        break;
    }
}
