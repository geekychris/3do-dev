/*
 * RJ BIRTHDAY - 3DO port: input.c's input_read() (keyboard IDCMP +
 * joystick port 2 on the Amiga) from the 3DO pad.
 *
 *   Play:   D-pad moves, A acts (fire), C opens the guest editor (E),
 *           X leaves the game (Esc).
 *   Title:  A starts, B help (H), C guest editor (E), X quits (Q).
 *   Guest editor: UP/DOWN pick, B deletes, P adds a name (Return),
 *           X goes back (Esc).
 *   Typing a name (high score / new guest) arcade style: A adds a
 *           letter, UP/DOWN change it, B deletes, P or C confirms.
 */
#include <exec/types.h>
#include "amiga3do.h"
#include "game.h"

extern GameState gs;            /* main.c */

static const char wheel[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";

void input_init(void) { }

static void cycle_last_letter(int dir)
{
    int n = (int)sizeof(wheel) - 1, i;
    UBYTE *c;
    if (gs.entry_pos <= 0)
        return;
    c = (UBYTE *)&gs.entry_name[gs.entry_pos - 1];
    for (i = 0; i < n && wheel[i] != *c; i++)
        ;
    i = (i + dir + n) % n;
    *c = (UBYTE)wheel[i];
}

void input_read(InputState *inp, struct Window *win)
{
    ULONG held = pad_held(0), pressed = pad_pressed(0);
    UWORD prev = inp->prev_fire;
    (void)win;

    inp->last_char = 0;
    inp->key_up = inp->key_down = 0;
    inp->key_return = inp->key_backspace = inp->key_delete = 0;
    inp->bits = 0;

    if (gs.state == GS_ENTER_NAME || gs.state == GS_ADD_GUEST) {
        if (pressed & PAD_A)            inp->last_char = 'A';
        if (pressed & PAD_UP)           cycle_last_letter(+1);
        if (pressed & PAD_DOWN)         cycle_last_letter(-1);
        if (pressed & PAD_B)            inp->key_backspace = 1;
        if (pressed & (PAD_P | PAD_C))  inp->key_return = 1;
        if (amiga_quit_requested())     inp->bits |= INP_ESC;
        inp->fire_edge = 0;
        inp->prev_fire = 0;
        return;
    }

    if (held & PAD_LEFT)  inp->bits |= INP_LEFT;
    if (held & PAD_RIGHT) inp->bits |= INP_RIGHT;
    if (held & PAD_UP)    inp->bits |= INP_UP;
    if (held & PAD_DOWN)  inp->bits |= INP_DOWN;
    if (held & PAD_A)     inp->bits |= INP_FIRE;
    if (pressed & PAD_UP)   inp->key_up = 1;
    if (pressed & PAD_DOWN) inp->key_down = 1;

    if (gs.state == GS_TITLE) {
        if (pressed & PAD_P)        inp->bits |= INP_FIRE;
        if (pressed & PAD_B)        inp->last_char = 'h';
        if (pressed & PAD_C)        inp->last_char = 'e';
        if (amiga_quit_requested()) inp->last_char = 'q';
    } else if (gs.state == GS_GUEST_EDIT) {
        if (pressed & PAD_B)        inp->key_delete = 1;
        if (pressed & PAD_P)        inp->key_return = 1;
        if (amiga_quit_requested()) inp->bits |= INP_ESC;
    } else {
        if (pressed & PAD_C)        inp->last_char = 'e';
        if (pressed & PAD_P)        inp->bits |= INP_FIRE;
        if (amiga_quit_requested()) inp->bits |= INP_ESC;
    }

    inp->fire_edge = (inp->bits & INP_FIRE) && !prev;
    inp->prev_fire = (inp->bits & INP_FIRE) ? 1 : 0;
}
