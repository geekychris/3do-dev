/*
 * 3DO control pads -> PAD_* bits. Polled once per gfx_swap().
 */
#include "controlpad.h"
#include "event.h"

#include "amiga_internal.h"

#define ALL_BUTTONS (ControlUp | ControlDown | ControlLeft | ControlRight | ControlA | \
                     ControlB | ControlC | ControlStart | ControlX | ControlLeftShift | \
                     ControlRightShift)

static ULONG s_held[2], s_prev[2];
static int   s_ready;

static ULONG
map(u32 b)
{
  ULONG r = 0;
  if(b & ControlUp)         r |= PAD_UP;
  if(b & ControlDown)       r |= PAD_DOWN;
  if(b & ControlLeft)       r |= PAD_LEFT;
  if(b & ControlRight)      r |= PAD_RIGHT;
  if(b & ControlA)          r |= PAD_A;
  if(b & ControlB)          r |= PAD_B;
  if(b & ControlC)          r |= PAD_C;
  if(b & ControlStart)      r |= PAD_P;
  if(b & ControlX)          r |= PAD_X;
  if(b & ControlLeftShift)  r |= PAD_L;
  if(b & ControlRightShift) r |= PAD_R;
  return r;
}

void
amiga_input_init(void)
{
  if(s_ready)
    return;
  InitControlPad(2);
  s_held[0] = s_held[1] = s_prev[0] = s_prev[1] = 0;
  s_ready = 1;
  amiga_input_poll();
  /* buttons already down at start don't count as presses */
  s_prev[0] = s_held[0];
  s_prev[1] = s_held[1];
}

void
amiga_input_poll(void)
{
  int i;
  if(!s_ready)
    return;
  for(i = 0; i < 2; i++)
    {
      u32 b = 0;
      s_prev[i] = s_held[i];
      if(DoControlPad(i + 1, &b, ALL_BUTTONS) >= 0)
        s_held[i] = map(b);
      else
        s_held[i] = 0;
    }
}

void
amiga_input_exit(void)
{
  if(s_ready)
    KillControlPad();
  s_ready = 0;
}

ULONG pad_held(int port)        { return (port >= 0 && port < 2) ? s_held[port] : 0; }
ULONG pad_pressed(int port)     { return (port >= 0 && port < 2) ? (s_held[port] & ~s_prev[port]) : 0; }
int   amiga_quit_requested(void) { return (pad_pressed(0) & PAD_X) != 0; }
