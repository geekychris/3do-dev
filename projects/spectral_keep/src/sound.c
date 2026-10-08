/*
 * Music and sound effects (Music.cs, Beeper.cs) on the Amiga layer's Paula,
 * i.e. the 3DO's DSP.
 *
 * The four music loops are rendered offline by tools/assets.py (the same
 * composer and synthesiser as the Unity game) and loaded from disc when a
 * track starts. A loop is up to 460 KB, more than one Paula sample can be,
 * so it plays as a chain of 128 KB blocks: a 50 Hz audio-time tick sets
 * the registers for the next block while the current one plays, and Paula
 * (or the DSP voice's linked sample) moves on to it seamlessly. Music
 * plays on channels 0 and 1 together (centred), effects on 2 and 3.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "amiga3do.h"
#include "paula.h"
#include "keep.h"

#define PERIOD     321              /* 11050 Hz */
#define RATE       11050L
#define BLOCK      131064L          /* bytes per Paula block (<= 65535 words, 8-byte multiple) */
#define MAXBLOCKS  8
#define MUSIC_VOL  36
#define SFX_VOL    56

static const char *track_file[4] = {
    "title.raw", "gatehouse.raw", "crypt.raw", "tower.raw"
};

static BYTE  *sfx_data;
static BYTE  *clip[SFX_COUNT];
static ULONG  clip_len[SFX_COUNT];
static UWORD  silence[2];
static int    fx_chan = 2;

static BYTE  *music;                /* the loaded loop */
static LONG   music_len;
static int    track = T_NONE, enabled = 1;
static int    nblocks, playing_block;
static ULONG  block_start[MAXBLOCKS], block_len[MAXBLOCKS];
static ULONG  ticks, switch_at;     /* audio ticks */
static long   duck_until;
static int    vol;

static ULONG block_ticks(int b) { return (ULONG)(block_len[b] * 50 / RATE); }

static void queue_block(int b)      /* what Paula reloads when the current block ends */
{
    custom.aud[0].ac_ptr = custom.aud[1].ac_ptr = (UWORD *)(music + block_start[b]);
    custom.aud[0].ac_len = custom.aud[1].ac_len = (UWORD)(block_len[b] / 2);
}

/* audio thread, 50 Hz */
static void music_tick(void)
{
    int target;
    ticks++;
    if (music && nblocks > 1 && ticks >= switch_at) {
        /* half way through a block: queue the one after it */
        int next = (playing_block + 1) % nblocks, after = (next + 1) % nblocks;
        queue_block(after);
        switch_at += block_ticks(next) - block_ticks(next) / 2 + block_ticks(after) / 2;
        playing_block = next;
    }
    target = !enabled || !music ? 0 : (long)ticks < duck_until ? MUSIC_VOL / 5 : MUSIC_VOL;
    if (vol < target) vol++;
    else if (vol > target) vol--;
    if (music) custom.aud[0].ac_vol = custom.aud[1].ac_vol = (UWORD)vol;
}

static void music_stop(void)
{
    UWORD k = paula_lock();
    paula_dmacon(3);
    paula_unlock(k);
    if (music) FreeVec(music);
    music = 0;
    track = T_NONE;
}

void snd_music(int t)
{
    UWORD k;
    LONG len = 0;
    int b;
    if (t == track && music) return;
    music_stop();
    if (t < 0 || t >= T_NONE) return;
    music = (BYTE *)amiga_load_file((CONST_STRPTR)track_file[t], &len);
    if (!music || len < 8) {
        keep_log("no music %s\n", track_file[t]);
        if (music) FreeVec(music);
        music = 0;
        return;
    }
    music_len = len & ~7L;
    nblocks = 0;
    for (b = 0; b < MAXBLOCKS && (ULONG)b * BLOCK < (ULONG)music_len; b++) {
        block_start[b] = b * BLOCK;
        block_len[b] = music_len - block_start[b] > BLOCK ? BLOCK : music_len - block_start[b];
        nblocks++;
    }
    track = t;
    k = paula_lock();
    vol = 0;
    custom.aud[0].ac_per = custom.aud[1].ac_per = PERIOD;
    custom.aud[0].ac_vol = custom.aud[1].ac_vol = 0;
    queue_block(0);
    paula_dmacon(0x8000 | 3);       /* both latch block 0 */
    queue_block(nblocks > 1 ? 1 : 0);
    playing_block = 0;
    switch_at = ticks + block_ticks(0) + (nblocks > 1 ? block_ticks(1) / 2 : 0);
    paula_unlock(k);
}

void snd_music_enable(int on) { enabled = on; }
void snd_duck(long t) { duck_until = (long)ticks + t; }

int snd_init(void)
{
    LONG len = 0;
    ULONG off;
    int i, n;
    paula_set_tick(music_tick);
    sfx_data = (BYTE *)amiga_load_file((CONST_STRPTR)"sfx.raw", &len);
    if (!sfx_data || len < 4) return 0;
    n = (int)((ULONG *)sfx_data)[0];
    if (n > SFX_COUNT) n = SFX_COUNT;
    off = 4 + 4 * ((ULONG *)sfx_data)[0];
    for (i = 0; i < n; i++) {
        clip_len[i] = ((ULONG *)sfx_data)[1 + i];
        clip[i] = sfx_data + off;
        off += clip_len[i];
    }
    return 1;
}

void snd_play(int id)
{
    UWORD k;
    int ch = fx_chan;
    if (id < 0 || id >= SFX_COUNT || !clip[id]) return;
    fx_chan = fx_chan == 2 ? 3 : 2;
    k = paula_lock();
    paula_dmacon((UWORD)(1 << ch));
    custom.aud[ch].ac_ptr = (UWORD *)clip[id];
    custom.aud[ch].ac_len = (UWORD)(clip_len[id] / 2);
    custom.aud[ch].ac_per = PERIOD;
    custom.aud[ch].ac_vol = SFX_VOL;
    paula_dmacon((UWORD)(0x8000 | (1 << ch)));
    custom.aud[ch].ac_ptr = silence;    /* after the clip, Paula latches silence */
    custom.aud[ch].ac_len = 1;
    paula_unlock(k);
}
