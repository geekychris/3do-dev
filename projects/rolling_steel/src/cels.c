/*
 * Cels (3DO headers only).
 *
 * A flat-shaded quadrilateral is one texel of its colour mapped onto the
 * four corners A B C D: HDX/HDY = B - A, VDX/VDY = D - A,
 * HDDX/HDDY = (C - D) - (B - A). Sprites (the marbles, scenery spheres,
 * shadows) are small 16-bit images scaled by the cel engine, optionally
 * with a pixel-processor mode that mixes them with the frame buffer.
 */
#include "graphics.h"
#include "celutils.h"
#include "cels.h"

#define MAX_CELS 1100
#define MAX_TEX  24

static uint32 col[MAX_CELS];
static CCB    cel[MAX_CELS];
static unsigned char kind[MAX_CELS];       /* 0 quad, 1 + texture: sprite */
static CCB   *tmpl, *tex[MAX_TEX];
static int    tex_w[MAX_TEX], tex_h[MAX_TEX], ntex, n;

int cels_init(void)
{
    int i;
    tmpl = CreateCel(1, 1, 16, CREATECEL_UNCODED, col);
    if (!tmpl)
        return 0;
    for (i = 0; i < MAX_CELS; i++) {
        cel[i] = *tmpl;
        cel[i].ccb_Flags = (tmpl->ccb_Flags | CCB_NPABS | CCB_SPABS | CCB_BGND) & ~CCB_LAST;
        cel[i].ccb_SourcePtr = (CelData *)&col[i];
        cel[i].ccb_NextPtr = &cel[i + 1 < MAX_CELS ? i + 1 : i];
    }
    return 1;
}

static int lasts[4], nlasts;

void cels_begin(void)
{
    int i;
    if (n > 0)
        cel[n - 1].ccb_Flags &= ~CCB_LAST;
    for (i = 0; i < nlasts; i++) cel[lasts[i]].ccb_Flags &= ~CCB_LAST;
    nlasts = 0;
    n = 0;
}

int cels_mark(void) { return n; }

/* cels from..to-1 as a list of their own (split screen) */
void *cels_segment(int from, int to)
{
    if (to <= from) return 0;
    cel[to - 1].ccb_Flags |= CCB_LAST;
    if (nlasts < 4) lasts[nlasts++] = to - 1;
    return &cel[from];
}

int cels_count(void) { return n; }

void cels_quad(long ax, long ay, long bx, long by, long cx, long cy, long dx, long dy,
               unsigned short rgb15, unsigned long pixc)
{
    CCB *c;
    if (!tmpl || n >= MAX_CELS)
        return;
    col[n] = ((uint32)rgb15 << 16) | rgb15;
    c = &cel[n];
    if (kind[n]) {
        *c = *tmpl;
        c->ccb_Flags = (tmpl->ccb_Flags | CCB_NPABS | CCB_SPABS | CCB_BGND) & ~CCB_LAST;
        c->ccb_SourcePtr = (CelData *)&col[n];
        c->ccb_NextPtr = &cel[n + 1 < MAX_CELS ? n + 1 : n];
        kind[n] = 0;
    }
    c->ccb_PIXC = pixc ? pixc : tmpl->ccb_PIXC;
    c->ccb_XPos = ax;
    c->ccb_YPos = ay;
    c->ccb_HDX = (bx - ax) << 4;              /* 16.16 -> 12.20 */
    c->ccb_HDY = (by - ay) << 4;
    c->ccb_VDX = dx - ax;
    c->ccb_VDY = dy - ay;
    c->ccb_HDDX = ((cx - dx) - (bx - ax)) << 4;
    c->ccb_HDDY = ((cy - dy) - (by - ay)) << 4;
    n++;
}

int cels_tex(int w, int h, unsigned short *pixels)
{
    CCB *c;
    if (ntex >= MAX_TEX)
        return -1;
    c = CreateCel(w, h, 16, CREATECEL_UNCODED, pixels);
    if (!c)
        return -1;
    c->ccb_Flags &= ~CCB_BGND;                /* pixel value 0 is transparent */
    tex[ntex] = c;
    tex_w[ntex] = w;
    tex_h[ntex] = h;
    return ntex++;
}

/* a texture centred on (x, y) at hx x hy half size (16.16 px) */
void cels_sprite(int t, long x, long y, long hx, long hy, unsigned long pixc)
{
    CCB *c;
    if (t < 0 || t >= ntex || n >= MAX_CELS || hx <= 0 || hy <= 0)
        return;
    c = &cel[n];
    if (kind[n] != 1 + t) {
        *c = *tex[t];
        c->ccb_Flags = (tex[t]->ccb_Flags | CCB_NPABS | CCB_SPABS) & ~CCB_LAST;
        c->ccb_NextPtr = &cel[n + 1 < MAX_CELS ? n + 1 : n];
        kind[n] = (unsigned char)(1 + t);
    }
    c->ccb_PIXC = pixc ? pixc : tex[t]->ccb_PIXC;
    c->ccb_XPos = x - hx;
    c->ccb_YPos = y - hy;
    c->ccb_HDX = ((hx * 2) / tex_w[t]) << 4;  /* 16.16 px per texel -> 12.20 */
    c->ccb_HDY = 0;
    c->ccb_VDX = 0;
    c->ccb_VDY = (hy * 2) / tex_h[t];
    c->ccb_HDDX = 0;
    c->ccb_HDDY = 0;
    n++;
}

void *cels_list(void)
{
    if (!n)
        return 0;
    cel[n - 1].ccb_Flags |= CCB_LAST;
    return cel;
}
