// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Tank Duel's shell on the shared engine: input, the title with
 * the autopilot fighting behind it, the fight, game over, the help pages,
 * sound and explosions for the events, and everything exposed over the
 * AmigaBridge so the game can be driven and tested from the host:
 *
 *   vars   score round enemies kills shots hits hp reload state over won
 *          autoplay x z yaw speed damage start_round paused help fps polys
 *          us_update us_draw (24: the bridge takes 32 a client)
 *   hooks  status, start [round], title, autoplay <on|off>, spawn <type>
 *          [dist], clear (every enemy destroyed), hp <n>, seed <n>,
 *          help <0-2>, pause <on|off>, selftest, activate
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "td.h"
#include "duel.h"

const char game_title[] = "Tank Duel";
const char game_client[] = "tank_duel";
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

enum { SFX_FIRE, SFX_EFIRE, SFX_LOB, SFX_BOOM, SFX_HIT, SFX_GLANCE, SFX_BOUNCE, SFX_HURT, SFX_CRATE, SFX_CLEAR, SFX_OVER, SFX_WIN };

Duel du;
TDGame game;
static unsigned prev_in, play_in;
static int fire_lock;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw, v_x, v_z, v_speed, v_enemies, v_hp, v_reload;
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
    if (n > sizeof(du.flash) - 1) n = sizeof(du.flash) - 1;
    memcpy(du.flash, s, n);
    du.flash[n] = 0;
    du.flash_timer = 30;
    du.flash_pen = pen;
}

static void blast(fix x, fix z, int y, int size, int pen)
{
    int k;
    for (k = 0; k < MAX_BLASTS; k++)
        if (du.blast[k].t <= 0) {
            du.blast[k].t = 10;
            du.blast[k].x = x >> 16;
            du.blast[k].z = z >> 16;
            du.blast[k].y = y;
            du.blast[k].size = size;
            du.blast[k].pen = pen;
            return;
        }
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_FIRE, 1800, 60, 30))) { a3_noise(d, 1800, 127, 3, 2); }
    if ((d = a3_sfx_define(SFX_EFIRE, 1400, 44, 20))) a3_noise(d, 1400, 100, 5, 2);
    if ((d = a3_sfx_define(SFX_LOB, 1600, 44, 20))) a3_tone(d, 1600, 300, 900, 80, 900);
    if ((d = a3_sfx_define(SFX_BOOM, 4000, 60, 45))) a3_noise(d, 4000, 127, 10, 2);
    if ((d = a3_sfx_define(SFX_HIT, 1200, 54, 35))) { a3_noise(d, 600, 120, 2, 2); a3_tone(d + 600, 600, 500, 300, 100, 500); }
    if ((d = a3_sfx_define(SFX_GLANCE, 900, 50, 30))) a3_tone(d, 900, 2200, 1800, 90, 800);
    if ((d = a3_sfx_define(SFX_BOUNCE, 500, 40, 15))) a3_tone(d, 500, 1500, 1100, 80, 400);
    if ((d = a3_sfx_define(SFX_HURT, 2400, 60, 50))) { a3_noise(d, 1200, 127, 4, 2); a3_tone(d + 1200, 1200, 160, 90, 110, 1000); }
    if ((d = a3_sfx_define(SFX_CRATE, 1400, 50, 25))) a3_noise(d, 1400, 110, 6, 2);
    if ((d = a3_sfx_define(SFX_CLEAR, 4800, 60, 55))) {
        a3_tone(d, 1200, 523, 523, 100, 80);
        a3_tone(d + 1200, 1200, 659, 659, 100, 80);
        a3_tone(d + 2400, 1200, 784, 784, 100, 80);
        a3_tone(d + 3600, 1200, 1046, 1046, 110, 1000);
    }
    if ((d = a3_sfx_define(SFX_OVER, 6000, 60, 60))) a3_tone(d, 6000, 330, 80, 110, 3000);
    if ((d = a3_sfx_define(SFX_WIN, 6000, 60, 60))) {
        a3_tone(d, 1500, 784, 784, 100, 80);
        a3_tone(d + 1500, 1500, 1046, 1046, 100, 80);
        a3_tone(d + 3000, 3000, 1568, 1568, 110, 2000);
    }
}

static void handle_events(void)
{
    TDEvent e;
    int quiet = du.state == DS_TITLE;
    while (td_pop_event(&game, &e)) {
        switch (e.type) {                    /* what is seen happens in the demo too */
        case TDE_KILL: blast(e.x, e.z, 20, 26, 30); break;
        case TDE_HIT: case TDE_GLANCE: case TDE_CRATE_HIT: blast(e.x, e.z, 20, 8, 27); break;
        case TDE_CRATE_BREAK: blast(e.x, e.z, 26, 22, 14); break;
        case TDE_SPLASH: blast(e.x, e.z, e.a == 2 ? 10 : 20, e.a == 2 ? 30 : 8, 30); break;
        case TDE_HURT: if (!quiet) du.hurt_timer = 6; break;
        }
        if (quiet) continue;
        switch (e.type) {
        case TDE_FIRE: a3_sfx(SFX_FIRE); break;
        case TDE_EFIRE: a3_sfx(SFX_EFIRE); break;
        case TDE_LOB: a3_sfx(SFX_LOB); flash("INCOMING MORTAR", 26); break;
        case TDE_KILL: a3_sfx(SFX_BOOM); flash(e.b == TK_SCOUT ? "SCOUT DOWN" : e.b == TK_MORTAR ? "MORTAR DOWN" : "BULWARK DOWN", 28); break;
        case TDE_HIT: a3_sfx(SFX_HIT); break;
        case TDE_GLANCE: a3_sfx(SFX_GLANCE); flash("GLANCED OFF THE SHIELD", 29); break;
        case TDE_BOUNCE: a3_sfx(SFX_BOUNCE); break;
        case TDE_HURT: a3_sfx(SFX_HURT); break;
        case TDE_SPLASH: if (e.a == 2) a3_sfx(SFX_BOOM); break;
        case TDE_CRATE_HIT: case TDE_CRATE_BREAK: a3_sfx(SFX_CRATE); break;
        case TDE_ROUND_CLEAR: a3_sfx(SFX_CLEAR); break;
        case TDE_DEAD: a3_sfx(SFX_OVER); break;
        case TDE_WIN: a3_sfx(SFX_WIN); break;
        }
        if (e.type == TDE_KILL || e.type == TDE_DEAD || e.type == TDE_ROUND_CLEAR || e.type == TDE_ROUND || e.type == TDE_WIN)
            AB_I("EV %s a=%ld b=%ld score=%ld round=%ld hp=%ld", td_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.round, (long)game.tk[0].hp);
    }
}

/* ---------- game flow ---------- */
static void start_game(int round)
{
    td_new_game(&game, next_seed++, round);
    du.state = DS_PLAY;
    du.paused = 0;
    du.help = 0;
    du.autoplay = 0;
    fire_lock = 1;
    memset(du.blast, 0, sizeof(du.blast));
    AB_I("START round %ld seed %ld", (long)round, (long)game.seed);
}

static void to_title(void)
{
    du.state = DS_TITLE;
    td_new_game(&game, 777 + next_seed, 1);
    memset(du.blast, 0, sizeof(du.blast));
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[220];
    static const char *const st[] = { "title", "fight", "over" };
    int k, n = 0;
    (void)args;
    n = sprintf(tmp, "%s r%ld hp %ld at %ld,%ld score %ld k%ld s%ld h%ld |", st[du.state], (long)game.round,
                (long)game.tk[0].hp, (long)(game.tk[0].x >> 16), (long)(game.tk[0].z >> 16), (long)game.score,
                (long)game.kills, (long)game.shots, (long)game.hits);
    for (k = 1; k < game.ntanks && n < 100; k++)
        if (game.tk[k].alive) n += sprintf(tmp + n, " %c%ld", td_tank_names[game.tk[k].type][0], (long)game.tk[k].hp);
    if (n < 110) sprintf(tmp + n, "%s%s", game.over ? " over" : "", game.won ? " won" : "");
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int round = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (round < 1) round = 1;
    if (round > TD_ROUNDS) round = TD_ROUNDS;
    start_game(round);
    sprintf(tmp, "round %ld, %ld enemies, seed %ld", (long)round, (long)td_enemies_left(&game), (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    du.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, du.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

/* a practice target: every enemy and all cover gone, then one of `type`
 * straight ahead at `dist`, facing you (a bulwark: its shield), holding
 * still with its gun silent */
static int hook_spawn(const char *args, char *buf, int bufsz)
{
    int type = !args ? TK_SCOUT : args[0] == 'm' ? TK_MORTAR : args[0] == 'b' ? TK_BULWARK : TK_SCOUT, e;
    const char *sp = args ? strchr(args, ' ') : NULL;
    int32_t d = sp ? atol(sp + 1) : 300;
    Tank *p = &game.tk[0];
    game.ntanks = 1;
    game.nobs = 0;
    game.between = 0;
    e = td_add_tank(&game, type, p->x + d * a3_sin(p->yaw), p->z + d * a3_cos(p->yaw), p->yaw + 0x8000);
    if (e > 0) { game.tk[e].reload = 9999; game.tk[e].held = 1; }
    reply(buf, bufsz, e > 0 ? td_tank_names[type] : "no room");
    return 0;
}

static int hook_clear(const char *args, char *buf, int bufsz)
{
    int k;
    (void)args;
    for (k = 1; k < game.ntanks; k++) game.tk[k].alive = 0;
    reply(buf, bufsz, "every enemy destroyed");
    return 0;
}

static int hook_hp(const char *args, char *buf, int bufsz)
{
    game.tk[0].hp = args && args[0] ? atoi(args) : TD_HP;
    reply(buf, bufsz, "armour set");
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
    du.help = args && args[0] ? atoi(args) : 1;
    if (du.help < 0 || du.help > 2) du.help = 0;
    reply(buf, bufsz, du.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    du.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, du.paused ? "paused" : "running");
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
    ab_register_var("round",       AB_TYPE_I32, &game.round);
    ab_register_var("enemies",     AB_TYPE_I32, &v_enemies);
    ab_register_var("kills",       AB_TYPE_I32, &game.kills);
    ab_register_var("shots",       AB_TYPE_I32, &game.shots);
    ab_register_var("hits",        AB_TYPE_I32, &game.hits);
    ab_register_var("hp",          AB_TYPE_I32, &v_hp);
    ab_register_var("reload",      AB_TYPE_I32, &v_reload);
    ab_register_var("state",       AB_TYPE_I32, &du.state);
    ab_register_var("over",        AB_TYPE_I32, &game.over);
    ab_register_var("won",         AB_TYPE_I32, &game.won);
    ab_register_var("autoplay",    AB_TYPE_I32, &du.autoplay);
    ab_register_var("x",           AB_TYPE_I32, &v_x);
    ab_register_var("z",           AB_TYPE_I32, &v_z);
    ab_register_var("yaw",         AB_TYPE_I32, &game.tk[0].yaw);
    ab_register_var("speed",       AB_TYPE_I32, &v_speed);
    ab_register_var("damage",      AB_TYPE_I32, &game.damage_taken);
    ab_register_var("start_round", AB_TYPE_I32, &du.start_round);
    ab_register_var("paused",      AB_TYPE_I32, &du.paused);
    ab_register_var("help",        AB_TYPE_I32, &du.help);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",   AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line fight status", hook_status);
    ab_register_hook("start",    "start [round 1-8]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot fights", hook_autoplay);
    ab_register_hook("spawn",    "spawn scout/mortar/bulwark [DIST]: a lone practice target ahead, gun held", hook_spawn);
    ab_register_hook("clear",    "Every enemy destroyed (the round is won)", hook_clear);
    ab_register_hook("hp",       "hp N: the armour", hook_hp);
    ab_register_hook("seed",     "seed N: the next game's seed", hook_seed);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(duel_palette);
    plat_sky(4, 0x369, 0xBCD, 120);
    render_init();
    du.start_round = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { du.help = (du.help + 1) % 3; return 1; }
    if (du.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) du.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (du.state == DS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (du.state) {
    case DS_TITLE:
        if ((edge & IN_UP) && du.start_round < TD_ROUNDS) du.start_round++;
        if ((edge & IN_DOWN) && du.start_round > 1) du.start_round--;
        if (edge & (IN_FIRE | IN_START)) start_game(du.start_round);
        break;
    case DS_PLAY:
        if (edge & IN_PAUSE) du.paused = !du.paused;
        if (edge & IN_AUTO) du.autoplay = !du.autoplay;
        play_in = (in & IN_LEFT ? TD_IN_LEFT : 0) | (in & IN_RIGHT ? TD_IN_RIGHT : 0) | (in & IN_UP ? TD_IN_UP : 0) |
                  (in & IN_DOWN ? TD_IN_DOWN : 0) | ((in & IN_FIRE) && !fire_lock ? TD_IN_FIRE : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case DS_OVER:
        if (du.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return running;
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
        if (du.help || du.paused) continue;
        if (du.state == DS_TITLE) {
            if (game.over || game.won) td_new_game(&game, a3_hash(game.seed), 1 + (int)(game.seed % 3));
            td_tick(&game, td_bot(&game));
        } else if (du.state == DS_PLAY) {
            td_tick(&game, du.autoplay ? td_bot(&game) : play_in);
        }
        handle_events();
        for (k = 0; k < MAX_BLASTS; k++) if (du.blast[k].t > 0) du.blast[k].t--;
        if (du.flash_timer > 0) du.flash_timer--;
        if (du.state_timer > 0) du.state_timer--;
        if (du.hurt_timer > 0) du.hurt_timer--;
        if (du.state == DS_PLAY && (game.over || game.won)) {
            du.state = DS_OVER;
            du.state_timer = 40;
            if (game.score > du.best) du.best = game.score;
        }
    }
    us_update = plat_us_since(t0);
    if (du.state != last_state) {
        static const char *const st[] = { "TITLE", "FIGHT", "OVER" };
        AB_I("STATE %s score %ld", st[du.state], (long)game.score);
        last_state = du.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (du.help) render_help(cv, du.help);
    else if (du.state == DS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    v_polys = a3_npolys;
    v_x = game.tk[0].x >> 16;
    v_z = game.tk[0].z >> 16;
    v_speed = (int32_t)((game.tk[0].v * 10) >> 16);
    v_enemies = td_enemies_left(&game);
    v_hp = game.tk[0].hp;
    v_reload = game.tk[0].reload;
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
        ab_test_begin("tank_duel_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
