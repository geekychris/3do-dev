/*
 * 3DO port: Void Trader's shaded triangles drawn by the cel engine.
 *
 * Each triangle is a cel of one texel (the face colour) mapped onto a
 * quadrilateral whose last two corners coincide: A, B, C, C. For a 1x1
 * source that is HDX/HDY = B - A, VDX/VDY = C - A and HDDX/HDDY = A - B.
 * The list starts with the space-and-stars layer and keeps the painter's
 * order, so engine3d.c only swaps AreaMove/AreaDraw/AreaEnd for tc_tri().
 * (3DO headers only.)
 */
#include "graphics.h"
#include "celutils.h"
#include "tri_cels.h"

#define MAX_TRI 400

static uint32 col[MAX_TRI];
static CCB    cel[MAX_TRI];
static CCB   *tmpl;
static CCB   *base;
static int    ntri;

int
tc_init(void)
{
    tmpl = CreateCel(1, 1, 16, CREATECEL_UNCODED, col);
    return tmpl != 0;
}

void tc_begin(void *base_ccb) { base = (CCB *)base_ccb; ntri = 0; }

void
tc_tri(long ax, long ay, long bx, long by, long cx, long cy, unsigned short rgb15)
{
    CCB *c;
    if (!tmpl || ntri >= MAX_TRI)
        return;
    col[ntri] = ((uint32)rgb15 << 16) | rgb15;
    c = &cel[ntri];
    *c = *tmpl;
    c->ccb_Flags = (tmpl->ccb_Flags | CCB_NPABS | CCB_SPABS | CCB_BGND) & ~CCB_LAST;
    c->ccb_SourcePtr = (CelData *)&col[ntri];
    c->ccb_XPos = ax;
    c->ccb_YPos = ay;
    c->ccb_HDX = (bx - ax) << 4;          /* 16.16 -> 12.20 */
    c->ccb_HDY = (by - ay) << 4;
    c->ccb_VDX = cx - ax;
    c->ccb_VDY = cy - ay;
    c->ccb_HDDX = (ax - bx) << 4;
    c->ccb_HDDY = (ay - by) << 4;
    if (ntri > 0)
        cel[ntri - 1].ccb_NextPtr = c;
    ntri++;
}

void *
tc_list(void)
{
    CCB *last;
    if (!base)
        return 0;
    base->ccb_Flags |= CCB_NPABS;
    if (!ntri) {
        base->ccb_Flags |= CCB_LAST;
        return base;
    }
    base->ccb_Flags &= ~CCB_LAST;
    base->ccb_NextPtr = &cel[0];
    last = &cel[ntri - 1];
    last->ccb_Flags |= CCB_LAST;
    last->ccb_NextPtr = 0;
    return base;
}
