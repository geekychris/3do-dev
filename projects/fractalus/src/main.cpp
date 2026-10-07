// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * fractalus — Amiga homage to Rescue on Fractalus (Lucasfilm, 1985).
 *
 * Phase 1: fractal terrain, cockpit HUD, flight controls (yaw/pitch/thrust).
 * Later phases add pilot rescue, Jaggi enemies, combat, sound.
 *
 * Built as C++ (no exceptions/RTTI/STL) targeting m68020, AGA 320x256x8bpp.
 * Bridge daemon integration for live variable inspection & remote control.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <devices/inputevent.h>
#include <libraries/dos.h>
#include <dos/dos.h>
#include <dos/dosextens.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

extern "C" {
#include "bridge_client.h"
}

#include "render.h"
#include "terrain.h"
#include "game.h"
#include "pilots.h"
#include "combat.h"
#include "sfx.h"
#include "modplay.h"
#ifdef AMIGA3DO
#include "amiga3do.h"
#endif

/* Amiga library bases — declared in render.cpp as extern.
 * On OS4 PPC, <proto/{intuition,graphics}.h> already extern these as
 * struct Library *, so gate the app-owned defs out for that build. */
#ifndef __PPC__
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase       = NULL;
#endif

/*
 * Ask gcc's amiga startup for a fatter stack — Terrain::heights is 16 KB
 * on its own, plus smooth()'s scratch is another 16 KB (also 16-bit
 * scratch), plus normal C++ frames. Default CLI stack is 4 KB, so
 * anything less than ~48 KB will blow up in main()'s prologue.
 */
extern "C" { ULONG __stack = 65536; }

/*
 * File-scope so heights/scratch land in BSS instead of on main()'s
 * stack. Terrain alone is 16 KB — putting it in main() would still
 * overflow even with the bigger stack request during nested calls.
 */
static Terrain   g_terrain;
static Renderer  g_renderer;
static Game      g_game;
static PilotList g_pilots;
static Combat    g_combat;
static Sfx       g_sfx;

/* Raw-key codes (Amiga rawkeycodes). Primary controls are WASD to
 * avoid FS-UAE's default arrow-keys-mapped-to-joystick behaviour.
 * Arrows still work if FS-UAE isn't intercepting them. */
#define RK_ESC     0x45
#define RK_LEFT    0x4F
#define RK_RIGHT   0x4E
#define RK_UP      0x4C
#define RK_DOWN    0x4D
#define RK_SPACE   0x40
#define RK_RETURN  0x44
#define RK_L       0x28
#define RK_W       0x11
#define RK_A       0x20
#define RK_S       0x21
#define RK_D       0x22
#define RK_Q       0x10
#define RK_Z       0x31
#define RK_UP_MASK 0x80

static UWORD input_flags = 0;

static void apply_key(UWORD code)
{
    UWORD released = code & RK_UP_MASK;
    UWORD raw      = code & 0x7F;
    UWORD bit = 0;

    /* Debug: every raw key the game window receives. If SPACE presses at
     * the QEMU / OS4 window don't start the mission, this line shows
     * whether IDCMP_RAWKEY is even delivering the event and what code
     * OS4 assigned to it. Cheap when quiet, invaluable when broken. */
    AB_I("apply_key: raw=0x%02lx %s", (long)raw, released ? "UP" : "DOWN");

    switch (raw) {
    /* Turn (yaw) */
    case RK_A: case RK_LEFT:  bit = INPUT_LEFT;   break;
    case RK_D: case RK_RIGHT: bit = INPUT_RIGHT;  break;
    /* Thrust / brake */
    case RK_W: case RK_UP:    bit = INPUT_THRUST; break;
    case RK_S: case RK_DOWN:  bit = INPUT_BRAKE;  break;
    /* Pitch (secondary — Q/Z, near WASD) */
    case RK_Q:                bit = INPUT_UP;     break;
    case RK_Z:                bit = INPUT_DOWN;   break;
    /* Actions */
    case RK_SPACE:            bit = INPUT_FIRE;    break;
    case RK_RETURN:           bit = INPUT_RESTART; break;
    case RK_L:                bit = INPUT_LAND;    break;
    default: break;
    }

    if (!bit) return;
    if (released) input_flags &= ~bit;
    else          input_flags |= bit;
}

/* Bridge-exposed state, so we can poke values live during dev. */
static GameState g_state;
static ULONG     g_frame_count = 0;
static LONG      g_force_restart = 0;

#ifndef AMIGA3DO   /* 3DO port: one program at a time anyway */
/* Singleton lock file-scope handle, so release_singleton can find it
 * at any exit point without threading it through function signatures. */
static struct MsgPort *g_singleton_port = NULL;
static const char     *SINGLETON_PORT_NAME = "fractalus.singleton";

static int acquire_singleton(int bridge_ok)
{
    Forbid();
    struct MsgPort *existing = FindPort((CONST_STRPTR)SINGLETON_PORT_NAME);
    if (existing) {
        Permit();
        /* Bridge log is more visible than printf — Run >NIL: eats printf,
         * but AB_W reaches the host log. Only log if the primary ab_init
         * (in main) has already succeeded for this task. */
        if (bridge_ok) {
            AB_W("singleton: refusing to start — another fractalus "
                 "already holds port '%s' (owner task %p)",
                 SINGLETON_PORT_NAME, existing->mp_SigTask);
        }
        printf("fractalus already running - exit that first.\n");
        return -1;
    }
    g_singleton_port = CreateMsgPort();
    if (!g_singleton_port) {
        Permit();
        printf("fractalus: CreateMsgPort failed for singleton lock\n");
        return -1;
    }
    g_singleton_port->mp_Node.ln_Name = (char *)SINGLETON_PORT_NAME;
    g_singleton_port->mp_Node.ln_Pri  = 0;
    AddPort(g_singleton_port);
    Permit();
    return 0;
}

static void release_singleton(void)
{
    if (g_singleton_port) {
        RemPort(g_singleton_port);
        DeleteMsgPort(g_singleton_port);
        g_singleton_port = NULL;
    }
}
#endif

#ifdef AMIGA3DO
/* 3DO port: main()'s reset_world lambda as a function (armcpp predates
 * C++11); it only used globals and bridge_ok */
static int g_bridge_ok = 0;
extern LONG g_debug_all_jaggis;
static void reset_world(ULONG seed)
{
    Terrain   &terrain  = g_terrain;
    Game      &game     = g_game;
    PilotList &pilots   = g_pilots;
    Combat    &combat   = g_combat;
    int bridge_ok = g_bridge_ok;
        g_state.seed = seed;
        terrain.generate(seed);
        game.init(&g_state, &terrain, &pilots, &combat);
        pilots.spawn(FX16_TOINT(g_state.ship.x),
                     FX16_TOINT(g_state.ship.z),
                     seed ^ 0xA5A5A5A5UL, terrain);
        combat.init(seed ^ 0x33445566UL,
                    FX16_TOINT(g_state.ship.x),
                    g_state.ship.y,
                    FX16_TOINT(g_state.ship.z));
        if (bridge_ok) {
            LONG jaggis = 0;
            for (LONG pi = 0; pi < pilots.count(); pi++)
                if (pilots[pi].is_jaggi) jaggis++;
            AB_I("mission ready: %ld/12 jaggis hiding (debug=%ld)",
                 (long)jaggis, (long)g_debug_all_jaggis);
        }
    }
#endif

int main(void)
{
    Terrain   &terrain  = g_terrain;
    Renderer  &renderer = g_renderer;
    Game      &game     = g_game;
    PilotList &pilots   = g_pilots;
    Combat    &combat   = g_combat;
    Sfx       &sfx      = g_sfx;

    /* Grab the singleton lock BEFORE any other init. Concurrent fractalus
     * instances split IDCMP focus so keys land randomly on either window
     * and vanish — we saw this stack up to 9 processes on OS4. Passing
     * bridge_ok=0 here means the "already running" AB_W log is skipped
     * on the very first attempt (ab_init hasn't run yet); we only log
     * on the collision case where our OWN ab_init later succeeds. */
#ifndef AMIGA3DO
    if (acquire_singleton(0) != 0) {
        return 20;
    }
#endif

    /* On OS4 the library bases are typed struct Library by the proto
     * headers; on 68k they're the specific struct types. Gate the cast
     * so both arches compile. */
#ifndef AMIGA3DO
#ifdef __PPC__
    IntuitionBase = OpenLibrary((CONST_STRPTR)"intuition.library", 39);
#else
    IntuitionBase = (struct IntuitionBase *)OpenLibrary(
                        (CONST_STRPTR)"intuition.library", 39);
#endif
    if (!IntuitionBase) {
        printf("no intuition\n");
        release_singleton();
        return 20;
    }
#ifdef __PPC__
    GfxBase = OpenLibrary((CONST_STRPTR)"graphics.library", 39);
#else
    GfxBase = (struct GfxBase *)OpenLibrary(
                  (CONST_STRPTR)"graphics.library", 39);
#endif
    if (!GfxBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        release_singleton();
        return 20;
    }

#endif

    /* Optional bridge — we don't hard-fail if the daemon isn't running,
     * so the game plays standalone from Workbench too. */
    extern LONG g_bench_mask;
    extern LONG g_debug_all_jaggis;
    int bridge_ok = (ab_init("FRAC") == 0);
    if (bridge_ok) {
        AB_I("fractalus v0.1 (Phase 1) starting");
        ab_register_var("frame_count",    AB_TYPE_I32, &g_frame_count);
        ab_register_var("ship_x",         AB_TYPE_I32, &g_state.ship.x);
        ab_register_var("ship_z",         AB_TYPE_I32, &g_state.ship.z);
        ab_register_var("ship_y",         AB_TYPE_I32, &g_state.ship.y);
        ab_register_var("ship_yaw",       AB_TYPE_I32, &g_state.ship.yaw);
        ab_register_var("ship_pitch",     AB_TYPE_I32, &g_state.ship.pitch);
        ab_register_var("ship_speed",     AB_TYPE_I32, &g_state.ship.speed);
        ab_register_var("fuel",           AB_TYPE_I32, &g_state.fuel);
        ab_register_var("shield",         AB_TYPE_I32, &g_state.shield);
        ab_register_var("seed",           AB_TYPE_U32, &g_state.seed);
        /* GameState.running/rescue_state/mode are now LONG (see comment in
         * game.h) — safe to register as I32 without alias garbage. */
        ab_register_var("running",        AB_TYPE_I32, &g_state.running);
        ab_register_var("rescue_state",   AB_TYPE_I32, &g_state.rescue_state);
        ab_register_var("mode",           AB_TYPE_I32, &g_state.mode);
        ab_register_var("pilots_saved",   AB_TYPE_I32, &g_state.pilots_rescued);
        ab_register_var("pilots_lost",    AB_TYPE_I32, &g_state.pilots_lost);
        ab_register_var("score",          AB_TYPE_I32, &g_state.score);
        /* Toggle from the bridge to isolate render bottlenecks —
         * see g_bench_mask BENCH_* bit table in render.cpp. */
        ab_register_var("bench_mask",     AB_TYPE_I32, &g_bench_mask);
        /* Debug: force every spawned pilot to be a jaggi. */
        ab_register_var("debug_all_jaggis", AB_TYPE_I32,
                        &g_debug_all_jaggis);
        /* Debug: set to 1 to force a mid-mission restart on the
         * next frame — useful for applying a debug-var change
         * without having to die first. Auto-clears back to 0. */
        ab_register_var("force_restart",    AB_TYPE_I32,
                        &g_force_restart);
    }

    render_init_math();
#ifdef AMIGA3DO
    gfx_set_max_steps(10);   /* 3DO port: draws at ~9 fps; logic keeps time */
#endif
    /* Audio: modplay owns Paula directly (music on ch 0-2, SFX on
     * ch 3 stolen from music briefly). Sfx generates its samples in
     * chip RAM and hands them to modplay_sfx() on play(). */
    int mod_ok = (modplay_init() == 0);
    if (bridge_ok) AB_I("modplay %s", mod_ok ? "up" : "unavailable");
    /* Title screen gets its own softer ambient track; game gets the
     * heavy-metal riff. Boot starts on TITLE so play the ambient. */
    if (mod_ok) modplay_start_song(MODPLAY_SONG_TITLE);

    sfx.init();
    game.bind_sfx(&sfx);
    combat.bind_sfx(&sfx);

    /* World reset — called on boot and on SPACE-restart from end
     * screens. Regenerates terrain from a fresh seed, respawns pilots
     * and saucers, resets ship + score + shield + fuel. */
#ifndef AMIGA3DO
    auto reset_world = [&](ULONG seed) {
        g_state.seed = seed;
        terrain.generate(seed);
        game.init(&g_state, &terrain, &pilots, &combat);
        pilots.spawn(FX16_TOINT(g_state.ship.x),
                     FX16_TOINT(g_state.ship.z),
                     seed ^ 0xA5A5A5A5UL, terrain);
        combat.init(seed ^ 0x33445566UL,
                    FX16_TOINT(g_state.ship.x),
                    g_state.ship.y,
                    FX16_TOINT(g_state.ship.z));
        if (bridge_ok) {
            LONG jaggis = 0;
            for (LONG pi = 0; pi < pilots.count(); pi++)
                if (pilots[pi].is_jaggi) jaggis++;
            AB_I("mission ready: %ld/12 jaggis hiding (debug=%ld)",
                 (long)jaggis, (long)g_debug_all_jaggis);
        }
    };
#else
    g_bridge_ok = bridge_ok;
#endif
    reset_world(0xC0FFEE01UL);

    /* Sanity-check terrain generation — useful when tuning fractal
     * parameters. Bridge picks up the log line automatically. */
    if (bridge_ok) {
        LONG mn = 255, mx = 0;
        ULONG sum = 0;
        const UBYTE *H = terrain.raw();
        for (LONG i = 0; i < TERRAIN_SIZE * TERRAIN_SIZE; i++) {
            if (H[i] < mn) mn = H[i];
            if (H[i] > mx) mx = H[i];
            sum += H[i];
        }
        AB_I("terrain: min=%ld max=%ld mean=%ld",
             (long)mn, (long)mx,
             (long)(sum / (TERRAIN_SIZE * TERRAIN_SIZE)));
    }

    int err = renderer.open_display();
    if (err) {
        printf("display open failed: %d\n", err);
        if (bridge_ok) ab_cleanup();
#ifndef AMIGA3DO
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
        release_singleton();
#endif
        return 20;
    }

    /* Boot-time world init already done by reset_world above — the
     * per-pilot / per-saucer dump was diagnostic scaffolding, no
     * longer needed now that spawns are deterministic per seed. */
    /* jaggi count now logged from inside reset_world so restarts
     * report too. Static win-target line stays useful. */
    if (bridge_ok) {
        AB_I("goal: rescue %ld of %ld pilots to win",
             (long)MISSION_WIN_PILOTS, (long)pilots.count());
    }

    /* Main loop. Input is polled from IDCMP each frame; game logic runs
     * once per frame; renderer VBLANK-syncs internally. */
    struct MsgPort *up = renderer.user_port();
    while (g_state.running) {
#ifdef AMIGA3DO
        /* 3DO port: pad -> apply_key()'s input bits */
        {
            ULONG held = pad_held(0);
            UWORD f = 0;
            if (held & PAD_LEFT)  f |= INPUT_LEFT;      /* A / left */
            if (held & PAD_RIGHT) f |= INPUT_RIGHT;     /* D / right */
            if (held & PAD_UP)    f |= INPUT_THRUST;    /* W / up */
            if (held & PAD_DOWN)  f |= INPUT_BRAKE;     /* S / down */
            if (held & PAD_L)     f |= INPUT_UP;        /* Q: climb */
            if (held & PAD_R)     f |= INPUT_DOWN;      /* Z: descend */
            if (held & PAD_A)     f |= INPUT_FIRE;      /* Space */
            if (held & PAD_B)     f |= INPUT_LAND;      /* L: land */
            if (held & PAD_P)     f |= INPUT_RESTART | INPUT_FIRE;  /* Return */
            input_flags = f;
            if (amiga_quit_requested()) { g_state.running = 0; break; }   /* Esc */
        }
#else
        struct IntuiMessage *msg;
        ULONG sig = SetSignal(0L, 0L);
        if (sig & SIGBREAKF_CTRL_C) { g_state.running = 0; break; }

        while ((msg = (struct IntuiMessage *)GetMsg(up))) {
            ULONG cls  = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);
            if (cls == IDCMP_RAWKEY) {
                if ((code & 0x7F) == RK_ESC) { g_state.running = 0; break; }
                apply_key(code);
            }
        }
        if (!g_state.running) break;
#endif

        /* Bridge polling: TWO per frame — once before game.tick (drains
         * queued host commands into the client's state) and once after
         * render (flushes replies + heartbeat back). Was three per frame
         * plus per-phase ab_perf_section_* which chewed too much bridge
         * bandwidth on PPC and pinned FPS to ~1. But dropping to a
         * single poll after render starved the bridge queue and hung
         * the client after ~60 frames — the OS4 daemon needs a mid-
         * frame drain to keep queued commands from backing up.
         * Perf tracing is off by default; re-enable if you can accept
         * the extra bridge traffic while profiling. */
        if (bridge_ok) ab_poll();

#ifdef AMIGA3DO
        /* 3DO port: the game was tuned for 25 fps: one logic tick per two
         * 50 Hz layer steps, whatever the drawing rate (about 9 fps) */
        static int s_half_steps = 0;
        int s_steps = gfx_steps();
        int s_ticks;
        s_half_steps += s_steps;
        s_ticks = s_half_steps >> 1;
        s_half_steps &= 1;
        for (int s_t = 0; s_t < s_ticks; s_t++) {
#endif
        UBYTE prev_mode = g_state.mode;
        game.tick(input_flags);
        /* Music silences when a mission ends — score screen shouldn't
         * fight either song. Title music resumes on end-screen -> title
         * transition (handled at reset_world time). */
        if (prev_mode == GM_PLAYING && g_state.mode != GM_PLAYING) {
            modplay_stop();
        }

        /* Edge-detect keypresses (this-frame ^ prev). Used below so
         * a SPACE that broke us out of ATTRACT doesn't ALSO immediately
         * fire title_go on the same held press — the user has to
         * release and press again to launch a mission. Same guard also
         * covers title_go / end_go against spurious level-triggered
         * restarts when a key was already down at mode transition. */
        static UWORD prev_input_flags = 0;
        UWORD input_edge = input_flags & ~prev_input_flags;

        /* ATTRACT (idle demo) transitions:
         *   TITLE, no input for ATTRACT_IDLE_FRAMES  -> GM_ATTRACT
         *   ATTRACT, any real keypress (edge)        -> GM_TITLE
         *
         * On the ATTRACT break we also zero input_flags so title_go
         * (still evaluated below) can't fire on the same frame — the
         * player just wanted out of the demo, not to launch a mission. */
        static UWORD attract_idle = 0;
        const UWORD ATTRACT_IDLE_FRAMES = 150;   /* ~5 sec at 30 fps */
        if (g_state.mode == GM_TITLE) {
            if (input_flags == 0) {
                if (attract_idle < 65535) attract_idle++;
                if (attract_idle >= ATTRACT_IDLE_FRAMES) {
                    ULONG demo_seed =
                        g_state.seed * 1103515245UL + 12345UL;
                    if (bridge_ok) AB_I("attract: idle -> demo");
                    reset_world(demo_seed);
                    g_state.mode = GM_ATTRACT;
                    attract_idle = 0;
                }
            } else {
                attract_idle = 0;
            }
        } else if (g_state.mode == GM_ATTRACT) {
            if (input_edge != 0) {
                if (bridge_ok) AB_I("attract: input -> title");
                g_state.mode = GM_TITLE;
                g_state.state_timer = 0;
                attract_idle = 0;
                input_flags = 0;
                input_edge = 0;
            }
        }

        /* End-screen restart: after the mission has ended, if SPACE is
         * pressed AND the end screen has been showing for >30 ticks
         * (so a mid-airlock fire doesn't accidentally restart), regen
         * the world from a fresh seed. */
        /* Start / restart triggers — all use input_edge so a key that
         * was already held during a mode change doesn't count:
         *   TITLE      : SPACE (INPUT_FIRE) — the "go" key
         *   WIN/LOSE   : RETURN (INPUT_RESTART)
         *   any mode   : force_restart bridge var
         * Debounced: state_timer > 30 avoids accidental double-fires. */
        UWORD title_go =
            (g_state.mode == GM_TITLE
             && (input_edge & INPUT_FIRE)
             && g_state.state_timer > 15);
        UWORD end_go =
            ((g_state.mode == GM_WIN || g_state.mode == GM_LOSE)
             && (input_edge & INPUT_RESTART)
             && g_state.state_timer > 30);
        if (title_go || end_go || g_force_restart) {
            ULONG next_seed = g_state.seed * 1103515245UL + 12345UL;
            if (bridge_ok) AB_I("restart: new mission, seed=%ld",
                                (long)next_seed);
            reset_world(next_seed);
            g_state.mode = GM_PLAYING;   /* leave title / end screen */
            input_flags = 0;
            g_force_restart = 0;
            modplay_start_song(MODPLAY_SONG_GAME);   /* metal for combat */
        }

        prev_input_flags = input_flags;
#ifdef AMIGA3DO
        }
        {   /* 3DO port: log changes for tests and the debug console */
            static LONG l_mode = -1, l_saved = -1;
            if (bridge_ok && ((LONG)g_state.mode != l_mode ||
                              (LONG)g_state.pilots_rescued != l_saved)) {
                AB_I("mode=%ld saved=%ld lost=%ld score=%ld",
                     (long)g_state.mode, (long)g_state.pilots_rescued,
                     (long)g_state.pilots_lost, (long)g_state.score);
                l_mode = g_state.mode;
                l_saved = g_state.pilots_rescued;
            }
        }
#endif

        renderer.render(g_state, terrain, pilots, combat);

        g_frame_count++;
        /* Single bridge poll per frame — see comment above the game.tick
         * call for the rationale. Handles pending var reads/writes,
         * hook calls, and drains the reply queue. */
        if (bridge_ok) ab_poll();
#ifdef AMIGA3DO
        while (s_steps-- > 0)
            modplay_tick();  /* 3DO port: music at 50 Hz (ProTracker rate) */
#else
        modplay_tick();      /* one music tick per frame (VBlank-ish) */
#endif
        sfx.tick();

        /* Cheap heartbeat + real wall-clock FPS from DateStamp (50Hz
         * PAL ticks). If this stops arriving the game has FROZEN; if
         * it keeps arriving but with tiny FPS, the raycaster stalled
         * on some path; if fps stays healthy but input doesn't
         * respond, keyboard focus is the culprit not the game. */
        if (bridge_ok && (g_frame_count % 30) == 0) {
            struct DateStamp now;
            DateStamp(&now);
            static struct DateStamp prev = { 0, 0, 0 };
            static LONG prev_frame = 0;
            LONG dticks = (now.ds_Days   - prev.ds_Days)   * 24 * 60 * 3000
                        + (now.ds_Minute - prev.ds_Minute) * 3000
                        + (now.ds_Tick   - prev.ds_Tick);
            LONG dframes = g_frame_count - prev_frame;
            /* 50 ticks/sec → fps10 = 500 * dframes / dticks (fps × 10). */
            LONG fps10 = (dticks > 0) ? (500L * dframes) / dticks : 0;
            AB_I("hb: frame=%ld fps=%ld.%ld yaw=%ld spd=%ld resc=%ld sh=%ld",
                 (long)g_frame_count,
                 (long)(fps10 / 10), (long)(fps10 % 10),
                 (long)g_state.ship.yaw,
                 (long)g_state.ship.speed,
                 (long)g_state.rescue_state,
                 (long)g_state.shield);
            prev = now;
            prev_frame = g_frame_count;
        }
    }

    if (bridge_ok) {
        AB_I("fractalus shutting down (%ld frames)", (long)g_frame_count);
    }
    sfx.shutdown();
    modplay_stop();
    modplay_cleanup();
    renderer.close_display();
    if (bridge_ok) ab_cleanup();
#ifndef AMIGA3DO
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
    release_singleton();
#endif
    return 0;
}
