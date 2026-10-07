// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "sfx.h"
#include "modplay.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

/*
 * SFX system, now routed through modplay's Paula-direct SFX slot
 * on channel 3. Music plays on channels 0-2, SFX on channel 3 —
 * modplay silences the music briefly on channel 3 while the SFX
 * plays, then resumes.
 *
 * We generate 4 sample waveforms in chip RAM once at init and
 * hand pointers to modplay_sfx() on demand. If two SFX fire the
 * same tick, the latter wins (no queueing).
 */

static void gen_laser(BYTE *buf, int n)
{
    LONG phase = 0;
    for (int i = 0; i < n; i++) {
        LONG inc = 24 - (i * 18 / n);
        LONG amp = 100 - (i * 100 / n);
        phase += inc;
        LONG saw = ((phase & 0x7F) - 64);
        buf[i] = (BYTE)((saw * amp) / 100);
    }
}
static void gen_explosion(BYTE *buf, int n)
{
    ULONG seed = 0xABCDEF12UL;
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245UL + 12345UL;
        LONG amp = 127 - (i * 127 / n);
        BYTE noise = (BYTE)(seed >> 24);
        buf[i] = (BYTE)((noise * amp) / 127);
    }
}
static void gen_damage(BYTE *buf, int n)
{
    for (int i = 0; i < n; i++)
        buf[i] = ((i >> 2) & 1) ? (BYTE)80 : (BYTE)-80;
}
static void gen_rescue(BYTE *buf, int n)
{
    LONG phase = 0;
    for (int i = 0; i < n; i++) {
        LONG inc = 8 + (i * 20 / n);
        phase += inc;
        LONG saw = ((phase & 0x7F) - 64);
        buf[i] = (BYTE)saw;
    }
}

int Sfx::init()
{
    memset(this, 0, sizeof(*this));
    static const struct { int len; UWORD per; } cfg[SFX_COUNT] = {
        { 512, 300 },   /* LASER */
        { 1024, 250 },  /* EXPLOSION */
        { 400, 400 },   /* DAMAGE */
        { 800, 350 },   /* RESCUE */
    };
    int allocated = 0;
    for (int i = 0; i < SFX_COUNT; i++) {
        sample_len[i]    = cfg[i].len;
        sample_period[i] = cfg[i].per;
        sample[i] = (BYTE *)AllocMem(cfg[i].len, MEMF_CHIP | MEMF_CLEAR);
        if (!sample[i]) continue;
        opened[i] = 1;
        allocated++;
        switch (i) {
        case SFX_LASER:     gen_laser    (sample[i], cfg[i].len); break;
        case SFX_EXPLOSION: gen_explosion(sample[i], cfg[i].len); break;
        case SFX_DAMAGE:    gen_damage   (sample[i], cfg[i].len); break;
        case SFX_RESCUE:    gen_rescue   (sample[i], cfg[i].len); break;
        }
    }
    /* Signal unavailability if not a single sample landed in chip
     * RAM — caller (main.cpp) logs the outcome and continues. */
    return allocated > 0 ? 0 : -1;
}

void Sfx::shutdown()
{
    for (int i = 0; i < SFX_COUNT; i++) {
        if (sample[i]) {
            FreeMem(sample[i], sample_len[i]);
            sample[i] = 0;
        }
        opened[i] = 0;
    }
}

void Sfx::tick(void) { /* nothing — modplay owns Paula timing */ }

void Sfx::play(int id)
{
    if (id < 0 || id >= SFX_COUNT) return;
    if (!sample[id]) return;
    /* length in WORDS for modplay_sfx. */
    modplay_sfx(sample[id], (UWORD)(sample_len[id] >> 1),
                sample_period[id], 64);
}
