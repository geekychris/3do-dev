#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Render a .mod to a .wav for listening on the Mac: python3 render.py x.mod [x.wav]

Plays one pass of the order list at Paula's PAL clock, 4 channels panned
Amiga-style. Supports the effects the songs here use (0xy, Axy, Cxx, ECx,
Fxx); it is a preview, not a full player."""
import struct
import sys
import wave

RATE = 44100
CLOCK = 3546895


def render(path, out):
    d = open(path, "rb").read()
    smp = []
    off = 1084
    for i in range(31):
        h = d[20 + i * 30:50 + i * 30]
        ln, ft, vol, ls, ll = struct.unpack(">HBBHH", h[22:30])
        smp.append([ln * 2, vol, ls * 2, ll * 2])
    nord = d[950]
    order = list(d[952:952 + nord])
    npat = max(d[952:1080]) + 1
    pat = d[1084:1084 + npat * 1024]
    off = 1084 + npat * 1024
    for s in smp:
        s.append(d[off:off + s[0]])
        off += s[0]
    sdata = [bytes(x[4]) for x in smp]
    speed, bpm = 6, 125
    ch = [dict(s=None, pos=0.0, per=428, vol=0, cmd=0, prm=0) for _ in range(4)]
    L, R = [], []
    pan = [0, 1, 1, 0]
    for o in order:
        for row in range(64):
            for c in range(4):
                b = pat[o * 1024 + row * 16 + c * 4: o * 1024 + row * 16 + c * 4 + 4]
                sn = (b[0] & 0xF0) | (b[2] >> 4)
                per = ((b[0] & 0xF) << 8) | b[1]
                cmd, prm = b[2] & 0xF, b[3]
                k = ch[c]
                if sn:
                    k["s"] = sn - 1
                    k["vol"] = smp[sn - 1][1]
                if per:
                    k["per"] = per
                    k["pos"] = 0.0
                    k["on"] = True
                k["cmd"], k["prm"] = cmd, prm
                if cmd == 0xC:
                    k["vol"] = min(prm, 64)
                if cmd == 0xF:
                    if prm < 32:
                        speed = prm or 1
                    else:
                        bpm = prm
            for tick in range(speed):
                n = int(RATE * 2.5 / bpm)
                for c in range(4):
                    k = ch[c]
                    per = k["per"]
                    if k["cmd"] == 0 and k["prm"]:
                        t = tick % 3
                        semis = 0 if t == 0 else (k["prm"] >> 4 if t == 1 else k["prm"] & 15)
                        per = int(per / (2 ** (semis / 12)))
                    if k["cmd"] == 0xA and tick:
                        k["vol"] = max(0, min(64, k["vol"] + (k["prm"] >> 4) - (k["prm"] & 15)))
                    if k["cmd"] == 0xE and k["prm"] >> 4 == 0xC and tick == (k["prm"] & 15):
                        k["vol"] = 0
                    k["tp"] = per
                for i in range(n):
                    lv = rv = 0.0
                    for c in range(4):
                        k = ch[c]
                        if k["s"] is None or not k.get("on"):
                            continue
                        s = smp[k["s"]]
                        data = sdata[k["s"]]
                        p = int(k["pos"])
                        if p >= s[0]:
                            if s[3] > 2:
                                k["pos"] = s[2] + (k["pos"] - s[2]) % s[3]
                                p = int(k["pos"])
                            else:
                                k["on"] = False
                                continue
                        v = data[p]
                        v = v - 256 if v > 127 else v
                        v = v * k["vol"] / 64.0
                        if pan[c]:
                            rv += v
                        else:
                            lv += v
                        k["pos"] += CLOCK / k["tp"] / RATE
                    L.append(lv)
                    R.append(rv)
    w = wave.open(out, "wb")
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(RATE)
    frames = bytearray()
    for a, b in zip(L, R):
        a2, b2 = a * 0.75 + b * 0.25, b * 0.75 + a * 0.25
        frames += struct.pack("<hh", int(max(-32767, min(32767, a2 * 110))),
                              int(max(-32767, min(32767, b2 * 110))))
    w.writeframes(bytes(frames))
    w.close()
    print("%s: %.1f s" % (out, len(L) / RATE))


if __name__ == "__main__":
    render(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else sys.argv[1].rsplit(".", 1)[0] + ".wav")
