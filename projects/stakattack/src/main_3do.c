/*
 * STAKATTACK - 3DO port.
 *
 * game.c / draw.c are the Amiga sources (geekychris/amiga_games) drawing
 * through the Amiga graphics API in sdk/amiga. Music is the original
 * stakattack.mod (on the disc) played by the layer's C ProTracker player;
 * the sound effects below are generated exactly as in the Amiga main.c.
 * This file replaces the AmigaOS main.c (screen, input handler, keyboard).
 *
 * Controls (3DO pad): LEFT/RIGHT move, DOWN soft drop, A or UP rotate,
 * B or C hard drop, P start/pause, X quits to the menu.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <string.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "ptplayer.h"
#include "game.h"
#include "draw.h"

#define CUSTOM_BASE ((void *)0)

static GameState gs;
static UBYTE *mod_data;
static LONG mod_size;
static BOOL music_playing;

/* ---- sound effects (from the Amiga main.c) ---- */
static BYTE *sfx_drop_data = NULL;
static BYTE *sfx_clear_data = NULL;
static BYTE *sfx_lock_data = NULL;
static BYTE *sfx_move_data = NULL;

#define SFX_DROP_LEN  256
#define SFX_CLEAR_LEN 512
#define SFX_LOCK_LEN  128
#define SFX_MOVE_LEN  64

static SfxStructure sfx_drop_sfx;
static SfxStructure sfx_clear_sfx;
static SfxStructure sfx_lock_sfx;
static SfxStructure sfx_move_sfx;

static ULONG sfx_rng_state = 0x12345678;

static ULONG sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245 + 12345;
    return (sfx_rng_state >> 16) & 0x7FFF;
}

static void build_sfx(void)
{
    int i;

    /* Drop: deep thud */
    sfx_drop_data = (BYTE *)AllocMem(SFX_DROP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_drop_data) {
        for (i = 0; i < SFX_DROP_LEN; i++) {
            int env = 127 - (i * 127 / SFX_DROP_LEN);
            int wave = (i % 16 < 8) ? 80 : -80;
            sfx_drop_data[i] = (BYTE)((wave * env) / 127);
        }
        sfx_drop_sfx.sfx_ptr = (APTR)sfx_drop_data;
        sfx_drop_sfx.sfx_len = SFX_DROP_LEN / 2;
        sfx_drop_sfx.sfx_per = 400;
        sfx_drop_sfx.sfx_vol = 64;
        sfx_drop_sfx.sfx_cha = -1;
        sfx_drop_sfx.sfx_pri = 10;
    }

    /* Clear: rising sparkle */
    sfx_clear_data = (BYTE *)AllocMem(SFX_CLEAR_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_clear_data) {
        for (i = 0; i < SFX_CLEAR_LEN; i++) {
            int env = 127 - (i * 100 / SFX_CLEAR_LEN);
            int freq = 4 + (i * 12 / SFX_CLEAR_LEN);
            int wave = (i % freq < freq / 2) ? 70 : -70;
            int noise = (int)(sfx_rng() & 0x1F) - 16;
            sfx_clear_data[i] = (BYTE)(((wave + noise) * env) / 127);
        }
        sfx_clear_sfx.sfx_ptr = (APTR)sfx_clear_data;
        sfx_clear_sfx.sfx_len = SFX_CLEAR_LEN / 2;
        sfx_clear_sfx.sfx_per = 200;
        sfx_clear_sfx.sfx_vol = 55;
        sfx_clear_sfx.sfx_cha = -1;
        sfx_clear_sfx.sfx_pri = 20;
    }

    /* Lock: short click */
    sfx_lock_data = (BYTE *)AllocMem(SFX_LOCK_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_lock_data) {
        for (i = 0; i < SFX_LOCK_LEN; i++) {
            int env = (i < 8) ? 127 : 127 - ((i - 8) * 127 / (SFX_LOCK_LEN - 8));
            int wave = (i % 6 < 3) ? 60 : -60;
            sfx_lock_data[i] = (BYTE)((wave * env) / 127);
        }
        sfx_lock_sfx.sfx_ptr = (APTR)sfx_lock_data;
        sfx_lock_sfx.sfx_len = SFX_LOCK_LEN / 2;
        sfx_lock_sfx.sfx_per = 300;
        sfx_lock_sfx.sfx_vol = 45;
        sfx_lock_sfx.sfx_cha = -1;
        sfx_lock_sfx.sfx_pri = 5;
    }

    /* Move: tiny tick */
    sfx_move_data = (BYTE *)AllocMem(SFX_MOVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_move_data) {
        for (i = 0; i < SFX_MOVE_LEN; i++) {
            int env = (i < 4) ? 127 : 127 - ((i - 4) * 127 / (SFX_MOVE_LEN - 4));
            sfx_move_data[i] = (BYTE)((((i % 4 < 2) ? 40 : -40) * env) / 127);
        }
        sfx_move_sfx.sfx_ptr = (APTR)sfx_move_data;
        sfx_move_sfx.sfx_len = SFX_MOVE_LEN / 2;
        sfx_move_sfx.sfx_per = 200;
        sfx_move_sfx.sfx_vol = 30;
        sfx_move_sfx.sfx_cha = -1;
        sfx_move_sfx.sfx_pri = 2;
    }
}

static void free_sfx(void)
{
    if (sfx_drop_data) FreeMem(sfx_drop_data, SFX_DROP_LEN);
    if (sfx_clear_data) FreeMem(sfx_clear_data, SFX_CLEAR_LEN);
    if (sfx_lock_data) FreeMem(sfx_lock_data, SFX_LOCK_LEN);
    if (sfx_move_data) FreeMem(sfx_move_data, SFX_MOVE_LEN);
}

static const char *state_names[] = { "TITLE", "PLAYING", "PAUSED", "GAMEOVER" };

static int read_pad(void)
{
    ULONG held = pad_held(0), pressed = pad_pressed(0);
    int in = 0;

    if (held & PAD_LEFT)  in |= GINPUT_LEFT;
    if (held & PAD_RIGHT) in |= GINPUT_RIGHT;
    if (held & PAD_DOWN)  in |= GINPUT_DOWN;
    if (gs.state == STATE_PLAYING) {
        if (pressed & (PAD_A | PAD_UP)) in |= GINPUT_ROTATE;
        if (pressed & (PAD_B | PAD_C))  in |= GINPUT_DROP;
    } else if (pressed & (PAD_A | PAD_B | PAD_C)) {
        in |= GINPUT_START;
    }
    if (pressed & PAD_P) in |= GINPUT_START;
    return in;
}

int main(void)
{
    struct RastPort *rp;
    int last_state = -1;
    LONG last_lines = 0;

    ab_init("STAK");
    amiga_set_progdir("stakattack");
    if (!gfx_init(palette, NUM_COLORS))
        return 1;
    build_sfx();
    mt_install_cia(CUSTOM_BASE, NULL, 1);
    mod_data = (UBYTE *)amiga_load_file("PROGDIR:stakattack.mod", &mod_size);
    if (mod_data) {
        AB_I("loaded stakattack.mod (%ld bytes)", (long)mod_size);
        mt_init(CUSTOM_BASE, mod_data, NULL, 0);
        mt_MusicChannels = 2;
        mt_mastervol(CUSTOM_BASE, 40);
        mt_Enable = 1;
        music_playing = TRUE;
    } else {
        AB_W("could not load stakattack.mod");
    }

    game_init(&gs);

    for (;;) {
        int in;
        if (amiga_quit_requested())
            break;
        in = read_pad();
        game_update(&gs, in);

        if (gs.just_locked && sfx_lock_data)
            mt_playfx(CUSTOM_BASE, &sfx_lock_sfx);
        if (gs.just_cleared && sfx_clear_data)
            mt_playfx(CUSTOM_BASE, &sfx_clear_sfx);
        if (gs.just_dropped && sfx_drop_data)
            mt_playfx(CUSTOM_BASE, &sfx_drop_sfx);

        rp = gfx_back();
        draw_clear(rp);
        switch (gs.state) {
        case STATE_TITLE:
            draw_title(rp);
            break;
        case STATE_PLAYING:
        case STATE_PAUSED:
            draw_field(rp, &gs);
            draw_ghost_piece(rp, &gs);
            draw_current_piece(rp, &gs);
            draw_next_piece(rp, &gs);
            draw_hud(rp, &gs);
            if (gs.clear_timer > 0)
                draw_line_clear_flash(rp, &gs);
            if (gs.state == STATE_PAUSED)
                draw_paused(rp);
            break;
        case STATE_GAMEOVER:
            draw_field(rp, &gs);
            draw_hud(rp, &gs);
            draw_gameover(rp, &gs);
            break;
        }

        if (gs.state != last_state) {
            AB_I("state=%s level=%d lines=%d score=%ld", state_names[gs.state & 3],
                 (int)gs.level, (int)gs.lines, (long)gs.score);
            last_state = gs.state;
        }
        if (gs.lines != last_lines) {
            last_lines = gs.lines;
            AB_I("lines=%d score=%ld", (int)gs.lines, (long)gs.score);
        }
        gfx_swap();
    }

    AB_I("exit lines=%d score=%ld", (int)gs.lines, (long)gs.score);
    if (music_playing)
        mt_end(CUSTOM_BASE);
    mt_remove_cia(CUSTOM_BASE);
    free_sfx();
    if (mod_data)
        FreeMem(mod_data, mod_size);
    gfx_exit();
    return 0;
}
