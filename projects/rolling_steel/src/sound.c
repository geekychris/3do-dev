/*
 * Music and sounds (Music.cs, Sfx.cs) on the Amiga layer's Paula - the
 * 3DO's DSP. The themes and effects are rendered offline by tools/sounds.py.
 *
 *   channels 0 + 1  the music, streamed in 128 KB blocks (a theme is up to
 *                   210 KB, more than one Paula sample), centred
 *   channel 2       the rolling rumble: a loop whose pitch and volume follow
 *                   the marble's speed (GameDirector.UpdateRollAudio); a
 *                   second effect borrows it for a moment
 *   channel 3       effects
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdio.h>
#include "amiga3do.h"
#include "paula.h"
#include "rs.h"

#define PERIOD     321              /* 11050 Hz */
#define RATE       11050L
#define BLOCK      131064L
#define MAXBLOCKS  4
#define MUSIC_VOL  30
#define SFX_VOL    58
#define ROLL_VOL   28
#define NCLIPS     (SFX_COUNT + 1)  /* + the roll loop */

static BYTE  *sfx_data, *clip[NCLIPS];
static ULONG  clip_len[NCLIPS];
static UWORD  silence[2];

static BYTE  *music;
static LONG   music_len;
static int    theme = -1, enabled = 1;
static int    nblocks, playing_block;
static ULONG  block_start[MAXBLOCKS], block_len[MAXBLOCKS];
static ULONG  ticks, switch_at;
static int    vol;
static ULONG  fx_busy_until[4];     /* audio ticks */
static int    roll_on;
static fix    roll_target;

static ULONG block_ticks(int b) { return (ULONG)(block_len[b] * 50 / RATE); }

static void queue_block(int b)
{
    custom.aud[0].ac_ptr = custom.aud[1].ac_ptr = (UWORD *)(music + block_start[b]);
    custom.aud[0].ac_len = custom.aud[1].ac_len = (UWORD)(block_len[b] / 2);
}

/* audio thread, 50 Hz */
static void tick(void)
{
    int target;
    ticks++;
    if (music && nblocks > 1 && ticks >= switch_at) {
        int next = (playing_block + 1) % nblocks, after = (next + 1) % nblocks;
        queue_block(after);
        switch_at += block_ticks(next) - block_ticks(next) / 2 + block_ticks(after) / 2;
        playing_block = next;
    }
    target = enabled && music ? MUSIC_VOL : 0;
    if (vol < target) vol++;
    else if (vol > target) vol--;
    if (music) custom.aud[0].ac_vol = custom.aud[1].ac_vol = (UWORD)vol;
    /* the rumble eases toward its target, like the Unity Lerp */
    if (roll_on && ticks >= fx_busy_until[2]) {
        int v = (int)((roll_target * ROLL_VOL) >> 12);
        int cur = custom.aud[2].ac_vol;
        cur += (v - cur) / 4 + (v > cur ? 1 : v < cur ? -1 : 0);
        custom.aud[2].ac_vol = (UWORD)(cur < 0 ? 0 : cur);
        /* pitch 0.6 .. 1.5 with speed */
        custom.aud[2].ac_per = (UWORD)((PERIOD * 4096L) / (2458 + ((roll_target * 3686) >> 12)));
    }
}

static void music_stop(void)
{
    UWORD k = paula_lock();
    paula_dmacon(3);
    paula_unlock(k);
    if (music) FreeVec(music);
    music = 0;
    theme = -1;
}

void snd_music(int t)
{
    char name[16];
    LONG len = 0;
    UWORD k;
    int b;
    if (t == theme && music) return;
    music_stop();
    if (t < 0) return;
    sprintf(name, "theme%d.raw", t);
    music = (BYTE *)amiga_load_file((CONST_STRPTR)name, &len);
    if (!music || len < 8) {
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
    theme = t;
    k = paula_lock();
    vol = 0;
    custom.aud[0].ac_per = custom.aud[1].ac_per = PERIOD;
    custom.aud[0].ac_vol = custom.aud[1].ac_vol = 0;
    queue_block(0);
    paula_dmacon(0x8000 | 3);
    queue_block(nblocks > 1 ? 1 : 0);
    playing_block = 0;
    switch_at = ticks + block_ticks(0) + (nblocks > 1 ? block_ticks(1) / 2 : 0);
    paula_unlock(k);
}

void snd_music_enable(int on) { enabled = on; }

int snd_init(void)
{
    LONG len = 0;
    ULONG off;
    int i, n;
    paula_set_tick(tick);
    sfx_data = (BYTE *)amiga_load_file((CONST_STRPTR)"sfx.raw", &len);
    if (!sfx_data || len < 4) return 0;
    n = (int)((ULONG *)sfx_data)[0];
    if (n > NCLIPS) n = NCLIPS;
    off = 4 + 4 * ((ULONG *)sfx_data)[0];
    for (i = 0; i < n; i++) {
        clip_len[i] = ((ULONG *)sfx_data)[1 + i];
        clip[i] = sfx_data + off;
        off += clip_len[i];
    }
    return 1;
}

static void start(int ch, int id, int volume, int loop)
{
    UWORD k = paula_lock();
    paula_dmacon((UWORD)(1 << ch));
    custom.aud[ch].ac_ptr = (UWORD *)clip[id];
    custom.aud[ch].ac_len = (UWORD)(clip_len[id] / 2);
    custom.aud[ch].ac_per = PERIOD;
    custom.aud[ch].ac_vol = (UWORD)volume;
    paula_dmacon((UWORD)(0x8000 | (1 << ch)));
    if (!loop) {
        custom.aud[ch].ac_ptr = silence;
        custom.aud[ch].ac_len = 1;
    }
    paula_unlock(k);
}

void snd_play(int id)
{
    int ch = 3;
    if (id < 0 || id >= SFX_COUNT || !clip[id]) return;
    /* a second effect on top of a playing one borrows the rumble's channel */
    if (ticks < fx_busy_until[3]) {
        ch = 2;
        roll_on = 0;
    }
    fx_busy_until[ch] = ticks + clip_len[id] * 50 / RATE + 1;
    start(ch, id, SFX_VOL, 0);
}

void snd_roll(fix speed01)
{
    roll_target = speed01;
    if (!clip[SFX_COUNT] || ticks < fx_busy_until[2]) return;
    if (!roll_on) {
        start(2, SFX_COUNT, 0, 1);
        roll_on = 1;
    }
}
