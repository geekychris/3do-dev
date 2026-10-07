/*
 * Paula (Amiga audio) emulation for the 3DO compatibility layer.
 *
 * Game code that writes audio registers keeps working on `custom`: set
 * aud[n].ac_ptr/ac_len/ac_per/ac_vol, then start channels with
 * paula_dmacon(DMAF_SETCLR | DMAF_AUD0 ...). Writing custom.dmacon directly
 * also works (it is applied at the next mixer tick), but only the last write
 * per tick counts, so prefer paula_dmacon(). The mixer runs in
 * sdk/amiga/src/amiga_audio.c and streams to the 3DO audio folio.
 */
#ifndef PAULA_H
#define PAULA_H

#include "amiga_types.h"

#define PAULA_CLOCK_PAL 3546895L   /* period = clock / sample rate */

struct AudChannel {
	UWORD *ac_ptr;      /* sample start (signed 8-bit data) */
	UWORD  ac_len;      /* length in words */
	UWORD  ac_per;      /* period */
	UWORD  ac_vol;      /* 0..64 */
	UWORD  ac_dat;
	UWORD  ac_pad[2];
};

struct Custom {
	UWORD dmacon;               /* write: SETCLR semantics, see above */
	UWORD dmaconr;              /* read: currently enabled channels */
	UWORD intena, intreq, adkcon;
	UWORD joy0dat, joy1dat;     /* not updated: use pad_held() */
	UWORD pad0;
	struct AudChannel aud[4];
};

extern struct Custom custom;

void paula_dmacon(UWORD v);
/* Optional tick called at 50 Hz from the mixer (e.g. a music player). */
void paula_set_tick(void (*tick)(void));
void paula_master_volume(int vol);   /* 0..64 */

#endif
