// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_fix.h - 16.16 fixed point for the games' simulations: a 68020 has no
 * FPU. FMUL and FDIV use 64-bit intermediates (built with -m68020-60, so
 * no instruction the 68060 lacks); square roots and lengths stay in 32 bits.
 */
#ifndef A3D_FIX_H
#define A3D_FIX_H

#include <stdint.h>

typedef int32_t fix;
#define FONE        65536
#define FX(x)       ((fix)((x) * 65536.0))
#ifdef AMIGA3DO
/* the 3DO: no 64-bit arithmetic, the same results in 32-bit pieces */
#include "a3d_i64.h"
#define FMUL(a, b)  a3_mul_sh16((int32_t)(a), (int32_t)(b))
#define FDIV(a, b)  a3_div_sh16((int32_t)(a), (int32_t)(b))
#else
#define FMUL(a, b)  ((fix)(((int64_t)(a) * (b)) >> 16))
#define FDIV(a, b)  ((fix)((((int64_t)(a)) * 65536) / (b)))
#endif
#define FINT(a)     ((a) >> 16)
#define FROMI(i)    ((fix)(i) * 65536)

fix fsqrt(fix a);
fix fhypot(fix a, fix b);
fix fhypot3(fix a, fix b, fix c);
fix fsin(fix rad);
fix fcos(fix rad);
fix fatan2(fix y, fix x);               /* radians, -pi..pi, as Math.atan2 */
fix fasin(fix x);
fix fexp_neg(fix x);                    /* e^-x for x >= 0 */
int a3_angle(fix rad);                  /* radians -> the engine's 16-bit angle */

#endif
