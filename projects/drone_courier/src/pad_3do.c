/*
 * pad_3do.c - Drone Courier on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   up/down forward/back, left/right turn (title: the day)
 *   A       climb (let go and the drone sinks slowly); start
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
#include "dc.h"
#include "courier.h"

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
    sprintf(buf, "state=%ld day=%ld score=%ld x=%ld y=%ld z=%ld yaw=%ld carrying=%ld done=%ld battery=%ld auto=%ld paused=%ld",
            (long)co.state, (long)game.day, (long)game.score, (long)(game.x >> 16), (long)(game.y >> 16), (long)(game.z >> 16),
            (long)game.yaw, (long)game.carrying, (long)game.jobs_done, (long)(game.battery >> 16), (long)co.autoplay, (long)co.paused);
}
