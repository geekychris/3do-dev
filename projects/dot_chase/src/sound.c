// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * DOT_CHASE - Sound
 * Garden sounds via direct Paula chip access
 *
 * Channel 0: seed pips (a pentatonic run, one note per seed)
 * Channel 1: SFX (catch a spider, lose a life, bonus, extra life)
 * Channel 2: cricket chirps that quicken as the garden empties
 * Channel 3: bubbling arpeggio while the spiders are scared
 */
#include <exec/types.h>
#include <exec/memory.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include "paula.h"
#include "sound.h"

/* 3DO port: registers live in the layer's Paula emulation (paula.h) */
#define CUSTOM (&custom)

#define WAVE_LEN     32
#define NOISE_LEN    128

/* Waveform buffers in chip RAM */
static BYTE *wave_square;
static BYTE *wave_sine;
static BYTE *wave_noise;
static BYTE *wave_tri;

/* Channel timers (frames remaining) */
static WORD ch_timer[4];

/* Seed pips walk up and down a pentatonic scale */
static const UWORD pip_notes[] = { 339, 285, 254, 214, 190, 214, 254, 285 };
#define PIP_LEN 8
static WORD pip_pos;

/* Cricket chirps */
static WORD pulse_on;
static WORD pulse_speed_level;
static WORD pulse_timer;

/* Scared-spider arpeggio */
static WORD power_on;
static WORD power_phase;

/* Simple note sequencer on channel 1: 0 = rest, 1 = noise burst */
static const UWORD *seq_notes;
static WORD seq_len, seq_pos, seq_timer, seq_speed;
static BYTE *seq_wave;

static const UWORD lose_notes[] = { 1, 1, 0, 381, 0, 453, 0, 508, 508, 0, 678, 678, 678 };
static const UWORD catch_notes[] = { 1, 226, 190, 151, 113 };
static const UWORD bonus_notes[] = { 285, 214, 170 };
static const UWORD extra_notes[] = { 254, 0, 254, 190, 151, 127 };

static void play(int ch, BYTE *wave, UWORD words, UWORD period, UWORD vol, WORD frames)
{
    paula_dmacon((UWORD)(DMAF_AUD0 << ch));
    CUSTOM->aud[ch].ac_ptr = (UWORD *)wave;
    CUSTOM->aud[ch].ac_len = words;
    CUSTOM->aud[ch].ac_per = period;
    CUSTOM->aud[ch].ac_vol = vol;
    paula_dmacon((UWORD)(DMAF_SETCLR | (DMAF_AUD0 << ch)));
    ch_timer[ch] = frames;
}

static void start_seq(const UWORD *notes, WORD len, WORD speed, BYTE *wave)
{
    seq_notes = notes;
    seq_len = len;
    seq_pos = 0;
    seq_timer = speed;   /* first note on the next update */
    seq_speed = speed;
    seq_wave = wave;
}

int sound_init(void)
{
    int i;

    wave_square = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_sine   = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_noise  = (BYTE *)AllocMem(NOISE_LEN, MEMF_CHIP | MEMF_CLEAR);
    wave_tri    = (BYTE *)AllocMem(WAVE_LEN, MEMF_CHIP | MEMF_CLEAR);

    if (!wave_square || !wave_sine || !wave_noise || !wave_tri) {
        sound_cleanup();
        return -1;
    }

    for (i = 0; i < WAVE_LEN / 2; i++) wave_square[i] = 90;
    for (i = WAVE_LEN / 2; i < WAVE_LEN; i++) wave_square[i] = -90;

    {
        static const BYTE sine_tab[32] = {
            0, 25, 49, 71, 90, 106, 117, 125,
            127, 125, 117, 106, 90, 71, 49, 25,
            0, -25, -49, -71, -90, -106, -117, -125,
            -127, -125, -117, -106, -90, -71, -49, -25
        };
        for (i = 0; i < 32; i++) wave_sine[i] = sine_tab[i];
    }

    {
        ULONG seed = 4243;
        for (i = 0; i < NOISE_LEN; i++) {
            seed = seed * 1103515245 + 12345;
            wave_noise[i] = (BYTE)((seed >> 16) & 0xFF);
        }
    }

    for (i = 0; i < 16; i++) wave_tri[i] = (BYTE)(-120 + i * 16);
    for (i = 16; i < WAVE_LEN; i++) wave_tri[i] = (BYTE)(120 - (i - 16) * 16);

    paula_dmacon(DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3);

    for (i = 0; i < 4; i++) ch_timer[i] = 0;
    pip_pos = 0;
    pulse_on = 0;
    pulse_speed_level = 0;
    pulse_timer = 0;
    power_on = 0;
    power_phase = 0;
    seq_notes = NULL;

    return 0;
}

void sound_cleanup(void)
{
    int ch;

    paula_dmacon(DMAF_AUD0 | DMAF_AUD1 | DMAF_AUD2 | DMAF_AUD3);

    for (ch = 0; ch < 4; ch++) {
        CUSTOM->aud[ch].ac_vol = 0;
        CUSTOM->aud[ch].ac_per = 1;
        CUSTOM->aud[ch].ac_len = 1;
    }

    if (wave_square) { FreeMem(wave_square, WAVE_LEN); wave_square = NULL; }
    if (wave_sine)   { FreeMem(wave_sine, WAVE_LEN);   wave_sine = NULL; }
    if (wave_noise)  { FreeMem(wave_noise, NOISE_LEN); wave_noise = NULL; }
    if (wave_tri)    { FreeMem(wave_tri, WAVE_LEN);    wave_tri = NULL; }
}

void sound_seed(void)
{
    play(0, wave_tri, WAVE_LEN / 2, pip_notes[pip_pos], 30, 2);
    pip_pos = (pip_pos + 1) % PIP_LEN;
}

void sound_eat_spider(void)
{
    start_seq(catch_notes, 5, 3, wave_square);
}

void sound_die(void)
{
    start_seq(lose_notes, 13, 5, wave_square);
    pulse_on = 0;
    power_on = 0;
    paula_dmacon(DMAF_AUD2 | DMAF_AUD3);
}

void sound_bonus(void)
{
    start_seq(bonus_notes, 3, 4, wave_sine);
}

void sound_extra_life(void)
{
    start_seq(extra_notes, 6, 5, wave_sine);
}

void sound_power(void)
{
    power_on = 1;
    power_phase = 0;
}

void sound_power_off(void)
{
    if (!power_on) return;
    power_on = 0;
    paula_dmacon(DMAF_AUD3);
}

void sound_set_pulse(int speed)
{
    pulse_on = 1;
    pulse_speed_level = speed;
}

void sound_pulse_off(void)
{
    pulse_on = 0;
    paula_dmacon(DMAF_AUD2);
}

void sound_update(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (ch_timer[i] > 0) {
            ch_timer[i]--;
            if (ch_timer[i] == 0) {
                paula_dmacon((UWORD)(DMAF_AUD0 << i));
            }
        }
    }

    /* Channel 1 sequences */
    if (seq_notes) {
        seq_timer++;
        if (seq_timer >= seq_speed) {
            UWORD n = seq_notes[seq_pos];
            seq_timer = 0;
            if (n == 1)
                play(1, wave_noise, NOISE_LEN / 2, 300, 40, seq_speed - 1);
            else if (n)
                play(1, seq_wave, WAVE_LEN / 2, n, 42, seq_speed - 1);
            else
                paula_dmacon(DMAF_AUD1);
            seq_pos++;
            if (seq_pos >= seq_len) seq_notes = NULL;
        }
    }

    /* Crickets: three quick chirps, then a pause that shortens as
     * the seeds run out. Quiet while the arpeggio plays. */
    if (pulse_on && !power_on) {
        WORD gap = pulse_speed_level == 0 ? 48 : pulse_speed_level == 1 ? 34 : 22;
        pulse_timer++;
        if (pulse_timer == 1 || pulse_timer == 4 || pulse_timer == 7)
            play(2, wave_sine, WAVE_LEN / 2, 120, 14, 2);
        if (pulse_timer >= gap) pulse_timer = 0;
    }

    /* Scared spiders: a bubbling four-note arpeggio */
    if (power_on) {
        static const UWORD arp[4] = { 254, 214, 170, 214 };
        power_phase++;
        if ((power_phase % 3) == 1)
            play(3, wave_tri, WAVE_LEN / 2, arp[(power_phase / 3) & 3], 20, 0);
    }
}
