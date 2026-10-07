/*
 * BULLION DASH - 3DO port: the game's gfx.h screen API (gfx.c on the
 * Amiga: custom screen + double buffer) on the sdk/amiga layer. The names
 * are bd_* (see gfx.h) because the layer has its own gfx_init/gfx_swap.
 */
#include <exec/types.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "game.h"

/* Bullion Dash palette (from the Amiga gfx.c) */
static const UWORD palette[16] = {
    0x000,  /*  0 BG:        black */
    0xA52,  /*  1 BRICK:     brown */
    0xC73,  /*  2 BRICK_HI:  light brown */
    0x888,  /*  3 SOLID:     gray */
    0xAAA,  /*  4 SOLID_HI:  light gray */
    0x0AA,  /*  5 LADDER:    cyan */
    0x088,  /*  6 BAR:       dark cyan */
    0xEE0,  /*  7 GOLD:      yellow */
    0xFF8,  /*  8 GOLD_HI:   bright yellow */
    0x0D0,  /*  9 PLAYER:    green */
    0x0F4,  /* 10 PLAYER_HI: light green */
    0xD00,  /* 11 ENEMY:     red */
    0xF80,  /* 12 ENEMY_HI:  orange */
    0xFFF,  /* 13 TEXT:      white */
    0x113,  /* 14 HUD_BG:   dark blue */
    0xA52,  /* 15 TRAP:     same as brick */
};

int bd_gfx_init(void)               { return gfx_init(palette, 16) ? 0 : 1; }
void bd_gfx_cleanup(void)           { gfx_exit(); }
struct RastPort *bd_gfx_backbuffer(void) { return gfx_back(); }
/* log state changes for tests and the debug console */
static void log_state(void)
{
    extern GameState gs;
    static const char *names[] = { "TITLE", "PLAYING", "DYING", "LEVEL_DONE", "GAMEOVER", "EDITOR" };
    static int last_state = -1;
    static long last_score = -1, last_gold = -1;
    if (gs.state != last_state) {
        AB_I("state=%s level=%ld score=%ld lives=%ld", gs.state <= 5 ? names[gs.state] : "?",
             (long)gs.level_num, (long)gs.score, (long)gs.lives);
        last_state = gs.state;
    }
    if (gs.state == STATE_PLAYING && ((long)gs.score != last_score || (long)gs.gold_collected != last_gold)) {
        AB_I("score=%ld gold=%ld/%ld", (long)gs.score, (long)gs.gold_collected, (long)gs.gold_total);
        last_score = gs.score;
        last_gold = gs.gold_collected;
    }
}

void bd_gfx_swap(void)              { log_state(); gfx_swap(); }
void bd_gfx_vsync(void)             { /* gfx_swap() paces to 50 fps */ }
struct Screen *bd_gfx_screen(void)  { return 0; }
void bd_gfx_set_palette(void)       { gfx_load_rgb4(palette, 16); }
