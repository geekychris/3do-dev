/*
 * Paula emulation + streaming to the 3DO audio folio.
 *
 * DSP voices (the normal path): each Amiga channel is a varmono8.dsp
 * sample player on the 3DO's DSP, panned Amiga-style through mixer4x2.dsp.
 * An audio thread runs the Paula tick (music player) at 50 Hz of audio
 * clock time, independent of the game's frame rate, and turns the Paula
 * registers into DSP commands: DMA on starts the latched block as a looping
 * sample; new ptr/len registers while it plays link the next block after it
 * (LinkAttachments + ReleaseAttachment = "finish this block, then latch",
 * as Paula does); period and volume are knobs. No CPU mixing.
 *
 * Software mixer (fallback if the DSP voices can't be set up): the four
 * channels are mixed into 22050 Hz mono 16-bit buffers queued on a
 * music.lib sound spooler (halfmonosample.dsp -> directout.dsp), from
 * gfx_swap(); the Paula tick runs at 50 Hz of mixed-audio time.
 */
#include "audio.h"
#include "debug.h"
#include "kernel.h"
#include "operror.h"
#include "mem.h"
#include "soundspooler.h"
#include "string.h"
#include "semaphore.h"
#include "task.h"

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
	const signed char *zbase;  /* cache: is block zbase/zlen all zero? */
	ULONG zlen;
	int   zero;
	UWORD trig;                /* counts DMA off->on (DSP voices) */
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
static volatile int s_tick_num = 50, s_tick_den = 1;
static long   s_tick_acc;             /* in units of 1/(RATE*den) s */

/* ------------------------------------------------------------------ Paula */

static void
latch(int c)
{
	s_ch[c].base = (const signed char *)custom.aud[c].ac_ptr;
	s_ch[c].len  = (ULONG)custom.aud[c].ac_len * 2;
	s_ch[c].pos  = 0;
}

static Item s_lock = -1;              /* DSP voices: Paula state vs. the audio thread */
static int  s_dsp;                    /* DSP voices running */

static void lock(void)   { if (s_lock >= 0) LockSemaphore(s_lock, 1); }
static void unlock(void) { if (s_lock >= 0) UnlockSemaphore(s_lock); }

void
paula_dmacon(UWORD v)
{
	int c;
	lock();
	for (c = 0; c < 4; c++) {
		if (!(v & (1 << c)))
			continue;
		if (v & 0x8000) {
			if (!s_ch[c].on) {
				latch(c);
				s_ch[c].on = 1;
				s_ch[c].trig++;        /* DSP voices: a new note */
			}
			custom.dmaconr |= (UWORD)(1 << c);
		} else {
			s_ch[c].on = 0;
			custom.dmaconr &= (UWORD)~(1 << c);
		}
	}
	unlock();
}

/* With DSP voices the music tick runs on the audio thread: games that
 * change the player's state from the main loop can bracket that with
 * paula_lock()/paula_unlock() (like Disable()/Enable() around the Amiga's
 * audio interrupt). */
UWORD paula_lock(void)            { lock(); return 0; }
void  paula_unlock(UWORD sr)      { (void)sr; unlock(); }
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

/* Add (or, for the first channel, store) count samples of one channel
 * into acc, with no end-of-block checks: the caller guarantees the block
 * holds them. Unrolled because the ARM60 has no cache: every instruction
 * and every acc[] access is a DRAM cycle. */
static ULONG
mix_run(long *acc, int count, const signed char *b, ULONG pos, ULONG step, int vol, int first)
{
	if (first) {
		while (count >= 4) {
			acc[0] = (long)b[pos >> 16] * vol; pos += step;
			acc[1] = (long)b[pos >> 16] * vol; pos += step;
			acc[2] = (long)b[pos >> 16] * vol; pos += step;
			acc[3] = (long)b[pos >> 16] * vol; pos += step;
			acc += 4;
			count -= 4;
		}
		while (count--) {
			*acc++ = (long)b[pos >> 16] * vol;
			pos += step;
		}
	} else {
		while (count >= 4) {
			acc[0] += (long)b[pos >> 16] * vol; pos += step;
			acc[1] += (long)b[pos >> 16] * vol; pos += step;
			acc[2] += (long)b[pos >> 16] * vol; pos += step;
			acc[3] += (long)b[pos >> 16] * vol; pos += step;
			acc += 4;
			count -= 4;
		}
		while (count--) {
			*acc++ += (long)b[pos >> 16] * vol;
			pos += step;
		}
	}
	return pos;
}

/* Rest of a chunk for a channel looping one block (pos < len, step <= len):
 * the same sample-by-sample rule as mix() - wrap at the start of a sample,
 * leave pos unwrapped after the last - without re-latching. An all-zero
 * block (ProTracker's idle loop) only advances pos. Stores pos in s_ch[c].
 * Returns n. */
static int
mix_loop(int c, long *acc, int i, int n, int filled, const signed char *b,
         ULONG pos, ULONG step, ULONG len, int vol)
{
	ULONG bytes = len >> 16, k;
	int silent = (vol == 0);
	if (!silent) {
		if (s_ch[c].zbase != b || s_ch[c].zlen != bytes) {
			s_ch[c].zbase = b;
			s_ch[c].zlen = bytes;
			s_ch[c].zero = 1;
			for (k = 0; k < bytes; k++)
				if (b[k]) { s_ch[c].zero = 0; break; }
		}
		silent = s_ch[c].zero;
	}
	if (silent) {
		/* only the position moves: value after the last sample is
		 * ((pos + (r-1)*step) mod len) + step */
		ULONG r = (ULONG)(n - i);
		if (r) {
			ULONG v = pos + (r - 1) * step;
			if (v >= len)
				v %= len;
			pos = v + step;
		}
		/* zero contribution: only samples not yet holding data need it */
		for (; i < n; i++)
			if (i >= filled)
				acc[i] = 0;
		s_ch[c].pos = pos;
		return n;
	}
	for (; i < n && i < filled; i++) {
		if (pos >= len) pos -= len;
		acc[i] += (long)b[pos >> 16] * vol;
		pos += step;
	}
	for (; i < n; i++) {
		if (pos >= len) pos -= len;
		acc[i] = (long)b[pos >> 16] * vol;
		pos += step;
	}
	s_ch[c].pos = pos;
	return n;
}

/* Mix n samples of all channels into out. */
static void
mix(short *out, int n)
{
	int c, i;
	int filled = 0;          /* acc[0..filled) holds data from earlier channels */
	long acc[CHUNK];

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
		i = 0;
		while (i < n) {
			int k, end;
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
				/* Registers can't change during one mix() call, so every
				 * later reload in it re-latches this same block: loop it
				 * in place (identical result, no per-block overhead). The
				 * step <= len case keeps the wrap rule simple. */
				if (step <= len) {
					for (; filled < i; filled++)   /* close any gap first */
						acc[filled] = 0;
					i = mix_loop(c, acc, i, n, filled, b, pos, step, len, vol);
					if (i > filled)
						filled = i;
					pos = s_ch[c].pos;
					break;
				}
			}
			/* samples left before the block ends: mix them without checks.
			 * ProTracker loops tiny blocks (often one silent word), so
			 * avoid the (software) division when only a few fit. */
			{
				ULONG rem = len - pos;
				if (rem <= (step << 3)) {
					ULONG acc_step = step;
					k = 1;
					while (acc_step < rem) {
						acc_step += step;
						k++;
					}
				} else {
					k = (int)((rem + step - 1) / step);
				}
			}
			end = (k < n - i) ? i + k : n;
			if (vol == 0) {
				/* silent but running: just advance (block reloads still happen) */
				pos += step * (ULONG)(end - i);
				i = end;
				continue;
			}
			if (i < filled) {
				int add_end = end < filled ? end : filled;
				pos = mix_run(acc + i, add_end - i, b, pos, step, vol, 0);
				i = add_end;
			}
			if (i < end) {
				pos = mix_run(acc + i, end - i, b, pos, step, vol, 1);
				i = end;
				filled = i;
			}
		}
		s_ch[c].pos = pos;
		/* a channel that stopped early leaves a gap: zero it once */
		for (; filled < i; filled++)
			acc[filled] = 0;
	}
	for (i = filled; i < n; i++)
		acc[i] = 0;
	if (s_master == 64) {
		for (i = 0; i < n; i++) {
			long v = acc[i];         /* 4 x 127 x 64 = 32512 fits */
			out[i] = (short)v;
		}
	} else {
		for (i = 0; i < n; i++) {
			long v = (acc[i] * s_master) >> 6;
			out[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
		}
	}
}


/* ------------------------------------------------------------------ DSP voices */

#define VOICE_SAMPLES  96                 /* cached Sample items */
#define AUDIO_THREAD_PRI 150      /* above the game (100); 200+ is reserved */

typedef struct {
	const signed char *key;          /* the Paula block */
	ULONG keylen;
	Item  sample;
	void *copy;                      /* aligned copy (or 0: plays in place) */
	ULONG copylen;
	ULONG sum;                       /* checksum of a copied block */
	ULONG used;                      /* LRU stamp */
} VSample;

typedef struct {
	Item  ins, freq, amp;
	Item  att[8];                    /* attachments of the current note */
	Item  asmp[8];                   /* their samples */
	int   natt;
	int   playing;
	UWORD trig;                      /* last trigger seen */
	const signed char *qbase;        /* block queued to play next (or playing) */
	ULONG qlen;
	ULONG prev_att_len;              /* bytes of the block before qbase */
	int   rate, vol;                 /* last knob values (-1: unknown) */
} Voice;

static VSample s_vs[VOICE_SAMPLES];
static ULONG   s_zero[4];                /* looping silence (Paula's idle block) */
static Item    s_zero_smp = -1;
static ULONG   s_vs_clock;
static Voice   s_v[4];
static Item    s_mixer = -1, s_cue = -1, s_thread = -1;
static Item    s_clock = -1;             /* audio clock ownership */
static ULONG   s_old_duration;
static Item    s_parent = -1;
static ULONG   s_sig_ready, s_sig_done;
static volatile int s_quit;
static ULONG   s_ticks_logged;
static int     s_thread_ok;

/* Checksum of a copied block, to notice a game rewriting it in place
 * (synthesised sound). Short blocks are summed whole; long ones at 64
 * spread positions plus the tail, so a retriggered long sample costs
 * almost nothing. */
static ULONG
block_sum(const signed char *b, ULONG len)
{
	ULONG sum = len, k, step;
	if (len <= 256) {
		for (k = 0; k < len; k++)
			sum = sum * 31 + (UBYTE)b[k];
		return sum;
	}
	step = len >> 6;
	for (k = 0; k < len; k += step)
		sum = sum * 31 + (UBYTE)b[k];
	for (k = len - 16; k < len; k++)
		sum = sum * 31 + (UBYTE)b[k];
	return sum;
}

/* A block as a Sample item, cached. The DSP's sample DMA wants 4-byte
 * aligned addresses and lengths; Paula's are 2-byte. Misaligned blocks
 * up to 16 KB are copied (loops stay exact: an odd number of words is
 * stored twice); longer ones play in place, rounded out to 4 bytes.
 * Returns 0 for silence (ProTracker's one-word idle block). */
static Item
voice_sample(const signed char *b, ULONG len)
{
	int i, victim = -1;
	ULONG oldest = 0xFFFFFFFFUL, sum = 0, k;
	int all_zero = 1;
	VSample *v;
	TagArg tags[8];
	const void *addr;
	ULONG bytes;

	if (!b || len < 2)
		return 0;
	if (len <= 64) {
		for (k = 0; k < len; k++)
			if (b[k]) { all_zero = 0; break; }
		if (all_zero)
			return 0;
	}
	if (len <= 4)
		return 0;
	for (i = 0; i < VOICE_SAMPLES; i++) {
		v = &s_vs[i];
		if (v->sample > 0 && v->key == b && v->keylen == len) {
			if (v->copy) {
				/* the game may rewrite a buffer in place (synthesised sound) */
				sum = block_sum(b, len);
				if (sum != v->sum)
					break;              /* stale copy: rebuild below */
			}
			v->used = ++s_vs_clock;
			return v->sample;
		}
	}
	if (i < VOICE_SAMPLES) {
		victim = i;
	} else {
		for (i = 0; i < VOICE_SAMPLES; i++) {
			int busy = 0, c, a;
			if (s_vs[i].sample <= 0) { victim = i; break; }
			/* never evict a sample a voice is using */
			for (c = 0; c < 4; c++)
				for (a = 0; a < s_v[c].natt; a++)
					if (s_vs[i].sample == s_v[c].asmp[a])
						busy = 1;
			if (!busy && s_vs[i].used < oldest) {
				oldest = s_vs[i].used;
				victim = i;
			}
		}
		if (victim < 0)
			return 0;
	}
	v = &s_vs[victim];
	if (v->sample > 0)
		UnloadSample(v->sample);        /* also deletes its attachments */
	if (v->copy)
		FreeMem(v->copy, v->copylen);
	memset(v, 0, sizeof(*v));
	if ((((ULONG)b | len) & 3) && len <= 16384) {
		ULONG n = (len & 3) ? len * 2 : len;
		v->copy = AllocMem(n, MEMTYPE_AUDIO);
		if (!v->copy)
			return 0;
		memcpy(v->copy, b, len);
		if (n != len)
			memcpy((char *)v->copy + len, b, len);
		v->copylen = n;
		v->sum = block_sum(b, len);
		addr = v->copy;
		bytes = n;
	} else {
		ULONG a0 = (ULONG)b & ~3UL, a1 = ((ULONG)b + len + 3) & ~3UL;
		addr = (const void *)a0;
		bytes = a1 - a0;
	}
	tags[0].ta_Tag = AF_TAG_ADDRESS;      tags[0].ta_Arg = (TagData)addr;
	tags[1].ta_Tag = AF_TAG_NUMBYTES;     tags[1].ta_Arg = (TagData)bytes;
	tags[2].ta_Tag = AF_TAG_WIDTH;        tags[2].ta_Arg = (TagData)1;
	tags[3].ta_Tag = AF_TAG_CHANNELS;     tags[3].ta_Arg = (TagData)1;
	tags[4].ta_Tag = AF_TAG_SUSTAINBEGIN; tags[4].ta_Arg = (TagData)0;
	tags[5].ta_Tag = AF_TAG_SUSTAINEND;   tags[5].ta_Arg = (TagData)bytes;
	tags[6].ta_Tag = TAG_END;
	v->sample = CreateSample(tags);
	if (v->sample < 0) {
		if (v->copy)
			FreeMem(v->copy, v->copylen);
		memset(v, 0, sizeof(*v));
		return 0;
	}
	v->key = b;
	v->keylen = len;
	v->used = ++s_vs_clock;
	return v->sample;
}

/* Paula period -> varmono8 phase increment ($8000 = one sample per
 * 44.1 kHz frame) */
static int
voice_rate(UWORD per)
{
	ULONG r;
	if (per < 64)
		per = 64;
	r = 2635504UL / per;                 /* PAULA_CLOCK_PAL * 32768 / 44100 */
	return r > 0xFFFF ? 0xFFFF : (int)r;
}

static void
voice_knobs(int c, UWORD per, int vol)
{
	Voice *v = &s_v[c];
	int rate = voice_rate(per);
	int amp;
	if (vol > 64)
		vol = 64;
	amp = (vol * s_master * 0x7FFF) >> 12;   /* vol/64 * master/64 */
	if (rate != v->rate) {
		TweakRawKnob(v->freq, rate);
		v->rate = rate;
	}
	if (amp != v->vol) {
		TweakRawKnob(v->amp, amp);
		v->vol = amp;
	}
}

static void
voice_detach(int c)
{
	Voice *v = &s_v[c];
	int a;
	for (a = 0; a < v->natt; a++)
		if (v->att[a] > 0)
			DetachSample(v->att[a]);
	v->natt = 0;
}

/* Silence a voice. A stopped (or drained) DSP sample player keeps
 * outputting its last value - a DC offset - so silence is amplitude 0. */
static void
voice_stop(int c)
{
	Voice *v = &s_v[c];
	if (v->playing) {
		TweakRawKnob(v->amp, 0);
		v->vol = 0;
		v->playing = 0;
	}
}

/* Bring one DSP voice in line with Paula channel c (snapshot taken under
 * the lock). */

#ifdef VOICE_DEBUG
#define VLOG(x) amiga_log x
#else
#define VLOG(x)
#endif

static void
voice_sync(int c, int on, UWORD trig, const signed char *lbase, ULONG llen,
           const signed char *rbase, ULONG rlen, UWORD per, int vol)
{
	Voice *v = &s_v[c];
	if (!on) {
		if (v->playing) VLOG(("V%d t%d off\n", c, (int)s_ticks_logged));
		voice_stop(c);
		v->trig = trig;
		return;
	}
	if (trig != v->trig) {
		/* new note: the block latched at DMA-on, looping until the
		 * registers say otherwise */
		Item smp;
		v->trig = trig;
		smp = voice_sample(lbase, llen);
		VLOG(("V%d t%d note %x+%d per %d vol %d smp %d\n", c, (int)s_ticks_logged, (int)lbase, (int)llen, per, vol, (int)smp));
		v->qbase = lbase;
		v->qlen = llen;
		if (smp > 0 && v->natt >= 1 && v->asmp[0] == smp) {
			/* same block again (drums, explosions): keep the attachment,
			 * drop any linked blocks and restart - no detach/attach gap */
			int a;
			LinkAttachments(v->att[0], 0);
			for (a = 1; a < v->natt; a++)
				DetachSample(v->att[a]);
			v->natt = 1;
		} else {
			voice_stop(c);
			StopInstrument(v->ins, NULL);
			voice_detach(c);
			if (smp <= 0)
				return;
			v->att[0] = AttachSample(v->ins, smp, "InFIFO");
			if (v->att[0] < 0)
				return;
			v->asmp[0] = smp;
			v->natt = 1;
		}
		v->rate = v->vol = -1;
		voice_knobs(c, per, vol);
		StartInstrument(v->ins, NULL);
		v->playing = 1;
	}
	if (!v->playing)
		return;
	voice_knobs(c, per, vol);
	if (rbase != v->qbase || rlen != v->qlen) {
		/* new registers: finish the current block, then play (and loop)
		 * the new one - or stop, for silence */
		Item cur = v->att[v->natt - 1], smp = voice_sample(rbase, rlen), nxt = 0;
		if (smp <= 0)
			smp = s_zero_smp;       /* Paula then loops silence; so do we */
		VLOG(("V%d t%d next %x+%d smp %d\n", c, (int)s_ticks_logged, (int)rbase, (int)rlen, (int)smp));
		v->qbase = rbase;
		v->qlen = rlen;
		if (smp > 0 && v->natt < 8) {
			nxt = AttachSample(v->ins, smp, "InFIFO");
			if (nxt > 0) {
				v->asmp[v->natt] = smp;
				v->att[v->natt++] = nxt;
			} else
				nxt = 0;
		}
		LinkAttachments(cur, nxt);
		ReleaseAttachment(cur, NULL);
		if (v->natt >= 8) {
			/* a game streaming blocks: keep the newest two */
			DetachSample(v->att[0]);
			memmove(v->att, v->att + 1, (v->natt - 1) * sizeof(Item));
			memmove(v->asmp, v->asmp + 1, (v->natt - 1) * sizeof(Item));
			v->natt--;
		}
	}
}

/* Snapshot the Paula state under the lock, then drive the DSP. */
static void
voices_update(void)
{
	int c, on[4];
	UWORD trig[4], per[4];
	int vol[4];
	const signed char *lb[4], *rb[4];
	ULONG ll[4], rl[4];
	lock();
	if (custom.dmacon) {
		paula_dmacon(custom.dmacon);
		custom.dmacon = 0;
	}
	for (c = 0; c < 4; c++) {
		on[c] = s_ch[c].on;
		trig[c] = s_ch[c].trig;
		lb[c] = s_ch[c].base;
		ll[c] = s_ch[c].len;
		rb[c] = (const signed char *)custom.aud[c].ac_ptr;
		rl[c] = (ULONG)custom.aud[c].ac_len * 2;
		per[c] = custom.aud[c].ac_per;
		vol[c] = custom.aud[c].ac_vol;
	}
	unlock();
	for (c = 0; c < 4; c++)
		voice_sync(c, on[c], trig[c], lb[c], ll[c], rb[c], rl[c], per[c], vol[c]);
}

static void
voices_free(void)
{
	int c, i;
	for (c = 0; c < 4; c++) {
		voice_stop(c);
		if (s_v[c].ins > 0)
			StopInstrument(s_v[c].ins, NULL);
		voice_detach(c);
		if (s_v[c].ins > 0)
			UnloadInstrument(s_v[c].ins);
		s_v[c].ins = -1;
	}
	for (i = 0; i < VOICE_SAMPLES; i++) {
		if (s_vs[i].sample > 0)
			UnloadSample(s_vs[i].sample);
		if (s_vs[i].copy)
			FreeMem(s_vs[i].copy, s_vs[i].copylen);
		memset(&s_vs[i], 0, sizeof(s_vs[i]));
	}
	if (s_zero_smp > 0)
		UnloadSample(s_zero_smp);
	s_zero_smp = -1;
	if (s_mixer > 0)
		UnloadInstrument(s_mixer);
	if (s_cue > 0)
		DeleteItem(s_cue);
	s_mixer = s_cue = -1;
}

static Err s_verr;                     /* why the DSP voices failed */
static const char *s_vwhat = "";
#define VCHECK(x, what) do { Err e_ = (Err)(x); if (e_ < 0) { s_verr = e_; s_vwhat = what; return 0; } } while (0)

static int
voices_init(void)
{
	/* Amiga stereo: channels 0 and 3 left, 1 and 2 right, partly blended
	 * so headphones aren't harsh. Each side's gains sum to < $7FFF. */
	static const char *lg[4] = { "LeftGain0", "LeftGain1", "LeftGain2", "LeftGain3" };
	static const char *rg[4] = { "RightGain0", "RightGain1", "RightGain2", "RightGain3" };
	static const int near_gain = 0x2A00, far_gain = 0x1400;
	static const char *in[4] = { "Input0", "Input1", "Input2", "Input3" };
	int c;
	TagArg none[1];
	none[0].ta_Tag = TAG_END;
	s_mixer = LoadInstrument("mixer4x2.dsp", 0, 100);
	VCHECK(s_mixer, "mixer4x2.dsp");
	for (c = 0; c < 4; c++) {
		int left = (c == 0 || c == 3);
		Item k;
		s_v[c].ins = LoadInstrument("varmono8.dsp", 0, 100);
		VCHECK(s_v[c].ins, "varmono8.dsp");
		s_v[c].freq = GrabKnob(s_v[c].ins, "Frequency");
		VCHECK(s_v[c].freq, "Frequency knob");
		s_v[c].amp = GrabKnob(s_v[c].ins, "Amplitude");
		VCHECK(s_v[c].amp, "Amplitude knob");
		s_v[c].rate = s_v[c].vol = -1;
		VCHECK(ConnectInstruments(s_v[c].ins, "Output", s_mixer, (char *)in[c]), "connect");
		k = GrabKnob(s_mixer, (char *)lg[c]);
		if (k < 0 && c == 1)
			k = GrabKnob(s_mixer, "LeftGain");      /* name in some docs */
		if (k >= 0)
			TweakRawKnob(k, left ? near_gain : far_gain);
		k = GrabKnob(s_mixer, (char *)rg[c]);
		if (k >= 0)
			TweakRawKnob(k, left ? far_gain : near_gain);
	}
	s_cue = CreateItem(MKNODEID(AUDIONODE, AUDIO_CUE_NODE), none);
	VCHECK(s_cue, "cue");
	{
		TagArg t[7];
		memset(s_zero, 0, sizeof(s_zero));
		t[0].ta_Tag = AF_TAG_ADDRESS;      t[0].ta_Arg = (TagData)s_zero;
		t[1].ta_Tag = AF_TAG_NUMBYTES;     t[1].ta_Arg = (TagData)sizeof(s_zero);
		t[2].ta_Tag = AF_TAG_WIDTH;        t[2].ta_Arg = (TagData)1;
		t[3].ta_Tag = AF_TAG_CHANNELS;     t[3].ta_Arg = (TagData)1;
		t[4].ta_Tag = AF_TAG_SUSTAINBEGIN; t[4].ta_Arg = (TagData)0;
		t[5].ta_Tag = AF_TAG_SUSTAINEND;   t[5].ta_Arg = (TagData)sizeof(s_zero);
		t[6].ta_Tag = TAG_END;
		s_zero_smp = CreateSample(t);
		VCHECK(s_zero_smp, "silence sample");
	}
	StartInstrument(s_mixer, NULL);
	return 1;
}

/* The audio thread: owns every audio item; runs the tick on the audio
 * clock and syncs the voices after each one. */
static void
audio_thread(void)
{
	AudioTime next;
	ULONG frac = 0, rate;
	int ok;

	ok = OpenAudioFolio() >= 0;
	if (!ok) s_vwhat = "OpenAudioFolio";
	if (ok && !voices_init()) {
		voices_free();
		CloseAudioFolio();
		ok = 0;
	}
	s_thread_ok = ok;
	SendSignal(s_parent, s_sig_ready);
	if (!ok)
		return;
	/* 147 DSP frames per audio clock tick = exactly 300 Hz, so 50 Hz Paula
	 * ticks are exactly 6 clock ticks apart (the default 240 Hz clock
	 * would put them 4 or 5 ticks apart) */
	s_clock = OwnAudioClock();
	if (s_clock >= 0) {
		s_old_duration = GetAudioDuration();
		SetAudioDuration(s_clock, 147);
	}
	rate = (ULONG)GetAudioRate();            /* audio clock, 16.16 ticks per second */
	if (rate == 0)
		rate = 240UL << 16;
	next = GetAudioTime();
	while (!s_quit) {
		/* next tick in 1/(num) s steps of the audio clock, keeping the
		 * remainder so 240 Hz / 50 Hz = 4.8 doesn't drift */
		ULONG num = (ULONG)s_tick_num << 16, den = (ULONG)s_tick_den;
		frac += rate * den;
		next += frac / num;
		frac %= num;
		SleepUntilTime(s_cue, next);
		if (s_quit)
			break;
		if ((long)(GetAudioTime() - next) > (long)(rate >> 18))
			next = GetAudioTime();           /* far behind: don't rush */
		lock();
		if (s_tick)
			s_tick();
		unlock();
		voices_update();
		if ((++s_ticks_logged % 250) == 0)
			amiga_log("AMIGA3DO: audio ticks=%d\n", (int)s_ticks_logged);
	}
	voices_free();
	if (s_clock >= 0) {
		SetAudioDuration(s_clock, s_old_duration);
		DisownAudioClock(s_clock);
		s_clock = -1;
	}
	CloseAudioFolio();
	SendSignal(s_parent, s_sig_done);
}

static int
voices_start(void)
{
	s_lock = CreateSemaphore("amiga.paula", 100);
	if (s_lock < 0) {
		amiga_log("AMIGA3DO: audio semaphore failed (%d)\n", (int)s_lock);
		return 0;
	}
	s_sig_ready = (ULONG)AllocSignal(0);
	s_sig_done = (ULONG)AllocSignal(0);
	if (!s_sig_ready || !s_sig_done) {
		amiga_log("AMIGA3DO: audio signals failed\n");
		return 0;
	}
	s_parent = CURRENTTASK->t.n_Item;
	s_quit = 0;
	s_thread_ok = 0;
	s_thread = CreateThread("amiga.audio", AUDIO_THREAD_PRI, audio_thread, 4096);
	if (s_thread < 0) {
		char eb[96];
		GetSysErr(eb, sizeof(eb), s_thread);
		amiga_log("AMIGA3DO: audio thread failed: %s (main pri %d)\n", eb,
		          (int)CURRENTTASK->t.n_Priority);
		return 0;
	}
	WaitSignal(s_sig_ready);
	if (!s_thread_ok) {
		char eb[96];
		eb[0] = 0;
		if (s_verr < 0)
			GetSysErr(eb, sizeof(eb), (Item)s_verr);
		amiga_log("AMIGA3DO: DSP voices failed at %s: %s\n", s_vwhat, eb);
	}
	return s_thread_ok;
}

static void
voices_stop(void)
{
	if (s_thread >= 0 && s_thread_ok) {
		s_quit = 1;
		WaitSignal(s_sig_done);
	}
	s_thread = -1;
	if (s_sig_ready) FreeSignal(s_sig_ready);
	if (s_sig_done) FreeSignal(s_sig_done);
	s_sig_ready = s_sig_done = 0;
	if (s_lock >= 0)
		DeleteSemaphore(s_lock);
	s_lock = -1;
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
	s_dsp = 0;
	if (voices_start()) {
		s_dsp = 1;
		s_ok = 1;
		amiga_log("AMIGA3DO: audio ready (DSP voices)\n");
		return;
	}
	voices_stop();
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

	if (s_dsp)
		return;            /* the audio thread does everything */
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

	if (s_dsp) {
		voices_stop();
		s_dsp = 0;
		s_ok = 0;
		s_tick = 0;
		return;
	}
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
