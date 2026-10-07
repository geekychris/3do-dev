/* 3DO Amiga compatibility shim */
#ifndef SHIM_HARDWARE_CIA_H
#define SHIM_HARDWARE_CIA_H
#include "amiga_types.h"
/* Reading CIA registers for input must be replaced with pad_held()/pad_pressed(). */
#define CIAF_GAMEPORT0 (1<<6)
#define CIAF_GAMEPORT1 (1<<7)
#define CIAB_GAMEPORT0 6
#define CIAB_GAMEPORT1 7
#endif
