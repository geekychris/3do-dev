/* Planet Chomp sounds: synthesised at startup, played on Paula */
#ifndef SFX_H
#define SFX_H
enum { SFX_CRUMB_A, SFX_CRUMB_B, SFX_KEY, SFX_EAT, SFX_DEATH, SFX_START, SFX_CLEAR, SFX_EXTRA,
       SFX_HUM, SFX_FRIGHT, SFX_COUNT };
int  sfx_init(void);
void sfx_play(int id);
void sfx_crumb(void);
void sfx_ambience(int which);      /* 0 silent, 1 hum, 2 bubbling (spooks scared) */
#endif
