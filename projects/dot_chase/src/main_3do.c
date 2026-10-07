/*
 * DOT CHASE - 3DO port.
 *
 * game.c / draw.c / sound.c are the Amiga sources (geekychris/amiga_games)
 * drawing through the Amiga graphics API in sdk/amiga and playing through
 * its Paula emulation. This file replaces the AmigaOS main.c (screen,
 * double buffering, IDCMP keyboard, joystick port) and input.c.
 *
 * Controls (3DO pad): D-pad steers, A or P starts, X quits to the menu.
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
    0xFF0,  /*  1: Yellow (dot-eater) */
    0xF00,  /*  2: Red (Blinky) */
    0xFBD,  /*  3: Pink (Pinky) */
    0x0FF,  /*  4: Cyan (Inky) */
    0xF80,  /*  5: Orange (Clyde) */
    0x22F,  /*  6: Blue (walls/frightened ghost) */
    0xFFF,  /*  7: White (text) */
    0x118,  /*  8: Dark blue (maze outline) */
    0xFDA,  /*  9: Peach (ghost face) */
    0xF9B,  /* 10: Dark pink */
    0xA00,  /* 11: Dark red */
    0x0F0,  /* 12: Green */
    0xA0F,  /* 13: Purple */
    0x888,  /* 14: Gray */
    0xEED,  /* 15: Cream (dots) */
};

static const char *state_name(WORD s)
{
    switch (s) {
    case STATE_TITLE:      return "TITLE";
    case STATE_READY:      return "READY";
    case STATE_PLAYING:    return "PLAYING";
    case STATE_DYING:      return "DYING";
    case STATE_LEVEL_DONE: return "LEVEL_DONE";
    case STATE_GAMEOVER:   return "GAMEOVER";
    case STATE_EAT_GHOST:  return "EAT_GHOST";
    }
    return "?";
}

static void read_pad(InputState *in)
{
    ULONG held = pad_held(0), pressed = pad_pressed(0);

    memset(in, 0, sizeof(*in));
    in->dir = DIR_NONE;
    if (held & PAD_UP)    in->dir = DIR_UP;
    if (held & PAD_DOWN)  in->dir = DIR_DOWN;
    if (held & PAD_LEFT)  in->dir = DIR_LEFT;
    if (held & PAD_RIGHT) in->dir = DIR_RIGHT;
    in->start = (held & (PAD_A | PAD_B | PAD_C)) || (pressed & PAD_P) ? 1 : 0;
    in->quit = amiga_quit_requested() ? 1 : 0;
}

int main(void)
{
    struct RastPort *rp;
    InputState input;
    int startup_delay = 10;      /* as the original: ignore input briefly */
    WORD last_state = -1, prev_fright = 0;
    LONG last_score = 0;

    ab_init("DOT");
    if (!gfx_init(palette, 16))
        return 1;
    if (sound_init() == -1)
        AB_W("sound init failed");

    game_init(&gs);
    draw_set_dirty();

    for (;;) {
        WORD prev_state = gs.state, prev_level = gs.level;

        read_pad(&input);
        if (startup_delay > 0) {
            startup_delay--;
            memset(&input, 0, sizeof(input));
            input.dir = DIR_NONE;
        } else if (input.quit) {
            break;
        }

        game_update(&gs, &input);

        if (gs.level != prev_level ||
            (prev_state == STATE_TITLE && gs.state == STATE_READY) ||
            (prev_state == STATE_GAMEOVER && gs.state == STATE_TITLE))
            draw_set_dirty();

        if (gs.ev_flags & EV_CHOMP)     sound_chomp();
        if (gs.ev_flags & EV_EAT_GHOST) sound_eat_ghost();
        if (gs.ev_flags & EV_DIE)       sound_die();
        if (gs.ev_flags & EV_FRUIT)     sound_fruit();
        if (gs.ev_flags & EV_EXTRA)     sound_extra_life();
        if (gs.ev_flags & EV_POWER)     sound_power();
        gs.ev_flags = 0;

        if (prev_fright && !gs.fright_active)
            sound_power_off();
        prev_fright = gs.fright_active ? 1 : 0;
        if (gs.state == STATE_PLAYING) {
            if (gs.fright_active) sound_siren_off();
            else                  sound_set_siren(gs.siren_speed);
        } else {
            sound_siren_off();
            sound_power_off();
        }

        rp = gfx_back();
        switch (gs.state) {
        case STATE_TITLE:    draw_title(rp, &gs); break;
        case STATE_GAMEOVER: draw_gameover(rp, &gs); break;
        default:             draw_game(rp, &gs); break;
        }
        sound_update();

        if (gs.state != last_state) {
            AB_I("state=%s level=%d score=%d lives=%d", state_name(gs.state),
                 (int)gs.level, (int)gs.score, (int)gs.lives);
            last_state = gs.state;
        }
        if (gs.score != last_score) {
            if (gs.score > last_score && (gs.score / 100) != (last_score / 100))
                AB_I("score=%d level=%d", (int)gs.score, (int)gs.level);
            last_score = gs.score;
        }
        gfx_swap();
    }

    AB_I("exit score=%d", (int)gs.score);
    sound_cleanup();
    gfx_exit();
    return 0;
}
