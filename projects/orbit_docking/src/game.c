// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Orbit Docking's shell on the shared engine: input (the
 * keyboard's separate keys, or the joystick with fire as a shift), the
 * title with the autopilot docking behind it, play, game over, the help
 * pages, sound for the events, and everything exposed over the
 * AmigaBridge so the game can be driven and tested from the host:
 *
 *   vars   score level lives fuel range close x y vx vy roll_err spin_err
 *          docks crashes fail state over won autoplay start_level paused
 *          help fps polys us_update us_draw (28: the bridge takes 32 a client)
 *   hooks  status, start [level], title, autoplay <on|off>, lineup [dist]
 *          (lined up and matched, dist out), fuel <n>, lives <n>, help <0-2>,
 *          pause <on|off>, selftest, activate
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "od.h"
#include "dock.h"

const char game_title[] = "Orbit Docking";
const char game_client[] = "orbit_docking";
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
#define IN_FWD    0x0400
#define IN_BACK   0x0800
#define IN_ROLL_L 0x1000
#define IN_ROLL_R 0x2000

enum { SFX_THRUST, SFX_DOCK, SFX_CRASH, SFX_FUEL, SFX_OVER, SFX_WIN };

Dock dk;
ODGame game;
static unsigned prev_in, play_in;
static int fire_lock;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw, v_fuel, v_range, v_close, v_x, v_y, v_vx, v_vy, v_roll, v_spin;
static int selftest_pending, test_pass, test_fail, thrust_timer;

unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x4F: return IN_LEFT;                 /* cursor keys: across */
    case 0x4E: return IN_RIGHT;
    case 0x4C: return IN_UP;
    case 0x4D: return IN_DOWN;
    case 0x11: return IN_FWD;                  /* W, S: in and out */
    case 0x21: return IN_BACK;
    case 0x20: return IN_ROLL_L;               /* A, D: roll */
    case 0x22: return IN_ROLL_R;
    case 0x40: return IN_FIRE;                 /* space */
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
    if (n > sizeof(dk.flash) - 1) n = sizeof(dk.flash) - 1;
    memcpy(dk.flash, s, n);
    dk.flash[n] = 0;
    dk.flash_timer = 40;
    dk.flash_pen = pen;
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_THRUST, 600, 20, 5))) a3_noise(d, 600, 50, 20, 100);
    if ((d = a3_sfx_define(SFX_DOCK, 4800, 60, 55))) {
        a3_noise(d, 800, 110, 3, 2);
        a3_tone(d + 800, 1300, 523, 523, 100, 80);
        a3_tone(d + 2100, 1300, 659, 659, 100, 80);
        a3_tone(d + 3400, 1400, 784, 784, 110, 1000);
    }
    if ((d = a3_sfx_define(SFX_CRASH, 5000, 64, 60))) { a3_noise(d, 3000, 127, 6, 2); a3_tone(d + 3000, 2000, 200, 80, 100, 1800); }
    if ((d = a3_sfx_define(SFX_FUEL, 2400, 50, 40))) { a3_tone(d, 800, 880, 880, 90, 100); a3_tone(d + 1600, 800, 880, 880, 90, 600); }
    if ((d = a3_sfx_define(SFX_OVER, 6000, 60, 60))) a3_tone(d, 6000, 440, 110, 110, 3000);
    if ((d = a3_sfx_define(SFX_WIN, 6000, 60, 60))) {
        a3_tone(d, 1500, 784, 784, 100, 80);
        a3_tone(d + 1500, 1500, 1046, 1046, 100, 80);
        a3_tone(d + 3000, 3000, 1568, 1568, 110, 2000);
    }
}

static void handle_events(void)
{
    ODEvent e;
    int quiet = dk.state == DS_TITLE;
    char tmp[40];
    while (od_pop_event(&game, &e)) {
        if (quiet) continue;
        switch (e.type) {
        case ODE_DOCK: a3_sfx(SFX_DOCK); break;
        case ODE_CRASH: case ODE_DEBRIS:
            a3_sfx(SFX_CRASH);
            sprintf(tmp, "CRASH: %s", od_fail_names[e.a]);
            flash(tmp, 26);
            break;
        case ODE_FUEL_OUT: a3_sfx(SFX_FUEL); flash("OUT OF FUEL", 26); break;
        case ODE_OVER: a3_sfx(SFX_OVER); break;
        case ODE_WIN: a3_sfx(SFX_WIN); break;
        }
        AB_I("EV %s a=%ld b=%ld score=%ld level=%ld lives=%ld", od_event_names[e.type], (long)e.a, (long)e.b,
             (long)game.score, (long)game.level, (long)game.lives);
    }
}

/* ---------- game flow ---------- */
static void start_game(int level)
{
    od_new_game(&game, next_seed++, level);
    dk.state = DS_PLAY;
    dk.paused = 0;
    dk.help = 0;
    dk.autoplay = 0;
    fire_lock = 1;
    AB_I("START level %ld seed %ld", (long)level, (long)game.seed);
}

static void to_title(void)
{
    dk.state = DS_TITLE;
    od_new_game(&game, 777 + next_seed, 1);
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[120];
    static const char *const st[] = { "title", "fly", "over" };
    (void)args;
    sprintf(tmp, "%s S%ld lives %ld | range %ld close %ld at %ld,%ld | roll %ld spin %ld | fuel %ld docks %ld%s%s",
            st[dk.state], (long)game.level, (long)game.lives, (long)v_range, (long)v_close, (long)v_x, (long)v_y,
            (long)v_roll, (long)v_spin, (long)v_fuel, (long)game.docks, game.over ? " over" : "", game.won ? " won" : "");
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int level = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (level < 1) level = 1;
    if (level > OD_LEVELS) level = OD_LEVELS;
    start_game(level);
    sprintf(tmp, "station %ld, seed %ld", (long)level, (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    dk.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, dk.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

/* on the axis, rolled and turning with the slot, still, dist out */
static int hook_lineup(const char *args, char *buf, int bufsz)
{
    int32_t d = args && args[0] ? atol(args) : 100;
    game.x = game.y = 0;
    game.vx = game.vy = game.vz = 0;
    game.z = FROMI(OD_PORT_Z + OD_NOSE + d);
    game.roll = game.st_angle;
    game.roll_rate = game.st_spin;
    reply(buf, bufsz, "lined up");
    return 0;
}

static int hook_fuel(const char *args, char *buf, int bufsz)
{
    game.fuel = FROMI(args && args[0] ? atoi(args) : 500);
    reply(buf, bufsz, "fuel set");
    return 0;
}

static int hook_lives(const char *args, char *buf, int bufsz)
{
    game.lives = args && args[0] ? atoi(args) : OD_LIVES;
    reply(buf, bufsz, "lives set");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    dk.help = args && args[0] ? atoi(args) : 1;
    if (dk.help < 0 || dk.help > 2) dk.help = 0;
    reply(buf, bufsz, dk.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    dk.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, dk.paused ? "paused" : "running");
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
    ab_register_var("level",       AB_TYPE_I32, &game.level);
    ab_register_var("lives",       AB_TYPE_I32, &game.lives);
    ab_register_var("fuel",        AB_TYPE_I32, &v_fuel);
    ab_register_var("range",       AB_TYPE_I32, &v_range);
    ab_register_var("close",       AB_TYPE_I32, &v_close);
    ab_register_var("x",           AB_TYPE_I32, &v_x);
    ab_register_var("y",           AB_TYPE_I32, &v_y);
    ab_register_var("vx",          AB_TYPE_I32, &v_vx);
    ab_register_var("vy",          AB_TYPE_I32, &v_vy);
    ab_register_var("roll_err",    AB_TYPE_I32, &v_roll);
    ab_register_var("spin_err",    AB_TYPE_I32, &v_spin);
    ab_register_var("docks",       AB_TYPE_I32, &game.docks);
    ab_register_var("crashes",     AB_TYPE_I32, &game.crashes);
    ab_register_var("fail",        AB_TYPE_I32, &game.last_fail);
    ab_register_var("state",       AB_TYPE_I32, &dk.state);
    ab_register_var("over",        AB_TYPE_I32, &game.over);
    ab_register_var("won",         AB_TYPE_I32, &game.won);
    ab_register_var("autoplay",    AB_TYPE_I32, &dk.autoplay);
    ab_register_var("start_level", AB_TYPE_I32, &dk.start_level);
    ab_register_var("paused",      AB_TYPE_I32, &dk.paused);
    ab_register_var("help",        AB_TYPE_I32, &dk.help);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",   AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line flight status", hook_status);
    ab_register_hook("start",    "start [station 1-6]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot docks", hook_autoplay);
    ab_register_hook("lineup",   "lineup [DIST]: on the axis, matched to the slot, still, DIST out (100)", hook_lineup);
    ab_register_hook("fuel",     "fuel N", hook_fuel);
    ab_register_hook("lives",    "lives N", hook_lives);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(dock_palette);
    plat_sky(0, 0, 0, 0);
    dk.start_level = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { dk.help = (dk.help + 1) % 3; return 1; }
    if (dk.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) dk.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (dk.state == DS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (dk.state) {
    case DS_TITLE:
        if ((edge & IN_UP) && dk.start_level < OD_LEVELS) dk.start_level++;
        if ((edge & IN_DOWN) && dk.start_level > 1) dk.start_level--;
        if (edge & (IN_FIRE | IN_START)) start_game(dk.start_level);
        break;
    case DS_PLAY:
        if (edge & IN_PAUSE) dk.paused = !dk.paused;
        if (edge & IN_AUTO) dk.autoplay = !dk.autoplay;
        if (!(in & IN_FIRE)) fire_lock = 0;
        if ((in & IN_FIRE) && !fire_lock)       /* the joystick: fire shifts up/down to in/out, left/right to roll */
            play_in = (in & IN_UP ? OD_IN_FWD : 0) | (in & IN_DOWN ? OD_IN_BACK : 0) |
                      (in & IN_LEFT ? OD_IN_ROLL_L : 0) | (in & IN_RIGHT ? OD_IN_ROLL_R : 0);
        else
            play_in = (in & IN_LEFT ? OD_IN_LEFT : 0) | (in & IN_RIGHT ? OD_IN_RIGHT : 0) |
                      (in & IN_UP ? OD_IN_UP : 0) | (in & IN_DOWN ? OD_IN_DOWN : 0);
        play_in |= (in & IN_FWD ? OD_IN_FWD : 0) | (in & IN_BACK ? OD_IN_BACK : 0) |
                   (in & IN_ROLL_L ? OD_IN_ROLL_L : 0) | (in & IN_ROLL_R ? OD_IN_ROLL_R : 0);
        break;
    case DS_OVER:
        if (dk.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
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
        unsigned bits = 0;
        if (dk.help || dk.paused) continue;
        if (dk.state == DS_TITLE) {
            if (game.over || game.won || game.between == 1) od_new_game(&game, game.seed + 1, 1 + (int)(game.seed % 3));
            od_tick(&game, od_bot(&game));
        } else if (dk.state == DS_PLAY) {
            bits = dk.autoplay ? od_bot(&game) : play_in;
            od_tick(&game, bits);
        }
        if (bits && game.fuel > 0 && --thrust_timer <= 0) { a3_sfx(SFX_THRUST); thrust_timer = 6; }
        handle_events();
        if (dk.flash_timer > 0) dk.flash_timer--;
        if (dk.state_timer > 0) dk.state_timer--;
        if (dk.state == DS_PLAY && (game.over || game.won)) {
            dk.state = DS_OVER;
            dk.state_timer = 40;
            if (game.score > dk.best) dk.best = game.score;
        }
    }
    us_update = plat_us_since(t0);
    if (dk.state != last_state) {
        static const char *const st[] = { "TITLE", "FLY", "OVER" };
        AB_I("STATE %s score %ld", st[dk.state], (long)game.score);
        last_state = dk.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (dk.help) render_help(cv, dk.help);
    else if (dk.state == DS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    v_polys = a3_npolys;
    v_fuel = game.fuel >> 16;
    v_range = (game.z >> 16) - (OD_PORT_Z + OD_NOSE);
    v_close = (int32_t)((-game.vz * 100) >> 16);
    v_x = game.x >> 16;
    v_y = game.y >> 16;
    v_vx = (int32_t)((game.vx * 100) >> 16);
    v_vy = (int32_t)((game.vy * 100) >> 16);
    v_roll = (int32_t)od_roll_error(&game) * 360 / 65536;
    v_spin = ((game.roll_rate - game.st_spin) >> 8) * 25 * 360 / 65536;
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
        ab_test_begin("orbit_docking_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
