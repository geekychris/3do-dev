// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Harbour Crane's shell on the shared engine: input, the title
 * with the autopilot working behind it, play, game over, the help pages,
 * sound for the events, and everything exposed over the AmigaBridge so
 * the game can be driven and tested from the host:
 *
 *   vars   score level loaded nbox lost dents state over won capsized
 *          autoplay trolley_x hook_x hook_y cable carrying next falling
 *          heel_mrad list_mrad swing_mrad time_s total_loaded start_level
 *          paused help fps polys us_update us_draw
 *   hooks  status, start [level], title, autoplay <on|off>, grab,
 *          place <slot> [height], box <len> <weight>, wind <milli>,
 *          swell <milli>, time <s>, seed <n>, help <0-2>, pause <on|off>,
 *          selftest, activate
 *   mem    heights (6 bytes: the stacks in each slot)
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_i64.h"
#include "a3d_sound.h"
#include "hc.h"
#include "crane.h"

const char game_title[] = "Harbour Crane";
const char game_client[] = "harbour_crane";
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

enum { SFX_GRAB, SFX_RELEASE, SFX_LAND, SFX_DENT, SFX_LOST, SFX_BUMP, SFX_SPLASH, SFX_CLEAR, SFX_OVER, SFX_WIN, SFX_GUST };

Crane cr;
HCGame game;
static unsigned prev_in, play_in;
static int fire_lock;                   /* the fire that started the game doesn't also grab */
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw, v_tx, v_hx, v_hy, v_L, v_heel, v_list, v_swing, v_time;
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
    strncpy(cr.flash, s, sizeof(cr.flash) - 1);
    cr.flash_timer = 40;
    cr.flash_pen = pen;
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_GRAB, 900, 44, 20))) { a3_noise(d, 300, 80, 2, 2); a3_tone(d + 300, 600, 300, 300, 80, 400); }
    if ((d = a3_sfx_define(SFX_RELEASE, 600, 40, 20))) a3_tone(d, 600, 500, 350, 80, 400);
    if ((d = a3_sfx_define(SFX_LAND, 1600, 54, 30))) { a3_noise(d, 900, 120, 6, 2); a3_tone(d + 900, 700, 140, 110, 90, 600); }
    if ((d = a3_sfx_define(SFX_DENT, 2400, 60, 40))) { a3_noise(d, 1200, 127, 2, 2); a3_tone(d + 1200, 1200, 220, 160, 110, 1000); }
    if ((d = a3_sfx_define(SFX_LOST, 6000, 60, 50))) { a3_noise(d, 3000, 110, 10, 300); a3_tone(d + 3000, 3000, 300, 80, 90, 2500); }
    if ((d = a3_sfx_define(SFX_BUMP, 600, 40, 15))) a3_noise(d, 600, 100, 4, 2);
    if ((d = a3_sfx_define(SFX_SPLASH, 3000, 50, 25))) a3_noise(d, 3000, 90, 14, 400);
    if ((d = a3_sfx_define(SFX_CLEAR, 4800, 60, 55))) {
        a3_tone(d, 1200, 392, 392, 100, 80);           /* a ship's horn, then a fanfare */
        a3_tone(d + 1200, 1200, 392, 392, 100, 80);
        a3_tone(d + 2400, 2400, 523, 523, 110, 1600);
    }
    if ((d = a3_sfx_define(SFX_OVER, 6000, 60, 60))) a3_tone(d, 6000, 330, 90, 110, 3000);
    if ((d = a3_sfx_define(SFX_WIN, 6000, 60, 60))) {
        a3_tone(d, 1500, 523, 523, 100, 80);
        a3_tone(d + 1500, 1500, 659, 659, 100, 80);
        a3_tone(d + 3000, 3000, 784, 784, 110, 2000);
    }
    if ((d = a3_sfx_define(SFX_GUST, 2400, 34, 10))) a3_noise(d, 2400, 60, 30, 1200);
}

static void handle_events(void)
{
    HCEvent e;
    int quiet = cr.state == CS_TITLE;   /* the demo is quiet */
    while (hc_pop_event(&game, &e)) {
        if (e.type == HCE_LOST || (e.type == HCE_SPLASH && e.a == 1)) {
            cr.splash_timer = 20;
            cr.splash_x = (game.fx >> 16) + 24;
        }
        if (quiet) continue;
        switch (e.type) {
        case HCE_GRAB: a3_sfx(SFX_GRAB); break;
        case HCE_RELEASE: a3_sfx(SFX_RELEASE); break;
        case HCE_LAND: a3_sfx(SFX_LAND); break;
        case HCE_DENT: a3_sfx(SFX_DENT); flash("DENTED!", 26); break;
        case HCE_LOST: a3_sfx(SFX_LOST); flash("LOST IN THE HARBOUR", 26); break;
        case HCE_BUMP: a3_sfx(SFX_BUMP); break;
        case HCE_SPLASH: a3_sfx(SFX_SPLASH); break;
        case HCE_CLEAR: a3_sfx(SFX_CLEAR); break;
        case HCE_CAPSIZE: flash("SHE'S GOING OVER!", 26); break;
        case HCE_TIMEOUT: flash("TIME UP", 26); break;
        case HCE_OVER: a3_sfx(SFX_OVER); break;
        case HCE_WIN: a3_sfx(SFX_WIN); break;
        case HCE_GUST: a3_sfx(SFX_GUST); flash("GUST!", 29); break;
        }
        if (e.type != HCE_BUMP && e.type != HCE_SPLASH && e.type != HCE_GUST)
            AB_I("EV %s a=%ld b=%ld score=%ld level=%ld loaded=%ld lost=%ld", hc_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.level, (long)game.loaded, (long)game.lost);
    }
}

/* ---------- game flow ---------- */
static void start_game(int level)
{
    hc_new_game(&game, next_seed++, level);
    cr.state = CS_PLAY;
    cr.paused = 0;
    cr.help = 0;
    cr.autoplay = 0;
    fire_lock = 1;
    AB_I("START level %ld seed %ld", (long)level, (long)game.seed);
}

static void to_title(void)
{
    cr.state = CS_TITLE;
    hc_new_game(&game, 777 + next_seed, 1);
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[220];
    static const char *const st[] = { "title", "play", "over" };
    (void)args;
    sprintf(tmp, "%s level %ld | loaded %ld/%ld lost %ld dents %ld score %ld | trolley %ld hook %ld,%ld cable %ld | "
            "carrying %ld falling %ld | heel %ld list %ld mrad | stacks %ld%ld%ld%ld%ld%ld | time %ld s %s%s| fps %ld",
            st[cr.state], (long)game.level, (long)game.loaded, (long)game.nbox, (long)game.lost, (long)game.dents,
            (long)game.score, (long)(game.tx >> 16), (long)(hc_hook_x(&game) >> 16), (long)(hc_hook_y(&game) >> 16),
            (long)(game.L >> 16), (long)game.carrying, (long)game.falling, (long)v_heel, (long)v_list,
            (long)game.height[0], (long)game.height[1], (long)game.height[2], (long)game.height[3], (long)game.height[4],
            (long)game.height[5], (long)(game.time_left / 25), game.capsized ? "capsized " : "",
            game.over ? "over " : game.won ? "won " : "", (long)v_fps);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int level = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (level < 1) level = 1;
    if (level > HC_LEVELS) level = HC_LEVELS;
    start_game(level);
    sprintf(tmp, "level %ld, %ld containers, seed %ld", (long)level, (long)game.nbox, (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    cr.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, cr.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

/* the hook straight down over the next container, resting on it, and grab */
static int hook_grab(const char *args, char *buf, int bufsz)
{
    (void)args;
    if (game.carrying >= 0 || game.falling >= 0 || game.next >= game.nbox) { reply(buf, bufsz, "nothing to grab"); return 1; }
    game.tx = FROMI(HC_PICK_X);
    game.tv = game.th = game.om = game.ldot = 0;
    game.L = FROMI(HC_GANTRY_Y - HC_BOX_H - 4);
    game.prev_in = 0;
    hc_tick(&game, HC_IN_FIRE);
    reply(buf, bufsz, game.carrying >= 0 ? "grabbed" : "missed");
    return game.carrying < 0;
}

/* the carried container straight over slot s, h units above what is
 * there (default 4), and let go */
static int hook_place(const char *args, char *buf, int bufsz)
{
    int s = args && args[0] ? atoi(args) : 0, h = 4, w;
    const char *sp = args ? strchr(args, ' ') : NULL;
    fix x, sup;
    if (sp) h = atoi(sp + 1);
    if (game.carrying < 0) { reply(buf, bufsz, "not carrying anything"); return 1; }
    w = hc_box_w(&game, game.carrying);
    x = FROMI(HC_SHIP_X0 + s * HC_SLOT_W + 1 + w / 2);
    sup = hc_support_y(&game, x - FROMI(w / 2), x + FROMI(w / 2), 0);
    game.tx = x;
    game.tv = game.th = game.om = game.ldot = 0;
    game.L = FROMI(HC_GANTRY_Y) - (sup + FROMI(HC_BOX_H + h));
    game.prev_in = 0;
    hc_tick(&game, HC_IN_FIRE);
    reply(buf, bufsz, "let go");
    return 0;
}

static int hook_box(const char *args, char *buf, int bufsz)
{
    const char *sp = args ? strchr(args, ' ') : NULL;
    if (game.next >= game.nbox) { reply(buf, bufsz, "no container waiting"); return 1; }
    if (args && args[0]) game.box[game.next].len = atoi(args) == 2 ? 2 : 1;
    if (sp) { int w = atoi(sp + 1); game.box[game.next].weight = w < 1 ? 1 : w > 4 ? 4 : w; }
    reply(buf, bufsz, "the next container changed");
    return 0;
}

static int hook_wind(const char *args, char *buf, int bufsz)
{
    game.wind = (args && args[0] ? atoi(args) : 0) * 65536L / 1000;
    game.gust = 0;
    reply(buf, bufsz, "wind set");
    return 0;
}

static int hook_swell(const char *args, char *buf, int bufsz)
{
    game.swell = (args && args[0] ? atoi(args) : 0) * 65536L / 1000;
    reply(buf, bufsz, "swell set");
    return 0;
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
    reply(buf, bufsz, "the next game uses that seed");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    cr.help = args && args[0] ? atoi(args) : 1;
    if (cr.help < 0 || cr.help > 2) cr.help = 0;
    reply(buf, bufsz, cr.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    cr.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, cr.paused ? "paused" : "running");
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
    ab_register_var("score",        AB_TYPE_I32, &game.score);
    ab_register_var("level",        AB_TYPE_I32, &game.level);
    ab_register_var("loaded",       AB_TYPE_I32, &game.loaded);
    ab_register_var("nbox",         AB_TYPE_I32, &game.nbox);
    ab_register_var("lost",         AB_TYPE_I32, &game.lost);
    ab_register_var("dents",        AB_TYPE_I32, &game.dents);
    ab_register_var("state",        AB_TYPE_I32, &cr.state);
    ab_register_var("over",         AB_TYPE_I32, &game.over);
    ab_register_var("won",          AB_TYPE_I32, &game.won);
    ab_register_var("capsized",     AB_TYPE_I32, &game.capsized);
    ab_register_var("autoplay",     AB_TYPE_I32, &cr.autoplay);
    ab_register_var("trolley_x",    AB_TYPE_I32, &v_tx);
    ab_register_var("hook_x",       AB_TYPE_I32, &v_hx);
    ab_register_var("hook_y",       AB_TYPE_I32, &v_hy);
    ab_register_var("cable",        AB_TYPE_I32, &v_L);
    ab_register_var("carrying",     AB_TYPE_I32, &game.carrying);
    ab_register_var("next",         AB_TYPE_I32, &game.next);
    ab_register_var("falling",      AB_TYPE_I32, &game.falling);
    ab_register_var("heel_mrad",    AB_TYPE_I32, &v_heel);
    ab_register_var("list_mrad",    AB_TYPE_I32, &v_list);
    ab_register_var("swing_mrad",   AB_TYPE_I32, &v_swing);
    ab_register_var("time_s",       AB_TYPE_I32, &v_time);
    ab_register_var("total_loaded", AB_TYPE_I32, &game.total_loaded);
    ab_register_var("start_level",  AB_TYPE_I32, &cr.start_level);
    ab_register_var("paused",       AB_TYPE_I32, &cr.paused);
    ab_register_var("help",         AB_TYPE_I32, &cr.help);
    ab_register_var("fps",          AB_TYPE_I32, &v_fps);
    ab_register_var("polys",        AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",    AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",      AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line game status", hook_status);
    ab_register_hook("start",    "start [level 1-8]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot works the crane", hook_autoplay);
    ab_register_hook("grab",     "The hook straight onto the next container, and grab it", hook_grab);
    ab_register_hook("place",    "place SLOT [H]: the load straight over slot 0-5, H units up (4), and let go", hook_place);
    ab_register_hook("box",      "box LEN WEIGHT: change the next container (1-2 slots, 1-4 t)", hook_box);
    ab_register_hook("wind",     "wind N: N/1000 units a tick^2 (+ to the right)", hook_wind);
    ab_register_hook("swell",    "swell N: the roll, N/1000 radians", hook_swell);
    ab_register_hook("time",     "time S: seconds left", hook_time);
    ab_register_hook("seed",     "seed N: the next game's seed", hook_seed);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);

    ab_register_memregion("heights", game.height, sizeof(game.height), "6 bytes: tiers in each slot");
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(crane_palette);
    plat_sky(4, 0x48C, 0xBDE, 140);
    cr.start_level = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { cr.help = (cr.help + 1) % 3; return 1; }
    if (cr.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) cr.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (cr.state == CS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (cr.state) {
    case CS_TITLE:
        if ((edge & IN_UP) && cr.start_level < HC_LEVELS) cr.start_level++;
        if ((edge & IN_DOWN) && cr.start_level > 1) cr.start_level--;
        if (edge & (IN_FIRE | IN_START)) start_game(cr.start_level);
        break;
    case CS_PLAY:
        if (edge & IN_PAUSE) cr.paused = !cr.paused;
        if (edge & IN_AUTO) cr.autoplay = !cr.autoplay;
        play_in = (in & IN_LEFT ? HC_IN_LEFT : 0) | (in & IN_RIGHT ? HC_IN_RIGHT : 0) | (in & IN_UP ? HC_IN_UP : 0) |
                  (in & IN_DOWN ? HC_IN_DOWN : 0) | ((in & IN_FIRE) && !fire_lock ? HC_IN_FIRE : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case CS_OVER:
        if (cr.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
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
        if (cr.help || cr.paused) continue;
        if (cr.state == CS_TITLE) {
            if (game.over || game.won) hc_new_game(&game, a3_hash(game.seed), 1 + (int)(game.seed % 2));
            hc_tick(&game, hc_bot(&game));
        } else if (cr.state == CS_PLAY) {
            hc_tick(&game, cr.autoplay ? hc_bot(&game) : play_in);
        }
        handle_events();
        if (cr.flash_timer > 0) cr.flash_timer--;
        if (cr.state_timer > 0) cr.state_timer--;
        if (cr.splash_timer > 0) cr.splash_timer--;
        if (cr.state == CS_PLAY && (game.over || game.won)) {
            cr.state = CS_OVER;
            cr.state_timer = 40;
            if (game.score > cr.best) cr.best = game.score;
        }
    }
    us_update = plat_us_since(t0);
    if (cr.state != last_state) {
        static const char *const st[] = { "TITLE", "PLAY", "OVER" };
        AB_I("STATE %s score %ld", st[cr.state], (long)game.score);
        last_state = cr.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (cr.help) render_help(cv, cr.help);
    else if (cr.state == CS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    v_polys = a3_npolys;
    v_tx = game.tx >> 16;
    v_hx = hc_hook_x(&game) >> 16;
    v_hy = hc_hook_y(&game) >> 16;
    v_L = game.L >> 16;
    v_heel = a3_mul_shr(game.heel, 1000, 16);      /* 3DO: no int64_t */
    v_list = a3_mul_shr(game.list, 1000, 16);
    v_swing = a3_mul_shr(game.th, 1000, 16);
    v_time = game.time_left / 25;
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
        ab_test_begin("harbour_crane_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
