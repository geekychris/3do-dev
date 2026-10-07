/*
 * FRANK THE FROG - 3DO port.
 *
 * playfield.c, lanes.c, frank.c, score.c, sound.c and modplay.c are the
 * Amiga sources (geekychris/amiga_games), drawing through the Amiga
 * graphics API in sdk/amiga and playing the generated music through its
 * Paula emulation. This file replaces main.c and gfx.c: the title and
 * overlay drawing are copied from main.c, the state machine is main.c's
 * loop body split into one 50 Hz game_step() (as in the Atari ST port) so
 * gfx_steps() can keep the game at full speed while drawing catches up.
 *
 * Controls (3DO pad): D-pad hops, A or P starts, X quits to the menu.
 */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <stdio.h>
#include <string.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "game.h"
#include "playfield.h"
#include "frank.h"
#include "lanes.h"
#include "score.h"
#include "sound.h"
#include "modplay.h"

/* Amiga palette from gfx.c (12 bit $0RGB) */
static const UWORD palette[16] = {
	0x000, 0x070, 0x333, 0x00B, 0x0E0, 0x060, 0x830, 0xD00,
	0xED0, 0x08D, 0x606, 0x050, 0x8E4, 0xFFF, 0x003, 0xCC0,
};

static int game_state = STATE_TITLE;
static int transition_frames = 0;

/* ---- from the Amiga main.c ---- */
static void draw_title(struct RastPort *rp)
{
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 0, 0, SCREEN_W - 1, SCREEN_H - 1);

    /* Big frog face */
    SetAPen(rp, (long)COL_FROG);
    RectFill(rp, 130, 40, 190, 90);
    /* Eyes */
    SetAPen(rp, (long)COL_WHITE);
    RectFill(rp, 135, 42, 148, 55);
    RectFill(rp, 170, 42, 183, 55);
    /* Pupils */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 140, 46, 144, 51);
    RectFill(rp, 175, 46, 179, 51);
    /* Mouth */
    SetAPen(rp, (long)COL_FROG_DARK);
    Move(rp, 145, 75);
    Draw(rp, 155, 80);
    Draw(rp, 165, 75);
    /* Nostrils */
    WritePixel(rp, 150, 65);
    WritePixel(rp, 168, 65);

    /* Title text */
    SetAPen(rp, (long)COL_FROG);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 14 * 8) / 2, 110);
    Text(rp, (CONST_STRPTR)"FRANK THE FROG", 14L);

    SetAPen(rp, (long)COL_WHITE);
    Move(rp, (SCREEN_W - 16 * 8) / 2, 150);
    Text(rp, (CONST_STRPTR)"Press A to start", 16L);

    /* Controls help */
    SetAPen(rp, (long)COL_ROAD_LINE);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 180);
    Text(rp, (CONST_STRPTR)"D-pad hops          ", 20L);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 195);
    Text(rp, (CONST_STRPTR)"A or P to start     ", 20L);
    Move(rp, (SCREEN_W - 24 * 8) / 2, 210);
    Text(rp, (CONST_STRPTR)"X quits to the menu ", 20L);
}

static void draw_gameover(struct RastPort *rp)
{
    char buf[32];

    /* Semi-transparent overlay effect */
    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 60, 80, 260, 170);
    SetAPen(rp, (long)COL_WHITE);
    Move(rp, 60, 80); Draw(rp, 260, 80);
    Draw(rp, 260, 170); Draw(rp, 60, 170); Draw(rp, 60, 80);

    SetAPen(rp, (long)COL_CAR_RED);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 9 * 8) / 2, 105);
    Text(rp, (CONST_STRPTR)"GAME OVER", 9L);

    sprintf(buf, "SCORE: %06ld", (long)score_get());
    SetAPen(rp, (long)COL_WHITE);
    Move(rp, (SCREEN_W - (int)strlen(buf) * 8) / 2, 130);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));

    SetAPen(rp, (long)COL_ROAD_LINE);
    Move(rp, (SCREEN_W - 20 * 8) / 2, 155);
    Text(rp, (CONST_STRPTR)"Press A to retry    ", 20L);
}

static void draw_level_complete(struct RastPort *rp)
{
    char buf[32];

    SetAPen(rp, (long)COL_BG);
    RectFill(rp, 80, 90, 240, 150);
    SetAPen(rp, (long)COL_FROG);
    Move(rp, 80, 90); Draw(rp, 240, 90);
    Draw(rp, 240, 150); Draw(rp, 80, 150); Draw(rp, 80, 90);

    SetAPen(rp, (long)COL_FROG);
    SetBPen(rp, (long)COL_BG);
    Move(rp, (SCREEN_W - 14 * 8) / 2, 115);
    Text(rp, (CONST_STRPTR)"LEVEL COMPLETE", 14L);

    sprintf(buf, "+1000 BONUS!");
    SetAPen(rp, (long)COL_CAR_YELLOW);
    Move(rp, (SCREEN_W - 12 * 8) / 2, 140);
    Text(rp, (CONST_STRPTR)buf, (long)strlen(buf));
}

/* ---- end of copied drawing ---- */

/* one 50 Hz game step: the Amiga main loop body minus drawing (from the
 * Atari ST port's split of it) */
static void game_step(int joy_dx, int joy_dy, int joy_fire, int *prev_fire)
{
    int death_done, ride_dx;

    switch (game_state) {
    case STATE_TITLE:
        if (joy_fire && !*prev_fire) {
            game_state = STATE_PLAYING;
            score_init();
            frank_init();
            lanes_init(1);
            lanes_reset_home();
            AB_I("START_GAME");
        }
        break;

    case STATE_PLAYING:
        if (joy_dx || joy_dy) {
            int old_highest = frank_highest_row();
            frank_move(joy_dx, joy_dy);
            sound_hop();
            if (frank_highest_row() < old_highest)
                score_add(10);
        }
        lanes_tick();
        if (frank_row() >= ROW_RIVER_5 && frank_row() <= ROW_RIVER_1) {
            if (!lanes_check_river(frank_x(), frank_y(), &ride_dx)) {
                AB_I("SPLASH row=%ld col=%ld", frank_row(), frank_col());
                frank_start_death();
                game_state = STATE_DYING;
                sound_splash();
            } else if (ride_dx != 0) {
                int new_x = frank_x() + ride_dx;
                if (new_x < -4 || new_x + TILE_W > SCREEN_W + 4) {
                    AB_I("CARRIED_OFF");
                    frank_start_death();
                    game_state = STATE_DYING;
                    sound_splash();
                } else {
                    frank_set_x(new_x);
                }
            }
        }
        if (game_state == STATE_PLAYING &&
            frank_row() >= ROW_ROAD_5 && frank_row() <= ROW_ROAD_1) {
            if (lanes_check_car(frank_x(), frank_y())) {
                AB_I("SPLAT row=%ld col=%ld", frank_row(), frank_col());
                frank_start_death();
                game_state = STATE_DYING;
                sound_splat();
            }
        }
        if (game_state == STATE_PLAYING && frank_row() == ROW_HOME) {
            int slot = lanes_check_home(frank_x());
            if (slot >= 0) {
                lanes_fill_home(slot);
                score_add(50);
                sound_home();
                AB_I("HOME slot=%ld score=%ld", slot, score_get());
                if (lanes_all_home()) {
                    game_state = STATE_LEVEL;
                    transition_frames = 60;
                    sound_levelup();
                    AB_I("LEVEL_COMPLETE level=%ld", score_level());
                } else {
                    frank_init();
                }
            } else {
                frank_start_death();
                game_state = STATE_DYING;
                sound_splash();
            }
        }
        break;

    case STATE_DYING:
        death_done = !frank_die_tick();
        lanes_tick();
        if (death_done) {
            score_lose_life();
            if (score_lives() <= 0) {
                game_state = STATE_GAMEOVER;
                transition_frames = 30;
                sound_gameover();
                AB_I("GAME_OVER score=%ld", score_get());
            } else {
                frank_init();
                game_state = STATE_PLAYING;
            }
        }
        break;

    case STATE_LEVEL:
        transition_frames--;
        if (transition_frames <= 0) {
            score_next_level();
            lanes_reset_home();
            lanes_init(score_level());
            frank_init();
            game_state = STATE_PLAYING;
            AB_I("LEVEL level=%ld", score_level());
        }
        break;

    case STATE_GAMEOVER:
        lanes_tick();
        if (transition_frames > 0)
            transition_frames--;
        else if (joy_fire && !*prev_fire)
            game_state = STATE_TITLE;
        break;
    }
    *prev_fire = joy_fire;
}

/* drawing, as the Amiga main loop does it per state */
static void draw_frame(struct RastPort *rp)
{
    static int title_drawn = 0;
    if (game_state == STATE_TITLE) {
        /* the Amiga version draws the title once (on both buffers) */
        if (!title_drawn)
            draw_title(rp);
        title_drawn = 1;
        return;
    }
    title_drawn = 0;
    playfield_draw(rp);
    lanes_draw(rp);
    if (game_state == STATE_PLAYING || game_state == STATE_DYING)
        frank_draw(rp);
    score_draw(rp);
    if (game_state == STATE_LEVEL)
        draw_level_complete(rp);
    else if (game_state == STATE_GAMEOVER)
        draw_gameover(rp);
}

int main(void)
{
    static const char *names[] = { "TITLE", "PLAYING", "DYING", "LEVEL", "GAMEOVER" };
    int prev_fire = 0, last_state = -1, steps;

    ab_init("FROG");
    if (!gfx_init(palette, 16))
        return 1;
    if (sound_init() != 0)
        AB_W("sound init failed");
    modplay_start();
    score_init();
    frank_init();
    lanes_init(1);

    for (;;) {
        ULONG pressed = pad_pressed(0), held = pad_held(0);
        int dx = 0, dy = 0, fire;
        if (amiga_quit_requested())
            break;
        /* hops are edge triggered, like the Amiga keyboard/joystick code */
        if (pressed & PAD_LEFT)  dx = -1;
        if (pressed & PAD_RIGHT) dx = 1;
        if (pressed & PAD_UP)    dy = -1;
        if (pressed & PAD_DOWN)  dy = 1;
        fire = (held & (PAD_A | PAD_B | PAD_C | PAD_P)) ? 1 : 0;

        steps = gfx_steps();
        while (steps-- > 0) {
            game_step(dx, dy, fire, &prev_fire);
            dx = dy = 0;            /* a hop happens once */
            modplay_tick();
        }
        if (game_state != last_state) {
            AB_I("state=%s score=%ld lives=%ld level=%ld", names[game_state],
                 (long)score_get(), (long)score_lives(), (long)score_level());
            last_state = game_state;
        }
        draw_frame(gfx_back());
        gfx_swap();
    }

    AB_I("exit score=%ld", (long)score_get());
    modplay_stop();
    sound_cleanup();
    gfx_exit();
    return 0;
}
