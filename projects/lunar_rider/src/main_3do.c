/*
 * LUNAR RIDER - 3DO port.
 *
 * game.c / draw.c / sound.c are the Amiga sources (geekychris/amiga_games)
 * drawing through the Amiga graphics API in sdk/amiga and playing through
 * its Paula emulation. This file replaces the AmigaOS main.c (screen,
 * double buffering, keyboard and joystick) and input.c.
 *
 * Controls (3DO pad): LEFT/RIGHT speed, UP or B jump, A or C shoot,
 * A or P starts, X quits to the menu.
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
    0x001,  /*  0: Black (sky) */
    0xFFF,  /*  1: White (stars, text) */
    0x214,  /*  2: Dark purple (far mountains) */
    0x426,  /*  3: Purple (near mountains) */
    0x840,  /*  4: Brown (ground) */
    0x520,  /*  5: Dark brown (ground detail) */
    0xFD0,  /*  6: Yellow (buggy body) */
    0xAAA,  /*  7: Gray (buggy wheels) */
    0xF00,  /*  8: Red (explosions) */
    0xF80,  /*  9: Orange (bullets/fire) */
    0x0EF,  /* 10: Cyan (UFOs) */
    0x0F0,  /* 11: Green (checkpoint text) */
    0x44F,  /* 12: Blue (bombs) */
    0xFF0,  /* 13: Bright yellow (score) */
    0x555,  /* 14: Dark gray (rocks) */
    0xF8F,  /* 15: Pink (meteors) */
};

static const char *state_names[] = { "TITLE", "PLAYING", "DYING", "GAMEOVER", "CHECKPOINT" };

static void read_pad(InputState *in)
{
    ULONG held = pad_held(0), pressed = pad_pressed(0);

    memset(in, 0, sizeof(*in));
    in->left = (held & PAD_LEFT) ? 1 : 0;
    in->right = (held & PAD_RIGHT) ? 1 : 0;
    in->jump = (held & (PAD_UP | PAD_B)) ? 1 : 0;
    in->fire = (held & (PAD_A | PAD_C)) ? 1 : 0;
    if (pressed & PAD_P)                 /* P starts from the title / game over */
        in->fire = 1;
    in->quit = amiga_quit_requested() ? 1 : 0;
}

int main(void)
{
    struct RastPort *rp;
    InputState input;
    int startup_delay = 10;      /* as the original: ignore input briefly */
    int steps;
    WORD last_state = -1, last_cp = -1;
    LONG last_score = 0;

    ab_init("LUNAR");
    if (!gfx_init(palette, 16))
        return 1;
    sound_init();
    game_init(&gs);
    gs.state = STATE_TITLE;

    for (;;) {
        rp = gfx_back();
        read_pad(&input);
        if (startup_delay > 0) {
            startup_delay--;
            memset(&input, 0, sizeof(input));
        } else if (input.quit) {
            break;
        }

        /* state machine from the Amiga main.c, run as many logic steps as
         * 50 Hz time requires (gfx_steps) so the game keeps its speed even
         * though drawing the scenery takes longer than one frame */
        steps = gfx_steps();
        while (steps-- > 0) {
            int last = (steps == 0);
            switch (gs.state) {
            case STATE_TITLE:
                gs.frame++;
                if (input.fire || input.jump) {
                    game_init(&gs);
                    gs.state = STATE_PLAYING;
                }
                break;
            case STATE_PLAYING:
            case STATE_CHECKPOINT:
                game_update(&gs, &input);
                if (gs.ev_shoot)      sound_shoot();
                if (gs.ev_explode)    sound_explode();
                if (gs.ev_jump)       sound_jump();
                if (gs.ev_checkpoint) sound_checkpoint();
                if (gs.ev_death)      sound_death();
                break;
            case STATE_DYING:
            case STATE_GAMEOVER:
                game_update(&gs, &input);
                if (gs.ev_explode) sound_explode();
                if (gs.state == STATE_GAMEOVER && (input.fire || input.jump) && last)
                    gs.state = STATE_TITLE;
                break;
            }
            if (!last)
                sound_update();
        }
        switch (gs.state) {
        case STATE_TITLE:
            draw_title(rp, gs.frame);
            break;
        case STATE_GAMEOVER:
            draw_game(rp, &gs);
            draw_gameover(rp, gs.score);
            break;
        default:
            draw_game(rp, &gs);
            if (gs.state == STATE_CHECKPOINT)
                draw_checkpoint(rp, gs.checkpoint_cur);
            break;
        }
        sound_update();

        if (gs.state != last_state || gs.checkpoint_cur != last_cp) {
            AB_I("state=%s checkpoint=%d score=%d lives=%d",
                 gs.state < 5 ? state_names[gs.state] : "?", (int)gs.checkpoint_cur,
                 (int)gs.score, (int)gs.lives);
            last_state = gs.state;
            last_cp = gs.checkpoint_cur;
        }
        if (gs.score != last_score) {
            last_score = gs.score;
            AB_I("score=%d", (int)gs.score);
        }
        gfx_swap();
    }

    AB_I("exit score=%d", (int)gs.score);
    sound_cleanup();
    gfx_exit();
    return 0;
}
