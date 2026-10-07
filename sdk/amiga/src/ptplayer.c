/*
 * ProTracker MOD player on the Paula emulation, see ptplayer.h.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Chris Collins
 * From the Atari ST ports of geekychris/amiga_games (st_port/ptplayer.c),
 * made C89 for Norcroft.
 *
 * Written from the ProTracker replay semantics (as documented in the
 * ProTracker 2.3 player and the MOD format notes); ticks run from the
 * VBL through paula.c at 50 Hz, or BPM * 2 / 5 Hz after an Fxx >= 32.
 */
#include <string.h>
#include "paula.h"
#include "ptplayer.h"

#define PAULA_HZ 3546895L

UBYTE mt_Enable;
UBYTE mt_E8Trigger;
UBYTE mt_MusicChannels = 4;

/* finetune 0 periods, C-1 .. B-3 */
static const UWORD base_period[36] = {
	856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
	428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
	214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
};
static UWORD ptab[16][36];		/* by finetune 0..15 (8..15 = -8..-1) */

static const UBYTE sine[32] = {
	0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
	255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24,
};

struct sample {
	const signed char *data;
	ULONG len;			/* bytes */
	const signed char *loop;
	ULONG loop_len;			/* bytes; <= 2: no loop */
	UBYTE vol, finetune;
};

struct chan {
	const struct sample *smp;
	int period, vol, finetune;
	int cmd, param;
	int porta_target, porta_speed;
	int vib_cmd, vib_pos;
	int trem_cmd, trem_pos;
	int offset_mem;
	int loop_row, loop_cnt;
	int delay_period;		/* pending ED note */
	int out_period, out_vol;	/* this tick, after effects */
	int sfx_ticks, sfx_pri;		/* sound effect playing */
};

static const UBYTE *mod;
static struct sample smp[32];
static struct chan ch[4];
static const UBYTE *patterns;
static int song_len, pos, row, tick, speed, bpm;
static int break_row, jump_pos, pdelay, pdelay_cnt;
static int master = 64;
static UBYTE music_mask = 0x0f;
static int installed, sound_ok;
static const signed char zeros[4];

/* ---- Paula output ---- */

static void trigger(int c, const signed char *data, ULONG len,
		    const signed char *loop, ULONG loop_len)
{
	struct AudChannel *a = &custom.aud[c];
	paula_dmacon(1 << c);
	a->ac_ptr = (UWORD *)data;
	a->ac_len = (UWORD)(len >> 1);
	paula_dmacon(0x8200 | (1 << c));
	/* latched: what Paula plays after this block */
	if (loop && loop_len > 2)
	{
		a->ac_ptr = (UWORD *)loop;
		a->ac_len = (UWORD)(loop_len >> 1);
	}
	else
	{
		a->ac_ptr = (UWORD *)zeros;
		a->ac_len = 1;
	}
}

static int music_owns(int c)
{
	return !ch[c].sfx_ticks && (music_mask & (1 << c));
}

static void play_note(int c, int offset)
{
	struct chan *k = &ch[c];
	const struct sample *s = k->smp;
	if (!s || !s->data || !music_owns(c))
		return;
	if ((ULONG)offset >= s->len)
		offset = s->len - 2;
	trigger(c, s->data + offset, s->len - offset, s->loop, s->loop_len);
}

/* ---- periods ---- */

static int note_index(int period)
{
	int i;
	int best = 0, bd = 0x7fff;
	for (i = 0; i < 36; i++)
	{
		int d = base_period[i] - period;
		if (d < 0) d = -d;
		if (d < bd) { bd = d; best = i; }
	}
	return best;
}

static int tuned(int period, int ft)
{
	return ptab[ft & 15][note_index(period)];
}

static int arp_period(struct chan *k, int semis)
{
	const UWORD *t = ptab[k->finetune & 15];
	int i, best = 0, bd = 0x7fff;
	for (i = 0; i < 36; i++)
	{
		int d = t[i] - k->period;
		if (d < 0) d = -d;
		if (d < bd) { bd = d; best = i; }
	}
	i = best + semis;
	return t[i > 35 ? 35 : i];
}

static int clamp_period(int p)
{
	return p < 113 ? 113 : p > 856 ? 856 : p;
}

static void vol_slide(struct chan *k, int param)
{
	if (param >> 4)
		k->vol += param >> 4;
	else
		k->vol -= param & 15;
	k->vol = k->vol < 0 ? 0 : k->vol > 64 ? 64 : k->vol;
}

static void tone_porta(struct chan *k)
{
	if (!k->porta_target)
		return;
	if (k->period < k->porta_target)
	{
		k->period += k->porta_speed;
		if (k->period > k->porta_target)
			k->period = k->porta_target;
	}
	else if (k->period > k->porta_target)
	{
		k->period -= k->porta_speed;
		if (k->period < k->porta_target)
			k->period = k->porta_target;
	}
	k->out_period = k->period;
}

static int wave(int p)
{
	int v = sine[p & 31];
	return (p & 32) ? -v : v;
}

static void vibrato(struct chan *k)
{
	int depth = k->vib_cmd & 15, spd = k->vib_cmd >> 4;
	k->out_period = k->period + ((wave(k->vib_pos) * depth) >> 7);
	k->vib_pos = (k->vib_pos + spd) & 63;
}

/* ---- rows ---- */

static void row_start(void)
{
	int c;
	const UBYTE *p = patterns + (ULONG)mod[952 + pos] * 1024 + row * 16;

	for (c = 0; c < 4; c++, p += 4)
	{
		struct chan *k = &ch[c];
		int sn = (p[0] & 0xf0) | (p[2] >> 4);
		int per = ((p[0] & 0x0f) << 8) | p[1];
		int cmd = p[2] & 0x0f, prm = p[3];
		int ex = prm >> 4, ey = prm & 15;

		k->cmd = cmd;
		k->param = prm;
		k->delay_period = 0;
		if (sn && sn < 32)
		{
			k->smp = &smp[sn];
			k->vol = smp[sn].vol;
			k->finetune = smp[sn].finetune;
		}
		if (cmd == 0xe && ex == 5)
			k->finetune = ey;
		if (per)
		{
			int tp = tuned(per, k->finetune);
			if (cmd == 3 || cmd == 5)
			{
				k->porta_target = tp;
				if (cmd == 3 && prm)
					k->porta_speed = prm;
			}
			else if (cmd == 0xe && ex == 0xd && ey)
				k->delay_period = tp;
			else
			{
				int off = 0;
				k->period = tp;
				k->vib_pos = k->trem_pos = 0;
				if (cmd == 9)
				{
					if (prm)
						k->offset_mem = prm << 8;
					off = k->offset_mem;
				}
				play_note(c, off);
			}
		}
		k->out_period = k->period;

		switch (cmd)
		{
		case 3:
			break;
		case 4:
			if (ex) k->vib_cmd = (k->vib_cmd & 0x0f) | (ex << 4);
			if (ey) k->vib_cmd = (k->vib_cmd & 0xf0) | ey;
			break;
		case 7:
			if (ex) k->trem_cmd = (k->trem_cmd & 0x0f) | (ex << 4);
			if (ey) k->trem_cmd = (k->trem_cmd & 0xf0) | ey;
			break;
		case 0xb:
			jump_pos = prm;
			break_row = 0;
			break;
		case 0xc:
			k->vol = prm > 64 ? 64 : prm;
			break;
		case 0xd:
			if (jump_pos < 0)
				jump_pos = pos + 1;
			break_row = ex * 10 + ey;
			if (break_row > 63)
				break_row = 0;
			break;
		case 0xe:
			switch (ex)
			{
			case 1: k->period = clamp_period(k->period - ey); k->out_period = k->period; break;
			case 2: k->period = clamp_period(k->period + ey); k->out_period = k->period; break;
			case 6:
				if (!ey)
					k->loop_row = row;
				else if (!k->loop_cnt)
				{
					k->loop_cnt = ey;
					break_row = k->loop_row;
					jump_pos = pos;
				}
				else if (--k->loop_cnt)
				{
					break_row = k->loop_row;
					jump_pos = pos;
				}
				break;
			case 8: mt_E8Trigger = ey; break;
			case 0xa: k->vol = k->vol + ey > 64 ? 64 : k->vol + ey; break;
			case 0xb: k->vol = k->vol - ey < 0 ? 0 : k->vol - ey; break;
			case 0xc: if (!ey) k->vol = 0; break;
			case 0xe: if (!pdelay_cnt) pdelay = ey; break;
			}
			break;
		case 0xf:
			if (prm)
			{
				if (prm < 32)
					speed = prm;
				else
				{
					bpm = prm;
					paula_set_tick_rate(bpm * 2, 5);
				}
			}
			break;
		}
		k->out_vol = k->vol;
	}
}

static void row_tick(void)
{
	int c;
	for (c = 0; c < 4; c++)
	{
		struct chan *k = &ch[c];
		int ex = k->param >> 4, ey = k->param & 15;
		k->out_period = k->period;
		switch (k->cmd)
		{
		case 0:
			if (k->param)
			{
				int t = tick % 3;
				k->out_period = t == 0 ? k->period : arp_period(k, t == 1 ? ex : ey);
			}
			break;
		case 1: k->period = clamp_period(k->period - k->param); k->out_period = k->period; break;
		case 2: k->period = clamp_period(k->period + k->param); k->out_period = k->period; break;
		case 3: tone_porta(k); break;
		case 4: vibrato(k); break;
		case 5: tone_porta(k); vol_slide(k, k->param); break;
		case 6: vibrato(k); vol_slide(k, k->param); break;
		case 7:
		{
			int depth = k->trem_cmd & 15, spd = k->trem_cmd >> 4;
			int v = k->vol + ((wave(k->trem_pos) * depth) >> 6);
			k->out_vol = v < 0 ? 0 : v > 64 ? 64 : v;
			k->trem_pos = (k->trem_pos + spd) & 63;
			continue;
		}
		case 0xa: vol_slide(k, k->param); break;
		case 0xe:
			if (ex == 9 && ey && tick % ey == 0)
				play_note(c, 0);
			else if (ex == 0xc && tick == ey)
				k->vol = 0;
			else if (ex == 0xd && tick == ey && k->delay_period)
			{
				k->period = k->out_period = k->delay_period;
				k->delay_period = 0;
				play_note(c, 0);
			}
			break;
		}
		k->out_vol = k->vol;
	}
}

static void output(void)
{
	int c;
	for (c = 0; c < 4; c++)
	{
		if (!music_owns(c))
			continue;
		custom.aud[c].ac_per = (UWORD)clamp_period(ch[c].out_period ? ch[c].out_period : 428);
		custom.aud[c].ac_vol = (UWORD)((WORD)ch[c].out_vol * (WORD)master >> 6);
	}
}

void mt_music(void *custom_base)
{
	int c;
	(void)custom_base;
	for (c = 0; c < 4; c++)
		if (ch[c].sfx_ticks && !--ch[c].sfx_ticks)
			ch[c].sfx_pri = 0;
	if (!mod || !mt_Enable)
		return;

	if (tick == 0 && !pdelay_cnt)
		row_start();
	else
		row_tick();
	output();

	if (++tick >= speed)
	{
		tick = 0;
		if (pdelay)
		{
			pdelay_cnt = pdelay;
			pdelay = 0;
		}
		if (pdelay_cnt && --pdelay_cnt)
			return;			/* repeat the row's effect ticks */
		pdelay_cnt = 0;
		if (jump_pos >= 0)
		{
			pos = jump_pos;
			row = break_row;
			jump_pos = -1;
		}
		else if (++row >= 64)
		{
			row = 0;
			pos++;
		}
		if (pos >= song_len)
			pos = 0;
	}
}

static void vbl_tick(void)
{
	mt_music(0);
}

/* ---- API ---- */

void mt_install_cia(void *custom_base, void *autovec, UBYTE pal)
{
	int ft, n;
	(void)custom_base; (void)autovec; (void)pal;
	for (ft = 0; ft < 16; ft++)
	{
		int f = ft < 8 ? ft : ft - 16;
		for (n = 0; n < 36; n++)
		{
			/* period * 2^(-f/96), rounded: 1/8 semitone per step */
			long p = base_period[n];
			long num = 1000000, den = 1000000;
			static const long step[8] = {	/* 2^(k/96) * 1e6 */
				1000000, 1007246, 1014545, 1021897,
				1029302, 1036761, 1044274, 1051841,
			};
			if (f > 0) den = step[f];
			else if (f < 0) num = step[-f];
			ptab[ft][n] = (UWORD)((p * num + den / 2) / den);
		}
	}
	if (installed)
		return;
	sound_ok = paula_init(vbl_tick, 50) == 0;
	installed = 1;
}

void mt_remove_cia(void *custom_base)
{
	(void)custom_base;
	if (installed && sound_ok)
		paula_exit();
	installed = sound_ok = 0;
}

int mt_sound_ok(void)
{
	return sound_ok ? 0 : 1;
}

void mt_init(void *custom_base, APTR module, APTR samples, UBYTE songpos)
{
	int i;
	const UBYTE *m = (const UBYTE *)module;
	const signed char *sd;
	int npat = 0;
	UWORD sr = paula_lock();

	(void)custom_base;
	mod = m;
	song_len = m[950] ? m[950] : 1;
	for (i = 0; i < 128; i++)
		if (m[952 + i] + 1 > npat)
			npat = m[952 + i] + 1;
	patterns = m + 1084;
	sd = samples ? (const signed char *)samples
		     : (const signed char *)(patterns + (ULONG)npat * 1024);
	memset(smp, 0, sizeof(smp));
	for (i = 1; i < 32; i++)
	{
		const UBYTE *h = m + 20 + (i - 1) * 30;
		struct sample *s = &smp[i];
		ULONG len = ((ULONG)h[22] << 8 | h[23]) * 2;
		ULONG rs = ((ULONG)h[26] << 8 | h[27]) * 2;
		ULONG rl = ((ULONG)h[28] << 8 | h[29]) * 2;
		s->data = len ? sd : 0;
		s->len = len;
		s->finetune = h[24] & 15;
		s->vol = h[25] > 64 ? 64 : h[25];
		if (rs + rl > len)
			rl = rs < len ? len - rs : 0;
		s->loop = sd + rs;
		s->loop_len = rl;
		sd += len;
	}
	memset(ch, 0, sizeof(ch));
	pos = songpos < song_len ? songpos : 0;
	row = tick = 0;
	speed = 6;
	bpm = 125;
	jump_pos = -1;
	break_row = pdelay = pdelay_cnt = 0;
	paula_set_tick_rate(50, 1);
	paula_unlock(sr);
}

void mt_end(void *custom_base)
{
	int c;
	UWORD sr = paula_lock();
	(void)custom_base;
	mt_Enable = 0;
	for (c = 0; c < 4; c++)
		if (!ch[c].sfx_ticks)
		{
			paula_dmacon(1 << c);
			custom.aud[c].ac_vol = 0;
		}
	mod = 0;
	paula_unlock(sr);
}

void mt_musicmask(void *custom_base, UBYTE mask)
{
	(void)custom_base;
	music_mask = mask & 15;
}

void mt_mastervol(void *custom_base, UWORD vol)
{
	(void)custom_base;
	master = vol > 64 ? 64 : vol;
}

void mt_playfx(void *custom_base, SfxStructure *sfx)
{
	int c = sfx->sfx_cha, first, i;
	UWORD sr;
	(void)custom_base;

	if (!sound_ok || !sfx->sfx_ptr || sfx->sfx_len <= 0)
		return;
	sr = paula_lock();
	if (c < 0 || c > 3)
	{
		/* channels beyond those kept for music; with all four kept for
		 * music the last one is shared.  A free one first, otherwise
		 * the lowest priority one if ours is at least as high. */
		first = mt_MusicChannels < 4 ? mt_MusicChannels : 3;
		c = -1;
		for (i = 3; i >= first; i--)
			if (!ch[i].sfx_ticks)
			{
				c = i;
				break;
			}
		if (c < 0)
			for (i = 3; i >= first; i--)
				if (sfx->sfx_pri >= ch[i].sfx_pri && (c < 0 || ch[i].sfx_pri < ch[c].sfx_pri))
					c = i;
	}
	else if (ch[c].sfx_ticks && sfx->sfx_pri < ch[c].sfx_pri)
		c = -1;
	if (c >= 0)
	{
		ULONG bytes = (ULONG)sfx->sfx_len * 2;
		/* duration in 50 Hz ticks */
		long t = (long)(bytes * (ULONG)sfx->sfx_per / (PAULA_HZ / 50)) + 1;
		ch[c].sfx_ticks = 0;
		trigger(c, (const signed char *)sfx->sfx_ptr, bytes, 0, 0);
		custom.aud[c].ac_per = sfx->sfx_per;
		custom.aud[c].ac_vol = sfx->sfx_vol > 64 ? 64 : sfx->sfx_vol;
		ch[c].sfx_ticks = (int)t;
		ch[c].sfx_pri = sfx->sfx_pri;
	}
	paula_unlock(sr);
}

void mt_soundfx(void *custom_base, APTR sample, UWORD length, UWORD period, UWORD volume)
{
	SfxStructure s;
	s.sfx_ptr = sample;
	s.sfx_len = length;
	s.sfx_per = period;
	s.sfx_vol = volume;
	s.sfx_cha = -1;
	s.sfx_pri = 1;
	mt_playfx(custom_base, &s);
}
