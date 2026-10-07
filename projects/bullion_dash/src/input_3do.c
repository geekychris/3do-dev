/*
 * BULLION DASH - 3DO port: the game's input.h API (input.c on the Amiga:
 * joystick port 2 + IDCMP keyboard) from the 3DO pad.
 *
 *   D-pad      move / editor cursor        A     fire (start, place tile)
 *   B / C      dig left / right            X     quit (Esc)
 *   on the title: C opens the editor (E key)
 *   editor:    B cycles tile (Space), L saves (F1), R loads (F2),
 *              P test-plays (F3)
 *
 * Same semantics as the original: directions and fire are held state,
 * input_fire() / input_key() are edge-triggered.
 */
#include <exec/types.h>
#include <string.h>
#include "amiga3do.h"
#include "game.h"
#include "input.h"

#define MAX_KEYS 128


static UBYTE keys_curr[MAX_KEYS], keys_prev[MAX_KEYS];
static int dx_val, dy_val, fire_val, prev_fire;

void input_init(struct Window *win) { (void)win; }

void input_update(void)
{
    ULONG held;
    int mode;
    int dig;
    extern GameState gs;             /* main.c */
    held = pad_held(0);
    mode = gs.state == STATE_TITLE ? 0 : gs.state == STATE_EDITOR ? 2 : 1;
    dig = 0;

    prev_fire = fire_val;
    memcpy(keys_prev, keys_curr, sizeof(keys_prev));
    memset(keys_curr, 0, sizeof(keys_curr));

    dx_val = dy_val = 0;
    if (held & PAD_LEFT)  dx_val = -1;
    if (held & PAD_RIGHT) dx_val = 1;
    if (held & PAD_UP)    dy_val = -1;
    if (held & PAD_DOWN)  dy_val = 1;
    fire_val = (held & PAD_A) ? 1 : 0;

    if (mode == 1) {
        /* digging is fire held + a direction: B digs left, C digs right */
        if (held & PAD_B) { dx_val = -1; dig = 1; }
        if (held & PAD_C) { dx_val = 1;  dig = 1; }
        if (dig) fire_val = 1;
    }
    if (held & PAD_P) {
        if (mode == 2) keys_curr[KEY_F3] = 1;
        else           fire_val = 1;
    }
    if (mode == 0 && (held & PAD_C)) keys_curr[KEY_E] = 1;
    if (mode == 2) {
        if (held & PAD_B) keys_curr[KEY_SPACE] = 1;
        if (held & PAD_L) keys_curr[KEY_F1] = 1;
        if (held & PAD_R) keys_curr[KEY_F2] = 1;
    }
    if (amiga_quit_requested()) keys_curr[KEY_ESC] = 1;
}

int input_dx(void)        { return dx_val; }
int input_dy(void)        { return dy_val; }
int input_fire(void)      { return fire_val && !prev_fire; }
int input_fire_held(void) { return fire_val; }

int input_key(int rawkey)
{
    if (rawkey < 0 || rawkey >= MAX_KEYS) return 0;
    return keys_curr[rawkey] && !keys_prev[rawkey];
}

int input_any_key(void)   { return pad_held(0) != 0; }
