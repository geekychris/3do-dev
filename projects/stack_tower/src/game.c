// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * game.c - Stack Tower's shell on the shared engine: input (with the
 * turn auto-repeat), the title with a demo game, play, game over, the
 * help pages, sound for the events, and everything exposed over the
 * AmigaBridge so the game can be driven and tested from the host:
 *
 *   vars   score level lines state mode over won pieces col row ptype hold
 *          garbage view ghost combo time_ms start_level fps polys us_update
 *          us_draw (+ in_keys in_joy from the platform)
 *   hooks  status, start [mode] [level], title, move <n>, rotate <+1|-1>,
 *          drop, hold, garbage [gaps], seed <n>, help <0-2>, pause <on|off>,
 *          selftest, activate
 *   mem    board (16 x 24 bytes, bottom row first; 0 empty, 1-8 pieces, 9 rubble)
 */
#include <exec/types.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "bridge_client.h"
#include "a3d_plat.h"
#include "a3d_sound.h"
#include "st.h"
#include "stack.h"

const char game_title[] = "Stack Tower";
const char game_client[] = "stack_tower";
const char game_music[] = "PROGDIR:stack_tower.mod";
const char game_voice[] = "";

#define IN_LEFT   0x0001
#define IN_RIGHT  0x0002
#define IN_UP     0x0004               /* rotate clockwise */
#define IN_DOWN   0x0008               /* soft drop */
#define IN_FIRE   0x0010               /* hard drop / start */
#define IN_CCW    0x0020
#define IN_HOLD   0x0040
#define IN_GHOST  0x0080
#define IN_PAUSE  0x0100
#define IN_ESC    0x0200
#define IN_START  0x0400
#define IN_HELP   0x0800

enum { SFX_MOVE, SFX_ROTATE, SFX_DROP, SFX_LOCK, SFX_CLEAR, SFX_LEVEL, SFX_OVER, SFX_HOLD, SFX_GARBAGE, SFX_WIN };

Stack stack;
StGame game;
static unsigned prev_in;
static int das_dir, das_timer;
static uint32_t next_seed = 1;
static int32_t t_last, t_fps;
static int frames, last_state = -1;
static int32_t v_fps, v_polys, us_update, us_draw, v_col, v_row, v_ptype, v_view;
static int selftest_pending, test_pass, test_fail;

unsigned game_key_bit(unsigned code)
{
    switch (code) {
    case 0x4F: case 0x20: return IN_LEFT;      /* cursor left, A */
    case 0x4E: case 0x22: return IN_RIGHT;     /* cursor right, D */
    case 0x4C: case 0x32: return IN_UP;        /* cursor up, X */
    case 0x4D: return IN_DOWN;
    case 0x40: return IN_FIRE;                 /* space */
    case 0x31: return IN_CCW;                  /* Z */
    case 0x33: case 0x60: return IN_HOLD;      /* C, left shift */
    case 0x24: return IN_GHOST;                /* G */
    case 0x19: return IN_PAUSE;                /* P */
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
    strncpy(stack.flash, s, sizeof(stack.flash) - 1);
    stack.flash_timer = 40;
}

/* ---------- sound ---------- */
void game_sound_init(void)
{
    int8_t *d;
    if ((d = a3_sfx_define(SFX_MOVE, 160, 24, 10))) a3_tone(d, 160, 900, 900, 60, 120);
    if ((d = a3_sfx_define(SFX_ROTATE, 300, 30, 12))) a3_tone(d, 300, 700, 1100, 70, 200);
    if ((d = a3_sfx_define(SFX_DROP, 900, 50, 30))) { a3_noise(d, 900, 110, 4, 4); }
    if ((d = a3_sfx_define(SFX_LOCK, 400, 40, 20))) a3_tone(d, 400, 220, 140, 90, 380);
    if ((d = a3_sfx_define(SFX_CLEAR, 3000, 50, 40))) {
        a3_tone(d, 1000, 523, 523, 90, 120);
        a3_tone(d + 1000, 1000, 659, 659, 90, 120);
        a3_tone(d + 2000, 1000, 1046, 1046, 100, 800);
    }
    if ((d = a3_sfx_define(SFX_LEVEL, 3600, 50, 45))) {
        a3_tone(d, 1200, 392, 392, 90, 100);
        a3_tone(d + 1200, 1200, 523, 523, 90, 100);
        a3_tone(d + 2400, 1200, 784, 784, 100, 1000);
    }
    if ((d = a3_sfx_define(SFX_OVER, 6000, 60, 60))) a3_tone(d, 6000, 440, 110, 110, 3000);
    if ((d = a3_sfx_define(SFX_HOLD, 400, 30, 12))) a3_tone(d, 400, 1300, 700, 70, 300);
    if ((d = a3_sfx_define(SFX_GARBAGE, 1400, 50, 30))) a3_noise(d, 1400, 120, 8, 200);
    if ((d = a3_sfx_define(SFX_WIN, 4800, 60, 60))) {
        a3_tone(d, 1200, 523, 523, 100, 80);
        a3_tone(d + 1200, 1200, 659, 659, 100, 80);
        a3_tone(d + 2400, 1200, 784, 784, 100, 80);
        a3_tone(d + 3600, 1200, 1046, 1046, 110, 1000);
    }
}

static void handle_events(void)
{
    StEvent e;
    while (st_pop_event(&game, &e)) {
        if (stack.state == SS_TITLE) continue;              /* the demo is quiet */
        switch (e.type) {
        case STE_MOVE: a3_sfx(SFX_MOVE); break;
        case STE_ROTATE: case STE_KICK: a3_sfx(SFX_ROTATE); break;
        case STE_DROP: a3_sfx(SFX_DROP); break;
        case STE_LOCK: a3_sfx(SFX_LOCK); break;
        case STE_HOLD: a3_sfx(SFX_HOLD); break;
        case STE_GARBAGE: a3_sfx(SFX_GARBAGE); break;
        case STE_CLEAR:
            a3_sfx(SFX_CLEAR);
            {
                static const char *const names[] = { "", "RING!", "DOUBLE!", "TRIPLE!", "QUAD!", "PENTA!", "MEGA!" };
                flash(names[e.a < 7 ? e.a : 6]);
            }
            break;
        case STE_LEVEL: a3_sfx(SFX_LEVEL); flash("LEVEL UP"); break;
        case STE_OVER: a3_sfx(SFX_OVER); break;
        case STE_WIN: a3_sfx(SFX_WIN); break;
        }
        if (e.type != STE_MOVE && e.type != STE_ROTATE)
            AB_I("EV %s a=%ld b=%ld score=%ld lines=%ld level=%ld", st_event_names[e.type], (long)e.a,
                 (long)e.b, (long)game.score, (long)game.lines, (long)game.level);
    }
}

/* ---------- game flow ---------- */
static void start_game(int mode, int level)
{
    st_new_game(&game, mode, next_seed++, level, stack.view >> 8);
    stack.state = SS_PLAY;
    stack.paused = 0;
    stack.help = 0;
    das_dir = 0;
    AB_I("START %s level %ld seed %ld", st_mode_names[mode], (long)level, (long)game.seed);
}

static void to_title(void)
{
    stack.state = SS_TITLE;
    st_new_game(&game, MODE_MARATHON, 777 + next_seed, 1, 0);
    render_title_start();
}

/* the title's demo: a piece placed every few frames, more or less sensibly */
static void demo_step(void)
{
    static int t;
    if (++t < 6) return;
    t = 0;
    if (game.over) { st_new_game(&game, MODE_MARATHON, a3_hash((uint32_t)game.seed), 1, 0); return; }
    {
        int best_col = 0, best_rot = 0, best = 1 << 30, r, c;
        for (r = 0; r < 4; r++)
            for (c = 0; c < ST_COLS; c++) {
                StGame *g = &game;
                Piece p = g->piece;
                int cells[5][2], n, k, h = 0;
                p.rot = r;
                p.col = c;
                if (st_collides(g, &p)) continue;
                while (1) { p.row--; if (st_collides(g, &p)) { p.row++; break; } }
                n = st_cells_of(&p, cells);
                for (k = 0; k < n; k++) h += cells[k][1] * 4;
                h += (int)(a3_hash((uint32_t)(c * 4 + r + g->pieces * 64)) & 3);
                if (h < best) { best = h; best_col = c; best_rot = r; }
            }
        game.piece.rot = best_rot;
        game.piece.col = best_col;
        game.last_col = best_col;
        st_hard_drop(&game);
    }
}

/* ---------- bridge hooks ---------- */
static void reply(char *buf, int bufsz, const char *s)
{
    if (buf && bufsz > 0) { strncpy(buf, s, bufsz - 1); buf[bufsz - 1] = 0; }
}

static int hook_status(const char *args, char *buf, int bufsz)
{
    char tmp[160];
    static const char *const st[] = { "title", "play", "over" };
    (void)args;
    sprintf(tmp, "%s %s level %ld | score %ld rings %ld pieces %ld | piece %s col %ld row %ld | hold %ld | %s%s| fps %ld",
            st[stack.state], st_mode_names[game.mode], (long)game.level, (long)game.score, (long)game.lines,
            (long)game.pieces, st_piece_names[game.piece.type], (long)game.piece.col, (long)game.piece.row,
            (long)game.hold, game.over ? "over " : "", game.won ? "won " : "", (long)v_fps);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_start(const char *args, char *buf, int bufsz)
{
    int mode = MODE_MARATHON, level = 1;
    char tmp[64];
    if (args && args[0]) {
        if (!strncmp(args, "ris", 3)) mode = MODE_RISING;
        else if (!strncmp(args, "dig", 3)) mode = MODE_DIG;
        { const char *sp = strchr(args, ' '); if (sp) level = atoi(sp + 1); }
    }
    if (level < 1) level = 1;
    start_game(mode, level);
    sprintf(tmp, "%s from level %ld, seed %ld", st_mode_names[mode], (long)level, (long)game.seed);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_title(const char *args, char *buf, int bufsz) { (void)args; to_title(); reply(buf, bufsz, "title"); return 0; }

static int hook_move(const char *args, char *buf, int bufsz)
{
    int n = args && args[0] ? atoi(args) : 1, k, done = 0;
    char tmp[48];
    for (k = 0; k < (n < 0 ? -n : n); k++) done += st_move(&game, n < 0 ? -1 : 1);
    sprintf(tmp, "moved %ld of %ld, col %ld", (long)done, (long)(n < 0 ? -n : n), (long)game.piece.col);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_rotate(const char *args, char *buf, int bufsz)
{
    int ok = st_rotate(&game, args && args[0] == '-' ? -1 : 1);
    reply(buf, bufsz, ok ? "rotated" : "blocked");
    return !ok;
}

static int hook_drop(const char *args, char *buf, int bufsz)
{
    char tmp[48];
    (void)args;
    sprintf(tmp, "dropped %ld rows, score %ld", (long)st_hard_drop(&game), (long)game.score);
    reply(buf, bufsz, tmp);
    return 0;
}

static int hook_hold(const char *args, char *buf, int bufsz)
{
    (void)args;
    reply(buf, bufsz, st_hold(&game) ? "held" : "can't hold now");
    return 0;
}

static int hook_garbage(const char *args, char *buf, int bufsz)
{
    st_add_garbage(&game, args && args[0] ? atoi(args) : 1);
    reply(buf, bufsz, game.over ? "garbage: pushed over the top" : "garbage ring added");
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
    stack.help = args && args[0] ? atoi(args) : 1;
    if (stack.help < 0 || stack.help > 2) stack.help = 0;
    reply(buf, bufsz, stack.help ? "help shown" : "help closed");
    return 0;
}

static int hook_pause(const char *args, char *buf, int bufsz)
{
    stack.paused = (args && (args[0] == '0' || !strcmp(args, "off"))) ? 0 : 2;   /* 2: no PAUSED text */
    reply(buf, bufsz, stack.paused ? "paused" : "running");
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
    ab_register_var("lines",       AB_TYPE_I32, &game.lines);
    ab_register_var("state",       AB_TYPE_I32, &stack.state);
    ab_register_var("mode",        AB_TYPE_I32, &game.mode);
    ab_register_var("over",        AB_TYPE_I32, &game.over);
    ab_register_var("won",         AB_TYPE_I32, &game.won);
    ab_register_var("pieces",      AB_TYPE_I32, &game.pieces);
    ab_register_var("col",         AB_TYPE_I32, &v_col);
    ab_register_var("row",         AB_TYPE_I32, &v_row);
    ab_register_var("ptype",       AB_TYPE_I32, &v_ptype);
    ab_register_var("hold",        AB_TYPE_I32, &game.hold);
    ab_register_var("garbage",     AB_TYPE_I32, &game.garbage_left);
    ab_register_var("view",        AB_TYPE_I32, &v_view);
    ab_register_var("ghost",       AB_TYPE_I32, &stack.ghost);
    ab_register_var("combo",       AB_TYPE_I32, &game.combo);
    ab_register_var("time_ms",     AB_TYPE_I32, &game.time_ms);
    ab_register_var("start_level", AB_TYPE_I32, &stack.start_level);
    ab_register_var("paused",      AB_TYPE_I32, &stack.paused);
    ab_register_var("help",        AB_TYPE_I32, &stack.help);
    ab_register_var("fps",         AB_TYPE_I32, &v_fps);
    ab_register_var("polys",       AB_TYPE_I32, &v_polys);
    ab_register_var("us_update",   AB_TYPE_I32, &us_update);
    ab_register_var("us_draw",     AB_TYPE_I32, &us_draw);

    ab_register_hook("status",   "One-line game status", hook_status);
    ab_register_hook("start",    "start [marathon/rising/dig] [level]", hook_start);
    ab_register_hook("title",    "Back to the title", hook_title);
    ab_register_hook("move",     "move N: turn the tower N columns (negative: left)", hook_move);
    ab_register_hook("rotate",   "rotate 1 or -1", hook_rotate);
    ab_register_hook("drop",     "Hard drop", hook_drop);
    ab_register_hook("hold",     "Hold the piece", hook_hold);
    ab_register_hook("garbage",  "garbage [gaps]: push up a ring of rubble", hook_garbage);
    ab_register_hook("seed",     "seed N: the next game's seed (same pieces as the browser)", hook_seed);
    ab_register_hook("help",     "help N: rules page 1 or 2 (0 closes)", hook_help);
    ab_register_hook("pause",    "pause on/off (on: a still frame for screenshots)", hook_pause);
    ab_register_hook("selftest", "Run the tests inside the game", hook_selftest);
    ab_register_hook("activate", "The game's window to the front and active (injected keys go to it)", hook_activate);

    ab_register_memregion("board", game.board, sizeof(game.board), "16x24 bytes, bottom row first");
}

/* ---------- the frame ---------- */
void game_init(void)
{
    plat_palette(stack_palette);
    plat_sky(4, 0x113, 0x548, 256);
    stack.ghost = 1;
    stack.start_level = 1;
    register_bridge();
    to_title();
    t_last = t_fps = plat_ticks();
}

int game_input(unsigned in)
{
    unsigned edge = in & ~prev_in;
    int running = 1;
    prev_in = in;

    if (edge & IN_HELP) { stack.help = (stack.help + 1) % 3; return 1; }
    if (stack.help) {
        if (edge & (IN_FIRE | IN_START | IN_ESC)) stack.help = 0;
        return 1;
    }
    if (edge & IN_ESC) {
        if (stack.state == SS_TITLE) running = 0;
        else to_title();
        return running;
    }
    switch (stack.state) {
    case SS_TITLE:
        if (edge & IN_LEFT) stack.mode = (stack.mode + MODE_COUNT - 1) % MODE_COUNT;
        if (edge & IN_RIGHT) stack.mode = (stack.mode + 1) % MODE_COUNT;
        if ((edge & IN_UP) && stack.start_level < 10) stack.start_level++;
        if ((edge & IN_DOWN) && stack.start_level > 1) stack.start_level--;
        if (edge & (IN_FIRE | IN_START)) start_game(stack.mode, stack.start_level);
        break;
    case SS_PLAY:
        if (edge & IN_PAUSE) stack.paused = !stack.paused;
        if (stack.paused) break;
        if (edge & IN_GHOST) stack.ghost = !stack.ghost;
        if (edge & IN_UP) st_rotate(&game, 1);
        if (edge & IN_CCW) st_rotate(&game, -1);
        if (edge & IN_HOLD) st_hold(&game);
        if (edge & IN_FIRE) st_hard_drop(&game);
        st_soft_drop(&game, (in & IN_DOWN) != 0);
        /* turning: one column on the press; held, it repeats (game_update,
         * on the 25 Hz clock: AmigaOS 4 calls this every 10 ms) */
        {
            int dir = (in & IN_LEFT) ? -1 : (in & IN_RIGHT) ? 1 : 0;
            if (dir && (edge & (IN_LEFT | IN_RIGHT))) { st_move(&game, dir); das_dir = dir; das_timer = 7; }
            else if (dir != das_dir) das_dir = 0;
        }
        break;
    case SS_OVER:
        if (stack.state_timer == 0 && (edge & (IN_FIRE | IN_START))) to_title();
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
        if (stack.help || stack.paused) continue;
        if (stack.state == SS_TITLE) demo_step();
        if (stack.state == SS_PLAY && das_dir && --das_timer <= 0) {     /* held: repeat after 0.28 s */
            st_move(&game, das_dir);
            das_timer = 1;
        }
        st_tick(&game, 40);
        handle_events();
        if (stack.flash_timer > 0) stack.flash_timer--;
        if (stack.state_timer > 0) stack.state_timer--;
        if (stack.state == SS_PLAY && (game.over || game.won)) {
            stack.state = SS_OVER;
            stack.state_timer = 40;
            if (game.score > stack.best[game.mode]) stack.best[game.mode] = game.score;
        }
        /* the tower turns towards the piece's column, the short way round */
        {
            int target = game.piece.col << 8, d = ((target - stack.view + 8 * 256) & (16 * 256 - 1)) - 8 * 256;
            stack.view = (stack.view + (d / 3 ? d / 3 : d)) & (16 * 256 - 1);
        }
    }
    us_update = plat_us_since(t0);
    if (stack.state != last_state) {
        static const char *const st[] = { "TITLE", "PLAY", "OVER" };
        AB_I("STATE %s score %ld", st[stack.state], (long)game.score);
        last_state = stack.state;
    }
}

void game_draw(Canvas *cv, int buf)
{
    uint32_t t0 = plat_clock();
    (void)buf;
    if (stack.help) render_help(cv, stack.help);
    else if (stack.state == SS_TITLE) render_title(cv, &game);
    else render_game(cv, &game);
    us_draw = plat_us_since(t0);
}

void game_after_frame(void)
{
    int32_t now = plat_ticks();
    v_polys = a3_npolys;
    v_col = game.piece.col;
    v_row = game.piece.row;
    v_ptype = game.piece.type;
    v_view = stack.view;
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
        ab_test_begin("stack_tower_selftest");
        run_tests(bridge_check);
        ab_test_end();
        AB_I("SELFTEST %ld passed, %ld failed in %ld ms", (long)test_pass, (long)test_fail,
             (long)((plat_ticks() - t0) * 20));
        /* the tests used their own game; ours carries on */
        t_last = plat_ticks();
    }
}
