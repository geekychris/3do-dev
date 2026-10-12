#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""RJ Birthday: the arcade cabinet's four voice clips.

Spoken by Kokoro TTS (Kokoro-82M, Apache-2.0) through the local
kokoro_runtime server, then given a robot arcade treatment (pitched down,
ring-modulated, bit-crushed) and written as the game wants them: signed
8-bit mono raw at 10026 Hz, snd_arcade1..4.raw, one per on-screen quote
(clip n goes with quote n in game.c).

Needs the server: kokoro_runtime/target/release/kokoro-server
(127.0.0.1:8770, override with KOKORO_URL).
"""
import io
import json
import math
import os
import struct
import urllib.request
import wave

from modlib import takeme

URL = os.environ.get("KOKORO_URL", "http://127.0.0.1:8770")
RATE = 10026
VOICE = "am_onyx"
PHRASES = ["Game on!", "Ready, player!", "High score!", "Level up!"]


def speak(text):
    req = urllib.request.Request(
        URL + "/tts",
        data=json.dumps({"text": text, "voice": VOICE, "speed": 0.85, "format": "wav"}).encode(),
        headers={"Content-Type": "application/json"})
    data = urllib.request.urlopen(req, timeout=60).read()
    w = wave.open(io.BytesIO(data))
    n, ch, sw, rate = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(n)
    if sw != 2:
        raise ValueError("expected 16-bit wav")
    vals = struct.unpack("<%dh" % (n * ch), raw)[::ch]
    return [v / 32768.0 for v in vals], rate


def robot(vals, rate):
    # pitch down a little (play slower): resample by 0.88 at the source rate
    step = 0.88
    out, pos = [], 0.0
    while pos < len(vals) - 1:
        i = int(pos)
        f = pos - i
        out.append(vals[i] * (1 - f) + vals[i + 1] * f)
        pos += step
    # ring modulation, mixed in for a metallic edge
    rm = [v * (0.55 + 0.45 * math.sin(2 * math.pi * 70 * k / rate)) for k, v in enumerate(out)]
    # low-pass (moving average) before going down to 10 kHz
    win = max(1, int(rate / RATE))
    acc, lp = 0.0, []
    for k, v in enumerate(rm):
        acc += v
        if k >= win:
            acc -= rm[k - win]
        lp.append(acc / win)
    # resample to the game rate
    res, pos, ratio = [], 0.0, rate / RATE
    while pos < len(lp) - 1:
        res.append(lp[int(pos)])
        pos += ratio
    # trim silence, normalise, bit-crush to 5 bits
    thr = 0.02 * max(abs(v) for v in res)
    a = next(i for i, v in enumerate(res) if abs(v) > thr)
    b = len(res) - next(i for i, v in enumerate(reversed(res)) if abs(v) > thr)
    res = res[max(0, a - 50):b + 200]
    peak = max(abs(v) for v in res) or 1.0
    crushed = []
    for v in res:
        q = round(v / peak * 15) / 15.0
        crushed.append(max(-127, min(127, int(q * 120))))
    if len(crushed) & 1:
        crushed.append(0)
    return bytes(c & 0xFF for c in crushed)


def main():
    for i, text in enumerate(PHRASES):
        vals, rate = speak(text)
        data = robot(vals, rate)
        path = takeme("rj_birthday", "snd_arcade%d.raw" % (i + 1))
        with open(path, "wb") as f:
            f.write(data)
        print("%s: %r, %d bytes (%.2f s)" % (path, text, len(data), len(data) / RATE))


if __name__ == "__main__":
    main()
