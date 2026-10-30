/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

/*
 * MINER METEOR - 3DO port.
 *
 * game.c / levels.c are the Amiga game.cpp / levels.cpp (geekychris/
 * amiga_games) converted to C for Norcroft; render.c draws the same
 * picture through the sdk/amiga layer. This file replaces main.cpp:
 * the 3DO pad instead of IDCMP rawkeys and the joystick fire button,
 * gfx_swap() instead of ChangeScreenBuffer + WaitTOF.
 *
 * C on the title plays a recorded run through the first level (demo.h).
 *
 * Controls (3DO pad): LEFT/RIGHT walk, A, B or UP jump, A or P starts /
 * returns to the title, X quits to the menu.
 *
 * The Amiga version is silent (its audio functions are stubs), so is
 * this one.
 */
#include <exec/types.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "game.h"
#include "render.h"
#include "demo.h"

static GameState state;

/* Port addition: the recorded first-level run in demo.h, started from the
 * title with C. It feeds the game one input per tick (so it plays
 * the same at any frame rate) and hands control back when the level is
 * done. */
static int demo_run = -1, demo_tick;
static int demo_input(UBYTE *in)
{
    if (demo_run < 0) return 0;
    if (state.mode == GM_TITLE) { *in = INPUT_START; return 1; }
    if (state.mode != GM_PLAYING || state.level_index != 0 || demo_run >= DEMO_RUNS) {
        demo_run = -1;
        AB_I("demo: done, level=%ld score=%ld", (long)state.level_index + 1, (long)state.score);
        return 0;
    }
    *in = DEMO_RUN[demo_run][0];
    if (++demo_tick >= DEMO_RUN[demo_run][1]) { demo_run++; demo_tick = 0; }
    return 1;
}

static const char *mode_names[] = { "TITLE", "PLAYING", "WIN", "LOSE", "LEVELWIN" };

static void log_state(void)
{
    AB_I("state=%s level=%ld lives=%ld score=%ld keys=%ld/%ld air=%ld",
         mode_names[state.mode % 5], (long)state.level_index + 1, (long)state.lives,
         (long)state.score, (long)(state.keys_total - state.keys_remaining),
         (long)state.keys_total, (long)state.air);
}

int main(void)
{
    LONG last_mode = -1, last_level = -1, last_lives = -1, last_keys = -1;
    ULONG frames = 0;

    ab_init("MINER");
    if (render_open() != 0)
        return 1;
    game_init(&state);
    AB_I("miner-meteor: entering main loop");

    while (!amiga_quit_requested()) {
        ULONG held = pad_held(0);
        ULONG pressed = pad_pressed(0);
        UBYTE in = 0;
        int steps, i;

        if (held & PAD_LEFT)                    in |= INPUT_LEFT;
        if (held & PAD_RIGHT)                   in |= INPUT_RIGHT;
        if (held & (PAD_A | PAD_B | PAD_UP))    in |= INPUT_JUMP;

        if (state.mode == GM_TITLE && (pressed & PAD_C) && demo_run < 0) {
            demo_run = 0;
            demo_tick = 0;
            AB_I("demo: start");
        }

        /* The Amiga runs one tick per 50 Hz frame; keep that speed if a
         * frame takes longer to draw. INPUT_START is the rising edge of
         * SPACE / fire there: here a press of A or P, given to the first
         * tick. */
        steps = gfx_steps();
        for (i = 0; i < steps; i++) {
            UBYTE t = in;
            if (i == 0 && (pressed & (PAD_A | PAD_P))) t |= INPUT_START;
            demo_input(&t);
            game_tick(t);
        }

        if (state.mode != last_mode || state.level_index != last_level ||
            state.lives != last_lives || state.keys_remaining != last_keys) {
            log_state();
            last_mode = state.mode;
            last_level = state.level_index;
            last_lives = state.lives;
            last_keys = state.keys_remaining;
        }
        if (state.mode == GM_PLAYING && (++frames % 250) == 0)
            AB_I("status: x=%ld y=%ld air=%ld", (long)state.player.x,
                 (long)state.player.y, (long)state.air);

        if (state.mode == GM_TITLE)
            render_title(&state);
        else if (state.mode == GM_WIN || state.mode == GM_LOSE)
            render_endscreen(&state);
        else
            render_frame(&state);
        render_flip();
    }

    AB_I("miner-meteor: exit score=%ld", (long)state.score);
    render_close();
    return 0;
}
