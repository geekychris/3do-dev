// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * a3d_sound.c - effects and voice clips (see a3d_sound.h); the platform
 * plays them (a3_sound_play in main_68k.c / main_os4.c).
 *
 * A .voc file: "SCVO", UWORD clip count, UWORD sample rate, then for each
 * clip a ULONG offset and a ULONG length into the file, then the signed
 * 8-bit samples.
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <string.h>

#include "a3d_sound.h"

static struct { int8_t *data; int len, vol, pri; } fx[A3_MAX_SFX];

int8_t *a3_sfx_define(int id, int len, int vol, int pri)
{
    if (id < 0 || id >= A3_MAX_SFX) return 0;
    len &= ~1;
    if (fx[id].data) a3_sound_free(fx[id].data, fx[id].len);
    fx[id].data = (int8_t *)a3_sound_alloc(len);
    fx[id].len = len;
    fx[id].vol = vol;
    fx[id].pri = pri;
    return fx[id].data;
}

void a3_sfx(int id)
{
    if (id >= 0 && id < A3_MAX_SFX && fx[id].data)
        a3_sound_play(fx[id].data, fx[id].len, A3_FX_PERIOD, fx[id].vol, fx[id].pri);
}

static uint32_t rng = 4242;
static int noise8(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (int)((rng >> 16) & 255) - 128;
}

void a3_tone(int8_t *d, int n, int f0, int f1, int amp, int fade)
{
    uint32_t ph = 0;
    int i;
    if (fade < 1) fade = 1;
    for (i = 0; i < n; i++) {
        int f = f0 + ((f1 - f0) * i) / n, t, a = amp;
        ph += (uint32_t)((f * 65536L) / A3_FX_RATE);
        t = (int)((ph >> 8) & 255);
        t = t < 128 ? t * 2 - 128 : 383 - t * 2;
        if (i > n - fade) a = (amp * (n - i)) / fade;
        d[i] = (int8_t)((t * a) / 128);
    }
}

void a3_noise(int8_t *d, int n, int amp, int lp, int attack)
{
    int i, acc = 0;
    if (lp < 1) lp = 1;
    for (i = 0; i < n; i++) {
        int env = i < attack ? (amp * i) / attack : (amp * (n - i)) / (n - attack);
        acc += noise8() - acc / lp;
        d[i] = (int8_t)(((acc / lp) * env) / 128);
    }
}

/* ---------- the voice ---------- */
static uint8_t *voc;                    /* the whole file */
static long voc_size;
static int8_t *voc_play;                /* the clip playing (chip RAM on 68k) */
static long voc_play_size;
static int voc_count, voc_period;

static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

int a3_voice_load(const char *path)
{
    BPTR fh;
    uint32_t biggest = 0;
    int i;
    if (!path || !path[0] || !(fh = Open((CONST_STRPTR)path, MODE_OLDFILE))) return 0;
#ifdef __PPC__
    voc_size = (long)GetFileSize(fh);
    if (voc_size > 8) voc = (uint8_t *)AllocVecTags(voc_size, AVT_Type, MEMF_SHARED, TAG_DONE);
#else
    Seek(fh, 0, OFFSET_END);
    voc_size = Seek(fh, 0, OFFSET_BEGINNING);
    if (voc_size > 8) voc = (uint8_t *)AllocVec(voc_size, MEMF_ANY);
#endif
    if (voc && Read(fh, voc, voc_size) != voc_size) { FreeVec(voc); voc = 0; }
    Close(fh);
    if (!voc) return 0;
    if (memcmp(voc, "SCVO", 4)) { FreeVec(voc); voc = 0; return 0; }
    voc_count = (voc[4] << 8) | voc[5];
    voc_period = (int)(3546895L / ((voc[6] << 8) | voc[7]));
    for (i = 0; i < voc_count; i++) {
        uint32_t off = be32(voc + 8 + i * 8), len = be32(voc + 12 + i * 8);
        if (off + len > (uint32_t)voc_size || len > 131070) { voc_count = i; break; }
        if (len > biggest) biggest = len;
    }
    if (biggest) voc_play = (int8_t *)a3_sound_alloc((int)biggest);
    if (!voc_play) { voc_count = 0; return 0; }
    voc_play_size = (long)biggest;
    return voc_count;
}

int a3_voice_clips(void) { return voc_count; }

void a3_voice_say(int n)
{
    uint32_t off, len;
    if (!voc_play || n <= 0 || n >= voc_count) return;
    off = be32(voc + 8 + n * 8);
    len = be32(voc + 12 + n * 8) & ~1UL;
    if (len < 2) return;
    memcpy(voc_play, voc + off, len);
    a3_sound_play(voc_play, (int)len, voc_period, 64, 100);
}

void a3_sfx_cleanup(void)
{
    int k;
    for (k = 0; k < A3_MAX_SFX; k++)
        if (fx[k].data) { a3_sound_free(fx[k].data, fx[k].len); fx[k].data = 0; }
    if (voc_play) { a3_sound_free(voc_play, (int)voc_play_size); voc_play = 0; }
    if (voc) { FreeVec(voc); voc = 0; }
    voc_count = 0;
}
