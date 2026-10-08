/*
 * PLANET CHOMP - 3DO version of geekychris/planet-chomp (Unity, C#).
 *
 * A Pac-Man homage on a tiny planet. The planet, maze walls, crumbs, keys,
 * nest, chomper and spooks are drawn by the cel engine under the frame
 * (render.c, cels.c); the frame on top only carries the HUD, which is kept
 * transparent elsewhere and only redrawn where it changes. Rules in game.c,
 * sounds in sfx.c, sprite textures in sprites.c. Logic at 50 steps/s.
 *
 * Pad: D-pad steers (relative to the screen; a turn waits for the next
 * junction), L/R spin the view, C whole-planet view, P pause,
 * A or P start, X quits to the menu.
 */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "pc.h"
#include "game.h"
#include "sfx.h"
#include "sprites.h"

#define PEN_CLEAR  255
enum { P_WHITE = 1, P_DIM, P_YELLOW, P_GOLD, P_RED, P_PINK, P_CYAN, P_ORANGE, P_RING, P_RINGDIM,
       P_FRIGHT, P_EYES, P_BAR_BG };

static struct RastPort *rp;

/* ---- HUD on the frame ----
 * The frame is transparent except for the HUD. Each frame the HUD calls
 * below only *list* what should be on screen; hud_end() compares that
 * with last frame's list, erases what went away (to the transparent pen)
 * and draws what is new - unchanged text costs nothing. */

enum { HI_TEXT, HI_BIG, HI_DISC, HI_RING, HI_BAR };
typedef struct {
    short type, x, y, a, b, pen;        /* a, b: scale / radius / bar end */
    short x0, y0, x1, y1;               /* covered rectangle */
    char  s[40];
} HudItem;
#define MAXH 64
static HudItem hud[2][MAXH];
static int nhud[2], cur;

static HudItem *hud_add(int type, int x, int y, int a, int b, int pen, const char *str)
{
    HudItem *h;
    if (nhud[cur] >= MAXH) return 0;
    h = &hud[cur][nhud[cur]++];
    h->type = (short)type; h->x = (short)x; h->y = (short)y; h->a = (short)a; h->b = (short)b;
    h->pen = (short)pen;
    h->s[0] = 0;
    if (str) { strncpy(h->s, str, sizeof(h->s) - 1); h->s[sizeof(h->s) - 1] = 0; }
    return h;
}

static void set_rect(HudItem *h, int x0, int y0, int x1, int y1)
{
    if (!h) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > GFX_WIDTH - 1) x1 = GFX_WIDTH - 1;
    if (y1 > 255) y1 = 255;
    h->x0 = (short)x0; h->y0 = (short)y0; h->x1 = (short)x1; h->y1 = (short)y1;
}

static void text(int x, int y, const char *s, int pen)      /* y = top of the glyphs */
{
    set_rect(hud_add(HI_TEXT, x, y, 0, 0, pen, s), x, y - 1, x + 8 * (int)strlen(s), y + 8);
}

static void ctext(int y, const char *s, int pen) { text(160 - 4 * (int)strlen(s), y, s, pen); }

static void big(int y, const char *s, int scale, int pen)
{
    int w = 8 * scale * (int)strlen(s), x = 160 - w / 2;
    set_rect(hud_add(HI_BIG, x, y, scale, 0, pen, s), x, y, x + w + 1, y + 8 * scale + 1);
}

static void disc(int cx, int cy, int r, int pen)
{
    set_rect(hud_add(HI_DISC, cx, cy, r, 0, pen, 0), cx - r, cy - r, cx + r, cy + r);
}

static void ring(int cx, int cy, int r, int pen)
{
    set_rect(hud_add(HI_RING, cx, cy, r, 0, pen, 0), cx - r, cy - r, cx + r, cy + r);
}

static void bar(int x0, int y0, int x1, int y1, int pen)
{
    set_rect(hud_add(HI_BAR, x0, y0, x1, y1, pen, 0), x0, y0, x1, y1);
}

static int same(const HudItem *a, const HudItem *b)
{
    return a->x == b->x && a->y == b->y && a->type == b->type && a->a == b->a && a->b == b->b &&
           a->pen == b->pen && a->s[0] == b->s[0] && strcmp(a->s, b->s) == 0;
}

static int overlap(const HudItem *a, const HudItem *b)
{
    return a->x0 <= b->x1 && b->x0 <= a->x1 && a->y0 <= b->y1 && b->y0 <= a->y1;
}

static void hud_draw(const HudItem *h)
{
    static const short cs[32][2] = {
        {16384,0},{16069,3196},{15137,6270},{13623,9102},{11585,11585},{9102,13623},{6270,15137},{3196,16069},
        {0,16384},{-3196,16069},{-6270,15137},{-9102,13623},{-11585,11585},{-13623,9102},{-15137,6270},{-16069,3196},
        {-16384,0},{-16069,-3196},{-15137,-6270},{-13623,-9102},{-11585,-11585},{-9102,-13623},{-6270,-15137},{-3196,-16069},
        {0,-16384},{3196,-16069},{6270,-15137},{9102,-13623},{11585,-11585},{13623,-9102},{15137,-6270},{16069,-3196}
    };
    int k;
    switch (h->type) {
    case HI_TEXT:
        SetAPen(rp, h->pen);
        SetDrMd(rp, JAM1);
        Move(rp, h->x, h->y + 6);
        Text(rp, (STRPTR)h->s, strlen(h->s));
        break;
    case HI_BIG:
        SetAPen(rp, P_BAR_BG);
        gfx_text_big(rp, h->x + 1, h->y + 1, h->s, h->a);   /* drop shadow */
        SetAPen(rp, h->pen);
        gfx_text_big(rp, h->x, h->y, h->s, h->a);
        break;
    case HI_DISC:
        SetAPen(rp, h->pen);
        for (k = -h->a; k <= h->a; k++) {
            int w = 0;
            while ((w + 1) * (w + 1) + k * k <= h->a * h->a) w++;
            RectFill(rp, h->x - w, h->y + k, h->x + w, h->y + k);
        }
        break;
    case HI_RING:
        SetAPen(rp, h->pen);
        Move(rp, h->x + h->a, h->y);
        for (k = 1; k <= 32; k++)
            Draw(rp, h->x + (h->a * cs[k & 31][0]) / 16384, h->y - (h->a * cs[k & 31][1]) / 16384);
        break;
    case HI_BAR:
        SetAPen(rp, h->pen);
        RectFill(rp, h->x, h->y, h->a, h->b);
        break;
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
    static unsigned char keep[MAXH], redraw[MAXH];
    /* last frame's items that are gone: erase them */
    /* items nearly always come in the same order: try the same index first */
    for (i = 0; i < nhud[prev]; i++) {
        HudItem *o = &hud[prev][i];
        keep[i] = 0;
        if (i < nhud[cur] && same(o, &hud[cur][i])) { keep[i] = 1; continue; }
        for (j = 0; j < nhud[cur]; j++)
            if (same(o, &hud[cur][j])) { keep[i] = 1; break; }
    }
    for (j = 0; j < nhud[cur]; j++) {
        redraw[j] = 1;
        if (j < nhud[prev] && keep[j] && same(&hud[prev][j], &hud[cur][j])) { redraw[j] = 0; continue; }
        for (i = 0; i < nhud[prev]; i++)
            if (keep[i] && same(&hud[prev][i], &hud[cur][j])) { redraw[j] = 0; break; }
    }
    SetAPen(rp, PEN_CLEAR);
    for (i = 0; i < nhud[prev]; i++)
        if (!keep[i]) {
            HudItem *o = &hud[prev][i];
            RectFill(rp, o->x0, o->y0, o->x1, o->y1);
            /* anything still wanted that the erase touched is drawn again */
            for (j = 0; j < nhud[cur]; j++)
                if (!redraw[j] && overlap(o, &hud[cur][j])) redraw[j] = 1;
            SetAPen(rp, PEN_CLEAR);
        }
    for (j = 0; j < nhud[cur]; j++)
        if (redraw[j]) hud_draw(&hud[cur][j]);
}

/* ---- radar: the whole sphere around the player; centre = here, rim =
 * the far side, in the camera's screen frame ---- */

static void radar_dot(V3 p, V3 d, V3 cr, V3 cu, int cx, int cy, int rad, int size, int pen)
{
    /* angle(p, d)/pi along the tangent toward d */
    fix cosang = v3_dot14(p, d), x, y, a;
    V3 t = v3_sub(d, v3_scale14(p, cosang));
    if (!(t.x | t.y | t.z)) { disc(cx, cy, size, pen); return; }
    t = v3_norm14(t);
    /* acos via a small table-free approximation: a/pi ~ (1 - cos)/2 shaped */
    a = (ONE14 - cosang) / 2;                 /* 0 .. 16384 */
    a = (a * 3 + (fix)isqrt32((unsigned long)a << 14)) / 4;   /* closer to acos/pi */
    x = (v3_dot14(t, cr) * a) >> 14;
    y = (v3_dot14(t, cu) * a) >> 14;
    disc(cx + (int)((x * rad) >> 14), cy - (int)((y * rad) >> 14), size, pen);
}

static void radar(void)
{
    int cx = 282, cy = 214, rad = 30, i;
    V3 p = mover_dir(&g.player);
    V3 cr = v3_norm14(v3_cross14(g.cam.focus, g.cam.up)), cu = g.cam.up;
    ring(cx, cy, rad + 4, P_RING);
    ring(cx, cy, (rad + 4) / 2, P_RINGDIM);
    for (i = 0; i < 4; i++)
        if (g.key[mz_keys[i]]) radar_dot(p, mz_dir[mz_keys[i]], cr, cu, cx, cy, rad, 2, P_GOLD);
    radar_dot(p, mz_dir[mz_nest], cr, cu, cx, cy, rad, 2, P_PINK);
    for (i = 0; i < 4; i++) {
        static const int tint[4] = { P_RED, P_PINK, P_CYAN, P_ORANGE };
        Ghost *gh = &g.ghost[i];
        int pen = gh->mode == GM_EATEN ? P_EYES : gh->fright ? P_FRIGHT : tint[i];
        radar_dot(p, mover_dir(&gh->mv), cr, cu, cx, cy, rad, 2, pen);
    }
    disc(cx, cy, 2, P_YELLOW);
    text(cx - 20, cy - rad - 16, "RADAR", P_DIM);
}

/* ---- sprites for this frame ---- */

static RSprite spr[6];

static int chomper_dir(void)
{
    /* which way the heading points on screen */
    V3 p = player_world(), q = v3_add(p, v3_scale14(g.player.heading, Q8(1)));
    long x0, y0, x1, y1, dx, dy;
    if (!rd_project(p, &x0, &y0) || !rd_project(q, &x1, &y1)) return 0;
    dx = x1 - x0; dy = y1 - y0;
    if ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)) return dx > 0 ? 0 : 2;
    return dy < 0 ? 1 : 3;
}

static int build_sprites(void)
{
    int n = 0, i;
    if (g.state != GS_TITLE && g.state != GS_OVER) {
        int mouth = 1;
        fix half = Q8(0.55);
        if (g.player.moving) {
            int ph = (int)(g.mouth % 12);
            mouth = ph < 4 ? 0 : ph < 8 ? 1 : 2;
        }
        if (g.state == GS_DYING) {
            /* gape, then shrink to nothing */
            long t = g.timer - TICKS(0.4);
            mouth = 2;
            if (t > 0) half = half * (TICKS(1.4) - (t > TICKS(1.4) ? TICKS(1.4) : t)) / TICKS(1.4);
        }
        if (half > 0) {
            spr[n].pos = player_world();
            spr[n].tex = tex_chomper[g.powered][chomper_dir()][mouth];
            spr[n].half = half;
            n++;
        }
    }
    if (!(g.state == GS_DYING && g.timer > TICKS(0.4)) && g.state != GS_CLEAR && g.state != GS_OVER)
        for (i = 0; i < 4; i++) {
            Ghost *gh = &g.ghost[i];
            int t;
            if (gh->mode == GM_EATEN) t = tex_ghost[GT_EYES];
            else if (gh->fright)
                t = (g.power_left < TICKS(2) && (g.power_left % 20) < 10) ? tex_ghost[GT_FLASH]
                                                                          : tex_ghost[GT_FRIGHT];
            else t = tex_ghost[i];
            spr[n].pos = ghost_world(gh);
            spr[n].tex = t;
            spr[n].half = Q8(0.6);
            n++;
        }
    return n;
}

/* ---- screens ---- */

static void hud_play(void)
{
    char buf[32];
    int i, lives;
    text(8, 4, "SCORE", P_DIM);
    sprintf(buf, "%ld", g.score);
    text(8, 14, buf, P_WHITE);
    ctext(4, "HIGH SCORE", P_DIM);
    sprintf(buf, "%ld", g.hiscore);
    ctext(14, buf, P_WHITE);
    sprintf(buf, "LEVEL %d", g.level);
    text(312 - 8 * (int)strlen(buf), 4, buf, P_YELLOW);
    sprintf(buf, "CRUMBS %d", g.crumbs_left);
    text(312 - 8 * (int)strlen(buf), 14, buf, P_DIM);
    lives = g.lives - ((g.state == GS_READY || g.state == GS_PLAYING) ? 1 : 0);
    for (i = 0; i < lives && i < 8; i++) disc(14 + i * 14, 244, 5, P_YELLOW);
    if (g.power_left > 0) {
        int w = (int)(160 * g.power_left / (g.power_total ? g.power_total : 1));
        int blink = g.power_left < TICKS(2) && (g.power_left % 20) < 10;
        bar(80, 28, 239, 32, P_BAR_BG);
        if (w > 0) bar(80, 28, 80 + w - 1, 32, blink ? P_WHITE : P_GOLD);
        ctext(36, "INVINCIBLE!", P_GOLD);
    }
    radar();
    /* score popups float up from eaten spooks */
    for (i = 0; i < 8; i++) {
        long x, y;
        if (g.popup[i].age < 0 || !rd_project(g.popup[i].world, &x, &y)) continue;
        sprintf(buf, "%d", g.popup[i].pts);
        text((int)(x >> 16) - 4 * (int)strlen(buf), (int)(((y >> 16) * 256) / 240) - 10 - g.popup[i].age / 2,
             buf, P_CYAN);
    }
    switch (g.state) {
    case GS_READY:
        big(150, "READY!", 2, P_YELLOW);
        if (g.level == 1) ctext(172, "EAT EVERY CRUMB - GRAB A KEY", P_DIM);
        break;
    case GS_CLEAR:
        big(80, "PLANET CLEARED!", 2, P_YELLOW);
        break;
    }
    if (g.paused) big(110, "PAUSED", 2, P_WHITE);
    if (g.demo) ctext(228, "DEMO - PRESS A", P_DIM);
}

static void hud_title(void)
{
    char buf[32];
    int i;
    static const int tint[4] = { P_RED, P_PINK, P_CYAN, P_ORANGE };
    big(34, "PLANET CHOMP", 3, P_YELLOW);
    ctext(64, "A PAC-MAN HOMAGE ON A VERY SMALL WORLD", P_DIM);
    for (i = 0; i < 4; i++) {
        int x = 28 + i * 72;
        disc(x, 92, 5, tint[i]);
        text(x + 9, 88, ghost_name[i], tint[i]);
    }
    ctext(118, "EAT EVERY CRUMB ON THE PLANET.", P_WHITE);
    ctext(130, "GRAB A GOLDEN KEY TO TURN THE TABLES.", P_WHITE);
    ctext(148, "D-PAD MOVE   L/R SPIN THE VIEW", P_DIM);
    ctext(160, "C WHOLE PLANET   P PAUSE", P_DIM);
    if ((g.clock / 25) % 3 != 2) big(190, "PRESS A TO START", 2, P_YELLOW);
    sprintf(buf, "HIGH SCORE  %ld", g.hiscore);
    ctext(236, buf, P_DIM);
}

static void hud_over(void)
{
    char buf[32];
    big(100, "GAME OVER", 3, P_RED);
    ctext(132, "PRESS A TO PLAY AGAIN", P_DIM);
    sprintf(buf, "SCORE %ld   HIGH SCORE %ld", g.score, g.hiscore);
    ctext(150, buf, P_WHITE);
}

/* ---- high score in NVRAM ---- */

static void hiscore_load(void)
{
    BPTR f = Open((CONST_STRPTR)"planetchomp.hi", MODE_OLDFILE);
    long v = 0;
    if (f) {
        if (Read(f, &v, 4) == 4 && v > 0 && v < 100000000L) g.hiscore = v;
        Close(f);
    }
}

static void hiscore_save(void)
{
    BPTR f = Open((CONST_STRPTR)"planetchomp.hi", MODE_NEWFILE);
    if (f) {
        Write(f, &g.hiscore, 4);
        Close(f);
    }
}

int main(void)
{
    ULONG frames = 0;
    long saved_hi = 0;
    int last_state = -1;

    ab_init("PLANET");
    amiga_set_progdir("planet_chomp");
    if (!gfx_init_mode(GFX_RGB16, 256, 0, 0))
        return 1;
    gfx_set_rgb24(P_WHITE, 0xF0F4FF);
    gfx_set_rgb24(P_DIM, 0x98A0C0);
    gfx_set_rgb24(P_YELLOW, 0xFFD838);
    gfx_set_rgb24(P_GOLD, 0xFFB828);
    gfx_set_rgb24(P_RED, 0xFF4C40);
    gfx_set_rgb24(P_PINK, 0xFF8CD8);
    gfx_set_rgb24(P_CYAN, 0x60F0FF);
    gfx_set_rgb24(P_ORANGE, 0xFFA640);
    gfx_set_rgb24(P_RING, 0x5080FF);
    gfx_set_rgb24(P_RINGDIM, 0x284080);
    gfx_set_rgb24(P_FRIGHT, 0x3048FF);
    gfx_set_rgb24(P_EYES, 0xC0C0D0);
    gfx_set_rgb24(P_BAR_BG, 0x101828);
    /* something to look at while the sounds and sprites are made */
    rp = gfx_back();
    SetRast(rp, P_BAR_BG);
    SetAPen(rp, P_YELLOW);
    gfx_text_big(rp, 160 - 8 * 3 * 12 / 2, 100, "PLANET CHOMP", 3);
    SetAPen(rp, P_DIM);
    SetDrMd(rp, JAM1);
    Move(rp, 160 - 4 * 10, 146);
    Text(rp, (STRPTR)"LOADING...", 10);
    gfx_swap();
    gfx_set_transparent_pen(PEN_CLEAR);
    if (!pc_cels_init() || !sprites_init()) {
        AB_E("cels unavailable");
        return 1;
    }
    tex_key_id = tex_key;
    if (!sfx_init())
        AB_W("no memory for sounds");
    rd_init();
    game_init();
    hiscore_load();
    saved_hi = g.hiscore;
    rp = gfx_back();
    SetAPen(rp, PEN_CLEAR);
    RectFill(rp, 0, 0, GFX_WIDTH - 1, gfx_height() - 1);
    AB_I("ready cells=%ld walls=%ld", (long)CELLS, (long)mz_nwalls);

    while (!amiga_quit_requested()) {
        int steps = gfx_steps(), nspr, flags = 0;
        ULONG held = pad_held(0), pressed = pad_pressed(0);
        while (steps-- > 0) {
            game_step(held, pressed);
            pressed = 0;                            /* an edge counts once */
        }
        if (g.state == GS_CLEAR && g.timer > TICKS(0.6) && (g.timer % 18) < 9)
            flags |= RD_WALL_FLASH;
        nspr = build_sprites();
        rd_frame(&g.cam, g.crumb, g.key, spr, nspr, flags);

        hud_begin();
        if (g.state == GS_TITLE) hud_title();
        else {
            hud_play();
            if (g.state == GS_OVER) hud_over();
        }
        hud_end();
        if (g.state != last_state) {
            if (g.state == GS_OVER && g.hiscore > saved_hi) {
                hiscore_save();
                saved_hi = g.hiscore;
            }
            last_state = g.state;
        }
        gfx_set_underlay(pc_cels_list());
        gfx_swap();
        if ((++frames % 250) == 0)
            AB_I("stats walls=%ld cels=%ld score=%ld", (long)rd_stats_walls, (long)rd_stats_cels, g.score);
    }
    if (g.hiscore > saved_hi) hiscore_save();
    gfx_set_underlay(0);
    gfx_exit();
    return 0;
}
