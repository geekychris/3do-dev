/*
 * pad_3do.c - Harbour Crane on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   left/right the trolley, up/down the winch (title: the level)
 *   A       grab / let go; start
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
#include "hc.h"
#include "crane.h"

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
    sprintf(buf, "state=%ld level=%ld score=%ld tx=%ld len=%ld carrying=%ld loaded=%ld lost=%ld auto=%ld paused=%ld",
            (long)cr.state, (long)game.level, (long)game.score, (long)(game.tx >> 16), (long)(game.L >> 16),
            (long)game.carrying, (long)game.total_loaded, (long)game.lost, (long)cr.autoplay, (long)cr.paused);
}
