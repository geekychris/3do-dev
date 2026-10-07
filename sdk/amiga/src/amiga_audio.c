/*
 * Paula emulation + streaming to the 3DO audio folio.
 *
 * Four Amiga channels are mixed in software into 22050 Hz mono 16-bit
 * buffers (441 samples = 1/50 s each) and queued on a music.lib sound
 * spooler playing through halfmonosample.dsp -> directout.dsp. Mixing runs
 * from gfx_swap(), keeping a few buffers queued; the Paula tick (music
 * player) runs at 50 Hz of *audio* time regardless of the game's frame rate.
 */
#include "audio.h"
#include "debug.h"
#include "kernel.h"
#include "operror.h"
#include "mem.h"
#include "soundspooler.h"
#include "string.h"

#include "amiga_internal.h"

#define RATE          22050
#define CHUNK         448                 /* samples per buffer (~1/50 s); bytes must be a
                                             multiple of 4 for audio DMA */
#define NUM_BUFS      8
#define TARGET_QUEUED 4                   /* ~80 ms of latency */
/* 16.16 step per output sample = (PAULA_CLOCK / period) / RATE */
#define STEP_NUM      10541918UL          /* PAULA_CLOCK_PAL * 65536 / RATE */

struct Custom custom;

static struct {
	const signed char *base;   /* latched sample start */
	ULONG len;                 /* latched length in bytes */
	ULONG pos;                 /* 16.16 position in bytes */
	int   on;
} s_ch[4];

static Item   s_out = -1, s_sampler = -1;
static SoundSpooler *s_sspl;
static short *s_buf[NUM_BUFS];
static ULONG  s_sig[NUM_BUFS];
static int    s_next;                 /* next buffer to fill */
static int    s_queued;
static int    s_ok;
static int    s_started;
static int    s_master = 64;
static void (*s_tick)(void);
static int    s_tick_num = 50, s_tick_den = 1;
static long   s_tick_acc;             /* in units of 1/(RATE*den) s */

/* ------------------------------------------------------------------ Paula */

static void
latch(int c)
{
	s_ch[c].base = (const signed char *)custom.aud[c].ac_ptr;
	s_ch[c].len  = (ULONG)custom.aud[c].ac_len * 2;
	s_ch[c].pos  = 0;
}

void
paula_dmacon(UWORD v)
{
	int c;
	for (c = 0; c < 4; c++) {
		if (!(v & (1 << c)))
			continue;
		if (v & 0x8000) {
			if (!s_ch[c].on) {
				latch(c);
				s_ch[c].on = 1;
			}
			custom.dmaconr |= (UWORD)(1 << c);
		} else {
			s_ch[c].on = 0;
			custom.dmaconr &= (UWORD)~(1 << c);
		}
	}
}

UWORD paula_lock(void)            { return 0; }
void  paula_unlock(UWORD sr)      { (void)sr; }
void  paula_master_volume(int v)  { s_master = v < 0 ? 0 : v > 64 ? 64 : v; }
void  paula_set_tick(void (*t)(void)) { s_tick = t; }

void
paula_set_tick_rate(int num, int den)
{
	if (num > 0 && den > 0) {
		s_tick_num = num;
		s_tick_den = den;
	}
}

int
paula_init(void (*tick)(void), int tick_hz)
{
	s_tick = tick;
	paula_set_tick_rate(tick_hz > 0 ? tick_hz : 50, 1);
	return 0;   /* audio itself comes up with gfx_init(); ticks run either way */
}

void
paula_exit(void)
{
	s_tick = 0;
}

/* Mix n samples of all channels into out. */
static void
mix(short *out, int n)
{
	int c, i;
	long acc[CHUNK];

	for (i = 0; i < n; i++)
		acc[i] = 0;
	for (c = 0; c < 4; c++) {
		ULONG pos, step, len;
		const signed char *b;
		int vol = custom.aud[c].ac_vol > 64 ? 64 : custom.aud[c].ac_vol;
		UWORD per = custom.aud[c].ac_per;

		if (!s_ch[c].on || !s_ch[c].base || s_ch[c].len < 2 || per < 64)
			continue;
		step = STEP_NUM / per;
		pos = s_ch[c].pos;
		b = s_ch[c].base;
		len = s_ch[c].len << 16;
		for (i = 0; i < n; i++) {
			if (pos >= len) {
				/* end of block: reload the (possibly new) registers */
				pos -= len;
				latch(c);
				b = s_ch[c].base;
				len = s_ch[c].len << 16;
				if (!b || len < (2UL << 16)) {
					pos = 0;
					break;
				}
				if (pos >= len)
					pos = 0;
			}
			acc[i] += (long)b[pos >> 16] * vol;
			pos += step;
		}
		s_ch[c].pos = pos;
	}
	for (i = 0; i < n; i++) {
		long v = (acc[i] * s_master) >> 6;   /* 4 x 127 x 64 x 64 >> 6 = 32512 fits */
		out[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
	}
}

/* ------------------------------------------------------------------ 3DO side */

void
amiga_audio_init(void)
{
	int i;

	memset(&custom, 0, sizeof(custom));
	memset(s_ch, 0, sizeof(s_ch));
	s_ok = 0;
	s_started = 0;
	s_queued = 0;
	s_next = 0;
	s_tick_acc = 0;
	if (OpenAudioFolio() < 0) {
		amiga_log("AMIGA3DO: audio folio unavailable - no sound\n");
		return;
	}
	s_out = LoadInstrument("directout.dsp", 0, 100);
	s_sampler = LoadInstrument("halfmonosample.dsp", 0, 100);
	if (s_out < 0 || s_sampler < 0) {
		amiga_log("AMIGA3DO: audio instruments unavailable (%d %d) - no sound\n",
		          (int)s_out, (int)s_sampler);
		return;
	}
	ConnectInstruments(s_sampler, "Output", s_out, "InputLeft");
	ConnectInstruments(s_sampler, "Output", s_out, "InputRight");
	StartInstrument(s_out, NULL);
	for (i = 0; i < NUM_BUFS; i++) {
		s_buf[i] = (short *)AllocMem(CHUNK * 2, MEMTYPE_AUDIO);
		if (!s_buf[i]) {
			amiga_log("AMIGA3DO: no memory for audio buffers - no sound\n");
			return;
		}
		memset(s_buf[i], 0, CHUNK * 2);
	}
	s_sspl = ssplCreateSoundSpooler(NUM_BUFS, s_sampler);
	if (!s_sspl) {
		amiga_log("AMIGA3DO: sound spooler failed - no sound\n");
		return;
	}
	s_ok = 1;
	amiga_log("AMIGA3DO: audio ready %d Hz\n", RATE);
}

/* Run the tick for each 1/50 s of mixed audio (or num/den Hz). */
static void
run_ticks(int samples)
{
	if (!s_tick)
		return;
	s_tick_acc += (long)samples * s_tick_num;
	while (s_tick_acc >= (long)RATE * s_tick_den) {
		s_tick_acc -= (long)RATE * s_tick_den;
		s_tick();
	}
}

void
amiga_audio_frame(void)
{
	int guard = 0;

	if (custom.dmacon) {
		paula_dmacon(custom.dmacon);
		custom.dmacon = 0;
	}
	if (!s_ok) {
		/* still run the music tick so game logic that waits on it works */
		run_ticks(CHUNK);
		return;
	}
	/* reclaim finished buffers */
	{
		ULONG done = GetCurrentSignals() & (s_sig[0] | s_sig[1] | s_sig[2] | s_sig[3] |
		                                    s_sig[4] | s_sig[5] | s_sig[6] | s_sig[7]);
		if (done) {
			int i;
			WaitSignal(done);           /* already set: returns at once, clears them */
			ssplProcessSignals(s_sspl, done, NULL);
			for (i = 0; i < NUM_BUFS; i++)
				if (done & s_sig[i]) {
					s_sig[i] = 0;
					s_queued--;
				}
		}
	}
	/* keep TARGET_QUEUED buffers in flight */
	while (s_queued < TARGET_QUEUED && guard++ < NUM_BUFS) {
		short *b = s_buf[s_next];
		long sig;
		run_ticks(CHUNK);
		mix(b, CHUNK);
		sig = ssplSpoolData(s_sspl, (char *)b, CHUNK * 2, NULL);
		if (sig < 0) {
			char eb[96];
			GetSysErr(eb, sizeof(eb), (Item)sig);
			amiga_log("AMIGA3DO: audio spool error %s - sound off\n", eb);
			s_ok = 0;
			break;
		}
		if (sig == 0)
			break;
		s_sig[s_next] = (ULONG)sig;
		s_next = (s_next + 1) % NUM_BUFS;
		s_queued++;
	}
	if (!s_started && s_queued > 0) {
		ssplStartSpooler(s_sspl, 0x7FFF);
		s_started = 1;
	}
}

void
amiga_audio_exit(void)
{
	int i;

	s_tick = 0;
	if (s_sspl) {
		ssplAbort(s_sspl, NULL);
		ssplDeleteSoundSpooler(s_sspl);
		s_sspl = 0;
	}
	for (i = 0; i < NUM_BUFS; i++) {
		if (s_buf[i])
			FreeMem(s_buf[i], CHUNK * 2);
		s_buf[i] = 0;
		s_sig[i] = 0;
	}
	if (s_sampler >= 0)
		UnloadInstrument(s_sampler);
	if (s_out >= 0)
		UnloadInstrument(s_out);
	s_sampler = s_out = -1;
	if (s_ok)
		CloseAudioFolio();
	s_ok = 0;
}

void mt_stopfx(void *c, UBYTE ch)
{
	(void)c;
	if (ch < 4)
		paula_dmacon((UWORD)(1 << ch));
}
