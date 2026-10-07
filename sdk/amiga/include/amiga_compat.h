/*
 * Amiga exec / dos / intuition declarations for the 3DO compatibility layer.
 * Implemented in sdk/amiga/src/amiga_sys.c.
 */
#ifndef AMIGA_COMPAT_H
#define AMIGA_COMPAT_H

#include <stdlib.h>
#include <string.h>
#include "amiga_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- exec memory (no chip/fast distinction on the 3DO) ---- */
#define MEMF_ANY     0
#define MEMF_PUBLIC  (1L << 0)
#define MEMF_CHIP    (1L << 1)
#define MEMF_FAST    (1L << 2)
#define MEMF_CLEAR   (1L << 16)
#define MEMF_REVERSE (1L << 18)

APTR  AllocMem(ULONG size, ULONG flags);
void  FreeMem(APTR p, ULONG size);
APTR  AllocVec(ULONG size, ULONG flags);
void  FreeVec(APTR p);
void  CopyMem(const void *src, void *dst, ULONG n);
void  CopyMemQuick(const void *src, void *dst, ULONG n);
ULONG AvailMem(ULONG flags);
#define Forbid()  ((void)0)
#define Permit()  ((void)0)
#define Disable() ((void)0)
#define Enable()  ((void)0)

/* ---- AmigaDOS files ----
 * Read-only files come from the disc: "PROGDIR:x", "x" and "DH0:x" all map
 * to "$boot/<progdir>/x" (see amiga_set_progdir). Writes (high scores,
 * saves) go to the 3DO's NVRAM as "/NVRAM/<name>"; NVRAM is tiny (32 KB
 * shared by everything), so keep them small. */
typedef long BPTR;
#define MODE_OLDFILE     1005
#define MODE_NEWFILE     1006
#define MODE_READWRITE   1004
#define OFFSET_BEGINNING (-1)
#define OFFSET_CURRENT   0
#define OFFSET_END       1
#define ACCESS_READ      (-2)
#define ACCESS_WRITE     (-1)

BPTR  Open(CONST_STRPTR name, LONG mode);
LONG  Close(BPTR fh);
LONG  Read(BPTR fh, APTR buf, LONG len);
LONG  Write(BPTR fh, const void *buf, LONG len);
LONG  Seek(BPTR fh, LONG pos, LONG mode);
BPTR  Lock(CONST_STRPTR name, LONG mode);
void  UnLock(BPTR lock);
LONG  DeleteFile(CONST_STRPTR name);
LONG  IoErr(void);
void  Delay(LONG ticks);                 /* 1/50 s */
/* Whole-file convenience: returns malloc'd data (free with FreeVec) or NULL. */
APTR  amiga_load_file(CONST_STRPTR name, LONG *size_out);

/* AmigaDOS time: days, minutes, ticks (1/50 s) since the game started */
struct DateStamp { LONG ds_Days, ds_Minute, ds_Tick; };
struct DateStamp *DateStamp(struct DateStamp *ds);

/* ---- objects only referenced through pointers ---- */
struct Screen;
struct Window;
struct ScreenBuffer;
struct MsgPort;
struct Message;
struct IntuiMessage;
struct Library;
struct GfxBase;
struct IntuitionBase;
struct DosLibrary;
struct ExecBase;
struct Task;
struct Interrupt;
struct IORequest;

#define SysBase   ((struct ExecBase *)0)

#ifdef __cplusplus
}
#endif

#endif
