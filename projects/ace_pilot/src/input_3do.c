/*
 * ACE PILOT - 3DO port: input.c's API (keyboard + joystick ports 1/2 on
 * the Amiga) from the 3DO pads; pad 2 flies player 2 in split screen.
 *
 *   D-pad fly, A fire, L/R throttle down/up, B toggles the display mode
 *   (P key). Title: L = 1 player (F1), R = 2 players (F2), A starts,
 *   X quits / leaves the game (Esc).
 */
#include <exec/types.h>
#include "amiga3do.h"
#include "input.h"

void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code)   { (void)code; }
void input_reset(void)          { }

static UWORD pad_bits(int port)
{
    ULONG held = pad_held(port);
    UWORD r = 0;
    if (held & PAD_LEFT)  r |= INPUT_LEFT;
    if (held & PAD_RIGHT) r |= INPUT_RIGHT;
    if (held & PAD_UP)    r |= INPUT_UP;
    if (held & PAD_DOWN)  r |= INPUT_DOWN;
    if (held & (PAD_A | PAD_C | PAD_P)) r |= INPUT_FIRE;
    if (held & PAD_R)     r |= INPUT_THROT_UP | INPUT_TWO_P;
    if (held & PAD_L)     r |= INPUT_THROT_DN | INPUT_START;
    if (held & PAD_B)     r |= INPUT_MODE;
    return r;
}

UWORD input_read_p1(void)
{
    UWORD r = pad_bits(0);
    if (amiga_quit_requested()) r |= INPUT_ESC;
    return r;
}

UWORD input_read_p2(void) { return pad_bits(1); }
