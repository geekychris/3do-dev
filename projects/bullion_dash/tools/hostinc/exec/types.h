/* host build of the rules (tools/hostcheck.c): the Amiga types */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef uint8_t UBYTE;
typedef int8_t BYTE;
typedef uint16_t UWORD;
typedef int16_t WORD;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int16_t BOOL;
typedef char *STRPTR;
typedef const char *CONST_STRPTR;
typedef void *APTR;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif
#endif
