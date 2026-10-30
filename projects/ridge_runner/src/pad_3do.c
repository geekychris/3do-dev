/*
 * pad_3do.c - Ridge Runner on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   left/right steer, up throttle, down brake (title: the track)
 *   A       boost; start
 *   P       start (title), pause (play)
 *   B       the autopilot on/off
 *   C       the help pages
 *   L       back to the title
 *   X       quit to the menu
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "rr.h"
#include "runner.h"

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
    sprintf(buf, "state=%ld track=%ld score=%ld x=%ld z=%ld v=%ld yaw=%ld gate=%ld energy=%ld boost=%ld auto=%ld paused=%ld",
            (long)rn.state, (long)game.track, (long)game.score, (long)(game.x >> 16), (long)(game.z >> 16),
            (long)(game.v >> 12), (long)game.yaw, (long)game.gate, (long)(game.energy >> 12), (long)game.boosting,
            (long)rn.autoplay, (long)rn.paused);
}
