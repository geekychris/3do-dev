// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "sfx.h"
#include "modplay.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <string.h>

/* --- Sample generators (all output 8-bit signed) ---------------- */

static void gen_laser(BYTE *b, int n)
{
    /* Downward chirp — sawtooth pitch drops, amplitude fades. */
    LONG phase = 0;
    int i;
    for (i = 0; i < n; i++) {
        LONG inc = 30 - (i * 22 / n);
        LONG amp = 110 - (i * 110 / n);
        LONG saw;
        phase += inc;
        saw = ((phase & 0x7F) - 64);
        b[i] = (BYTE)((saw * amp) / 110);
    }
}

static void gen_explosion(BYTE *b, int n)
{
    ULONG seed = 0xC0DE1234UL;
    int i;
    for (i = 0; i < n; i++) {
        LONG amp;
        BYTE noise;
        seed = seed * 1103515245UL + 12345UL;
        amp = 127 - (i * 127 / n);
        noise = (BYTE)(seed >> 24);
        b[i] = (BYTE)((noise * amp) / 127);
    }
}

static void gen_hit(BYTE *b, int n)
{
    int i;
    /* Short low buzz — square-ish at 40Hz-ish. */
    for (i = 0; i < n; i++) {
        LONG amp = 90 - (i * 90 / n);
        b[i] = ((i >> 3) & 1) ? (BYTE)amp : (BYTE)-amp;
    }
}

static void gen_dock(BYTE *b, int n)
{
    /* Rising arpeggio — three ascending saw tones stitched. */
    LONG phase = 0;
    int i;
    for (i = 0; i < n; i++) {
        LONG inc = 8 + (i / (n / 24));
        LONG saw;
        phase += inc;
        saw = ((phase & 0x7F) - 64);
        b[i] = (BYTE)saw;
    }
}

static void gen_buy(BYTE *b, int n)
{
    /* Cash-register chirp — quick two-tone. */
    LONG phase = 0;
    int mid = n / 2;
    int i;
    for (i = 0; i < n; i++) {
        LONG inc = (i < mid) ? 40 : 60;
        LONG saw;
        LONG amp;
        phase += inc;
        saw = ((phase & 0x7F) - 64);
        amp = 80 - (i * 60 / n);
        b[i] = (BYTE)((saw * amp) / 80);
    }
}

/* --- state ------------------------------------------------------ */

static BYTE  *g_sample[SFX_COUNT];
static UWORD  g_len[SFX_COUNT];
static UWORD  g_period[SFX_COUNT];

int vt_sfx_init(void)
{
    int ok;
    int i;
    static const struct { int len; UWORD per; } cfg[SFX_COUNT] = {
        { 512, 300 },   /* LASER */
        { 1024, 250 },  /* EXPLOSION */
        { 400, 380 },   /* HIT */
        { 900, 320 },   /* DOCK */
        { 700, 260 },   /* BUY */
    };
    memset(g_sample, 0, sizeof(g_sample));
    ok = 0;
    for (i = 0; i < SFX_COUNT; i++) {
        g_len[i]    = cfg[i].len;
        g_period[i] = cfg[i].per;
        g_sample[i] = (BYTE *)AllocMem(cfg[i].len, MEMF_CHIP | MEMF_CLEAR);
        if (!g_sample[i]) continue;
        ok++;
        switch (i) {
        case SFX_LASER:     gen_laser    (g_sample[i], cfg[i].len); break;
        case SFX_EXPLOSION: gen_explosion(g_sample[i], cfg[i].len); break;
        case SFX_HIT:       gen_hit      (g_sample[i], cfg[i].len); break;
        case SFX_DOCK:      gen_dock     (g_sample[i], cfg[i].len); break;
        case SFX_BUY:       gen_buy      (g_sample[i], cfg[i].len); break;
        }
    }
    return ok > 0 ? 0 : -1;
}

void vt_sfx_shutdown(void)
{
    int i;
    for (i = 0; i < SFX_COUNT; i++) {
        if (g_sample[i]) {
            FreeMem(g_sample[i], g_len[i]);
            g_sample[i] = NULL;
        }
    }
}

void vt_sfx_play(int id)
{
    if (id < 0 || id >= SFX_COUNT || !g_sample[id]) return;
    modplay_sfx(g_sample[id], (UWORD)(g_len[id] >> 1),
                g_period[id], 64);
}
