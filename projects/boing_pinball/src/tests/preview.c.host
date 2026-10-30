// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * preview.c - make host-preview: each table drawn top down (tests/table_N.ppm,
 * 1 px = 1 mm) with the ball's paths from bot-played games over it, and the
 * games' statistics: what got hit, how long balls lasted, any ball that
 * left the table or stuck. For designing the layouts.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "a3d.h"
#include "../pb.h"

#define W 540
#define H 1120
static unsigned char img[H][W][3];
static Table table;
static Game game;

static void px(int x, int y, int r, int g, int b)
{
    int iy = 1060 - y, ix = x + 10;
    if (ix < 0 || ix >= W || iy < 0 || iy >= H) return;
    img[iy][ix][0] = (unsigned char)r; img[iy][ix][1] = (unsigned char)g; img[iy][ix][2] = (unsigned char)b;
}

static void disc(double cx, double cy, double rad, int r, int g, int b)
{
    int x, y;
    for (y = (int)(cy - rad); y <= (int)(cy + rad) + 1; y++)
        for (x = (int)(cx - rad); x <= (int)(cx + rad) + 1; x++)
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= rad * rad) px(x, y, r, g, b);
}

static void capsule(double ax, double ay, double bx, double by, double rad, int r, int g, int b)
{
    int i, n = (int)(sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay))) + 1;
    for (i = 0; i <= n; i++) disc(ax + (bx - ax) * i / n, ay + (by - ay) * i / n, rad, r, g, b);
}
#define D(f) ((f) / 65536.0)

static void draw_table(void)
{
    int i;
    memset(img, 20, sizeof img);
    for (i = 0; i < table.ndeco; i++) {
        const Deco *d = &table.deco[i];
        if (d->kind == DK_DISC) disc(d->x, d->y, d->a, 40, 40, 70);
    }
    for (i = 0; i < L_COUNT; i++) if (table.lamp[i].r) disc(table.lamp[i].x, table.lamp[i].y, table.lamp[i].r, 90, 70, 30);
    for (i = 0; i < table.nseg; i++) {
        const Seg *s = &table.seg[i];
        int r = 200, g = 200, b = 210;
        if (s->kind == SK_OUTER) { r = 150; g = 90; b = 200; }
        if (s->kind == SK_SLING) { r = 255; g = 80; b = 80; }
        if (s->kind == SK_GATE) { r = 80; g = 255; b = 80; }
        if (s->kind == SK_DROP) { r = 255; g = 180; b = 30; }
        capsule(D(s->ax), D(s->ay), D(s->bx), D(s->by), D(s->r), r, g, b);
    }
    for (i = 0; i < table.ncirc; i++) {
        const Circ *c = &table.circ[i];
        disc(D(c->cx), D(c->cy), D(c->r), c->bumper ? 230 : 240, c->bumper ? 40 : 240, c->bumper ? 40 : 230);
    }
    for (i = 0; i < table.nsens; i++) {
        const Sensor *s = &table.sens[i];
        capsule(D(s->ax), D(s->ay), D(s->bx), D(s->by), 0.6, 0, 200, 255);
    }
    for (i = 0; i < table.nflip; i++) {
        const FlipDef *f = &table.flip[i];
        double a = D(f->rest);
        capsule(D(f->px), D(f->py), D(f->px) + D(f->len) * cos(a), D(f->py) + D(f->len) * sin(a), D(f->r2) + 2, 255, 255, 255);
        a = D(f->up);
        capsule(D(f->px), D(f->py), D(f->px) + D(f->len) * cos(a), D(f->py) + D(f->len) * sin(a), 1.5, 120, 120, 120);
    }
    for (i = 0; i < table.nramp; i++) {
        const Ramp *r = &table.ramp[i];
        int k;
        for (k = 1; k < r->n; k++) capsule(r->pts[k - 1][0], r->pts[k - 1][1], r->pts[k][0], r->pts[k][1], 1.5, 90, 170, 255);
    }
    disc(D(table.saucer_x), D(table.saucer_y), D(table.saucer_r), 0, 0, 0);
}

int main(int argc, char **argv)
{
    int t, games = argc > 1 ? atoi(argv[1]) : 20;
    for (t = 0; t < TB_COUNT; t++) {
        long hits[EV_COUNT];
        long steps = 0, escaped = 0, stuck = 0, balls = 0, maxsub = 0, tests = 0, cell_over = 0;
        int gnum, gy, gx, maxcell = 0;
        char fn[64];
        FILE *f;
        memset(hits, 0, sizeof hits);
        pb_build_table(&table, t);
        for (gy = 0; gy < PB_GY; gy++) for (gx = 0; gx < PB_GX; gx++) {
            if (table.ngrid[gy][gx] > PB_GMAX) cell_over++;
            if (table.ngrid[gy][gx] > maxcell) maxcell = table.ngrid[gy][gx];
        }
        draw_table();
        for (gnum = 0; gnum < games; gnum++) {
            Bot bot = { 0 };
            PbInput in;
            int32_t still = 0, s;
            bot.rng = 1234 + gnum * 77;
            pb_game_init(&game, &table, 99 + gnum);
            pb_game_start(&game);
            for (s = 0; s < PB_SECS(600) && game.state != GS_OVER; s++) {
                int i, k;
                pb_bot(&game, &in, &bot);
                pb_game_tick(&game, &in);
                steps++;
                if (game.w.substeps > maxsub) maxsub = game.w.substeps;
                for (k = 0; k < game.nev; k++) hits[game.ev[k].type]++;
                game.nev = 0;
                for (i = 0; i < game.w.nballs; i++) {
                    Ball *b = &game.w.balls[i];
                    int x = FINT(b->x), y = FINT(b->y);
                    if (b->state == BS_PLAY || b->state == BS_RAMP) {
                        if (gnum < 6) px(x, y, b->state == BS_RAMP ? 120 : 255, b->state == BS_RAMP ? 255 : 230, 0);
                        if (x < -3 || x > 523 || y > 1043) { escaped++; printf("  escaped: table %d game %d at %d,%d\n", t, gnum, x, y); b->state = BS_DRAINED; }
                    }
                }
                /* stuck: a ball in play that hasn't moved far in 8 s */
                {
                    static int32_t lx, ly;
                    if (game.w.nballs && game.w.balls[0].state == BS_PLAY) {
                        int32_t dx = game.w.balls[0].x - lx, dy = game.w.balls[0].y - ly;
                        if (dx < 0) dx = -dx;
                        if (dy < 0) dy = -dy;
                        if (dx > FROMI(30) || dy > FROMI(30)) { still = 0; lx = game.w.balls[0].x; ly = game.w.balls[0].y; }
                        else if (++still > PB_SECS(8)) {
                            stuck++; still = 0;
                            printf("  stuck: table %d game %d at %ld,%ld v %ld,%ld\n", t, gnum, (long)FINT(game.w.balls[0].x), (long)FINT(game.w.balls[0].y),
                                   (long)FINT(game.w.balls[0].vx), (long)FINT(game.w.balls[0].vy));
                            game.w.balls[0].vy = FROMI(-2000); game.w.balls[0].y = FROMI(-30);
                        }
                    } else still = 0;
                }
                tests += game.w.tests; game.w.tests = 0;
            }
            balls += game.ball_no;
        }
        printf("%s: %d games, %.0f s a ball, score avg ?, substeps max %ld avg tests/step %.1f, grid max %d%s\n",
               table.name, games, steps / (double)PB_HZ / (balls ? balls : 1), maxsub, tests / (double)(steps ? steps : 1),
               maxcell, cell_over ? " OVERFLOW" : "");
        printf("  bumper %ld sling %ld drop %ld bank %ld spin %ld ramp %ld rampFail %ld saucer %ld mission %ld/%ld lanes %ld skill %ld multiball %ld jackpot %ld drain %ld outlane %ld save %ld\n",
               hits[EV_BUMPER], hits[EV_SLING], hits[EV_DROP], hits[EV_BANK], hits[EV_SPIN], hits[EV_RAMP], hits[EV_RAMPFAIL],
               hits[EV_SAUCER], hits[EV_MISSIONDONE], hits[EV_MISSIONSTART], hits[EV_LANESDONE], hits[EV_SKILL],
               hits[EV_MULTIBALL], hits[EV_JACKPOT], hits[EV_DRAIN], hits[EV_OUTLANE], hits[EV_SAVE]);
        if (escaped || stuck) printf("  ESCAPED %ld STUCK %ld\n", escaped, stuck);
        sprintf(fn, "tests/table_%d.ppm", t);
        f = fopen(fn, "wb");
        if (f) { fprintf(f, "P6 %d %d 255\n", W, H); fwrite(img, 1, sizeof img, f); fclose(f); }
    }
    return 0;
}
