/*
 * Flat-shaded quadrilaterals as cels: one texel of the colour, mapped onto
 * the four corners A B C D (clockwise or not - both are drawn). For a 1x1
 * source: HDX/HDY = B - A, VDX/VDY = D - A, HDDX/HDDY = (C - D) - (B - A).
 * A triangle is a quad with C == D. 3DO headers only.
 */
#include "graphics.h"
#include "celutils.h"
#include "pc.h"

#define MAX_CELS 1400

static uint32 col[MAX_CELS];
static CCB    cel[MAX_CELS];
static CCB   *tmpl;
static int    n;

int
pc_cels_init(void)
{
    int i;
    tmpl = CreateCel(1, 1, 16, CREATECEL_UNCODED, col);
    if (!tmpl)
        return 0;
    /* everything but position, shape, colour and the link is the same
     * for every quad: set it once */
    for (i = 0; i < MAX_CELS; i++) {
        cel[i] = *tmpl;
        cel[i].ccb_Flags = (tmpl->ccb_Flags | CCB_NPABS | CCB_SPABS | CCB_BGND) & ~CCB_LAST;
        cel[i].ccb_SourcePtr = (CelData *)&col[i];
        cel[i].ccb_NextPtr = &cel[i + 1 < MAX_CELS ? i + 1 : i];
    }
    return 1;
}

void pc_cels_begin(void)
{
    if (n > 0)
        cel[n - 1].ccb_Flags &= ~CCB_LAST;      /* last frame's end */
    n = 0;
}
int  pc_cels_count(void) { return n; }

void
pc_quad(long ax, long ay, long bx, long by, long cx, long cy, long dx, long dy,
        unsigned short rgb15)
{
    CCB *c;
    if (!tmpl || n >= MAX_CELS)
        return;
    col[n] = ((uint32)rgb15 << 16) | rgb15;
    c = &cel[n];
    c->ccb_XPos = ax;
    c->ccb_YPos = ay;
    c->ccb_HDX = (bx - ax) << 4;               /* 16.16 -> 12.20 */
    c->ccb_HDY = (by - ay) << 4;
    c->ccb_VDX = dx - ax;
    c->ccb_VDY = dy - ay;
    c->ccb_HDDX = ((cx - dx) - (bx - ax)) << 4;
    c->ccb_HDDY = ((cy - dy) - (by - ay)) << 4;
    n++;
}

void *
pc_cels_list(void)
{
    if (!n)
        return 0;
    cel[n - 1].ccb_Flags |= CCB_LAST;
    return cel;
}
