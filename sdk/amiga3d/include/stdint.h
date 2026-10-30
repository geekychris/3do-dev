/*
 * stdint.h - the C99 fixed-width types for Norcroft ARM C (C89), for the
 * amiga3d engine and its games (sdk/amiga3d). There is deliberately no
 * int64_t: the ARM60 runtime has no 64-bit helpers, so 64-bit arithmetic
 * goes through a3d_i64.h instead (and a leftover use fails to compile).
 */
#ifndef A3D_STDINT_H
#define A3D_STDINT_H

typedef signed char        int8_t;
typedef unsigned char      uint8_t;
typedef short              int16_t;
typedef unsigned short     uint16_t;
typedef long               int32_t;
typedef unsigned long      uint32_t;
typedef long               intptr_t;
typedef unsigned long      uintptr_t;

#define INT8_MIN   (-128)
#define INT8_MAX   127
#define UINT8_MAX  255
#define INT16_MIN  (-32768)
#define INT16_MAX  32767
#define UINT16_MAX 65535
#define INT32_MIN  (-2147483647L - 1)
#define INT32_MAX  2147483647L
#define UINT32_MAX 4294967295UL

#endif
