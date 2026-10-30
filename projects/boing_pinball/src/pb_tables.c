// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * pb_tables.c - the three tables. They share the cabinet: the outer
 * walls and the top arch, the plunger lane, the flippers, the
 * slingshots, the inlanes and outlanes and the three top lanes. Comet
 * Tour is the browser's table, wall for wall. Deep Dive and Robot Foundry
 * are new upper playfields with the same kinds of parts.
 */
#include <string.h>
#include "a3d.h"
#include "a3d_fix.h"
#include "pb.h"

const char *const pb_table_names[TB_COUNT] = { "COMET TOUR", "DEEP DIVE", "ROBOT FOUNDRY" };

static Table *T;

#define DEG(d)   ((fix)(((int32_t)(d) * 205887) / 180))
#define MX(x)    (2 * PB_MID - (x))     /* mirrored across the centre line */
#define ANG_PI   32768                  /* the engine's 16-bit angles */

static Seg *wallf(fix ax, fix ay, fix bx, fix by, fix r, fix e, int kind, int h)
{
    Seg *s;
    if (T->nseg >= PB_MAX_SEG) return NULL;
    s = &T->seg[T->nseg++];
    pb_seg_init(s, ax, ay, bx, by, r, e, kind);
    s->h = (int16_t)h;
    return s;
}

static Seg *wall(int ax, int ay, int bx, int by, fix r, fix e, int kind, int h)
{
    return wallf(FROMI(ax), FROMI(ay), FROMI(bx), FROMI(by), r, e, kind, h);
}

/* a chain of walls through n points */
static void poly(const int16_t *p, int n, fix r, fix e, int kind, int h, int mirror)
{
    int i;
    for (i = 0; i + 1 < n; i++) {
        int ax = p[i * 2], bx = p[i * 2 + 2];
        if (mirror) { ax = MX(ax); bx = MX(bx); }
        wall(ax, p[i * 2 + 1], bx, p[i * 2 + 3], r, e, kind, h);
    }
}

/* an arc of n walls, centre (cx, cy), from angle a0 to a1 (16-bit angles) */
static void arc(int cx, int cy, int rad, int32_t a0, int32_t a1, int n, fix r, fix e, int kind, int h)
{
    int i;
    fix px = 0, py = 0;
    for (i = 0; i <= n; i++) {
        int a = (int)(a0 + (a1 - a0) * i / n);
        fix x = FROMI(cx) + rad * a3_cos(a), y = FROMI(cy) + rad * a3_sin(a);
        if (i) wallf(px, py, x, y, r, e, kind, h);
        px = x; py = y;
    }
}

static void gate(int ax, int ay, int bx, int by, int ow_x, int ow_y)
{
    Seg *s = wall(ax, ay, bx, by, FROMI(2), FX(0.45), SK_GATE, 12);
    if (s) { s->ow_x = (int8_t)ow_x; s->ow_y = (int8_t)ow_y; }
}

static void circ(int x, int y, int r, fix e, int bumper, int h)
{
    Circ *c;
    if (T->ncirc >= PB_MAX_CIRC) return;
    c = &T->circ[T->ncirc++];
    c->cx = FROMI(x); c->cy = FROMI(y); c->r = FROMI(r); c->e = e;
    c->bumper = (int8_t)bumper; c->h = (int16_t)h;
    c->id = (int8_t)(T->ncirc - 1);
}
static void post(int x, int y) { circ(x, y, 6, FX(0.5), 0, 34); }
static void bumper(int x, int y) { circ(x, y, 24, FX(0.5), 1, 40); }

static void sensor(int ax, int ay, int bx, int by, int kind, int idx)
{
    Sensor *s;
    int g = 18;                          /* the ball's radius and a substep's move */
    if (T->nsens >= PB_MAX_SENS) return;
    s = &T->sens[T->nsens++];
    s->ax = FROMI(ax); s->ay = FROMI(ay); s->bx = FROMI(bx); s->by = FROMI(by);
    s->kind = (int8_t)kind; s->idx = (int8_t)idx;
    s->bb[0] = (int16_t)((ax < bx ? ax : bx) - g); s->bb[1] = (int16_t)((ay < by ? ay : by) - g);
    s->bb[2] = (int16_t)((ax > bx ? ax : bx) + g); s->bb[3] = (int16_t)((ay > by ? ay : by) + g);
}

static void drop(int ax, int ay, int bx, int by, int bank)
{
    Seg *s = wall(ax, ay, bx, by, FROMI(4), FX(0.35), SK_DROP, 26);
    if (!s) return;
    s->id = (int8_t)T->ndrop;
    s->bank = (int8_t)bank;
    T->drop_seg[T->ndrop++] = T->nseg - 1;
    if (bank + 1 > T->nbank) T->nbank = bank + 1;
}

static void flipper(int side, int px, int py, int len, fix r1, fix r2, int rest, int up)
{
    FlipDef *f = &T->flip[T->nflip++];
    f->side = side;
    f->px = FROMI(px); f->py = FROMI(py); f->len = FROMI(len); f->r1 = r1; f->r2 = r2;
    f->rest = DEG(rest); f->up = DEG(up);
}

static void spinner(int ax, int y, int bx)
{
    int i = T->nspin++;
    sensor(ax, y, bx, y, SN_SPIN, i);
    T->spin[i].x = FROMI((ax + bx) / 2); T->spin[i].y = FROMI(y);
}

/* a ramp's entrance channel (two walls, a cap, posts) and its path */
static void ramp(int x0, int x1, int y0, const int16_t *pts, int n, int exit_x, int exit_y)
{
    Ramp *r = &T->ramp[T->nramp];
    int i;
    wall(x0, y0, x0, y0 + 76, FROMI(4), FX(0.45), SK_WALL, 30);
    wall(x1, y0, x1, y0 + 76, FROMI(4), FX(0.45), SK_WALL, 30);
    /* the cap, peaked (the browser's is flat) so a ball can't sit on top of it */
    wall(x0, y0 + 84, (x0 + x1) / 2, y0 + 96, FROMI(3), FX(0.45), SK_WALL, 30);
    wall((x0 + x1) / 2, y0 + 96, x1, y0 + 84, FROMI(3), FX(0.45), SK_WALL, 30);
    post(x0, y0); post(x1, y0);
    sensor(x0, y0 + 58, x1, y0 + 58, SN_RAMPIN, T->nramp);
    r->n = n;
    for (i = 0; i < n; i++) { r->pts[i][0] = pts[i * 3]; r->pts[i][1] = pts[i * 3 + 1]; r->pts[i][2] = pts[i * 3 + 2]; }
    r->prof[0] = 0;
    for (i = 1; i < n; i++)
        r->prof[i] = r->prof[i - 1] + fhypot3(FROMI(pts[i * 3] - pts[i * 3 - 3]), FROMI(pts[i * 3 + 1] - pts[i * 3 - 2]),
                                             FROMI(pts[i * 3 + 2] - pts[i * 3 - 1]));
    r->exit_x = FROMI(exit_x); r->exit_y = FROMI(exit_y); r->exit_vy = FROMI(-380);
    T->nramp++;
}

static void lamp(int id, int x, int y, int r, int col)
{
    T->lamp[id].x = (int16_t)x; T->lamp[id].y = (int16_t)y; T->lamp[id].r = (int16_t)r; T->lamp[id].col = (int8_t)col;
}

static Deco *deco(int kind, int col, int x, int y, int a, int b)
{
    Deco *d;
    if (T->ndeco >= PB_MAX_DECO) return NULL;
    d = &T->deco[T->ndeco++];
    memset(d, 0, sizeof *d);
    d->kind = (int8_t)kind; d->col = (int8_t)col; d->x = (int16_t)x; d->y = (int16_t)y; d->a = (int16_t)a; d->b = (int16_t)b;
    return d;
}

static void deco_quad(int col, int x0, int y0, int x1, int y1, int x2, int y2, int x3, int y3)
{
    Deco *d = deco(DK_QUAD, col, 0, 0, 0, 0);
    if (!d) return;
    d->px[0] = (int16_t)x0; d->py[0] = (int16_t)y0; d->px[1] = (int16_t)x1; d->py[1] = (int16_t)y1;
    d->px[2] = (int16_t)x2; d->py[2] = (int16_t)y2; d->px[3] = (int16_t)x3; d->py[3] = (int16_t)y3;
}

static void deco_text(int col, int x, int y, const char *s)
{
    Deco *d = deco(DK_TEXT, col, x, y, 0, 0);
    if (d) d->text = s;
}

/* an arrow on the playfield pointing up at a shot */
static void arrow(int col, int x, int y, int ang_deg)
{
    int32_t a = (int32_t)ang_deg * 65536 / 360;
    int32_t c = a3_cos((int)a), s = a3_sin((int)a);
    /* tip 22 mm ahead, base 12 mm either side, 10 mm behind */
#define AX(f, l) (x + (int)(((int32_t)(f) * c - (int32_t)(l) * s) >> 16))
#define AY(f, l) (y + (int)(((int32_t)(f) * s + (int32_t)(l) * c) >> 16))
    deco_quad(col, AX(22, 0), AY(22, 0), AX(-10, 12), AY(-10, 12), AX(-4, 0), AY(-4, 0), AX(-10, -12), AY(-10, -12));
#undef AX
#undef AY
}

/* ---------------------------------------------------------------- shared */

static void cabinet(void)
{
    static const int16_t left[] = { 20, -60, 20, 380, 0, 470, 0, 780 };
    static const int16_t rout[] = { 458, -60, 458, 380, 478, 470 };
    static const int16_t sep[] = { 58, 360, 58, 190, 150, 132 };
    static const int16_t sling[] = { 100, 345, 100, 225, 150, 192 };
    int i;
    Seg *s;

    /* the outer shell: the left wall with its kink into the outlane, the top arch, the right wall */
    poly(left, 4, FROMI(3), FX(0.4), SK_OUTER, 45, 0);
    arc(260, 780, 260, ANG_PI, 0, 44, FROMI(3), FX(0.4), SK_OUTER, 45);
    wall(520, 780, 520, -60, FROMI(3), FX(0.4), SK_OUTER, 45);
    /* the plunger lane: its inner wall and floor; the right outlane's outer wall */
    wall(478, -60, 478, 776, FROMI(4), FX(0.45), SK_WALL, 30);
    wall(478, 30, 520, 30, FROMI(3), FX(0.2), SK_FLOOR, 8);
    poly(rout, 3, FROMI(3), FX(0.4), SK_OUTER, 45, 0);
    gate(478, 780, 520, 796, 0, 1);      /* tilted (the browser's is flat) so a ball can't sit on it */
    /* the inlane / outlane separators leading into the flipper pivots */
    poly(sep, 3, FROMI(4), FX(0.45), SK_WALL, 30, 0);
    poly(sep, 3, FROMI(4), FX(0.45), SK_WALL, 30, 1);
    /* slingshots: the kicking face towards the middle, rubber-backed walls behind */
    wall(sling[0], sling[1], sling[2], sling[3], FROMI(5), FX(0.45), SK_WALL, 30);
    wall(sling[2], sling[3], sling[4], sling[5], FROMI(5), FX(0.45), SK_WALL, 30);
    s = wall(sling[0], sling[1], sling[4], sling[5], FROMI(5), FX(0.6), SK_SLING, 30);
    if (s) s->id = 0;
    wall(MX(sling[0]), sling[1], MX(sling[2]), sling[3], FROMI(5), FX(0.45), SK_WALL, 30);
    wall(MX(sling[2]), sling[3], MX(sling[4]), sling[5], FROMI(5), FX(0.45), SK_WALL, 30);
    s = wall(MX(sling[4]), sling[5], MX(sling[0]), sling[1], FROMI(5), FX(0.6), SK_SLING, 30);
    if (s) s->id = 1;
    /* the top lanes */
    for (i = 0; i < 4; i++) wall(150 + 60 * i, 800, 150 + 60 * i, 862, FROMI(4), FX(0.45), SK_WALL, 30);
    for (i = 0; i < 3; i++) sensor(150 + 60 * i, 832, 210 + 60 * i, 832, SN_LANE, i);
    post(58, 360); post(MX(58), 360);
    sensor(58, 282, 100, 282, SN_IN, 0);  sensor(20, 262, 58, 262, SN_OUT, 0);
    sensor(378, 282, 420, 282, SN_IN, 1); sensor(420, 262, 458, 262, SN_OUT, 1);
    sensor(478, 760, 520, 760, SN_LANEOUT, 0);
    flipper(0, 150, 126, 76, FROMI(11), FX(6.5), -30, 28);
    flipper(1, MX(150), 126, 76, FROMI(11), FX(6.5), 210, 152);
    T->plunger_x = FROMI(499);
    T->plunger_rest = FROMI(30) + PB_R;
    /* the lamps every table has: the top lanes, the bonus multiplier, the ball save */
    for (i = 0; i < 3; i++) lamp(L_LANE0 + i, 180 + 60 * i, 812, 11, LC_BLUE);
    for (i = 0; i < 4; i++) lamp(L_X2 + i, 194 + 30 * i, 360, 8, LC_YELLOW);
    lamp(L_SAVE, 239, 210, 10, LC_GREEN);
    /* arrows on the inlanes */
    arrow(4, 79, 320, 270); arrow(4, MX(79), 320, 270);
}

/* the left orbit: up-only past a spinner, round the top arch */
static void left_orbit(void)
{
    gate(0, 780, 50, 766, 0, 1);
    wall(50, 470, 50, 760, FROMI(4), FX(0.45), SK_WALL, 30);
    post(50, 470);
    spinner(0, 640, 50);
    lamp(L_SPIN0, 25, 590, 8, LC_YELLOW);
}

/* ---------------------------------------------------------------- Comet Tour */

static void comet_tour(void)
{
    static const int16_t path[] = { 354, 590, 0, 356, 680, 28, 350, 770, 58, 318, 870, 82, 250, 915, 92, 170, 905, 90,
                                    105, 850, 80, 72, 760, 64, 66, 640, 46, 72, 520, 30, 78, 420, 16, 79, 372, 8 };
    int i;
    T->name = "COMET TOUR";
    T->blurb = "THE ORIGINAL: RAMP, ORBIT, SAUCER";
    T->mission[0] = "VENUS FOG"; T->mission[1] = "MARS ROCKS"; T->mission[2] = "MERCURY DASH";
    T->mission[3] = "JUPITER STORM"; T->mission[4] = "SATURN RINGS";
    T->theme = TB_COMET;
    cabinet();
    left_orbit();
    ramp(330, 378, 520, path, 12, 79, 362);
    bumper(188, 700); bumper(292, 700); bumper(240, 618);
    for (i = 0; i < 3; i++) drop(462, 480 + i * 36, 462, 508 + i * 36, 0);
    T->saucer_x = FROMI(112); T->saucer_y = FROMI(560); T->saucer_r = FROMI(15);
    for (i = 0; i < 3; i++) lamp(L_LOCK0 + i, 354, 492 - i * 34, 9, LC_BLUE);
    lamp(L_JACKPOT, 354, 392, 12, LC_YELLOW);
    for (i = 0; i < 5; i++) lamp(L_M0 + i, 160 + i * 40, 440, 10, LC_RED);
    lamp(L_MISSION, 112, 604, 10, LC_RED);
    for (i = 0; i < 3; i++) lamp(L_DROP0 + i, 432, 494 + i * 36, 6, LC_ORANGE);
    lamp(L_SUPER, 239, 520, 16, LC_MAGENTA);
    /* the art: planets, a comet's tail across the middle */
    deco(DK_DISC, 3, 240, 700, 70, 70);
    deco(DK_RING, 5, 240, 700, 100, 30);
    deco_quad(4, 380, 330, 120, 470, 112, 456, 372, 316);
    deco(DK_DISC, 5, 384, 324, 18, 18);
    deco(DK_DISC, 4, 90, 470, 30, 30);
    arrow(5, 354, 560, 90); arrow(5, 25, 540, 90); arrow(5, 112, 520, 90);
    deco_text(5, 180, 260, "COMET");
}

/* ---------------------------------------------------------------- Deep Dive */

static void deep_dive(void)
{
    static const int16_t path[] = { 140, 590, 0, 138, 680, 28, 144, 770, 58, 176, 860, 82, 240, 908, 92, 310, 898, 90,
                                    372, 846, 80, 404, 760, 64, 412, 640, 46, 406, 520, 30, 400, 420, 16, 399, 372, 8 };
    int i;
    T->name = "DEEP DIVE";
    T->blurb = "FOUR JELLYFISH, AN UPPER FLIPPER";
    T->mission[0] = "CORAL REEF"; T->mission[1] = "SHIPWRECK"; T->mission[2] = "WHIRLPOOL";
    T->mission[3] = "SEA SERPENT"; T->mission[4] = "PEARL DIVER";
    T->theme = TB_DEEP;
    cabinet();
    /* the right orbit: up-only past a spinner, beside the plunger lane */
    wall(436, 480, 436, 740, FROMI(4), FX(0.45), SK_WALL, 30);
    post(436, 480);
    gate(436, 742, 478, 754, 0, 1);
    spinner(436, 640, 478);
    lamp(L_SPIN0, 457, 600, 8, LC_YELLOW);
    /* the ramp on the left, over the top and down to the right inlane */
    ramp(116, 164, 520, path, 12, 399, 362);
    /* jellyfish: four bumpers */
    bumper(232, 760); bumper(170, 690); bumper(300, 700); bumper(250, 640);
    /* the wreck: a bank of three targets across the middle */
    for (i = 0; i < 3; i++) drop(214 + i * 36, 560, 242 + i * 36, 560, 0);
    /* an upper flipper on the left wall, fed by a guide that ends at its pivot */
    wall(0, 566, 40, 506, FROMI(4), FX(0.45), SK_WALL, 30);
    flipper(0, 40, 500, 60, FROMI(9), FX(5.5), -25, 25);
    T->saucer_x = FROMI(380); T->saucer_y = FROMI(620); T->saucer_r = FROMI(15);
    for (i = 0; i < 3; i++) lamp(L_LOCK0 + i, 140, 492 - i * 34, 9, LC_BLUE);
    lamp(L_JACKPOT, 140, 392, 12, LC_YELLOW);
    for (i = 0; i < 5; i++) lamp(L_M0 + i, 180 + i * 30, 470, 9, LC_RED);
    lamp(L_MISSION, 380, 660, 10, LC_RED);
    for (i = 0; i < 3; i++) lamp(L_DROP0 + i, 228 + i * 36, 530, 6, LC_ORANGE);
    lamp(L_SUPER, 239, 420, 16, LC_MAGENTA);
    deco(DK_DISC, 3, 240, 700, 120, 100);
    deco(DK_DISC, 4, 100, 300, 20, 20); deco(DK_DISC, 4, 380, 300, 16, 16);
    deco(DK_RING, 5, 380, 620, 30, 30);
    arrow(5, 140, 560, 90); arrow(5, 457, 520, 90); arrow(5, 380, 580, 90);
    deco_text(5, 168, 260, "DEEP DIVE");
}

/* ---------------------------------------------------------------- Robot Foundry */

static void robot_foundry(void)
{
    static const int16_t path[] = { 239, 670, 0, 239, 750, 26, 252, 840, 56, 300, 905, 80, 370, 890, 88, 410, 820, 78,
                                    420, 720, 62, 416, 620, 46, 408, 520, 30, 402, 430, 16, 399, 372, 8 };
    int i;
    T->name = "ROBOT FOUNDRY";
    T->blurb = "TWO TARGET BANKS, A CENTRE RAMP";
    T->mission[0] = "POWER UP"; T->mission[1] = "ASSEMBLY"; T->mission[2] = "TURBINE";
    T->mission[3] = "CONVEYOR"; T->mission[4] = "CIRCUIT";
    T->theme = TB_FOUNDRY;
    cabinet();
    left_orbit();
    /* the centre ramp, round the top right and down to the right inlane */
    ramp(215, 263, 600, path, 11, 399, 362);
    /* three bumpers at the top left */
    bumper(110, 700); bumper(160, 760); bumper(165, 650);
    /* bank 1: three targets on the right wall, as on Comet Tour; bank 2: three across the left */
    for (i = 0; i < 3; i++) drop(462, 480 + i * 36, 462, 508 + i * 36, 0);
    for (i = 0; i < 3; i++) drop(86 + i * 36, 520, 114 + i * 36, 520, 1);
    /* an upper flipper on the right wall, fed by a guide that ends at its pivot */
    wall(478, 706, 462, 646, FROMI(4), FX(0.45), SK_WALL, 30);
    flipper(1, 462, 640, 58, FROMI(9), FX(5.5), 205, 155);
    T->saucer_x = FROMI(360); T->saucer_y = FROMI(590); T->saucer_r = FROMI(15);
    for (i = 0; i < 3; i++) lamp(L_LOCK0 + i, 239, 572 - i * 34, 9, LC_BLUE);
    lamp(L_JACKPOT, 239, 470, 12, LC_YELLOW);
    for (i = 0; i < 5; i++) lamp(L_M0 + i, 300 + i * 26, 470 - i * 14, 9, LC_RED);
    lamp(L_MISSION, 360, 550, 10, LC_RED);
    for (i = 0; i < 3; i++) lamp(L_DROP0 + i, 432, 494 + i * 36, 6, LC_ORANGE);
    for (i = 0; i < 3; i++) lamp(L_DROP0 + 3 + i, 100 + i * 36, 494, 6, LC_ORANGE);
    lamp(L_SUPER, 239, 400, 16, LC_MAGENTA);
    deco_quad(3, 180, 560, 300, 560, 320, 900, 160, 900);
    deco(DK_RING, 4, 140, 705, 75, 75);
    deco(DK_DISC, 5, 360, 590, 26, 26);
    arrow(5, 239, 640, 90); arrow(5, 25, 540, 90); arrow(5, 360, 530, 90);
    deco_text(5, 156, 260, "FOUNDRY");
}

/* ---------------------------------------------------------------- the grid */

static void grid_add(int x0, int y0, int x1, int y1, uint16_t ref)
{
    int gx, gy, gx0 = x0 >> PB_CELL_SH, gx1 = x1 >> PB_CELL_SH, gy0 = (y0 + 64) >> PB_CELL_SH, gy1 = (y1 + 64) >> PB_CELL_SH;
    if (gx0 < 0) gx0 = 0;
    if (gy0 < 0) gy0 = 0;
    if (gx1 >= PB_GX) gx1 = PB_GX - 1;
    if (gy1 >= PB_GY) gy1 = PB_GY - 1;
    for (gy = gy0; gy <= gy1; gy++)
        for (gx = gx0; gx <= gx1; gx++)
            if (T->ngrid[gy][gx] < PB_GMAX) T->grid[gy][gx][T->ngrid[gy][gx]++] = ref;
            else T->ngrid[gy][gx] = PB_GMAX + 1;          /* overflowed: the tests look for this */
}

static void build_grid(void)
{
    int i;
    for (i = 0; i < T->nseg; i++) {
        const Seg *s = &T->seg[i];
        grid_add(s->bb[0], s->bb[1], s->bb[2], s->bb[3], (uint16_t)i);
    }
    for (i = 0; i < T->ncirc; i++) {
        const Circ *c = &T->circ[i];
        int g = FINT(c->r + PB_R) + 4, x = FINT(c->cx), y = FINT(c->cy);
        grid_add(x - g, y - g, x + g, y + g, (uint16_t)((1 << 12) | i));
    }
    for (i = 0; i < T->nsens; i++) {
        const Sensor *s = &T->sens[i];
        grid_add(s->bb[0], s->bb[1], s->bb[2], s->bb[3], (uint16_t)((2 << 12) | i));
    }
}

void pb_build_table(Table *t, int which)
{
    memset(t, 0, sizeof *t);
    T = t;
    switch (which) {
    case TB_DEEP:    deep_dive(); break;
    case TB_FOUNDRY: robot_foundry(); break;
    default:         comet_tour(); break;
    }
    build_grid();
}
