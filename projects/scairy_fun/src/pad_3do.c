/*
 * pad_3do.c - ScAIry Fun on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   steer, accelerate, brake (when you drive)
 *   A       the autopilot on/off - take over, hand back; start
 *   B       the view
 *   C       the help pages
 *   P       start (title), pause (play)
 *   L       back to the title
 *   X       quit to the menu
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "sc.h"

extern World world;

const A3PadKey a3_pad_keys[] = {
    { PAD_A, A3_JOY_FIRE, 0 },
    { PAD_B, 0, 0x34 },                 /* V: the view */
    { PAD_C, 0, 0x25 },                 /* H: help */
    { PAD_P, 0, 0x19 },                 /* P: pause */
    { PAD_P, 0, 0x44 },                 /* return: start */
    { PAD_L, 0, 0x45 },                 /* Esc: the title */
    { 0, 0, 0 }
};

void a3_game_status(char *buf)
{
    sprintf(buf, "state=%ld score=%ld seg=%ld d=%ld v=%ld ai=%ld view=%ld mult=%ld",
            (long)world.state, (long)world.score, (long)seg_of(world.car.z), (long)(world.car.d >> FBITS),
            (long)world.car.v, (long)world.ai.on, (long)world.view, (long)world.mult);
}
