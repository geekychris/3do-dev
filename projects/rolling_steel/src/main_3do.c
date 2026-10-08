/*
 * ROLLING STEEL - 3DO version of geekychris/rolling_steel (Unity, C#).
 *
 * An isometric roll-a-marble-downhill game after the 1984 Atari cabinet:
 * six descending courses, one shared clock, no jump button, no brakes.
 * The courses are the Unity game's own files, built into geometry by
 * tools/courses.py; the marble's physics (phys.c) and the 3D (render.c,
 * an orthographic camera you can turn, tilt and zoom, drawn by the cel
 * engine) are written for the 3DO. Rules in game.c; this file is the main
 * loop, the HUD (Hud.cs) and best times in NVRAM.
 *
 * Pad: D-pad pushes the marble (relative to the camera), L/R turn the view
 * (tap: 45 degrees, hold: spin), A/B zoom in/out, C + up/down tilt, P pause,
 * X quits to the menu. Title: up/down course, left/right one or two players
 * (the second on pad 2), C music, A start.
 */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "game.h"

#define PEN_CLEAR 255
enum { P_WHITE = 1, P_AMBER, P_CYAN, P_RED, P_GREEN, P_DIM, P_GOLD, P_SILVER, P_BRONZE, P_SHADOW,
       P_ORANGE, P_BAR, P_SEL, P_FAINT };

static struct RastPort *rp;

void rs_log(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    AB_I("%s", buf);
}

void *rs_load(const char *name, long *size)
{
    LONG n = 0;
    void *p = amiga_load_file((CONST_STRPTR)name, &n);
    *size = n;
    return p;
}

void rs_free(void *p) { FreeVec(p); }
void *rs_alloc(long bytes) { return AllocVec(bytes, MEMF_ANY); }

/* best times, in NVRAM */
void rs_save_progress(void)
{
    BPTR f = Open((CONST_STRPTR)"rollingsteel.prog", MODE_NEWFILE);
    if (!f) return;
    Write(f, G.best, sizeof(G.best));
    Write(f, &G.best_run, 4);
    Write(f, &G.best_run_falls, 4);
    Close(f);
}

void rs_load_progress(void)
{
    BPTR f = Open((CONST_STRPTR)"rollingsteel.prog", MODE_OLDFILE);
    if (!f) return;
    if (Read(f, G.best, sizeof(G.best)) != sizeof(G.best)) memset(G.best, 0, sizeof(G.best));
    Read(f, &G.best_run, 4);
    Read(f, &G.best_run_falls, 4);
    Close(f);
}

/* ---- the HUD: listed each frame, redrawn only where it changed ---- */

enum { HI_TEXT, HI_BIG, HI_BAR };
typedef struct {
    short type, x, y, a, pen;
    short x0, y0, x1, y1;
    char s[44];
} HudItem;
#define MAXHUD 48
static HudItem hl[2][MAXHUD];
static int nhud[2], cur;

static HudItem *hud_add(int type, int x, int y, int a, int pen, const char *s)
{
    HudItem *h;
    if (nhud[cur] >= MAXHUD) return 0;
    h = &hl[cur][nhud[cur]++];
    h->type = (short)type; h->x = (short)x; h->y = (short)y; h->a = (short)a; h->pen = (short)pen;
    h->s[0] = 0;
    if (s) { strncpy(h->s, s, sizeof(h->s) - 1); h->s[sizeof(h->s) - 1] = 0; }
    return h;
}

static void set_rect(HudItem *h, int x0, int y0, int x1, int y1)
{
    if (!h) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GFX_WIDTH - 1) x1 = GFX_WIDTH - 1;
    if (y1 > 239) y1 = 239;
    h->x0 = (short)x0; h->y0 = (short)y0; h->x1 = (short)x1; h->y1 = (short)y1;
}

static void text(int x, int y, const char *s, int pen)
{
    set_rect(hud_add(HI_TEXT, x, y, 0, pen, s), x, y - 1, x + 8 * (int)strlen(s), y + 8);
}
static void ctext(int cx, int y, const char *s, int pen) { text(cx - 4 * (int)strlen(s), y, s, pen); }
static void rtext(int rx, int y, const char *s, int pen) { text(rx - 8 * (int)strlen(s), y, s, pen); }

static void big(int cx, int y, const char *s, int pen)
{
    int w = 16 * (int)strlen(s), x = cx - w / 2;
    set_rect(hud_add(HI_BIG, x, y, 2, pen, s), x, y, x + w + 1, y + 17);
}

static void bar(int x0, int y0, int x1, int y1, int pen)
{
    HudItem *h = hud_add(HI_BAR, x0, y0, 0, pen, 0);
    if (h) { h->a = (short)x1; set_rect(h, x0, y0, x1, y1); }
}

static int same(const HudItem *a, const HudItem *b)
{
    return a->x == b->x && a->y == b->y && a->type == b->type && a->a == b->a && a->pen == b->pen &&
           a->y1 == b->y1 && strcmp(a->s, b->s) == 0;
}

static int overlap(const HudItem *a, const HudItem *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

static void hud_draw(const HudItem *h)
{
    switch (h->type) {
    case HI_TEXT:
        SetAPen(rp, P_SHADOW);
        SetDrMd(rp, JAM1);
        Move(rp, h->x + 1, h->y + 7);
        Text(rp, (STRPTR)h->s, strlen(h->s));
        SetAPen(rp, h->pen);
        Move(rp, h->x, h->y + 6);
        Text(rp, (STRPTR)h->s, strlen(h->s));
        break;
    case HI_BIG:
        SetAPen(rp, P_SHADOW);
        gfx_text_big(rp, h->x + 2, h->y + 2, h->s, h->a);
        SetAPen(rp, h->pen);
        gfx_text_big(rp, h->x, h->y, h->s, h->a);
        break;
    case HI_BAR:
        SetAPen(rp, h->pen);
        RectFill(rp, h->x0, h->y0, h->x1, h->y1);
        break;
    }
}

static void hud_begin(void) { cur ^= 1; nhud[cur] = 0; }

static void hud_end(void)
{
    int prev = cur ^ 1, i, j;
    static unsigned char keep[MAXHUD], redraw[MAXHUD];
    for (i = 0; i < nhud[prev]; i++) {
        keep[i] = 0;
        for (j = 0; j < nhud[cur]; j++)
            if (same(&hl[prev][i], &hl[cur][j])) { keep[i] = 1; break; }
    }
    for (j = 0; j < nhud[cur]; j++) {
        redraw[j] = 1;
        for (i = 0; i < nhud[prev]; i++)
            if (keep[i] && same(&hl[prev][i], &hl[cur][j])) { redraw[j] = 0; break; }
    }
    SetAPen(rp, PEN_CLEAR);
    for (i = 0; i < nhud[prev]; i++)
        if (!keep[i]) {
            HudItem *o = &hl[prev][i];
            SetAPen(rp, PEN_CLEAR);
            RectFill(rp, o->x0, o->y0, o->x1, o->y1);
            for (j = 0; j < nhud[cur]; j++)
                if (!redraw[j] && overlap(o, &hl[cur][j])) redraw[j] = 1;
        }
    for (j = 0; j < nhud[cur]; j++)
        if (redraw[j]) {
            int k;
            for (k = j + 1; k < nhud[cur]; k++)
                if (!redraw[k] && overlap(&hl[cur][j], &hl[cur][k])) redraw[k] = 1;
        }
    for (j = 0; j < nhud[cur]; j++)
        if (redraw[j]) hud_draw(&hl[cur][j]);
}

/* Progress.Format: mm:ss.s, or a dash */
static void fmt_time(char *buf, long steps)
{
    long t = steps * 2;                                     /* hundredths */
    if (steps <= 0) { strcpy(buf, "--:--"); return; }
    if (t >= 6000) sprintf(buf, "%ld:%02ld.%ld", t / 6000, (t % 6000) / 100, (t % 100) / 10);
    else sprintf(buf, "%ld.%ld", t / 100, (t % 100) / 10);
}

static const char *medal_name(int m) { return m == MEDAL_GOLD ? "GOLD" : m == MEDAL_SILVER ? "SILVER" : m == MEDAL_BRONZE ? "BRONZE" : ""; }
static int medal_pen(int m) { return m == MEDAL_GOLD ? P_GOLD : m == MEDAL_SILVER ? P_SILVER : m == MEDAL_BRONZE ? P_BRONZE : P_FAINT; }

static int blink(void) { return (G.clock / 18) % 3 != 2; }

static void banner(int x0, int w, const char *title, int pen, const char *sub)
{
    render_shade(x0, 92, x0 + w, 146, 4);
    big(x0 + w / 2, 100, title, pen);
    if (sub && sub[0]) ctext(x0 + w / 2, 128, sub, P_WHITE);
}

static void hud_player(int i, int x0, int w)
{
    Player *p = &G.p[i];
    char buf[48], t[16];
    int two = G.nplayers > 1;
    long pr;
    render_shade(x0, 0, x0 + w, 20, 4);
    if (two) sprintf(buf, "P%d", i + 1);
    else sprintf(buf, "%d/%d %s", G.level + 1, course_count, C.name);
    if (two && p->wins) sprintf(buf + strlen(buf), " WON %d", p->wins);
    text(x0 + 4, 6, buf, two ? (i ? P_ORANGE : P_CYAN) : P_CYAN);
    sprintf(t, "%ld.%ld", p->time_left / 50, (p->time_left % 50) / 5);
    if (two) ctext(x0 + w / 2 + 8, 6, t, p->time_left <= 500 ? P_RED : P_AMBER);
    else big(x0 + 200, 2, t, p->time_left <= 500 ? P_RED : P_AMBER);
    sprintf(buf, "FALLS %d", p->deaths);
    rtext(x0 + w - 4, 6, two ? buf + 6 : buf, P_AMBER);
    /* progress along the route */
    pr = course_progress(p);
    bar(x0 + w / 4, 23, x0 + w * 3 / 4 - 1, 24, P_BAR);
    if (pr > 0) bar(x0 + w / 4, 23, x0 + w / 4 + (int)(((w / 2 - 1) * pr) >> 16), 24, two && i ? P_ORANGE : P_CYAN);
    fmt_time(t, p->course_time);
    if (!two && G.best[G.level] > 0) {
        char b[16];
        fmt_time(b, G.best[G.level]);
        sprintf(buf, "%s   BEST %s", t, b);
    } else strcpy(buf, t);
    ctext(x0 + w / 2, 29, buf, P_DIM);
    if (p->dying) banner(x0, w, p->death_reason, P_RED, "-3 SECONDS");
    else if (p->out_of_time && G.state == ST_PLAYING) banner(x0, w, "OUT OF TIME", P_RED, "");
    else if (p->finished && G.state == ST_PLAYING) { fmt_time(t, p->finish_time); banner(x0, w, "FINISHED", P_GREEN, t); }
}

static void hud_title(void)
{
    char buf[48], t[16];
    int i;
    render_shade(0, 0, 320, 240, 5);
    big(160, 10, "ROLLING STEEL", P_AMBER);
    ctext(160, 32, "SIX COURSES, ONE CLOCK", P_CYAN);
    for (i = 0; i < course_count; i++) {
        static const char *names[6] = { "PRACTICE", "BEGINNER", "INTERMEDIATE", "AERIAL", "SILLY", "ULTIMATE" };
        int y = 50 + i * 12, sel = i == G.title_select;
        if (sel) bar(36, y - 2, 284, y + 8, P_SEL);
        sprintf(buf, "%d  %s", i + 1, names[i]);
        text(44, y, buf, sel ? P_WHITE : P_DIM);
        fmt_time(t, G.best[i]);
        rtext(222, y, t, G.best[i] > 0 ? P_AMBER : P_FAINT);
        if (G.best[i] > 0 && i == G.level) text(230, y, medal_name(medal_for(G.best[i])), medal_pen(medal_for(G.best[i])));
    }
    ctext(160, 128, G.want_players > 1 ? "TWO PLAYERS - PADS 1 AND 2" : "ONE PLAYER",
          G.want_players > 1 ? P_ORANGE : P_CYAN);
    if (G.best_run > 0) {
        fmt_time(t, G.best_run);
        sprintf(buf, "BEST FULL RUN %s (%ld FALLS)", t, G.best_run_falls);
        ctext(160, 140, buf, P_CYAN);
    }
    ctext(160, 156, "UP/DOWN COURSE  LEFT/RIGHT PLAYERS", P_DIM);
    ctext(160, 167, "L/R TURN  A/B ZOOM  C+UP/DOWN TILT", P_FAINT);
    ctext(160, 178, G.music_on ? "C MUSIC: ON   P PAUSE" : "C MUSIC: OFF   P PAUSE", P_FAINT);
    if (blink()) big(160, 204, "PRESS A TO START", P_GREEN);
}

static void hud(void)
{
    char buf[64], t[16];
    int i;
    hud_begin();
    if (G.state == ST_TITLE) hud_title();
    else {
        if (G.nplayers > 1) {
            for (i = 0; i < 2; i++) hud_player(i, i * 160, 160);
            bar(159, 0, 160, 239, P_SHADOW);
        } else hud_player(0, 0, 320);
        if (G.state == ST_CLEAR) {
            if (G.nplayers > 1) {
                fmt_time(t, G.last_course_time);
                sprintf(buf, "%s - P1 %d - %d P2", t, G.p[0].wins, G.p[1].wins);
                banner(0, 320, G.last_winner >= 0 ? (G.last_winner ? "P2 TAKES IT" : "P1 TAKES IT") : "COURSE CLEAR",
                       G.last_winner == 1 ? P_ORANGE : G.last_winner == 0 ? P_CYAN : P_GREEN, buf);
            } else {
                fmt_time(t, G.last_course_time);
                sprintf(buf, "%s %s%s", t, medal_name(G.last_medal), G.last_was_best ? " - NEW BEST" : "");
                banner(0, 320, "COURSE CLEAR", G.last_medal ? medal_pen(G.last_medal) : P_GREEN, buf);
            }
        } else if (G.state == ST_WON) {
            if (G.nplayers > 1) {
                const char *who = G.p[0].wins == G.p[1].wins ? "A DRAW" : G.p[0].wins > G.p[1].wins ? "P1 WINS" : "P2 WINS";
                sprintf(buf, "P1 %d - %d P2 - PRESS A", G.p[0].wins, G.p[1].wins);
                banner(0, 320, who, G.p[0].wins >= G.p[1].wins ? P_CYAN : P_ORANGE, buf);
            } else {
                fmt_time(t, G.p[0].run_time);
                sprintf(buf, "%s FALLS %d%s", t, G.p[0].deaths, G.last_was_best ? " - NEW BEST RUN" : "");
                banner(0, 320, "ALL CLEAR", P_GREEN, buf);
            }
        } else if (G.state == ST_GAMEOVER)
            banner(0, 320, "OUT OF TIME", P_RED, "PRESS A TO TRY AGAIN");
        if (G.paused) banner(0, 320, "PAUSED", P_WHITE, "P TO CARRY ON");
        if (G.demo && blink()) ctext(160, 226, "DEMO - PRESS A BUTTON", P_DIM);
    }
    hud_end();
}

int main(void)
{
    ULONG frames = 0;
    static const ULONG pens[] = { 0, 0xF2F2F2, 0xFFC740, 0x73D9FF, 0xFF594D, 0x73FF80, 0xB3B3C0, 0xFFD140,
                                  0xD1DBEB, 0xD98C4D, 0x060608, 0xFF9E59, 0x404050, 0x30507A, 0x6A6A78 };
    int i;
    ab_init("ROLL");
    amiga_set_progdir("rolling_steel");
    if (!gfx_init_mode(GFX_RGB16, 240, 0, 0))
        return 1;
    for (i = 1; i < (int)(sizeof(pens) / sizeof(pens[0])); i++) gfx_set_rgb24(i, pens[i]);
    rp = gfx_back();
    SetRast(rp, P_SHADOW);
    SetAPen(rp, P_AMBER);
    gfx_text_big(rp, 160 - 13 * 8, 100, "ROLLING STEEL", 2);
    SetAPen(rp, P_DIM);
    SetDrMd(rp, JAM1);
    Move(rp, 160 - 4 * 10, 136);
    Text(rp, (STRPTR)"LOADING...", 10);
    gfx_swap();
    gfx_set_transparent_pen(PEN_CLEAR);
    gfx_set_max_steps(6);
    if (!render_init()) {
        AB_E("no cels");
        return 1;
    }
    if (!snd_init())
        AB_W("no sound effects");
    game_init();
    rp = gfx_back();
    SetAPen(rp, PEN_CLEAR);
    RectFill(rp, 0, 0, GFX_WIDTH - 1, 239);
    AB_I("ready courses=%ld", (long)course_count);

    while (!amiga_quit_requested()) {
        int steps = gfx_steps();
        ULONG h0 = pad_held(0), p0 = pad_pressed(0), h1 = pad_held(1), p1 = pad_pressed(1);
        void *list;
        int views;
        while (steps-- > 0) {
            game_step(h0, p0, h1, p1);
            p0 = p1 = 0;
        }
        render_begin();
        views = G.loaded ? game_draw() : 0;
        if (G.state != ST_TITLE) {
            Player *p = &G.p[0];
            if (p->flash > 0) render_flash((p->flash_r * p->flash) >> 7, (p->flash_g * p->flash) >> 7, (p->flash_b * p->flash) >> 7);
        }
        hud();
        if (views > 1) {
            int end = render_mark();
            void *l = render_segment(game_marks[0], game_marks[1]);
            void *r = render_segment(game_marks[1], game_marks[2]);
            void *o = render_segment(game_marks[2], end);
            render_end();
            gfx_set_underlay_split(l, r, o, 160);
        } else {
            list = render_end();
            gfx_set_underlay(list);
        }
        gfx_swap();
        if ((++frames % 250) == 0)
            AB_I("stats quads=%ld cels=%ld state=%ld course=%ld", (long)render_stats_quads, (long)render_stats_cels,
                 (long)G.state, (long)G.level + 1);
    }
    gfx_set_underlay(0);
    gfx_exit();
    return 0;
}
