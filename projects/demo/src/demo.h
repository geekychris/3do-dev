#ifndef DEMO_H_INCLUDED
#define DEMO_H_INCLUDED

#include "celutils.h"
#include "controlpad.h"
#include "debug.h"
#include "displayutils.h"
#include "event.h"
#include "graphics.h"
#include "hardware.h"
#include "mem.h"
#include "operamath.h"
#include "stdio.h"
#include "string.h"
#include "types.h"

#define SCREEN_W 320
#define SCREEN_H 240

/* font.c */
Err   font_init(void);
void *font_glyph(char c, s32 style);

#endif
