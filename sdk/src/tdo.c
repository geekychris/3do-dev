#include "tdo.h"

#include "debug.h"
#include "stdio.h"
#include "varargs.h"

static s32 s_pass;
static s32 s_fail;

void
tdo_log(const char *fmt_, ...)
{
  char buf[512];
  va_list args;

  va_start(args, fmt_);
  vsprintf(buf, fmt_, args);
  va_end(args);

  kprintf("%s", buf);
}

int
tdo_check(const char *name_, int ok_)
{
  if(ok_)
    {
      s_pass++;
      kprintf("TDO:PASS %s\n", name_);
    }
  else
    {
      s_fail++;
      kprintf("TDO:FAIL %s\n", name_);
    }
  return ok_;
}

int
tdo_check_eq(const char *name_, s32 got_, s32 want_)
{
  if(got_ == want_)
    {
      s_pass++;
      kprintf("TDO:PASS %s\n", name_);
      return 1;
    }
  s_fail++;
  kprintf("TDO:FAIL %s got=%d want=%d\n", name_, got_, want_);
  return 0;
}

void
tdo_test_done(void)
{
  kprintf("TDO:DONE pass=%d fail=%d\n", s_pass, s_fail);
}
