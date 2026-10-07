/*
  tdo.h - small support library compiled into every project (sdk/src)

  Logging
  -------
  The 3DO kernel's kprintf() only reliably formats the first THREE varargs:
  anything the compiler passes on the stack (4th arg onwards) prints garbage.
  tdo_log() formats on the client side and is safe with any number of args.
  Output goes to the debug console, which the tdo harness captures
  (`tdo log`, the MCP `emu_log` tool, Emulator.debug_log).

  On-target tests
  ---------------
  tdo_check()/tdo_test_done() print machine-readable lines that
  `./3do test <project>` turns into a pass/fail exit code:

      TDO:PASS <name>
      TDO:FAIL <name> <detail>
      TDO:DONE pass=<n> fail=<n>
*/
#ifndef TDO_H_INCLUDED
#define TDO_H_INCLUDED

#include "types.h"

void tdo_log(const char *fmt, ...);

int  tdo_check(const char *name, int ok);
int  tdo_check_eq(const char *name, s32 got, s32 want);
void tdo_test_done(void);

#endif
