/*
 * Sprite textures for the cel engine, drawn here once (32 x 32, 16-bit,
 * 0 = transparent): the chomper (4 facings x 3 mouth openings, normal and
 * powered), the spooks (4 colours, frightened, flashing, eyes only) and
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

/* chomper: mouth along axis (ax, ay), half-opening given by tan in 1/16 */
static int chomper(int powered, int ax, int ay, int open16)
{
    unsigned short *p = newtex();
    int x, y;
    if (!p) return -1;
    for (y = 0; y < S; y++)
        for (x = 0; x < S; x++) {
            int dx = x - 15, dy = y - 15, along, across, l;
            if (dx * dx + dy * dy > 15 * 15) continue;
            along = dx * ax + dy * ay;           /* toward the mouth */
            across = dx * ay - dy * ax;
            if (across < 0) across = -across;
            if (along > 1 && across * 16 < along * open16) continue;   /* the mouth */
            l = light(x, y, 15, 15, 15);
            p[y * S + x] = powered ? rgb(255 * l / 256, 150 * l / 256, 30 * l / 256)
                                   : rgb(255 * l / 256, 214 * l / 256, 40 * l / 256);
        }
    /* eye: above the centre when facing left/right, to its left when facing
     * up/down, a little back from the mouth */
    {
        int ex = 15 - (ay != 0 ? 6 : 0) - ax * 2, ey = 15 - (ax != 0 ? 6 : 0) - ay * 2;
        for (y = -2; y <= 2; y++)
            for (x = -2; x <= 2; x++)
                if (x * x + y * y <= 4)
                    p[(ey + y) * S + ex + x] = rgb(20, 20, 30);
    }
    return pc_tex_create(S, S, p);
}

static int ghost(int r, int g, int b, int fright, int eyes_only)
{
    unsigned short *p = newtex();
    int x, y;
    if (!p) return -1;
    if (!eyes_only)
        for (y = 1; y < S; y++)
            for (x = 2; x < S - 2; x++) {
                int dx = x - 16, dy = y - 14, l, in;
                if (y <= 14) in = dx * dx + dy * dy <= 14 * 14;      /* dome */
                else {
                    /* skirt: three scallops along the bottom */
                    int sx = (x - 2) % 9 - 4, bottom = 27 + (sx * sx < 9 ? 3 : 0);
                    in = y <= bottom;
                }
                if (!in) continue;
                l = light(x, y, 16, 14, 14);
                p[y * S + x] = rgb(r * l / 256, g * l / 256, b * l / 256);
            }
    if (fright && !eyes_only) {
        /* small eyes and a wavy mouth */
        static const int mx[] = { 7, 9, 11, 13, 15, 17, 19, 21, 23, 25 };
        int i;
        for (y = 10; y <= 12; y++) for (x = 10; x <= 12; x++) p[y * S + x] = rgb(255, 220, 200);
        for (y = 10; y <= 12; y++) for (x = 20; x <= 22; x++) p[y * S + x] = rgb(255, 220, 200);
        for (i = 0; i < 10; i++) {
            int yy = 20 + (i & 1);
            p[yy * S + mx[i]] = p[yy * S + mx[i] + 1] = rgb(255, 220, 200);
        }
    } else {
        /* white eyes with blue pupils */
        int e;
        for (e = 0; e < 2; e++) {
            int cx = e ? 21 : 11, cy = 12;
            for (y = -5; y <= 5; y++)
                for (x = -4; x <= 4; x++)
                    if (x * x * 25 + y * y * 16 <= 400)
                        p[(cy + y) * S + cx + x] = rgb(250, 250, 255);
            for (y = -2; y <= 2; y++)
                for (x = -2; x <= 2; x++)
                    if (x * x + y * y <= 4)
                        p[(cy + 1 + y) * S + cx + 1 + x] = rgb(30, 40, 200);
        }
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
    static const int open16[3] = { 1, 8, 16 };       /* ~5, 25, 45 degrees */
    int pw, d, m;
    for (pw = 0; pw < 2; pw++)
        for (d = 0; d < 4; d++)
            for (m = 0; m < 3; m++)
                if ((tex_chomper[pw][d][m] = chomper(pw, dir[d][0], dir[d][1], open16[m])) < 0)
                    return 0;
    tex_ghost[GT_BLAZE] = ghost(255, 64, 51, 0, 0);
    tex_ghost[GT_PETAL] = ghost(255, 140, 216, 0, 0);
    tex_ghost[GT_FROST] = ghost(77, 242, 255, 0, 0);
    tex_ghost[GT_EMBER] = ghost(255, 166, 51, 0, 0);
    tex_ghost[GT_FRIGHT] = ghost(50, 70, 255, 1, 0);
    tex_ghost[GT_FLASH] = ghost(240, 240, 255, 1, 0);
    tex_ghost[GT_EYES] = ghost(0, 0, 0, 0, 1);
    tex_key = key();
    for (d = 0; d < GT_COUNT; d++) if (tex_ghost[d] < 0) return 0;
    return tex_key >= 0;
}
