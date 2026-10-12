/*
 * Sprite textures for the cel engine, drawn here once (32 x 32, 16-bit,
 * 0 = transparent): the chomper (4 facings x 3 mouth openings, normal and
 * powered), the spooks (4 colours, frightened, flashing, eaten) and
 * the key. In the Unity game these are small 3D models.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "pc.h"
#include "sprites.h"

#define S 32
int tex_chomper[2][4][3];
int tex_ghost[GT_COUNT];
int tex_key;

static unsigned short *newtex(void)
{
    return (unsigned short *)AllocMem(S * S * 2, MEMF_CLEAR);
}

static unsigned short rgb(int r, int g, int b)
{
    if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    r = r >> 3; g = g >> 3; b = b >> 3;
    if (!(r | g | b)) b = 1;                     /* black must not be 0 (transparent) */
    return (unsigned short)((r << 10) | (g << 5) | b);
}

/* lit sphere shading: brighter toward the upper left */
static int light(int x, int y, int cx, int cy, int rad)
{
    /* 300 - 140 d^2 / r^2, with the divide done once per radius */
    static int last_rad = -1, off;
    static long k;
    int dx, dy;
    if (rad != last_rad) { last_rad = rad; k = (140L << 16) / (rad * rad); off = rad / 3; }
    dx = x - cx + off; dy = y - cy + off;
    return 300 - (int)(((long)(dx * dx + dy * dy) * k) >> 16);   /* 160 .. 300 (/256) */
}

static void dot(unsigned short *p, int cx, int cy, int r2, unsigned short c)
{
    int x, y;
    for (y = -3; y <= 3; y++)
        for (x = -3; x <= 3; x++)
            if (x * x + y * y <= r2 && cx + x >= 0 && cx + x < S && cy + y >= 0 && cy + y < S)
                p[(cy + y) * S + cx + x] = c;
}

/* chomper: a round green critter seen from above, facing (ax, ay): two
 * eyes up front and an oval mouth on its leading edge that opens wider
 * (open16: 1 shut .. 16 wide) */
static int chomper(int powered, int ax, int ay, int open16)
{
    unsigned short *p = newtex();
    int x, y, s;
    if (!p) return -1;
    for (y = 0; y < S; y++)
        for (x = 0; x < S; x++) {
            int dx = x - 15, dy = y - 15, along, across, l, mw, mh;
            if (dx * dx + dy * dy > 14 * 14) continue;
            along = dx * ax + dy * ay;           /* toward the front */
            across = dx * ay - dy * ax;
            l = light(x, y, 15, 15, 14);
            p[y * S + x] = powered ? rgb(255 * l / 256, 200 * l / 256, 60 * l / 256)
                                   : rgb(140 * l / 256, 230 * l / 256, 60 * l / 256);
            /* the mouth: an oval centred on the front edge */
            mw = 3 + open16 / 3;
            mh = 1 + open16 / 4;
            along -= 12;
            if (across * across * mh * mh + along * along * mw * mw <= mw * mw * mh * mh)
                p[y * S + x] = rgb(110, 20, 40);
        }
    /* eyes either side of the nose, pupils looking ahead */
    for (s = -1; s <= 1; s += 2) {
        int ex = 15 + ax * 4 + ay * s * 5, ey = 15 + ay * 4 - ax * s * 5;
        dot(p, ex, ey, 6, rgb(250, 250, 240));
        dot(p, ex + ax, ey + ay, 1, rgb(20, 20, 30));
    }
    return pc_tex_create(S, S, p);
}

/* spook: a floating jellyfish - a bell with four trailing strands and
 * one big slit-pupilled eye. fright: grey, eye shut. wisp_only: what is
 * left after it is eaten, a small puff drifting home. */
static int spook(int r, int g, int b, int fright, int wisp_only)
{
    unsigned short *p = newtex();
    int x, y, i;
    if (!p) return -1;
    if (wisp_only) {
        for (y = 10; y < 22; y++)
            for (x = 10; x < 22; x++) {
                int dx = x - 16, dy = y - 16;
                if (dx * dx + dy * dy <= 25) p[y * S + x] = rgb(190, 200, 220);
            }
        dot(p, 16, 15, 2, rgb(40, 40, 60));
        return pc_tex_create(S, S, p);
    }
    for (y = 1; y < 21; y++)
        for (x = 3; x < S - 3; x++) {
            int dx = x - 16, dy = y - 13, l;
            /* bell: a circle on top, squashed to an ellipse below */
            if (dy <= 0 ? dx * dx + dy * dy > 12 * 12 : dx * dx * 49 + dy * dy * 144 > 144 * 49)
                continue;
            l = light(x, y, 16, 12, 12);
            p[y * S + x] = rgb(r * l / 256, g * l / 256, b * l / 256);
        }
    for (i = 0; i < 4; i++) {                    /* strands, wavy */
        int sx = 8 + i * 5 + (i > 1);
        for (y = 19; y < 31; y++) {
            int wx = sx + (((y + i * 2) / 3) & 1);
            p[y * S + wx] = p[y * S + wx + 1] = rgb(r * 3 / 4, g * 3 / 4, b * 3 / 4);
        }
    }
    if (fright) {
        /* eye shut tight: a curved lid */
        for (x = 11; x <= 21; x++) {
            int dx = x - 16;
            y = 11 + (dx * dx) / 12;
            p[y * S + x] = p[(y + 1) * S + x] = rgb(40, 40, 60);
        }
    } else {
        for (y = -5; y <= 5; y++)
            for (x = -6; x <= 6; x++)
                if (x * x * 25 + y * y * 36 <= 900)
                    p[(11 + y) * S + 16 + x] = rgb(250, 245, 220);
        for (y = -4; y <= 4; y++)                /* slit pupil */
            p[(11 + y) * S + 16] = p[(11 + y) * S + 17] = rgb(20, 10, 30);
    }
    return pc_tex_create(S, S, p);
}

static int key(void)
{
    unsigned short *p = newtex();
    int x, y;
    if (!p) return -1;
    for (y = 0; y < S; y++)
        for (x = 0; x < S; x++) {
            int dx = x - 10, dy = y - 10, d2 = dx * dx + dy * dy, gold = 0;
            if (d2 <= 64 && d2 >= 16) gold = 1;                    /* the bow (ring) */
            if (y >= 9 && y <= 12 && x >= 17 && x <= 29) gold = 1;  /* shaft */
            if (x >= 22 && x <= 24 && y >= 12 && y <= 17) gold = 1; /* teeth */
            if (x >= 27 && x <= 29 && y >= 12 && y <= 19) gold = 1;
            if (gold) {
                int l = 200 + ((31 - x - y) * 2);
                p[y * S + x] = rgb(255 * l / 256, 200 * l / 256, 50 * l / 256);
            }
        }
    return pc_tex_create(S, S, p);
}

int sprites_init(void)
{
    static const int dir[4][2] = { { 1, 0 }, { 0, -1 }, { -1, 0 }, { 0, 1 } };
    static const int open16[3] = { 1, 8, 16 };       /* shut, half, wide */
    int pw, d, m;
    for (pw = 0; pw < 2; pw++)
        for (d = 0; d < 4; d++)
            for (m = 0; m < 3; m++)
                if ((tex_chomper[pw][d][m] = chomper(pw, dir[d][0], dir[d][1], open16[m])) < 0)
                    return 0;
    tex_ghost[GT_GLOOM] = spook(170, 80, 255, 0, 0);
    tex_ghost[GT_SPARK] = spook(240, 220, 60, 0, 0);
    tex_ghost[GT_MIST] = spook(210, 210, 230, 0, 0);
    tex_ghost[GT_MOSS] = spook(40, 200, 170, 0, 0);
    tex_ghost[GT_FRIGHT] = spook(136, 144, 168, 1, 0);
    tex_ghost[GT_FLASH] = spook(240, 240, 255, 1, 0);
    tex_ghost[GT_EYES] = spook(0, 0, 0, 0, 1);
    tex_key = key();
    for (d = 0; d < GT_COUNT; d++) if (tex_ghost[d] < 0) return 0;
    return tex_key >= 0;
}
