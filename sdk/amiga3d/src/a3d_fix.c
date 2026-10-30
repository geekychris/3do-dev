// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_fix.c - 16.16 fixed point for the games' simulations (see a3d_fix.h).
 * Square roots and lengths are 32-bit: the 68k GCC 6.5 miscompiled 64-bit
 * versions at -O2.
 */
#include "a3d.h"
#include "a3d_fix.h"

/* square roots in 32-bit arithmetic: the 68k GCC 6.5 miscompiles 64-bit
 * versions at -O2 (they came out 0). sqrt(a * 65536) = sqrt(a) * 256, with a
 * shifted up by an even amount first for the precision. */
fix fsqrt(fix a)
{
    uint32_t v = (uint32_t)a, r;
    int k = 0;
    if (a <= 0) return 0;
    while (v < (1UL << 28) && k < 15) { v <<= 2; k++; }
    r = a3_isqrt(v);
    return (fix)(k <= 8 ? r << (8 - k) : r >> (k - 8));
}

/* lengths in 32-bit arithmetic: scale down until the squares fit (the
 * 64-bit version was miscompiled by the 68k GCC 6.5 at -O2) */
fix fhypot(fix a, fix b)
{
    uint32_t x = (uint32_t)(a < 0 ? -a : a), y = (uint32_t)(b < 0 ? -b : b);
    int sh = 0;
    while ((x | y) >= 46000) { x >>= 1; y >>= 1; sh++; }
    return (fix)(a3_isqrt(x * x + y * y) << sh);
}

fix fhypot3(fix a, fix b, fix c)
{
    uint32_t x = (uint32_t)(a < 0 ? -a : a), y = (uint32_t)(b < 0 ? -b : b), z = (uint32_t)(c < 0 ? -c : c);
    int sh = 0;
    while ((x | y | z) >= 37000) { x >>= 1; y >>= 1; z >>= 1; sh++; }
    return (fix)(a3_isqrt(x * x + y * y + z * z) << sh);
}

/* radians (16.16) -> the engine's 16-bit angle */
#ifdef AMIGA3DO
int a3_angle(fix rad) { return (int)a3_mul_hi(rad, 683565276L); }
#else
int a3_angle(fix rad) { return (int)(((int64_t)rad * 683565276LL) >> 32); }
#endif
fix fsin(fix rad) { return a3_sin(a3_angle(rad)); }
fix fcos(fix rad) { return a3_cos(a3_angle(rad)); }

fix fatan2(fix y, fix x)
{
    fix ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    int t;
    if (!ax && !ay) return 0;
    if (ay <= ax) t = a3_atan_ratio(ay, ax);
    else t = 16384 - a3_atan_ratio(ax, ay);
    if (x < 0) t = 32768 - t;
    if (y < 0) t = -t;
    return FMUL(t, 411775);              /* 16-bit angle -> radians */
}

fix fexp_neg(fix x)
{
    int k;
    fix y, r;
    if (x <= 0) return FONE;
    if (x > FROMI(20)) return 0;
    k = (int)(x / 45426);                /* ln 2 */
    y = x - k * 45426;
    r = FONE - y + FMUL(y, y) / 2 - FMUL(FMUL(y, y), y) / 6;
    return r >> k;
}

fix fasin(fix x) { return fatan2(x, fsqrt(FONE - FMUL(x, x))); }
