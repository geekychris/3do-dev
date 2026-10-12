/*
 * DOT CHASE - 3DO port. A ladybug gathers seeds in a hedge maze while
 * four spiders hunt it; a flower turns the tables.
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

/* Color palette: 16 colors (indices named in game.h) */
static const UWORD palette[16] = {
    0x211,  /*  0: dark soil (bg) */
    0xE22,  /*  1: ladybug red */
    0xA5E,  /*  2: Webster, purple */
    0x8AD,  /*  3: Silk, steel blue */
    0xB73,  /*  4: Tangle, brown */
    0xDDB,  /*  5: Nibs, bone */
    0x283,  /*  6: hedge */
    0xFFF,  /*  7: white (text) */
    0x152,  /*  8: hedge shadow */
    0x4B4,  /*  9: hedge leaves */
    0xFC2,  /* 10: gold */
    0x000,  /* 11: black (spots, legs) */
    0x7D4,  /* 12: leaf green */
    0x82C,  /* 13: plum */
    0x999,  /* 14: gray */
    0xDB8,  /* 15: seed */
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
    case STATE_EAT_SPIDER:  return "CAUGHT_SPIDER";
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

        if (gs.ev_flags & EV_SEED)     sound_seed();
        if (gs.ev_flags & EV_EAT_SPIDER) sound_eat_spider();
        if (gs.ev_flags & EV_DIE)       sound_die();
        if (gs.ev_flags & EV_BONUS)     sound_bonus();
        if (gs.ev_flags & EV_EXTRA)     sound_extra_life();
        if (gs.ev_flags & EV_POWER)     sound_power();
        gs.ev_flags = 0;

        if (prev_fright && !gs.fright_active)
            sound_power_off();
        prev_fright = gs.fright_active ? 1 : 0;
        if (gs.state == STATE_PLAYING) {
            if (gs.fright_active) sound_pulse_off();
            else                  sound_set_pulse(gs.pulse_speed);
        } else {
            sound_pulse_off();
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
