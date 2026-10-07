/*
 * NOVA DEFENSE - 3DO port.
 *
 * game.c / draw.c / sound.c are the Amiga sources (geekychris/amiga_games)
 * drawing through the Amiga graphics API in sdk/amiga and playing through
 * its Paula emulation. This file replaces the AmigaOS main.c (screen,
 * double buffering, IDCMP keyboard and mouse) and input.c.
 *
 * Controls (3DO pad): LEFT/RIGHT move, A (or B/C) fire, A or P start,
 * X quit to menu.
 */
#include <exec/types.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "game.h"
#include "draw.h"
#include "sound.h"

static GameState gs;
static InputState input;

/* the Amiga version's LoadRGB32 palette, as $RRGGBB */
static const ULONG palette[16] = {
    0x000000, 0xFFFFFF, 0x00DD00, 0x008800, 0xEE2222, 0xFFAA00, 0x00EEEE, 0xEE22EE,
    0xFFFF00, 0x4444FF, 0x8888FF, 0xFF6644, 0x44FF44, 0x888888, 0xFF8800, 0xAAAAAA
};

static const char *state_names[] = { "TITLE", "PLAYING", "DYING", "GAMEOVER", "WAVE_CLEAR" };

static void read_pad(void)
{
    ULONG held = pad_held(0), pressed = pad_pressed(0);
    BOOL fire = (held & (PAD_A | PAD_B | PAD_C)) ? TRUE : FALSE;

    input.left = (held & PAD_LEFT) ? TRUE : FALSE;
    input.right = (held & PAD_RIGHT) ? TRUE : FALSE;
    input.fire_pressed = ((pressed & (PAD_A | PAD_B | PAD_C)) ||
                          (gs.state != STATE_PLAYING && (pressed & PAD_P))) ? TRUE : FALSE;
    input.fire = fire;
    input.mouse_dx = 0;
    input.quit = amiga_quit_requested() ? TRUE : FALSE;
}

int main(void)
{
    struct RastPort *rp;
    int i, last_state = -1;
    LONG last_score = 0;
    int startup_delay = 10;     /* as the original: ignore fire for 10 frames */

    ab_init("NOVA");
    if (!gfx_init(0, 16))
        return 1;
    for (i = 0; i < 16; i++)
        gfx_set_rgb24(i, palette[i]);
    if (sound_init() != 0)
        AB_W("sound init failed");

    game_init(&gs);
    game_srand(gfx_frame() * 7919 + 1);

    for (;;) {
        rp = gfx_back();
        read_pad();
        if (startup_delay > 0) {
            startup_delay--;
            input.fire_pressed = FALSE;
        }
        if (input.quit)
            break;

        game_update(&gs, &input);

        if (gs.ev_shoot)      sound_play_shoot();
        if (gs.ev_alien_hit)  sound_play_alien_explode();
        if (gs.ev_player_hit) sound_play_player_explode();
        if (gs.ev_ufo_hit)    sound_play_alien_explode();
        if (gs.ev_march)      sound_play_march(gs.march_note);
        sound_play_ufo(gs.ufo.active);

        switch (gs.state) {
        case STATE_TITLE:    draw_title(rp, &gs); break;
        case STATE_GAMEOVER: draw_gameover(rp, &gs); break;
        default:             draw_game(rp, &gs); break;
        }
        sound_update();

        if (gs.state != last_state) {
            AB_I("state=%s wave=%d score=%d lives=%d",
                 gs.state < 5 ? state_names[gs.state] : "?",
                 (int)gs.wave, (int)gs.score, (int)gs.lives);
            last_state = gs.state;
        }
        if (gs.score != last_score && gs.state == STATE_PLAYING) {
            AB_I("score=%d wave=%d aliens=%d", (int)gs.score, (int)gs.wave,
                 (int)gs.swarm.alive_count);
            last_score = gs.score;
        }
        gfx_swap();
    }

    AB_I("exit score=%d", (int)gs.score);
    sound_cleanup();
    gfx_exit();
    return 0;
}
