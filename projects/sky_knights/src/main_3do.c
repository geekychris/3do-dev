/*
 * SKY KNIGHTS - 3DO port.
 *
 * game.c / draw.c / sound.c are the Amiga sources (geekychris/amiga_games)
 * drawing through the Amiga graphics API in sdk/amiga and playing through
 * its Paula emulation; flap.raw and smash.raw are on the disc in
 * sky_knights/. This file replaces the AmigaOS main.c (screen, double
 * buffering, IDCMP keyboard, joystick port 2) and input.c.
 *
 * Controls: pad 1 is player 1 (keyboard on the Amiga), pad 2 is player 2
 * (joystick port 2). LEFT/RIGHT steer, A/B/C or UP flap. On the title A
 * (or P) starts one player, C (or P on pad 2) starts two. X quits.
 */
#include <exec/types.h>
#include <string.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "game.h"
#include "draw.h"
#include "sound.h"

static GameState gs;

/* Color palette: 16 colors (from the Amiga main.c) */
static const UWORD palette[16] = {
    0x000,  /*  0: Black (bg) */
    0xFFF,  /*  1: White (text) */
    0x840,  /*  2: Brown (platforms) */
    0x520,  /*  3: Dark brown (platform shadow) */
    0xFC0,  /*  4: Yellow (P1 mount) */
    0x06F,  /*  5: Blue (P1 knight) */
    0x0CF,  /*  6: Cyan (P2 mount) */
    0xF22,  /*  7: Red (P2 knight/enemies) */
    0xF80,  /*  8: Orange (Swooper) */
    0xAAA,  /*  9: Gray (Raider) */
    0x22A,  /* 10: Dark blue (Wraith) */
    0x0E0,  /* 11: Green (pods) */
    0xFF0,  /* 12: Bright yellow (score) */
    0xCA8,  /* 13: Tan (ground) */
    0xF8F,  /* 14: Pink (egg about to hatch) */
    0xCCC,  /* 15: Light gray */
};

static const char *state_names[] = { "TITLE", "PLAYING", "WAVE_INTRO", "DYING", "GAMEOVER" };

static UWORD pad_bits(int port)
{
    ULONG held = pad_held(port);
    UWORD r = 0;
    if (held & PAD_LEFT)  r |= INP_LEFT;
    if (held & PAD_RIGHT) r |= INP_RIGHT;
    if (held & (PAD_A | PAD_B | PAD_C | PAD_UP)) r |= INP_FLAP;
    return r;
}

static void read_pads(InputState *in)
{
    ULONG p0 = pad_pressed(0), p1 = pad_pressed(1);

    memset(in, 0, sizeof(*in));
    in->p1 = pad_bits(0);
    in->p2 = pad_bits(1);
    if (gs.state == STATE_TITLE) {
        if (p0 & (PAD_A | PAD_P)) in->sys |= INP_START1;
        if ((p0 & PAD_C) || (p1 & PAD_P)) in->sys |= INP_START2;
        in->p1 &= (UWORD)~INP_FLAP;      /* A starts the game, not a selection toggle */
    }
    if (amiga_quit_requested()) in->sys |= INP_ESC;
}

int main(void)
{
    struct RastPort *rp;
    InputState input;
    int startup_delay = 10;      /* as the original: ignore input briefly */
    WORD last_state = -1, last_wave = -1;
    LONG last_score = 0;

    ab_init("SKY");
    amiga_set_progdir("sky_knights");
    if (!gfx_init(palette, 16))
        return 1;
    sound_init();
    {
        /* the samples sound.c loads: report them so tests can check the disc */
        LONG len = 0;
        void *f = amiga_load_file("PROGDIR:flap.raw", &len);
        AB_I("samples flap.raw=%ld bytes", (long)len);
        if (f) FreeMem(f, len);
    }
    game_init(&gs);

    for (;;) {
        read_pads(&input);
        if (startup_delay > 0) {
            startup_delay--;
            memset(&input, 0, sizeof(input));
        } else if (input.sys & INP_ESC) {
            break;
        }

        game_update(&gs, &input);

        if (gs.ev_flags & EV_FLAP) sound_flap();
        if (gs.ev_flags & EV_KILL) sound_kill();
        if (gs.ev_flags & EV_EGG)  sound_egg();
        if (gs.ev_flags & EV_DIE)  sound_die();
        if (gs.ev_flags & EV_WAVE) sound_wave();
        gs.ev_flags = 0;

        rp = gfx_back();
        switch (gs.state) {
        case STATE_TITLE:      draw_title(rp, &gs); break;
        case STATE_WAVE_INTRO: draw_wave_intro(rp, &gs); break;
        case STATE_PLAYING:    draw_playing(rp, &gs); break;
        case STATE_GAMEOVER:   draw_gameover(rp, &gs); break;
        }
        sound_update();

        if (gs.state != last_state || gs.wave != last_wave) {
            AB_I("state=%s players=%d wave=%d score=%d/%d",
                 gs.state < 5 ? state_names[gs.state] : "?", (int)gs.num_players,
                 (int)gs.wave, (int)gs.score[0], (int)gs.score[1]);
            last_state = gs.state;
            last_wave = gs.wave;
        }
        if (gs.score[0] + gs.score[1] != last_score) {
            last_score = gs.score[0] + gs.score[1];
            AB_I("score=%d/%d wave=%d", (int)gs.score[0], (int)gs.score[1], (int)gs.wave);
        }
        gfx_swap();
    }

    AB_I("exit score=%d/%d", (int)gs.score[0], (int)gs.score[1]);
    sound_cleanup();
    gfx_exit();
    return 0;
}
