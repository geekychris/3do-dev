/*
 * pad_3do.c - Marble Tilt on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   tilt the board (up/down on the title: the start level)
 *   A / P   start; P pauses in play
 *   B       the autopilot on/off
 *   C       the help pages
 *   L       back to the title
 *   X       quit to the menu
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "tilt.h"

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
    sprintf(buf, "state=%ld level=%ld score=%ld time=%ld falls=%ld roll=%ld pitch=%ld x=%ld z=%ld auto=%ld paused=%ld",
            (long)tl.state, (long)game.level, (long)game.score, (long)(game.time_left / 25), (long)game.falls,
            (long)(game.roll >> 8), (long)(game.pitch >> 8), (long)(game.x >> 16), (long)(game.z >> 16),
            (long)tl.autoplay, (long)tl.paused);
}
