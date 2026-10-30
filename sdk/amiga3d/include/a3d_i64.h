/*
 * a3d_i64.h - the engine's 64-bit arithmetic in 32-bit pieces, for the
 * 3DO: Norcroft ARM C has no long long that links (the ARM60 runtime has
 * no 64-bit helpers) and the ARM60 has no long multiply. Every function
 * gives exactly the result of the 64-bit C expression it replaces (the
 * games' simulations must reproduce the host's trace hashes bit for bit).
 */
#ifndef A3D_I64_H
#define A3D_I64_H

#include <stdint.h>

/* (int32_t)(((int64_t)a * b) >> 16): FMUL */
int32_t a3_mul_sh16(int32_t a, int32_t b);
/* (int32_t)(((int64_t)a * 65536) / b), truncated as C does: FDIV */
int32_t a3_div_sh16(int32_t a, int32_t b);
/* (int32_t)(((int64_t)a * b) >> 32) */
int32_t a3_mul_hi(int32_t a, int32_t b);
/* (uint32_t)(((uint64_t)a * b) >> 32) */
uint32_t a3_umul_hi(uint32_t a, uint32_t b);
/* (int32_t)(((int64_t)a * b) / c), truncated as C does */
int32_t a3_muldiv(int32_t a, int32_t b, int32_t c);
/* (int32_t)(((int64_t)a * b) >> sh), 0 <= sh < 32 */
int32_t a3_mul_shr(int32_t a, int32_t b, int sh);
/* the sign of the 64-bit a*b + c*d + e*f: 1 if >= 0, else 0 */
int a3_dot3_nonneg(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f);

/* q / z for q >= 0, z > 0 without a divide (a reciprocal table to z < 8192):
 * floor(q / z) or one more - for drawing, not for simulations */
int32_t a3_recip_div(int32_t q, int32_t z);

/* a 64-bit signed value, for code that needs to carry one */
typedef struct { uint32_t lo; int32_t hi; } a3_i64;
a3_i64 a3_i64_mul(int32_t a, int32_t b);
a3_i64 a3_i64_add(a3_i64 x, a3_i64 y);
a3_i64 a3_i64_from(int32_t a);
int    a3_i64_cmp(a3_i64 x, a3_i64 y);        /* -1, 0, 1 */
int32_t a3_i64_shr(a3_i64 x, int sh);         /* (int32_t)(x >> sh), 0 <= sh < 32 */
int32_t a3_i64_div(a3_i64 x, int32_t c);      /* (int32_t)(x / c), truncated */

#endif
