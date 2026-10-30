// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - the frame loop's game side: input, the fixed 25 Hz simulation,
 * what to draw, sound and voice for the simulation's events, and
 * everything exposed over the AmigaBridge, so the game can be driven,
 * tuned and tested from the host (devbench MCP / REST):
 *
 *   vars   score cover mult ap ap_pct kmh seg lane_d route state view
 *          cstate saves needless takeovers hazard_next glare nseen fps polys
 *          us_build us_draw us_hud, in_keys in_joy (main.c), and the tunables
 *          god_mode ai_perfect traffic_pct speed_pct mult_secs aqua_kmh
 *          draw_dist
 *   hooks  status, start, route <n>, scene <hazard|finish>, ap <on|off>,
 *          view <chase|cockpit>, say <n>, spawn <car|truck|deer|debris>
 *          [segs], skip, title, pause <on|off>, selftest
 *   mem    route (the segments: Seg x nseg), plan (the autopilot's 16
 *          planned offsets), seen (what it has detected)
 */
#include <exec/types.h>
#include <proto/dos.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "sc.h"
#include "models.h"
#include "render.h"
#include "sound.h"
#include "game.h"

const char game_title[] = "ScAIry Fun";
const char game_client[] = "scairy_fun";
const char game_music[] = "PROGDIR:scairy.mod";
const char game_voice[] = "PROGDIR:scairy.voc";

World world;
static Cam cam;
int paused;

/* bridge mirrors and stage timings (microseconds) */
static int32_t us_update, us_build, us_draw, us_hud;
static int32_t v_ap, v_ap_pct, v_kmh, v_seg, v_lane_d, v_view, v_cstate, v_hazard_next;
static int32_t v_fps, v_polys, v_frame, v_glare, v_msg, v_nseen;

static int32_t t_last, t_fps, t_log;
static int frames, last_state = -1, pal_route = -1, pal_glare = -1, sky_route = -1, sky_view = -1;
static unsigned prev_in;

/* The rules: help_page 1..HELP_PAGES shows that page (the game is paused
 * meanwhile). The title also shows them in turn, between the autopilot
 * demos. */
static int help_page, help_auto;
static int32_t title_t0;
#define TITLE_DEMO_TICKS 600            /* 1/50 s */
#define TITLE_PAGE_TICKS 400
#define TITLE_ART_TICKS  400            /* the painted title (AmigaOS 4) */
static A3Image *title_img;              /* NULL on the classic Amiga */
static int title_art;                   /* showing it now */

/* ---------- input ---------- */
unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x4F: return IN_LEFT;
    case 0x4E: return IN_RIGHT;
    case 0x4C: return IN_UP;
    case 0x4D: return IN_DOWN;
    case 0x40: return IN_FIRE;     /* space */
    case 0x64: return IN_FIRE;     /* left alt */
    case 0x34: return IN_VIEW;     /* V */
    case 0x33: return IN_VIEW;     /* C */
    case 0x45: return IN_ESC;
    case 0x50: return IN_START;    /* F1 */
    case 0x44: return IN_START;    /* return */
    case 0x19: return IN_PAUSE;    /* P */
    case 0x5F: return IN_HELP;     /* HELP */
    case 0x25: return IN_HELP;     /* H */
    }
    return 0;
}

unsigned game_joy_bits(unsigned j)
{
    return (j & A3_JOY_LEFT ? IN_LEFT : 0) | (j & A3_JOY_RIGHT ? IN_RIGHT : 0) | (j & A3_JOY_UP ? IN_UP : 0) |
           (j & A3_JOY_DOWN ? IN_DOWN : 0) | (j & A3_JOY_FIRE ? IN_FIRE : 0);
}

/* ---------- sound: effects synthesised at start-up ---------- */
void game_sound_init(void)
{
    static const int len[SFX_COUNT] = { 2048, 4096, 512, 2400, 2400, 320, 3600, 3000, 6000, 600 };
    static const int pri[SFX_COUNT] = {   60,   60,  40,   30,   30,  30,   50,   50,   20,  10 };
    static const int vol[SFX_COUNT] = {   64,   64,  44,   40,   40,  48,   44,   48,   36,  30 };
    int k, i;
    for (k = 0; k < SFX_COUNT; k++) {
        int8_t *d = a3_sfx_define(k, len[k], vol[k], pri[k]);
        int n = len[k];
        if (!d) continue;
        switch (k) {
        case SFX_CRASH:  a3_noise(d, n, 127, 2, 8); break;
        case SFX_SPLASH: a3_noise(d, n, 127, 6, 600); break;
        case SFX_SCRAPE:
            a3_noise(d, n, 90, 1, 4);
            for (i = 0; i < n; i++) if ((i / 6) & 1) d[i] = (int8_t)(d[i] / 2);
            break;
        case SFX_CHIME_ON:  a3_tone(d, n / 2, 660, 660, 90, 200); a3_tone(d + n / 2, n / 2, 990, 990, 90, 900); break;
        case SFX_CHIME_OFF: a3_tone(d, n / 2, 990, 990, 90, 200); a3_tone(d + n / 2, n / 2, 660, 660, 90, 900); break;
        case SFX_CONE:   a3_tone(d, n, 220, 90, 110, n); break;
        case SFX_SAVE:
            a3_tone(d, 1200, 523, 523, 90, 150);
            a3_tone(d + 1200, 1200, 659, 659, 90, 150);
            a3_tone(d + 2400, 1200, 784, 784, 100, 900);
            break;
        case SFX_ALERT:  a3_tone(d, 900, 1200, 1200, 110, 60); a3_tone(d + 1400, 900, 1200, 1200, 110, 60); break;
        case SFX_SIREN:  a3_tone(d, n / 2, 620, 930, 100, 40); a3_tone(d + n / 2, n / 2, 930, 620, 100, 300); break;
        case SFX_BLIP:   a3_tone(d, n, 1500, 1800, 80, n); break;
        }
    }
}

/* ---------- the colours ---------- */
static uint16_t mix(uint16_t c, uint16_t to, int num, int den)
{
    int r = (c >> 8) & 15, g = (c >> 4) & 15, b = c & 15;
    r += ((((to >> 8) & 15) - r) * num) / den;
    g += ((((to >> 4) & 15) - g) * num) / den;
    b += (((to & 15) - b) * num) / den;
    return (uint16_t)((r << 8) | (g << 4) | b);
}

/* the route's colours; the low sun washes everything towards white */
static void sc_palette(int route, int glare)
{
    uint16_t pal[32];
    int i;
    for (i = 0; i < 32; i++) {
        pal[i] = route_palette[route][i];
        if (glare > 0 && i >= 2) pal[i] = mix(pal[i], 0xFFE, glare * 10, 16 * 16);
    }
    plat_palette(pal);
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static const char *const state_names[] = { "title", "intro", "play", "arrived", "update", "gameover", "win" };
static const char *const car_states[] = { "driving", "aquaplane", "splash", "roll" };

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[160];
    int h = world_hazard_ahead(&world, 24);
    (void)args;
    sprintf(tmp, "%s route %ld seg %ld/%ld | %ld km/h d %ld %s | ap %s x%ld %ld%% | score %ld cover %ld saves %ld needless %ld | next %s | fps %ld",
            state_names[world.state], (long)world.route + 1, (long)seg_of(world.car.z), (long)world.finish,
            (long)KMH(world.car.v), (long)(world.car.d >> FBITS), car_states[world.car.state],
            world.ai.on ? "on" : "off", (long)world.mult, (long)world_ap_pct(&world),
            (long)world.score, (long)world.cover, (long)world.saves, (long)world.needless,
            h >= 0 ? hz_name(world.haz[h].type) : "-", (long)v_fps);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    (void)args;
    world_start_game(&world);
    world.state = GS_PLAY;
    paused = 0;
    reply(buf, bufsz, "game started on route 1");
    return 0;
}

static int hook_route(const char *args, char *buf, int bufsz)
{
    char tmp[64];
    int r = args && args[0] ? atoi(args) - 1 : 0;
    if (r < 0 || r >= NUM_ROUTES) { reply(buf, bufsz, "route 1..4"); return 1; }
    if (world.state == GS_TITLE || world.state == GS_GAMEOVER || world.state == GS_WIN) {
        world.score = 0;
        world.cover = 100;
    }
    world_start_route(&world, r);
    world.state = GS_PLAY;
    paused = 0;
    sprintf(tmp, "route %ld: %s", (long)r + 1, route_info[r].name);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_scene(const char *args, char *buf, int bufsz)
{
    char tmp[96];
    const char *name = (args && args[0]) ? args : "flood";
    if (!world_scene(&world, name)) {
        sprintf(tmp, "no such scene: %s (flood ghost phantom cones glare siren debris deer cyclist tractor merge goats peds finish)", name);
        reply(buf, bufsz, tmp);
        return 1;
    }
    paused = 0;
    sprintf(tmp, "scene %s: route %ld seg %ld, autopilot on", name, (long)world.route + 1,
            (long)seg_of(world.car.z));
    AB_I("SCENE %s route %ld seg %ld", name, (long)world.route + 1, (long)seg_of(world.car.z));
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_ap(const char *args, char *buf, int bufsz)
{
    int on = !(args && (args[0] == '0' || !strcmp(args, "off")));
    world_set_ap(&world, on);
    reply(buf, bufsz, world.ai.on ? "autopilot on" : "autopilot off");
    return on != world.ai.on;
}

static int hook_view(const char *args, char *buf, int bufsz)
{
    if (args && args[0] == 'c' && args[1] == 'o') world.view = CAM_COCKPIT;
    else if (args && args[0] == 'c') world.view = CAM_CHASE;
    else world.view ^= 1;
    reply(buf, bufsz, world.view == CAM_COCKPIT ? "cockpit view" : "chase view");
    return 0;
}

static int hook_say(const char *args, char *buf, int bufsz)
{
    char tmp[64];
    int m = args && args[0] ? atoi(args) : MSG_WELCOME;
    if (m <= 0 || m >= MSG_COUNT) { reply(buf, bufsz, "say 1..19"); return 1; }
    world_say(&world, m);
    sprintf(tmp, "%s (%s)", msg_text[m], voice_clips() > m ? "spoken" : "no voice clip");
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_spawn(const char *args, char *buf, int bufsz)
{
    static const struct { const char *name; int type; } kinds[] = {
        { "car", OBJ_CAR }, { "truck", OBJ_TRUCK }, { "deer", OBJ_DEER }, { "debris", OBJ_DEBRIS },
        { "cyclist", OBJ_CYCLIST }, { "goat", OBJ_GOAT }, { "ped", OBJ_PED }, { "tractor", OBJ_TRACTOR }
    };
    char tmp[80];
    int type = OBJ_CAR, k, i, segs = 12;
    int32_t v = 0, d;
    for (k = 0; k < (int)(sizeof(kinds) / sizeof(kinds[0])); k++)
        if (args && !strncmp(args, kinds[k].name, strlen(kinds[k].name))) type = kinds[k].type;
    if (args) { const char *sp = strchr(args, ' '); if (sp) segs = atoi(sp + 1); }
    d = world.car.d >= 0 ? lane_centre(&world, lane_of(&world, world.car.d)) * (1 << FBITS) : world.car.d;
    if (type == OBJ_CAR || type == OBJ_TRUCK) v = FROM_KMH(40);
    if (type == OBJ_DEER || type == OBJ_GOAT || type == OBJ_PED) d = (road_right(&world) + 40) * (1 << FBITS);
    i = world_spawn(&world, type, world.car.z + ((int32_t)segs << (SEG_BITS + FBITS)), d, v, 0);
    sprintf(tmp, i < 0 ? "no free slot" : "spawned %s #%ld %ld segments ahead", obj_name(type), (long)i, (long)segs);
    AB_I("SPAWN %s", tmp);
    reply(buf, bufsz, tmp);
    return i < 0;
}

static int hook_skip(const char *args, char *buf, int bufsz)
{
    (void)args;
    world_scene(&world, "finish");
    reply(buf, bufsz, "12 segments before the destination");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    /* 2: paused without the PAUSED text, so a screenshot shows the game */
    paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;
    reply(buf, bufsz, paused ? "paused" : "running");
    return 0;
}

static int hook_help(const char *args, char *buf, int bufsz)
{
    char tmp[32];
    int p = args && args[0] ? atoi(args) : 1;
    if (p < 0 || p > HELP_PAGES) p = 0;
    help_page = p;
    help_auto = 0;
    sprintf(tmp, p ? "help page %ld" : "help closed", (long)p);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz)
{
    (void)args;
    world_init(&world);
    paused = 0;
    reply(buf, bufsz, "title");
    return 0;
}

static int test_pass, test_fail, selftest_pending;
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

/* The suite simulates whole routes - longer than devbench waits for a hook
 * reply - so the hook only queues it; the results stream back as TEST_
 * messages. */
static int hook_selftest(const char *args, char *buf, int bufsz)
{
    (void)args;
    selftest_pending = 1;
    reply(buf, bufsz, "self-test queued; results stream as TEST_ messages");
    return 0;
}

static void run_selftest(void)
{
    int32_t t0 = plat_ticks();
    test_pass = test_fail = 0;
    ab_test_begin("scairy_fun_selftest");
    sc_run_tests(bridge_check);
    ab_test_end();
    AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
         (long)((plat_ticks() - t0) * 20));
    t_last = plat_ticks();                  /* don't try to catch up */
}

static void register_bridge(void)
{
    ab_register_var("score",       AB_TYPE_I32, &world.score);
    ab_register_var("cover",       AB_TYPE_I32, &world.cover);
    ab_register_var("mult",        AB_TYPE_I32, &world.mult);
    ab_register_var("ap",          AB_TYPE_I32, &v_ap);
    ab_register_var("ap_pct",      AB_TYPE_I32, &v_ap_pct);
    ab_register_var("kmh",         AB_TYPE_I32, &v_kmh);
    ab_register_var("seg",         AB_TYPE_I32, &v_seg);
    ab_register_var("lane_d",      AB_TYPE_I32, &v_lane_d);
    ab_register_var("route",       AB_TYPE_I32, &world.route);
    ab_register_var("state",       AB_TYPE_I32, &world.state);
    ab_register_var("view",        AB_TYPE_I32, &v_view);
    ab_register_var("cstate",      AB_TYPE_I32, &v_cstate);
    ab_register_var("saves",       AB_TYPE_I32, &world.saves);
    ab_register_var("needless",    AB_TYPE_I32, &world.needless);
    ab_register_var("takeovers",   AB_TYPE_I32, &world.takeovers);
    ab_register_var("hazard_next", AB_TYPE_I32, &v_hazard_next);
    ab_register_var("glare",       AB_TYPE_I32, &v_glare);
    ab_register_var("nseen",       AB_TYPE_I32, &v_nseen);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("us_build",    AB_TYPE_I32, &us_build);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);
    ab_register_var("us_hud",      AB_TYPE_I32, &us_hud);
    ab_register_var("god_mode",    AB_TYPE_I32, &world.tune.god_mode);
    ab_register_var("ai_perfect",  AB_TYPE_I32, &world.tune.ai_perfect);
    ab_register_var("traffic_pct", AB_TYPE_I32, &world.tune.traffic_pct);
    ab_register_var("speed_pct",   AB_TYPE_I32, &world.tune.speed_pct);
    ab_register_var("mult_secs",   AB_TYPE_I32, &world.tune.mult_secs);
    ab_register_var("aqua_kmh",    AB_TYPE_I32, &world.tune.aqua_kmh);
    ab_register_var("draw_dist",   AB_TYPE_I32, &render_draw_dist);
    /* that's 30 of the bridge's 32 (AB_MAX_VARS); main.c adds in_keys, in_joy */

    ab_register_hook("status",   "One-line game status", hook_status);
    ab_register_hook("start",    "Start a new game on route 1", hook_start);
    ab_register_hook("route",    "route N: drive route 1 to 4", hook_route);
    ab_register_hook("scene",    "scene NAME: before a hazard (flood ghost phantom cones glare siren debris deer cyclist tractor merge goats peds finish)", hook_scene);
    ab_register_hook("ap",       "ap on/off: the autopilot", hook_ap);
    ab_register_hook("view",     "view chase/cockpit", hook_view);
    ab_register_hook("say",      "say N: the AI says message 1 to 19", hook_say);
    ab_register_hook("spawn",    "spawn KIND [segments]: car truck deer debris cyclist goat ped tractor", hook_spawn);
    ab_register_hook("skip",     "Jump to just before the destination", hook_skip);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("pause",    "pause on/off (one still frame for screenshots)", hook_pause);
    ab_register_hook("help",     "help N: show rules page 1 to 3 (0 closes)", hook_help);
    ab_register_hook("selftest", "Run the unit and gameplay tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);

    ab_register_memregion("route", world.seg, sizeof(world.seg), "Seg[560]: cx y dir side[2] flags closed ghost");
    ab_register_memregion("plan",  world.ai.plan, sizeof(world.ai.plan), "16 x LONG: planned offset (fine) per segment ahead");
    ab_register_memregion("seen",  world.ai.seen, sizeof(world.ai.seen), "12 x Seen: what the autopilot detects");
}

/* ---------- events -> sound, voice, log ---------- */
static void handle_events(void)
{
    Event e;
    while (world_pop_event(&world, &e)) {
        switch (e.type) {
        case EV_SAY:        voice_say(e.a); AB_I("SAY %s", msg_text[e.a]); continue;
        case EV_AP_ON:      sfx(SFX_CHIME_ON); break;
        case EV_AP_OFF:     sfx(SFX_CHIME_OFF); break;
        case EV_UNAVAILABLE:
        case EV_MULT_UP:    sfx(SFX_BLIP); break;
        case EV_SAVE:
        case EV_ARRIVED:    sfx(SFX_SAVE); break;
        case EV_SCRAPE:     sfx(SFX_SCRAPE); break;
        case EV_CONE:       sfx(SFX_CONE); break;
        case EV_DEBRIS: case EV_REAREND: case EV_CRASH: case EV_ROLL: case EV_HIT_ANIMAL:
                            sfx(SFX_CRASH); break;
        case EV_SPLASH:     sfx(SFX_SPLASH); break;
        case EV_PHANTOM:
        case EV_TICKET:     sfx(SFX_ALERT); break;
        }
        AB_I("EV %s a=%ld b=%ld seg=%ld score=%ld cover=%ld", ev_name[e.type], (long)e.a, (long)e.b,
             (long)seg_of(world.car.z), (long)world.score, (long)world.cover);
    }
}

/* ---------- the frame ---------- */
void game_init(void)
{
    if (!render_init()) AB_E("render_init failed");
    title_img = g_image_load("PROGDIR:scairy_title.img");
    if (title_img) AB_I("the painted title loaded");
    world_init(&world);
    register_bridge();
    t_fps = t_last = t_log = plat_ticks();
}

int game_input(unsigned in)
{
    int running = 1;
    unsigned edge = in & ~prev_in;
    if (edge & IN_HELP) {
        help_page = help_page < HELP_PAGES ? help_page + 1 : 0;
        help_auto = 0;
        AB_I("HELP page %ld", (long)help_page);
    }
    if (help_page && !help_auto && world.state != GS_TITLE && (edge & (IN_FIRE | IN_START | IN_ESC))) {
        help_page = 0;                      /* back to the game; this press does nothing else */
        prev_in = in;
        return 1;
    }
    if (edge & IN_PAUSE) { paused = !paused; AB_I(paused ? "PAUSED" : "RESUMED"); }
    if (edge & IN_VIEW) { world.view ^= 1; AB_I("VIEW %s", world.view ? "cockpit" : "chase"); }
    if (edge & IN_ESC) {
        if (world.state == GS_TITLE) running = 0;
        else { world_init(&world); AB_I("back to title"); }
    }
    prev_in = in;
    return running;
}

void game_update(unsigned in)
{
    int32_t now = plat_ticks();
    uint32_t t0;
    int steps;

    /* fixed 25 Hz simulation, whatever the frame rate */
    steps = (int)((now - t_last) / 2);
    if (steps > 0) t_last += steps * 2;
    if (steps > 4 || steps < 0) steps = 4;

    t0 = plat_clock();
    /* on the title, the rules in turn between autopilot demos */
    if (world.state == GS_TITLE) {
        int32_t art = title_img ? TITLE_ART_TICKS : 0, t = now - title_t0, cycle = art + TITLE_DEMO_TICKS + HELP_PAGES * TITLE_PAGE_TICKS;
        if (t < 0 || t >= cycle) { title_t0 = now; t = 0; }
        title_art = t < art;
        t -= art;
        if (t < 0) t = 0;
        if (!help_page || help_auto) {
            int p = t < TITLE_DEMO_TICKS ? 0 : 1 + (int)((t - TITLE_DEMO_TICKS) / TITLE_PAGE_TICKS);
            help_auto = p > 0;
            help_page = p;
        }
    } else {
        title_t0 = now;
        title_art = 0;
        if (help_auto) help_page = help_auto = 0;
    }

    while (steps-- > 0 && !paused && !(help_page && world.state != GS_TITLE)) {
        world_update(&world, in);
        handle_events();
        if (world.siren && (world.frame % 13) == 0) sfx(SFX_SIREN);
    }
    us_update = plat_us_since(t0);

    if (world.state != last_state) {
        AB_I("STATE %s route %ld score %ld", state_names[world.state], (long)world.route + 1, (long)world.score);
        last_state = world.state;
        render_hud_reset();                 /* the panel's layout differs on the title */
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0;
    if (world.route != pal_route || world.glare != pal_glare) {
        sc_palette(world.route, world.glare);
        pal_route = world.route;
        pal_glare = world.glare;
    }
    if (world.route != sky_route || world.view != sky_view) {
        plat_sky(PEN_SKY, route_sky[world.route][0], route_sky[world.route][1], render_horizon(world.view));
        sky_route = world.route;
        sky_view = world.view;
    }
    if (title_art && title_img && world.state == GS_TITLE && !help_page) {
        /* the painted title, the demo running behind it */
        g_image_draw(cv, title_img, 0, 0);
#ifdef AMIGA3DO
        g_text_centre(cv, SCREEN_H - 26, PEN_WHITE, "PRESS A TO START");
#else
        g_text_centre(cv, SCREEN_H - 26, PEN_WHITE, "PRESS FIRE TO START");
#endif
#ifdef AMIGA3DO
        g_text_centre(cv, SCREEN_H - 12, PEN_GREY, "C: HOW TO PLAY");
#else
        g_text_centre(cv, SCREEN_H - 12, PEN_GREY, "HELP OR H: HOW TO PLAY");
#endif
        render_hud_reset();
        return;
    }
    if (help_page) {
        t0 = plat_clock();
        render_help(cv, help_page);
        render_hud_reset();                 /* it covered the HUD */
        us_draw = plat_us_since(t0);
        us_build = us_hud = 0;
        return;
    }
    if (world.state == GS_UPDATE) {
        t0 = plat_clock();
        render_update_screen(cv, &world);
        render_hud_reset();                 /* it covered the HUD */
        us_draw = plat_us_since(t0);
        us_build = us_hud = 0;
        return;
    }
    t0 = plat_clock();
    camera_follow(&cam, &world);
    render_build(&world, &cam);
    us_build = plat_us_since(t0);
    t0 = plat_clock();
    render_draw(cv, &world, &cam);
    us_draw = plat_us_since(t0);
    t0 = plat_clock();
    render_overlay(cv, &world, &cam);
    if (world.state == GS_TITLE) render_title(cv, &world);
    render_hud(cv, buf, &world, (int)v_fps, paused == 1);
    us_hud = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    int h = world_hazard_ahead(&world, 24);
    v_ap = world.ai.on;
    v_ap_pct = world_ap_pct(&world);
    v_kmh = KMH(world.car.v);
    v_seg = seg_of(world.car.z);
    v_lane_d = world.car.d >> FBITS;
    v_view = world.view;
    v_cstate = world.car.state;
    v_hazard_next = h >= 0 ? world.haz[h].type : 0;
    v_glare = world.glare;
    v_msg = world.msg;
    v_nseen = world.ai.nseen;
    v_polys = render_stats.polys;
    v_frame = world.frame;
    frames++;
    if (now - t_fps >= 100) {
        v_fps = (frames * 50L) / (now - t_fps);
        frames = 0;
        t_fps = now;
    }
    if (now - t_log >= 500) {
        AB_D("fps=%ld polys=%ld segs=%ld models=%ld dropped=%ld build=%ld draw=%ld hud=%ld",
             (long)v_fps, (long)render_stats.polys, (long)render_stats.segs, (long)render_stats.models,
             (long)render_stats.dropped, (long)us_build, (long)us_draw, (long)us_hud);
        t_log = now;
    }
    ab_poll();
    if (selftest_pending) { selftest_pending = 0; run_selftest(); }
}
