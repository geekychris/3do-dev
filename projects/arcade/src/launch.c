/*
 * Runs a game executable from the disc and waits for it to exit.
 * Kept separate from main.c: it needs the 3DO kernel headers, which
 * clash with the Amiga compatibility headers the menu uses.
 */
#include "debug.h"
#include "filefunctions.h"
#include "item.h"
#include "kernel.h"
#include "task.h"
#include "types.h"

#include "tdo.h"

int
arcade_launch(const char *path)
{
  Item task = LoadProgram((char *)path);
  if(task < 0)
    {
      tdo_log("ARCADE: launch %s failed (%d)\n", path, (int)task);
      return (int)task;
    }
  tdo_log("ARCADE: launched %s\n", path);
  while(LookupItem(task) != NULL)
    WaitSignal(SIGF_DEADTASK);
  tdo_log("ARCADE: returned from %s\n", path);
  return 0;
}
