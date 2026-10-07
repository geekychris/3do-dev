/*
 * ORB HUNTER - 3DO port.
 *
 * game.c, draw.c and levels.c are the Amiga sources (geekychris/amiga_games)
 * drawing through the Amiga graphics API in sdk/amiga. The sound effects,
 * MOD loading/validation and the state machine below are copied from the
 * Amiga main.c; this file replaces its screen, IDCMP and joystick code.
 *
 * Music: the Amiga version plays DH2:Dev/axelf.mod, a cover of a commercial
 * song that is not part of the source; it is not shipped. If you have a
 * MOD of your own, put it on the disc as orb_hunter/axelf.mod and it plays.
 *
 * Controls (3DO pad): LEFT/RIGHT move, UP or B jump / aim up, DOWN roll,
 * A fire, A or P starts, X quits to the menu.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <string.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "ptplayer.h"
#include "game.h"
#include "draw.h"
#include "input.h"

#define CUSTOM_BASE ((void *)0)

/* ---- from the Amiga main.c ---- */
static UBYTE *mod_data = NULL;
static ULONG  mod_size = 0;

/* Sound effect samples in chip RAM */
#define SFX_SHOOT_LEN       128
#define SFX_MISSILE_LEN     256
#define SFX_BOMB_LEN        512
#define SFX_ITEM_GET_LEN    768
#define SFX_PLAYER_HIT_LEN  256
#define SFX_ENEMY_HIT_LEN   128
#define SFX_DOOR_OPEN_LEN   256
#define SFX_JUMP_LEN        128

static BYTE *sfx_shoot_data = NULL;
static BYTE *sfx_missile_data = NULL;
static BYTE *sfx_bomb_data = NULL;
static BYTE *sfx_item_get_data = NULL;
static BYTE *sfx_player_hit_data = NULL;
static BYTE *sfx_enemy_hit_data = NULL;
static BYTE *sfx_door_open_data = NULL;
static BYTE *sfx_jump_data = NULL;

/* SFX structures for ptplayer */
static SfxStructure sfx_shoot_sfx;
static SfxStructure sfx_missile_sfx;
static SfxStructure sfx_bomb_sfx;
static SfxStructure sfx_item_get_sfx;
static SfxStructure sfx_player_hit_sfx;
static SfxStructure sfx_enemy_hit_sfx;
static SfxStructure sfx_door_open_sfx;
static SfxStructure sfx_jump_sfx;

/* Game state */
static GameState gs;

/* Amiga palette: 16 colors for 4 bitplanes (sci-fi-themed) */
static const UWORD palette[16] = {
    0x001,  /*  0: near-black (BG) */
    0xFFF,  /*  1: white */
    0xE40,  /*  2: orange-red (hunter suit) */
    0xFC0,  /*  3: yellow (suit detail/visor) */
    0x141,  /*  4: dark green (cavern rock) */
    0x262,  /*  5: medium green */
    0x668,  /*  6: grey-blue (metal) */
    0x99A,  /*  7: light grey */
    0xF30,  /*  8: red-orange (lava/magma) */
    0xF80,  /*  9: bright orange */
    0xFF0,  /* 10: yellow (missiles) */
    0xA0F,  /* 11: purple (enemy) */
    0xF0A,  /* 12: magenta */
    0x06F,  /* 13: blue (doors) */
    0x0FF,  /* 14: cyan (items) */
    0x0F0,  /* 15: bright green (beam) */
};

/* --- Sound effect generation --- */

static ULONG sfx_rng_state = 98765;
static WORD sfx_rng(void)
{
    sfx_rng_state = sfx_rng_state * 1103515245UL + 12345UL;
    return (WORD)((sfx_rng_state >> 16) & 0x7FFF);
}

static void build_sfx(void)
{
    WORD i;

    /* Shoot: quick high-pitched blip, square wave descending */
    sfx_shoot_data = (BYTE *)AllocMem(SFX_SHOOT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_shoot_data) {
        for (i = 0; i < SFX_SHOOT_LEN; i++) {
            WORD t = (i * 127) / SFX_SHOOT_LEN;
            WORD env = 127 - t;
            WORD freq = 25 - (i * 10) / SFX_SHOOT_LEN;
            sfx_shoot_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 64 : -64) * env / 127);
        }
    }

    /* Missile: longer bass thump */
    sfx_missile_data = (BYTE *)AllocMem(SFX_MISSILE_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_missile_data) {
        for (i = 0; i < SFX_MISSILE_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_MISSILE_LEN;
            WORD freq = 4 + (i * 2) / SFX_MISSILE_LEN;
            WORD tone = ((i * freq) & 0xFF) > 128 ? 80 : -80;
            WORD noise = (sfx_rng() % 40) - 20;
            sfx_missile_data[i] = (BYTE)(((tone + noise) * env) / 127);
        }
    }

    /* Bomb: explosion noise with decay */
    sfx_bomb_data = (BYTE *)AllocMem(SFX_BOMB_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_bomb_data) {
        for (i = 0; i < SFX_BOMB_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_BOMB_LEN;
            WORD noise = (sfx_rng() % 256) - 128;
            WORD tone = ((i * 3) & 0xFF) > 128 ? 40 : -40;
            sfx_bomb_data[i] = (BYTE)(((noise + tone) * env) / 127);
        }
    }

    /* Item get: ascending arpeggio (3 quick tones) */
    sfx_item_get_data = (BYTE *)AllocMem(SFX_ITEM_GET_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_item_get_data) {
        for (i = 0; i < SFX_ITEM_GET_LEN; i++) {
            WORD seg = i / (SFX_ITEM_GET_LEN / 3);
            WORD freq;
            WORD env = 100;
            if (seg == 0) freq = 20;
            else if (seg == 1) freq = 25;
            else freq = 30;
            /* fade out last third */
            if (i > SFX_ITEM_GET_LEN * 2 / 3)
                env = 100 - ((i - SFX_ITEM_GET_LEN * 2 / 3) * 100) / (SFX_ITEM_GET_LEN / 3);
            sfx_item_get_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 60 : -60) * env / 100);
        }
    }

    /* Player hit: low noise burst */
    sfx_player_hit_data = (BYTE *)AllocMem(SFX_PLAYER_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_player_hit_data) {
        for (i = 0; i < SFX_PLAYER_HIT_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_PLAYER_HIT_LEN;
            WORD noise = (sfx_rng() % 200) - 100;
            sfx_player_hit_data[i] = (BYTE)((noise * env) / 127);
        }
    }

    /* Enemy hit: mid-pitched tick */
    sfx_enemy_hit_data = (BYTE *)AllocMem(SFX_ENEMY_HIT_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_enemy_hit_data) {
        for (i = 0; i < SFX_ENEMY_HIT_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_ENEMY_HIT_LEN;
            sfx_enemy_hit_data[i] = (BYTE)((((i * 15) & 0xFF) > 128 ? 50 : -50) * env / 127);
        }
    }

    /* Door open: descending sweep */
    sfx_door_open_data = (BYTE *)AllocMem(SFX_DOOR_OPEN_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_door_open_data) {
        for (i = 0; i < SFX_DOOR_OPEN_LEN; i++) {
            WORD env = 127 - (i * 80) / SFX_DOOR_OPEN_LEN;
            WORD freq = 30 - (i * 20) / SFX_DOOR_OPEN_LEN;
            sfx_door_open_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 50 : -50) * env / 127);
        }
    }

    /* Jump: quick rising tone */
    sfx_jump_data = (BYTE *)AllocMem(SFX_JUMP_LEN, MEMF_CHIP | MEMF_CLEAR);
    if (sfx_jump_data) {
        for (i = 0; i < SFX_JUMP_LEN; i++) {
            WORD env = 127 - (i * 127) / SFX_JUMP_LEN;
            WORD freq = 10 + (i * 20) / SFX_JUMP_LEN;
            sfx_jump_data[i] = (BYTE)((((i * freq) & 0xFF) > 128 ? 50 : -50) * env / 127);
        }
    }

    /* Setup SFX structures */
    sfx_shoot_sfx.sfx_ptr = sfx_shoot_data;
    sfx_shoot_sfx.sfx_len = SFX_SHOOT_LEN / 2;
    sfx_shoot_sfx.sfx_per = 180;
    sfx_shoot_sfx.sfx_vol = 50;
    sfx_shoot_sfx.sfx_cha = -1;
    sfx_shoot_sfx.sfx_pri = 30;

    sfx_missile_sfx.sfx_ptr = sfx_missile_data;
    sfx_missile_sfx.sfx_len = SFX_MISSILE_LEN / 2;
    sfx_missile_sfx.sfx_per = 400;
    sfx_missile_sfx.sfx_vol = 60;
    sfx_missile_sfx.sfx_cha = -1;
    sfx_missile_sfx.sfx_pri = 40;

    sfx_bomb_sfx.sfx_ptr = sfx_bomb_data;
    sfx_bomb_sfx.sfx_len = SFX_BOMB_LEN / 2;
    sfx_bomb_sfx.sfx_per = 350;
    sfx_bomb_sfx.sfx_vol = 64;
    sfx_bomb_sfx.sfx_cha = -1;
    sfx_bomb_sfx.sfx_pri = 50;

    sfx_item_get_sfx.sfx_ptr = sfx_item_get_data;
    sfx_item_get_sfx.sfx_len = SFX_ITEM_GET_LEN / 2;
    sfx_item_get_sfx.sfx_per = 200;
    sfx_item_get_sfx.sfx_vol = 64;
    sfx_item_get_sfx.sfx_cha = -1;
    sfx_item_get_sfx.sfx_pri = 60;

    sfx_player_hit_sfx.sfx_ptr = sfx_player_hit_data;
    sfx_player_hit_sfx.sfx_len = SFX_PLAYER_HIT_LEN / 2;
    sfx_player_hit_sfx.sfx_per = 450;
    sfx_player_hit_sfx.sfx_vol = 64;
    sfx_player_hit_sfx.sfx_cha = -1;
    sfx_player_hit_sfx.sfx_pri = 55;

    sfx_enemy_hit_sfx.sfx_ptr = sfx_enemy_hit_data;
    sfx_enemy_hit_sfx.sfx_len = SFX_ENEMY_HIT_LEN / 2;
    sfx_enemy_hit_sfx.sfx_per = 220;
    sfx_enemy_hit_sfx.sfx_vol = 48;
    sfx_enemy_hit_sfx.sfx_cha = -1;
    sfx_enemy_hit_sfx.sfx_pri = 25;

    sfx_door_open_sfx.sfx_ptr = sfx_door_open_data;
    sfx_door_open_sfx.sfx_len = SFX_DOOR_OPEN_LEN / 2;
    sfx_door_open_sfx.sfx_per = 280;
    sfx_door_open_sfx.sfx_vol = 50;
    sfx_door_open_sfx.sfx_cha = -1;
    sfx_door_open_sfx.sfx_pri = 35;

    sfx_jump_sfx.sfx_ptr = sfx_jump_data;
    sfx_jump_sfx.sfx_len = SFX_JUMP_LEN / 2;
    sfx_jump_sfx.sfx_per = 160;
    sfx_jump_sfx.sfx_vol = 40;
    sfx_jump_sfx.sfx_cha = -1;
    sfx_jump_sfx.sfx_pri = 20;
}

/* SFX callback functions (called from game.c) */
void sfx_shoot(void)
{
    if (sfx_shoot_data)
        mt_playfx(CUSTOM_BASE, &sfx_shoot_sfx);
}

void sfx_missile(void)
{
    if (sfx_missile_data)
        mt_playfx(CUSTOM_BASE, &sfx_missile_sfx);
}

void sfx_explode(void)
{
    if (sfx_bomb_data)
        mt_playfx(CUSTOM_BASE, &sfx_bomb_sfx);
}

void sfx_powerup(void)
{
    if (sfx_item_get_data)
        mt_playfx(CUSTOM_BASE, &sfx_item_get_sfx);
}

void sfx_player_hit(void)
{
    if (sfx_player_hit_data)
        mt_playfx(CUSTOM_BASE, &sfx_player_hit_sfx);
}

void sfx_hit(void)
{
    if (sfx_enemy_hit_data)
        mt_playfx(CUSTOM_BASE, &sfx_enemy_hit_sfx);
}

void sfx_door(void)
{
    if (sfx_door_open_data)
        mt_playfx(CUSTOM_BASE, &sfx_door_open_sfx);
}

void sfx_jump(void)
{
    if (sfx_jump_data)
        mt_playfx(CUSTOM_BASE, &sfx_jump_sfx);
}

/* --- MOD loading --- */

static UBYTE *load_file_to_chip(const char *path, ULONG *out_size)
{
    BPTR fh;
    UBYTE *buf = NULL;
    LONG len;
    fh = Open((CONST_STRPTR)path, MODE_OLDFILE);
    if (!fh) return NULL;

    /* Get file size via Seek */
    Seek(fh, 0, OFFSET_END);
    len = Seek(fh, 0, OFFSET_BEGINNING);
    if (len <= 0) { Close(fh); return NULL; }

    buf = (UBYTE *)AllocMem(len, MEMF_CHIP);
    if (!buf) { Close(fh); return NULL; }

    if (Read(fh, buf, len) != len) {
        FreeMem(buf, len);
        Close(fh);
        return NULL;
    }

    Close(fh);
    *out_size = (ULONG)len;
    return buf;
}

/* Validate a ProTracker MOD image in memory.
 * Returns 1 if the file is a well-formed 4-channel MOD whose declared
 * pattern + sample data fits within mod_size, 0 otherwise. */
static WORD validate_mod(const UBYTE *data, ULONG size)
{
    const UBYTE *sig;
    const UBYTE *ptable;
    ULONG song_len;
    ULONG max_pat = 0;
    ULONG num_patterns;
    ULONG pattern_bytes;
    ULONG sample_bytes = 0;
    ULONG needed;
    WORD i;

    if (!data || size < 1084) return 0;

    /* Signature at offset 1080 - accept common 4-channel variants only,
       since ptplayer here assumes standard MOD layout. */
    sig = data + 1080;
    if (!((sig[0] == 'M' && sig[1] == '.' && sig[2] == 'K' && sig[3] == '.') ||
          (sig[0] == 'M' && sig[1] == '!' && sig[2] == 'K' && sig[3] == '!') ||
          (sig[0] == 'F' && sig[1] == 'L' && sig[2] == 'T' && sig[3] == '4') ||
          (sig[0] == '4' && sig[1] == 'C' && sig[2] == 'H' && sig[3] == 'N'))) {
        return 0;
    }

    /* Song length (1..128) and pattern table */
    song_len = data[950];
    if (song_len == 0 || song_len > 128) return 0;
    ptable = data + 952;

    /* Highest pattern number referenced */
    for (i = 0; i < 128; i++) {
        if (ptable[i] > max_pat) max_pat = ptable[i];
    }
    if (max_pat >= 128) return 0;
    num_patterns = max_pat + 1;
    pattern_bytes = num_patterns * 1024UL; /* 64 rows * 4 ch * 4 bytes */

    /* Sample lengths are stored as words (in 2-byte units) at offset
       22 + 30*i for i in 0..30. */
    for (i = 0; i < 31; i++) {
        const UBYTE *s = data + 20 + i * 30;
        ULONG len_words = ((ULONG)s[22] << 8) | (ULONG)s[23];
        sample_bytes += len_words * 2UL;
    }

    needed = 1084UL + pattern_bytes + sample_bytes;
    if (needed > size) return 0;

    return 1;
}

/* --- Screen setup with double buffering --- */

/* ---- end of copied code ---- */

static const char *state_name(int s)
{
    switch (s) {
    case STATE_TITLE:           return "TITLE";
    case STATE_PLAYING:         return "PLAYING";
    case STATE_ROOM_TRANSITION: return "ROOM_TRANSITION";
    case STATE_ITEM_GET:        return "ITEM_GET";
    case STATE_DEAD:            return "DEAD";
    case STATE_GAMEOVER:        return "GAMEOVER";
    }
    return "?";
}

/* input.c's input_read() from the 3DO pad */
static UWORD read_pad(void)
{
    ULONG held = pad_held(0), pressed = pad_pressed(0);
    UWORD r = 0;
    if (held & PAD_LEFT)  r |= INPUT_LEFT;
    if (held & PAD_RIGHT) r |= INPUT_RIGHT;
    if (held & (PAD_UP | PAD_B)) r |= INPUT_UP;
    if (held & PAD_DOWN)  r |= INPUT_DOWN;
    if (held & (PAD_A | PAD_C)) r |= INPUT_FIRE;
    if (pressed & PAD_P)  r |= INPUT_START;
    if (amiga_quit_requested()) r |= INPUT_ESC;
    return r;
}

int main(void)
{
    WORD running = 1;
    WORD music_playing = 0;
    int last_state = -1, last_room = -1;

    ab_init("ORB");
    amiga_set_progdir("orb_hunter");
    if (!gfx_init(palette, 16))
        return 1;
    build_sfx();
    mod_data = load_file_to_chip("PROGDIR:axelf.mod", &mod_size);
    if (mod_data) {
        AB_I("Loaded axelf.mod (%ld bytes)", (long)mod_size);
        if (validate_mod(mod_data, mod_size)) {
            mt_install_cia(CUSTOM_BASE, NULL, 1);
            mt_init(CUSTOM_BASE, mod_data, NULL, 0);
            mt_MusicChannels = 2;
            mt_Enable = 1;
            music_playing = 1;
        } else {
            AB_W("axelf.mod failed validation - no music");
            FreeMem(mod_data, mod_size);
            mod_data = NULL;
            mod_size = 0;
        }
    } else {
        AB_W("no axelf.mod on the disc - sound effects only");
        mt_install_cia(CUSTOM_BASE, NULL, 1);     /* sound effects still use the player */
    }

    levels_init();
    memset(&gs, 0, sizeof(GameState));
    gs.state = STATE_TITLE;

    while (running) {
        UWORD inp = read_pad();
        if (inp & INPUT_ESC)
            break;
        {
            struct RastPort *rp = gfx_back();
        /* State machine */
        switch (gs.state) {
            case STATE_TITLE:
                draw_title(rp);
                if (inp & (INPUT_FIRE | INPUT_START)) {
                    game_init(&gs);
                }
                break;

            case STATE_PLAYING:
            case STATE_ROOM_TRANSITION:
                game_update(&gs,
                    (inp & INPUT_LEFT) ? 1 : 0,
                    (inp & INPUT_RIGHT) ? 1 : 0,
                    (inp & INPUT_UP) ? 1 : 0,
                    (inp & INPUT_DOWN) ? 1 : 0,
                    (inp & INPUT_UP) ? 1 : 0,
                    (inp & INPUT_FIRE) ? 1 : 0);
                draw_frame(rp, &gs);
                break;

            case STATE_ITEM_GET:
                draw_frame(rp, &gs);
                draw_item_get(rp, &gs);
                if (inp & (INPUT_FIRE | INPUT_START)) {
                    gs.state = STATE_PLAYING;
                }
                break;

            case STATE_DEAD:
                game_update(&gs,
                    (inp & INPUT_LEFT) ? 1 : 0,
                    (inp & INPUT_RIGHT) ? 1 : 0,
                    (inp & INPUT_UP) ? 1 : 0,
                    (inp & INPUT_DOWN) ? 1 : 0,
                    (inp & INPUT_UP) ? 1 : 0,
                    (inp & INPUT_FIRE) ? 1 : 0);
                draw_frame(rp, &gs);
                break;

            case STATE_GAMEOVER:
                draw_gameover(rp);
                if (inp & (INPUT_FIRE | INPUT_START)) {
                    gs.state = STATE_TITLE;
                }
                break;
        }
    }

        if (gs.state != last_state || gs.room_x * 64 + gs.room_y != last_room) {
            AB_I("state=%s room=%ld,%ld health=%ld missiles=%ld", state_name(gs.state),
                 (long)gs.room_x, (long)gs.room_y, (long)gs.hunter.health, (long)gs.hunter.missiles);
            last_state = gs.state;
            last_room = gs.room_x * 64 + gs.room_y;
        }
        gfx_swap();
        gs.frame++;
    }

    AB_I("exit");
    if (music_playing)
        mt_end(CUSTOM_BASE);
    mt_remove_cia(CUSTOM_BASE);
    if (mod_data) FreeMem(mod_data, mod_size);
    gfx_exit();
    return 0;
}
