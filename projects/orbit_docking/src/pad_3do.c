/*
 * pad_3do.c - Orbit Docking on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   move across (the craft's left, right, up, down);
 *           on the title up/down picks the level
 *   A       forward (in); starts the game on the title
 *   C       back (out)
 *   L / R   roll left / right
 *   B       the autopilot on/off
 *   P       start (title), pause (play)
 *   X       quit to the menu
 * (The help pages and Esc have no button: there are not enough.)
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "od.h"
#include "dock.h"

const A3PadKey a3_pad_keys[] = {
    { PAD_A, 0, 0x11 },                 /* W: in */
    { PAD_A, 0, 0x44 },                 /* return: start */
    { PAD_C, 0, 0x21 },                 /* S: out */
    { PAD_L, 0, 0x20 },                 /* A: roll left */
    { PAD_R, 0, 0x22 },                 /* D: roll right */
    { PAD_B, 0, 0x35 },                 /* B: the autopilot */
    { PAD_P, 0, 0x19 },                 /* P: pause */
    { PAD_P, 0, 0x44 },                 /* return: start */
    { 0, 0, 0 }
};

void a3_game_status(char *buf)
{
    sprintf(buf, "state=%ld level=%ld score=%ld x=%ld y=%ld z=%ld roll=%ld docks=%ld crashes=%ld auto=%ld paused=%ld",
            (long)dk.state, (long)game.level, (long)game.score, (long)(game.x >> 16), (long)(game.y >> 16),
            (long)(game.z >> 16), (long)(game.roll >> 8), (long)game.docks, (long)game.crashes, (long)dk.autoplay, (long)dk.paused);
}
