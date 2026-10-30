// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Boing Pinball's shell on the shared engine: input, the title
 * (the bot plays the chosen table behind it), play, game over, the help
 * pages, sound for the events, and everything exposed over the
 * AmigaBridge so the game can be driven and tested from the host:
 *
 *   vars   score ball balls_left state table pstate multiball mission locks
 *          bonus_x tilt_warn tilted ball_x ball_y ball_vx ball_vy ball_st
 *          nballs flip_l flip_r (degrees up) cam fps us_update us_draw
 *          substeps paused help
 *          (+ in_keys in_joy from the platform)
 *   hooks  status, start [comet|deep|foundry], title, table <name>,
 *          place <x> <y> [vx vy], launch [0-1000], flip <l|r> [ms],
 *          nudge <l|r|u>, event <name>, bot <on|off>, help <0-2>,
 *          pause <on|off>, selftest, activate
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "pinball.h"

int run_tests(void (*check)(int cond, const char *name));

const char game_title[] = "Boing Pinball";
const char game_client[] = "boing_pinball";
const char game_music[] = "PROGDIR:boing_pinball.mod";
const char game_voice[] = "";

#define IN_LFLIP  0x0001
#define IN_RFLIP  0x0002
#define IN_PLUNGE 0x0004
#define IN_NL     0x0008               /* nudge left (and the title's table choice) */
#define IN_NR     0x0010
#define IN_NU     0x0020
#define IN_PAUSE  0x0040
#define IN_HELP   0x0080
#define IN_ESC    0x0100
#define IN_START  0x0200
#define IN_BOT    0x0400               /* the 3DO: the bot plays (B on the pad) */

enum { SFX_FLIP, SFX_BUMPER, SFX_SLING, SFX_DROP, SFX_SPIN, SFX_LANE, SFX_SAUCER, SFX_KICK,
       SFX_PLUNGE, SFX_DRAIN, SFX_JINGLE, SFX_BIG, SFX_WARN, SFX_RAMP, SFX_FAIL, SFX_WALL };

Pinball pin;
Game game;
Table table;
static Bot bot;
static unsigned held, prev_in;
static int pending_nudge_x, pending_nudge_y, pending_pull = -1;
#ifdef AMIGA3DO
int bot_play;                           /* the 3DO: B toggles it; pad_3do.c reports it */
#else
static int bot_play;                    /* the bot plays the real game (a hook, for the tests) */
#endif
static int plunger_lock;                /* the key that started the game isn't a pull until it's let go */
static int flip_hold[2];                /* frames a hook holds a flipper */
static int32_t t_last, t_fps, acc_us;
static int frames, last_state = -1;
static int32_t v_fps, us_update, us_draw, v_ball, v_left, v_mb, v_mission, v_locks, v_bonusx, v_tw, v_tilted;
static int32_t v_bx, v_by, v_bvx, v_bvy, v_bst, v_nballs, v_sub, v_pstate, v_flip_l, v_flip_r;
static int selftest_pending, test_pass, test_fail;
static uint32_t seed = 1;

unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x60: case 0x31: case 0x64: case 0x66: return IN_LFLIP;    /* left shift, Z, left Alt, left Amiga */
    case 0x61: case 0x3A: case 0x65: case 0x67: return IN_RFLIP;    /* right shift, /, right Alt, right Amiga */
    case 0x40: case 0x4D: case 0x44: case 0x43: return IN_PLUNGE;   /* space, down, return, enter */
    case 0x4F: return IN_NL;
    case 0x4E: return IN_NR;
    case 0x4C: return IN_NU;
    case 0x19: return IN_PAUSE;                                     /* P */
    case 0x5F: case 0x25: return IN_HELP;                           /* HELP, H */
    case 0x45: return IN_ESC;
    case 0x50: return IN_START;                                     /* F1 */
#ifdef AMIGA3DO
    case 0x35: return IN_BOT;                                       /* B (the 3DO pad's B) */
#endif
    }
    return 0;
}

unsigned game_joy_bits(unsigned j)
{
    return (j & A3_JOY_LEFT ? IN_LFLIP : 0) | (j & A3_JOY_RIGHT ? IN_RFLIP : 0) |
           (j & (A3_JOY_FIRE | A3_JOY_DOWN) ? IN_PLUNGE : 0) | (j & A3_JOY_UP ? IN_NU : 0);
}

/* ---------- sound ---------- */
static void arp(int8_t *d, const int *notes, int n, int each, int amp)
{
    int i;
    for (i = 0; i < n; i++) a3_tone(d + i * each, each, notes[i], notes[i], amp, each / 3);
}

void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_FLIP, 600, 40, 20))) { a3_noise(d, 600, 70, 3, 2); a3_tone(d, 600, 140, 70, 40, 300); }
    if ((d = a3_sfx_define(SFX_BUMPER, 1100, 55, 30))) { a3_tone(d, 1100, 620, 240, 110, 500); a3_noise(d, 400, 50, 1, 2); }
    if ((d = a3_sfx_define(SFX_SLING, 800, 45, 25))) a3_tone(d, 800, 330, 140, 100, 400);
    if ((d = a3_sfx_define(SFX_DROP, 700, 45, 25))) { a3_tone(d, 700, 980, 500, 90, 300); a3_noise(d, 500, 60, 2, 2); }
    if ((d = a3_sfx_define(SFX_SPIN, 200, 25, 8))) a3_tone(d, 200, 1900, 1900, 60, 120);
    if ((d = a3_sfx_define(SFX_LANE, 800, 40, 18))) a3_tone(d, 800, 880, 1320, 90, 300);
    if ((d = a3_sfx_define(SFX_SAUCER, 2400, 55, 30))) a3_tone(d, 2400, 130, 60, 120, 1200);
    if ((d = a3_sfx_define(SFX_KICK, 900, 50, 26))) a3_noise(d, 900, 110, 4, 4);
    if ((d = a3_sfx_define(SFX_PLUNGE, 2000, 50, 28))) a3_noise(d, 2000, 90, 6, 200);
    if ((d = a3_sfx_define(SFX_DRAIN, 7000, 55, 40))) a3_tone(d, 7000, 420, 70, 100, 3000);
    if ((d = a3_sfx_define(SFX_JINGLE, 3600, 50, 45))) { static const int n[4] = { 784, 988, 1175, 1568 }; arp(d, n, 4, 900, 90); }
    if ((d = a3_sfx_define(SFX_BIG, 8400, 60, 60))) { static const int n[7] = { 440, 523, 659, 880, 1047, 1319, 1760 }; arp(d, n, 7, 1200, 100); }
    if ((d = a3_sfx_define(SFX_WARN, 4200, 60, 50))) a3_tone(d, 4200, 110, 110, 120, 1500);
    if ((d = a3_sfx_define(SFX_RAMP, 3000, 45, 30))) { static const int n[3] = { 523, 784, 1047 }; arp(d, n, 3, 1000, 90); }
    if ((d = a3_sfx_define(SFX_FAIL, 3000, 40, 25))) a3_tone(d, 3000, 300, 150, 80, 1500);
    if ((d = a3_sfx_define(SFX_WALL, 300, 30, 6))) a3_noise(d, 300, 60, 3, 2);
}

static void say(const char *s)
{
    strncpy(pin.toast, s, sizeof pin.toast - 1);
    pin.toast_timer = 40;
}

static void handle_events(void)
{
    int i;
    for (i = 0; i < game.nev; i++) {
        const Event *e = &game.ev[i];
        /* the lights react in the demo too, but it's quiet */
        if (e->type == EV_BUMPER && e->id >= 0 && e->id < PB_MAX_CIRC) pin.bumper_flash[e->id] = 4;
        if (e->type == EV_SLING) pin.sling_flash[e->id & 1] = 3;
        if (pin.demo) continue;
        switch (e->type) {
        case EV_BUMPER: a3_sfx(SFX_BUMPER); break;
        case EV_SLING: a3_sfx(SFX_SLING); break;
        case EV_DROP: a3_sfx(SFX_DROP); break;
        case EV_SPIN: a3_sfx(SFX_SPIN); break;
        case EV_SENSOR: if (e->kind == SN_LANE || e->kind == SN_IN) a3_sfx(SFX_LANE); break;
        case EV_SAUCER: a3_sfx(SFX_SAUCER); break;
        case EV_KICK: a3_sfx(SFX_KICK); break;
        case EV_PLUNGE: a3_sfx(SFX_PLUNGE); break;
        case EV_DRAIN: a3_sfx(SFX_DRAIN); break;
        case EV_FLIPSTOP: if (e->up) a3_sfx(SFX_FLIP); break;
        case EV_WALL: if (e->val > 900) a3_sfx(SFX_WALL); break;
        case EV_RAMP: a3_sfx(SFX_RAMP); break;
        case EV_RAMPFAIL: a3_sfx(SFX_FAIL); break;
        case EV_SKILL: case EV_LANESDONE: case EV_BANK: case EV_LOCK: case EV_SAVE: case EV_MISSIONSTART:
            a3_sfx(SFX_JINGLE); break;
        case EV_MULTIBALL: case EV_JACKPOT: case EV_MISSIONDONE: case EV_SUPERNOVA: a3_sfx(SFX_BIG); break;
        case EV_TILTWARN: case EV_TILT: case EV_MISSIONFAIL: a3_sfx(SFX_WARN); break;
        }
        switch (e->type) {                  /* the bridge log, for the tests and for watching a game */
        case EV_SKILL: case EV_LANESDONE: case EV_BANK: case EV_LOCK: case EV_MULTIBALL: case EV_JACKPOT:
        case EV_MISSIONSTART: case EV_MISSIONDONE: case EV_MISSIONFAIL: case EV_SUPERNOVA: case EV_SAVE:
        case EV_ENDBALL: case EV_GAMEOVER: case EV_TILTWARN: case EV_TILT: case EV_RAMP: case EV_SAUCER: case EV_DRAIN:
            AB_I("EVENT %s score %ld ball %ld", pb_event_names[e->type], (long)game.score, (long)game.ball_no);
            break;
        }
    }
    game.nev = 0;
}

/* ---------- states ---------- */
static void choose_table(int which)
{
    if (!render_build(which)) AB_E("no memory for the table's picture");
    pb_game_init(&game, &table, seed);
}

static void start_demo(void)
{
    pin.demo = 1;
    pb_game_init(&game, &table, seed++);
    pb_game_start(&game);
    memset(&bot, 0, sizeof bot);
    bot.rng = seed * 2654435761u;
    render_follow(1);
    render_title_start();
}

static void to_title(void)
{
    pin.state = PS_TITLE;
    pin.paused = 0;
    start_demo();
}

static void start_game(int which)
{
    if (which != pin.table) choose_table(which);
    pin.demo = 0;
    bot_play = 0;
    pb_game_init(&game, &table, seed++);
    pb_game_start(&game);
    pin.state = PS_PLAY;
    pin.paused = 0;
    plunger_lock = 1;
    acc_us = 0;
    render_follow(1);
    AB_I("START %s", table.name);
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int table_arg(const char *a)
{
    if (!a || !a[0]) return pin.table;
    if (a[0] == 'd' || a[0] == '1') return TB_DEEP;
    if (a[0] == 'f' || a[0] == 'r' || a[0] == '2') return TB_FOUNDRY;
    return TB_COMET;
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[200], sc[16];
    static const char *const st[] = { "title", "play", "over" };
    static const char *const gs[] = { "attract", "play", "bonus", "over" };
    (void)args;
    pb_fmt(sc, game.score);
    sprintf(tmp, "%s %s/%s ball %ld score %s balls %ld locks %ld bonus %ldx mission %ld multiball %ld tilt %ld | fps %ld",
            table.name, st[pin.state], gs[game.state], (long)game.ball_no, sc, (long)game.w.nballs, (long)game.locks,
            (long)game.bonus_x, (long)game.mission, (long)game.multiball, (long)game.tilt_warnings, (long)v_fps);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    char tmp[64];
    start_game(table_arg(args));
    sprintf(tmp, "playing %s", table.name);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_table(const char *args, char *buf, int bufsz)
{
    choose_table(table_arg(args));
    to_title();
    reply(buf, bufsz, table.name);
    return 0;
}

/* place x y [vx vy]: the first ball (or a new one) at x, y mm, moving */
static int hook_place(const char *args, char *buf, int bufsz)
{
    long x = 239, y = 600, vx = 0, vy = 0;
    Ball *b;
    char tmp[64];
#ifndef AMIGA3DO                        /* the 3DO's libc has no sscanf (and no bridge hooks) */
    if (args) sscanf(args, "%ld %ld %ld %ld", &x, &y, &vx, &vy);
#endif
    b = game.w.nballs ? &game.w.balls[0] : pb_add_ball(&game.w, 0, 0, 0, 0, BS_PLAY);
    if (!b) { reply(buf, bufsz, "no ball"); return 1; }
    b->state = BS_PLAY; b->z = 0; b->autol = -1;
    b->x = FROMI(x); b->y = FROMI(y); b->vx = FROMI(vx); b->vy = FROMI(vy);
    sprintf(tmp, "ball at %ld,%ld moving %ld,%ld", x, y, vx, vy);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_launch(const char *args, char *buf, int bufsz)
{
    pending_pull = args && args[0] ? atoi(args) : 900;
    reply(buf, bufsz, "launching");
    return 0;
}

static int hook_flip(const char *args, char *buf, int bufsz)
{
    int side = args && (args[0] == 'r' || args[0] == 'R') ? 1 : 0, ms = 300;
    const char *sp = args ? strchr(args, ' ') : NULL;
    if (sp) ms = atoi(sp + 1);
    flip_hold[side] = (ms + 39) / 40;
    reply(buf, bufsz, side ? "right flippers up" : "left flippers up");
    return 0;
}

static int hook_nudge(const char *args, char *buf, int bufsz)
{
    char c = args && args[0] ? args[0] : 'u';
    pending_nudge_x = c == 'l' ? -1 : c == 'r' ? 1 : 0;
    pending_nudge_y = c == 'u' ? 1 : 0;
    reply(buf, bufsz, "nudged");
    return 0;
}

/* event <name>: feed the rules an event as if the table had made it */
static int hook_event(const char *args, char *buf, int bufsz)
{
    Event e;
    int i;
    memset(&e, 0, sizeof e);
    e.type = -1;
    for (i = 0; i < EV_COUNT; i++) if (args && !strcmp(args, pb_event_names[i])) e.type = (int8_t)i;
    if (args && !strncmp(args, "lane", 4) && args[4] >= 'A' && args[4] <= 'C') {
        e.type = EV_SENSOR; e.kind = SN_LANE; e.id = (int8_t)(args[4] - 'A');
    }
    if (e.type < 0) { reply(buf, bufsz, "unknown event"); return 1; }
    e.ball = game.w.nballs ? (int16_t)game.w.balls[0].id : -1;
    pb_game_handle(&game, &e);
    reply(buf, bufsz, "handled");
    return 0;
}

static int hook_bot(const char *args, char *buf, int bufsz)
{
    bot_play = !(args && (args[0] == '0' || !strcmp(args, "off")));
    if (bot_play) { memset(&bot, 0, sizeof bot); bot.rng = 99; }
    reply(buf, bufsz, bot_play ? "the bot is playing" : "you are playing");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    pin.help = args && args[0] ? atoi(args) : 1;
    if (pin.help < 0 || pin.help > 2) pin.help = 0;
    reply(buf, bufsz, pin.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    pin.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, pin.paused ? "paused" : "running");
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
    ab_register_var("ball",       AB_TYPE_I32, &v_ball);
    ab_register_var("balls_left", AB_TYPE_I32, &v_left);
    ab_register_var("state",      AB_TYPE_I32, &pin.state);
    ab_register_var("pstate",     AB_TYPE_I32, &v_pstate);
    ab_register_var("table",      AB_TYPE_I32, &pin.table);
    ab_register_var("multiball",  AB_TYPE_I32, &v_mb);
    ab_register_var("mission",    AB_TYPE_I32, &v_mission);
    ab_register_var("locks",      AB_TYPE_I32, &v_locks);
    ab_register_var("bonus_x",    AB_TYPE_I32, &v_bonusx);
    ab_register_var("tilt_warn",  AB_TYPE_I32, &v_tw);
    ab_register_var("tilted",     AB_TYPE_I32, &v_tilted);
    ab_register_var("ball_x",     AB_TYPE_I32, &v_bx);
    ab_register_var("ball_y",     AB_TYPE_I32, &v_by);
    ab_register_var("ball_vx",    AB_TYPE_I32, &v_bvx);
    ab_register_var("ball_vy",    AB_TYPE_I32, &v_bvy);
    ab_register_var("ball_st",    AB_TYPE_I32, &v_bst);
    ab_register_var("nballs",     AB_TYPE_I32, &v_nballs);
    ab_register_var("flip_l",     AB_TYPE_I32, &v_flip_l);
    ab_register_var("flip_r",     AB_TYPE_I32, &v_flip_r);
    ab_register_var("cam",        AB_TYPE_I32, &pin.cam);
    ab_register_var("paused",     AB_TYPE_I32, &pin.paused);
    ab_register_var("help",       AB_TYPE_I32, &pin.help);
    ab_register_var("fps",        AB_TYPE_I32, &v_fps);
    ab_register_var("substeps",   AB_TYPE_I32, &v_sub);
    ab_register_var("us_update",  AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",    AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line game status", hook_status);
    ab_register_hook("start",    "start [comet/deep/foundry]: a game on that table", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("table",    "table comet/deep/foundry: choose it at the title", hook_table);
    ab_register_hook("place",    "place X Y [VX VY]: put the ball there (mm; mm/s)", hook_place);
    ab_register_hook("launch",   "launch [0-1000]: fire the plunger that hard", hook_launch);
    ab_register_hook("flip",     "flip l/r [ms]: hold the flippers up", hook_flip);
    ab_register_hook("nudge",    "nudge l/r/u", hook_nudge);
    ab_register_hook("event",    "event NAME: ramp/bumper/saucer/spin/drop/laneA... as if hit", hook_event);
    ab_register_hook("bot",      "bot on/off: the demo player plays your game", hook_bot);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    register_bridge();
    choose_table(TB_COMET);
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;
    held = in;

    if (edge & IN_HELP) { pin.help = (pin.help + 1) % 3; return 1; }
    if (pin.help) {
        if (edge & (IN_PLUNGE | IN_START | IN_ESC)) pin.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (pin.state == PS_TITLE) running = 0;
        else to_title();
        return running;
    }
    switch (pin.state) {
    case PS_TITLE:
        if (edge & (IN_NL | IN_LFLIP)) { choose_table((pin.table + TB_COUNT - 1) % TB_COUNT); start_demo(); }
        if (edge & (IN_NR | IN_RFLIP)) { choose_table((pin.table + 1) % TB_COUNT); start_demo(); }
        if (edge & (IN_PLUNGE | IN_START)) start_game(pin.table);
        break;
    case PS_PLAY:
        if (edge & IN_PAUSE) pin.paused = !pin.paused;
        if (pin.paused) break;
        if (edge & IN_NL) pending_nudge_x = -1;
        if (edge & IN_NR) pending_nudge_x = 1;
        if (edge & IN_NU) pending_nudge_y = 1;
        if (edge & IN_BOT) bot_play = !bot_play;
        break;
    case PS_OVER:
        if (pin.state_timer == 0 && (edge & (IN_PLUNGE | IN_START))) to_title();
        break;
    }
    return running;
}

void game_update(unsigned in)
{
    int32_t now = plat_ticks();
    uint32_t t0 = plat_clock();
    const int32_t step_us = 1000000 / PB_HZ;
    PbInput pi;
    int i, steps = 0;
    (void)in;
    acc_us += (now - t_last) * 20000;
    t_last = now;
    if (acc_us > 100000) acc_us = 100000;           /* at most 0.1 s behind */
    if (pin.help || pin.paused) acc_us = 0;
    while (acc_us >= step_us) {
        acc_us -= step_us;
        if (pin.demo || bot_play) pb_bot(&game, &pi, &bot);
        else {
            memset(&pi, 0, sizeof pi);
            pi.left = (held & IN_LFLIP) != 0;
            pi.right = (held & IN_RFLIP) != 0;
            if (!(held & IN_PLUNGE)) plunger_lock = 0;
            pi.plunger = (held & IN_PLUNGE) && !plunger_lock;
            pi.pull_to = -1;
        }
        if (flip_hold[0] > 0) pi.left = 1;
        if (flip_hold[1] > 0) pi.right = 1;
        if (pending_pull >= 0) { pi.pull_to = pending_pull; pending_pull = -2; }
        else if (pending_pull == -2) { pi.pull_to = -1; pi.plunger = 0; pending_pull = -1; }   /* the next step lets go */
        if (!steps) { pi.nudge_x = pending_nudge_x; pi.nudge_y = pending_nudge_y; pending_nudge_x = pending_nudge_y = 0; }
        pb_game_tick(&game, &pi);
        steps++;
        handle_events();
        if (pin.demo && game.state == GS_OVER) start_demo();
    }
    v_sub = game.w.substeps;
    for (i = 0; i < 2; i++) if (flip_hold[i] > 0) flip_hold[i]--;
    for (i = 0; i < PB_MAX_CIRC; i++) if (pin.bumper_flash[i] > 0) pin.bumper_flash[i]--;
    for (i = 0; i < 2; i++) if (pin.sling_flash[i] > 0) pin.sling_flash[i]--;
    if (pin.toast_timer > 0) pin.toast_timer--;
    if (pin.state_timer > 0) pin.state_timer--;
    if (pin.state == PS_PLAY && game.state == GS_OVER) {
        pin.state = PS_OVER;
        pin.state_timer = 40;
        if (game.score > pin.best[pin.table]) pin.best[pin.table] = game.score;
        AB_I("GAMEOVER %s score %ld", table.name, (long)game.score);
    }
    if (!pin.help && !pin.paused) render_follow(0);
    us_update = plat_us_since(t0);
    if (pin.state != last_state) {
        static const char *const st[] = { "TITLE", "PLAY", "OVER" };
        AB_I("STATE %s %s score %ld", st[pin.state], table.name, (long)game.score);
        last_state = pin.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    pin.frame++;
    if (pin.help) render_help(cv, pin.help);
    else if (pin.state == PS_TITLE) render_title(cv);
    else render_game(cv);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    const Ball *b = game.w.nballs ? &game.w.balls[0] : NULL;
    v_ball = game.ball_no;
    v_left = game.balls_per_game + game.extra_balls - game.ball_no;
    v_pstate = game.state;
    v_mb = game.multiball; v_mission = game.mission; v_locks = game.locks; v_bonusx = game.bonus_x;
    v_tw = game.tilt_warnings; v_tilted = game.tilted;
    v_nballs = game.w.nballs;
    v_bx = b ? FINT(b->x) : 0; v_by = b ? FINT(b->y) : 0;
    v_bvx = b ? FINT(b->vx) : 0; v_bvy = b ? FINT(b->vy) : 0;
    v_bst = b ? b->state : -1;
    /* the lower flippers: degrees up from rest */
    {
        const Flipper *f = &game.w.flip[0], *g2 = &game.w.flip[1];
        v_flip_l = (int32_t)((f->ang - f->d.rest) * 180 / 205887);
        v_flip_r = (int32_t)((g2->d.rest - g2->ang) * 180 / 205887);
    }
    frames++;
    if (now - t_fps >= 100) {
        v_fps = (frames * 50L) / (now - t_fps);
        frames = 0;
        t_fps = now;
    }
    ab_poll();
    if (selftest_pending) {
        int32_t t0 = plat_ticks();
        int was = pin.table;
        selftest_pending = 0;
        test_pass = test_fail = 0;
        ab_test_begin("boing_pinball_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        /* the tests built their own tables; put ours back */
        choose_table(was);
        to_title();
        t_last = plat_ticks();
    }
    (void)say;
}
