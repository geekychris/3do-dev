// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Marble Tilt's shell on the shared engine: input, the title with
 * the autopilot rolling behind it, play, game over, the help pages, sound
 * for the events, and everything exposed over the AmigaBridge so the game
 * can be driven and tested from the host:
 *
 *   vars   score level time_s falls bumps state over won autoplay x z vx
 *          vz roll pitch gates falling row col start_level paused help fps
 *          polys us_update us_draw (27: the bridge takes 32 a client)
 *   hooks  status, start [level], title, autoplay <on|off>, put <row> <col>,
 *          time <s>, help <0-2>, pause <on|off>, selftest, activate
 *   mem    board (144 bytes: the cells, row 0 the far edge)
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "mt.h"
#include "tilt.h"

const char game_title[] = "Marble Tilt";
const char game_client[] = "marble_tilt";
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

enum { SFX_WALL, SFX_BUMP, SFX_FALL, SFX_RESPAWN, SFX_CP, SFX_GATE, SFX_EXIT, SFX_OVER, SFX_WIN };

Tilt tl;
MTGame game;
static unsigned prev_in, play_in;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw, v_x, v_z, v_vx, v_vz, v_roll, v_pitch, v_time, v_row, v_col;
static uint8_t board_mem[MT_N * MT_N];
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
    if (n > sizeof(tl.flash) - 1) n = sizeof(tl.flash) - 1;
    memcpy(tl.flash, s, n);
    tl.flash[n] = 0;
    tl.flash_timer = 30;
    tl.flash_pen = pen;
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_WALL, 400, 40, 10))) a3_noise(d, 400, 90, 3, 2);
    if ((d = a3_sfx_define(SFX_BUMP, 900, 54, 25))) a3_tone(d, 900, 900, 1500, 100, 700);
    if ((d = a3_sfx_define(SFX_FALL, 3000, 56, 40))) a3_tone(d, 3000, 800, 120, 100, 1500);
    if ((d = a3_sfx_define(SFX_RESPAWN, 800, 44, 20))) a3_tone(d, 800, 600, 900, 80, 500);
    if ((d = a3_sfx_define(SFX_CP, 1600, 50, 30))) { a3_tone(d, 800, 784, 784, 90, 100); a3_tone(d + 800, 800, 1046, 1046, 90, 600); }
    if ((d = a3_sfx_define(SFX_GATE, 700, 34, 10))) a3_noise(d, 700, 70, 12, 300);
    if ((d = a3_sfx_define(SFX_EXIT, 4800, 60, 55))) {
        a3_tone(d, 1200, 523, 523, 100, 80);
        a3_tone(d + 1200, 1200, 659, 659, 100, 80);
        a3_tone(d + 2400, 1200, 784, 784, 100, 80);
        a3_tone(d + 3600, 1200, 1046, 1046, 110, 1000);
    }
    if ((d = a3_sfx_define(SFX_OVER, 6000, 60, 60))) a3_tone(d, 6000, 440, 110, 110, 3000);
    if ((d = a3_sfx_define(SFX_WIN, 6000, 60, 60))) {
        a3_tone(d, 1500, 784, 784, 100, 80);
        a3_tone(d + 1500, 1500, 1046, 1046, 100, 80);
        a3_tone(d + 3000, 3000, 1568, 1568, 110, 2000);
    }
}

static void handle_events(void)
{
    MTEvent e;
    int quiet = tl.state == TS_TITLE;
    while (mt_pop_event(&game, &e)) {
        if (quiet) continue;
        switch (e.type) {
        case MTE_WALL: a3_sfx(SFX_WALL); break;
        case MTE_BUMP: a3_sfx(SFX_BUMP); break;
        case MTE_FALL: a3_sfx(SFX_FALL); flash("FELL THROUGH -3 S", 26); break;
        case MTE_RESPAWN: a3_sfx(SFX_RESPAWN); break;
        case MTE_CHECKPOINT: a3_sfx(SFX_CP); flash("CHECKPOINT", 29); break;
        case MTE_GATE: a3_sfx(SFX_GATE); break;
        case MTE_EXIT: a3_sfx(SFX_EXIT); break;
        case MTE_TIMEOUT: flash("TIME UP", 26); break;
        case MTE_OVER: a3_sfx(SFX_OVER); break;
        case MTE_WIN: a3_sfx(SFX_WIN); break;
        }
        if (e.type != MTE_WALL && e.type != MTE_BUMP && e.type != MTE_GATE)
            AB_I("EV %s a=%ld b=%ld score=%ld level=%ld", mt_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.level);
    }
}

/* ---------- game flow ---------- */
static void start_game(int level)
{
    mt_new_game(&game, next_seed++, level);
    tl.state = TS_PLAY;
    tl.paused = 0;
    tl.help = 0;
    tl.autoplay = 0;
    AB_I("START level %ld %s", (long)level, mt_level_names[level - 1]);
}

static void to_title(void)
{
    tl.state = TS_TITLE;
    mt_new_game(&game, 777 + next_seed, 1);
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[120];
    static const char *const st[] = { "title", "play", "over" };
    (void)args;
    sprintf(tmp, "%s L%ld | cell %ld,%ld [%c] v %ld,%ld | t %ld s falls %ld score %ld%s%s%s",
            st[tl.state], (long)game.level, (long)v_row, (long)v_col, mt_at(&game, v_row, v_col),
            (long)v_vx, (long)v_vz, (long)(game.time_left / 25), (long)game.falls, (long)game.score,
            game.falling ? " falling" : "", game.over ? " over" : "", game.won ? " won" : "");
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int level = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (level < 1) level = 1;
    if (level > MT_LEVELS) level = MT_LEVELS;
    start_game(level);
    sprintf(tmp, "level %ld: %s", (long)level, mt_level_names[level - 1]);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    tl.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, tl.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

/* the marble, still, in the middle of cell (row, col) */
static int hook_put(const char *args, char *buf, int bufsz)
{
    int r = args && args[0] ? atoi(args) : 1, c = 1;
    const char *sp = args ? strchr(args, ' ') : NULL;
    char tmp[40];
    if (sp) c = atoi(sp + 1);
    game.x = mt_cell_x(c);
    game.z = mt_cell_z(r);
    game.vx = game.vz = 0;
    game.falling = 0;
    sprintf(tmp, "on %ld,%ld [%c]", (long)r, (long)c, mt_at(&game, r, c));
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_time(const char *args, char *buf, int bufsz)
{
    game.time_left = (args && args[0] ? atoi(args) : 60) * 25;
    reply(buf, bufsz, "time set");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    tl.help = args && args[0] ? atoi(args) : 1;
    if (tl.help < 0 || tl.help > 2) tl.help = 0;
    reply(buf, bufsz, tl.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    tl.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, tl.paused ? "paused" : "running");
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
    ab_register_var("time_s",      AB_TYPE_I32, &v_time);
    ab_register_var("falls",       AB_TYPE_I32, &game.falls);
    ab_register_var("bumps",       AB_TYPE_I32, &game.bumps);
    ab_register_var("state",       AB_TYPE_I32, &tl.state);
    ab_register_var("over",        AB_TYPE_I32, &game.over);
    ab_register_var("won",         AB_TYPE_I32, &game.won);
    ab_register_var("autoplay",    AB_TYPE_I32, &tl.autoplay);
    ab_register_var("x",           AB_TYPE_I32, &v_x);
    ab_register_var("z",           AB_TYPE_I32, &v_z);
    ab_register_var("vx",          AB_TYPE_I32, &v_vx);
    ab_register_var("vz",          AB_TYPE_I32, &v_vz);
    ab_register_var("roll",        AB_TYPE_I32, &v_roll);
    ab_register_var("pitch",       AB_TYPE_I32, &v_pitch);
    ab_register_var("gates",       AB_TYPE_I32, &game.gates_open);
    ab_register_var("falling",     AB_TYPE_I32, &game.falling);
    ab_register_var("row",         AB_TYPE_I32, &v_row);
    ab_register_var("col",         AB_TYPE_I32, &v_col);
    ab_register_var("start_level", AB_TYPE_I32, &tl.start_level);
    ab_register_var("paused",      AB_TYPE_I32, &tl.paused);
    ab_register_var("help",        AB_TYPE_I32, &tl.help);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",   AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line game status", hook_status);
    ab_register_hook("start",    "start [level 1-8]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot rolls the marble", hook_autoplay);
    ab_register_hook("put",      "put ROW COL: the marble, still, on that cell", hook_put);
    ab_register_hook("time",     "time S: seconds left", hook_time);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);

    ab_register_memregion("board", board_mem, sizeof(board_mem), "144 bytes: the cells, row 0 the far edge");
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(tilt_palette);
    plat_sky(4, 0x225, 0x001, 256);
    tl.start_level = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { tl.help = (tl.help + 1) % 3; return 1; }
    if (tl.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) tl.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (tl.state == TS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (tl.state) {
    case TS_TITLE:
        if ((edge & (IN_UP | IN_RIGHT)) && tl.start_level < MT_LEVELS) tl.start_level++;
        if ((edge & (IN_DOWN | IN_LEFT)) && tl.start_level > 1) tl.start_level--;
        if (edge & (IN_FIRE | IN_START)) start_game(tl.start_level);
        break;
    case TS_PLAY:
        if (edge & IN_PAUSE) tl.paused = !tl.paused;
        if (edge & IN_AUTO) tl.autoplay = !tl.autoplay;
        play_in = (in & IN_LEFT ? MT_IN_LEFT : 0) | (in & IN_RIGHT ? MT_IN_RIGHT : 0) | (in & IN_UP ? MT_IN_UP : 0) |
                  (in & IN_DOWN ? MT_IN_DOWN : 0);
        break;
    case TS_OVER:
        if (tl.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
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
        if (tl.help || tl.paused) continue;
        if (tl.state == TS_TITLE) {
            if (game.over || game.won || game.between == 1) mt_new_game(&game, game.seed + 1, 1 + (int)(game.seed % MT_LEVELS));
            mt_tick(&game, mt_bot(&game));
        } else if (tl.state == TS_PLAY) {
            mt_tick(&game, tl.autoplay ? mt_bot(&game) : play_in);
        }
        handle_events();
        if (tl.flash_timer > 0) tl.flash_timer--;
        if (tl.state_timer > 0) tl.state_timer--;
        if (tl.state == TS_PLAY && (game.over || game.won)) {
            tl.state = TS_OVER;
            tl.state_timer = 40;
            if (game.score > tl.best) tl.best = game.score;
        }
    }
    us_update = plat_us_since(t0);
    if (tl.state != last_state) {
        static const char *const st[] = { "TITLE", "PLAY", "OVER" };
        AB_I("STATE %s score %ld", st[tl.state], (long)game.score);
        last_state = tl.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (tl.help) render_help(cv, tl.help);
    else if (tl.state == TS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    int r;
    v_polys = a3_npolys;
    v_x = game.x >> 16;
    v_z = game.z >> 16;
    v_vx = (int32_t)((game.vx * 100) >> 16);
    v_vz = (int32_t)((game.vz * 100) >> 16);
    v_roll = (int32_t)((game.roll * 1000) >> 16);
    v_pitch = (int32_t)((game.pitch * 1000) >> 16);
    v_time = game.time_left / 25;
    v_row = mt_row_of(game.z);
    v_col = mt_col_of(game.x);
    for (r = 0; r < MT_N; r++) memcpy(board_mem + r * MT_N, game.cell[r], MT_N);
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
        ab_test_begin("marble_tilt_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
