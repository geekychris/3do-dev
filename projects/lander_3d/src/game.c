// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Lander 3D's shell on the shared engine: input, the title (the
 * autopilot flying a demo), flying (four 1/100 s simulation steps a
 * frame), the outcome of each landing, ships and levels, sound, and the
 * AmigaBridge interface:
 *
 *   vars   score ships level world state outcome agl vs hs tilt fuel
 *          throttle x z y pad_dist autopilot fps polys us_update us_draw
 *          probe_used menu paused help ground_win (+ in_keys in_joy)
 *   hooks  status, start <moon|mars|asteroid|tour> [level], title,
 *          autopilot <on|off>, place <pad|high|offpad> (over the x1 pad, for
 *          tests), help <0-2>, pause <on|off>, activate, selftest
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "ld.h"
#include "lander.h"

const char game_title[] = "Lander 3D";
const char game_client[] = "lander_3d";
const char game_music[] = "PROGDIR:lander_3d.mod";
const char game_voice[] = "";

#define IN_LEFT   0x0001
#define IN_RIGHT  0x0002
#define IN_UP     0x0004
#define IN_DOWN   0x0008
#define IN_FIRE   0x0010               /* full thrust / start */
#define IN_THR_UP 0x0020
#define IN_THR_DN 0x0040
#define IN_PAUSE  0x0100
#define IN_ESC    0x0200
#define IN_START  0x0400
#define IN_HELP   0x0800

enum { SFX_THRUST, SFX_RCS, SFX_LAND, SFX_CRASH, SFX_FUEL, SFX_BEEP };

Lander lander;
Level level;
static unsigned prev_in, held_in;
static uint32_t next_seed = 2026;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_agl, v_vs, v_hs, v_tilt, v_fuel, v_thr, v_x, v_z, v_y, v_pad, v_world, v_outcome, v_probe;
static int32_t v_fps, v_polys, us_update, us_draw;
static int selftest_pending, test_pass, test_fail;
static int probing;                     /* the level's autopilot check, running in the background */

unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x4F: case 0x20: return IN_LEFT;      /* cursor left, A */
    case 0x4E: case 0x22: return IN_RIGHT;     /* cursor right, D */
    case 0x4C: case 0x11: return IN_UP;        /* cursor up, W */
    case 0x4D: case 0x21: return IN_DOWN;      /* cursor down, S */
    case 0x40: case 0x26: return IN_FIRE;      /* space, J */
    case 0x12: return IN_THR_UP;               /* E */
    case 0x10: return IN_THR_DN;               /* Q */
    case 0x19: return IN_PAUSE;                /* P */
    case 0x45: return IN_ESC;
    case 0x44: case 0x50: return IN_START;     /* return, F1 */
    case 0x5F: case 0x25: return IN_HELP;      /* HELP, H */
    }
    return 0;
}

unsigned game_joy_bits(unsigned j)
{
    return (j & A3_JOY_LEFT ? IN_LEFT : 0) | (j & A3_JOY_RIGHT ? IN_RIGHT : 0) |
           (j & A3_JOY_UP ? IN_UP : 0) | (j & A3_JOY_DOWN ? IN_DOWN : 0) | (j & A3_JOY_FIRE ? IN_FIRE : 0);
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_THRUST, 1200, 40, 10))) a3_noise(d, 1200, 70, 10, 60);
    if ((d = a3_sfx_define(SFX_RCS, 300, 22, 8))) a3_noise(d, 300, 50, 1, 10);
    if ((d = a3_sfx_define(SFX_LAND, 4000, 54, 60))) {
        a3_tone(d, 1300, 523, 523, 90, 100);
        a3_tone(d + 1300, 1300, 659, 659, 90, 100);
        a3_tone(d + 2600, 1400, 784, 784, 100, 1000);
    }
    if ((d = a3_sfx_define(SFX_CRASH, 5000, 64, 60))) a3_noise(d, 5000, 127, 3, 20);
    if ((d = a3_sfx_define(SFX_FUEL, 2400, 50, 40))) { a3_tone(d, 1200, 880, 880, 90, 60); a3_tone(d + 1200, 1200, 660, 660, 90, 300); }
    if ((d = a3_sfx_define(SFX_BEEP, 300, 30, 20))) a3_tone(d, 300, 1400, 1400, 70, 150);
}

/* ---------- game flow ---------- */
static int world_for(int menu, int level_no) { return menu < W_COUNT ? menu : (level_no - 1) % W_COUNT; }

static void begin_level(void)
{
    int w = world_for(lander.menu, lander.level);
    ld_new_level(&level, w, lander.level, next_seed++, 0);
    ld_probe_begin(&level);
    probing = 1;
    render_palette(w);
    lander.steady = 0;
    AB_I("LEVEL %s %ld seed %ld fuel %ld", ld_worlds[w].name, (long)lander.level, (long)level.seed,
         (long)FINT(level.fuel));
}

static void start_game(int menu, int lv)
{
    lander.menu = menu;
    lander.level = lv < 1 ? 1 : lv;
    lander.ships = 3;
    lander.score = 0;
    lander.demo = 0;
    lander.paused = lander.help = 0;
    lander.state = LS_PLAY;
    begin_level();
}

static void to_title(void)
{
    lander.state = LS_TITLE;
    lander.demo = 1;
    render_title_start();
    ld_new_level(&level, W_MOON, 2, a3_hash(next_seed++), 0);
    render_palette(W_MOON);
}

static void outcome(void)
{
    const LdWorld *W = &ld_worlds[level.world];
    LdResult *r = &level.result;
    lander.state = LS_RESULT;
    lander.state_timer = 100;
    lander.last_score = r->score;
    lander.score += r->score;
    switch (level.outcome) {
    case OUT_LANDED:
        strcpy(lander.msg, r->pad == 5 ? "PERFECT x5" : r->pad == 2 ? "LANDED x2" : "LANDED");
        sprintf(lander.msg2, "SOFT %ld  FUEL %ld", (long)r->soft, (long)r->fuel_bonus);
        a3_sfx(SFX_LAND);
        break;
    case OUT_OFFPAD:
        strcpy(lander.msg, "OFF THE PAD");
        strcpy(lander.msg2, "SAFE, BUT NOT ON A PAD");
        a3_sfx(SFX_BEEP);
        break;
    case OUT_CRASHED:
        strcpy(lander.msg, "CRASHED");
        if (!r->vs_ok) sprintf(lander.msg2, "DOWN AT %ld.%ld M/S (LIMIT %ld.%ld)", (long)FINT(r->vs), (long)(r->vs * 10 >> 16) % 10,
                               (long)FINT(W->lim_vs), (long)(W->lim_vs * 10 >> 16) % 10);
        else if (!r->hs_ok) sprintf(lander.msg2, "SLIDING AT %ld.%ld M/S", (long)FINT(r->hs), (long)(r->hs * 10 >> 16) % 10);
        else if (!r->tilt_ok) sprintf(lander.msg2, "TILTED %ld DEGREES", (long)FINT(FMUL(r->tilt, FX(57.29578))));
        else strcpy(lander.msg2, "THE GROUND WAS TOO STEEP");
        lander.ships--;
        a3_sfx(SFX_CRASH);
        break;
    default:
        strcpy(lander.msg, "LOST");
        strcpy(lander.msg2, "LEFT THE LANDING AREA");
        lander.ships--;
        a3_sfx(SFX_CRASH);
        break;
    }
    if (lander.score > lander.best) lander.best = lander.score;
    AB_I("OUTCOME %s score %ld total %ld ships %ld | vs %ld.%02ld hs %ld.%02ld tilt %ld pad x%ld",
         ld_outcome_names[level.outcome], (long)r->score, (long)lander.score, (long)lander.ships,
         (long)FINT(r->vs), (long)((r->vs < 0 ? -r->vs : r->vs) * 100 >> 16) % 100, (long)FINT(r->hs),
         (long)(r->hs * 100 >> 16) % 100, (long)FINT(FMUL(r->tilt, FX(57.29578))), (long)r->pad);
    if (lander.ships <= 0) { lander.state = LS_OVER; lander.state_timer = 50; }
}

static void after_result(void)
{
    if (level.outcome == OUT_LANDED || level.outcome == OUT_OFFPAD) lander.level++;
    lander.state = LS_PLAY;
    begin_level();
}

/* ---------- bridge ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[200];
    static const char *const st[] = { "title", "play", "result", "over" };
    (void)args;
    sprintf(tmp, "%s %s level %ld | agl %ld vs %ld hs %ld tilt %ld fuel %ld%% thr %ld%% | %s | score %ld ships %ld | %s | fps %ld",
            st[lander.state], ld_worlds[level.world].name, (long)level.level, (long)v_agl, (long)v_vs, (long)v_hs,
            (long)v_tilt, (long)v_fuel, (long)v_thr, ld_outcome_names[level.outcome], (long)lander.score,
            (long)lander.ships, lander.demo ? "autopilot" : "pilot", (long)v_fps);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int menu = 0, lv = 1;
    char tmp[64];
    if (args && args[0]) {
        if (!strncmp(args, "mars", 4)) menu = 1;
        else if (!strncmp(args, "ast", 3)) menu = 2;
        else if (!strncmp(args, "tour", 4)) menu = 3;
        { const char *sp = strchr(args, ' '); if (sp) lv = atoi(sp + 1); }
    }
    start_game(menu, lv);
    sprintf(tmp, "%s level %ld, fuel %ld s", ld_worlds[level.world].name, (long)level.level, (long)FINT(level.fuel));
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autopilot(const char *args, char *buf, int bufsz)
{
    lander.demo = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, lander.demo ? "the autopilot is flying" : "you are flying");
    return 0;
}

static int hook_place(const char *args, char *buf, int bufsz)
{
    const Pad *p = &level.T.pads[0];
    level.c.p[0] = p->x;
    level.c.p[2] = p->z;
    level.c.v[0] = level.c.v[1] = level.c.v[2] = 0;
    level.c.pitch = level.c.roll = level.c.wp = level.c.wr = 0;
    level.c.p[1] = p->y + FROMI(args && !strncmp(args, "high", 4) ? 60 : 6);
    if (args && !strncmp(args, "off", 3)) level.c.p[0] += p->r + FROMI(6);
    reply(buf, bufsz, "placed over the x1 pad");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    lander.help = args && args[0] ? atoi(args) : 1;
    if (lander.help < 0 || lander.help > 2) lander.help = 0;
    reply(buf, bufsz, lander.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    lander.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;
    reply(buf, bufsz, lander.paused ? "paused" : "running");
    return 0;
}

static int hook_activate(const char *args, char *buf, int bufsz) { (void)args; plat_activate(); reply(buf, bufsz, "window active"); return 0; }

static void bridge_check(int cond, const char *name)
{
    if (cond) test_pass++; else test_fail++;
    ab_test_assert(cond, name, "test_cases.c", 0);
}

static int hook_selftest(const char *args, char *buf, int bufsz)
{
    (void)args;
    selftest_pending = 1;
    reply(buf, bufsz, "self-test queued; results stream as TEST_ messages");
    return 0;
}

static void register_bridge(void)
{
    ab_register_var("score",      AB_TYPE_I32, &lander.score);
    ab_register_var("ships",      AB_TYPE_I32, &lander.ships);
    ab_register_var("level",      AB_TYPE_I32, &lander.level);
    ab_register_var("world",      AB_TYPE_I32, &v_world);
    ab_register_var("state",      AB_TYPE_I32, &lander.state);
    ab_register_var("outcome",    AB_TYPE_I32, &v_outcome);
    ab_register_var("agl",        AB_TYPE_I32, &v_agl);
    ab_register_var("vs",         AB_TYPE_I32, &v_vs);
    ab_register_var("hs",         AB_TYPE_I32, &v_hs);
    ab_register_var("tilt",       AB_TYPE_I32, &v_tilt);
    ab_register_var("fuel",       AB_TYPE_I32, &v_fuel);
    ab_register_var("throttle",   AB_TYPE_I32, &v_thr);
    ab_register_var("x",          AB_TYPE_I32, &v_x);
    ab_register_var("y",          AB_TYPE_I32, &v_y);
    ab_register_var("z",          AB_TYPE_I32, &v_z);
    ab_register_var("pad_dist",   AB_TYPE_I32, &v_pad);
    ab_register_var("autopilot",  AB_TYPE_I32, &lander.demo);
    ab_register_var("probe_used", AB_TYPE_I32, &v_probe);
    ab_register_var("menu",       AB_TYPE_I32, &lander.menu);
    ab_register_var("paused",     AB_TYPE_I32, &lander.paused);
    ab_register_var("help",       AB_TYPE_I32, &lander.help);
    ab_register_var("fps",        AB_TYPE_I32, &v_fps);
    ab_register_var("polys",      AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",  AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",    AB_TYPE_I32, &us_draw);
    ab_register_var("ground_win", AB_TYPE_I32, &ground_win);

    ab_register_hook("status",    "One-line game status", hook_status);
    ab_register_hook("start",     "start moon/mars/asteroid/tour [level]", hook_start);
    ab_register_hook("title",     "Back to the title", hook_title);
    ab_register_hook("autopilot", "autopilot on/off: let it fly", hook_autopilot);
    ab_register_hook("place",     "place pad/high/offpad: put the craft over the x1 pad (tests)", hook_place);
    ab_register_hook("help",      "help N: rules page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",     "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("activate",  "The game's window to the front and active (injected keys go to it)", hook_activate);
    ab_register_hook("selftest",  "Run the tests inside the game", hook_selftest);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    render_init_models();
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    prev_in = in;
    held_in = in;
    if (edge & IN_HELP) { lander.help = (lander.help + 1) % 3; return 1; }
    if (lander.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) lander.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (lander.state == LS_TITLE) return 0;
        to_title();
        return 1;
    }
    switch (lander.state) {
    case LS_TITLE:
        if (edge & IN_LEFT) lander.menu = (lander.menu + 3) % 4;
        if (edge & IN_RIGHT) lander.menu = (lander.menu + 1) % 4;
        if (edge & (IN_FIRE | IN_START)) start_game(lander.menu, 1);
        break;
    case LS_PLAY:
        if (edge & IN_PAUSE) lander.paused = !lander.paused;
        if (edge & IN_THR_UP) { lander.steady += FX(0.1); if (lander.steady > FONE) lander.steady = FONE; }
        if (edge & IN_THR_DN) { lander.steady -= FX(0.1); if (lander.steady < 0) lander.steady = 0; }
        break;
    case LS_RESULT:
        if (lander.state_timer < 75 && (edge & (IN_FIRE | IN_START))) after_result();
        break;
    case LS_OVER:
        if (lander.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return 1;
}

static void pilot_input(LdInput *in)
{
    in->throttle = (held_in & IN_FIRE) ? FONE : lander.steady;
    in->pitch = (held_in & IN_UP) ? FONE : (held_in & IN_DOWN) ? -FONE : 0;
    in->roll = (held_in & IN_LEFT) ? FONE : (held_in & IN_RIGHT) ? -FONE : 0;
}

void game_update(unsigned in)
{
    int32_t now = plat_ticks();
    uint32_t t0 = plat_clock();
    int steps = (int)((now - t_last) / 2), k;
    (void)in;
    if (steps > 0) t_last += steps * 2;
    if (steps > 4 || steps < 0) steps = 4;
    while (steps-- > 0) {
        int e;
        if (lander.help || lander.paused) continue;
        lander.frame++;
        if (lander.state_timer > 0) lander.state_timer--;
        if (lander.state == LS_RESULT && lander.state_timer == 0) after_result();
        if (lander.state != LS_PLAY && lander.state != LS_TITLE) continue;
        for (k = 0; k < LD_STEP_HZ / 25; k++) {
            LdInput li;
            if (lander.demo) ld_autopilot(&level, &li);
            else pilot_input(&li);
            ld_step(&level, &li);
        }
        while ((e = ld_pop_event(&level)) != LDE_NONE)
            if (e == LDE_FUEL_OUT && lander.state == LS_PLAY) { a3_sfx(SFX_FUEL); AB_W("FUEL OUT"); }
        if (lander.state == LS_PLAY && !level.outcome && level.c.throttle > FX(0.05) && (lander.frame % 6) == 0) a3_sfx(SFX_THRUST);
        if (lander.state == LS_PLAY && !level.outcome && (level.c.rcs[0] || level.c.rcs[1]) && (lander.frame % 5) == 0) a3_sfx(SFX_RCS);
        if (level.outcome) {
            if (lander.state == LS_PLAY) outcome();
            else if (lander.state == LS_TITLE && lander.frame % 75 == 0)      /* the demo goes round again */
                ld_new_level(&level, (lander.frame / 75) % W_COUNT, 2, a3_hash(next_seed++), 0), render_palette(level.world);
        }
    }
    /* the level's autopilot check, a slice a frame */
    if (probing && lander.state != LS_TITLE) {
        fix before = level.fuel_max;
#ifdef AMIGA3DO
        if (ld_probe_run(&level, 30)) {         /* the 3DO: smaller slices, or a frame takes a third of a second */
#else
        if (ld_probe_run(&level, 150)) {
#endif
            probing = 0;
            AB_I("PROBE %s, used %ld s of fuel%s", ld_outcome_names[level.probe_outcome], (long)FINT(level.probe_used),
                 level.fuel_max > before ? " - tank topped up" : "");
            if (level.fuel_max > before) { strcpy(lander.msg2, "TANK TOPPED UP"); lander.frame = 0; }
        }
    }
    us_update = plat_us_since(t0);
    if (lander.state != last_state) {
        static const char *const st[] = { "TITLE", "PLAY", "RESULT", "OVER" };
        AB_I("STATE %s score %ld ships %ld", st[lander.state], (long)lander.score, (long)lander.ships);
        last_state = lander.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (lander.help) render_help(cv, lander.help);
    else if (lander.state == LS_TITLE) render_title(cv, &level);
    else render_game(cv, &level);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    const Pad *p = &level.T.pads[ld_nearest_pad(&level, 0)];
    v_agl = FINT(ld_agl(&level));
    v_vs = (int32_t)((-level.c.v[1]) * 100 >> 16);         /* cm/s */
    v_hs = (int32_t)(fhypot(level.c.v[0], level.c.v[2]) * 100 >> 16);
    v_tilt = FINT(FMUL(ld_tilt(&level.c), FX(57.29578)));
    v_fuel = (int32_t)(FDIV(level.fuel, level.fuel_max ? level.fuel_max : 1) * 100 >> 16);
    v_thr = (int32_t)(level.c.throttle * 100 >> 16);
    v_x = FINT(level.c.p[0]); v_y = FINT(level.c.p[1]); v_z = FINT(level.c.p[2]);
    v_pad = FINT(fhypot(p->x - level.c.p[0], p->z - level.c.p[2]));
    v_world = level.world;
    v_outcome = level.outcome;
    v_probe = FINT(level.probe_used);
    v_polys = a3_npolys;
    frames++;
    if (now - t_fps >= 100) {
        v_fps = (frames * 50L) / (now - t_fps);
        frames = 0;
        t_fps = now;
    }
    ab_poll();
    if (selftest_pending) {
        int32_t t0 = plat_ticks();
        selftest_pending = 0;
        test_pass = test_fail = 0;
        ab_test_begin("lander_3d_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
