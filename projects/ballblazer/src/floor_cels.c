/*
 * 3DO port: Ballblazer's chequered floor drawn by the cel engine.
 *
 * The camera always looks along the pitch, so in each band of screen rows
 * that lies in one row of cells, the cell edges are straight lines to the
 * vanishing point. One cel per band draws it: the source is a single row
 * of alternating texels (one per cell), and the cel engine maps each texel
 * onto the trapezoid between the band's top and bottom edges (HDX at the
 * top, HDX + HDDX at the bottom). pitch.c computes the geometry; this file
 * only holds the CCBs (3DO headers only, apart from the Amiga ones).
 */
#include "graphics.h"
#include "celutils.h"
#include "string.h"
#include "floor_cels.h"

#define TEX_W     260
#define MAX_CELS  (2 * 96)

static uint16 tex[TEX_W];
static CCB  *tmpl;
static CCB   cels[MAX_CELS];
static int   ncels;

int
fc_init(unsigned short even_rgb15, unsigned short odd_rgb15)
{
    int i;
    for (i = 0; i < TEX_W; i++)
        tex[i] = (i & 1) ? odd_rgb15 : even_rgb15;
    tmpl = CreateCel(TEX_W, 1, 16, CREATECEL_UNCODED, tex);
    return tmpl != 0;
}

void fc_begin(void) { ncels = 0; }

void
fc_band(long x16, long y16, long hdx20, long vdx16, long vdy16, long hddx20, int texels)
{
    CCB *c;
    uint32 pre1;
    if (!tmpl || ncels >= MAX_CELS || texels < 1 || vdy16 <= 0)
        return;
    if (texels > TEX_W)
        texels = TEX_W;
    c = &cels[ncels++];
    *c = *tmpl;
    c->ccb_Flags = (tmpl->ccb_Flags | CCB_NPABS | CCB_SPABS | CCB_BGND) & ~CCB_LAST;
    c->ccb_SourcePtr = (CelData *)tex;
    c->ccb_XPos = x16;
    c->ccb_YPos = y16;
    c->ccb_HDX = hdx20;
    c->ccb_HDY = 0;
    c->ccb_VDX = vdx16;
    c->ccb_VDY = vdy16;
    c->ccb_HDDX = hddx20;
    c->ccb_HDDY = 0;
    pre1 = (uint32)c->ccb_PRE1 & ~(uint32)PRE1_TLHPCNT_MASK;
    c->ccb_PRE1 = pre1 | (((uint32)texels - PRE1_TLHPCNT_PREFETCH) & PRE1_TLHPCNT_MASK);
    c->ccb_Width = texels;
    if (ncels > 1)
        cels[ncels - 2].ccb_NextPtr = c;
}

void *
fc_list(void)
{
    if (!ncels)
        return 0;
    cels[ncels - 1].ccb_Flags |= CCB_LAST;
    cels[ncels - 1].ccb_NextPtr = 0;
    return cels;
}
