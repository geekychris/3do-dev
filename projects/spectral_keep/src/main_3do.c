/*
 * SPECTRAL KEEP - 3DO version of geekychris/spectral-keep (Unity, C#).
 *
 * An isometric flip-screen adventure in the style of Knight Lore: three
 * keeps of rooms, relics to carry to the throne, keys for the gates,
 * guards, hounds, ghosts and bouncers.
 *
 * The room is drawn by the cel engine under the frame (scene.c): a
 * background image of the floor and walls, then pre-rendered sprites of
 * every block and character (vox.c) in depth order. The frame on top only
 * carries the text, kept transparent elsewhere and redrawn only where it
 * changes. Rules in game.c, rooms and actors in room.c, physics in world.c,
 * music and sounds in sound.c. Logic at 50 steps/s.
 *
 * Pad: D-pad walks (screen-relative; C on the title switches to grid
 * directions), A or B jumps, P pauses, X quits to the menu. On the title,
 * Up/Down choose a keep, A or P starts, B turns the music on or off.
 */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "keep.h"

void *scene_frame(int show_player, int panel, int flash_ink);
void  scene_init(void);
void  scene_room(void);
extern int scene_ncels;
extern const unsigned char keep_font[96][8];

#define PEN_CLEAR  255
#define PEN_BLACK  8                /* pens 1..7: the Spectrum inks */
#define PEN_SHADOW 9

static UWORD pen16[16];

void keep_log(const char *fmt, ...)
{
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    AB_I("%s", buf);
}

/* ---- HUD on the frame ----
 * Each frame the HUD only *lists* what should be on screen; hud_end()
 * compares that with last frame's list, erases what went away (to the
 * transparent pen) and draws what is new. */

enum { HI_TEXT, HI_BIG, HI_BAR };
typedef struct {
    short type, x, y, pen, bg;
    short x0, y0, x1, y1;           /* covered rectangle */
    char  s[44];
} HudItem;
#define MAXHUD 40
static HudItem hud[2][MAXHUD];
static int nhud[2], cur;

static void clip_rect(HudItem *h)
{
    if (h->x0 < 0) h->x0 = 0;
    if (h->y0 < 0) h->y0 = 0;
    if (h->x1 > GFX_WIDTH - 1) h->x1 = GFX_WIDTH - 1;
    if (h->y1 > 239) h->y1 = 239;
}

static HudItem *hud_add(int type, int x, int y, int pen, int bg, const char *s)
{
    HudItem *h;
    if (nhud[cur] >= MAXHUD) return 0;
    h = &hud[cur][nhud[cur]++];
    h->type = (short)type; h->x = (short)x; h->y = (short)y; h->pen = (short)pen; h->bg = (short)bg;
    h->s[0] = 0;
    if (s) { strncpy(h->s, s, sizeof(h->s) - 1); h->s[sizeof(h->s) - 1] = 0; }
    return h;
}

static void text_bg(int x, int y, const char *s, int pen, int bg)
{
    HudItem *h = hud_add(HI_TEXT, x, y, pen, bg, s);
    if (!h) return;
    h->x0 = (short)x; h->y0 = (short)y;
    h->x1 = (short)(x + 8 * (int)strlen(h->s) - 1); h->y1 = (short)(y + 7);
    clip_rect(h);
}
static void text(int x, int y, const char *s, int pen) { text_bg(x, y, s, pen, -1); }
static void ctext(int y, const char *s, int pen) { text(160 - 4 * (int)strlen(s), y, s, pen); }

static void big(int y, const char *s, int pen)       /* double size, with a drop shadow */
{
    int w = 16 * (int)strlen(s), x = 160 - w / 2;
    HudItem *h = hud_add(HI_BIG, x, y, pen, -1, s);
    if (!h) return;
    h->x0 = (short)x; h->y0 = (short)y; h->x1 = (short)(x + w + 1); h->y1 = (short)(y + 17);
    clip_rect(h);
}

static void bar(int x0, int y0, int x1, int y1, int pen)
{
    HudItem *h = hud_add(HI_BAR, x0, y0, pen, -1, 0);
    if (!h) return;
    h->x0 = (short)x0; h->y0 = (short)y0; h->x1 = (short)x1; h->y1 = (short)y1;
    clip_rect(h);
}

static int same(const HudItem *a, const HudItem *b)
{
    return a->type == b->type && a->x == b->x && a->y == b->y && a->pen == b->pen && a->bg == b->bg &&
           a->x1 == b->x1 && a->y1 == b->y1 && strcmp(a->s, b->s) == 0;
}

static int overlap(const HudItem *a, const HudItem *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

static void fill(int x0, int y0, int x1, int y1, int pen)
{
    UWORD *row = gfx_pixels16() + y0 * GFX_WIDTH, v = pen == PEN_CLEAR ? gfx_pen_rgb16(PEN_CLEAR) : pen16[pen & 15];
    int y;
    for (y = y0; y <= y1; y++, row += GFX_WIDTH) {
        UWORD *p = row + x0, *e = row + x1;
        while (p <= e) *p++ = v;
    }
}

static void glyphs(int x, int y, const char *s, int pen, int bg, int scale)
{
    UWORD *px = gfx_pixels16(), v = pen16[pen & 15], b = bg >= 0 ? pen16[bg & 15] : 0;
    for (; *s; s++, x += 8 * scale) {
        int c = (unsigned char)*s, r, k, i, j;
        const unsigned char *g = keep_font[(c >= 32 && c < 128) ? c - 32 : 0];
        for (r = 0; r < 8; r++)
            for (k = 0; k < 8; k++) {
                int on = (g[r] >> (7 - k)) & 1;
                if (!on && bg < 0) continue;
                for (j = 0; j < scale; j++)
                    for (i = 0; i < scale; i++) {
                        int X = x + k * scale + i, Y = y + r * scale + j;
                        if (X >= 0 && X < GFX_WIDTH && Y >= 0 && Y < 240)
                            px[Y * GFX_WIDTH + X] = on ? v : b;
                    }
            }
    }
}

static void hud_draw(const HudItem *h)
{
    switch (h->type) {
    case HI_TEXT: glyphs(h->x, h->y, h->s, h->pen, h->bg, 1); break;
    case HI_BIG:
        glyphs(h->x + 2, h->y + 2, h->s, PEN_SHADOW, -1, 2);
        glyphs(h->x, h->y, h->s, h->pen, -1, 2);
        break;
    case HI_BAR: fill(h->x0, h->y0, h->x1, h->y1, h->pen); break;
    }
}

static void hud_begin(void)
{
    cur ^= 1;
    nhud[cur] = 0;
}

static void hud_end(void)
{
    int prev = cur ^ 1, i, j;
    static unsigned char keep[MAXHUD], redraw[MAXHUD];
    for (i = 0; i < nhud[prev]; i++) {
        keep[i] = 0;
        for (j = 0; j < nhud[cur]; j++)
            if (same(&hud[prev][i], &hud[cur][j])) { keep[i] = 1; break; }
    }
    for (j = 0; j < nhud[cur]; j++) {
        redraw[j] = 1;
        for (i = 0; i < nhud[prev]; i++)
            if (keep[i] && same(&hud[prev][i], &hud[cur][j])) { redraw[j] = 0; break; }
    }
    for (i = 0; i < nhud[prev]; i++)
        if (!keep[i]) {
            HudItem *o = &hud[prev][i];
            fill(o->x0, o->y0, o->x1, o->y1, PEN_CLEAR);
            /* anything still wanted that the erase touched is drawn again */
            for (j = 0; j < nhud[cur]; j++)
                if (!redraw[j] && overlap(o, &hud[cur][j])) redraw[j] = 1;
        }
    /* later items draw over earlier ones: once one is redrawn, so is everything on top of it */
    for (j = 0; j < nhud[cur]; j++)
        if (redraw[j]) {
            int k;
            for (k = j + 1; k < nhud[cur]; k++)
                if (!redraw[k] && overlap(&hud[cur][j], &hud[cur][k])) redraw[k] = 1;
        }
    for (j = 0; j < nhud[cur]; j++)
        if (redraw[j]) hud_draw(&hud[cur][j]);
}

/* ---- screens (Hud.cs) ---- */

static int blink(int per_s) { return ((G.clock * per_s * 2) / 50) % 2 == 0; }

/* word wrap like Hud.Wrap; returns the number of lines */
static int wrap(const char *t, int width, char lines[][44], int max)
{
    int n = 0, len = 0;
    lines[0][0] = 0;
    while (*t && n < max) {
        const char *w = t;
        int wl = 0;
        while (w[wl] && w[wl] != ' ') wl++;
        if (len > 0 && len + 1 + wl > width) {
            if (++n >= max) break;
            lines[n][0] = 0;
            len = 0;
        }
        if (len > 0) lines[n][len++] = ' ';
        if (wl > width) wl = width;
        memcpy(lines[n] + len, w, wl);
        len += wl;
        lines[n][len] = 0;
        t = w + wl;
        while (*t == ' ') t++;
    }
    return len > 0 ? n + 1 : n;
}

static void hud_panel(void)
{
    char buf[44], lines[2][44];
    int n, i;
    if (!R.room) return;
    ctext(202, R.room->name, R.room->ink ? R.room->ink : INK_WHITE);
    sprintf(buf, "{%d", G.lives);
    text(10, 212, buf, INK_RED);
    sprintf(buf, "|%d/%d", G.relics, G.level->relics);
    text(60, 212, buf, INK_YELLOW);
    sprintf(buf, "}%d", G.keys);
    text(130, 212, buf, INK_CYAN);
    sprintf(buf, "ROOM %d/%d", G.nvisited, G.level->nrooms);
    text(180, 212, buf, INK_WHITE);
    if (game_message_visible()) {
        n = wrap(G.message, 38, lines, 2);
        for (i = 0; i < n; i++) ctext(222 + 9 * i, lines[i], G.msg_ink);
    } else
        ctext(231, G.level->name, INK_WHITE);
}

static void hud_title(void)
{
    char buf[44];
    int i;
    bar(0, 157, GFX_WIDTH - 1, 239, PEN_BLACK);
    big(10, "SPECTRAL KEEP", INK_YELLOW);
    ctext(34, "AN ISOMETRIC ADVENTURE", INK_CYAN);
    for (i = 0; i < keep_nlevels && i < 4; i++) {
        int sel = i == G.selected, y = 165 + 10 * i;
        sprintf(buf, "%d %s", i + 1, keep_levels[i].name);
        text(56, y, sel ? ">" : " ", INK_WHITE);
        text_bg(72, y, buf, sel ? PEN_BLACK : INK_WHITE, sel ? ((G.clock / 33) % 2 ? INK_CYAN : INK_YELLOW) : PEN_BLACK);
    }
    ctext(200, "A PLAY   UP/DOWN CHOOSE A KEEP", INK_WHITE);
    sprintf(buf, "C CONTROLS:%s   B MUSIC:%s", G.grid_controls ? "GRID" : "SCREEN", G.music_on ? "ON" : "OFF");
    ctext(212, buf, INK_GREEN);
    ctext(228, "(C) 2026 CLAUDE SOFTWARE", INK_MAGENTA);
}

static void hud_play(void)
{
    hud_panel();
    if (G.state == GS_LEVELDONE && blink(3)) big(60, "WELL DONE!", INK_YELLOW);
    if (G.paused) {
        bar(124, 80, 195, 89, INK_RED);
        ctext(81, "PAUSED", INK_WHITE);
    }
}

static void hud_end_screen(void)
{
    char buf[44];
    bar(0, 0, GFX_WIDTH - 1, 239, PEN_BLACK);
    if (G.state == GS_GAMEOVER) {
        big(86, "GAME OVER", INK_RED);
        sprintf(buf, "RELICS FOUND: %d", G.relics);
        ctext(130, buf, INK_YELLOW);
        sprintf(buf, "ROOMS EXPLORED: %d", G.nvisited);
        ctext(140, buf, INK_CYAN);
    } else {
        big(56, "VICTORY!", INK_YELLOW);
        ctext(100, "EVERY KEEP HAS FALLEN", INK_CYAN);
        ctext(110, "AND THE CROWN IS YOURS.", INK_CYAN);
        ctext(140, "THANKS FOR PLAYING", INK_MAGENTA);
    }
    if (blink(2)) ctext(180, "PRESS A", INK_WHITE);
}

int main(void)
{
    ULONG frames = 0;
    struct RastPort *rp;
    static const ULONG zx[8] = { 0x000000, 0x2040FF, 0xFF3030, 0xFF40FF, 0x30E030, 0x30F0F0, 0xFFF040, 0xFFFFFF };
    int i, last_state = -1;

    ab_init("KEEP");
    amiga_set_progdir("spectral_keep");
    if (!gfx_init_mode(GFX_RGB16, 240, 0, 0))
        return 1;
    for (i = 1; i < 8; i++) gfx_set_rgb24(i, zx[i]);
    gfx_set_rgb24(PEN_BLACK, 0x080808);
    gfx_set_rgb24(PEN_SHADOW, 0x101018);
    for (i = 0; i < 16; i++) pen16[i] = gfx_pen_rgb16(i);
    /* something to look at while the sprites are drawn */
    rp = gfx_back();
    SetRast(rp, PEN_BLACK);
    glyphs(160 - 13 * 8 + 2, 102, "SPECTRAL KEEP", PEN_SHADOW, -1, 2);
    glyphs(160 - 13 * 8, 100, "SPECTRAL KEEP", INK_YELLOW, -1, 2);
    glyphs(160 - 4 * 10, 140, "LOADING...", INK_WHITE, -1, 1);
    gfx_swap();
    gfx_set_transparent_pen(PEN_CLEAR);
    gfx_set_max_steps(8);
    if (!vox_init()) {
        AB_E("no memory for sprites");
        return 1;
    }
    scene_init();
    if (!snd_init())
        AB_W("no sound effects");
    game_init();
    rp = gfx_back();
    SetAPen(rp, PEN_CLEAR);
    RectFill(rp, 0, 0, GFX_WIDTH - 1, 239);
    AB_I("ready keeps=%ld", (long)keep_nlevels);

    while (!amiga_quit_requested()) {
        int steps = gfx_steps();
        ULONG held = pad_held(0), pressed = pad_pressed(0);
        void *list = 0;
        while (steps-- > 0) {
            game_step(held, pressed);
            pressed = 0;                            /* an edge counts once */
            if (G.room_changed) {
                if (G.trace) keep_log("building %s\n", R.room->id);
                scene_room();
                if (G.trace) keep_log("built %s\n", R.room->id);
                G.room_changed = 0;
            }
        }
        if (G.state != GS_GAMEOVER && G.state != GS_VICTORY)
            list = scene_frame(G.state != GS_DYING, G.state != GS_TITLE || G.tour,
                               G.clock < G.flash_until ? G.flash_ink : -1);
        hud_begin();
        if (G.state == GS_TITLE && G.tour) {
            ctext(202, R.room->name, R.room->ink ? R.room->ink : INK_WHITE);
            ctext(216, G.level->name, INK_WHITE);
            ctext(230, "L+R OR A: BACK TO THE TITLE", INK_GREEN);
        } else if (G.state == GS_TITLE) hud_title();
        else if (G.state == GS_GAMEOVER || G.state == GS_VICTORY) hud_end_screen();
        else hud_play();
        hud_end();
        if (G.state != last_state) last_state = G.state;
        gfx_set_underlay(list);
        gfx_swap();
        if ((++frames % 250) == 0)
            AB_I("stats cels=%ld state=%ld room=%s", (long)scene_ncels, (long)G.state, R.room ? R.room->id : "-");
        if (G.trace && R.player >= 0 && (frames % 10) == 0) {
            const Body *b = R.act[R.player].body;
            keep_log("at %ld %ld %ld\n", (long)(b->px * 100 >> 12), (long)(b->py * 100 >> 12), (long)(b->pz * 100 >> 12));
        }
    }
    gfx_set_underlay(0);
    gfx_exit();
    return 0;
}
