/*
 * pad_3do.c - Lander 3D on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   up/down pitch, left/right roll (title: left/right the world)
 *   A       full thrust; start
 *   B / C   the steady throttle up / down (E / Q)
 *   P       start (title), pause (play)
 *   L       back to the title
 *   R       the help pages
 *   X       quit to the menu
 * The autopilot flies the title's demo.
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "ld.h"
#include "lander.h"

const A3PadKey a3_pad_keys[] = {
    { PAD_A, A3_JOY_FIRE, 0 },
    { PAD_B, 0, 0x12 },                 /* E: throttle up */
    { PAD_C, 0, 0x10 },                 /* Q: throttle down */
    { PAD_P, 0, 0x19 },                 /* P: pause */
    { PAD_P, 0, 0x44 },                 /* return: start */
    { PAD_L, 0, 0x45 },                 /* Esc: the title */
    { PAD_R, 0, 0x25 },                 /* H: help */
    { 0, 0, 0 }
};

void a3_game_status(char *buf)
{
    sprintf(buf, "state=%ld menu=%ld world=%ld level=%ld score=%ld ships=%ld y=%ld vy=%ld pitch=%ld roll=%ld throttle=%ld steady=%ld outcome=%ld demo=%ld",
            (long)lander.state, (long)lander.menu, (long)level.world, (long)lander.level, (long)lander.score, (long)lander.ships,
            (long)(level.c.p[1] >> 16), (long)(level.c.v[1] >> 12), (long)(level.c.pitch >> 10), (long)(level.c.roll >> 10),
            (long)(level.c.throttle >> 12), (long)(lander.steady >> 12), (long)level.outcome, (long)lander.demo);
}
