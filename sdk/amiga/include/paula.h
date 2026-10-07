/*
 * Paula (Amiga audio) emulation for the 3DO compatibility layer.
 *
 * Game code that drives the audio registers keeps working on `custom`:
 * set aud[n].ac_ptr/ac_len/ac_per/ac_vol and start channels with
 * paula_dmacon(DMAF_SETCLR | DMAF_AUD0 ...). As on the real chip, starting
 * a channel latches ac_ptr/ac_len; when the sample ends the channel reloads
 * whatever ac_ptr/ac_len hold *then* (that's how Amiga code loops samples).
 * Period and volume changes apply immediately.
 *
 * Writing custom.dmacon directly also works, but it is only noticed at the
 * next mixer pass and only the last value counts, so prefer paula_dmacon().
 *
 * The mixer (sdk/amiga/src/amiga_audio.c) mixes the 4 channels to 22 kHz
 * and streams them to the 3DO audio folio. A tick function (e.g. a music
 * player) runs at 50 Hz of *audio* time, so music keeps tempo even when a
 * game draws slower than 50 fps.
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

void  paula_dmacon(UWORD v);
/* Tick called at tick_hz (default 50) of audio time; returns 0 if audio works. */
int   paula_init(void (*tick)(void), int tick_hz);
void  paula_exit(void);
void  paula_set_tick(void (*tick)(void));          /* same, keeps the rate */
void  paula_set_tick_rate(int num, int den);       /* tick at num/den Hz */
UWORD paula_lock(void);                            /* no-op: single-threaded */
void  paula_unlock(UWORD sr);
void  paula_master_volume(int vol);                /* 0..64 */

#endif
