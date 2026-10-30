/*
 * pad_3do.c - Stack Tower on the 3DO pad (sdk/amiga3d/include/a3d_3do.h).
 *
 *   D-pad left/right  turn the tower (title: the mode)
 *   D-pad up          rotate clockwise (title: the start level)
 *   D-pad down        soft drop
 *   A                 hard drop; start
 *   B                 rotate anticlockwise
 *   C                 hold
 *   P                 start (title), pause (play)
 *   L                 the ghost piece on/off
 *   R                 the help pages
 *   X                 quit to the menu
 */
#include <stdio.h>
#include "amiga3do.h"
#include "a3d_plat.h"
#include "a3d_3do.h"
#include "st.h"
#include "stack.h"

const A3PadKey a3_pad_keys[] = {
    { PAD_A, A3_JOY_FIRE, 0 },
    { PAD_B, 0, 0x31 },                 /* Z: anticlockwise */
    { PAD_C, 0, 0x33 },                 /* C: hold */
    { PAD_P, 0, 0x19 },                 /* P: pause */
    { PAD_P, 0, 0x44 },                 /* return: start */
    { PAD_L, 0, 0x24 },                 /* G: the ghost */
    { PAD_R, 0, 0x25 },                 /* H: help */
    { 0, 0, 0 }
};

void a3_game_status(char *buf)
{
    sprintf(buf, "state=%ld mode=%ld level=%ld score=%ld lines=%ld pieces=%ld col=%ld row=%ld rot=%ld hold=%ld view=%ld paused=%ld",
            (long)stack.state, (long)game.mode, (long)game.level, (long)game.score, (long)game.lines, (long)game.pieces,
            (long)game.piece.col, (long)game.piece.row, (long)game.piece.rot, (long)game.hold, (long)stack.view, (long)stack.paused);
}
