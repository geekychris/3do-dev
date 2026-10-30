// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Drone Courier's shell on the shared engine: input, the title
 * with the autopilot working behind it, play, game over, the help pages,
 * sound for the events, and everything exposed over the AmigaBridge so
 * the game can be driven and tested from the host:
 *
 *   vars   score day jobs jobs_done fails battery job_s carrying fragile
 *          target state over won autoplay x y z speed landed bumps
 *          start_day view_range towers paused help fps polys us_update
 *          us_draw (30: the bridge takes 32 a client)
 *   hooks  status, start [day], title, autoplay <on|off>, hop [above]
 *          (over the next pad), job <s>, battery <n>, help <0-2>,
 *          pause <on|off>, selftest, activate
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "dc.h"
#include "courier.h"

const char game_title[] = "Drone Courier";
const char game_client[] = "drone_courier";
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

enum { SFX_PICKUP, SFX_DELIVER, SFX_BUMP, SFX_BROKEN, SFX_LATE, SFX_GUST, SFX_DAY, SFX_OVER, SFX_WIN, SFX_HUM };

Courier co;
DCGame game;
static unsigned prev_in, play_in;
static int fire_lock;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1, hum_timer;
static int32_t v_fps, v_polys, us_update, us_draw, v_battery, v_job, v_x, v_y, v_z, v_speed;
static int selftest_pending, test_pass, test_fail;

unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x4F: case 0x20: return IN_LEFT;      /* cursor left, A */
    case 0x4E: case 0x22: return IN_RIGHT;     /* cursor right, D */
    case 0x4C: case 0x11: return IN_UP;        /* cursor up, W */
    case 0x4D: case 0x21: return IN_DOWN;      /* cursor down, S */
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
    if (n > sizeof(co.flash) - 1) n = sizeof(co.flash) - 1;
    memcpy(co.flash, s, n);
    co.flash[n] = 0;
    co.flash_timer = 40;
    co.flash_pen = pen;
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_PICKUP, 1600, 50, 30))) { a3_tone(d, 800, 660, 660, 90, 100); a3_tone(d + 800, 800, 990, 990, 90, 600); }
    if ((d = a3_sfx_define(SFX_DELIVER, 4800, 60, 50))) {
        a3_tone(d, 1200, 523, 523, 100, 80);
        a3_tone(d + 1200, 1200, 659, 659, 100, 80);
        a3_tone(d + 2400, 1200, 784, 784, 100, 80);
        a3_tone(d + 3600, 1200, 1046, 1046, 110, 1000);
    }
    if ((d = a3_sfx_define(SFX_BUMP, 900, 54, 30))) a3_noise(d, 900, 120, 3, 2);
    if ((d = a3_sfx_define(SFX_BROKEN, 3000, 60, 50))) { a3_noise(d, 1500, 127, 1, 2); a3_tone(d + 1500, 1500, 300, 120, 100, 1200); }
    if ((d = a3_sfx_define(SFX_LATE, 3000, 56, 45))) a3_tone(d, 3000, 400, 200, 100, 1500);
    if ((d = a3_sfx_define(SFX_GUST, 2400, 30, 8))) a3_noise(d, 2400, 60, 30, 1200);
    if ((d = a3_sfx_define(SFX_DAY, 4800, 60, 55))) {
        a3_tone(d, 1600, 784, 784, 100, 80);
        a3_tone(d + 1600, 1600, 988, 988, 100, 80);
        a3_tone(d + 3200, 1600, 1175, 1175, 110, 1000);
    }
    if ((d = a3_sfx_define(SFX_OVER, 6000, 60, 60))) a3_tone(d, 6000, 440, 110, 110, 3000);
    if ((d = a3_sfx_define(SFX_WIN, 6000, 60, 60))) {
        a3_tone(d, 1500, 784, 784, 100, 80);
        a3_tone(d + 1500, 1500, 1046, 1046, 100, 80);
        a3_tone(d + 3000, 3000, 1568, 1568, 110, 2000);
    }
    if ((d = a3_sfx_define(SFX_HUM, 800, 14, 2))) a3_tone(d, 800, 180, 190, 60, 100);
}

static void handle_events(void)
{
    DCEvent e;
    int quiet = co.state == CS_TITLE;
    while (dc_pop_event(&game, &e)) {
        if (quiet) continue;
        switch (e.type) {
        case DCE_PICKUP: a3_sfx(SFX_PICKUP); flash(e.a ? "FRAGILE PARCEL - GENTLY!" : "PARCEL ABOARD", e.a ? 26 : 29); break;
        case DCE_DELIVER: a3_sfx(SFX_DELIVER); flash("DELIVERED", 28); break;
        case DCE_BUMP: a3_sfx(SFX_BUMP); break;
        case DCE_HARD: a3_sfx(SFX_BUMP); flash("TOO HARD - TRY AGAIN", 27); break;
        case DCE_BROKEN: a3_sfx(SFX_BROKEN); flash("PARCEL BROKEN", 26); break;
        case DCE_LATE: a3_sfx(SFX_LATE); flash("TOO LATE", 26); break;
        case DCE_GUST: a3_sfx(SFX_GUST); break;
        case DCE_FLAT: flash("BATTERY FLAT", 26); break;
        case DCE_DAY: a3_sfx(SFX_DAY); break;
        case DCE_OVER: a3_sfx(SFX_OVER); break;
        case DCE_WIN: a3_sfx(SFX_WIN); break;
        }
        if (e.type != DCE_BUMP && e.type != DCE_GUST)
            AB_I("EV %s a=%ld b=%ld score=%ld day=%ld done=%ld", dc_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.day, (long)game.jobs_done);
    }
}

/* ---------- game flow ---------- */
static void start_game(int day)
{
    dc_new_game(&game, next_seed++, day);
    co.state = CS_PLAY;
    co.paused = 0;
    co.help = 0;
    co.autoplay = 0;
    co.cam_yaw = game.yaw;
    fire_lock = 1;
    AB_I("START day %ld seed %ld", (long)day, (long)game.seed);
}

static void to_title(void)
{
    co.state = CS_TITLE;
    dc_new_game(&game, 777 + next_seed, 1);
    co.cam_yaw = game.yaw;
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
    int b = dc_goal(&game);
    (void)args;
    sprintf(tmp, "%s D%ld %ld/%ld fails %ld | at %ld,%ld,%ld %s | to %s %ld,%ld | batt %ld t %ld%s%s",
            st[co.state], (long)game.day, (long)game.jobs_done, (long)game.jobs, (long)game.fails, (long)v_x, (long)v_y,
            (long)v_z, game.landed ? "landed" : "flying", game.carrying ? "cust" : "depot", (long)(b / DC_N),
            (long)(b % DC_N), (long)v_battery, (long)v_job, game.over ? " over" : "", game.won ? " won" : "");
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int day = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (day < 1) day = 1;
    if (day > DC_DAYS) day = DC_DAYS;
    start_game(day);
    sprintf(tmp, "day %ld: %ld jobs, seed %ld", (long)day, (long)game.jobs, (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    co.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, co.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

/* hovering, still, `above` over the middle of the next pad */
static int hook_hop(const char *args, char *buf, int bufsz)
{
    int b = dc_goal(&game), above = args && args[0] ? atoi(args) : 20;
    game.x = dc_block_x(b % DC_N);
    game.z = dc_block_z(b / DC_N);
    game.y = FROMI(dc_roof(&game, b) + above);
    game.vx = game.vy = game.vz = 0;
    game.landed = 0;
    reply(buf, bufsz, game.carrying ? "over the customer's pad" : "over the depot");
    return 0;
}

static int hook_job(const char *args, char *buf, int bufsz)
{
    game.job_time = (args && args[0] ? atoi(args) : 60) * 25;
    reply(buf, bufsz, "clock set");
    return 0;
}

static int hook_battery(const char *args, char *buf, int bufsz)
{
    game.battery = FROMI(args && args[0] ? atoi(args) : 100);
    reply(buf, bufsz, "battery set");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    co.help = args && args[0] ? atoi(args) : 1;
    if (co.help < 0 || co.help > 2) co.help = 0;
    reply(buf, bufsz, co.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    co.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, co.paused ? "paused" : "running");
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
    ab_register_var("score",      AB_TYPE_I32, &game.score);
    ab_register_var("day",        AB_TYPE_I32, &game.day);
    ab_register_var("jobs",       AB_TYPE_I32, &game.jobs);
    ab_register_var("jobs_done",  AB_TYPE_I32, &game.jobs_done);
    ab_register_var("fails",      AB_TYPE_I32, &game.fails);
    ab_register_var("battery",    AB_TYPE_I32, &v_battery);
    ab_register_var("job_s",      AB_TYPE_I32, &v_job);
    ab_register_var("carrying",   AB_TYPE_I32, &game.carrying);
    ab_register_var("fragile",    AB_TYPE_I32, &game.fragile);
    ab_register_var("target",     AB_TYPE_I32, &game.target);
    ab_register_var("state",      AB_TYPE_I32, &co.state);
    ab_register_var("over",       AB_TYPE_I32, &game.over);
    ab_register_var("won",        AB_TYPE_I32, &game.won);
    ab_register_var("autoplay",   AB_TYPE_I32, &co.autoplay);
    ab_register_var("x",          AB_TYPE_I32, &v_x);
    ab_register_var("y",          AB_TYPE_I32, &v_y);
    ab_register_var("z",          AB_TYPE_I32, &v_z);
    ab_register_var("speed",      AB_TYPE_I32, &v_speed);
    ab_register_var("landed",     AB_TYPE_I32, &game.landed);
    ab_register_var("bumps",      AB_TYPE_I32, &game.bumps);
    ab_register_var("start_day",  AB_TYPE_I32, &co.start_day);
    ab_register_var("view_range", AB_TYPE_I32, &view_range);
    ab_register_var("towers",     AB_TYPE_I32, &towers_drawn);
    ab_register_var("paused",     AB_TYPE_I32, &co.paused);
    ab_register_var("help",       AB_TYPE_I32, &co.help);
    ab_register_var("fps",        AB_TYPE_I32, &v_fps);
    ab_register_var("polys",      AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",  AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",    AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line status", hook_status);
    ab_register_hook("start",    "start [day 1-5]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot flies", hook_autoplay);
    ab_register_hook("hop",      "hop [ABOVE]: hovering, still, ABOVE (20) over the next pad", hook_hop);
    ab_register_hook("job",      "job S: seconds left on this job", hook_job);
    ab_register_hook("battery",  "battery N: 0-100", hook_battery);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(courier_palette);
    plat_sky(4, 0x48C, 0xCDE, 120);
    co.start_day = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { co.help = (co.help + 1) % 3; return 1; }
    if (co.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) co.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (co.state == CS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (co.state) {
    case CS_TITLE:
        if ((edge & IN_UP) && co.start_day < DC_DAYS) co.start_day++;
        if ((edge & IN_DOWN) && co.start_day > 1) co.start_day--;
        if (edge & (IN_FIRE | IN_START)) start_game(co.start_day);
        break;
    case CS_PLAY:
        if (edge & IN_PAUSE) co.paused = !co.paused;
        if (edge & IN_AUTO) co.autoplay = !co.autoplay;
        play_in = (in & IN_LEFT ? DC_IN_LEFT : 0) | (in & IN_RIGHT ? DC_IN_RIGHT : 0) | (in & IN_UP ? DC_IN_UP : 0) |
                  (in & IN_DOWN ? DC_IN_DOWN : 0) | ((in & IN_FIRE) && !fire_lock ? DC_IN_CLIMB : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case CS_OVER:
        if (co.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
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
        if (co.help || co.paused) continue;
        if (co.state == CS_TITLE) {
            if (game.over || game.won || game.between == 1) dc_new_game(&game, game.seed + 1, 1);
            dc_tick(&game, dc_bot(&game));
        } else if (co.state == CS_PLAY) {
            dc_tick(&game, co.autoplay ? dc_bot(&game) : play_in);
            if (!game.landed && --hum_timer <= 0) { a3_sfx(SFX_HUM); hum_timer = 8; }
        }
        handle_events();
        co.cam_yaw = (co.cam_yaw + (int)(int16_t)(game.yaw - co.cam_yaw) / 5) & 0xFFFF;
        if (co.flash_timer > 0) co.flash_timer--;
        if (co.state_timer > 0) co.state_timer--;
        if (co.state == CS_PLAY && (game.over || game.won)) {
            co.state = CS_OVER;
            co.state_timer = 40;
            if (game.score > co.best) co.best = game.score;
        }
    }
    us_update = plat_us_since(t0);
    if (co.state != last_state) {
        static const char *const st[] = { "TITLE", "FLY", "OVER" };
        AB_I("STATE %s score %ld", st[co.state], (long)game.score);
        last_state = co.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (co.help) render_help(cv, co.help);
    else if (co.state == CS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    v_polys = a3_npolys;
    v_battery = game.battery >> 16;
    v_job = game.job_time / 25;
    v_x = game.x >> 16;
    v_y = game.y >> 16;
    v_z = game.z >> 16;
    v_speed = (int32_t)((fhypot(game.vx, game.vz) * 10) >> 16);
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
        ab_test_begin("drone_courier_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
