// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Ridge Runner's shell on the shared engine: input, the title
 * with the autopilot racing behind it, the countdown, the race, game
 * over, the help pages, sound for the events, and everything exposed over
 * the AmigaBridge so the game can be driven and tested from the host:
 *
 *   vars   score track place gate gates_hit missed jumps bumps state over
 *          won finished qualified autoplay x y z speed yaw air energy
 *          boosting offtrack time_s countdown start_track ground_win paused
 *          help fps polys us_update us_draw
 *   hooks  status, start [track], title, autoplay <on|off>, warp <z>,
 *          skip (to the finish), time <s>, seed <n>, help <0-2>,
 *          pause <on|off>, selftest, activate
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "rr.h"
#include "runner.h"

const char game_title[] = "Ridge Runner";
const char game_client[] = "ridge_runner";
const char game_music[] = "";
const char game_voice[] = "";

#define IN_LEFT   0x0001
#define IN_RIGHT  0x0002
#define IN_UP     0x0004
#define IN_DOWN   0x0008
#define IN_FIRE   0x0010
#define IN_PAUSE  0x0020
#define IN_ESC    0x0040
#define IN_START  0x0080
#define IN_HELP   0x0100
#define IN_AUTO   0x0200
#define IN_BOOST  0x0400

#define COUNTDOWN 75

enum { SFX_GATE, SFX_MISS, SFX_JUMP, SFX_LAND, SFX_BUMP, SFX_FINISH, SFX_FAIL, SFX_BEEP, SFX_GO, SFX_OVER, SFX_WIN };

Runner rn;
RRGame game;
static unsigned prev_in, play_in;
static int fire_lock;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1, last_track = 1;
static int32_t v_fps, v_polys, us_update, us_draw, v_x, v_z, v_speed, v_time, v_place, v_energy;
static int selftest_pending, test_pass, test_fail;

unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x4F: case 0x20: return IN_LEFT;      /* cursor left, A */
    case 0x4E: case 0x22: return IN_RIGHT;     /* cursor right, D */
    case 0x4C: case 0x11: return IN_UP;        /* cursor up, W */
    case 0x4D: case 0x21: return IN_DOWN;      /* cursor down, S */
    case 0x40: return IN_FIRE;                 /* space */
    case 0x60: case 0x61: return IN_BOOST;     /* shift */
    case 0x19: return IN_PAUSE;                /* P */
    case 0x35: return IN_AUTO;                 /* B */
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

static void flash(const char *s, int pen)
{
    size_t n = strlen(s);
    if (n > sizeof(rn.flash) - 1) n = sizeof(rn.flash) - 1;
    memcpy(rn.flash, s, n);
    rn.flash[n] = 0;
    rn.flash_timer = 30;
    rn.flash_pen = pen;
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_GATE, 1600, 50, 30))) { a3_tone(d, 800, 880, 880, 90, 100); a3_tone(d + 800, 800, 1318, 1318, 90, 600); }
    if ((d = a3_sfx_define(SFX_MISS, 2400, 54, 30))) a3_tone(d, 2400, 300, 180, 100, 1200);
    if ((d = a3_sfx_define(SFX_JUMP, 1200, 40, 15))) a3_tone(d, 1200, 400, 900, 80, 800);
    if ((d = a3_sfx_define(SFX_LAND, 1400, 54, 25))) a3_noise(d, 1400, 120, 5, 2);
    if ((d = a3_sfx_define(SFX_BUMP, 700, 44, 20))) { a3_noise(d, 400, 110, 2, 2); a3_tone(d + 400, 300, 200, 150, 90, 250); }
    if ((d = a3_sfx_define(SFX_FINISH, 4800, 60, 55))) {
        a3_tone(d, 1200, 523, 523, 100, 80);
        a3_tone(d + 1200, 1200, 659, 659, 100, 80);
        a3_tone(d + 2400, 1200, 784, 784, 100, 80);
        a3_tone(d + 3600, 1200, 1046, 1046, 110, 1000);
    }
    if ((d = a3_sfx_define(SFX_FAIL, 4000, 60, 55))) a3_tone(d, 4000, 392, 196, 100, 2500);
    if ((d = a3_sfx_define(SFX_BEEP, 900, 50, 40))) a3_tone(d, 900, 660, 660, 90, 300);
    if ((d = a3_sfx_define(SFX_GO, 2000, 56, 45))) a3_tone(d, 2000, 1320, 1320, 100, 1200);
    if ((d = a3_sfx_define(SFX_OVER, 6000, 60, 60))) a3_tone(d, 6000, 440, 110, 110, 3000);
    if ((d = a3_sfx_define(SFX_WIN, 6000, 60, 60))) {
        a3_tone(d, 1500, 784, 784, 100, 80);
        a3_tone(d + 1500, 1500, 1046, 1046, 100, 80);
        a3_tone(d + 3000, 3000, 1568, 1568, 110, 2000);
    }
}

static void handle_events(void)
{
    RREvent e;
    int quiet = rn.state == RS_TITLE;
    char tmp[32];
    while (rr_pop_event(&game, &e)) {
        if (quiet) continue;
        switch (e.type) {
        case RRE_GATE: a3_sfx(SFX_GATE); sprintf(tmp, "GATE +%ld S", (long)(9 - game.track / 2)); flash(tmp, 28); break;
        case RRE_MISS: a3_sfx(SFX_MISS); flash("MISSED THE GATE -3 S", 26); break;
        case RRE_JUMP: a3_sfx(SFX_LAND); if (e.a > 16) flash("BIG AIR!", 29); break;
        case RRE_LAND: a3_sfx(SFX_LAND); break;
        case RRE_BUMP: a3_sfx(SFX_BUMP); break;
        case RRE_FINISH: break;
        case RRE_QUALIFY: a3_sfx(SFX_FINISH); break;
        case RRE_FAIL: a3_sfx(SFX_FAIL); break;
        case RRE_TIMEOUT: flash("OUT OF TIME", 26); break;
        case RRE_OVER: a3_sfx(SFX_OVER); break;
        case RRE_WIN: a3_sfx(SFX_WIN); break;
        }
        if (e.type != RRE_BUMP && e.type != RRE_LAND && e.type != RRE_JUMP)
            AB_I("EV %s a=%ld b=%ld score=%ld track=%ld place=%ld", rr_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.track, (long)rr_place_now(&game));
    }
}

/* ---------- game flow ---------- */
static void start_game(int track)
{
    rr_new_game(&game, next_seed++, track);
    rn.state = RS_PLAY;
    rn.paused = 0;
    rn.help = 0;
    rn.autoplay = 0;
    rn.cam_yaw = game.yaw;
    rn.countdown = COUNTDOWN;
    render_theme(track);
    last_track = track;                 /* (a new track mid-game restarts the countdown; this isn't one) */
    fire_lock = 1;
    AB_I("START track %ld seed %ld", (long)track, (long)game.seed);
}

static void to_title(void)
{
    rn.state = RS_TITLE;
    rn.countdown = 0;
    rr_new_game(&game, 777 + next_seed, 1);
    rn.cam_yaw = game.yaw;
    render_theme(1);
    last_track = 1;
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[220];
    static const char *const st[] = { "title", "race", "over" };
    (void)args;
    sprintf(tmp, "%s track %ld %s | place %ld gate %ld/%ld missed %ld | z %ld x %ld (centre %ld) speed %ld | "
            "energy %ld %s%s%s| time %ld s score %ld %s%s| fps %ld",
            st[rn.state], (long)game.track, rr_track_names[game.track - 1], (long)rr_place_now(&game), (long)game.gate,
            (long)RR_GATES, (long)game.missed, (long)(game.z >> 16), (long)(game.x >> 16),
            (long)rr_centre(&game, game.z >> 16), (long)(game.v >> 16), (long)(game.energy >> 16),
            game.boosting ? "boost " : "", game.air ? "air " : "", game.offtrack ? "offtrack " : "",
            (long)(game.time_left / 25), (long)game.score, game.over ? "over " : game.won ? "won " : "",
            game.finished ? "finished " : "", (long)v_fps);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int track = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (track < 1) track = 1;
    if (track > RR_TRACKS) track = RR_TRACKS;
    start_game(track);
    if (args && strchr(args, 'n')) rn.countdown = 0;      /* "start 3 now": no countdown */
    sprintf(tmp, "track %ld %s, seed %ld", (long)track, rr_track_names[track - 1], (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    rn.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, rn.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

/* the bike to z on the course's middle, facing along it (the gates
 * before it counted as passed) */
static int hook_warp(const char *args, char *buf, int bufsz)
{
    int32_t z = args && args[0] ? atol(args) : 0;
    char tmp[48];
    if (z < 0) z = 0;
    if (z > RR_LEN - 10) z = RR_LEN - 10;
    game.z = z * FONE;
    game.x = rr_centre(&game, z) * FONE;
    game.y = (rr_height(&game, game.x >> 16, z) + RR_HOVER) * FONE;
    game.vy = 0;
    game.air = 0;
    game.yaw = a3_atan2(rr_centre(&game, z + 60) - rr_centre(&game, z), 60);
    rn.cam_yaw = game.yaw;
    while (game.gate < RR_GATES && (game.gate + 1) * RR_GATE_GAP <= z) game.gate++;
    sprintf(tmp, "at z %ld, next gate %ld", (long)z, (long)game.gate);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_skip(const char *args, char *buf, int bufsz)
{
    char tmp[40];
    sprintf(tmp, "%ld", (long)(RR_LEN - 20));
    (void)args;
    return hook_warp(tmp, buf, bufsz);
}

static int hook_time(const char *args, char *buf, int bufsz)
{
    game.time_left = (args && args[0] ? atoi(args) : 60) * 25;
    reply(buf, bufsz, "time set");
    return 0;
}

static int hook_seed(const char *args, char *buf, int bufsz)
{
    next_seed = args && args[0] ? (uint32_t)atol(args) : 1;
    reply(buf, bufsz, "the next race uses that seed");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    rn.help = args && args[0] ? atoi(args) : 1;
    if (rn.help < 0 || rn.help > 2) rn.help = 0;
    reply(buf, bufsz, rn.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    rn.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, rn.paused ? "paused" : "running");
    return 0;
}

static int hook_activate(const char *args, char *buf, int bufsz)
{
    (void)args;
    plat_activate();
    reply(buf, bufsz, "window active");
    return 0;
}

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
    ab_register_var("score",       AB_TYPE_I32, &game.score);
    ab_register_var("track",       AB_TYPE_I32, &game.track);
    ab_register_var("place",       AB_TYPE_I32, &v_place);
    ab_register_var("gate",        AB_TYPE_I32, &game.gate);
    ab_register_var("gates_hit",   AB_TYPE_I32, &game.gates_hit);
    ab_register_var("missed",      AB_TYPE_I32, &game.missed);
    ab_register_var("jumps",       AB_TYPE_I32, &game.jumps);
    ab_register_var("state",       AB_TYPE_I32, &rn.state);
    ab_register_var("over",        AB_TYPE_I32, &game.over);
    ab_register_var("won",         AB_TYPE_I32, &game.won);
    ab_register_var("finished",    AB_TYPE_I32, &game.finished);
    ab_register_var("qualified",   AB_TYPE_I32, &game.qualified);
    ab_register_var("autoplay",    AB_TYPE_I32, &rn.autoplay);
    ab_register_var("x",           AB_TYPE_I32, &v_x);
    ab_register_var("z",           AB_TYPE_I32, &v_z);
    ab_register_var("speed",       AB_TYPE_I32, &v_speed);
    ab_register_var("energy",      AB_TYPE_I32, &v_energy);
    ab_register_var("boosting",    AB_TYPE_I32, &game.boosting);
    ab_register_var("offtrack",    AB_TYPE_I32, &game.offtrack);
    ab_register_var("time_s",      AB_TYPE_I32, &v_time);
    ab_register_var("countdown",   AB_TYPE_I32, &rn.countdown);
    ab_register_var("start_track", AB_TYPE_I32, &rn.start_track);
    ab_register_var("ground_win",  AB_TYPE_I32, &ground_win);
    ab_register_var("paused",      AB_TYPE_I32, &rn.paused);
    ab_register_var("help",        AB_TYPE_I32, &rn.help);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",   AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line race status", hook_status);
    ab_register_hook("start",    "start [track 1-6] [now]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot races", hook_autoplay);
    ab_register_hook("warp",     "warp Z: the bike to z on the course (0-16000)", hook_warp);
    ab_register_hook("skip",     "The bike to just before the finish", hook_skip);
    ab_register_hook("time",     "time S: seconds left", hook_time);
    ab_register_hook("seed",     "seed N: the next race's seed", hook_seed);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    render_init();
    rn.start_track = 1;
    register_bridge();
    to_title();
    render_theme(1);
    last_track = 1;
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { rn.help = (rn.help + 1) % 3; return 1; }
    if (rn.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) rn.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (rn.state == RS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (rn.state) {
    case RS_TITLE:
        if ((edge & (IN_UP | IN_RIGHT)) && rn.start_track < RR_TRACKS) rn.start_track++;
        if ((edge & (IN_DOWN | IN_LEFT)) && rn.start_track > 1) rn.start_track--;
        if (edge & (IN_FIRE | IN_START)) start_game(rn.start_track);
        break;
    case RS_PLAY:
        if (edge & IN_PAUSE) rn.paused = !rn.paused;
        if (edge & IN_AUTO) rn.autoplay = !rn.autoplay;
        play_in = (in & IN_LEFT ? RR_IN_LEFT : 0) | (in & IN_RIGHT ? RR_IN_RIGHT : 0) | (in & IN_UP ? RR_IN_UP : 0) |
                  (in & IN_DOWN ? RR_IN_DOWN : 0) | (((in & IN_FIRE) && !fire_lock) || (in & IN_BOOST) ? RR_IN_FIRE : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case RS_OVER:
        if (rn.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return running;
}

void game_update(unsigned in)
{
    int32_t now = plat_ticks();
    uint32_t t0 = plat_clock();
    int steps = (int)((now - t_last) / 2);
    (void)in;
    if (steps > 0) t_last += steps * 2;
    if (steps > 4 || steps < 0) steps = 4;
    while (steps-- > 0) {
        if (rn.help || rn.paused) continue;
        if (rn.state == RS_TITLE) {
            if (game.over || game.won || game.finished) rr_new_game(&game, a3_hash(game.seed), 1 + (int)(game.seed % RR_TRACKS));
            rr_tick(&game, rr_bot(&game));
        } else if (rn.state == RS_PLAY) {
            if (rn.countdown > 0) {
                if (rn.countdown % 25 == 0) a3_sfx(SFX_BEEP);
                if (--rn.countdown == 0) a3_sfx(SFX_GO);
            } else rr_tick(&game, rn.autoplay ? rr_bot(&game) : play_in);
        }
        handle_events();
        /* the camera's heading eases after the bike's */
        rn.cam_yaw = (rn.cam_yaw + (int)(int16_t)(game.yaw - rn.cam_yaw) / 4) & 0xFFFF;
        if (game.track != last_track) {
            render_theme(game.track);
            last_track = game.track;
            rn.cam_yaw = game.yaw;
            if (rn.state == RS_PLAY) rn.countdown = COUNTDOWN;
        }
        if (rn.flash_timer > 0) rn.flash_timer--;
        if (rn.state_timer > 0) rn.state_timer--;
        if (rn.state == RS_PLAY && (game.over || game.won)) {
            rn.state = RS_OVER;
            rn.state_timer = 40;
            if (game.score > rn.best) rn.best = game.score;
        }
    }
    us_update = plat_us_since(t0);
    if (rn.state != last_state) {
        static const char *const st[] = { "TITLE", "RACE", "OVER" };
        AB_I("STATE %s score %ld", st[rn.state], (long)game.score);
        last_state = rn.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (rn.help) render_help(cv, rn.help);
    else if (rn.state == RS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    v_polys = a3_npolys;
    v_x = game.x >> 16;
    v_z = game.z >> 16;
    v_speed = (int32_t)((game.v * 10) >> 16);
    v_energy = game.energy >> 16;
    v_time = game.time_left / 25;
    v_place = rr_place_now(&game);
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
        ab_test_begin("ridge_runner_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
