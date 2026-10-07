/*
 * ROCK BLASTER - 3DO port.
 *
 * game.c / draw.c are the unmodified Amiga sources (geekychris/amiga_games);
 * they draw through the Amiga graphics API provided by sdk/amiga. This file
 * replaces the AmigaOS main.c: palette, procedural sound effects (copied
 * from the original) and the main loop.
 *
 * Controls (3DO pad): LEFT/RIGHT rotate, UP or B thrust, A or C fire,
 * P start, X quit to menu.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "ptplayer.h"
#include "game.h"
#include "draw.h"
#include "input.h"

#define CUSTOM_BASE ((void *)0)

static GameState gs;

/* Amiga palette: 16 colors for 4 bitplanes */
static UWORD palette[16] = {
    0x001, 0xFFF, 0x08F, 0x0F8, 0x0F0, 0x888, 0xAAA, 0xCCC,
    0xF00, 0xF80, 0xFF0, 0xF0F, 0x80F, 0x444, 0xFA0, 0xFE0
};

/* --- procedural sound effects (from the Amiga main.c) --- */
#define SFX_SHOOT_LEN      128
#define SFX_EXPLODE_LEN    512
#define SFX_EXPLODE_SM_LEN 256
#define SFX_DIE_LEN        1024

static BYTE *sfx_shoot_data, *sfx_explode_data, *sfx_explode_sm_data, *sfx_die_data;
static SfxStructure sfx_shoot_sfx, sfx_explode_sfx, sfx_explode_sm_sfx, sfx_die_sfx;

static ULONG sfx_rng_state = 98765;
static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245UL + 12345UL;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

static void sfx_setup(SfxStructure *s, BYTE *data, WORD len, WORD per, WORD vol, BYTE pri)
{
    s->sfx_ptr = data;
    s->sfx_len = len / 2;
    s->sfx_per = per;
    s->sfx_vol = vol;
    s->sfx_cha = -1;
    s->sfx_pri = pri;
}

static void build_sfx(void)
{
    WORD i;

    sfx_shoot_data = (BYTE *)AllocMem(SFX_SHOOT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_shoot_data)
        for (i = 0; i < SFX_SHOOT_LEN; i++) {
            WORD t = (i * 127) / SFX_SHOOT_LEN;
            WORD env = 127 - t;
            sfx_shoot_data[i] = (BYTE)((((i * 20) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }

    sfx_explode_data = (BYTE *)AllocMem(SFX_EXPLODE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_data)
        for (i = 0; i < SFX_EXPLODE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_LEN;
            sfx_explode_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }

    sfx_explode_sm_data = (BYTE *)AllocMem(SFX_EXPLODE_SM_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_explode_sm_data)
        for (i = 0; i < SFX_EXPLODE_SM_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_EXPLODE_SM_LEN;
            sfx_explode_sm_data[i] = (BYTE)((sfx_rng() % 256 - 128) * env / 127);
        }

    sfx_die_data = (BYTE *)AllocMem(SFX_DIE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_die_data)
        for (i = 0; i < SFX_DIE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_DIE_LEN;
            WORD freq = 8 + (i * 40) / SFX_DIE_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 60 : -60;
            WORD noise = (sfx_rng() % 80) - 40;
            sfx_die_data[i] = (BYTE)(((tone + noise) * env) / 127);
        }

    sfx_setup(&sfx_shoot_sfx, sfx_shoot_data, SFX_SHOOT_LEN, 200, 50, 30);
    sfx_setup(&sfx_explode_sfx, sfx_explode_data, SFX_EXPLODE_LEN, 300, 64, 50);
    sfx_setup(&sfx_explode_sm_sfx, sfx_explode_sm_data, SFX_EXPLODE_SM_LEN, 250, 48, 40);
    sfx_setup(&sfx_die_sfx, sfx_die_data, SFX_DIE_LEN, 350, 64, 60);
}

static void free_sfx(void)
{
    FreeMem(sfx_shoot_data, SFX_SHOOT_LEN);
    FreeMem(sfx_explode_data, SFX_EXPLODE_LEN);
    FreeMem(sfx_explode_sm_data, SFX_EXPLODE_SM_LEN);
    FreeMem(sfx_die_data, SFX_DIE_LEN);
}

/* SFX callbacks used by game.c */
void sfx_shoot(void)         { if (sfx_shoot_data) mt_playfx(CUSTOM_BASE, &sfx_shoot_sfx); }
void sfx_explode_large(void) { if (sfx_explode_data) mt_playfx(CUSTOM_BASE, &sfx_explode_sfx); }
void sfx_explode_small(void) { if (sfx_explode_sm_data) mt_playfx(CUSTOM_BASE, &sfx_explode_sm_sfx); }
void sfx_thrust_tick(void)   { }
void sfx_die(void)           { if (sfx_die_data) mt_playfx(CUSTOM_BASE, &sfx_die_sfx); }

/* input.h API, from the 3DO pad */
UWORD input_read(void)
{
    ULONG p = pad_held(0);
    UWORD r = 0;
    if (p & PAD_LEFT)            r |= INPUT_LEFT;
    if (p & PAD_RIGHT)           r |= INPUT_RIGHT;
    if (p & (PAD_UP | PAD_B))    r |= INPUT_UP;
    if (p & (PAD_A | PAD_C))     r |= INPUT_FIRE;
    if (pad_pressed(0) & PAD_P)  r |= INPUT_FIRE;   /* P starts from the title */
    if (amiga_quit_requested())  r |= INPUT_ESC;
    return r;
}
void input_key_down(UWORD code) { (void)code; }
void input_key_up(UWORD code)   { (void)code; }
void input_reset(void)          { }

static const char *state_names[] = { "TITLE", "PLAYING", "DEAD", "GAMEOVER" };

int main(void)
{
    struct RastPort *rp;
    UWORD inp;
    int last_state = -1;
    LONG last_score = -1;

    ab_init("ROCK");
    if (!gfx_init(palette, 16))
        return 1;
    game_init_tables();
    build_sfx();
    mt_install_cia(CUSTOM_BASE, NULL, 1);
    mt_MusicChannels = 2;

    game_init(&gs);
    gs.state = STATE_TITLE;
    rp = gfx_back();

    for (;;) {
        inp = input_read();
        if (inp & INPUT_ESC)
            break;

        if (gs.state == STATE_TITLE) {
            draw_clear(rp);
            draw_title(rp);
            if (inp & INPUT_FIRE)
                game_init(&gs);
        } else {
            game_update(&gs, (inp & INPUT_LEFT) ? 1 : 0, (inp & INPUT_RIGHT) ? 1 : 0,
                        (inp & INPUT_UP) ? 1 : 0, (inp & INPUT_FIRE) ? 1 : 0);
            draw_clear(rp);
            draw_rocks(rp, gs.rocks);
            draw_bullets(rp, gs.bullets);
            draw_particles(rp, gs.particles);
            draw_ship(rp, &gs.ship, gs.frame);
            draw_hud(rp, &gs);
            if (gs.state == STATE_GAMEOVER)
                draw_gameover(rp, gs.score);
        }

        if (gs.state != last_state) {
            AB_I("state=%s level=%d score=%d lives=%d", state_names[gs.state & 3],
                 (int)gs.level, (int)gs.score, (int)gs.lives);
            last_state = gs.state;
        }
        if (gs.score != last_score && gs.state != STATE_TITLE) {
            AB_I("score=%d level=%d rocks=%d", (int)gs.score, (int)gs.level, (int)gs.rock_count);
            last_score = gs.score;
        }
        gfx_swap();
    }

    AB_I("exit score=%d", (int)gs.score);
    mt_remove_cia(CUSTOM_BASE);
    free_sfx();
    gfx_exit();
    return 0;
}
