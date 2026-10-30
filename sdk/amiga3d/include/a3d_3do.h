/*
 * a3d_3do.h - what a game on the amiga3d engine adds for the 3DO
 * (sdk/amiga3d/src/main_3do.c): how the pad drives it. The D-pad is the
 * joystick (game_joy_bits); each entry here turns a pad button into the
 * joystick's bits and/or one of the game's Amiga raw keys (game_key_bit).
 * X always quits to the menu (the arcade's convention).
 */
#ifndef A3D_3DO_H
#define A3D_3DO_H

#include <stdint.h>

typedef struct {
    uint32_t pad;                       /* PAD_A ... (amiga3do.h) */
    unsigned joy;                       /* A3_JOY_* bits it gives, or 0 */
    unsigned key;                       /* the Amiga raw key it presses, or 0 */
} A3PadKey;

extern const A3PadKey a3_pad_keys[];    /* ended by { 0, 0, 0 } */

/* the game's state in a line ("state=1 x=12 ..."), logged as "A3D: st ..."
 * once a second when it changes, for the tests (test.py) */
void a3_game_status(char *buf);

#endif
