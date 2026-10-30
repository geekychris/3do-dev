/*
 * pad_3do.c - Boing Pinball on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   L / R           the left / right flipper (D-pad left/right too, as
 *                   the joystick; on the title they choose the table)
 *   A, D-pad down   the plunger (hold, let go); start
 *   D-pad up        nudge
 *   B               the bot plays on/off
 *   C               the help pages
 *   P               pause
 *   X               quit to the menu
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "pb.h"
#include "pinball.h"

extern int bot_play;

const A3PadKey a3_pad_keys[] = {
    { PAD_L, 0, 0x60 },                 /* left shift: the left flipper */
    { PAD_R, 0, 0x61 },                 /* right shift: the right flipper */
    { PAD_A, A3_JOY_FIRE, 0 },          /* the plunger */
    { PAD_B, 0, 0x35 },                 /* the bot */
    { PAD_C, 0, 0x25 },                 /* H: help */
    { PAD_P, 0, 0x19 },                 /* P: pause */
    { PAD_P, 0, 0x50 },                 /* F1: start */
    { 0, 0, 0 }
};

void a3_game_status(char *buf)
{
    sprintf(buf, "state=%ld table=%ld score=%ld ball=%ld balls=%ld lflip=%ld rflip=%ld pull=%ld bot=%ld demo=%ld paused=%ld",
            (long)pin.state, (long)pin.table, (long)game.score, (long)game.ball_no, (long)game.w.nballs,
            (long)game.w.flip[0].held, (long)game.w.flip[1].held, (long)(game.pull >> 10), (long)bot_play,
            (long)pin.demo, (long)pin.paused);
}
