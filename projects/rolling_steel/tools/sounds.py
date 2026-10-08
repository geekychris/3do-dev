#!/usr/bin/env python3
"""Rolling Steel's soundtrack and effects for the 3DO, from Music.cs and Sfx.cs.

    .venv/bin/python projects/rolling_steel/tools/sounds.py

Writes takeme/rolling_steel/theme0..6.raw (the seven themes: title and one per
course) and sfx.raw (the ten effects and the rolling loop), signed 8-bit at
11050 Hz. The Unity game synthesises these at startup; the 3DO can't spare
the time, so they ship rendered - the same step sequencer and voices, note
for note, rendered at 22100 Hz and filtered down.
"""
import math
import os
import struct

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), "takeme", "rolling_steel")
OUT_RATE = 11050
RATE = OUT_RATE * 2
STEPS, BARS = 16, 8
TOTAL = STEPS * BARS
MINOR = [0, 2, 3, 5, 7, 8, 10]
MAJOR = [0, 2, 4, 5, 7, 9, 11]
R16 = [-1] * 16


def tile(order, *bars):
    out = []
    for i in order:
        out += bars[i]
    return out


def part(steps, wave="pulse", transpose=0, gain=0.2, decay=0.18, fm=2.0, duty=0.5, lowpass=False):
    return dict(steps=steps, wave=wave, transpose=transpose, gain=gain, decay=decay, fm=fm, duty=duty, lowpass=lowpass)


def song(bpm, root, swing, parts, kick=None, snare=None, hat=None, scale=MINOR):
    return dict(bpm=bpm, root=root, swing=swing, parts=parts, kick=kick, snare=snare, hat=hat, scale=scale)


def songs():
    o8 = [0] * 8
    title = song(100, 57, 0.1, [
        part(tile([0, 1, 0, 2, 0, 1, 0, 2], [0, 4, 7, 4, 9, 7, 4, 7, 0, 4, 7, 4, 11, 9, 7, 4],
                  [3, 7, 10, 7, 12, 10, 7, 10, 3, 7, 10, 7, 14, 12, 10, 7], [2, 5, 9, 5, 11, 9, 5, 9, 2, 5, 9, 5, 12, 11, 9, 5]),
             "pulse", duty=0.25, gain=0.13, decay=0.22),
        part(tile([0, 1, 0, 2, 0, 1, 0, 2], [0, -1, -1, -1, -1, -1, -1, -1, 4, -1, -1, -1, -1, -1, -1, -1],
                  [3, -1, -1, -1, -1, -1, -1, -1, 5, -1, -1, -1, -1, -1, -1, -1], [5, -1, -1, -1, -1, -1, -1, -1, 2, -1, -1, -1, -1, -1, -1, -1]),
             "tri", gain=0.20, decay=1.7, transpose=-12),
        part(tile([0, 1, 0, 2, 0, 1, 0, 2], [0, -1, -1, -1, 0, -1, -1, -1, 4, -1, -1, -1, 4, -1, -1, -1],
                  [3, -1, -1, -1, 3, -1, -1, -1, 5, -1, -1, -1, 5, -1, -1, -1], [5, -1, -1, -1, 5, -1, -1, -1, 2, -1, -1, -1, 2, -1, -1, -1]),
             "saw", gain=0.24, decay=0.45, transpose=-24, lowpass=True),
        part(tile([3, 0, 3, 1, 3, 0, 2, 1], [7, -1, -1, -1, -1, -1, 9, -1, -1, -1, -1, -1, 7, -1, -1, -1],
                  [10, -1, -1, -1, 9, -1, -1, -1, 7, -1, -1, -1, -1, -1, -1, -1], [4, -1, -1, -1, 5, -1, -1, -1, 4, -1, 2, -1, 0, -1, -1, -1], R16),
             "fm", fm=1.8, gain=0.15, decay=0.8, transpose=12),
    ], kick=tile(o8, [1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0]),
        hat=tile(o8, [0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1]))
    practice = song(122, 57, 0.14, [
        part(tile([0, 0, 1, 0, 0, 0, 1, 2], [0, -1, -1, 0, -1, -1, 7, -1, 3, -1, -1, 3, -1, 5, -1, -1],
                  [5, -1, -1, 5, -1, -1, 12, -1, 4, -1, -1, 4, -1, 2, -1, 0], [3, -1, 3, -1, 5, -1, 5, -1, 7, -1, 7, -1, 9, -1, 11, -1]),
             "saw", gain=0.26, decay=0.16, transpose=-24, lowpass=True),
        part(tile([0, 1, 0, 1, 0, 1, 2, 1], [0, 2, 4, 7, 4, 2, 0, 2, 4, 7, 9, 7, 4, 2, 0, 2],
                  [3, 5, 7, 10, 7, 5, 3, 5, 7, 10, 12, 10, 7, 5, 3, 5], [4, 7, 9, 11, 9, 7, 4, 7, 9, 11, 14, 11, 9, 7, 4, 2]),
             "pulse", duty=0.3, gain=0.13, decay=0.09),
        part(tile([3, 3, 0, 1, 3, 0, 2, 1], [7, -1, -1, -1, 9, -1, -1, -1, 7, -1, 4, -1, 5, -1, -1, -1],
                  [4, -1, 2, -1, 0, -1, -1, -1, 2, -1, -1, -1, -1, -1, -1, -1], [9, -1, 7, -1, 9, -1, 11, -1, 12, -1, -1, -1, -1, -1, -1, -1], R16),
             "fm", fm=2.4, gain=0.16, decay=0.4, transpose=12),
    ], kick=tile([0, 0, 0, 1, 0, 0, 0, 1], [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0], [1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 0, 1]),
        snare=tile(o8, [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0]),
        hat=tile(o8, [1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 1]))
    beginner = song(134, 50, 0.08, [
        part(tile([0, 0, 1, 1, 0, 0, 2, 1], [0, -1, 0, -1, 0, -1, 7, -1, 0, -1, 0, -1, 3, -1, 5, -1],
                  [5, -1, 5, -1, 5, -1, 12, -1, 4, -1, 4, -1, 2, -1, 0, -1], [3, -1, 3, 3, -1, 3, 5, -1, 7, -1, 7, 7, -1, 9, 10, -1]),
             "saw", gain=0.27, decay=0.13, transpose=-24, lowpass=True),
        part(tile([0, 1, 0, 1, 2, 1, 0, 1], [7, 4, 2, 4, 7, 4, 2, 4, 9, 7, 4, 7, 9, 7, 4, 7],
                  [10, 7, 5, 7, 10, 7, 5, 7, 12, 10, 7, 10, 12, 10, 7, 5], [11, 9, 7, 9, 11, 9, 7, 9, 14, 12, 9, 12, 14, 12, 9, 7]),
             "pulse", duty=0.2, gain=0.12, decay=0.07),
        part(tile([3, 0, 3, 1, 0, 2, 1, 3], [12, -1, -1, 10, -1, -1, 9, -1, 7, -1, -1, -1, -1, -1, -1, -1],
                  [9, -1, 7, -1, 5, -1, 4, -1, 3, -1, -1, -1, -1, -1, -1, -1], [7, -1, 9, -1, 10, -1, 12, -1, 14, -1, -1, -1, 12, -1, -1, -1], R16),
             "fm", fm=3.1, gain=0.15, decay=0.33, transpose=12),
    ], kick=tile([0, 0, 0, 1, 0, 0, 0, 1], [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0], [1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0]),
        snare=tile(o8, [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0]),
        hat=tile(o8, [1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1]))
    intermediate = song(144, 54, 0.0, [
        part(tile([0, 0, 1, 0, 2, 0, 1, 2], [0, 0, -1, 0, -1, 0, -1, 7, 0, 0, -1, 0, -1, 3, -1, 5],
                  [5, 5, -1, 5, -1, 5, -1, 12, 4, 4, -1, 4, -1, 2, -1, 0], [7, 7, -1, 7, -1, 5, -1, 4, 3, 3, -1, 3, -1, 2, -1, 1]),
             "saw", gain=0.28, decay=0.11, transpose=-24, lowpass=True),
        part(tile([0, 1, 2, 1, 0, 1, 2, 0], [0, 3, 7, 10, 7, 3, 0, 3, 7, 10, 14, 10, 7, 3, 0, 3],
                  [2, 5, 9, 12, 9, 5, 2, 5, 9, 12, 16, 12, 9, 5, 2, 5], [4, 7, 11, 14, 11, 7, 4, 7, 11, 14, 18, 14, 11, 7, 4, 2]),
             "pulse", duty=0.15, gain=0.11, decay=0.055),
        part(tile([3, 0, 1, 3, 0, 2, 1, 3], [14, -1, 12, -1, 11, -1, 9, -1, 7, -1, -1, -1, -1, -1, -1, -1],
                  [7, -1, -1, 9, -1, -1, 11, -1, 12, -1, -1, -1, 14, -1, -1, -1], [12, -1, 11, -1, 9, -1, 7, -1, 5, -1, 4, -1, 3, -1, -1, -1], R16),
             "fm", fm=4.2, gain=0.15, decay=0.28, transpose=12),
    ], kick=tile([0, 0, 1, 0, 0, 0, 1, 1], [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0], [1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 1, 1]),
        snare=tile(o8, [0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1]),
        hat=tile(o8, [1] * 16))
    aerial = song(126, 52, 0.12, [
        part(tile([0, 1, 0, 2, 0, 1, 2, 2], [7, -1, 11, -1, 14, -1, 11, -1, 9, -1, 7, -1, 4, -1, 7, -1],
                  [9, -1, 12, -1, 16, -1, 12, -1, 11, -1, 9, -1, 7, -1, 9, -1], [4, -1, 7, -1, 11, -1, 7, -1, 6, -1, 4, -1, 2, -1, 4, -1]),
             "fm", fm=1.3, gain=0.15, decay=0.5, transpose=12),
        part(tile([0, 1, 0, 2, 0, 1, 2, 2], [0, -1, -1, -1, -1, -1, -1, -1, 4, -1, -1, -1, -1, -1, -1, -1],
                  [2, -1, -1, -1, -1, -1, -1, -1, 5, -1, -1, -1, -1, -1, -1, -1], [4, -1, -1, -1, -1, -1, -1, -1, 0, -1, -1, -1, -1, -1, -1, -1]),
             "tri", gain=0.19, decay=1.9, transpose=-12),
        part(tile([0, 1, 0, 2, 0, 1, 2, 2], [0, -1, -1, 0, -1, -1, -1, 4, -1, -1, 0, -1, -1, -1, 2, -1],
                  [2, -1, -1, 2, -1, -1, -1, 5, -1, -1, 2, -1, -1, -1, 4, -1], [4, -1, -1, 4, -1, -1, -1, 7, -1, -1, 4, -1, -1, -1, 0, -1]),
             "saw", gain=0.24, decay=0.32, transpose=-24, lowpass=True),
    ], kick=tile(o8, [1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0]),
        hat=tile(o8, [0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 1]))
    silly = song(152, 48, 0.2, [
        part(tile([0, 1, 0, 2, 0, 1, 2, 2], [0, -1, 0, -1, 4, -1, 2, -1, 7, -1, 4, -1, 2, -1, 4, -1],
                  [3, -1, 3, -1, 7, -1, 5, -1, 9, -1, 7, -1, 5, -1, 2, -1], [4, -1, 4, -1, 7, -1, 9, -1, 11, -1, 9, -1, 7, -1, 4, -1]),
             "saw", gain=0.26, decay=0.12, transpose=-24, lowpass=True),
        part(tile([0, 0, 1, 0, 2, 0, 1, 2], [7, 7, -1, 9, -1, 7, -1, 4, 7, 7, -1, 9, -1, 11, -1, 9],
                  [9, 9, -1, 11, -1, 9, -1, 7, 12, 12, -1, 11, -1, 9, -1, 7], [4, 4, -1, 2, -1, 4, -1, 7, 9, 9, -1, 7, -1, 4, -1, 2]),
             "pulse", duty=0.12, gain=0.12, decay=0.05, transpose=12),
        part(tile([3, 0, 3, 1, 0, 2, 1, 3], [4, -1, 7, -1, 9, -1, 7, -1, 11, -1, -1, -1, 9, -1, -1, -1],
                  [12, -1, 11, -1, 9, -1, 7, -1, 4, -1, -1, -1, -1, -1, -1, -1], [7, -1, 9, -1, 11, -1, 12, -1, 14, -1, 12, -1, 11, -1, 9, -1], R16),
             "fm", fm=2.2, gain=0.16, decay=0.3, transpose=12),
    ], kick=tile([0, 0, 0, 1, 0, 0, 0, 1], [1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0], [1, 0, 1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1]),
        snare=tile(o8, [0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0]),
        hat=tile(o8, [1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1]), scale=MAJOR)
    ultimate = song(160, 47, 0.0, [
        part(tile([0, 0, 1, 0, 2, 1, 0, 2], [0, 0, 0, -1, 0, 0, -1, 0, 0, 0, 0, -1, 3, -1, 5, -1],
                  [5, 5, 5, -1, 5, 5, -1, 5, 4, 4, 4, -1, 2, -1, 0, -1], [7, 7, 7, -1, 6, 6, -1, 5, 3, 3, 3, -1, 2, -1, 1, -1]),
             "saw", gain=0.29, decay=0.09, transpose=-12, lowpass=True),
        part(tile([0, 1, 2, 1, 0, 2, 1, 2], [0, 3, 7, 10, 7, 3, 0, 3, 7, 10, 14, 10, 7, 3, 0, 3],
                  [3, 7, 10, 14, 10, 7, 3, 7, 10, 14, 17, 14, 10, 7, 3, 7], [5, 9, 12, 16, 12, 9, 5, 9, 12, 16, 19, 16, 12, 9, 5, 2]),
             "pulse", duty=0.14, gain=0.11, decay=0.05, transpose=12),
        part(tile([3, 0, 1, 3, 2, 0, 1, 2], [14, -1, -1, 12, -1, -1, 11, -1, 9, -1, -1, -1, 7, -1, -1, -1],
                  [7, -1, 7, -1, 9, -1, 10, -1, 12, -1, -1, -1, -1, -1, -1, -1], [12, -1, 10, -1, 9, -1, 7, -1, 5, -1, 3, -1, 2, -1, 0, -1], R16),
             "fm", fm=4.6, gain=0.15, decay=0.22, transpose=12),
    ], kick=tile([0, 1, 0, 1, 0, 1, 1, 1], [1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0], [1, 0, 0, 1, 1, 0, 1, 0, 1, 0, 0, 1, 1, 0, 1, 1]),
        snare=tile(o8, [0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 1, 0, 1, 1]),
        hat=tile(o8, [1] * 16))
    return [title, practice, beginner, intermediate, aerial, silly, ultimate]


def i32(x):
    x &= 0xFFFFFFFF
    return x - (1 << 32) if x & 0x80000000 else x


def noise(i):
    """Music.Noise: C# int arithmetic, which wraps."""
    x = i32((i << 13) ^ i)
    v = i32(x * i32(i32(x * x) * 15731 + 789221) + 1376312589) & 0x7FFFFFFF
    return 1.0 - v / 1073741824.0


def midi(n): return 440.0 * 2 ** ((n - 69) / 12.0)


def degree_freq(s, d, transpose):
    sc = s["scale"]
    octv = math.floor(d / len(sc))
    return midi(s["root"] + transpose + sc[d - octv * len(sc)] + 12 * octv)


def render(s):
    step_dur = 60.0 / s["bpm"] / 4.0
    total = math.ceil(step_dur * TOTAL * RATE)
    buf = np.zeros(total)
    step_samples = round(step_dur * RATE)
    dt = 1.0 / RATE
    for p in s["parts"]:
        for step, deg in enumerate(p["steps"][:TOTAL]):
            if deg < 0:
                continue
            freq = degree_freq(s, deg, p["transpose"])
            off = round(step_samples * s["swing"]) if step & 1 else 0
            start = step * step_samples + off
            ln = min(round(p["decay"] * 2.2 * RATE), total - start)
            phase = mod = lp = 0.0
            for i in range(max(0, ln)):
                t = i * dt
                env = min(t / 0.004, 1.0) * math.exp(-t / p["decay"])
                if t > 0.004 and env < 0.0005:
                    break
                phase += freq * dt
                if phase > 1:
                    phase -= 1
                w = p["wave"]
                if w == "saw":
                    v = 2 * phase - 1
                elif w == "tri":
                    v = 4 * abs(phase - 0.5) - 1
                elif w == "fm":
                    mod += freq * 2 * dt
                    if mod > 1:
                        mod -= 1
                    v = math.sin(phase * 2 * math.pi + p["fm"] * env * math.sin(mod * 2 * math.pi))
                else:
                    v = 1.0 if phase < p["duty"] else -1.0
                if p["lowpass"]:
                    lp += (v - lp) * 0.22
                    v = lp
                buf[start + i] += v * env * p["gain"]
    for step in range(TOTAL):
        start = step * step_samples
        if s["kick"] and s["kick"][step]:
            ph = 0.0
            for i in range(min(round(0.26 * RATE), total - start)):
                t = i * dt
                f = 145 + (46 - 145) * min(1.0, t / 0.07)
                ph += f * dt
                buf[start + i] += math.sin(ph * 2 * math.pi) * math.exp(-t / 0.10) * 0.5
        if s["snare"] and s["snare"][step]:
            for i in range(min(round(0.18 * RATE), total - start)):
                t = i * dt
                buf[start + i] += (noise(start + i) * 0.30 + math.sin(t * 195 * 2 * math.pi) * 0.12) * math.exp(-t / 0.055)
        if s["hat"] and s["hat"][step]:
            for i in range(min(round(0.06 * RATE), total - start)):
                t = i * dt
                buf[start + i] += (noise(start + i) - noise(start + i - 1)) * math.exp(-t / 0.014) * 0.16
    peak = np.abs(buf).max()
    x = buf * (0.82 / peak if peak > 1e-4 else 1.0)
    x = x / (1 + 0.25 * np.abs(x))
    fade = min(1200, len(x) // 16)
    k = np.arange(fade) / fade
    x[:fade] = x[len(x) - fade:] * (1 - k) + x[:fade] * k
    return x


# ---- Sfx.cs ----

def sfx_noise(t):
    return noise(int(t * 44100))


def env(t, dur, a, r):
    if t < a:
        return t / a
    if t > dur - r:
        return max(0.0, (dur - t) / r)
    return 1.0


def make(dur, fn, loop=False):
    n = max(1, int(RATE * dur))
    d = np.array([max(-1.0, min(1.0, fn(i / RATE))) for i in range(n)])
    if loop:
        fade = min(n // 8, 2000)
        k = np.arange(fade) / fade
        d[:fade] = d[n - fade:] * (1 - k) + d[:fade] * k
    return d


def effects():
    sine = lambda t, f: math.sin(t * f * 2 * math.pi)
    out = []
    out.append(make(0.45, lambda t: env(t, 0.45, 0.01, 0.25) * (sine(t, 330) + sine(t, 440) + sine(t, 660)) / 3))
    notes = [523, 659, 784, 1047]
    out.append(make(0.95, lambda t: env(t, 0.95, 0.01, 0.4) * sine(t, notes[min(3, int(t / 0.16))])))
    out.append(make(0.7, lambda t: env(t, 0.7, 0.005, 0.35) * sine(t, 420 + (60 - 420) * t / 0.7) * 0.9))
    out.append(make(0.12, lambda t: env(t, 0.12, 0.002, 0.05) * (sfx_noise(t) * 0.5 + sine(t, 180) * 0.5)))
    out.append(make(0.16, lambda t: env(t, 0.16, 0.004, 0.08) * sine(t, 880)))
    st = {"p": 0.0}
    def fall(t):
        f = 950 + (70 - 950) * (t / 1.25) ** 0.55
        st["p"] += f / RATE
        return env(t, 1.25, 0.005, 0.5) * (math.sin(st["p"] * 2 * math.pi) * 0.7 + sfx_noise(t) * 0.14)
    out.append(make(1.25, fall))
    st2 = {"p": 0.0}
    def sizzle(t):
        f = 420 + (90 - 420) * t / 0.85
        st2["p"] += f / RATE
        fizz = (sfx_noise(t) - sfx_noise(t - 1.0 / 44100)) * 0.55
        return env(t, 0.85, 0.004, 0.4) * (fizz + math.sin(st2["p"] * 2 * math.pi) * 0.35)
    out.append(make(0.85, sizzle))
    st3 = {"p": 0.0}
    def chomp(t):
        f = 260 + (55 - 260) * (t / 0.5) ** 0.7
        st3["p"] += f / RATE
        return env(t, 0.5, 0.003, 0.2) * (math.sin(st3["p"] * 2 * math.pi) * 0.75 + sfx_noise(t) * 0.3)
    out.append(make(0.5, chomp))
    def shatter(t):
        v = sfx_noise(t) * math.exp(-t / 0.045) * 0.55
        for k in range(5):
            u = t - k * 0.032
            if u < 0:
                continue
            f = 1150 + k * 470
            v += math.sin(u * f * 2 * math.pi + 3.2 * math.sin(u * f * 1.7 * 2 * math.pi)) * math.exp(-u / 0.085) * 0.2
        return v
    out.append(make(0.6, shatter))
    st4 = {"p": 0.0}
    def thud(t):
        f = 150 + (38 - 150) * min(1.0, t / 0.09)
        st4["p"] += f / RATE
        return env(t, 0.4, 0.002, 0.25) * (math.sin(st4["p"] * 2 * math.pi) * 0.8 + sfx_noise(t) * 0.25)
    out.append(make(0.4, thud))
    out.append(make(1.0, lambda t: sfx_noise(t) * 0.35 + sine(t, 55) * 0.2, loop=True))   # roll
    return out


def down(x, wrap):
    taps = 31
    k = np.arange(taps) - taps // 2
    h = np.sinc(k * 0.45) * np.hamming(taps)
    h /= h.sum()
    if wrap:
        y = np.real(np.fft.ifft(np.fft.fft(x) * np.fft.fft(np.roll(np.pad(h, (0, len(x) - taps)), -(taps // 2)))))
    else:
        y = np.convolve(x, h, mode="same")
    return y[::2]


def s8(x, seed):
    r = np.random.default_rng(seed)
    d = r.random(len(x)) - r.random(len(x))
    return np.clip(np.round(x * 127 + d * 0.5), -127, 127).astype(np.int8)


def main():
    os.makedirs(OUT, exist_ok=True)
    for i, s in enumerate(songs()):
        b = s8(down(render(s), True), i)
        b = b[:len(b) & ~7]
        open(os.path.join(OUT, "theme%d.raw" % i), "wb").write(b.tobytes())
        print("theme%d: %d bytes, %.1f s" % (i, len(b), len(b) / OUT_RATE))
    blobs = []
    for i, x in enumerate(effects()):
        b = s8(down(x, i == 10) / max(1e-3, np.abs(x).max()) * 0.95, 100 + i)
        b = np.concatenate([b, np.zeros((-len(b)) % 4, dtype=np.int8)])
        blobs.append(b.tobytes())
    head = struct.pack(">I", len(blobs)) + b"".join(struct.pack(">I", len(b)) for b in blobs)
    open(os.path.join(OUT, "sfx.raw"), "wb").write(head + b"".join(blobs))
    print("sfx.raw: %d clips" % len(blobs))


if __name__ == "__main__":
    main()
