/*
 * URANUS LANDER - 3DO port: input.c's API (keyboard + joystick port 2 on
 * the Amiga) from the 3DO pad.
 *
 *   LEFT/RIGHT rotate, A (or B/C) thrust, UP/DOWN pick letters,
 *   L toggles music (M key), X quits to the menu (Esc).
 */
#include <exec/types.h>
#include "amiga3do.h"
#include "input.h"

void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code)   { (void)code; }
void input_reset(void)          { }

UWORD input_read(void)
{
    ULONG held = pad_held(0);
    UWORD r = 0;
    if (held & PAD_LEFT)  r |= INPUT_LEFT;
    if (held & PAD_RIGHT) r |= INPUT_RIGHT;
    if (held & PAD_UP)    r |= INPUT_UP;
    if (held & PAD_DOWN)  r |= INPUT_DOWN;
    if (held & (PAD_A | PAD_B | PAD_C | PAD_P)) r |= INPUT_THRUST;
    if (held & PAD_L)     r |= INPUT_MUSIC;
    if (amiga_quit_requested()) r |= INPUT_ESC;
    return r;
}
