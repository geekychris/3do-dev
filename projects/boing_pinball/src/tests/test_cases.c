// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * test_cases.c - Boing Pinball's tests: the browser game's test suite
 * (boing-pinball.test.js) ported, the collision functions and short flights
 * checked against vectors from the browser's own core (vectors.h), and every
 * table checked: the drain gap, the grid, every ramp, spinner, bank and the
 * saucer reachable, and bot-played games where no ball leaves the table or
 * sticks. On the host (make host-test), on the Amiga (boing_pinball_test)
 * and inside the game (the selftest hook).
 */
#include <string.h>
#include "a3d.h"
#include "../pb.h"
#include "vectors.h"

const char test_client[] = "boing_pinball_test";

/* on an Amiga (and inside the game) the long simulations are shortened */
#if defined(__amigaos__) || defined(__amigaos4__) || defined(AMIGA) || defined(__AMIGA__)
#define FAST_BALLS 16
#define BOT_GAMES  1
#define BOT_SECS   90
#else
#define FAST_BALLS 120
#define BOT_GAMES  4
#define BOT_SECS   400
#endif

static Table tt;                        /* big: off the stack */
static Game tg;

#define F(n, d)  ((fix)(((int32_t)(n) << 16) / (d)))      /* n/d as a fix, no floating point (the 68k build has none) */
static fix fabs_(fix v) { return v < 0 ? -v : v; }
static int near_(fix a, fix b, int tol_mm) { return fabs_(a - b) <= FROMI(tol_mm); }
static int nearf(fix a, fix b, fix tol) { return fabs_(a - b) <= tol; }
#define SECS(n, d) ((int)((n) * PB_HZ / (d)))           /* n/d seconds in steps */

static void run(World *w, int n)
{
    int t;
    for (t = 0; t < n; t++) { pb_world_step(w, PB_STEP); w->nev = 0; }
}

static int count_ev(World *w, int n, int type, Ball *b, fix *best_speed)
{
    int t, k, c = 0;
    for (t = 0; t < n; t++) {
        pb_world_step(w, PB_STEP);
        for (k = 0; k < w->nev; k++) if (w->ev[k].type == type) c++;
        w->nev = 0;
        if (best_speed && b) { fix s = fhypot(b->vx, b->vy); if (s > *best_speed) *best_speed = s; }
    }
    return c;
}

static int game_ev(Game *g, int type)
{
    int k, c = 0;
    for (k = 0; k < g->nev; k++) if (g->ev[k].type == type) c++;
    return c;
}

static void ticks(Game *g, int n, const PbInput *in)
{
    PbInput none;
    memset(&none, 0, sizeof none);
    none.pull_to = -1;
    while (n-- > 0) pb_game_tick(g, in ? in : &none);
}

static PbInput inp(int left, int right, int plunger, int pull_to)
{
    PbInput in;
    memset(&in, 0, sizeof in);
    in.left = left; in.right = right; in.plunger = plunger; in.pull_to = pull_to;
    return in;
}

static void drain_all(Game *g)
{
    int i;
    for (i = 0; i < g->w.nballs; i++) {
        Ball *b = &g->w.balls[i];
        b->state = BS_PLAY; b->x = FROMI(239); b->y = FROMI(-30); b->vx = 0; b->vy = FROMI(-500);
    }
    ticks(g, 30, NULL);
}

/* ---------------------------------------------------------------- the browser's vectors */

static void vectors(void (*check)(int, const char *))
{
    unsigned i;
    int ok;
    Ball b;
    Seg s;
    Circ c;
    Flipper f;
    World *w = &tg.w;

    ok = 1;
    for (i = 0; i < sizeof seg_vec / sizeof seg_vec[0]; i++) {
        const int32_t *v = seg_vec[i];
        fix imp;
        memset(&b, 0, sizeof b);
        b.x = v[0]; b.y = v[1]; b.vx = v[2]; b.vy = v[3];
        pb_seg_init(&s, v[4], v[5], v[6], v[7], v[8], v[9], SK_WALL);
        imp = pb_hit_segment(&b, &s);
        if (seg_imp[i] < 0 ? imp >= 0 : !near_(imp, seg_imp[i], 2)) ok = 0;
        if (!nearf(b.x, v[10], F(1, 20)) || !nearf(b.y, v[11], F(1, 20)) || !near_(b.vx, v[12], 2) || !near_(b.vy, v[13], 2)) ok = 0;
    }
    check(ok, "hitSegment matches the browser");

    ok = 1;
    for (i = 0; i < sizeof flip_vec / sizeof flip_vec[0]; i++) {
        const int32_t *v = flip_vec[i];
        memset(&b, 0, sizeof b);
        memset(&f, 0, sizeof f);
        b.x = v[0]; b.y = v[1]; b.vx = v[2]; b.vy = v[3];
        f.d.len = FROMI(80); f.d.r1 = FROMI(11); f.d.r2 = F(13, 2); f.ang = v[4]; f.w = v[5];
        pb_hit_flipper(&b, &f);
        if (!nearf(b.x, v[6], F(1, 10)) || !nearf(b.y, v[7], F(1, 10)) || !near_(b.vx, v[8], 4) || !near_(b.vy, v[9], 4)) ok = 0;
    }
    check(ok, "hitFlipper matches the browser");

    ok = 1;
    for (i = 0; i < sizeof circ_vec / sizeof circ_vec[0]; i++) {
        const int32_t *v = circ_vec[i];
        fix nx, ny;
        memset(&b, 0, sizeof b);
        memset(&c, 0, sizeof c);
        b.x = v[0]; b.y = v[1]; b.vx = v[2]; b.vy = v[3];
        c.cx = v[4]; c.cy = v[5]; c.r = v[6]; c.e = v[7];
        pb_hit_circle(&b, &c, &nx, &ny);
        if (!nearf(b.x, v[8], F(1, 20)) || !nearf(b.y, v[9], F(1, 20)) || !near_(b.vx, v[10], 2) || !near_(b.vy, v[11], 2)) ok = 0;
    }
    check(ok, "hitCircle matches the browser");

    pb_build_table(&tt, TB_COMET);
    for (i = 0; i < sizeof flight_vec / sizeof flight_vec[0]; i++) {
        const int32_t *v = flight_vec[i];
        Ball *bb;
        pb_world_init(w, &tt);
        bb = pb_add_ball(w, v[0], v[1], v[2], v[3], BS_PLAY);
        run(w, v[4]);
        check(near_(bb->x, v[5], 6) && near_(bb->y, v[6], 6), "a short flight on Comet Tour ends where the browser's does");
    }
}

/* ---------------------------------------------------------------- the browser's tests */

static void physics(void (*check)(int, const char *))
{
    World *w = &tg.w;
    Ball *b;
    uint32_t rnd = 42;
    int i, ok, k;

    pb_build_table(&tt, TB_COMET);

    /* a fast ball never leaves the table through a wall */
    ok = 1;
    for (i = 0; i < FAST_BALLS && ok; i++) {
        int sp = 2000 + (int)(a3_rand(&rnd) % 5000), a = (int)(a3_rand(&rnd) * 2);
        pb_world_init(w, &tt);
        b = pb_add_ball(w, FROMI(80 + (int)(a3_rand(&rnd) % 330)), FROMI(200 + (int)(a3_rand(&rnd) % 700)),
                        sp * a3_cos(a), sp * a3_sin(a), BS_PLAY);
        for (k = 0; k < 4 * PB_HZ; k++) {
            int j;
            for (j = 0; j < w->nflip; j++) w->flip[j].held = ((k * 7 / PB_HZ) + j) % 2 == 0;
            pb_world_step(w, PB_STEP); w->nev = 0;
            if (b->state != BS_PLAY) break;
            if (b->x < FROMI(-2) || b->x > FROMI(522) || b->y > FROMI(1042)) { ok = 0; break; }
        }
    }
    check(ok, "a fast ball never leaves the table through a wall");

    /* at the speed limit a ball still can't pass through a wall or a post */
    {
        static const int16_t c[4][4] = { { 200, 600, -1, 0 }, { 260, 900, 0, 1 }, { 180, 700, 0, 1 }, { 300, 500, 1, 0 } };
        ok = 1;
        for (i = 0; i < 4; i++) {
            pb_world_init(w, &tt);
            b = pb_add_ball(w, FROMI(c[i][0]), FROMI(c[i][1]), c[i][2] * PB_MAX_SPEED, c[i][3] * PB_MAX_SPEED, BS_PLAY);
            run(w, SECS(12, 100));
            if (b->x < PB_R - FONE || b->x > FROMI(520) - PB_R + FONE || b->y > FROMI(1040) - PB_R + FONE) ok = 0;
        }
        check(ok, "at the speed limit a ball can't pass through a wall or a post");
    }

    /* a ball dropped hard onto a raised flipper doesn't tunnel through it */
    {
        static const int vys[3] = { -3000, -6000, -7000 };
        ok = 1;
        for (i = 0; i < 3; i++) {
            Flipper *f;
            fix tx, ty, side;
            pb_world_init(w, &tt);
            f = &w->flip[0]; f->held = 1; f->ang = f->d.up;
            pb_flipper_tip(f, &tx, &ty);
            b = pb_add_ball(w, (f->d.px + tx) / 2, (f->d.py + ty) / 2 + FROMI(120), 0, FROMI(vys[i]), BS_PLAY);
            run(w, SECS(8, 100));
            side = FMUL(tx - f->d.px, b->y - f->d.py) - FMUL(ty - f->d.py, b->x - f->d.px);
            if (side <= 0) ok = 0;
        }
        check(ok, "a ball dropped hard onto a raised flipper doesn't tunnel through it");
    }

    /* a flipper swinging into a resting ball throws it, harder near the tip */
    {
        fix speed[2] = { 0, 0 }, vy = 0;
        static const int frac[2] = { 15, 85 };
        for (i = 0; i < 2; i++) {
            Flipper *f;
            fix tx, ty, px, py, nx, ny, rad;
            pb_world_init(w, &tt);
            f = &w->flip[0];
            pb_flipper_tip(f, &tx, &ty);
            px = f->d.px + (tx - f->d.px) * frac[i] / 100; py = f->d.py + (ty - f->d.py) * frac[i] / 100;
            nx = -fsin(f->ang); ny = fcos(f->ang);
            rad = f->d.r1 + (f->d.r2 - f->d.r1) * frac[i] / 100 + PB_R + F(1, 2);
            b = pb_add_ball(w, px + FMUL(nx, rad), py + FMUL(ny, rad), 0, 0, BS_PLAY);
            f->held = 1;
            count_ev(w, SECS(12, 100), -1, b, &speed[i]);
            vy = b->vy;
        }
        check(speed[1] > FROMI(2500), "a tip shot is fast (over 2500 mm/s)");
        check(speed[1] > FMUL(speed[0], FX(1.8)), "the tip throws harder than the base (w x r)");
        check(vy > 0, "the flipper throws the ball up the table");
    }

    /* hitFlipper: a resting ball takes (1 + e) x the surface speed */
    {
        Flipper f;
        Ball bb;
        memset(&f, 0, sizeof f); memset(&bb, 0, sizeof bb);
        f.d.len = FROMI(80); f.d.r1 = FROMI(11); f.d.r2 = F(13, 2); f.w = FROMI(30);
        bb.x = FROMI(60); bb.y = f.d.r1 + FMUL(f.d.r2 - f.d.r1, F(3, 4)) + PB_R - F(1, 2);
        check(pb_hit_flipper(&bb, &f) > 0 && bb.vy > FROMI(1800 * 14 / 10) && bb.vy < FROMI(1800 * 17 / 10),
              "a resting ball takes (1 + e) x the flipper's surface speed");
    }

    /* one-way gates */
    pb_world_init(w, &tt);
    b = pb_add_ball(w, FROMI(499), FROMI(700), 0, FROMI(2500), BS_PLAY);
    run(w, SECS(1, 10));
    check(b->y > FROMI(800), "the plunger lane's gate lets a ball up");
    pb_world_init(w, &tt);
    b = pb_add_ball(w, FROMI(499), FROMI(830), 0, FROMI(-1500), BS_PLAY);
    run(w, SECS(6, 100));
    check(b->y > FROMI(792), "and stops it coming back down");

    /* the ramp */
    {
        int made, enter, fail;
        pb_world_init(w, &tt);
        b = pb_add_ball(w, FROMI(354), FROMI(520), 0, FROMI(2500), BS_PLAY);
        enter = 0; made = 0;
        for (k = 0; k < 300; k++) {
            int e;
            pb_world_step(w, PB_STEP);
            for (e = 0; e < w->nev; e++) { if (w->ev[e].type == EV_RAMPENTER) enter++; if (w->ev[e].type == EV_RAMP) made++; }
            w->nev = 0;
        }
        check(enter && made, "a strong shot goes up the ramp and round to the left inlane");
        pb_world_init(w, &tt);
        b = pb_add_ball(w, FROMI(354), FROMI(520), 0, FROMI(650), BS_PLAY);
        made = 0; fail = 0;
        for (k = 0; k < 240; k++) {
            int e;
            pb_world_step(w, PB_STEP);
            for (e = 0; e < w->nev; e++) { if (w->ev[e].type == EV_RAMPFAIL) fail++; if (w->ev[e].type == EV_RAMP) made++; }
            w->nev = 0;
        }
        check(fail && !made && b->state != BS_RAMP, "a weak one rolls back out");
    }

    /* slingshots kick; pop bumpers push out at least their kick speed */
    pb_world_init(w, &tt);
    b = pb_add_ball(w, FROMI(140), FROMI(280), FROMI(-600), FROMI(-300), BS_PLAY);
    check(count_ev(w, SECS(3, 10), EV_SLING, NULL, NULL) > 0, "the slingshot kicks");
    {
        fix out = 0;
        const Circ *bu = NULL;
        for (i = 0; i < tt.ncirc; i++) if (tt.circ[i].bumper) { bu = &tt.circ[i]; break; }
        pb_world_init(w, &tt);
        b = pb_add_ball(w, bu->cx, bu->cy + bu->r + PB_R + FROMI(5), 0, FROMI(-200), BS_PLAY);
        for (k = 0; k < 6; k++) {
            int e;
            pb_world_step(w, PB_STEP);
            for (e = 0; e < w->nev; e++) if (w->ev[e].type == EV_BUMPER) { fix s = fhypot(b->vx, b->vy); if (s > out) out = s; }
            w->nev = 0;
        }
        check(out >= FROMI(1000), "a pop bumper pushes the ball out at 1000 mm/s or more");
    }

    /* the spinner counts spins in proportion to the speed */
    {
        int slow, fast;
        pb_world_init(w, &tt);
        pb_add_ball(w, FROMI(25), FROMI(560), 0, FROMI(1200), BS_PLAY);
        slow = count_ev(w, SECS(3, 1), EV_SPIN, NULL, NULL);
        pb_world_init(w, &tt);
        pb_add_ball(w, FROMI(25), FROMI(560), 0, FROMI(3500), BS_PLAY);
        fast = count_ev(w, SECS(3, 1), EV_SPIN, NULL, NULL);
        check(slow >= 1 && fast * 2 > slow * 3, "the spinner counts spins in proportion to the ball's speed");
    }
}

static void rules(void (*check)(int, const char *))
{
    Game *g = &tg;
    Event e;
    PbInput in;
    int i, k;

    pb_build_table(&tt, TB_COMET);

    /* drop targets fall, the bank completes and comes back up */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    {
        int ok = 1;
        for (i = 0; i < tt.ndrop; i++) {
            Seg *d = &tt.seg[tt.drop_seg[i]];
            Ball *b;
            g->w.nballs = 0;
            b = pb_add_ball(&g->w, FROMI(400), (d->ay + d->by) / 2, FROMI(1500), 0, BS_PLAY);
            for (k = 0; k < 60 && b->state == BS_PLAY && d->up; k++) ticks(g, 1, NULL);
            if (d->up) ok = 0;
        }
        g->w.nballs = 0;
        check(ok, "each drop target falls when hit");
        check(g->banks == 1, "knocking all three completes the bank");
        ticks(g, 200, NULL);
        ok = 1;
        for (i = 0; i < tt.ndrop; i++) if (!tt.seg[tt.drop_seg[i]].up) ok = 0;
        check(ok, "the bank comes back up");
    }

    /* the saucer holds a ball and kicks it out without catching it straight back */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    g->w.nballs = 0;
    {
        Ball *b = pb_add_ball(&g->w, tt.saucer_x, tt.saucer_y + FROMI(30), 0, FROMI(-200), BS_PLAY);
        int caught = 0, kicked = 0;
        for (i = 0; i < 600 && !kicked; i++) {
            g->nev = 0;
            ticks(g, 1, NULL);
            caught += game_ev(g, EV_SAUCER); kicked += game_ev(g, EV_KICK);
        }
        ticks(g, 60, NULL);
        check(caught == 1 && kicked == 1 && b->state != BS_SAUCER, "the saucer holds the ball, kicks it out and lets it go");
    }

    /* the plunger and the skill shot */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    check(g->w.nballs == 1 && g->w.balls[0].state == BS_LANE, "a game starts with a ball in the plunger lane");
    in = inp(0, 0, 1, -1);
    ticks(g, 60, &in);
    check(g->pull > FX(0.45) && g->pull < FX(0.55), "half a second's pull is half the plunger");
    ticks(g, 1, NULL);
    check(g->w.balls[0].state == BS_PLAY && g->w.balls[0].vy > FROMI(2500), "letting go launches the ball");
    g->nev = 0;
    memset(&e, 0, sizeof e);
    e.type = EV_SENSOR; e.kind = SN_LANE; e.id = (int8_t)g->skill_lane; e.up = 0; e.x = 200; e.y = 832;
    pb_game_handle(g, &e);
    check(game_ev(g, EV_SKILL) == 1 && g->score >= 10000, "the lit lane on the way down is the skill shot");

    /* a weak launch rolls back down the lane and can be launched again */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    in = inp(0, 0, 0, 0);
    ticks(g, 1, &in); ticks(g, 1, NULL);
    for (i = 0; i < PB_SECS(3) && g->w.balls[0].state != BS_LANE; i++) ticks(g, 1, NULL);
    check(g->w.balls[0].state == BS_LANE, "a weak launch rolls back onto the plunger");

    pb_game_init(g, &tt, 7); pb_game_start(g);
    in = inp(0, 0, 0, 800);
    ticks(g, 1, &in);
    check(g->pull == F(800, 1000), "pull_to sets the plunger directly");
    ticks(g, 1, NULL);
    check(g->w.balls[0].state == BS_PLAY, "and letting go fires it");

    /* lane change */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    g->skill_armed = 0;
    g->lanes[0] = 1; g->lanes[1] = 0; g->lanes[2] = 0;
    in = inp(0, 1, 0, -1);
    ticks(g, 1, &in);
    check(!g->lanes[0] && g->lanes[1] && !g->lanes[2], "the right flipper moves the lit lane along");
    ticks(g, 1, &in);
    check(g->lanes[1] && !g->lanes[2], "held, it doesn't move it again");
    ticks(g, 1, NULL);
    in = inp(1, 0, 0, -1);
    ticks(g, 1, &in);
    check(g->lanes[0] && !g->lanes[1], "the left flipper moves it back");
    memset(&e, 0, sizeof e);
    e.type = EV_SENSOR; e.kind = SN_LANE; e.id = 1; pb_game_handle(g, &e);
    e.id = 2; pb_game_handle(g, &e);
    check(g->bonus_x == 2 && !g->lanes[0] && !g->lanes[1] && !g->lanes[2], "all three lanes raise the bonus multiplier");

    /* tilt */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    for (i = 0; i < 4; i++) { in = inp(0, 0, 0, -1); in.nudge_x = 1; ticks(g, 1, &in); ticks(g, 240, NULL); }
    check(g->tilt_warnings == 0, "spaced nudges don't warn");
    in = inp(0, 0, 0, -1); in.nudge_x = -1;
    ticks(g, 2, &in);
    check(g->tilt_warnings == 1, "two quick nudges warn");
    ticks(g, 30, NULL); ticks(g, 2, &in);
    check(g->tilt_warnings == 2 && !g->tilted, "two more warn again");
    ticks(g, 30, NULL); ticks(g, 2, &in);
    check(g->tilted && !g->w.flipper_power, "the third time it tilts");
    in = inp(1, 1, 0, -1);
    ticks(g, 30, &in);
    {
        int down = 1;
        for (i = 0; i < g->w.nflip; i++) if (g->w.flip[i].ang != g->w.flip[i].d.rest) down = 0;
        check(down, "tilted, the flippers stay down");
    }

    /* a nudge pushes the ball */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    g->w.nballs = 0;
    {
        Ball *b = pb_add_ball(&g->w, FROMI(239), FROMI(600), 0, 0, BS_PLAY);
        in = inp(0, 0, 0, -1); in.nudge_x = 1;
        ticks(g, 1, &in);
        check(b->vx > FROMI(150), "a nudge pushes the ball");
    }

    /* ball save, the bonus, game over after three balls */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    in = inp(0, 0, 0, 1000); ticks(g, 1, &in); ticks(g, 1, NULL);
    g->nev = 0;
    drain_all(g);
    check(game_ev(g, EV_SAVE) == 1 && g->ball_no == 1, "a quick drain is saved");
    g->save_until = 0;
    drain_all(g);
    check(g->state == GS_BONUS, "then the ball ends with the bonus");
    ticks(g, 400, NULL);
    check(g->state == GS_PLAY && g->ball_no == 2, "and the next ball comes");
    for (i = 0; i < 2; i++) { g->save_until = 0; g->plunge_at = 0; drain_all(g); ticks(g, 400, NULL); g->save_until = 0; }
    {
        Dmd d;
        pb_dmd(g, &d);
        check(g->state == GS_OVER && game_ev(g, EV_GAMEOVER) == 1 && !strcmp(d.big, "GAME OVER"), "game over after three balls");
    }

    /* a tilted ball scores no bonus */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    g->bonus_ramps = 5;
    pb_do_tilt(g);
    {
        int32_t before = g->score;
        drain_all(g);
        check(g->state == GS_BONUS && g->bonus_pts == 0 && g->score == before, "a tilted ball scores no bonus");
    }

    /* tilted with a ball in the plunger lane: it can still be launched, and the ball ends */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    pb_do_tilt(g);
    in = inp(0, 0, 0, 1000); ticks(g, 1, &in); ticks(g, 1, NULL);
    check(g->w.nballs == 1 && g->w.balls[0].state == BS_PLAY, "tilted, a ball in the lane can still be launched");
    for (i = 0; i < PB_SECS(30) && g->state == GS_PLAY; i++) ticks(g, 1, NULL);
    check(g->state == GS_BONUS || g->ball_no == 2, "and the tilted ball ends");

    /* multiball and jackpots */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    in = inp(0, 0, 0, 1000); ticks(g, 1, &in); ticks(g, 1, NULL);
    memset(&e, 0, sizeof e);
    e.type = EV_RAMP; e.x = 79; e.y = 362;
    for (i = 0; i < 3; i++) pb_game_handle(g, &e);
    check(g->multiball, "three ramp shots start multiball");
    ticks(g, 400, NULL);
    check(pb_live_balls(g) == 3, "with two more balls");
    {
        int32_t s = g->score;
        pb_game_handle(g, &e);
        check(g->score - s >= 50000, "a ramp in multiball scores a jackpot");
    }
    g->save_until = 0;
    for (i = 0; i < 2; i++) { Ball *b = &g->w.balls[i]; b->state = BS_PLAY; b->x = FROMI(239); b->y = FROMI(-30); b->vy = FROMI(-500); }
    ticks(g, 10, NULL);
    check(pb_live_balls(g) == 1 && !g->multiball && g->state == GS_PLAY, "multiball ends at one ball");

    /* missions */
    pb_game_init(g, &tt, 7); pb_game_start(g);
    memset(&e, 0, sizeof e);
    e.type = EV_SAUCER; e.x = 112; e.y = 560; e.ball = -1;
    pb_game_handle(g, &e);
    check(g->mission >= 0, "the saucer starts a mission");
    {
        int m = g->mission;
        int32_t s = g->score;
        for (i = 0; i < pb_missions[m].need; i++) pb_mission_hit(g, pb_missions[m].ev);
        check(g->mission < 0 && (g->missions_done & (1 << m)) && g->score - s >= pb_missions[m].award, "meeting its goal finishes it");
        pb_game_handle(g, &e);
        check(g->mission >= 0 && g->mission != m, "the next is a different one");
        g->time = g->mission_until + 1;
        g->nev = 0;
        ticks(g, 1, NULL);
        check(g->mission < 0 && game_ev(g, EV_MISSIONFAIL) == 1, "the clock ends it");
    }
    pb_game_init(g, &tt, 7); pb_game_start(g);
    g->missions_done = 31;
    pb_game_handle(g, &e);
    {
        int32_t s;
        check(g->supernova > g->time, "all five missions lead to Supernova");
        s = g->score;
        pb_award(g, 100, -9999, 0);
        check(g->score - s == 300, "which scores everything 3x");
    }

    /* the display */
    pb_game_init(g, &tt, 7);
    {
        Dmd d;
        pb_dmd(g, &d);
        check(!strcmp(d.big, "COMET TOUR"), "the display shows the table's name before a game");
        pb_game_start(g);
        g->time = PB_SECS(10); g->nmsg = 0;
        g->score = 1234560;
        pb_dmd(g, &d);
        check(!strcmp(d.big, "1,234,560"), "then the score, with commas");
        g->mission = 0; g->mission_n = 3; g->mission_until = g->time + PB_SECS(12.2);
        pb_dmd(g, &d);
        check(!strcmp(d.small, "VENUS 3/25 13S"), "the mission's count and clock");
        pb_say(g, "JACKPOT", "50,000", PB_SECS(2));
        pb_dmd(g, &d);
        check(!strcmp(d.big, "JACKPOT"), "and messages over them");
    }

    /* crossed() */
    {
        Sensor s;
        memset(&s, 0, sizeof s);
        s.bx = FROMI(10);
        check(pb_crossed(FROMI(5), FROMI(-1), FROMI(5), FROMI(1), &s) == 1 && pb_crossed(FROMI(5), FROMI(1), FROMI(5), FROMI(-1), &s) == -1 &&
              pb_crossed(FROMI(15), FROMI(-1), FROMI(15), FROMI(1), &s) == 0 && pb_crossed(FROMI(5), FROMI(1), FROMI(5), FROMI(2), &s) == 0,
              "sensor crossings by direction");
    }
}

/* ---------------------------------------------------------------- every table */

/* the events seen, a bit each; only bits 48-55 are ever compared, so the
 * 3DO (no 64-bit integers) keeps bits 32-63 in 32 */
#ifdef AMIGA3DO
typedef uint32_t SEEN_T;
#define BIT64(n) ((n) >= 32 ? (uint32_t)1 << ((n) - 32) : (uint32_t)0)
#else
typedef uint64_t SEEN_T;
#define BIT64(n) ((uint64_t)1 << (n))
#endif

/* Drop a ball onto a flipper's inlane side from above and flip after a
 * delay; returns the events seen in the next 2.5 s (a bit per type). */
static SEEN_T shot(int table, int flip, int drop_x, int delay)
{
    World *w = &tg.w;
    Ball *b;
    SEEN_T seen = 0;
    int t, k;
    Flipper *f;
    pb_world_init(w, &tt);
    f = &w->flip[flip];
    b = pb_add_ball(w, f->d.px + FROMI(drop_x), f->d.py + FROMI(90), 0, FROMI(-200), BS_PLAY);
    (void)table;
    for (t = 0; t < PB_SECS(3); t++) {
        f->held = t >= delay && t < delay + PB_SECS(0.3);
        pb_world_step(w, PB_STEP);
        for (k = 0; k < w->nev; k++) {
            int ty = w->ev[k].type;
            if (ty == EV_SENSOR) ty = 40 + w->ev[k].kind;
            if (ty == EV_RAMP || ty == EV_SPINHIT || ty == EV_DROP || ty == EV_RAMPENTER) seen |= BIT64(ty + 0);
            if (ty == EV_RAMP) seen |= BIT64(48 + w->ev[k].id);
            if (ty == EV_SPINHIT) seen |= BIT64(50 + w->ev[k].id);
            if (ty == EV_DROP) seen |= BIT64(52 + tt.seg[tt.drop_seg[w->ev[k].id]].bank);
            if (ty == EV_SAUCER) seen |= BIT64(54);
            if (ty == EV_BUMPER) seen |= BIT64(55);
        }
        w->nev = 0;
        if (b->state == BS_DRAINED) break;
    }
    return seen;
}

static void tables(void (*check)(int, const char *))
{
    int t, i, gy, gx;
    for (t = 0; t < TB_COUNT; t++) {
        SEEN_T all = 0, want = 0;
        int over = 0, fl, x, d;
        char name[160];
        pb_build_table(&tt, t);

        /* the drain gap between the lower flippers fits one ball, not two */
        {
            Flipper a, b;
            fix ax, ay, bx, by, gap;
            memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
            a.d = tt.flip[0]; a.ang = a.d.rest; b.d = tt.flip[1]; b.ang = b.d.rest;
            pb_flipper_tip(&a, &ax, &ay); pb_flipper_tip(&b, &bx, &by);
            gap = fhypot(bx - ax, by - ay) - a.d.r2 - b.d.r2;
            strcpy(name, tt.name); strcat(name, ": the drain gap fits one ball, not two");
            check(gap > 2 * PB_R && gap < 4 * PB_R, name);
        }
        for (gy = 0; gy < PB_GY; gy++) for (gx = 0; gx < PB_GX; gx++) if (tt.ngrid[gy][gx] > PB_GMAX) over = 1;
        strcpy(name, tt.name); strcat(name, ": every part fits the collision grid");
        check(!over, name);

        /* every ramp, spinner, bank and the saucer can be hit from a flipper */
        for (i = 0; i < tt.nramp; i++) want |= BIT64(48 + i);
        for (i = 0; i < tt.nspin; i++) want |= BIT64(50 + i);
        for (i = 0; i < tt.nbank; i++) want |= BIT64(52 + i);
        want |= BIT64(54);
        want |= BIT64(55);
        for (fl = 0; fl < tt.nflip && (all & want) != want; fl++)
            for (x = -30; x <= 60 && (all & want) != want; x += 6)
                for (d = 4; d <= 40 && (all & want) != want; d += 3) {
                    int dx = tt.flip[fl].side ? -x : x;
                    all |= shot(t, fl, dx, d);
                }
        strcpy(name, tt.name); strcat(name, ": every ramp, spinner, target bank, the saucer and the bumpers can be shot from a flipper");
        check((all & want) == want, name);

        /* bot-played games: no ball leaves the table or sticks */
        {
            int gnum, escaped = 0, stuck = 0;
            for (gnum = 0; gnum < BOT_GAMES; gnum++) {
                Bot bot;
                PbInput in;
                int32_t s, still = 0;
                fix lx = 0, ly = 0;
                memset(&bot, 0, sizeof bot);
                bot.rng = 777 + gnum;
                pb_game_init(&tg, &tt, 31 + gnum);
                pb_game_start(&tg);
                for (s = 0; s < PB_SECS(BOT_SECS) && tg.state != GS_OVER; s++) {
                    pb_bot(&tg, &in, &bot);
                    pb_game_tick(&tg, &in);
                    tg.nev = 0;
                    for (i = 0; i < tg.w.nballs; i++) {
                        Ball *b = &tg.w.balls[i];
                        if (b->state == BS_PLAY && (b->x < FROMI(-3) || b->x > FROMI(523) || b->y > FROMI(1043))) { escaped++; b->state = BS_DRAINED; }
                    }
                    if (tg.w.nballs && tg.w.balls[0].state == BS_PLAY) {
                        if (fabs_(tg.w.balls[0].x - lx) > FROMI(30) || fabs_(tg.w.balls[0].y - ly) > FROMI(30)) { still = 0; lx = tg.w.balls[0].x; ly = tg.w.balls[0].y; }
                        else if (++still > PB_SECS(8)) { stuck++; break; }
                    } else still = 0;
                }
                if (s >= PB_SECS(BOT_SECS) && BOT_SECS >= 400) stuck++;     /* a game that never ends: a loop */
            }
            strcpy(name, tt.name); strcat(name, ": in bot-played games no ball leaves the table");
            check(!escaped, name);
            strcpy(name, tt.name); strcat(name, ": or sticks, or loops for ever");
            check(!stuck, name);
        }
    }
}

int run_tests(void (*check)(int cond, const char *name))
{
    vectors(check);
    physics(check);
    rules(check);
    tables(check);
    return 0;
}
