/*
 * ORBITAL PATROL - 3DO port.
 *
 * game.c, draw.c, score.c and sound.c are the Amiga sources
 * (geekychris/amiga_games), drawing through the Amiga graphics API in
 * sdk/amiga; the music is the original orbital_patrol.mod (on the disc)
 * played by the layer's ProTracker player, and high scores are saved to
 * NVRAM. The state machine below is copied unchanged from main.c; this
 * file replaces its screen, IDCMP keyboard and joystick code.
 *
 * Controls (3DO pad): D-pad flies, A fires, B smart bomb, C hyperspace,
 * A or P starts, X quits to the menu.
 */
#include <exec/types.h>
#include <stdio.h>
#include <string.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "game.h"
#include "draw.h"
#include "score.h"
#include "sound.h"

static GameState gs;
static ScoreTable score_table;
static char entry_name[HISCORE_NAMELEN];
static WORD entry_cursor = 0;
static WORD entry_rank = -1;
static WORD startup_delay = 20;

/* Color palette (from the Amiga main.c) */
static const UWORD palette[16] = {
    0x000,  /*  0: Black (background) */
    0xFFF,  /*  1: White (text, lasers, humans) */
    0x336,  /*  2: Dim star / scanner border */
    0x220,  /*  3: Dark terrain / scanner bg */
    0x460,  /*  4: Terrain green */
    0x5A0,  /*  5: Terrain highlight */
    0xEE0,  /*  6: Ship body yellow / explosion yellow */
    0xF80,  /*  7: Thrust / drone / explosion orange */
    0x0F0,  /*  8: Diver green */
    0xF0F,  /*  9: Stalker magenta / hive */
    0xF00,  /* 10: Dropper red / explosion red / mine */
    0x0CF,  /* 11: HUD cyan / chaser */
    0x000,  /* 12: unused */
    0x000,  /* 13: unused */
    0x000,  /* 14: unused */
    0x000,  /* 15: unused */
};

static const char *state_name(WORD s)
{
    switch (s) {
    case STATE_TITLE:         return "TITLE";
    case STATE_LEVEL_START:   return "LEVEL_START";
    case STATE_PLAYING:       return "PLAYING";
    case STATE_DYING:         return "DYING";
    case STATE_RESPAWNING:    return "RESPAWNING";
    case STATE_LEVEL_CLEAR:   return "LEVEL_CLEAR";
    case STATE_GAMEOVER:      return "GAMEOVER";
    case STATE_HISCORE_ENTRY: return "HISCORE_ENTRY";
    case STATE_HISCORE_VIEW:  return "HISCORE_VIEW";
    }
    return "?";
}

/* input.c's input_read(), from the 3DO pad: held directions and fire,
 * smart bomb and hyperspace edge-triggered as in the original */
static WORD read_pad(void)
{
    ULONG held = pad_held(0), pressed = pad_pressed(0);
    WORD r = 0;
    if (held & PAD_LEFT)  r |= INPUT_LEFT;
    if (held & PAD_RIGHT) r |= INPUT_RIGHT;
    if (held & PAD_UP)    r |= INPUT_UP;
    if (held & PAD_DOWN)  r |= INPUT_DOWN;
    if (held & PAD_A)     r |= INPUT_FIRE | INPUT_START;
    if (held & PAD_P)     r |= INPUT_START;
    if (pressed & PAD_B)  r |= INPUT_BOMB;
    if (pressed & PAD_C)  r |= INPUT_HYPER;
    if (amiga_quit_requested()) r |= INPUT_QUIT;
    return r;
}

int main(void)
{
    struct RastPort *rp;
    WORD input;
    int running = 1, steps;
    WORD last_state = -1, last_level = -1;
    LONG last_score = 0;

    ab_init("ORBIT");
    amiga_set_progdir("orbital_patrol");
    if (!gfx_init(palette, 16))
        return 1;
    sound_init();
    if (sound_load_mod("PROGDIR:orbital_patrol.mod")) {
        AB_I("MOD loaded");
        sound_start_music();
    } else {
        AB_W("No MOD file found - continuing without music");
    }
    score_init(&score_table);
    score_load(&score_table);
    game_init(&gs, 1);
    gs.state = STATE_TITLE;
    gs.hiscore = score_table.count > 0 ? score_table.entries[0].score : 0;

    while (running) {
        rp = gfx_back();
        input = read_pad();
        if (input & INPUT_QUIT)
            break;
        if (startup_delay > 0) {
            startup_delay--;
            input = 0;
        }

        /* game logic at 50 steps/s even when drawing is slower (gfx_steps):
         * the state machine from main.c runs once per step and draws only
         * on the last one */
        steps = gfx_steps();
        while (steps-- > 0) {
            int draw = (steps == 0);
            /* State machine */
            switch (gs.state) {
            case STATE_TITLE:
                if (draw) draw_title(rp, &gs);
                if (gs.frame > 30 && (input & (INPUT_START | INPUT_FIRE))) {
                    game_init(&gs, 1);
                    gs.hiscore = score_table.count > 0 ? score_table.entries[0].score : 0;
                    gs.state = STATE_LEVEL_START;
                    gs.state_timer = 45;
                    game_spawn_wave(&gs);
                }
                break;

            case STATE_LEVEL_START:
                game_update(&gs, 0); /* no input during level start */
                if (draw) draw_all(rp, &gs);
                if (draw) draw_level_message(rp, gs.level, "GET READY");
                gs.state_timer--;
                if (gs.state_timer <= 0)
                    gs.state = STATE_PLAYING;
                break;

            case STATE_PLAYING:
                game_update(&gs, input);
                if (draw) draw_all(rp, &gs);
                break;

            case STATE_DYING:
                game_update(&gs, 0);
                if (draw) draw_all(rp, &gs);
                gs.state_timer--;
                if (gs.state_timer <= 0) {
                    if (gs.lives > 0) {
                        gs.state = STATE_RESPAWNING;
                        gs.state_timer = 60;
                        gs.ship.alive = 1;
                        gs.ship.wy = TO_FP(PLAY_TOP + (PLAY_BOT - PLAY_TOP) / 2);
                        gs.ship.vx = 0;
                        gs.ship.vy = 0;
                        gs.ship.invuln_timer = 120;
                    } else {
                        gs.state = STATE_GAMEOVER;
                        gs.state_timer = 180;
                    }
                }
                break;

            case STATE_RESPAWNING:
                game_update(&gs, input);
                if (draw) draw_all(rp, &gs);
                gs.state_timer--;
                if (gs.state_timer <= 0)
                    gs.state = STATE_PLAYING;
                break;

            case STATE_LEVEL_CLEAR:
                if (draw) draw_all(rp, &gs);
                {
                    WORD bonus = gs.humans_alive * SCORE_WAVE_BONUS;
                    char buf[40];
                    sprintf(buf, "BONUS %ld", (long)bonus);
                    if (draw) draw_level_message(rp, gs.level, buf);
                }
                gs.state_timer--;
                if (gs.state_timer <= 0) {
                    gs.score += gs.humans_alive * SCORE_WAVE_BONUS;
                    if (gs.score > gs.hiscore)
                        gs.hiscore = gs.score;
                    gs.level++;
                    gs.state = STATE_LEVEL_START;
                    gs.state_timer = 45;
                    game_spawn_wave(&gs);
                }
                break;

            case STATE_GAMEOVER:
                if (draw) draw_all(rp, &gs);
                if (draw) draw_gameover(rp, &gs);
                gs.state_timer--;
                if (gs.state_timer <= 0 || (input & (INPUT_START | INPUT_FIRE))) {
                    entry_rank = score_qualifies(&score_table, gs.score);
                    if (entry_rank >= 0) {
                        gs.state = STATE_HISCORE_ENTRY;
                        gs.state_timer = 0;
                        entry_cursor = 0;
                        memset(entry_name, 0, HISCORE_NAMELEN);
                        entry_name[0] = 'A';
                    } else {
                        gs.state = STATE_HISCORE_VIEW;
                        gs.state_timer = 300;
                    }
                }
                break;

            case STATE_HISCORE_ENTRY:
                if (draw) draw_all(rp, &gs);
                if (draw) draw_hiscore_entry(rp, &gs, entry_name, entry_cursor);
                /* Simple letter picker: left/right to change letter, fire to confirm */
                if (input & INPUT_UP) {
                    if (entry_name[entry_cursor] < 'Z')
                        entry_name[entry_cursor]++;
                    else
                        entry_name[entry_cursor] = 'A';
                }
                if (input & INPUT_DOWN) {
                    if (entry_name[entry_cursor] > 'A')
                        entry_name[entry_cursor]--;
                    else
                        entry_name[entry_cursor] = 'Z';
                }
                if (input & INPUT_RIGHT) {
                    if (entry_cursor < HISCORE_NAMELEN - 2) {
                        entry_cursor++;
                        if (entry_name[entry_cursor] == 0)
                            entry_name[entry_cursor] = 'A';
                    }
                }
                if (input & INPUT_LEFT) {
                    if (entry_cursor > 0) entry_cursor--;
                }
                if ((input & INPUT_FIRE) && gs.state_timer == 0) {
                    gs.state_timer = 10; /* debounce */
                }
                if (gs.state_timer > 0) {
                    gs.state_timer--;
                    if (gs.state_timer == 0 && !(input & INPUT_FIRE)) {
                        /* Confirm on fire release after debounce */
                        score_insert(&score_table, entry_rank, entry_name, gs.score);
                        score_save(&score_table);
                        gs.state = STATE_HISCORE_VIEW;
                        gs.state_timer = 300;
                    }
                }
                break;

            case STATE_HISCORE_VIEW:
                if (draw) SetRast(rp, COL_BLACK);
                if (draw) draw_hiscore_table(rp, &score_table);
                gs.state_timer--;
                if (gs.state_timer <= 0 || (input & (INPUT_START | INPUT_FIRE))) {
                    gs.state = STATE_TITLE;
                    gs.frame = 0;
                    startup_delay = 20;
                }
                break;
            }
            sound_update(&gs);
            gs.frame++;
            input &= (WORD)~(INPUT_BOMB | INPUT_HYPER);   /* one-shot */
        }

        if (gs.state != last_state || gs.level != last_level) {
            AB_I("state=%s level=%d score=%ld lives=%d", state_name(gs.state), (int)gs.level,
                 (long)gs.score, (int)gs.lives);
            last_state = gs.state;
            last_level = gs.level;
        }
        if (gs.score != last_score) {
            last_score = gs.score;
            AB_I("score=%ld humans=%d", (long)gs.score, (int)gs.humans_alive);
        }
        gfx_swap();
    }

    AB_I("exit score=%ld", (long)gs.score);
    sound_stop_music();
    sound_cleanup();
    gfx_exit();
    return 0;
}
