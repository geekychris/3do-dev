#!/usr/bin/env python3
"""Spectral Keep assets for the 3DO, from the Unity project's sources.

    .venv/bin/python projects/spectral_keep/tools/assets.py <spectral-keep checkout>

Writes
  src/textures.c                 the seven surface textures, 32x32 RGB, with the
                                 normal map's relief baked in (lit from the sun)
  takeme/spectral_keep/*.raw     the four music loops and the sound effects,
                                 signed 8-bit at 11050 Hz (Paula period 321)

The music is Music.cs ported line for line: the same songs, the same seeded
composer (with .NET's System.Random, so the melodies are the Unity ones) and
the same synthesiser, rendered at 22100 Hz and filtered down to 11050 Hz.
The 3DO can't synthesise 30-40 s loops at startup, so they ship rendered.
"""
import math
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT_RATE = 11050
RATE = OUT_RATE * 2


# ---- .NET System.Random (the seeded, Knuth subtractive generator) ----

class NetRandom:
    MBIG = 2147483647

    def __init__(self, seed):
        sa = [0] * 56
        sub = 2147483647 if seed == -2147483648 else abs(seed)
        mj = 161803398 - sub
        sa[55] = mj
        mk = 1
        for i in range(1, 55):
            ii = (21 * i) % 55
            sa[ii] = mk
            mk = mj - mk
            if mk < 0:
                mk += self.MBIG
            mj = sa[ii]
        for _ in range(4):
            for i in range(1, 56):
                sa[i] -= sa[1 + (i + 30) % 55]
                if sa[i] < 0:
                    sa[i] += self.MBIG
        self.sa, self.inext, self.inextp = sa, 0, 21

    def _sample(self):
        a = self.inext + 1
        if a >= 56:
            a = 1
        b = self.inextp + 1
        if b >= 56:
            b = 1
        r = self.sa[a] - self.sa[b]
        if r == self.MBIG:
            r -= 1
        if r < 0:
            r += self.MBIG
        self.sa[a] = r
        self.inext, self.inextp = a, b
        return r * (1.0 / self.MBIG)

    def next_double(self):
        return self._sample()

    def next(self, lo, hi=None):
        if hi is None:
            lo, hi = 0, lo
        return int(self._sample() * (hi - lo)) + lo


# ---- songs (Music.cs) ----

MINOR = [0, 2, 3, 5, 7, 8, 10]
DORIAN = [0, 2, 3, 5, 7, 9, 10]
HARMONIC = [0, 2, 3, 5, 7, 8, 11]


def song(name, bpm, root, seed, scale, bars, kick, snare, hat, bass, arp=None, pad=1.0,
         arp_level=1.0, lead=1.0, drums=1.0, bass_level=1.0, lead_octave=1):
    return dict(name=name, bpm=bpm, root=root, seed=seed, scale=scale, bars=bars, kick=kick,
                snare=snare, hat=hat, bass=bass, arp=arp or [0, 1, 2, 3, 2, 1, 0, 1], pad=pad,
                arp_level=arp_level, lead=lead, drums=drums, bass_level=bass_level,
                lead_octave=lead_octave)


SONGS = {
    "title": song("SPECTRAL KEEP", 108, 57, 11, MINOR, [0, 5, 2, 6, 0, 5, 2, 6, 3, 5, 0, 6, 3, 5, 6, 6],
                  "x.....x.x.......", "....x.......x...", "..x...x...x...x.", "x..x..x.x..x..x.", pad=1.1),
    "gatehouse": song("THE GATEHOUSE", 124, 50, 23, DORIAN, [0, 3, 0, 6, 0, 3, 6, 3, 2, 3, 0, 6, 2, 3, 4, 4],
                      "x...x...x...x...", "....x.......x...", ".x.x.x.x.x.x.x.x", "x.xx..x.x.xx..x.",
                      arp=[0, 2, 1, 3, 0, 2, 1, 2], pad=0.7),
    "crypt": song("THE CRYPT", 92, 52, 37, HARMONIC, [0, 5, 3, 4, 0, 5, 1, 4, 0, 3, 5, 4, 5, 3, 1, 4],
                  "x.......x..x....", "........x.......", "x...x...x...x...", "x.......x.x.....",
                  arp=[0, 1, 2, 1], pad=1.5, arp_level=0.6, drums=0.7, lead_octave=1),
    "tower": song("THE TOWER", 132, 48, 41, MINOR, [0, 5, 2, 6, 0, 5, 2, 6, 5, 6, 0, 0, 5, 6, 4, 4],
                  "x..x..x.x..x..x.", "....x.......x..x", "xxxxxxxxxxxxxxxx", "xx.xx.xxxx.xx.x.",
                  arp=[0, 1, 2, 3, 1, 2, 3, 2], pad=0.8, drums=1.1),
}


def note(s, degree):
    return s["root"] + 12 * (degree // 7) + s["scale"][degree % 7]


RHYTHMS = ["x..x..x.x...x...", "x.x.x...x..x....", "x...x.x.x.x.x...",
           "x..x..x...x.x...", "x.....x.x.x.x...", "x..x.x..x...x..."]


def motif(rng):
    notes = []
    deg = rng.next(0, 5)
    for bar in range(2):
        r = RHYTHMS[rng.next(len(RHYTHMS))]
        on = [i for i in range(16) if r[i] == "x"]
        for k in range(len(on)):
            nxt = on[k + 1] if k + 1 < len(on) else 16
            ln = min(nxt - on[k], 6)
            deg = max(-1, min(8, deg + rng.next(-2, 3)))
            notes.append((bar * 16 + on[k], ln * 0.92, deg))
    return notes


def nearest_chord_tone(want, chord_deg):
    best, bd = want, 99
    for o in range(-14, 22):
        d = chord_deg + o
        if (d - chord_deg) % 7 not in (0, 2, 4):
            continue
        if abs(d - want) < bd:
            bd, best = abs(d - want), d
    return best


def compose(s):
    ev = []
    rng = NetRandom(s["seed"])
    bars = len(s["bars"])
    for b in range(bars):
        deg = s["bars"][b]
        chord = [deg, deg + 2, deg + 4, deg + 7]
        for i in range(16):
            step = b * 16 + i
            if s["kick"][i] == "x":
                ev.append(("kick", step, 2, 0))
            if s["snare"][i] == "x":
                ev.append(("snare", step, 2, 0))
            if s["hat"][i] == "x":
                ev.append(("hat", step, 1, 0))
            if s["bass"][i] == "x":
                ln = 1
                while i + ln < 16 and s["bass"][i + ln] != "x" and ln < 4:
                    ln += 1
                octave = (i // 4) % 2 == 1 and rng.next_double() < 0.3
                ev.append(("bass", step, ln * 0.9, note(s, deg) - 12 + (12 if octave else 0)))
            if i % 2 == 0 or s["bpm"] < 110:
                a = s["arp"][(i // (1 if s["bpm"] < 110 else 2)) % len(s["arp"])]
                ev.append(("arp", step, 0.9, note(s, chord[a]) + 12))
        for c in (deg, deg + 2, deg + 4):
            ev.append(("pad", b * 16, 16, note(s, c) + 12))
    ma, mb = motif(rng), motif(rng)
    for section in range(bars // 4):
        m = mb if section == 2 else ma
        lift = 2 if section == 2 else 0
        for half in range(2):
            vary = half == 1 and section != 2
            for pos, ln, contour in m:
                step = section * 64 + half * 32 + pos
                deg = s["bars"][step // 16]
                want = contour + lift + ((pos % 3) - 1 if vary and pos >= 24 else 0) + s["lead_octave"] * 7
                if pos % 4 == 0:
                    want = nearest_chord_tone(want, deg)
                ev.append(("lead", step, ln, note(s, want) + 12))
    return ev


def hz(midi):
    return 440.0 * 2.0 ** ((midi - 69) / 12.0)


def add(buf, start, seconds, gen):
    n = len(buf)
    ln = int(seconds * RATE)
    vals = gen(ln)
    idx = (start + np.arange(ln)) % n
    np.add.at(buf, idx, vals)


def render_song(s):
    step_len = 60.0 / s["bpm"] / 4.0 * RATE
    n = int(round(len(s["bars"]) * 16 * step_len))
    events = compose(s)
    rng = NetRandom(s["seed"] * 7 + 1)
    mix = np.zeros(n)
    lead = np.zeros(n)
    step = step_len / RATE
    tau = 2.0 * math.pi
    for voice, st, steps, midi in events:
        at = int(round(st * step_len))
        dur = steps * step

        if voice == "kick":
            amp = 0.9 * s["drums"]

            def g(ln, amp=amp):
                out = np.empty(ln)
                ph = 0.0
                for i in range(ln):
                    t = i / RATE
                    ph += (45.0 + 110.0 * math.exp(-t * 28.0)) / RATE
                    out[i] = math.sin(ph * tau) * amp * math.exp(-t * 7.0)
                return out
            add(mix, at, 0.35, g)
        elif voice == "snare":
            amp = 0.38 * s["drums"]

            def g(ln, amp=amp):
                out = np.empty(ln)
                prev = 0.0
                for i in range(ln):
                    t = i / RATE
                    w = rng.next_double() * 2.0 - 1.0
                    hp = w - prev
                    prev = w
                    out[i] = (hp * 0.6 * math.exp(-t * 16.0) +
                              math.sin(t * 185.0 * tau) * 0.5 * math.exp(-t * 30.0)) * amp
                return out
            add(mix, at, 0.25, g)
        elif voice == "hat":
            amp = 0.11 * s["drums"]

            def g(ln, amp=amp):
                out = np.empty(ln)
                prev = 0.0
                for i in range(ln):
                    t = i / RATE
                    w = rng.next_double() * 2.0 - 1.0
                    hp = w - prev
                    prev = w
                    out[i] = hp * amp * math.exp(-t * 55.0)
                return out
            add(mix, at, 0.07, g)
        elif voice == "bass":
            f, amp = hz(midi), 0.32 * s["bass_level"]

            def g(ln, f=f, amp=amp, dur=dur):
                out = np.empty(ln)
                ph = y = 0.0
                for i in range(ln):
                    t = i / RATE
                    ph = (ph + f / RATE) % 1.0
                    saw = ph * 2.0 - 1.0
                    cut = 220.0 + 900.0 * math.exp(-t * 9.0)
                    y += (1.0 - math.exp(-tau * cut / RATE)) * (saw - y)
                    env = min(1.0, t / 0.005) * (1.0 if t < dur else math.exp(-(t - dur) * 40.0))
                    out[i] = y * amp * env
                return out
            add(mix, at, dur + 0.08, g)
        elif voice == "arp":
            f, amp = hz(midi), 0.075 * s["arp_level"]

            def g(ln, f=f, amp=amp):
                out = np.empty(ln)
                ph = y = 0.0
                for i in range(ln):
                    t = i / RATE
                    ph = (ph + f / RATE) % 1.0
                    pulse = 1.0 if ph < 0.25 else -0.33
                    y += 0.35 * (pulse - y)
                    out[i] = y * amp * math.exp(-t * 14.0) * min(1.0, t / 0.003)
                return out
            add(mix, at, dur + 0.05, g)
        elif voice == "pad":
            f, amp = hz(midi), 0.05 * s["pad"]
            a = 1.0 - math.exp(-tau * 1100.0 / RATE)

            def g(ln, f=f, amp=amp, a=a, dur=dur):
                out = np.empty(ln)
                p1, p2, y1, y2 = 0.0, 0.37, 0.0, 0.0
                for i in range(ln):
                    t = i / RATE
                    p1 = (p1 + f * 1.004 / RATE) % 1.0
                    p2 = (p2 + f * 0.996 / RATE) % 1.0
                    x = (p1 * 2.0 - 1.0) + (p2 * 2.0 - 1.0)
                    y1 += a * (x - y1)
                    y2 += a * (y1 - y2)
                    env = min(1.0, t / 0.45) * (1.0 if t < dur else math.exp(-(t - dur) * 6.0))
                    out[i] = y2 * amp * env
                return out
            add(mix, at, dur + 0.6, g)
        elif voice == "lead":
            f, amp = hz(midi), 0.16 * s["lead"]

            def g(ln, f=f, amp=amp, dur=dur):
                out = np.empty(ln)
                ph = y = 0.0
                for i in range(ln):
                    t = i / RATE
                    vib = 1.0 + 0.006 * math.sin(t * 5.5 * tau) if t > 0.15 else 1.0
                    ph = (ph + f * vib / RATE) % 1.0
                    x = (1.0 if ph < 0.5 else -1.0) * 0.6 + (ph * 2.0 - 1.0) * 0.4
                    y += 0.22 * (x - y)
                    env = min(1.0, t / 0.01) * ((1.0 - t / dur * 0.25) if t < dur
                                                else math.exp(-(t - dur) * 30.0) * 0.75)
                    out[i] = y * amp * env
                return out
            add(lead, at, dur + 0.12, g)
    # lead echo (dotted eighth), wrapping like everything else
    delay = int(round(3 * step_len))
    wet = [0.0] * n
    ld = lead.tolist()
    for _ in range(2):
        for i in range(n):
            wet[(i + delay) % n] = (ld[i] + wet[i]) * 0.35
    mix += lead + np.array(wet)
    mix = np.tanh(mix * 1.15)
    mix *= 0.85 / max(1e-6, np.abs(mix).max())
    return mix


# ---- sound effects (Beeper.cs, modern style) ----

def sfx_list():
    lerp = lambda a, b, t: a + (b - a) * min(1.0, max(0.0, t))
    relic = [523, 659, 784, 1047, 784, 1047, 1319]
    done = [392, 523, 659, 784, 659, 784, 1047, 0, 1047, 1047, 1047]
    start = [523, 0, 523, 659, 784]
    return [
        ("jump", 0.12, lambda t: lerp(300, 900, t / 0.12), 0),
        ("land", 0.03, lambda t: 120, 0),
        ("pickup", 0.18, lambda t: 1200 if t < 0.06 else 1600 if t < 0.12 else 2000, 0),
        ("relic", 0.5, lambda t: relic[min(6, int(t / 0.07))], 0),
        ("key", 0.25, lambda t: 1800 if t < 0.08 else 900 if t < 0.16 else 1800, 0),
        ("gate", 0.4, lambda t: lerp(90, 220, t / 0.4) * (1.0 if (t % 0.05) < 0.025 else 1.5), 0),
        ("death", 0.9, lambda t: lerp(1400, 60, math.sqrt(t / 0.9)), 0.35),
        ("door", 0.06, lambda t: lerp(200, 500, t / 0.06), 0),
        ("talk", 0.2, lambda t: 700 + 300 * (1 if math.sin(t * 60) >= 0 else -1), 0),
        ("bark", 0.16, lambda t: lerp(500, 180, t / 0.16), 0.4),
        ("done", 1.6, lambda t: done[min(10, int(t / 0.14))], 0),
        ("start", 0.7, lambda t: start[min(4, int(t / 0.14))], 0),
        ("blip", 0.03, lambda t: 1500, 0),
    ]


def render_sfx(dur, freq, noise_mix, noise):
    n = max(1, int(RATE * dur))
    out = np.empty(n)
    phase = 0.0
    for i in range(n):
        t = i / RATE
        f = freq(t)
        phase += f / RATE
        ph = phase % 1.0
        tri = 1.0 - 4.0 * abs(ph - 0.5)
        v = 0.0 if f <= 0 else tri * 0.75 + math.sin(ph * 4.0 * math.pi) * 0.25
        if noise_mix > 0:
            v = v + ((noise.next_double() * 2.0 - 1.0) - v) * (noise_mix * 0.5)
        attack = min(1.0, t / 0.004)
        decay = math.exp(-t * 3.5 / max(0.05, dur))
        v *= attack * decay * min(1.0, (dur - t) / 0.02)
        out[i] = v * 0.6
    return out


# ---- output ----

def lowpass_half(x, wrap):
    """22100 -> 11050 Hz: windowed-sinc low-pass, then every other sample."""
    taps = 31
    k = np.arange(taps) - taps // 2
    h = np.sinc(k * 0.45) * np.hamming(taps)
    h /= h.sum()
    if wrap:
        y = np.real(np.fft.ifft(np.fft.fft(x) * np.fft.fft(np.roll(np.pad(h, (0, len(x) - taps)), -(taps // 2)))))
    else:
        y = np.convolve(x, h, mode="same")
    return y[::2]


def to_s8(x, seed):
    r = np.random.default_rng(seed)
    d = (r.random(len(x)) - r.random(len(x)))          # TPDF dither, +-1 LSB
    return np.clip(np.round(x * 127.0 + d * 0.5), -127, 127).astype(np.int8)


def textures(src):
    names = ["Block", "Bricks", "Cobble", "Crate", "Planks", "Rock", "Slab"]
    light = np.array([-0.4, 0.55, 0.73])                # tangent space: from the upper left, towards the viewer
    light /= np.linalg.norm(light)
    rows = ["/* generated by tools/assets.py from the Unity project's Resources/Tex - do not edit */",
            "/* 32x32 RGB each: Block Bricks Cobble Crate Planks Rock Slab; relief from the normal maps */",
            "const unsigned char keep_tex[7][32 * 32 * 3] = {"]
    for nm in names:
        alb = np.asarray(Image.open(os.path.join(src, "Assets/Resources/Tex", nm + ".png")).convert("RGB")
                         .resize((32, 32), Image.BOX), dtype=float) / 255.0
        nrm = np.asarray(Image.open(os.path.join(src, "Assets/Resources/Tex", nm + "_N.png")).convert("RGB")
                         .resize((32, 32), Image.BOX), dtype=float) / 255.0 * 2.0 - 1.0
        nrm /= np.maximum(1e-6, np.linalg.norm(nrm, axis=2, keepdims=True))
        flat = light[2]
        relief = np.clip((nrm @ light) / flat, 0.55, 1.35)
        rgb = np.clip(alb * relief[..., None], 0, 1)
        vals = (rgb * 255 + 0.5).astype(int).reshape(-1)
        rows.append("    {" + ",".join(str(v) for v in vals) + "},")
    rows.append("};")
    open(os.path.join(ROOT, "src", "textures.c"), "w").write("\n".join(rows) + "\n")
    print("wrote src/textures.c")


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    src = sys.argv[1]
    textures(src)
    out = os.path.join(ROOT, "takeme", "spectral_keep")
    os.makedirs(out, exist_ok=True)
    only = sys.argv[2:]
    for name, s in SONGS.items():
        if only and name not in only:
            continue
        x = lowpass_half(render_song(s), wrap=True)
        b = to_s8(x, s["seed"])
        b = b[:len(b) & ~3]
        open(os.path.join(out, name + ".raw"), "wb").write(b.tobytes())
        print(f"wrote {name}.raw: {len(b)} bytes, {len(b) / OUT_RATE:.1f} s")
    noise = NetRandom(48)
    blobs = []
    for name, dur, freq, nm in sfx_list():
        x = lowpass_half(render_sfx(dur, freq, nm, noise), wrap=False)
        b = to_s8(x / 0.6, 7)                         # full scale; the game sets the level
        b = np.concatenate([b, np.zeros((-len(b)) % 4, dtype=np.int8)])
        blobs.append(b.tobytes())
    # sfx.raw: count, then each clip's length (big-endian), then the clips (4-byte aligned)
    head = struct.pack(">I", len(blobs)) + b"".join(struct.pack(">I", len(b)) for b in blobs)
    open(os.path.join(out, "sfx.raw"), "wb").write(head + b"".join(blobs))
    print(f"wrote sfx.raw: {len(blobs)} clips")


if __name__ == "__main__":
    main()
