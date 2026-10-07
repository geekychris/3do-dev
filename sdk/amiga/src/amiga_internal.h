/*
 * Shared by the layer's .c files (not for games). Include the 3DO headers
 * first, then this: it removes 3DO macros that collide with Amiga names and
 * pulls in the Amiga shim declarations.
 */
#ifndef AMIGA_INTERNAL_H
#define AMIGA_INTERNAL_H

#ifdef AllocMem
#undef AllocMem
#endif
#ifdef FreeMem
#undef FreeMem
#endif
#ifdef AvailMem
#undef AvailMem
#endif

#include "amiga_types.h"
#include "amiga_compat.h"
#include "amiga3do.h"
#include "paula.h"
#include "ptplayer.h"
#include "bridge_client.h"

/* amiga_gfx.c */
extern ULONG g_amiga_frame;

/* amiga_input.c */
void amiga_input_init(void);
void amiga_input_poll(void);
void amiga_input_exit(void);

/* amiga_audio.c */
void amiga_audio_init(void);
void amiga_audio_frame(void);     /* once per displayed frame (50 Hz) */
void amiga_audio_exit(void);

/* amiga_sys.c */
void amiga_wait_vbls(int n);
extern const char *g_amiga_name;

#endif
