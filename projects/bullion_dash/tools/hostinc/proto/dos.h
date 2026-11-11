#ifndef PROTO_DOS_H
#define PROTO_DOS_H
#include <dos/dos.h>
static inline BPTR Open(CONST_STRPTR n, long m) { (void)n; (void)m; return 0; }
static inline long Close(BPTR f) { (void)f; return 0; }
static inline long Read(BPTR f, void *b, long n) { (void)f; (void)b; (void)n; return 0; }
static inline long Write(BPTR f, const void *b, long n) { (void)f; (void)b; (void)n; return 0; }
#endif
