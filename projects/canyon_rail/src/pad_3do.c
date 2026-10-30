/*
 * pad_3do.c - Canyon Rail on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   move the sight (title: up/down the stage)
 *   A       fire (held: five shots a second); start
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
#include "cr.h"
#include "rail.h"

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
    sprintf(buf, "state=%ld stage=%ld score=%ld s=%ld sx=%ld sy=%ld shots=%ld kills=%ld shield=%ld boss=%ld auto=%ld paused=%ld",
            (long)rl.state, (long)game.stage, (long)game.score, (long)game.cam_s, (long)game.sx, (long)game.sy,
            (long)game.shots, (long)game.kills, (long)game.shield, (long)game.boss, (long)rl.autoplay, (long)rl.paused);
}
