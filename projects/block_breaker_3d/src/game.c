// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Block Breaker 3D's shell on the shared engine: input, the
 * title with the autopilot playing behind it, play, game over, the help
 * pages, sound and the bits of broken bricks for the events, and
 * everything exposed over the AmigaBridge so the game can be driven and
 * tested from the host:
 *
 *   vars   score level lives left state over won autoplay paddle_x paddle_y
 *          ball_x ball_y ball_z speed broken paddle_hits stuck start_level
 *          paused help fps polys us_update us_draw
 *   hooks  status, start [level], title, autoplay <on|off>, serve, clear,
 *          paddle <x> <y>, capsule <wide|slow|life|plough>, lives <n>,
 *          seed <n>, help <0-2>, pause <on|off>, selftest, activate
 *   mem    cells (72 bytes: [layer][row][col], layer 0 nearest, row 0 lowest)
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "bb.h"
#include "breaker.h"

const char game_title[] = "Block Breaker 3D";
const char game_client[] = "block_breaker_3d";
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

enum { SFX_WALL, SFX_PADDLE, SFX_BREAK, SFX_HIT, SFX_STEEL, SFX_LOST, SFX_SERVE, SFX_CAPSULE, SFX_CLEAR, SFX_OVER, SFX_WIN };

Breaker br;
BBGame game;
static unsigned prev_in, play_in;
static int fire_lock;                   /* the fire that started the game doesn't also serve */
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw, v_px, v_py, v_bx, v_by, v_bz;
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

static void flash(const char *s)
{
    strncpy(br.flash, s, sizeof(br.flash) - 1);
    br.flash_timer = 40;
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_WALL, 200, 20, 5))) a3_tone(d, 200, 300, 260, 50, 150);
    if ((d = a3_sfx_define(SFX_PADDLE, 700, 48, 20))) a3_tone(d, 700, 520, 620, 100, 500);
    if ((d = a3_sfx_define(SFX_BREAK, 1400, 50, 25))) { a3_noise(d, 700, 90, 3, 2); a3_tone(d + 700, 700, 900, 1500, 80, 600); }
    if ((d = a3_sfx_define(SFX_HIT, 500, 44, 22))) a3_tone(d, 500, 1200, 900, 90, 400);
    if ((d = a3_sfx_define(SFX_STEEL, 900, 44, 22))) a3_tone(d, 900, 2000, 1900, 90, 850);
    if ((d = a3_sfx_define(SFX_LOST, 5000, 60, 50))) a3_tone(d, 5000, 600, 90, 110, 2500);
    if ((d = a3_sfx_define(SFX_SERVE, 600, 40, 15))) a3_tone(d, 600, 400, 900, 90, 300);
    if ((d = a3_sfx_define(SFX_CAPSULE, 3000, 54, 40))) {
        a3_tone(d, 1000, 659, 659, 90, 120);
        a3_tone(d + 1000, 1000, 880, 880, 90, 120);
        a3_tone(d + 2000, 1000, 1318, 1318, 100, 800);
    }
    if ((d = a3_sfx_define(SFX_CLEAR, 4800, 60, 55))) {
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

/* bits of a broken brick, flying off */
static void burst(int cell, int type)
{
    int32_t x0, y0, z0;
    int k, made = 0;
    bb_cell_box(cell, &x0, &y0, &z0);
    for (k = 0; k < MAX_PARTS && made < 6; k++) {
        Part *p = &br.part[k];
        if (p->life > 0) continue;
        p->x = (x0 + BB_CW / 2) * 16;
        p->y = (y0 + BB_CH / 2) * 16;
        p->z = z0 * 16;
        p->vx = (int32_t)(a3_rand(&next_seed) % 129) - 64;
        p->vy = (int32_t)(a3_rand(&next_seed) % 129) - 40;
        p->vz = -(int32_t)(a3_rand(&next_seed) % 64) - 24;
        p->life = 14 + made;
        p->col = made & 1 ? brick_pen(type, 1) + 1 : brick_pen(type, 1);
        made++;
    }
}

static void parts_step(void)
{
    int k;
    for (k = 0; k < MAX_PARTS; k++) {
        Part *p = &br.part[k];
        if (p->life <= 0) continue;
        p->x += p->vx; p->y += p->vy; p->z += p->vz;
        p->vy -= 8;
        p->life--;
    }
}

static void handle_events(void)
{
    BBEvent e;
    int quiet = br.state == BS_TITLE;   /* the demo is quiet */
    while (bb_pop_event(&game, &e)) {
        if (e.type == BBE_BREAK) burst(e.a, e.b);
        if (quiet) continue;
        switch (e.type) {
        case BBE_WALL: a3_sfx(SFX_WALL); break;
        case BBE_PADDLE: a3_sfx(SFX_PADDLE); break;
        case BBE_BREAK: a3_sfx(SFX_BREAK); break;
        case BBE_HIT: a3_sfx(SFX_HIT); break;
        case BBE_STEEL: a3_sfx(SFX_STEEL); break;
        case BBE_LOST: a3_sfx(SFX_LOST); if (game.lives > 0) flash("BALL LOST"); break;
        case BBE_SERVE: a3_sfx(SFX_SERVE); break;
        case BBE_CAPSULE: {
            static const char *const msg[CAP_COUNT] = { "WIDE PADDLE", "SLOW BALL", "EXTRA LIFE", "PLOUGH!" };
            a3_sfx(SFX_CAPSULE);
            flash(msg[e.a]);
            break;
        }
        case BBE_CLEAR: a3_sfx(SFX_CLEAR); break;
        case BBE_OVER: a3_sfx(SFX_OVER); break;
        case BBE_WIN: a3_sfx(SFX_WIN); break;
        }
        if (e.type != BBE_WALL && e.type != BBE_PADDLE && e.type != BBE_HIT && e.type != BBE_STEEL && e.type != BBE_BREAK)
            AB_I("EV %s a=%ld b=%ld score=%ld level=%ld lives=%ld", bb_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.level, (long)game.lives);
    }
}

/* ---------- game flow ---------- */
static void start_game(int level)
{
    bb_new_game(&game, next_seed++, level);
    br.state = BS_PLAY;
    br.paused = 0;
    br.help = 0;
    br.autoplay = 0;
    fire_lock = 1;
    AB_I("START level %ld seed %ld", (long)level, (long)game.seed);
}

static void to_title(void)
{
    br.state = BS_TITLE;
    bb_new_game(&game, 777 + next_seed, 1);
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[200];
    static const char *const st[] = { "title", "play", "over" };
    (void)args;
    sprintf(tmp, "%s level %ld %s | score %ld lives %ld left %ld | ball %ld %ld %ld %s| paddle %ld %ld | %s%s%s%s| fps %ld",
            st[br.state], (long)game.level, bb_level_names[game.level - 1], (long)game.score, (long)game.lives,
            (long)game.left, (long)(game.bx / BB_FX), (long)(game.by / BB_FX), (long)(game.bz / BB_FX),
            game.stuck ? "stuck " : "", (long)(game.px / BB_FX), (long)(game.py / BB_FX),
            game.wide_t ? "wide " : "", game.slow_t ? "slow " : "", game.plough_t ? "plough " : "",
            game.over ? "over " : game.won ? "won " : "", (long)v_fps);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int level = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (level < 1) level = 1;
    if (level > BB_LEVELS) level = BB_LEVELS;
    start_game(level);
    sprintf(tmp, "level %ld, seed %ld", (long)level, (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    br.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, br.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

static int hook_serve(const char *args, char *buf, int bufsz)
{
    (void)args;
    if (!game.stuck || game.between) { reply(buf, bufsz, "the ball is in play"); return 1; }
    bb_tick(&game, BB_IN_FIRE);
    reply(buf, bufsz, "served");
    return 0;
}

static int hook_clear(const char *args, char *buf, int bufsz)
{
    (void)args;
    bb_clear_all(&game);
    reply(buf, bufsz, "every breakable brick broken");
    return 0;
}

static int hook_paddle(const char *args, char *buf, int bufsz)
{
    char tmp[48];
    if (args && args[0]) {
        const char *sp = strchr(args, ' ');
        game.px = (int32_t)atoi(args) * BB_FX;
        if (sp) game.py = (int32_t)atoi(sp + 1) * BB_FX;
    }
    sprintf(tmp, "paddle %ld %ld", (long)(game.px / BB_FX), (long)(game.py / BB_FX));
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_capsule(const char *args, char *buf, int bufsz)
{
    int t = !args ? CAP_WIDE : args[0] == 's' ? CAP_SLOW : args[0] == 'l' ? CAP_LIFE : args[0] == 'p' ? CAP_PLOUGH : CAP_WIDE;
    /* one dropped straight at the paddle */
    game.cap[0].active = 1;
    game.cap[0].type = t;
    game.cap[0].x = game.px;
    game.cap[0].y = game.py;
    game.cap[0].z = 120 * BB_FX;
    reply(buf, bufsz, bb_cap_names[t]);
    return 0;
}

static int hook_lives(const char *args, char *buf, int bufsz)
{
    game.lives = args && args[0] ? atoi(args) : BB_LIVES;
    reply(buf, bufsz, "lives set");
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
    br.help = args && args[0] ? atoi(args) : 1;
    if (br.help < 0 || br.help > 2) br.help = 0;
    reply(buf, bufsz, br.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    br.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, br.paused ? "paused" : "running");
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
    ab_register_var("left",        AB_TYPE_I32, &game.left);
    ab_register_var("state",       AB_TYPE_I32, &br.state);
    ab_register_var("over",        AB_TYPE_I32, &game.over);
    ab_register_var("won",         AB_TYPE_I32, &game.won);
    ab_register_var("autoplay",    AB_TYPE_I32, &br.autoplay);
    ab_register_var("paddle_x",    AB_TYPE_I32, &v_px);
    ab_register_var("paddle_y",    AB_TYPE_I32, &v_py);
    ab_register_var("ball_x",      AB_TYPE_I32, &v_bx);
    ab_register_var("ball_y",      AB_TYPE_I32, &v_by);
    ab_register_var("ball_z",      AB_TYPE_I32, &v_bz);
    ab_register_var("speed",       AB_TYPE_I32, &game.speed);
    ab_register_var("broken",      AB_TYPE_I32, &game.broken);
    ab_register_var("paddle_hits", AB_TYPE_I32, &game.paddle_hits);
    ab_register_var("stuck",       AB_TYPE_I32, &game.stuck);
    ab_register_var("wide",        AB_TYPE_I32, &game.wide_t);
    ab_register_var("start_level", AB_TYPE_I32, &br.start_level);
    ab_register_var("paused",      AB_TYPE_I32, &br.paused);
    ab_register_var("help",        AB_TYPE_I32, &br.help);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",   AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line game status", hook_status);
    ab_register_hook("start",    "start [level 1-8]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot plays", hook_autoplay);
    ab_register_hook("serve",    "Serve the ball", hook_serve);
    ab_register_hook("clear",    "Break every breakable brick (the level clears)", hook_clear);
    ab_register_hook("paddle",   "paddle X Y: put the paddle there (world units)", hook_paddle);
    ab_register_hook("capsule",  "capsule wide/slow/life/plough: drop one at the paddle", hook_capsule);
    ab_register_hook("lives",    "lives N", hook_lives);
    ab_register_hook("seed",     "seed N: the next game's seed", hook_seed);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);

    ab_register_memregion("cells", game.cell, sizeof(game.cell), "72 bytes: [layer][row][col], layer 0 nearest");
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(breaker_palette);
    plat_sky(0, 0, 0, 0);
    br.start_level = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { br.help = (br.help + 1) % 3; return 1; }
    if (br.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) br.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (br.state == BS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (br.state) {
    case BS_TITLE:
        if ((edge & IN_UP) && br.start_level < BB_LEVELS) br.start_level++;
        if ((edge & IN_DOWN) && br.start_level > 1) br.start_level--;
        if (edge & (IN_FIRE | IN_START)) start_game(br.start_level);
        break;
    case BS_PLAY:
        if (edge & IN_PAUSE) br.paused = !br.paused;
        if (edge & IN_AUTO) br.autoplay = !br.autoplay;
        play_in = (in & IN_LEFT ? BB_IN_LEFT : 0) | (in & IN_RIGHT ? BB_IN_RIGHT : 0) | (in & IN_UP ? BB_IN_UP : 0) |
                  (in & IN_DOWN ? BB_IN_DOWN : 0) | (in & IN_FIRE && !fire_lock ? BB_IN_FIRE : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case BS_OVER:
        if (br.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
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
        if (br.help || br.paused) continue;
        if (br.state == BS_TITLE) {
            if (game.over || game.won) bb_new_game(&game, a3_hash(game.seed), 1 + (int)(game.seed % 3));
            bb_tick(&game, bb_bot(&game));
        } else if (br.state == BS_PLAY) {
            bb_tick(&game, br.autoplay ? bb_bot(&game) : play_in);
        }
        handle_events();
        parts_step();
        if (br.flash_timer > 0) br.flash_timer--;
        if (br.state_timer > 0) br.state_timer--;
        if (br.state == BS_PLAY && (game.over || game.won)) {
            br.state = BS_OVER;
            br.state_timer = 40;
            if (game.score > br.best) br.best = game.score;
        }
    }
    us_update = plat_us_since(t0);
    if (br.state != last_state) {
        static const char *const st[] = { "TITLE", "PLAY", "OVER" };
        AB_I("STATE %s score %ld", st[br.state], (long)game.score);
        last_state = br.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (br.help) render_help(cv, br.help);
    else if (br.state == BS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    v_polys = a3_npolys;
    v_px = game.px / BB_FX;
    v_py = game.py / BB_FX;
    v_bx = game.bx / BB_FX;
    v_by = game.by / BB_FX;
    v_bz = game.bz / BB_FX;
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
        ab_test_begin("block_breaker_3d_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
