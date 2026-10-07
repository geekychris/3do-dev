/*
 * Paula + ptplayer API for the 3DO layer.
 *
 * Phase 1: the API is complete (games compile and call it normally) but
 * produces no sound yet; the 4-channel mixer streaming to the 3DO audio
 * folio replaces the bodies below without any change to game code.
 */
#include "amiga_internal.h"

struct Custom custom;

UBYTE mt_Enable;
UBYTE mt_E8Trigger;
UBYTE mt_MusicChannels;

static void (*s_tick)(void);
static int s_master = 64;

void amiga_audio_init(void) { memset(&custom, 0, sizeof(custom)); }
void amiga_audio_exit(void) { s_tick = 0; }

void
amiga_audio_frame(void)
{
  if(custom.dmacon)
    {
      paula_dmacon(custom.dmacon);
      custom.dmacon = 0;
    }
  if(s_tick)
    s_tick();
}

void
paula_dmacon(UWORD v)
{
  if(v & 0x8000)
    custom.dmaconr |= (UWORD)(v & 0x000F);
  else
    custom.dmaconr &= (UWORD)~(v & 0x000F);
}

void paula_set_tick(void (*tick)(void)) { s_tick = tick; }
void paula_master_volume(int vol)       { s_master = vol; }

void mt_install_cia(void *c, void *a, UBYTE pal) { (void)c; (void)a; (void)pal; }
void mt_remove_cia(void *c)                      { (void)c; }
void mt_init(void *c, APTR m, APTR s, UBYTE p)   { (void)c; (void)m; (void)s; (void)p; }
void mt_end(void *c)                             { (void)c; }
void mt_soundfx(void *c, APTR s, UWORD l, UWORD p, UWORD v) { (void)c; (void)s; (void)l; (void)p; (void)v; }
void mt_playfx(void *c, SfxStructure *sfx)       { (void)c; (void)sfx; }
void mt_stopfx(void *c, UBYTE ch)                { (void)c; (void)ch; }
void mt_musicmask(void *c, UBYTE m)              { (void)c; (void)m; }
void mt_mastervol(void *c, UWORD v)              { (void)c; s_master = v; }
void mt_music(void *c)                           { (void)c; }
