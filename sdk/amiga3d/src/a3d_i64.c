/*
 * a3d_i64.c - 64-bit arithmetic in 32-bit pieces (see a3d_i64.h).
 */
#include "a3d_i64.h"

/* the unsigned 64-bit product of a and b */
static void umul64(uint32_t a, uint32_t b, uint32_t *hi, uint32_t *lo)
{
    uint32_t al = a & 0xFFFF, ah = a >> 16, bl = b & 0xFFFF, bh = b >> 16;
    uint32_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint32_t mid = lh + hl, l;
    uint32_t c1 = mid < lh ? 0x10000UL : 0;
    l = ll + (mid << 16);
    *hi = hh + (mid >> 16) + c1 + (l < ll ? 1 : 0);
    *lo = l;
}

uint32_t a3_umul_hi(uint32_t a, uint32_t b)
{
    uint32_t hi, lo;
    umul64(a, b, &hi, &lo);
    return hi;
}

/* the signed 64-bit product: the unsigned one, with the high word
 * corrected for negative factors */
a3_i64 a3_i64_mul(int32_t a, int32_t b)
{
    a3_i64 r;
    uint32_t hi, lo;
    if (a >= -32768 && a <= 32767 && b >= -32768 && b <= 32767) {
        r.lo = (uint32_t)(a * b);
        r.hi = (int32_t)(a * b) < 0 ? -1 : 0;
        return r;
    }
    umul64((uint32_t)a, (uint32_t)b, &hi, &lo);
    if (a < 0) hi -= (uint32_t)b;
    if (b < 0) hi -= (uint32_t)a;
    r.lo = lo;
    r.hi = (int32_t)hi;
    return r;
}

a3_i64 a3_i64_add(a3_i64 x, a3_i64 y)
{
    a3_i64 r;
    r.lo = x.lo + y.lo;
    r.hi = (int32_t)((uint32_t)x.hi + (uint32_t)y.hi + (r.lo < x.lo ? 1 : 0));
    return r;
}

a3_i64 a3_i64_from(int32_t a)
{
    a3_i64 r;
    r.lo = (uint32_t)a;
    r.hi = a < 0 ? -1 : 0;
    return r;
}

int a3_i64_cmp(a3_i64 x, a3_i64 y)
{
    if (x.hi != y.hi) return x.hi < y.hi ? -1 : 1;
    if (x.lo != y.lo) return x.lo < y.lo ? -1 : 1;
    return 0;
}

int32_t a3_i64_shr(a3_i64 x, int sh)
{
    if (sh == 0) return (int32_t)x.lo;
    return (int32_t)(((uint32_t)x.hi << (32 - sh)) | (x.lo >> sh));
}

int32_t a3_mul_shr(int32_t a, int32_t b, int sh)
{
    return a3_i64_shr(a3_i64_mul(a, b), sh);
}

/* (a * b) >> 16 = a * bh + ah * bl + ((al * bl) >> 16), with a = ah:al and
 * b = bh:bl (ah, bh signed, al, bl unsigned 16-bit): the multiples of 2^16
 * shift out exactly, and only the low 32 bits are wanted */
int32_t a3_mul_sh16(int32_t a, int32_t b)
{
    uint32_t al, bl;
    if (a >= -32768 && a <= 32767 && b >= -32768 && b <= 32767) return (a * b) >> 16;
    al = (uint32_t)a & 0xFFFF;
    bl = (uint32_t)b & 0xFFFF;
    return (int32_t)((uint32_t)a * (uint32_t)(b >> 16) + (uint32_t)(a >> 16) * bl + ((al * bl) >> 16));
}

int32_t a3_mul_hi(int32_t a, int32_t b)
{
    return a3_i64_mul(a, b).hi;
}

/* the low 32 bits of the unsigned 64-bit hi:lo divided by d (d > 0) */
static uint32_t udiv64_lo(uint32_t hi, uint32_t lo, uint32_t d)
{
    uint32_t q, r;
    int k;
    if (hi == 0) return lo / d;
    r = hi % d;                         /* the quotient's high word is lost in the cast */
    q = 0;
    for (k = 31; k >= 0; k--) {
        uint32_t carry = r >> 31;
        r = (r << 1) | ((lo >> k) & 1);
        q <<= 1;
        if (carry || r >= d) { r -= d; q |= 1; }
    }
    return q;
}

/* the signed 64-bit x divided by c, truncated toward zero, low 32 bits */
int32_t a3_i64_div(a3_i64 x, int32_t c)
{
    int neg = 0;
    uint32_t hi = (uint32_t)x.hi, lo = x.lo, d, q;
    if (c == 0) return 0;
    if (x.hi < 0) {                     /* negate the 64-bit value */
        neg = 1;
        lo = ~lo + 1;
        hi = ~hi + (lo == 0 ? 1 : 0);
    }
    if (c < 0) { neg = !neg; d = (uint32_t)0 - (uint32_t)c; } else d = (uint32_t)c;
    q = udiv64_lo(hi, lo, d);
    return neg ? -(int32_t)q : (int32_t)q;
}

int32_t a3_muldiv(int32_t a, int32_t b, int32_t c)
{
    if (a >= -32768 && a <= 32767 && b >= -32768 && b <= 32767 && c != 0)
        return (a * b) / c;             /* the product fits in 32 bits */
    return a3_i64_div(a3_i64_mul(a, b), c);
}

int32_t a3_div_sh16(int32_t a, int32_t b)
{
    uint32_t na, nb, q1, r, q0;
    int neg, k;
    if (b == 0) return 0;
    neg = (a < 0) != (b < 0);
    na = a < 0 ? (uint32_t)0 - (uint32_t)a : (uint32_t)a;
    nb = b < 0 ? (uint32_t)0 - (uint32_t)b : (uint32_t)b;
    if (na < 0x8000UL) {                /* (a << 16) fits in 31 bits */
        q1 = (na << 16) / nb;
        return neg ? -(int32_t)q1 : (int32_t)q1;
    }
    /* (na * 65536) / nb = (na / nb) * 65536 + ((na % nb) * 65536) / nb */
    q1 = na / nb;
    r = na % nb;
    if (nb <= 0x10000UL) q0 = (r << 16) / nb;
    else {
        q0 = 0;
        for (k = 0; k < 16; k++) {
            uint32_t carry = r >> 31;
            r <<= 1;
            q0 <<= 1;
            if (carry || r >= nb) { r -= nb; q0 |= 1; }
        }
    }
    q1 = (q1 << 16) + q0;
    return neg ? -(int32_t)q1 : (int32_t)q1;
}

int a3_dot3_nonneg(int32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f)
{
    a3_i64 s;
    uint32_t m = (uint32_t)((a < 0 ? -a : a) | (c < 0 ? -c : c) | (e < 0 ? -e : e));
    uint32_t n = (uint32_t)((b < 0 ? -b : b) | (d < 0 ? -d : d) | (f < 0 ? -f : f));
    if (m < 0x8000UL && n < 0x4000UL)   /* each product < 2^29: the sum fits */
        return a * b + c * d + e * f >= 0;
    s = a3_i64_add(a3_i64_add(a3_i64_mul(a, b), a3_i64_mul(c, d)), a3_i64_mul(e, f));
    return s.hi >= 0;
}
