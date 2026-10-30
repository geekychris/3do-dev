// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Canyon Rail's shell on the shared engine: input, the title
 * with the autopilot riding behind it, play, game over, the help pages,
 * sound and explosions for the events, and everything exposed over the
 * AmigaBridge so the game can be driven and tested from the host:
 *
 *   vars   score stage shield combo shots hits kills hurt boss nodes ride
 *          sight_x sight_y state over won autoplay start_stage view_ahead
 *          paused help fps polys us_update us_draw (26: the bridge takes 32)
 *   hooks  status, start [stage], title, autoplay <on|off>, guardian (on
 *          to the ride's end), aim (the sight onto the nearest target),
 *          shield <n>, help <0-2>, pause <on|off>, selftest, activate
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "cr.h"
#include "rail.h"

const char game_title[] = "Canyon Rail";
const char game_client[] = "canyon_rail";
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

enum { SFX_SHOT, SFX_BOOM, SFX_HIT, SFX_BOLT, SFX_EFIRE, SFX_HURT, SFX_POD, SFX_BOSS, SFX_CLEAR, SFX_OVER, SFX_WIN };

Rail rl;
CRGame game;
static unsigned prev_in, play_in;
static int fire_lock;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw, v_nodes;
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
    if (n > sizeof(rl.flash) - 1) n = sizeof(rl.flash) - 1;
    memcpy(rl.flash, s, n);
    rl.flash[n] = 0;
    rl.flash_timer = 30;
    rl.flash_pen = pen;
}

static void add_burst(int32_t x, int32_t y, int32_t z, int size, int pen)
{
    int k;
    for (k = 0; k < MAX_BURSTS; k++)
        if (rl.burst[k].t <= 0) {
            rl.burst[k].t = 10; rl.burst[k].x = x; rl.burst[k].y = y; rl.burst[k].z = z;
            rl.burst[k].size = size; rl.burst[k].pen = pen;
            return;
        }
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_SHOT, 500, 36, 10))) a3_tone(d, 500, 1800, 700, 80, 400);
    if ((d = a3_sfx_define(SFX_BOOM, 3000, 60, 40))) a3_noise(d, 3000, 127, 8, 2);
    if ((d = a3_sfx_define(SFX_HIT, 700, 50, 25))) a3_tone(d, 700, 1200, 900, 90, 500);
    if ((d = a3_sfx_define(SFX_BOLT, 900, 50, 25))) { a3_noise(d, 400, 110, 2, 2); a3_tone(d + 400, 500, 1600, 2400, 80, 400); }
    if ((d = a3_sfx_define(SFX_EFIRE, 900, 34, 12))) a3_tone(d, 900, 300, 700, 70, 600);
    if ((d = a3_sfx_define(SFX_HURT, 2400, 60, 50))) { a3_noise(d, 1200, 127, 4, 2); a3_tone(d + 1200, 1200, 160, 90, 110, 1000); }
    if ((d = a3_sfx_define(SFX_POD, 1600, 50, 30))) { a3_tone(d, 800, 784, 784, 90, 100); a3_tone(d + 800, 800, 1175, 1175, 90, 600); }
    if ((d = a3_sfx_define(SFX_BOSS, 4000, 60, 50))) { a3_tone(d, 2000, 110, 110, 110, 100); a3_tone(d + 2000, 2000, 98, 98, 110, 1500); }
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

static void handle_events(void)
{
    CREvent e;
    int quiet = rl.state == RS_TITLE;
    int32_t x, y, z;
    while (cr_pop_event(&game, &e)) {
        switch (e.type) {                    /* what is seen happens in the demo too */
        case CRE_SHOT: rl.tracer_timer = 2; break;
        case CRE_KILL: case CRE_POD: cr_target_pos(&game, e.a, &x, &y, &z); add_burst(x, y, z, 30, 23); break;
        case CRE_HIT: if (game.boss) cr_node_pos(&game, e.a, &x, &y, &z); else cr_target_pos(&game, e.a, &x, &y, &z); add_burst(x, y, z, 12, 27); break;
        case CRE_BOLT_DOWN: cr_bolt_pos(&game, e.a, &x, &y, &z); add_burst(x, y, z, 14, 27); break;
        case CRE_NODE: cr_node_pos(&game, e.a, &x, &y, &z); add_burst(x, y, z, 50, 23); break;
        case CRE_HURT: if (!quiet) rl.hurt_timer = 6; break;
        }
        if (quiet) continue;
        switch (e.type) {
        case CRE_SHOT: a3_sfx(SFX_SHOT); break;
        case CRE_KILL: case CRE_NODE: a3_sfx(SFX_BOOM); break;
        case CRE_HIT: a3_sfx(SFX_HIT); break;
        case CRE_BOLT_DOWN: a3_sfx(SFX_BOLT); break;
        case CRE_FIRE: a3_sfx(SFX_EFIRE); break;
        case CRE_HURT: a3_sfx(SFX_HURT); break;
        case CRE_POD: a3_sfx(SFX_POD); flash("SHIELD +25", 28); break;
        case CRE_BOSS: a3_sfx(SFX_BOSS); break;
        case CRE_BOSS_DOWN: a3_sfx(SFX_CLEAR); break;
        case CRE_OVER: a3_sfx(SFX_OVER); break;
        case CRE_WIN: a3_sfx(SFX_WIN); break;
        }
        if (e.type == CRE_BOSS || e.type == CRE_BOSS_DOWN || e.type == CRE_STAGE || e.type == CRE_OVER || e.type == CRE_WIN)
            AB_I("EV %s a=%ld b=%ld score=%ld stage=%ld shield=%ld", cr_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.stage, (long)game.shield);
    }
}

/* ---------- game flow ---------- */
static void start_game(int stage)
{
    cr_new_game(&game, next_seed++, stage);
    rl.state = RS_PLAY;
    rl.paused = 0;
    rl.help = 0;
    rl.autoplay = 0;
    fire_lock = 1;
    memset(rl.burst, 0, sizeof(rl.burst));
    AB_I("START stage %ld seed %ld", (long)stage, (long)game.seed);
}

static void to_title(void)
{
    rl.state = RS_TITLE;
    cr_new_game(&game, 777 + next_seed, 1);
    memset(rl.burst, 0, sizeof(rl.burst));
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[120];
    static const char *const st[] = { "title", "ride", "over" };
    (void)args;
    sprintf(tmp, "%s S%ld ride %ld%s | shield %ld score %ld combo %ld | %ld/%ld hits kills %ld | sight %ld,%ld%s%s",
            st[rl.state], (long)game.stage, (long)game.cam_s, game.boss ? " GUARDIAN" : "", (long)game.shield,
            (long)game.score, (long)game.combo, (long)game.hits, (long)game.shots, (long)game.kills, (long)game.sx,
            (long)game.sy, game.over ? " over" : "", game.won ? " won" : "");
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int stage = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (stage < 1) stage = 1;
    if (stage > CR_STAGES) stage = CR_STAGES;
    start_game(stage);
    sprintf(tmp, "stage %ld, %ld targets, seed %ld", (long)stage, (long)game.ntg, (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    rl.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, rl.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

static int hook_guardian(const char *args, char *buf, int bufsz)
{
    int k;
    (void)args;
    for (k = 0; k < game.ntg; k++) game.tg[k].alive = 0;
    game.cam_s = CR_LEN - CR_SPEED;
    reply(buf, bufsz, "on to the guardian");
    return 0;
}

/* the sight onto the nearest turret in view (they hold still; a drone
 * swoops off the sight before a key from the host arrives), else the
 * nearest target of any kind */
static int hook_aim(const char *args, char *buf, int bufsz)
{
    int k, pass, best = -1, px, py;
    int32_t x, y, z, d, bd;
    char tmp[48];
    (void)args;
    for (pass = 0; pass < 2 && best < 0; pass++) {
        bd = 0x7FFFFFFF;
        for (k = 0; k < game.ntg; k++) {
            if (!game.tg[k].alive || game.tg[k].s - game.cam_s < 60) continue;
            if (pass == 0 && game.tg[k].type != TG_TURRET) continue;
            cr_target_pos(&game, k, &x, &y, &z);
            if (cr_project(&game, x, y, z, &px, &py, &d) && d < bd && px > 8 && px < CR_VIEW_W - 8 && py > 8 && py < CR_VIEW_H - 8) {
                bd = d; best = k; game.sx = px; game.sy = py;
            }
        }
    }
    sprintf(tmp, best < 0 ? "nothing in view" : "on target %ld at %ld,%ld", (long)best, (long)game.sx, (long)game.sy);
    reply(buf, bufsz, tmp);
    return best < 0;
}

static int hook_shield(const char *args, char *buf, int bufsz)
{
    game.shield = args && args[0] ? atoi(args) : CR_SHIELD;
    reply(buf, bufsz, "shield set");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    rl.help = args && args[0] ? atoi(args) : 1;
    if (rl.help < 0 || rl.help > 2) rl.help = 0;
    reply(buf, bufsz, rl.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    rl.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, rl.paused ? "paused" : "running");
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
    ab_register_var("stage",       AB_TYPE_I32, &game.stage);
    ab_register_var("shield",      AB_TYPE_I32, &game.shield);
    ab_register_var("combo",       AB_TYPE_I32, &game.combo);
    ab_register_var("shots",       AB_TYPE_I32, &game.shots);
    ab_register_var("hits",        AB_TYPE_I32, &game.hits);
    ab_register_var("kills",       AB_TYPE_I32, &game.kills);
    ab_register_var("hurt",        AB_TYPE_I32, &game.hurt);
    ab_register_var("boss",        AB_TYPE_I32, &game.boss);
    ab_register_var("nodes",       AB_TYPE_I32, &v_nodes);
    ab_register_var("ride",        AB_TYPE_I32, &game.cam_s);
    ab_register_var("sight_x",     AB_TYPE_I32, &game.sx);
    ab_register_var("sight_y",     AB_TYPE_I32, &game.sy);
    ab_register_var("state",       AB_TYPE_I32, &rl.state);
    ab_register_var("over",        AB_TYPE_I32, &game.over);
    ab_register_var("won",         AB_TYPE_I32, &game.won);
    ab_register_var("autoplay",    AB_TYPE_I32, &rl.autoplay);
    ab_register_var("start_stage", AB_TYPE_I32, &rl.start_stage);
    ab_register_var("view_ahead",  AB_TYPE_I32, &view_ahead);
    ab_register_var("paused",      AB_TYPE_I32, &rl.paused);
    ab_register_var("help",        AB_TYPE_I32, &rl.help);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",   AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line status", hook_status);
    ab_register_hook("start",    "start [stage 1-3]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot shoots", hook_autoplay);
    ab_register_hook("guardian", "On to the ride's end and the guardian", hook_guardian);
    ab_register_hook("aim",      "The sight onto the nearest turret in view (else any target)", hook_aim);
    ab_register_hook("shield",   "shield N", hook_shield);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(rail_palette);
    plat_sky(4, 0x58C, 0xFCA, 130);
    render_init();
    rl.start_stage = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { rl.help = (rl.help + 1) % 3; return 1; }
    if (rl.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) rl.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (rl.state == RS_TITLE) running = 0;
        else to_title();
        return running;
    }
    play_in = 0;
    switch (rl.state) {
    case RS_TITLE:
        if ((edge & IN_UP) && rl.start_stage < CR_STAGES) rl.start_stage++;
        if ((edge & IN_DOWN) && rl.start_stage > 1) rl.start_stage--;
        if (edge & (IN_FIRE | IN_START)) start_game(rl.start_stage);
        break;
    case RS_PLAY:
        if (edge & IN_PAUSE) rl.paused = !rl.paused;
        if (edge & IN_AUTO) rl.autoplay = !rl.autoplay;
        play_in = (in & IN_LEFT ? CR_IN_LEFT : 0) | (in & IN_RIGHT ? CR_IN_RIGHT : 0) | (in & IN_UP ? CR_IN_UP : 0) |
                  (in & IN_DOWN ? CR_IN_DOWN : 0) | ((in & IN_FIRE) && !fire_lock ? CR_IN_FIRE : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    case RS_OVER:
        if (rl.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
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
        if (rl.help || rl.paused) continue;
        if (rl.state == RS_TITLE) {
            if (game.over || game.won || game.between == 1) cr_new_game(&game, game.seed + 1, 1 + (int)(game.seed % CR_STAGES));
            cr_tick(&game, cr_bot(&game));
        } else if (rl.state == RS_PLAY) {
            cr_tick(&game, rl.autoplay ? cr_bot(&game) : play_in);
        }
        handle_events();
        for (k = 0; k < MAX_BURSTS; k++) if (rl.burst[k].t > 0) rl.burst[k].t--;
        if (rl.flash_timer > 0) rl.flash_timer--;
        if (rl.state_timer > 0) rl.state_timer--;
        if (rl.hurt_timer > 0) rl.hurt_timer--;
        if (rl.tracer_timer > 0) rl.tracer_timer--;
        if (rl.state == RS_PLAY && (game.over || game.won)) {
            rl.state = RS_OVER;
            rl.state_timer = 40;
            if (game.score > rl.best) rl.best = game.score;
        }
    }
    us_update = plat_us_since(t0);
    if (rl.state != last_state) {
        static const char *const st[] = { "TITLE", "RIDE", "OVER" };
        AB_I("STATE %s score %ld", st[rl.state], (long)game.score);
        last_state = rl.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (rl.help) render_help(cv, rl.help);
    else if (rl.state == RS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    int k;
    v_polys = a3_npolys;
    for (v_nodes = 0, k = 0; k < CR_NODES; k++) if (game.node_hp[k] > 0) v_nodes++;
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
        ab_test_begin("canyon_rail_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
