/*
 * JUMP QUEST - 3DO port: input.c's API (keyboard IDCMP + joystick port 2
 * on the Amiga) from the 3DO pad. Same held-state bits as the original.
 *
 *   D-pad: move (UP/DOWN on the title toggle 1/2 players)
 *   A, B or C: jump      P: start      X: quit to the menu
 */
#include <exec/types.h>
#include "amiga3do.h"
#include "game.h"

void input_init(void) { }

UWORD input_read(void)
{
    ULONG held = pad_held(0);
    UWORD r = 0;
    if (held & PAD_LEFT)  r |= INP_LEFT;
    if (held & PAD_RIGHT) r |= INP_RIGHT;
    if (held & PAD_UP)    r |= INP_UP;
    if (held & PAD_DOWN)  r |= INP_DOWN;
    if (held & (PAD_A | PAD_B | PAD_C)) r |= INP_JUMP;
    if (held & PAD_P)     r |= INP_START;
    return r;
}

BOOL input_check_esc(void) { return amiga_quit_requested() ? TRUE : FALSE; }
