// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

#include "sound.h"

#include <exec/memory.h>
#include <devices/audio.h>
#include <proto/exec.h>

/* Two samples:
 *   CLANG — long inharmonic metallic hit for goals
 *   PING  — short high-frequency blip for ball pickup
 * Both are 8-bit signed PCM in chip RAM. Each is played on the same
 * single-channel audio.device request; a new play() call aborts any
 * in-flight sample first. */
#define CLANG_LEN     8000    /* ~0.36s at 22kHz */
#define CLANG_PERIOD  162
#define PING_LEN      1400    /* ~65ms */
#define PING_PERIOD    98     /* higher sample rate → higher pitch */
#define VOLUME         64

static struct MsgPort *audio_port = NULL;
static struct IOAudio *audio_io   = NULL;
static BYTE *clang_data = NULL;
static BYTE *ping_data  = NULL;
static UBYTE which_channel[] = { 1, 2, 4, 8 };
static BOOL device_open = FALSE;
static BOOL playing     = FALSE;

/* 256-entry 8.8 sine table, shared by both synths. */
static WORD sin_table[256];
static const WORD sine_q1[65] = {
      0,   6,  13,  19,  25,  31,  38,  44,
     50,  56,  62,  68,  74,  80,  86,  92,
     98, 104, 109, 115, 121, 126, 132, 137,
    142, 147, 152, 157, 162, 167, 172, 177,
    181, 185, 190, 194, 198, 202, 206, 209,
    213, 216, 220, 223, 226, 229, 231, 234,
    237, 239, 241, 243, 245, 247, 248, 250,
    251, 252, 253, 254, 255, 255, 256, 256,
    256
};

static void build_sin_table(void)
{
    WORD i;
    for (i = 0; i <= 64; i++) {
        sin_table[i]         =  sine_q1[i];
        sin_table[128 - i]   =  sine_q1[i];
        sin_table[128 + i]   = -sine_q1[i];
        if (i > 0) sin_table[256 - i] = -sine_q1[i];
    }
}

static WORD noise_state = 12345;
static WORD noise_next(void)
{
    noise_state = (noise_state * 25173 + 13849) & 0x7FFF;
    return (noise_state & 0xFF) - 128;
}

/* CLANG: 6 inharmonic partials + noise attack, exp decay. */
static void synth_clang(BYTE *buf)
{
    LONG i;
    for (i = 0; i < CLANG_LEN; i++) {
        LONG t = ((LONG)i * 256) / CLANG_LEN;
        LONG env = 256 - t;
        LONG p0;
        LONG p1;
        LONG p2;
        LONG p3;
        LONG p4;
        LONG p5;
        LONG val;
        if (env < 0) env = 0;
        env = (env * env) >> 8;
        p0 = ((LONG)i *  19) / 16;
        p1 = ((LONG)i *  42) / 16;
        p2 = ((LONG)i *  82) / 16;
        p3 = ((LONG)i * 137) / 16;
        p4 = ((LONG)i * 218) / 16;
        p5 = ((LONG)i * 347) / 16;
        val = (sin_table[p0 & 0xFF] * 5) / 4;
        val +=  sin_table[p1 & 0xFF];
        val += (sin_table[p2 & 0xFF] * 3) / 4;
        val += (sin_table[p3 & 0xFF] * 3) / 4;
        val +=  sin_table[p4 & 0xFF] / 2;
        val +=  sin_table[p5 & 0xFF] / 3;
        if (i < 300) {
            LONG na = (300 - i) * 3;
            val += (noise_next() * na) >> 7;
        }
        val = (val * env) >> 8;
        if (val >  127) val =  127;
        if (val < -127) val = -127;
        buf[i] = (BYTE)val;
    }
}

/* PING: single high sine with sharp exponential decay. */
static void synth_ping(BYTE *buf)
{
    LONG i;
    for (i = 0; i < PING_LEN; i++) {
        LONG t = ((LONG)i * 256) / PING_LEN;
        LONG env = 256 - t;
        LONG p;
        LONG val;
        if (env < 0) env = 0;
        env = (env * env) >> 8;
        p = ((LONG)i * 24) / 8; /* higher fundamental */
        val = sin_table[p & 0xFF];
        val += sin_table[(p * 3) & 0xFF] / 4;   /* touch of overtone */
        val = (val * env) >> 8;
        if (val >  127) val =  127;
        if (val < -127) val = -127;
        buf[i] = (BYTE)val;
    }
}

#ifdef AMIGA3DO
/* 3DO port: no audio.device - play the same one-shot samples on Paula
 * channel 0 of the layer's emulation. After starting a sample the channel
 * is pointed at a silent word, so it stops after one pass (the usual
 * Amiga one-shot idiom). */
#include "paula.h"
#include <hardware/dmabits.h>
static UWORD silence[2];

BOOL sound_init(void)
{
    clang_data = (BYTE *)AllocMem(CLANG_LEN, MEMF_CHIP | MEMF_CLEAR);
    ping_data  = (BYTE *)AllocMem(PING_LEN,  MEMF_CHIP | MEMF_CLEAR);
    if (!clang_data || !ping_data) { sound_cleanup(); return FALSE; }
    build_sin_table();
    synth_clang(clang_data);
    synth_ping (ping_data);
    device_open = TRUE;
    return TRUE;
}

static void play(BYTE *data, LONG len, WORD period)
{
    if (!device_open || !data) return;
    paula_dmacon(DMAF_AUD0);
    custom.aud[0].ac_ptr = (UWORD *)data;
    custom.aud[0].ac_len = (UWORD)(len / 2);
    custom.aud[0].ac_per = (UWORD)period;
    custom.aud[0].ac_vol = VOLUME;
    paula_dmacon(DMAF_SETCLR | DMAF_AUD0);
    custom.aud[0].ac_ptr = silence;     /* reloaded when the sample ends */
    custom.aud[0].ac_len = 1;
    playing = TRUE;
}

void sound_play_clang(void) { play(clang_data, CLANG_LEN, CLANG_PERIOD); }
void sound_play_ping(void)  { play(ping_data,  PING_LEN,  PING_PERIOD);  }

void sound_cleanup(void)
{
    paula_dmacon(DMAF_AUD0);
    device_open = FALSE;
    if (clang_data) { FreeMem(clang_data, CLANG_LEN); clang_data = NULL; }
    if (ping_data)  { FreeMem(ping_data,  PING_LEN);  ping_data  = NULL; }
}
#else
BOOL sound_init(void)
{
    audio_port = CreateMsgPort();
    if (!audio_port) { sound_cleanup(); return FALSE; }
    audio_io = (struct IOAudio *)CreateIORequest(audio_port, sizeof(struct IOAudio));
    if (!audio_io) { sound_cleanup(); return FALSE; }

    clang_data = (BYTE *)AllocMem(CLANG_LEN, MEMF_CHIP | MEMF_CLEAR);
    ping_data  = (BYTE *)AllocMem(PING_LEN,  MEMF_CHIP | MEMF_CLEAR);
    if (!clang_data || !ping_data) { sound_cleanup(); return FALSE; }

    audio_io->ioa_Request.io_Message.mn_ReplyPort = audio_port;
    audio_io->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    audio_io->ioa_Request.io_Command = ADCMD_ALLOCATE;
    audio_io->ioa_Request.io_Flags   = ADIOF_NOWAIT;
    audio_io->ioa_AllocKey = 0;
    audio_io->ioa_Data     = which_channel;
    audio_io->ioa_Length   = sizeof(which_channel);
    if (OpenDevice(AUDIONAME, 0, (struct IORequest *)audio_io, 0) != 0) {
        sound_cleanup(); return FALSE;
    }
    device_open = TRUE;

    build_sin_table();
    synth_clang(clang_data);
    synth_ping (ping_data);
    return TRUE;
}

static void play(BYTE *data, LONG len, WORD period)
{
    if (!device_open || !data) return;
    if (playing) {
        AbortIO((struct IORequest *)audio_io);
        WaitIO ((struct IORequest *)audio_io);
    }
    audio_io->ioa_Request.io_Command = CMD_WRITE;
    audio_io->ioa_Request.io_Flags   = ADIOF_PERVOL;
    audio_io->ioa_Data   = (UBYTE *)data;
    audio_io->ioa_Length = len;
    audio_io->ioa_Period = period;
    audio_io->ioa_Volume = VOLUME;
    audio_io->ioa_Cycles = 1;
    BeginIO((struct IORequest *)audio_io);
    playing = TRUE;
}

void sound_play_clang(void) { play(clang_data, CLANG_LEN, CLANG_PERIOD); }
void sound_play_ping(void)  { play(ping_data,  PING_LEN,  PING_PERIOD);  }

void sound_cleanup(void)
{
    if (device_open) {
        if (playing) {
            AbortIO((struct IORequest *)audio_io);
            WaitIO ((struct IORequest *)audio_io);
        }
        CloseDevice((struct IORequest *)audio_io);
        device_open = FALSE;
    }
    if (clang_data) { FreeMem(clang_data, CLANG_LEN); clang_data = NULL; }
    if (ping_data)  { FreeMem(ping_data,  PING_LEN);  ping_data  = NULL; }
    if (audio_io)   { DeleteIORequest((struct IORequest *)audio_io); audio_io = NULL; }
    if (audio_port) { DeleteMsgPort(audio_port); audio_port = NULL; }
}
#endif
