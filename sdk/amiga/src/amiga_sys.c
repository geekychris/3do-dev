/*
 * exec memory, AmigaDOS files, timing, logging and the debug-bridge API
 * on the 3DO.
 */
#include "debug.h"
#include "graphics.h"
#include "stdarg.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "amiga_internal.h"
#include "tdo.h"

const char *g_amiga_name;
static char s_progdir[64] = "";
static LONG s_ioerr;

/* ------------------------------------------------------------------ memory */

/* Allocations carry a small header so FreeVec can work without a size. */
APTR
AllocMem(ULONG size, ULONG flags)
{
  void *p = (flags & MEMF_CLEAR) ? calloc(1, size ? size : 1) : malloc(size ? size : 1);
  if(!p)
    amiga_log("AllocMem(%d) failed\n", (int)size);
  return p;
}

void  FreeMem(APTR p, ULONG size)            { (void)size; if(p) free(p); }
APTR  AllocVec(ULONG size, ULONG flags)      { return AllocMem(size, flags); }
void  FreeVec(APTR p)                        { if(p) free(p); }
void  CopyMem(const void *s, void *d, ULONG n)      { memmove(d, s, n); }
void  CopyMemQuick(const void *s, void *d, ULONG n) { memmove(d, s, n); }
ULONG AvailMem(ULONG flags)                  { (void)flags; return 1024L * 1024L; }

/* ------------------------------------------------------------------ text */

int
vsnprintf(char *buf, unsigned int size, const char *fmt, va_list ap)
{
  char tmp[1024];
  int n = vsprintf(tmp, fmt, ap);
  if(size)
    {
      unsigned int k = ((unsigned int)n < size) ? (unsigned int)n : size - 1;
      memcpy(buf, tmp, k);
      buf[k] = 0;
    }
  return n;
}

int
snprintf(char *buf, unsigned int size, const char *fmt, ...)
{
  int n;
  va_list ap;
  va_start(ap, fmt);
  n = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return n;
}

static void
vlog(const char *prefix, const char *fmt, va_list ap)
{
  char buf[512];
  int n;
  vsprintf(buf, fmt, ap);
  n = (int)strlen(buf);
  if(prefix)
    {
      if(n && buf[n - 1] == '\n')
        tdo_log("%s%s", prefix, buf);
      else
        tdo_log("%s%s\n", prefix, buf);
    }
  else
    tdo_log("%s", buf);
}

void
amiga_log(const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vlog(0, fmt, ap);
  va_end(ap);
}

/* ------------------------------------------------------------------ bridge */

static char s_prefix[40] = "GAME: ";

int
ab_init(const char *name)
{
  int i;
  g_amiga_name = name;
  /* "ROCK BLASTER" -> "ROCK BLASTER: " */
  for(i = 0; name && name[i] && i < 30; i++)
    s_prefix[i] = name[i];
  s_prefix[i++] = ':';
  s_prefix[i++] = ' ';
  s_prefix[i] = 0;
  return 0;
}

void ab_cleanup(void) { }
void ab_poll(void) { }
struct DateStamp *
DateStamp(struct DateStamp *ds)
{
  /* from the display's field counter (60 Hz) as 50 Hz ticks */
  u32 fields = 0;
  ULONG ticks;
  QueryGraphics(QUERYGRAF_TAG_FIELDCOUNT, &fields);
  ticks = (ULONG)fields / 6 * 5 + ((ULONG)fields % 6) * 5 / 6;
  ds->ds_Days = (LONG)(ticks / (50UL * 60 * 60 * 24));
  ticks %= 50UL * 60 * 60 * 24;
  ds->ds_Minute = (LONG)(ticks / (50UL * 60));
  ds->ds_Tick = (LONG)(ticks % (50UL * 60));
  return ds;
}

void ab_heartbeat(void) { }
void ab_push_var(const char *n) { (void)n; }
int  ab_is_connected(void) { return 1; }
void ab_register_var(const char *n, int t, void *p) { (void)n; (void)t; (void)p; }
void ab_register_hook(const char *n, const char *d, int (*f)()) { (void)n; (void)d; (void)f; }
void ab_register_memregion(const char *n, void *p, ULONG s) { (void)n; (void)p; (void)s; }

void ab_log(const char *level, const char *fmt, ...)
{
  va_list ap;
  (void)level;
  va_start(ap, fmt);
  vlog(s_prefix, fmt, ap);
  va_end(ap);
}

#define AB_FN(NAME) void NAME(const char *fmt, ...) { va_list ap; va_start(ap, fmt); vlog(s_prefix, fmt, ap); va_end(ap); }
AB_FN(AB_I)
AB_FN(AB_W)
AB_FN(AB_E)
AB_FN(AB_D)

/* ------------------------------------------------------------------ files */

#define MAX_FILES 8
static FILE *s_files[MAX_FILES];

void
amiga_set_progdir(const char *dir)
{
  strncpy(s_progdir, dir ? dir : "", sizeof(s_progdir) - 1);
  s_progdir[sizeof(s_progdir) - 1] = 0;
}

/* AmigaDOS name -> 3DO path. Reads: "$boot/<progdir>/<file>", writes: "/NVRAM/<file>". */
static void
map_path(CONST_STRPTR name, int writing, char *out, int outlen)
{
  const char *base = name, *colon = strchr(name, ':'), *slash;
  if(colon)
    base = colon + 1;                     /* drop PROGDIR:, DH0:, S: ... */
  if(writing)
    {
      slash = strrchr(base, '/');
      if(slash)
        base = slash + 1;                 /* NVRAM is flat */
      sprintf(out, "/NVRAM/%.31s", base);
    }
  else if(base[0] == '$' || base[0] == '/')
    sprintf(out, "%.*s", outlen - 1, base);
  else if(s_progdir[0])
    sprintf(out, "$boot/%s/%.*s", s_progdir, outlen - 80, base);
  else
    sprintf(out, "$boot/%.*s", outlen - 10, base);
}

BPTR
Open(CONST_STRPTR name, LONG mode)
{
  char path[160];
  int i, writing = (mode == MODE_NEWFILE);
  FILE *f = 0;

  for(i = 0; i < MAX_FILES && s_files[i]; i++)
    ;
  if(i == MAX_FILES)
    {
      s_ioerr = 84;   /* ERROR_NO_FREE_STORE */
      return 0;
    }
  if(mode == MODE_READWRITE)
    {
      map_path(name, 1, path, sizeof(path));
      f = fopen(path, "r+");
      if(!f)
        f = fopen(path, "w");
    }
  else
    {
      map_path(name, writing, path, sizeof(path));
      f = fopen(path, writing ? "w" : "r");
      if(!f && !writing)
        {
          /* saved data (high scores) lives in NVRAM */
          map_path(name, 1, path, sizeof(path));
          f = fopen(path, "r");
        }
    }
  if(!f)
    {
      s_ioerr = 205;  /* ERROR_OBJECT_NOT_FOUND */
      return 0;
    }
  s_files[i] = f;
  return (BPTR)(i + 1);
}

static FILE *
fh(BPTR h)
{
  return (h >= 1 && h <= MAX_FILES) ? s_files[h - 1] : 0;
}

LONG
Close(BPTR h)
{
  FILE *f = fh(h);
  if(!f)
    return 0;
  fclose(f);
  s_files[h - 1] = 0;
  return 1;
}

LONG Read(BPTR h, APTR buf, LONG len)        { FILE *f = fh(h); return f ? fread(buf, 1, len, f) : -1; }
LONG Write(BPTR h, const void *buf, LONG len) { FILE *f = fh(h); return f ? fwrite(buf, 1, len, f) : -1; }

LONG
Seek(BPTR h, LONG pos, LONG mode)
{
  FILE *f = fh(h);
  LONG old;
  if(!f)
    return -1;
  old = ftell(f);
  fseek(f, pos, mode == OFFSET_BEGINNING ? 0 : mode == OFFSET_END ? 2 : 1   /* SEEK_SET/END/CUR */);
  return old;
}

BPTR
Lock(CONST_STRPTR name, LONG mode)
{
  BPTR h = Open(name, MODE_OLDFILE);
  (void)mode;
  return h;
}

void UnLock(BPTR lock)               { Close(lock); }
LONG DeleteFile(CONST_STRPTR name)   { (void)name; return 0; }
LONG IoErr(void)                     { return s_ioerr; }

APTR
amiga_load_file(CONST_STRPTR name, LONG *size_out)
{
  BPTR h = Open(name, MODE_OLDFILE);
  LONG size, got;
  UBYTE *buf;
  if(!h)
    return 0;
  Seek(h, 0, OFFSET_END);
  size = Seek(h, 0, OFFSET_BEGINNING);
  if(size <= 0)
    {
      Close(h);
      return 0;
    }
  buf = (UBYTE *)AllocVec(size, MEMF_ANY);
  got = buf ? Read(h, buf, size) : 0;
  Close(h);
  if(!buf || got != size)
    {
      FreeVec(buf);
      return 0;
    }
  if(size_out)
    *size_out = size;
  return buf;
}

/* ------------------------------------------------------------------ time */

void
Delay(LONG ticks)
{
  /* AmigaDOS ticks are 1/50 s; VBLs are 1/60 s */
  amiga_wait_vbls((int)((ticks * 6 + 4) / 5));
}
