// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * URANUS LANDER for Amiga
 *
 * Classic lunar lander game with:
 * - Procedurally generated terrain with landing pads
 * - Physics-based thrust and rotation
 * - Fuel management
 * - Multiple difficulty levels
 * - MOD music via ptplayer + procedural SFX
 * - High score table saved to disk
 * - Joystick + keyboard controls
 * - AmigaBridge integration for debug/tuning
 *
 * Controls:
 *   Left/Right or Joy L/R   Rotate ship
 *   Fire/Space/Up or Joy Up Thrust
 *   ESC                     Quit
 */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/dos.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <stdio.h>
#include <string.h>

#include "bridge_client.h"
#include "game.h"
#include "draw.h"
#include "input.h"
#include "sound.h"
#include "ptplayer.h"
#ifdef AMIGA3DO
#include "amiga3do.h"
#endif

/* Libraries */
struct IntuitionBase *IntuitionBase = NULL;
struct GfxBase       *GfxBase = NULL;

/* Screen & double buffering */
static struct Screen *screen = NULL;
static struct Window *window = NULL;
static struct ScreenBuffer *sbuf[2] = { NULL, NULL };
static struct RastPort rp_buf[2];
static WORD cur_buf = 0;
static struct MsgPort *db_port[2] = { NULL, NULL };
static BOOL safe_to_write[2] = { TRUE, TRUE };

/* Custom chip base for ptplayer */
#define CUSTOM_BASE ((void *)0xdff000)

/* MOD data loaded into chip RAM */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;

/* Game state */
static GameState gs;

/* Palette: 16 colors (brown tones for lunar terrain + planet) */
static UWORD palette[16] = {
    0x002,  /*  0: deep space */
    0xEEE,  /*  1: white */
    0x322,  /*  2: dark brown (terrain body, planet shadows) */
    0x644,  /*  3: medium brown (terrain surface, planet) */
    0x876,  /*  4: light tan (terrain highlight, planet) */
    0x226,  /*  5: dark blue */
    0xEC0,  /*  6: yellow (pads, HUD) */
    0x0C0,  /*  7: green (safe) */
    0xD00,  /*  8: red (danger) */
    0xF80,  /*  9: orange (flame) */
    0x0CE,  /* 10: cyan (ship) */
    0x08A,  /* 11: dark cyan */
    0xD0D,  /* 12: magenta (multiplier) */
    0xFE0,  /* 13: bright yellow (score) */
    0x533,  /* 14: warm brown (planet detail) */
    0xFFF,  /* 15: bright white */
};

/* --- MOD loading --- */

static UBYTE *load_file_to_chip(const char *path, ULONG *out_size)
{
    BPTR fh;
    UBYTE *buf = NULL;
    LONG len;

    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return NULL;

    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len <= 0) { Close(fh); return NULL; }

    buf = (UBYTE *)AllocMem(len, MEMF_CHIP);
    if (!buf) { Close(fh); return NULL; }

    if (Read(fh, buf, len) != len) {
        FreeMem(buf, len);
        Close(fh);
        return NULL;
    }

    Close(fh);
    *out_size = (ULONG)len;
    return buf;
}

/* --- Screen setup --- */

#ifdef AMIGA3DO
/* 3DO port: screen + double buffering from the sdk/amiga layer */
static WORD setup_display(void)   { return gfx_init(palette, 16) ? 1 : 0; }
static void swap_buffers(void)
{
    /* log state changes for tests and the debug console */
    static const char *names[] = { "TITLE", "PLAYING", "LANDED", "CRASHING", "GAMEOVER", "ENTER_NAME" };
    static int last_state = -1;
    if (gs.state != last_state) {
        AB_I("state=%s level=%ld score=%ld fuel=%ld", gs.state <= 5 ? names[gs.state] : "?",
             (long)gs.level, (long)gs.score, (long)gs.ship.fuel);
        last_state = gs.state;
    }
    gfx_swap();
}
static void cleanup_display(void) { gfx_exit(); }
#else
static WORD setup_display(void)
{
    WORD i;

    screen = OpenScreenTags(NULL,
        SA_Width,      SCREEN_W,
        SA_Height,     SCREEN_H,
        SA_Depth,      4,
        SA_DisplayID,  LORES_KEY,
        SA_Title,      (ULONG)"Uranus Lander",
        SA_ShowTitle,  FALSE,
        SA_Quiet,      TRUE,
        SA_Type,       CUSTOMSCREEN,
        TAG_DONE);

    if (!screen) return 0;

    /* Set palette */
    {
        struct ViewPort *vp = &screen->ViewPort;
        for (i = 0; i < 16; i++) {
            SetRGB4(vp, i,
                (palette[i] >> 8) & 0xF,
                (palette[i] >> 4) & 0xF,
                palette[i] & 0xF);
        }
    }

    /* Double buffering */
    sbuf[0] = AllocScreenBuffer(screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(screen, NULL, 0);
    if (!sbuf[0] || !sbuf[1]) return 0;

    db_port[0] = CreateMsgPort();
    db_port[1] = CreateMsgPort();
    if (!db_port[0] || !db_port[1]) return 0;

    sbuf[0]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[0];
    sbuf[1]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = db_port[1];

    InitRastPort(&rp_buf[0]);
    rp_buf[0].BitMap = sbuf[0]->sb_BitMap;
    InitRastPort(&rp_buf[1]);
    rp_buf[1].BitMap = sbuf[1]->sb_BitMap;

    SetRast(&rp_buf[0], 0);
    SetRast(&rp_buf[1], 0);

    /* Borderless window for IDCMP input */
    window = OpenWindowTags(NULL,
        WA_Left,         0,
        WA_Top,          0,
        WA_Width,        SCREEN_W,
        WA_Height,       SCREEN_H,
        WA_CustomScreen, (ULONG)screen,
        WA_Borderless,   TRUE,
        WA_Backdrop,     TRUE,
        WA_Activate,     TRUE,
        WA_RMBTrap,      TRUE,
        WA_IDCMP,        IDCMP_RAWKEY | IDCMP_CLOSEWINDOW,
        TAG_DONE);

    if (!window) return 0;

    cur_buf = 1;
    safe_to_write[0] = TRUE;
    safe_to_write[1] = TRUE;

    return 1;
}

static void swap_buffers(void)
{
    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(db_port[cur_buf]))
            WaitPort(db_port[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }

    WaitBlit();
    /* ChangeScreenBuffer returns 0 on failure. Only advance our
     * bookkeeping when the switch was actually accepted, otherwise
     * we'd flag the wrong buffer as pending-display and eventually
     * wait forever for a notification that will never arrive. */
    if (ChangeScreenBuffer(screen, sbuf[cur_buf])) {
        safe_to_write[cur_buf] = FALSE;
        cur_buf ^= 1;
    }

    if (!safe_to_write[cur_buf]) {
        while (!GetMsg(db_port[cur_buf]))
            WaitPort(db_port[cur_buf]);
        safe_to_write[cur_buf] = TRUE;
    }
}

static void cleanup_display(void)
{
    WORD i;

    if (window) { CloseWindow(window); window = NULL; }

    /* Retire whichever buffer is currently on display by switching to
     * a known "final" buffer (buffer 0). Once that succeeds — or if
     * there's no screen at all — no further SafeMessage notifications
     * can arrive, so we only need to drain the ones that were still
     * pending at swap time. */
    if (screen && sbuf[0]) {
        WaitBlit();
        if (ChangeScreenBuffer(screen, sbuf[0])) {
            /* We just displayed buffer 0; the other buffer is now
             * eligible to receive a SafeMessage. Track it. */
            safe_to_write[0] = FALSE;
        }
    }

    /* Drain any outstanding SafeMessages before freeing the buffers. */
    for (i = 0; i < 2; i++) {
        if (!safe_to_write[i] && db_port[i]) {
            while (!GetMsg(db_port[i])) WaitPort(db_port[i]);
            safe_to_write[i] = TRUE;
        }
    }

    for (i = 0; i < 2; i++) {
        if (sbuf[i]) { FreeScreenBuffer(screen, sbuf[i]); sbuf[i] = NULL; }
        if (db_port[i]) { DeleteMsgPort(db_port[i]); db_port[i] = NULL; }
    }

    if (screen) { CloseScreen(screen); screen = NULL; }
}

/* --- Bridge hooks --- */

/* Bounded copy helper: copy up to bufsz-1 chars from src into buf and
 * always NUL-terminate. Rejects non-positive bufsz. */
#endif

static void hook_reply(char *buf, int bufsz, const char *src)
{
    int i;
    if (!buf || bufsz <= 0) return;
    for (i = 0; i < bufsz - 1 && src && src[i] != '\0'; i++)
        buf[i] = src[i];
    buf[i] = '\0';
}

/* Bounded formatted helper: sprintf into a scratch buffer, then bounded-copy.
 * Only supports the single-LONG format strings used by hooks below. */
static void hook_replyf(char *buf, int bufsz, const char *fmt, long v)
{
    char scratch[64];
    if (!buf || bufsz <= 0) return;
    sprintf(scratch, fmt, v);
    hook_reply(buf, bufsz, scratch);
}

static int hook_reset(const char *args, char *buf, int bufsz)
{
    (void)args;
    game_init(&gs);
    hook_reply(buf, bufsz, "Game reset");
    return 0;
}

static int hook_next_level(const char *args, char *buf, int bufsz)
{
    (void)args;
    game_new_level(&gs);
    gs.state = STATE_PLAYING;
    hook_replyf(buf, bufsz, "Level %ld", (long)gs.level);
    return 0;
}

static int hook_add_life(const char *args, char *buf, int bufsz)
{
    (void)args;
    gs.lives++;
    hook_replyf(buf, bufsz, "Lives: %ld", (long)gs.lives);
    return 0;
}

static int hook_add_fuel(const char *args, char *buf, int bufsz)
{
    (void)args;
    gs.ship.fuel = (WORD)g_tune.fuel_max;
    hook_replyf(buf, bufsz, "Fuel refilled to %ld", (long)gs.ship.fuel);
    return 0;
}

/* --- Main --- */

int main(void)
{
    WORD running = 1;
    WORD music_playing = 0;
    WORD music_enabled = 1;
    WORD music_key_held = 0;
    WORD was_thrusting = 0;
    WORD startup_delay = 30;
    WORD cia_installed = 0;

#ifndef AMIGA3DO
    /* Open libraries */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39);
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39);
    if (!IntuitionBase || !GfxBase) {
        if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
        if (GfxBase) CloseLibrary((struct Library *)GfxBase);
        return 20;
    }
#else
    amiga_set_progdir("uranus_lander");
#endif

    /* Init bridge */
    ab_init("URANUS");
    AB_I("Uranus Lander starting up");

    /* Register tunables */
    ab_register_var("gravity",    AB_TYPE_I32, &g_tune.gravity);
    ab_register_var("thrust",     AB_TYPE_I32, &g_tune.thrust);
    ab_register_var("fuel_max",   AB_TYPE_I32, &g_tune.fuel_max);
    ab_register_var("safe_vy",    AB_TYPE_I32, &g_tune.safe_vy);
    ab_register_var("safe_vx",    AB_TYPE_I32, &g_tune.safe_vx);
    ab_register_var("safe_angle", AB_TYPE_I32, &g_tune.safe_angle);
    ab_register_var("turn_rate",  AB_TYPE_I32, &g_tune.turn_rate);
    ab_register_var("start_lives",AB_TYPE_I32, &g_tune.start_lives);
    ab_register_var("score",      AB_TYPE_I32, &gs.score);
    ab_register_var("lives",      AB_TYPE_I32, &gs.lives);
    ab_register_var("level",      AB_TYPE_I32, &gs.level);

    /* Register hooks */
    ab_register_hook("reset",      "Reset game to initial state", hook_reset);
    ab_register_hook("next_level", "Skip to next level",          hook_next_level);
    ab_register_hook("add_life",   "Add one extra life",          hook_add_life);
    ab_register_hook("add_fuel",   "Refill fuel tank",            hook_add_fuel);

    /* Clamp bridge-writable tunables into a safe range BEFORE the first
     * frame consumes them (reset_ship uses fuel_max as a WORD). */
    if (g_tune.fuel_max < 1)     g_tune.fuel_max = 1;
    if (g_tune.fuel_max > 32767) g_tune.fuel_max = 32767;

    /* Init trig tables */
    game_init_tables();

    /* Setup screen */
    if (!setup_display()) {
        AB_E("Failed to setup display");
        cleanup_display();
        ab_cleanup();
#ifndef AMIGA3DO
        CloseLibrary((struct Library *)GfxBase);
        CloseLibrary((struct Library *)IntuitionBase);
#endif
        return 20;
    }

    /* Build sound effects */
    sound_init();

    /* Load planet bitmap into chip RAM */
    planet_gfx_init();

    /* Load MOD music */
    mod_data = load_file_to_chip("PROGDIR:uranus.mod", &mod_size);
    if (mod_data) {
        AB_I("Loaded uranus.mod (%ld bytes)", (long)mod_size);
        mt_install_cia(CUSTOM_BASE, NULL, 1); /* PAL */
        cia_installed = 1;
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 1;  /* Only protect 1 channel for music, 3 for SFX */
        mt_mastervol(CUSTOM_BASE, 28); /* Lower music volume so SFX punch through */
        mt_Enable = 1;
        music_playing = 1;
    } else {
        AB_W("Could not load uranus.mod - SFX (land/beep) disabled");
        /* Without a valid mt_mod pointer, sfx_land/sfx_beep are unsafe to
         * call, so leave music_playing=0 and cia_installed=0 to skip SFX
         * playback. Cleanup below removes CIA only if we installed it. */
    }

    /* Init game to title screen */
    game_init_tables();
    game_init(&gs);
    gs.state = STATE_TITLE;
    input_reset();

    AB_I("Entering main loop");

    /* Main loop */
    while (running) {
        struct IntuiMessage *msg;
        UWORD inp;
        InputState is;

#ifndef AMIGA3DO   /* 3DO port: pad input (input_3do.c) */
        /* Process window messages */
        while ((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG cl = msg->Class;
            UWORD code = msg->Code;
            ReplyMsg((struct Message *)msg);

            if (cl == IDCMP_CLOSEWINDOW) {
                running = 0;
            } else if (cl == IDCMP_RAWKEY) {
                if (code & 0x80)
                    input_key_up(code & 0x7F);
                else
                    input_key_down(code);
            }
        }

        /* Check Ctrl-C */
        if (SetSignal(0L, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C)
            running = 0;

#endif
        /* Read input */
        inp = input_read();

        if (inp & INPUT_ESC)
            running = 0;

        /* M key toggles music (edge-triggered) */
        if (inp & INPUT_MUSIC) {
            if (!music_key_held) {
                music_key_held = 1;
                if (music_playing) {
                    music_enabled = !music_enabled;
                    if (music_enabled) {
                        /* Restart music from beginning */
                        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
                        mt_mastervol(CUSTOM_BASE, 28);
                        mt_Enable = 1;
                    } else {
                        /* Fully stop - mt_end silences all channels */
                        mt_end(CUSTOM_BASE);
                    }
                }
            }
        } else {
            music_key_held = 0;
        }

        /* Build input state */
        is.left   = (inp & INPUT_LEFT)   ? 1 : 0;
        is.right  = (inp & INPUT_RIGHT)  ? 1 : 0;
        is.thrust = (inp & INPUT_THRUST) ? 1 : 0;
        is.up     = (inp & INPUT_UP)     ? 1 : 0;
        is.down   = (inp & INPUT_DOWN)   ? 1 : 0;
        is.quit   = (inp & INPUT_ESC)    ? 1 : 0;

        /* Suppress input during startup */
        if (startup_delay > 0) {
            startup_delay--;
            memset(&is, 0, sizeof(is));
        }

        /* Get back buffer RastPort */
        {
#ifdef AMIGA3DO
            struct RastPort *rp = gfx_back();
#else
            struct RastPort *rp = &rp_buf[cur_buf];
#endif

            if (gs.state == STATE_TITLE) {
                stars_update(&gs);
                draw_clear(rp);
                draw_title(rp, &gs);
                gs.frame++;

                if (is.thrust || is.up) {
                    game_init(&gs);
                    gs.state = STATE_PLAYING;
                    startup_delay = 15;
                }
            } else {
#ifdef AMIGA3DO
                /* 3DO port: run the game at 50 steps/s however long the
                 * frame takes to draw (gfx_steps); draw once below */
                { int steps_ = gfx_steps(); while (steps_-- > 0) {
#endif
                /* Scroll stars during gameplay too */
                stars_update(&gs);

                /* Update game logic */
                game_update(&gs, &is);

                /* Thrust sound: play immediately on press, retrigger while held, silence on release */
                if (cia_installed) {
                    if (gs.ev_thrust) {
                        if (!was_thrusting || (gs.frame % 18) == 0)
                            sfx_thrust_play();
                        was_thrusting = 1;
                    } else {
                        if (was_thrusting)
                            sfx_thrust_stop();
                        was_thrusting = 0;
                    }
                }
                if (cia_installed) {
                    if (gs.ev_crash)
                        sfx_crash_play();
                    if (gs.ev_land)
                        sfx_land_play();
                    if (gs.ev_low_fuel)
                        sfx_beep_play();
                }

#ifdef AMIGA3DO
                } }
#endif
                /* Draw everything */
#ifdef AMIGA3DO
                draw_clear_play(rp);     /* transparent once the terrain layer is up */
#else
                draw_clear(rp);
#endif
                draw_stars(rp, &gs);
                draw_terrain(rp, &gs);
                draw_particles(rp, &gs);

                if (gs.state != STATE_CRASHING || gs.ship.alive)
                    draw_ship(rp, &gs);

                draw_hud(rp, &gs);

                /* State overlays */
                if (gs.state == STATE_LANDED)
                    draw_landed(rp, &gs);
                else if (gs.state == STATE_CRASHING)
                    draw_crash(rp, &gs);
                else if (gs.state == STATE_GAMEOVER)
                    draw_gameover(rp, &gs);
                else if (gs.state == STATE_ENTER_NAME)
                    draw_enter_name(rp, &gs);
            }
        }

        /* Poll bridge */
        ab_poll();

        /* Sync and swap */
#ifndef AMIGA3DO        /* gfx_swap() paces to 50 fps */
        WaitTOF();
#endif
        swap_buffers();
    }

    AB_I("Shutting down");

    /* Cleanup — mt_end silences channels only if music was actually playing.
     * mt_remove_cia is paired with mt_install_cia, tracked separately. */
    if (music_playing) {
        mt_end(CUSTOM_BASE);
    }
    if (cia_installed) {
        mt_remove_cia(CUSTOM_BASE);
    }

    if (mod_data) FreeMem(mod_data, mod_size);

    planet_gfx_cleanup();
    sound_cleanup();
    input_reset();
    cleanup_display();
    ab_cleanup();
#ifndef AMIGA3DO
    CloseLibrary((struct Library *)GfxBase);
    CloseLibrary((struct Library *)IntuitionBase);
#endif

    return 0;
}
