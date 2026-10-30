/*
 * cels_3do.c - the 3DO: Terra Strike's sorted polygons filled by the cel
 * engine instead of the CPU's span filler (as sdk/amiga3d does for the
 * amiga3d games). Each polygon is cels of one texel, its pen's colour,
 * mapped onto quadrilaterals - a fan of quads (v0, vk, vk+1, vk+2), the
 * last maybe a triangle - in the painter's order. The list is drawn under
 * the frame (gfx_set_underlay); the frame's view area is the transparent
 * pen, so the HUD and the text drawn over the view stay on top.
 */
#include "graphics.h"
#include "celutils.h"
#include "cels_3do.h"

#define MAX_CELS 1600                   /* render.c's draw list holds 640 polygons */

extern unsigned short gfx_pen_rgb16(int pen);
extern void gfx_set_underlay(void *list);

static CCB cels[MAX_CELS];
static int ncels;
static uint32_t pen_word[32];           /* a 1 x 1 16-bit source per pen */
static CCB *tmpl;
int32_t ts3_ncels;

/* game pixels to the display's 16.16 (256 lines shown on 240) */
#define CX(x) ((int32_t)(x) << 16)
#define CY(y) ((int32_t)(y) * 61440)

static void quad(int col, int ax, int ay, int bx, int by, int cx, int cy, int dx, int dy)
{
    CCB *c;
    int32_t AX = CX(ax), AY = CY(ay), BX = CX(bx), BY = CY(by), CXc = CX(cx), CYc = CY(cy), DX = CX(dx), DY = CY(dy);
    if (!tmpl || ncels >= MAX_CELS) return;
    c = &cels[ncels];
    *c = *tmpl;
    c->ccb_SourcePtr = (CelData *)&pen_word[col & 31];
    c->ccb_XPos = AX;
    c->ccb_YPos = AY;
    c->ccb_HDX = (BX - AX) << 4;        /* 16.16 -> 12.20 */
    c->ccb_HDY = (BY - AY) << 4;
    c->ccb_VDX = DX - AX;
    c->ccb_VDY = DY - AY;
    c->ccb_HDDX = ((CXc - DX) - (BX - AX)) << 4;
    c->ccb_HDDY = ((CYc - DY) - (BY - AY)) << 4;
    if (ncels > 0) cels[ncels - 1].ccb_NextPtr = c;
    ncels++;
}

void ts3_begin(int bg_pen, int w, int h)
{
    int i;
    if (!tmpl) {
        tmpl = CreateCel(1, 1, 16, CREATECEL_UNCODED, pen_word);
        if (tmpl) tmpl->ccb_Flags = (tmpl->ccb_Flags | CCB_NPABS | CCB_SPABS | CCB_BGND) & ~CCB_LAST;
    }
    for (i = 0; i < 32; i++) {          /* the pens' colours, as the palette has them now */
        uint32_t c = gfx_pen_rgb16(i);
        pen_word[i] = (c << 16) | c;
    }
    ncels = 0;
    quad(bg_pen, 0, 0, w, 0, w, h, 0, h);
}

void ts3_poly(const int16_t *x, const int16_t *y, int n, int col)
{
    int i;
    for (i = 1; i + 1 < n; i += 2) {
        int j = i + 2 < n ? i + 2 : i + 1;
        quad(col, x[0], y[0], x[i], y[i], x[i + 1], y[i + 1], x[j], y[j]);
    }
}

void ts3_rect(int x0, int y0, int x1, int y1, int col)
{
    quad(col, x0, y0, x1 + 1, y0, x1 + 1, y1 + 1, x0, y1 + 1);
}

void ts3_end(void)
{
    ts3_ncels = ncels;
    if (!ncels) { gfx_set_underlay(0); return; }
    cels[ncels - 1].ccb_Flags |= CCB_LAST;
    cels[ncels - 1].ccb_NextPtr = 0;
    gfx_set_underlay(&cels[0]);
}

/* ---------- q / z by a reciprocal: ceil(2^31 / z) from a table ---------- */
#define RZ_N 8192
static uint32_t rz[RZ_N];

int32_t ts3_div(int32_t q, int32_t z)
{
    uint32_t u, inv, al, ah, bl, bh, ll, mid, lo, hi, c1;
    int neg;
    if (z <= 0) return 0;
    if (z >= RZ_N) {                    /* shift both down: z keeps 12 bits */
        while (z >= RZ_N) { z >>= 1; q = q >= 0 ? q >> 1 : -((-q) >> 1); }
    }
    if (!rz[1]) {
        int k;
        rz[1] = 0x80000000UL;
        for (k = 2; k < RZ_N; k++) rz[k] = (0x80000000UL + (uint32_t)k - 1) / (uint32_t)k;
    }
    inv = rz[z];
    neg = q < 0;
    u = neg ? (uint32_t)0 - (uint32_t)q : (uint32_t)q;
    al = u & 0xFFFF; ah = u >> 16; bl = inv & 0xFFFF; bh = inv >> 16;
    ll = al * bl;
    mid = al * bh + ah * bl;
    c1 = mid < al * bh ? 0x10000UL : 0;
    lo = ll + (mid << 16);
    hi = ah * bh + (mid >> 16) + c1 + (lo < ll ? 1 : 0);
    u = (hi << 1) | (lo >> 31);
    return neg ? -(int32_t)u : (int32_t)u;
}
