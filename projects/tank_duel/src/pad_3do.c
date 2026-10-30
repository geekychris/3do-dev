/*
 * pad_3do.c - Tank Duel on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   up/down forward/back, left/right turn (title: the round)
 *   A       fire; start
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
#include "td.h"
#include "duel.h"

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
    sprintf(buf, "state=%ld round=%ld score=%ld x=%ld z=%ld yaw=%ld hp=%ld shots=%ld kills=%ld auto=%ld paused=%ld",
            (long)du.state, (long)game.round, (long)game.score, (long)(game.tk[0].x >> 16), (long)(game.tk[0].z >> 16),
            (long)game.tk[0].yaw, (long)game.tk[0].hp, (long)game.shots, (long)game.kills, (long)du.autoplay, (long)du.paused);
}
