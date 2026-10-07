/*
 * Amiga exec/types.h equivalents for the 3DO compatibility layer.
 * Norcroft's plain char is unsigned, so BYTE is explicitly signed.
 */
#ifndef AMIGA_TYPES_H
#define AMIGA_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef long            LONG;
typedef unsigned long   ULONG;
typedef short           WORD;
typedef unsigned short  UWORD;
typedef signed char     BYTE;
typedef unsigned char   UBYTE;
typedef short           BOOL;
typedef void           *APTR;
typedef char           *STRPTR;
typedef const char     *CONST_STRPTR;
typedef unsigned char   TEXT;
typedef long            LONGBITS;
typedef unsigned short  UWORDBITS;
typedef float           FLOAT;
typedef double          DOUBLE;
typedef unsigned long   Tag;

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef VOID
#define VOID void
#endif
#ifndef REGISTER
#define REGISTER register
#endif
#ifndef STATIC
#define STATIC static
#endif

/* C99 functions the 3DO libc lacks (implemented in amiga_sys.c) */
int snprintf(char *buf, unsigned int size, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
