// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef VT_SCANNER_H
#define VT_SCANNER_H

#include "engine3d.h"

/*
 * Elite's iconic 3D scanner — an ellipse in the dashboard
 * projecting each object onto the horizontal plane relative to
 * the player, with a vertical stalk indicating altitude above/
 * below the ecliptic. Colour-coded: enemies red, station green,
 * neutrals yellow.
 *
 * Coordinate mapping (player at origin, facing +Z in local frame):
 *   * X of the dot = ellipse_cx + local_x * scale_x
 *   * Y of the dot = ellipse_cy + local_z * scale_z (nearer = down,
 *     farther = up, so "ahead" is at the top of the ellipse)
 *   * Vertical stalk length ∝ local_y (up positive)
 */

struct RastPort;

/* Position + size are dashboard-relative coordinates. cx/cy is
 * the ellipse centre; ew/eh are half-width and half-height. */
void vt_scanner_draw(struct RastPort *rp,
                     int cx, int cy, int ew, int eh,
                     const Camera *cam,
                     const Entity *entities, int num);

#endif
