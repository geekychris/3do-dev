// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "scanner.h"

#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <proto/graphics.h>

/* Palette pens set up in main.c's install_palette. */
#define PEN_SCANNER_EDGE   121   /* dim green */
#define PEN_SCANNER_HORIZ  120   /* bright green */
#define PEN_ENEMY          124   /* warning red */
#define PEN_NEUTRAL        4     /* whiteish */
#define PEN_STATION        72    /* station shade base -> greenish */

/* Draw an axis-aligned ellipse outline. Half-widths cx±ew,
 * cy±eh. Two arcs (top + bottom) rasterised the naive integer way
 * — the ellipse is small so cost is trivial. */
static void ellipse(struct RastPort *rp, int cx, int cy, int ew, int eh)
{
    int px = -ew, py = 0;
    int i;
    /* Sweep x from -ew..+ew, compute y = eh * sqrt(1 - (x/ew)^2). */
    for (i = -ew; i <= ew; i++) {
        LONG num = (LONG)i * i * eh * eh;
        LONG den = (LONG)ew * ew;
        LONG y2  = (LONG)eh * eh - num / den;
        LONG y = 0;
        if (y2 > 0) {
            LONG r = 0, bit = 1L << 30, v = y2;
            while (bit > v) bit >>= 2;
            while (bit) {
                if (v >= r + bit) { v -= r + bit; r = (r >> 1) + bit; }
                else r >>= 1;
                bit >>= 2;
            }
            y = r;
        }
        WritePixel(rp, (WORD)(cx + i), (WORD)(cy + y));
        WritePixel(rp, (WORD)(cx + i), (WORD)(cy - y));
        (void)px; (void)py;
    }
}

/* Rotate a world offset into the player's local frame (undo yaw
 * and pitch). Roll is ignored — the scanner shows world axes
 * relative to the ship's heading, not its banking. */
static void world_to_local(const Camera *cam, LONG dx, LONG dy, LONG dz,
                           LONG *lx, LONG *ly, LONG *lz)
{
    LONG sp = e3d_sin(-cam->pitch), cp = e3d_cos(-cam->pitch);
    LONG sy = e3d_sin(-cam->yaw),   cy = e3d_cos(-cam->yaw);
    /* Undo yaw first (around Y), then pitch (around X). */
    LONG tx = ( cy * dx + sy * dz) >> FP;
    LONG ty = dy;
    LONG tz = (-sy * dx + cy * dz) >> FP;
    *lx = tx;
    *ly = ( cp * ty - sp * tz) >> FP;
    *lz = ( sp * ty + cp * tz) >> FP;
}

void vt_scanner_draw(struct RastPort *rp,
                     int cx, int cy, int ew, int eh,
                     const Camera *cam,
                     const Entity *entities, int num)
{
    const LONG MAX_RANGE = 12000;    /* 3DO port (C89): moved to the top of the block */
    int i;

    /* Ellipse outline. */
    SetAPen(rp, PEN_SCANNER_EDGE);
    ellipse(rp, cx, cy, ew, eh);

    /* Horizontal cross-hair through centre — the ecliptic. */
    SetAPen(rp, PEN_SCANNER_HORIZ);
    Move(rp, cx - ew, cy); Draw(rp, cx + ew, cy);
    Move(rp, cx, cy - eh); Draw(rp, cx, cy + eh);

    /* Range: anything within 12000 world units maps to the
     * ellipse edge. Beyond that it clips. */

    for (i = 0; i < num; i++) {
        const Entity *e = &entities[i];
        LONG dx;
        LONG dy;
        LONG dz;
        LONG lx, ly, lz;
        int sx;
        int sy;
        LONG relx, rely;
        LONG rr;
        LONG lim;
        int stalk;
        UBYTE pen;
        if (!e->active) continue;
        dx = e->x - cam->x;
        dy = e->y - cam->y;
        dz = e->z - cam->z;
        world_to_local(cam, dx, dy, dz, &lx, &ly, &lz);

        /* Map local X (side) + local Z (forward) to ellipse.
         * Forward (+lz) puts the dot toward the TOP of the
         * ellipse (screen y-). */
        sx = cx + (int)((lx * ew) / MAX_RANGE);
        sy = cy - (int)((lz * eh) / MAX_RANGE);
        /* Clip to ellipse. */
        relx = sx - cx; rely = sy - cy;
        rr = (relx * relx * eh * eh + rely * rely * ew * ew);
        lim = (LONG)ew * ew * eh * eh;
        if (rr > lim) continue;

        /* Vertical altitude stalk — length ∝ local Y, clamped. */
        stalk = (int)((ly * eh) / MAX_RANGE);
        if (stalk >  eh - 1) stalk =  eh - 1;
        if (stalk < -eh + 1) stalk = -eh + 1;

        pen = PEN_NEUTRAL;
        if (e->team == 1) pen = PEN_ENEMY;
        else if (e->team == 2) pen = PEN_STATION;

        SetAPen(rp, pen);
        /* Stalk (up = +Y = negative screen delta). */
        if (stalk != 0) {
            Move(rp, sx, sy);
            Draw(rp, sx, sy - stalk);
        }
        /* Dot at the stalk's tip so the whole thing reads as a
         * "pole with a ball on top". */
        WritePixel(rp, (WORD)sx, (WORD)(sy - stalk));
        WritePixel(rp, (WORD)(sx - 1), (WORD)(sy - stalk));
        WritePixel(rp, (WORD)(sx + 1), (WORD)(sy - stalk));
    }
}
