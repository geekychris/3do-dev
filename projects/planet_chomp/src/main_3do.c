/*
 * PLANET CHOMP - 3DO version of geekychris/planet-chomp (Unity, C#).
 *
 * Prototype: the sphere maze, the camera that rides over the planet, walls,
 * crumbs and the chomper, drawn by the cel engine (render.c, cels.c); the
 * frame on top only carries the HUD. Game logic at 50 steps/s.
 *
 * Pad: D-pad steers (relative to the screen; a turn waits for the next
 * junction), L/R spin the view, C toggles the whole-planet view, X quits.
 */
#include <exec/types.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>
#include "amiga3do.h"
#include "bridge_client.h"
#include "pc.h"

#define PEN_CLEAR  255
#define PEN_TEXT   1
#define PEN_DIM    2
#define SPEED_Q8   Q8(4.6)               /* world units per second */

static unsigned char crumb[CELLS];
static int  score, crumbs_left;

/* the chomper on rails: from -> to, t in Q16 */
static int  m_from, m_to = -1, m_exit;
static long m_t;
static V3   want;                        /* buffered steering direction (Q14), 0 = none */
static int  want_set;

static V3 lerp_dir(int a, int b, long t)
{
    V3 p;
    p.x = mz_dir[a].x + (((mz_dir[b].x - mz_dir[a].x) * (t >> 2)) >> 14);
    p.y = mz_dir[a].y + (((mz_dir[b].y - mz_dir[a].y) * (t >> 2)) >> 14);
    p.z = mz_dir[a].z + (((mz_dir[b].z - mz_dir[a].z) * (t >> 2)) >> 14);
    return v3_norm14(p);
}

/* open exit at c best matching direction d (Q14); -1 if none within ~70 deg */
static int best_exit(int c, V3 d, int not_k)
{
    int k, best = -1;
    fix bd = 5600;
    for (k = 0; k < 4; k++) {
        fix dot;
        if (!mz_open[c][k] || k == not_k) continue;
        dot = v3_dot14(mz_tan[c][k], d);
        if (dot > bd) { bd = dot; best = k; }
    }
    return best;
}

static void arrive(int c)
{
    if (crumb[c]) {
        crumb[c] = 0;
        crumbs_left--;
        score += 10;
        AB_I("chomp score=%ld left=%ld", (long)score, (long)crumbs_left);
    }
}

static void mover_step(void)
{
    long dt = (SPEED_Q8 << 16) / 50 / Q8(2.1);   /* ~ one cell length in t (Q16) */
    if (m_to < 0) {
        /* parked: start toward the wanted direction */
        if (want_set) {
            int k = best_exit(m_from, want, -1);
            if (k >= 0) { m_to = mz_nb[m_from][k]; m_exit = k; m_t = 0; want_set = 0; }
        }
        return;
    }
    /* a direction opposite the heading reverses at once */
    if (want_set && v3_dot14(mz_tan[m_from][m_exit], want) < -9000) {
        int back = mz_rev[m_from][m_exit];
        int f = m_from;
        m_from = m_to; m_to = f; m_exit = back;
        m_t = 65536 - m_t;
        want_set = 0;
    }
    m_t += dt;
    if (m_t >= 65536) {
        int c = m_to, in_k = mz_rev[m_from][m_exit], k = -1;
        V3 heading = v3_scale14(mz_tan[c][in_k], -ONE14);   /* the way we were going */
        m_t -= 65536;
        arrive(c);
        if (want_set) {
            k = best_exit(c, want, in_k);
            if (k >= 0) want_set = 0;
        }
        if (k < 0) k = best_exit(c, heading, in_k);         /* keep going straight */
        m_from = c;
        if (k < 0) { m_to = -1; m_t = 0; return; }
        m_to = mz_nb[c][k];
        m_exit = k;
    }
}

static V3 player_dir(void)
{
    return m_to < 0 ? mz_dir[m_from] : lerp_dir(m_from, m_to, m_t);
}

int main(void)
{
    struct RastPort *rp;
    Camera cam;
    int c, overview = 0;
    ULONG frames = 0;
    char buf[48];

    ab_init("PLANET");
    if (!gfx_init_mode(GFX_RGB16, 256, 0, 0))
        return 1;
    gfx_set_rgb24(PEN_TEXT, 0xE0E8FF);
    gfx_set_rgb24(PEN_DIM, 0x8890B0);
    gfx_set_transparent_pen(PEN_CLEAR);
    if (!pc_cels_init()) {
        AB_E("cels unavailable");
        return 1;
    }
    mz_build(2026UL + 7919UL);
    mz_build_walls();
    for (c = 0; c < CELLS; c++) crumb[c] = 1;
    crumb[mz_start] = crumb[mz_nest] = 0;
    crumbs_left = CELLS - 2;
    m_from = mz_start;
    rd_init();
    cam.focus = mz_dir[mz_start];
    cam.up.x = 0; cam.up.y = 0; cam.up.z = ONE14;
    cam.height = Q8(13);
    AB_I("ready cells=%ld walls=%ld", (long)CELLS, (long)mz_nwalls);

    while (!amiga_quit_requested()) {
        int steps = gfx_steps();
        ULONG held = pad_held(0), pressed = pad_pressed(0);
        if (pressed & PAD_C) overview = !overview;
        while (steps-- > 0) {
            V3 d, p;
            fix k;
            /* screen-relative steering, projected onto the surface here */
            d.x = d.y = d.z = 0;
            if (held & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) {
                V3 r = v3_norm14(v3_cross14(cam.up, cam.focus));   /* screen right on the surface */
                if (held & PAD_UP)    d = v3_add(d, cam.up);
                if (held & PAD_DOWN)  d = v3_sub(d, cam.up);
                if (held & PAD_RIGHT) d = v3_sub(d, r);
                if (held & PAD_LEFT)  d = v3_add(d, r);
                want = v3_norm14(d);
                want_set = 1;
            }
            mover_step();
            /* camera: ease the focus toward the player, carry "up" along */
            p = player_dir();
            cam.focus = v3_norm14(v3_add(cam.focus, v3_scale14(v3_sub(p, cam.focus), 2700)));
            k = v3_dot14(cam.up, cam.focus);
            cam.up = v3_norm14(v3_sub(cam.up, v3_scale14(cam.focus, k)));
            if (held & (PAD_L | PAD_R)) {
                V3 side = v3_cross14(cam.focus, cam.up);
                fix s = (held & PAD_L) ? -629 : 629;
                cam.up = v3_norm14(v3_add(v3_scale14(cam.up, 16372), v3_scale14(side, s)));
            }
            {
                fix target = overview ? Q8(42) : Q8(13);
                cam.height += (target - cam.height) / 10;
            }
        }

        rd_frame(&cam, player_dir(), crumb);

        /* the frame stays transparent (cleared once); the HUD text is drawn
         * with the transparent pen as background, so it overwrites itself */
        rp = gfx_back();
        if (frames == 0) {
            SetAPen(rp, PEN_CLEAR);
            RectFill(rp, 0, 0, GFX_WIDTH - 1, gfx_height() - 1);
        }
        SetDrMd(rp, JAM2);
        SetBPen(rp, PEN_CLEAR);
        SetAPen(rp, PEN_DIM);
        Move(rp, 8, 14); Text(rp, (STRPTR)"SCORE", 5);
        Move(rp, 236, 14); Text(rp, (STRPTR)"CRUMBS", 6);
        SetAPen(rp, PEN_TEXT);
        sprintf(buf, "%-6d", score);
        Move(rp, 8, 26); Text(rp, (STRPTR)buf, strlen(buf));
        sprintf(buf, "%-4d", crumbs_left);
        Move(rp, 236, 26); Text(rp, (STRPTR)buf, strlen(buf));
        SetAPen(rp, PEN_DIM);
        sprintf(buf, "W%-4d C%-5d", rd_stats_walls, rd_stats_cels);
        Move(rp, 8, 248); Text(rp, (STRPTR)buf, strlen(buf));
        gfx_set_underlay(pc_cels_list());
        gfx_swap();
        if ((++frames % 250) == 0)
            AB_I("stats walls=%ld cels=%ld score=%ld", (long)rd_stats_walls, (long)rd_stats_cels, (long)score);
    }
    gfx_set_underlay(0);
    gfx_exit();
    return 0;
}
