// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Ace Pilot 2's shell on the shared engine: input, the title
 * with the autopilot flying behind it, the briefing, the mission, its
 * result, the campaign's end, the help pages, sound, smoke and explosions
 * for the events, and everything exposed over the AmigaBridge so the game
 * can be driven and tested from the host:
 *
 *   vars   score mission phase state lives kills gkills shots hits hp ammo
 *          heat jam x y z yaw pitch roll speed throttle stalled view
 *          autoplay start_mission paused help enemies fps polys us_update
 *          us_draw (32: the bridge takes 32 a client)
 *   hooks  status, start [mission], title, autoplay <on|off>, spawn <type>
 *          [dist], clear (the objectives done), hp <n>, seed <n>, help
 *          <0-2>, pause <on|off>, view <chase|cockpit>, next, selftest,
 *          activate
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "ap.h"
#include "ace.h"

const char game_title[] = "Ace Pilot 2";
const char game_client[] = "ace_pilot2";
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
#define IN_THR_UP 0x0400
#define IN_THR_DN 0x0800
#define IN_VIEW   0x1000
#define IN_INVERT 0x2000

#define PEN_FIRE_EV  30
#define PEN_RED_EV   26
#define PEN_GREEN_EV 28

enum { SFX_GUN, SFX_EGUN, SFX_HIT, SFX_BOOM, SFX_FLAK, SFX_HURT, SFX_JAM, SFX_STALL, SFX_MARK,
       SFX_DONE, SFX_FAIL, SFX_LAND, SFX_WIN };

Ace ac;
APGame game;
static unsigned prev_in, play_in;
static int fire_lock;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw, v_x, v_y, v_z, v_speed, v_enemies, v_hp, v_ammo, v_heat, v_jam,
               v_yaw, v_pitch, v_roll, v_throttle, v_stalled;
static int selftest_pending, test_pass, test_fail;

unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x4F: return IN_LEFT;                 /* cursor left */
    case 0x4E: return IN_RIGHT;                /* cursor right */
    case 0x4C: return IN_UP;                   /* cursor up */
    case 0x4D: return IN_DOWN;                 /* cursor down */
    case 0x40: return IN_FIRE;                 /* space */
    case 0x20: return IN_THR_UP;               /* A */
    case 0x31: return IN_THR_DN;               /* Z */
    case 0x34: case 0x33: return IN_VIEW;      /* V, C */
    case 0x17: return IN_INVERT;               /* I */
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
    if (n > sizeof(ac.flash) - 1) n = sizeof(ac.flash) - 1;
    memcpy(ac.flash, s, n);
    ac.flash[n] = 0;
    ac.flash_timer = 40;
    ac.flash_pen = pen;
}

static void blast(fix x, fix y, fix z, int size, int pen)
{
    int k;
    for (k = 0; k < MAX_BLASTS; k++)
        if (ac.blast[k].t <= 0) {
            ac.blast[k].t = 12;
            ac.blast[k].x = x >> 16;
            ac.blast[k].y = y >> 16;
            ac.blast[k].z = z >> 16;
            ac.blast[k].size = size;
            ac.blast[k].pen = pen;
            return;
        }
}

static void puff(int32_t x, int32_t y, int32_t z, int size)
{
    Puff *p = &ac.puff[ac.next_puff];
    ac.next_puff = (ac.next_puff + 1) % MAX_PUFFS;
    p->t = 40; p->x = x; p->y = y; p->z = z; p->size = size;
}

/* smoke from the damaged and the falling, and from what burns below */
static void smoke(void)
{
    int k;
    for (k = 0; k < MAX_PUFFS; k++) if (ac.puff[k].t > 0) ac.puff[k].t--;
    if (game.tick % 3) return;
    for (k = 0; k < game.np; k++) {
        const Plane *p = &game.p[k];
        if (p->state == PS_FALL || (p->state == PS_FLY && p->hp * 2 < p->maxhp))
            puff(p->x >> 16, p->y >> 16, p->z >> 16, p->state == PS_FALL ? 5 : 3);
    }
    if (game.tick % 12) return;
    for (k = 0; k < game.ngr; k++)
        if (!game.gr[k].alive && game.gr[k].type != GT_BALLOON) puff(game.gr[k].x >> 16, 8, game.gr[k].z >> 16, 7);
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_GUN, 700, 50, 20))) a3_noise(d, 700, 120, 2, 1);
    if ((d = a3_sfx_define(SFX_EGUN, 700, 34, 15))) a3_noise(d, 700, 90, 4, 1);
    if ((d = a3_sfx_define(SFX_HIT, 600, 46, 25))) a3_tone(d, 600, 1400, 900, 90, 500);
    if ((d = a3_sfx_define(SFX_BOOM, 5000, 62, 45))) a3_noise(d, 5000, 127, 12, 2);
    if ((d = a3_sfx_define(SFX_FLAK, 2600, 50, 30))) a3_noise(d, 2600, 120, 16, 4);
    if ((d = a3_sfx_define(SFX_HURT, 1600, 58, 40))) { a3_noise(d, 800, 127, 3, 2); a3_tone(d + 800, 800, 300, 160, 100, 700); }
    if ((d = a3_sfx_define(SFX_JAM, 1200, 50, 35))) { a3_tone(d, 400, 900, 900, 90, 100); a3_tone(d + 400, 800, 500, 400, 90, 700); }
    if ((d = a3_sfx_define(SFX_STALL, 2400, 48, 30))) a3_tone(d, 2400, 700, 650, 90, 400);
    if ((d = a3_sfx_define(SFX_MARK, 2400, 54, 35))) { a3_tone(d, 1200, 784, 784, 100, 80); a3_tone(d + 1200, 1200, 1046, 1046, 100, 900); }
    if ((d = a3_sfx_define(SFX_DONE, 4800, 60, 55))) {
        a3_tone(d, 1200, 523, 523, 100, 80);
        a3_tone(d + 1200, 1200, 659, 659, 100, 80);
        a3_tone(d + 2400, 1200, 784, 784, 100, 80);
        a3_tone(d + 3600, 1200, 1046, 1046, 110, 1000);
    }
    if ((d = a3_sfx_define(SFX_FAIL, 6000, 60, 60))) a3_tone(d, 6000, 330, 80, 110, 3000);
    if ((d = a3_sfx_define(SFX_LAND, 1800, 50, 30))) a3_noise(d, 1800, 80, 20, 200);
    if ((d = a3_sfx_define(SFX_WIN, 6000, 60, 60))) {
        a3_tone(d, 1500, 784, 784, 100, 80);
        a3_tone(d + 1500, 1500, 1046, 1046, 100, 80);
        a3_tone(d + 3000, 3000, 1568, 1568, 110, 2000);
    }
}

static void handle_events(void)
{
    APEvent e;
    int quiet = ac.state == AS_TITLE;
    char tmp[40];
    while (ap_pop_event(&game, &e)) {
        switch (e.type) {                    /* what is seen happens in the demo too */
        case APE_KILL: blast(e.x, e.y, e.z, 26, PEN_FIRE_EV); break;
        case APE_GKILL: blast(e.x, e.y, e.z, e.b == GT_BALLOON ? 40 : 30, PEN_FIRE_EV); break;
        case APE_CRASH: blast(e.x, e.y + FX(4), e.z, 34, PEN_FIRE_EV); break;
        case APE_HURT: if (!quiet) ac.hurt_timer = 5; break;
        }
        if (quiet) continue;
        switch (e.type) {
        case APE_FIRE: a3_sfx(SFX_GUN); break;
        case APE_HIT: a3_sfx(SFX_HIT); break;
        case APE_KILL:
            a3_sfx(SFX_BOOM);
            sprintf(tmp, "%.20s DOWN", e.b == PT_FALCON ? "THE GREY FALCON" : e.b == PT_HERON ? "A HERON" : ap_plane_names[e.b]);
            {
                char *c;
                for (c = tmp; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
            }
            flash(tmp, e.b == PT_HERON ? PEN_RED_EV : PEN_GREEN_EV);
            break;
        case APE_GKILL:
            a3_sfx(SFX_BOOM);
            sprintf(tmp, "%.20s DESTROYED", ap_ground_names[e.b]);
            {
                char *c;
                for (c = tmp; *c; c++) if (*c >= 'a' && *c <= 'z') *c = (char)(*c - 32);
            }
            flash(tmp, PEN_GREEN_EV);
            break;
        case APE_FLAK: a3_sfx(SFX_FLAK); break;
        case APE_HURT: a3_sfx(SFX_HURT); break;
        case APE_JAM: a3_sfx(SFX_JAM); break;              /* the warning line says so */
        case APE_UNJAM: flash("GUNS CLEARED", PEN_GREEN_EV); break;
        case APE_STALL: a3_sfx(SFX_STALL); break;
        case APE_WAYPOINT: a3_sfx(SFX_MARK); sprintf(tmp, "MARKER %ld OF %ld", (long)e.a, (long)e.b); flash(tmp, PEN_GREEN_EV); break;
        case APE_OBJECTIVE: a3_sfx(SFX_MARK); break;
        case APE_HOMEWARD: flash("WELL DONE: NOW HOME", PEN_GREEN_EV); break;
        case APE_DONE: a3_sfx(SFX_DONE); flash("MISSION COMPLETE", PEN_GREEN_EV); break;
        case APE_FAILED: a3_sfx(SFX_FAIL); flash(e.a == 1 ? "THE TRAIN GOT THROUGH" : e.a == 2 ? (game.mission == 5 ? "THE DEPOT IS LOST" : "THE HERONS ARE LOST") : "MISSION FAILED", PEN_RED_EV); break;
        case APE_DOWN: flash("YOU ARE HIT - GOING DOWN", PEN_RED_EV); break;
        case APE_CRASH: if (e.a == 0) a3_sfx(SFX_BOOM); break;
        case APE_LANDED: a3_sfx(SFX_LAND); flash("LANDED", PEN_GREEN_EV); break;
        case APE_REARMED: a3_sfx(SFX_MARK); flash("REARMED AND REPAIRED", PEN_GREEN_EV); break;
        case APE_BOMBS: a3_sfx(SFX_FLAK); flash("A BOMBER GOT THROUGH", PEN_RED_EV); break;
        case APE_ALLY_LOST: flash("A HERON IS DOWN", PEN_RED_EV); break;
        case APE_ARRIVE: if (game.mission == 3) flash(game.p[e.a].home ? "A HERON IS HOME" : "THE HERONS ARE OVER ORME", PEN_GREEN_EV); break;
        case APE_EDGE: flash("THE EDGE OF THE SECTOR", PEN_RED_EV); break;
        }
        if (e.type == APE_KILL || e.type == APE_GKILL || e.type == APE_DONE || e.type == APE_FAILED || e.type == APE_DOWN ||
            e.type == APE_WAYPOINT || e.type == APE_OBJECTIVE || e.type == APE_LANDED || e.type == APE_REARMED || e.type == APE_JAM)
            AB_I("EV %s a=%ld b=%ld score=%ld mission=%ld hp=%ld", ap_event_names[e.type], (long)e.a, (long)e.b,
                 (long)game.score, (long)game.mission, (long)game.p[0].hp);
    }
}

/* ---------- game flow ---------- */
static void mission_start(void)
{
    ac.m_score = game.score; ac.m_kills = game.kills; ac.m_gkills = game.gkills;
    ac.m_shots = game.shots; ac.m_hits = game.hits;
    ac.cam_yaw = game.p[0].yaw;
    memset(ac.blast, 0, sizeof(ac.blast));
    memset(ac.puff, 0, sizeof(ac.puff));
    fire_lock = 1;
}

static void brief(void)
{
    ac.state = AS_BRIEF;
    ac.state_timer = 15;
    mission_start();
}

static void start_game(int mission, int briefing)
{
    ap_new_game(&game, next_seed++, mission);
    ac.paused = 0;
    ac.help = 0;
    ac.autoplay = 0;
    if (briefing) brief();
    else { ac.state = AS_PLAY; mission_start(); }
    AB_I("START mission %ld seed %ld", (long)mission, (long)game.seed);
}

static void demo(void)
{
    static const uint8_t demo_missions[] = { 8, 2, 1, 7, 5 };
    ap_new_game(&game, 777 + next_seed, demo_missions[next_seed % sizeof(demo_missions)]);
    next_seed++;
    ac.cam_yaw = game.p[0].yaw;
    memset(ac.blast, 0, sizeof(ac.blast));
    memset(ac.puff, 0, sizeof(ac.puff));
}

static void to_title(void)
{
    ac.state = AS_TITLE;
    ac.autoplay = 0;
    demo();
}

/* the mission over: on to the next, or the same again, or the end */
static void next(void)
{
    if (ap_next(&game)) brief();
    else {
        ac.state = AS_OVER;
        ac.state_timer = 30;
        if (game.score > ac.best) ac.best = game.score;
        a3_sfx(game.won ? SFX_WIN : SFX_FAIL);
    }
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[220], obj[40];
    static const char *const st[] = { "title", "brief", "flying", "result", "over" };
    static const char *const ph[] = { "out", "home", "done", "failed" };
    const Plane *p = &game.p[0];
    int k, n;
    (void)args;
    ap_objective_text(&game, obj);
    n = sprintf(tmp, "%s m%ld %s (%.28s) hp %ld ammo %ld at %ld,%ld,%ld v%ld score %ld k%ld g%ld lives %ld |", st[ac.state],
                (long)game.mission, ph[game.phase], obj, (long)p->hp, (long)p->ammo, (long)(p->x >> 16), (long)(p->y >> 16),
                (long)(p->z >> 16), (long)((p->v * 25) >> 16), (long)game.score, (long)game.kills, (long)game.gkills, (long)game.lives);
    for (k = 1; k < game.np && n < 190; k++)
        if (game.p[k].state == PS_FLY) n += sprintf(tmp + n, " %c%ld", ap_plane_names[game.p[k].type][0], (long)game.p[k].hp);
    if (n < 200) sprintf(tmp + n, "%s%s", game.over ? " over" : "", game.won ? " won" : "");
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int m = args && args[0] ? atoi(args) : 1;
    char tmp[64];
    if (m < 1) m = 1;
    if (m > AP_MISSIONS) m = AP_MISSIONS;
    start_game(m, 0);
    sprintf(tmp, "mission %ld: %.32s, seed %ld", (long)m, ap_missions[m - 1].name, (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_autoplay(const char *args, char *buf, int bufsz)
{
    ac.autoplay = !(args && (args[0] == '0' || !strcmp(args, "off")));
    reply(buf, bufsz, ac.autoplay ? "autopilot on" : "autopilot off");
    return 0;
}

/* a practice target: every other aeroplane gone, then one of `type` dead
 * ahead at `dist`, flying straight on at your speed, its gun silent */
static int hook_spawn(const char *args, char *buf, int bufsz)
{
    int type = !args ? PT_WASP : args[0] == 'h' ? PT_HORNET : args[0] == 'o' ? PT_OX : args[0] == 'f' ? PT_FALCON : PT_WASP, e;
    const char *sp = args ? strchr(args, ' ') : NULL;
    int32_t d = sp ? atol(sp + 1) : 200;
    Plane *p = &game.p[0];
    int32_t cp = a3_cos(p->pitch);
    char tmp[48];
    game.np = 1;
    e = ap_add_plane(&game, type, SIDE_ENEMY, p->x + d * FMUL(cp, a3_sin(p->yaw)), p->y + d * a3_sin(p->pitch),
                     p->z + d * FMUL(cp, a3_cos(p->yaw)), p->yaw);
    if (e > 0) { game.p[e].held = 1; game.p[e].v = p->v; game.p[e].pitch = p->pitch; game.p[e].ai = AI_HELD; }
    sprintf(tmp, "%.20s at %ld", e > 0 ? ap_plane_names[type] : "no room", (long)d);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_clear(const char *args, char *buf, int bufsz)
{
    (void)args;
    game.done = game.need > 0 ? game.need : 1;
    if (game.need == 0) { game.need = 1; game.wp_next = game.nwp; }
    reply(buf, bufsz, "objectives done: fly home");
    return 0;
}

static int hook_hp(const char *args, char *buf, int bufsz)
{
    game.p[0].hp = args && args[0] ? atoi(args) : AP_HP;
    reply(buf, bufsz, "damage set");
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
    ac.help = args && args[0] ? atoi(args) : 1;
    if (ac.help < 0 || ac.help > 2) ac.help = 0;
    reply(buf, bufsz, ac.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    ac.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, ac.paused ? "paused" : "running");
    return 0;
}

static int hook_view(const char *args, char *buf, int bufsz)
{
    if (args && args[0]) ac.view = (args[0] == 'c' && args[1] == 'o') || args[0] == '1' ? VIEW_COCKPIT : VIEW_CHASE;
    else ac.view ^= 1;
    reply(buf, bufsz, ac.view ? "cockpit" : "chase");
    return 0;
}

/* from the briefing or a result, on (as fire does) */
static int hook_next(const char *args, char *buf, int bufsz)
{
    (void)args;
    if (ac.state == AS_BRIEF) { ac.state = AS_PLAY; mission_start(); }
    else if (ac.state == AS_RESULT) next();
    else if (ac.state == AS_OVER) to_title();
    reply(buf, bufsz, "on");
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
    ab_register_var("score",         AB_TYPE_I32, &game.score);
    ab_register_var("mission",       AB_TYPE_I32, &game.mission);
    ab_register_var("phase",         AB_TYPE_I32, &game.phase);
    ab_register_var("state",         AB_TYPE_I32, &ac.state);
    ab_register_var("lives",         AB_TYPE_I32, &game.lives);
    ab_register_var("kills",         AB_TYPE_I32, &game.kills);
    ab_register_var("gkills",        AB_TYPE_I32, &game.gkills);
    ab_register_var("shots",         AB_TYPE_I32, &game.shots);
    ab_register_var("hits",          AB_TYPE_I32, &game.hits);
    ab_register_var("hp",            AB_TYPE_I32, &v_hp);
    ab_register_var("ammo",          AB_TYPE_I32, &v_ammo);
    ab_register_var("heat",          AB_TYPE_I32, &v_heat);
    ab_register_var("jam",           AB_TYPE_I32, &v_jam);
    ab_register_var("x",             AB_TYPE_I32, &v_x);
    ab_register_var("y",             AB_TYPE_I32, &v_y);
    ab_register_var("z",             AB_TYPE_I32, &v_z);
    ab_register_var("yaw",           AB_TYPE_I32, &v_yaw);
    ab_register_var("pitch",         AB_TYPE_I32, &v_pitch);
    ab_register_var("roll",          AB_TYPE_I32, &v_roll);
    ab_register_var("speed",         AB_TYPE_I32, &v_speed);
    ab_register_var("throttle",      AB_TYPE_I32, &v_throttle);
    ab_register_var("stalled",       AB_TYPE_I32, &v_stalled);
    ab_register_var("view",          AB_TYPE_I32, &ac.view);
    ab_register_var("autoplay",      AB_TYPE_I32, &ac.autoplay);
    ab_register_var("start_mission", AB_TYPE_I32, &ac.start_mission);
    ab_register_var("paused",        AB_TYPE_I32, &ac.paused);
    ab_register_var("help",          AB_TYPE_I32, &ac.help);
    ab_register_var("enemies",       AB_TYPE_I32, &v_enemies);
    ab_register_var("fps",           AB_TYPE_I32, &v_fps);
    ab_register_var("polys",         AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",     AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",       AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line flight status", hook_status);
    ab_register_hook("start",    "start [mission 1-8]: straight into the air", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("autoplay", "autoplay on/off: the autopilot flies", hook_autoplay);
    ab_register_hook("spawn",    "spawn wasp/hornet/ox/falcon [DIST]: a lone practice target ahead, gun silent", hook_spawn);
    ab_register_hook("clear",    "The mission's objectives done (fly home)", hook_clear);
    ab_register_hook("hp",       "hp N: the aeroplane's hit points (30 whole)", hook_hp);
    ab_register_hook("seed",     "seed N: the next game's seed", hook_seed);
    ab_register_hook("help",     "help N: page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("view",     "view chase/cockpit (no argument: toggle)", hook_view);
    ab_register_hook("next",     "On from the briefing, a result or the end (as fire)", hook_next);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(ace_palette);
    plat_sky(4, 0x38B, 0xBDE, 150);
    render_init();
    ac.start_mission = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { ac.help = (ac.help + 1) % 3; return 1; }
    if (ac.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) ac.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (ac.state == AS_TITLE) running = 0;
        else to_title();
        return running;
    }
    if (edge & IN_INVERT) ac.invert = !ac.invert;
    if (edge & IN_VIEW) ac.view ^= 1;
    play_in = 0;
    switch (ac.state) {
    case AS_TITLE:
        if ((edge & IN_UP) && ac.start_mission < AP_MISSIONS) ac.start_mission++;
        if ((edge & IN_DOWN) && ac.start_mission > 1) ac.start_mission--;
        if (edge & (IN_FIRE | IN_START)) start_game(ac.start_mission, 1);
        break;
    case AS_BRIEF:
        if (ac.state_timer == 0 && (edge & (IN_FIRE | IN_START))) { ac.state = AS_PLAY; mission_start(); }
        break;
    case AS_PLAY: {
        /* stick forward (up) puts the nose down, unless inverted */
        unsigned up = ac.invert ? AP_IN_NOSEUP : AP_IN_NOSEDN, dn = ac.invert ? AP_IN_NOSEDN : AP_IN_NOSEUP;
        if (edge & IN_PAUSE) ac.paused = !ac.paused;
        if (edge & IN_AUTO) ac.autoplay = !ac.autoplay;
        play_in = (in & IN_LEFT ? AP_IN_LEFT : 0) | (in & IN_RIGHT ? AP_IN_RIGHT : 0) | (in & IN_UP ? up : 0) |
                  (in & IN_DOWN ? dn : 0) | ((in & IN_FIRE) && !fire_lock ? AP_IN_FIRE : 0) |
                  (in & IN_THR_UP ? AP_IN_THR_UP : 0) | (in & IN_THR_DN ? AP_IN_THR_DN : 0);
        if (!(in & IN_FIRE)) fire_lock = 0;
        break;
    }
    case AS_RESULT:
        if (ac.state_timer == 0 && (edge & (IN_FIRE | IN_START))) next();
        break;
    case AS_OVER:
        if (ac.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
        break;
    }
    return running;
}

static void ease_camera(void)
{
    int d = (int16_t)((game.p[0].yaw - ac.cam_yaw) & 0xFFFF);
    ac.cam_yaw = (ac.cam_yaw + d / 4) & 0xFFFF;
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
        if (ac.help || ac.paused) continue;
        if (ac.state_timer > 0) ac.state_timer--;
        if (ac.flash_timer > 0) ac.flash_timer--;
        if (ac.state == AS_TITLE) {
            if (game.over || game.won || (game.phase >= PH_DONE && game.end_t == 0) || game.mtick > 6000) demo();
            ap_tick(&game, ap_bot(&game));
        } else if (ac.state == AS_PLAY || ac.state == AS_RESULT) {
            ap_tick(&game, ac.state == AS_PLAY ? (ac.autoplay ? ap_bot(&game) : play_in) : 0);
            if (ac.state == AS_PLAY && game.phase >= PH_DONE && game.end_t == 0) {
                ac.state = AS_RESULT;
                ac.result = game.phase;
                ac.state_timer = 25;
                if (game.score > ac.best) ac.best = game.score;
            }
        } else continue;
        handle_events();
        smoke();
        ease_camera();
        for (k = 0; k < MAX_BLASTS; k++) if (ac.blast[k].t > 0) ac.blast[k].t--;
        if (ac.hurt_timer > 0) ac.hurt_timer--;
    }
    us_update = plat_us_since(t0);
    if (ac.state != last_state) {
        static const char *const st[] = { "TITLE", "BRIEF", "FLYING", "RESULT", "OVER" };
        AB_I("STATE %s mission %ld score %ld", st[ac.state], (long)game.mission, (long)game.score);
        last_state = ac.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (ac.help) render_help(cv, ac.help);
    else if (ac.state == AS_TITLE) render_title(cv, &game);
    else if (ac.state == AS_BRIEF) render_brief(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    const Plane *p = &game.p[0];
    v_polys = a3_npolys;
    v_x = p->x >> 16;
    v_y = p->y >> 16;
    v_z = p->z >> 16;
    v_yaw = p->yaw;
    v_pitch = p->pitch;
    v_roll = p->roll;
    v_speed = (p->v * 25) >> 16;            /* units a second */
    v_throttle = p->throttle;
    v_stalled = p->stalled;
    v_enemies = ap_enemies_left(&game);
    v_hp = p->hp;
    v_ammo = p->ammo;
    v_heat = p->heat;
    v_jam = p->jam;
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
        ab_test_begin("ace_pilot2_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        t_last = plat_ticks();
    }
}
