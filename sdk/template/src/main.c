/*
  __NAME__ - starter 3DO program

  Double-buffered display, controller input and debug logging. Build and run:

    ./3do run __NAME__        window (keys: arrows, Z/X/C = A/B/C, Enter = P, Backspace = X)
    ./3do log __NAME__        print the debug console
    ./3do test __NAME__ --no-wait-done --expect "__NAME__: ready"
*/
#include "controlpad.h"
#include "debug.h"
#include "displayutils.h"
#include "event.h"
#include "graphics.h"
#include "stdio.h"
#include "types.h"

#include "tdo.h"

#define SCREEN_W 320
#define SCREEN_H 240

static
void
clear(ScreenContext *sc_, s32 screen_, Color color_)
{
  GrafCon gc;
  Rect r;

  r.rect_XLeft   = 0;
  r.rect_YTop    = 0;
  r.rect_XRight  = SCREEN_W;
  r.rect_YBottom = SCREEN_H;
  SetFGPen(&gc, color_);
  FillRect(sc_->sc_BitmapItems[screen_], &gc, &r);
}

static
void
text(ScreenContext *sc_, s32 screen_, s32 x_, s32 y_, const char *s_, Color color_)
{
  GrafCon gc;

  SetFGPen(&gc, color_);
  MoveTo(&gc, x_, y_);
  DrawText8(&gc, sc_->sc_BitmapItems[screen_], (const u8*)s_);
}

int
main(void)
{
  ScreenContext sc;
  Item vbl;
  s32 screen = 0;
  s32 x = 120;
  s32 y = 116;
  u32 frame = 0;
  char buf[64];

  if(OpenGraphicsFolio() < 0 || InitControlPad(1) < 0 ||
     CreateBasicDisplay(&sc, DI_TYPE_DEFAULT, 2) < 0)
    {
      kprintf("__NAME__: init failed\n");
      return 1;
    }
  vbl = GetVBLIOReq();

  tdo_log("__NAME__: ready (%dx%d)\n", sc.sc_BitmapWidth, sc.sc_BitmapHeight);

  for(;;)
    {
      u32 buttons = 0;

      DoControlPad(1, &buttons, ControlUp | ControlDown | ControlLeft | ControlRight);
      if(buttons & ControlX)
        break;
      if(buttons & ControlLeft)  x -= 2;
      if(buttons & ControlRight) x += 2;
      if(buttons & ControlUp)    y -= 2;
      if(buttons & ControlDown)  y += 2;
      if(buttons & ControlA)
        tdo_log("__NAME__: A pressed at %d,%d (frame %d)\n", x, y, frame);

      clear(&sc, screen, MakeRGB15(2, 3, 8));
      text(&sc, screen, 16, 16, "__NAME__", MakeRGB15(31, 20, 12));
      sprintf(buf, "frame %d", (int)frame);
      text(&sc, screen, 16, 28, buf, MakeRGB15(16, 16, 20));
      text(&sc, screen, x, y, "HELLO 3DO", MakeRGB15(31, 31, 31));

      DisplayScreen(sc.sc_Screens[screen], 0);
      screen = !screen;
      WaitVBL(vbl, 1);
      frame++;
    }

  kprintf("__NAME__: exit\n");
  DeleteBasicDisplay(&sc);
  KillControlPad();
  return 0;
}
