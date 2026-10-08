/*
 * 3DO ARCADE - game selector for the ported Amiga / Atari games.
 *
 * The game list is generated at build time from projects/<game>/arcade.txt
 * (see the Makefile). Every game lives on this disc as
 * $boot/<game>/<game> with its data next to it.
 *
 * Controls: UP/DOWN choose, A / C / P play. Inside a game, X returns here.
 */
#include <string.h>
#include <exec/types.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "games_gen.h"     /* GAMES[], NUM_GAMES (generated) */

int arcade_launch(const char *path);

static UWORD palette[16] = {
    0x000, 0xFFF, 0x113, 0x225, 0xD75, 0xFC6, 0x8AF, 0x58C,
    0xFE0, 0x0D8, 0xF44, 0x888, 0x444, 0x336, 0xAAA, 0x559
};
#define C_BG     0
#define C_WHITE  1
#define C_BAND1  2
#define C_BAND2  3
#define C_CORAL  4
#define C_GOLD   5
#define C_SKY    6
#define C_DIMSKY 7
#define C_YELLOW 8
#define C_GREEN  9
#define C_GREY   11
#define C_DARK   12
#define C_SEL    13
#define C_LIGHT  14

#define VISIBLE  12
/* The menu is drawn 1:1 (lines VIEW_Y0..VIEW_Y0+239 of the 256-line
 * screen): the default 256 -> 240 scaling drops a line in 16, which
 * clips rows of text. */
#define VIEW_Y0  8
#define LIST_Y   52                     /* list band top */
#define DESC_Y   (LIST_Y + VISIBLE * 11 + 7)
#define STARS    40

static WORD star_x[STARS], star_y[STARS], star_s[STARS];
static ULONG rng = 12345;

static WORD rnd(WORD n)
{
    rng = rng * 1103515245UL + 12345UL;
    return (WORD)(((rng >> 16) & 0x7FFF) % n);
}

static void text_at(struct RastPort *rp, LONG x, LONG y, const char *s, int pen)
{
    SetAPen(rp, pen);
    SetDrMd(rp, JAM1);
    Move(rp, x, y + 6);
    Text(rp, s, strlen(s));
}

/* Word-wrap a description into lines of at most `cols` characters and
 * draw lines skip..skip+maxlines-1. Returns the total number of lines. */
static int text_wrapped(struct RastPort *rp, LONG x, LONG y, const char *s, int cols, int pen,
                        int skip, int maxlines)
{
    char line[48];
    int lines = 0;
    while (*s) {
        int n = (int)strlen(s), cut = n;
        if (n > cols) {
            cut = cols;
            while (cut > 0 && s[cut] != ' ')
                cut--;
            if (cut == 0)
                cut = cols;
        }
        memcpy(line, s, cut);
        line[cut] = 0;
        if (lines >= skip && lines < skip + maxlines)
            text_at(rp, x, y + (lines - skip) * 10, line, pen);
        s += cut;
        while (*s == ' ')
            s++;
        lines++;
    }
    return lines;
}

static void draw_background(struct RastPort *rp, ULONG t)
{
    int i;
    SetRast(rp, C_BG);
    for (i = 0; i < STARS; i++) {
        WORD x = (WORD)((star_x[i] - (LONG)(t * star_s[i]) / 2) % 320);
        if (x < 0)
            x += 320;
        SetAPen(rp, star_s[i] > 2 ? C_LIGHT : C_DARK);
        WritePixel(rp, x, star_y[i]);
    }
}

static int menu(int sel)
{
    struct RastPort *rp;
    int top = 0, i;
    ULONG t = 0;
    int desc_sel = -1, desc_lines = 3;
    ULONG desc_t = 0;

    if (!gfx_init(palette, 16))
        return -1;
    gfx_set_view(GFX_VIEW_CROP, VIEW_Y0);
    rp = gfx_back();
    AB_I("menu games=%d selected=%s", NUM_GAMES, NUM_GAMES ? GAMES[sel].name : "-");

    for (;;) {
        ULONG pressed = pad_pressed(0);

        if (NUM_GAMES) {
            if (pressed & PAD_UP)
                sel = (sel + NUM_GAMES - 1) % NUM_GAMES;
            if (pressed & PAD_DOWN)
                sel = (sel + 1) % NUM_GAMES;
            if (pressed & PAD_LEFT)
                sel = sel >= VISIBLE ? sel - VISIBLE : 0;
            if (pressed & PAD_RIGHT)
                sel = sel + VISIBLE < NUM_GAMES ? sel + VISIBLE : NUM_GAMES - 1;
            if (pressed & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT))
                AB_I("select %s", GAMES[sel].name);
            if (pressed & (PAD_A | PAD_C | PAD_P))
                break;
        }
        if (sel < top)
            top = sel;
        if (sel >= top + VISIBLE)
            top = sel - VISIBLE + 1;

        draw_background(rp, t);

        /* title */
        SetAPen(rp, C_CORAL);
        gfx_text_big(rp, 160 - 5 * 16 / 2 * 2 + 1, 13, "3DO", 3);
        SetAPen(rp, C_GOLD);
        gfx_text_big(rp, 160 - 5 * 16 / 2 * 2, 12, "3DO", 3);
        SetAPen(rp, C_SKY);
        gfx_text_big(rp, 160 - 3 * 16 / 2 * 2 + 64, 16, "ARCADE", 2);
        text_at(rp, 160 - 28 * 4, 40, "AMIGA CLASSICS BY GEEKYCHRIS", C_DIMSKY);

        /* list */
        SetAPen(rp, C_BAND1);
        RectFill(rp, 16, LIST_Y, 303, LIST_Y + VISIBLE * 11 + 3);
        for (i = 0; i < VISIBLE && top + i < NUM_GAMES; i++) {
            int g = top + i;
            LONG y = LIST_Y + 3 + i * 11;
            if (g == sel) {
                SetAPen(rp, C_SEL);
                RectFill(rp, 18, y - 1, 301, y + 9);
                text_at(rp, 24, y + 1, ">", C_YELLOW);
            }
            text_at(rp, 40, y + 1, GAMES[g].title, g == sel ? C_YELLOW : C_WHITE);
            text_at(rp, 296 - 8 * (LONG)strlen(GAMES[g].genre), y + 1, GAMES[g].genre,
                    g == sel ? C_GOLD : C_GREY);
        }
        if (NUM_GAMES > VISIBLE) {
            /* scrollbar right of the list so it's clear there are more games */
            LONG track = VISIBLE * 11 + 3;
            LONG th = track * VISIBLE / NUM_GAMES;
            LONG ty = LIST_Y + (track - th) * top / (NUM_GAMES - VISIBLE);
            SetAPen(rp, C_BAND2);
            RectFill(rp, 305, LIST_Y, 308, LIST_Y + track);
            SetAPen(rp, C_GOLD);
            RectFill(rp, 305, ty, 308, ty + th - 1);
        }

        /* description */
        if (NUM_GAMES) {
            SetAPen(rp, C_BAND2);
            RectFill(rp, 16, DESC_Y, 303, DESC_Y + 36);
            /* long descriptions (story, then controls) turn a page of
             * three lines every 3 s, from the top for each new choice */
            if (sel != desc_sel) {
                desc_sel = sel;
                desc_t = 0;
            }
            {
                int pages = (desc_lines + 2) / 3;
                int page = (int)((desc_t / 150) % (ULONG)(pages > 0 ? pages : 1));
                desc_lines = text_wrapped(rp, 22, DESC_Y + 4, GAMES[sel].desc, 35, C_LIGHT,
                                          3 * page, 3);
            }
            desc_t += gfx_steps();          /* 50 per second, whatever the frame rate */
        } else {
            text_at(rp, 40, 120, "NO GAMES ON THIS DISC", C_CORAL);
        }
        text_at(rp, 160 - 36 * 4, DESC_Y + 42, "UP/DOWN CHOOSE  A PLAY  X QUITS GAME", ((t >> 4) & 1) ? C_GREEN : C_DIMSKY);

        gfx_swap();
        t++;
    }
    gfx_exit();
    return sel;
}

int main(void)
{
    int sel = 0, i;
    char path[80];

    ab_init("ARCADE");
    for (i = 0; i < STARS; i++) {
        star_x[i] = rnd(320);
        star_y[i] = rnd(256);
        star_s[i] = (WORD)(1 + rnd(4));
    }
    for (;;) {
        sel = menu(sel);
        if (sel < 0)
            return 1;
        sprintf(path, "$boot/%s/%s", GAMES[sel].name, GAMES[sel].name);
        AB_I("run %s", GAMES[sel].name);
        arcade_launch(path);
    }
    return 0;
}
