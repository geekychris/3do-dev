/*
 * Every sound is synthesised at startup, as in Sfx.cs: a phase-integrated
 * oscillator (sweeping pitch never clicks), square / triangle / sine, short
 * attack and release. Signed 8-bit at 11050 Hz (Paula period 321), played
 * through the layer's Paula emulation - i.e. on the 3DO's DSP.
 *   channel 0: waka   1, 3: other effects (alternating)   2: ambience loop
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "paula.h"
#include "sfx.h"

#define RATE   11050L               /* Paula period 321 */
#define PERIOD 321

enum { W_SQUARE, W_TRI, W_SINE };

static BYTE  *clip[SFX_COUNT];
static ULONG  clip_len[SFX_COUNT];
static UWORD  silence[2];
static int    waka_flip, fx_chan = 1, amb_now = -1;

static const short sine256[64] = {     /* quarter wave, Q14 */
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
    6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
    11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
    15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379
};

static long wave(int w, ULONG ph)      /* ph: 16-bit phase -> Q14 */
{
    ULONG p = ph & 0xFFFF;
    switch (w) {
    case W_SQUARE: return p < 32768 ? 9830 : -9830;                  /* +-0.6 */
    case W_TRI: {
        long d = (long)p - 32768;                                      /* 1 - 4|p - 0.5| */
        if (d < 0) d = -d;
        return 16384 - (d >> 1);
    }
    default: {
        int q = (int)(p >> 8), i = q & 63;
        long v = (q & 64) ? sine256[63 - i] : sine256[i];
        return (q & 128) ? -v : v;
    }
    }
}

/* frequency (Hz) at sample i of clip id */
static long freq(int id, long i)
{
    static const short start[16] = { 494, 988, 740, 622, 988, 740, 622, 0, 523, 1047, 784, 659, 1047, 784, 659, 0 };
    static const short clear[12] = { 523, 659, 784, 1047, 784, 1047, 1319, 1568, 1319, 1568, 2093, 2093 };
    static const short extra[6] = { 1319, 1568, 2637, 2093, 2349, 3136 };
    static const short key[5] = { 523, 659, 784, 1047, 1319 };
    long n;
    switch (id) {
    case SFX_WAKA_A: return 260 + 260 * i / (RATE / 10);
    case SFX_WAKA_B: return 520 - 260 * i / (RATE / 10);
    case SFX_KEY:    n = i / (RATE / 10); return key[n > 4 ? 4 : n];
    case SFX_EAT: {
        long x = i * 1024 / (RATE * 35 / 100);                         /* 0..1024 */
        long px = x * (1331 - 307 * x / 1024) / 1024;                  /* ~ x^0.7 */
        return 180 + 1220 * px / 1024;
    }
    case SFX_DEATH: {
        long base = 900 - 810 * i / (RATE * 3 / 2);
        long ph = i * 5664 / 100;          /* sin(60 t): 60/(2 pi) turns/s = 56.64 phase units/sample */
        return base + base * 12 / 100 * wave(W_SINE, (ULONG)ph) / 16384;
    }
    case SFX_START:  n = i / (RATE * 12 / 100); return start[n > 15 ? 15 : n];
    case SFX_CLEAR:  n = i / (RATE * 9 / 100);  return clear[n > 11 ? 11 : n];
    case SFX_EXTRA:  n = i / (RATE * 8 / 100);  return extra[n > 5 ? 5 : n];
    case SFX_SIREN:  return 420 + 200 * wave(W_SINE, (ULONG)(i * 65536L / (RATE * 6 / 10))) / 16384;
    case SFX_FRIGHT: {
        long s = wave(W_SINE, (ULONG)(i * 65536L / (RATE / 4)));
        if (s < 0) s = -s;
        return 180 + 120 * s / 16384;
    }
    }
    return 0;
}

static short wtab[3][256];             /* one cycle of each wave, Q14 */

static int build(int id, long ms, int w, int amp100, int looped)
{
    /* the ARM60 has no divide instruction: pitch is worked out every 16
     * samples, everything per sample is multiply-and-shift */
    long n = RATE * ms / 1000, i, att = RATE / 200, rel = RATE * 3 / 100;
    long amp = (long)amp100 * 256 / 100, f = 0, inc = 0;
    ULONG phase = 0;
    BYTE *b;
    n &= ~1L;
    b = (BYTE *)AllocMem((ULONG)n, MEMF_CLEAR);
    if (!b) return 0;
    for (i = 0; i < n; i++) {
        long v, env = 256;
        if ((i & 15) == 0) {
            f = freq(id, i);
            inc = (f * 380) >> 6;                   /* f * 65536 / 11050 */
        }
        phase += (ULONG)inc;
        if (!looped) {
            if (i < att) env = i * 256 / att;
            else if (n - i < rel) env = (n - i) * 256 / rel;
        }
        v = f <= 0 ? 0 : (((wtab[w][(phase >> 8) & 255] * amp) >> 8) * env) >> 8;   /* Q14 */
        b[i] = (BYTE)((v * 127) >> 14);
    }
    clip[id] = b;
    clip_len[id] = (ULONG)n;
    return 1;
}

int sfx_init(void)
{
    int w, i;
    for (w = 0; w < 3; w++)
        for (i = 0; i < 256; i++)
            wtab[w][i] = (short)wave(w, (ULONG)i << 8);
    return build(SFX_WAKA_A, 100, W_SQUARE, 50, 0) & build(SFX_WAKA_B, 100, W_SQUARE, 50, 0) &
           build(SFX_KEY, 600, W_TRI, 80, 0) & build(SFX_EAT, 350, W_SQUARE, 45, 0) &
           build(SFX_DEATH, 1500, W_TRI, 90, 0) & build(SFX_START, 1900, W_SQUARE, 35, 0) &
           build(SFX_CLEAR, 1100, W_TRI, 70, 0) & build(SFX_EXTRA, 500, W_TRI, 60, 0) &
           build(SFX_SIREN, 1200, W_SINE, 100, 1) & build(SFX_FRIGHT, 1000, W_TRI, 100, 1);
}

static void start(int ch, int id, int vol, int loop)
{
    UWORD k;
    if (!clip[id]) return;
    k = paula_lock();
    paula_dmacon((UWORD)(1 << ch));
    custom.aud[ch].ac_ptr = (UWORD *)clip[id];
    custom.aud[ch].ac_len = (UWORD)(clip_len[id] / 2);
    custom.aud[ch].ac_per = PERIOD;
    custom.aud[ch].ac_vol = (UWORD)vol;
    paula_dmacon((UWORD)(0x8000 | (1 << ch)));
    if (!loop) {                       /* after the clip, Paula latches silence */
        custom.aud[ch].ac_ptr = silence;
        custom.aud[ch].ac_len = 1;
    }
    paula_unlock(k);
}

void sfx_play(int id)
{
    start(fx_chan, id, 40, 0);
    fx_chan = fx_chan == 1 ? 3 : 1;
}

void sfx_waka(void)
{
    waka_flip = !waka_flip;
    start(0, waka_flip ? SFX_WAKA_A : SFX_WAKA_B, 30, 0);
}

void sfx_ambience(int which)
{
    if (which == amb_now) return;
    amb_now = which;
    if (which == 0) {
        UWORD k = paula_lock();
        paula_dmacon(1 << 2);
        paula_unlock(k);
    } else
        start(2, which == 2 ? SFX_FRIGHT : SFX_SIREN, which == 2 ? 14 : 8, 1);
}
