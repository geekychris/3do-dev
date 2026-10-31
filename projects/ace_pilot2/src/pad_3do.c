/*
 * pad_3do.c - Ace Pilot 2 on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad   left/right bank, up/down nose down/up (title: the mission)
 *   A       guns; start
 *   L / R   throttle down / up
 *   C       chase / cockpit view
 *   B       the autopilot on/off
 *   P       start (title), pause (play)
 *   X       quit to the menu
 *
 * The Amiga's help pages (H), invert (I) and Esc have no button left;
 * the stick is the usual way round (forward is nose down).
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "ap.h"
#include "ace.h"

const A3PadKey a3_pad_keys[] = {
    { PAD_A, A3_JOY_FIRE, 0 },
    { PAD_L, 0, 0x31 },                 /* Z: throttle down */
    { PAD_R, 0, 0x20 },                 /* A: throttle up */
    { PAD_C, 0, 0x34 },                 /* V: the view */
    { PAD_B, 0, 0x35 },                 /* B: the autopilot */
    { PAD_P, 0, 0x19 },                 /* P: pause */
    { PAD_P, 0, 0x44 },                 /* return: start */
    { 0, 0, 0 }
};

/* under 160 characters (main_3do.c's buffer) */
void a3_game_status(char *buf)
{
    const Plane *p = &game.p[0];
    sprintf(buf, "state=%ld m=%ld score=%ld x=%ld y=%ld z=%ld yaw=%ld pitch=%ld thr=%ld hp=%ld shots=%ld kills=%ld view=%ld auto=%ld paused=%ld",
            (long)ac.state, (long)game.mission, (long)game.score, (long)(p->x >> 16), (long)(p->y >> 16),
            (long)(p->z >> 16), (long)p->yaw, (long)p->pitch, (long)p->throttle, (long)p->hp,
            (long)game.shots, (long)game.kills, (long)ac.view, (long)ac.autoplay, (long)ac.paused);
}
