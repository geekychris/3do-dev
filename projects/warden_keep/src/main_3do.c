/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Chris Collins <chris@hitorro.com> */

/*
 * WARDEN KEEP - 3DO port.
 *
 * game.c is the Amiga game.cpp (geekychris/amiga_games) converted to C
 * for Norcroft; render.c is render.cpp's drawing code on the sdk/amiga
 * layer. This file replaces main.cpp: the 3DO pad instead of IDCMP
 * rawkeys, gfx_swap() instead of ChangeScreenBuffer + WaitTOF. The
 * direction auto-repeat is main.cpp's.
 *
 * Controls (3DO pad): D-pad steps one tile (held: repeats), A or P
 * begins / returns to the title, X quits to the menu. No sound (as on
 * the Amiga).
 */
#include <exec/types.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "game.h"
#include "render.h"

static GameState state;

static const char *mode_names[] = { "TITLE", "PLAYING", "WIN" };

/* Auto-repeat state per-direction. Move fires on rising edge, then
 * again after HOLD_DELAY frames, then every REPEAT_EVERY frames
 * while the key stays held. */
#define HOLD_DELAY    12    /* ~0.24s at 50 Hz PAL */
#define REPEAT_EVERY   6    /* ~0.12s between repeats */

int main(void)
{
    LONG last_mode = -1, last_jewels = -1, last_x = -1, last_y = -1;
    UBYTE prev_input = 0, held_dirs = 0;
    LONG held_frames = 0;

    ab_init("WARDEN");
    if (render_open() != 0)
        return 1;
    game_init(&state);
    AB_I("warden-keep: entering main loop");

    while (!amiga_quit_requested()) {
        ULONG held = pad_held(0);
        UBYTE input = 0, edge, dirs, edge_dirs, tick_input = 0;

        if (held & PAD_LEFT)            input |= INPUT_LEFT;
        if (held & PAD_RIGHT)           input |= INPUT_RIGHT;
        if (held & PAD_UP)              input |= INPUT_UP;
        if (held & PAD_DOWN)            input |= INPUT_DOWN;
        if (held & (PAD_A | PAD_P))     input |= INPUT_START;
        edge = (UBYTE)(input & ~prev_input);
        prev_input = input;

        dirs = (UBYTE)(input & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN));
        edge_dirs = (UBYTE)(edge & (INPUT_LEFT | INPUT_RIGHT | INPUT_UP | INPUT_DOWN));
        if (edge_dirs) {
            tick_input |= edge_dirs;
            held_dirs = dirs;
            held_frames = 0;
        } else if (dirs && dirs == held_dirs) {
            held_frames++;
            if (held_frames == HOLD_DELAY ||
                (held_frames > HOLD_DELAY &&
                 ((held_frames - HOLD_DELAY) % REPEAT_EVERY) == 0)) {
                tick_input |= dirs;
            }
        } else {
            held_dirs = dirs;
            held_frames = 0;
        }
        if (edge & INPUT_START) tick_input |= INPUT_START;

        game_tick(tick_input);

        if (state.mode != last_mode || state.jewels_left != last_jewels) {
            AB_I("state=%s score=%ld jewels_left=%ld", mode_names[state.mode % 3],
                 (long)state.score, (long)state.jewels_left);
            last_mode = state.mode;
            last_jewels = state.jewels_left;
        }
        if (state.mode == GM_PLAYING &&
            (state.player.wx != last_x || state.player.wy != last_y)) {
            AB_I("player wx=%ld wy=%ld", (long)state.player.wx, (long)state.player.wy);
            last_x = state.player.wx;
            last_y = state.player.wy;
        }

        if (state.mode == GM_TITLE)      render_title(&state);
        else if (state.mode == GM_WIN)   render_endscreen(&state);
        else                             render_frame(&state);
        render_flip();
    }

    AB_I("warden-keep: exit score=%ld", (long)state.score);
    render_close();
    return 0;
}
