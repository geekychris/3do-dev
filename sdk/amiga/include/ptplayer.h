/*
 * ptplayer API (Frank Wille's ProTracker player) for the 3DO layer.
 * Same calls as the Amiga asm version; the `custom` arguments are ignored.
 * Plays through the Paula emulation (paula.h).
 */
#ifndef PTPLAYER_H
#define PTPLAYER_H

#include "amiga_types.h"

typedef struct {
	APTR sfx_ptr;       /* sample start */
	WORD sfx_len;       /* length in words */
	WORD sfx_per;       /* replay period */
	WORD sfx_vol;       /* volume 0..64 */
	BYTE sfx_cha;       /* channel 0..3, or -1 for auto */
	BYTE sfx_pri;       /* priority (non-zero) */
} SfxStructure;

void mt_install_cia(void *custom, void *autovec, UBYTE pal);
void mt_remove_cia(void *custom);
void mt_init(void *custom, APTR module, APTR samples, UBYTE songpos);
void mt_end(void *custom);
void mt_soundfx(void *custom, APTR sample, UWORD length, UWORD period, UWORD volume);
void mt_playfx(void *custom, SfxStructure *sfx);
void mt_stopfx(void *custom, UBYTE channel);
void mt_musicmask(void *custom, UBYTE mask);
void mt_mastervol(void *custom, UWORD vol);
void mt_music(void *custom);
int  mt_sound_ok(void);   /* 0 = sound available */

extern UBYTE mt_Enable;
extern UBYTE mt_E8Trigger;
extern UBYTE mt_MusicChannels;

#endif
