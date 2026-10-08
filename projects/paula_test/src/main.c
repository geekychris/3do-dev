/*
 * Paula emulation test: a fixed script of Paula register operations, one
 * step per 50 Hz tick (25 ticks = 0.5 s per segment). test.py records the
 * audio and checks each segment's pitch, loudness and timing, so the DSP
 * voice backend (and the software mixer fallback) are tested against what
 * Paula would play, not against each other.
 *
 * Segments (channel 0 unless noted):
 *   0  64-byte square loop, period 428          -> 129.5 Hz
 *   1  period 214 while playing                 -> 259 Hz
 *   2  volume 64 -> 16                          -> 1/4 amplitude
 *   3  DMA off                                  -> silence
 *   4  one-shot: 2048-byte sine block, then ProTracker's empty word
 *                                               -> 0.124 s of sound, then silence
 *   5  start block (noise, 1024 bytes) then loop block (40-byte square)
 *                                               -> noise, then 443 Hz tone
 *   6  misaligned odd-word loop: 34 bytes at offset 2, period 428
 *                                               -> 243.7 Hz
 *   7  four channels: 64-byte squares at periods 428/339/285/214
 *                                               -> 129.5, 163.5, 194.5, 259 Hz
 *   8  retrigger every tick (like drums): a 128-byte one-shot (two
 *      cycles, 10 ms at period 277)            -> 50 bursts a second
 *   9  all off                                  -> silence
 */
#include "amiga3do.h"
#include "paula.h"
#include "bridge_client.h"
#include "string.h"

#define SEG 25

static ULONG square128_w[32], square64_w[16], square40_w[10], sine_w[512], noise_w[256], odd_w[10];
static const UWORD empty_word[2] = { 0, 0 };
static int tick_n = -1;
static volatile int done;

static const UWORD pal[2] = { 0x0000, 0x7FFF };

static void
set(int c, const void *p, ULONG bytes, UWORD per, UWORD vol)
{
    custom.aud[c].ac_ptr = (UWORD *)p;
    custom.aud[c].ac_len = (UWORD)(bytes / 2);
    custom.aud[c].ac_per = per;
    custom.aud[c].ac_vol = vol;
}

static void
tick(void)
{
    int seg, t;
    tick_n++;
    seg = tick_n / SEG;
    t = tick_n % SEG;
    if (t == 0)
        amiga_log("PAULA: segment %d tick %d\n", seg, tick_n);
    switch (seg) {
    case 0:
        if (t == 0) { set(0, square64_w, 64, 428, 64); paula_dmacon(0x8001); }
        break;
    case 1:
        if (t == 0) custom.aud[0].ac_per = 214;
        break;
    case 2:
        if (t == 0) custom.aud[0].ac_vol = 16;
        break;
    case 3:
        if (t == 0) paula_dmacon(0x0001);
        break;
    case 4:
        if (t == 0) {
            set(0, sine_w, 2048, 214, 64);
            paula_dmacon(0x8001);
            set(0, empty_word, 2, 214, 64);    /* latched after the block */
        }
        break;
    case 5:
        if (t == 0) {
            paula_dmacon(0x0001);
            set(0, noise_w, 1024, 200, 64);
            paula_dmacon(0x8001);
            set(0, square40_w, 40, 200, 64);
        }
        break;
    case 6:
        if (t == 0) {
            paula_dmacon(0x0001);
            set(0, (const char *)odd_w + 2, 34, 428, 64);
            paula_dmacon(0x8001);
        }
        break;
    case 7:
        if (t == 0) {
            paula_dmacon(0x000F);
            set(0, square64_w, 64, 428, 64);
            set(1, square64_w, 64, 339, 64);
            set(2, square64_w, 64, 285, 64);
            set(3, square64_w, 64, 214, 64);
            paula_dmacon(0x800F);
        }
        break;
    case 8:
        /* a 10 ms one-shot (two cycles at 200 Hz) restarted every tick */
        if (t == 0)
            paula_dmacon(0x000F);
        paula_dmacon(0x0001);
        set(0, square128_w, 128, 277, 64);
        paula_dmacon(0x8001);
        set(0, empty_word, 2, 277, 64);
        break;
    case 9:
        if (t == 0)
            paula_dmacon(0x000F);
        if (t == SEG - 1) {
            amiga_log("PAULA: done\n");
            done = 1;
        }
        break;
    }
}

int
main(void)
{
    signed char *b;
    int i;
    ab_init("PAULA");
    b = (signed char *)square64_w;
    for (i = 0; i < 64; i++) b[i] = i < 32 ? 100 : -100;
    b = (signed char *)square128_w;
    for (i = 0; i < 128; i++) b[i] = (i & 63) < 32 ? 100 : -100;
    b = (signed char *)square40_w;
    for (i = 0; i < 40; i++) b[i] = i < 20 ? 100 : -100;
    b = (signed char *)odd_w;
    for (i = 0; i < 40; i++) b[i] = 0;
    for (i = 0; i < 34; i++) b[2 + i] = i < 17 ? 100 : -100;
    b = (signed char *)sine_w;
    for (i = 0; i < 2048; i++) {
        /* triangle approximation of a 16-sample-period wave (~1036 Hz at period 214) */
        int ph = i & 15;
        b[i] = (signed char)(ph < 8 ? -112 + ph * 28 : 112 - (ph - 8) * 28);
    }
    b = (signed char *)noise_w;
    {
        ULONG r = 1;
        for (i = 0; i < 1024; i++) { r = r * 1103515245UL + 12345UL; b[i] = (signed char)(r >> 24); }
    }
    if (!gfx_init(pal, 2))
        return 1;
    paula_init(tick, 50);
    amiga_log("PAULA: ready\n");
    while (!done) {
        SetRast(gfx_back(), 0);
        gfx_swap();
    }
    for (i = 0; i < 10; i++)
        gfx_swap();
    paula_exit();
    gfx_exit();
    return 0;
}
