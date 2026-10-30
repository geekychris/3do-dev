/*
 * pad_3do.c - Block Breaker 3D on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   move the paddle (up/down on the title: the start level)
 *   A       serve / fire (start on the title)
 *   P       start on the title, pause in play
 *   B       the autopilot on/off
 *   C       the help pages
 *   L       back to the title
 *   X       quit to the menu
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "breaker.h"

const A3PadKey a3_pad_keys[] = {
    { PAD_A, A3_JOY_FIRE, 0 },
    { PAD_P, 0, 0x19 },                 /* P: pause */
    { PAD_P, 0, 0x44 },                 /* return: start */
    { PAD_B, 0, 0x35 },                 /* B: the autopilot */
    { PAD_C, 0, 0x25 },                 /* H: help */
    { PAD_L, 0, 0x45 },                 /* Esc: the title */
    { 0, 0, 0 }
};

void a3_game_status(char *buf)
{
    sprintf(buf, "state=%ld level=%ld score=%ld lives=%ld left=%ld px=%ld py=%ld stuck=%ld auto=%ld paused=%ld help=%ld",
            (long)br.state, (long)game.level, (long)game.score, (long)game.lives, (long)game.left,
            (long)(game.px / BB_FX), (long)(game.py / BB_FX), (long)game.stuck, (long)br.autoplay,
            (long)br.paused, (long)br.help);
}
