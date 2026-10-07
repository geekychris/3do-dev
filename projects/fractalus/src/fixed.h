// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#ifndef FRACTALUS_FIXED_H
#define FRACTALUS_FIXED_H

#include <exec/types.h>

/*
 * Fixed-point math for the flight/render pipeline. 16.16 for world
 * coords, 12-bit fractional for sin/cos so results stay well inside a
 * signed 32-bit multiply.
 *
 * Why fixed-point at all: 68020 has no FPU on the base A1200; libm
 * float ops would be soft-emulated and cost hundreds of cycles per
 * multiply. Every render-loop coord runs through these macros.
 */

/* World coord: 16.16. */
typedef LONG fx16;
#define FX16_SHIFT 16
#define FX16_ONE   (1L << FX16_SHIFT)
#define FX16(x)    ((fx16)((x) * FX16_ONE))
#define FX16_TOINT(x) ((LONG)((x) >> FX16_SHIFT))
#define FX16_MUL(a, b) (((fx16)(((SLONGBIG)(a) * (SLONGBIG)(b)) >> FX16_SHIFT)))

/* Angle: 4096 = full circle (2^12), lets us AND to wrap. */
#define ANGLE_BITS   12
#define ANGLE_FULL   (1L << ANGLE_BITS)
#define ANGLE_MASK   (ANGLE_FULL - 1)
#define ANGLE_HALF   (ANGLE_FULL >> 1)
#define ANGLE_QUART  (ANGLE_FULL >> 2)

/* sin/cos table: scaled by (1 << 12) = 4096, so mul then >>12. */
#define TRIG_SHIFT   12
#define TRIG_ONE     (1L << TRIG_SHIFT)

/* 64-bit for intermediate products in FX16_MUL. m68020 has muls.l which
 * gives a 32-bit result; for a 64-bit intermediate we lean on gcc's
 * long long. Slower than muls.l, but keeps the code portable. */
typedef long long SLONGBIG;

#endif
