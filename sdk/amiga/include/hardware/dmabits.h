/* 3DO Amiga compatibility shim */
#ifndef SHIM_HARDWARE_DMABITS_H
#define SHIM_HARDWARE_DMABITS_H
#define DMAF_SETCLR  0x8000
#define DMAF_AUDIO   0x000F
#define DMAF_AUD0    0x0001
#define DMAF_AUD1    0x0002
#define DMAF_AUD2    0x0004
#define DMAF_AUD3    0x0008
#define DMAF_DISK    0x0010
#define DMAF_SPRITE  0x0020
#define DMAF_BLITTER 0x0040
#define DMAF_COPPER  0x0080
#define DMAF_RASTER  0x0100
#define DMAF_MASTER  0x0200
#define DMAF_BLITHOG 0x0400
#define DMAF_ALL     0x01FF
#endif
