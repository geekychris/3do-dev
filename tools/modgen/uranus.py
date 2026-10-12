#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Uranus Lander: "Ring Drift" - an original, slow space-funk piece in D dorian.

The game keeps only channel 0 for music (the thrust and landing sounds take
the others), so the melody lives on channel 0 and the song still makes
sense with nothing else playing; bass is on 1, a chorused pad on 2 and the
drums on 3.
"""
from modlib import (Song, m, pad, pluck, kick, snare, hat, takeme,
                    MAJOR, MINOR)

song = Song("ring drift", speed=6, bpm=100)
lead = song.instrument("glass lead", pluck("bell", 32, cycles=80, sustain=0.3, amp=115), 48)
bass = song.instrument("funk bass", pluck("saw", 64, cycles=14, sustain=0.2, amp=110), 58)
vox = song.instrument("ring pad", pad("saw", 32, beats=16, detune=1, amp=80), 22)
bd = song.instrument("kick", kick(ms=220, f0=120.0), 60)
sd = song.instrument("snare", snare(ms=200, tone=170.0), 40)
hh = song.instrument("hat", hat(ms=30), 26)
oh = song.instrument("open hat", hat(ms=30, open_=True, seed=3), 20)
echo = song.instrument("echo lead", pluck("sine", 32, cycles=60, sustain=0.2, amp=90), 30)

Dm, G, Am, Bb, C, F = (("D-1", "D-2", MINOR), ("G-1", "G-2", MAJOR), ("A-1", "A-2", MINOR),
                       ("A#1", "A#2", MAJOR), ("C-2", "C-2", MAJOR), ("F-1", "F-2", MAJOR))

PROG_A = [Dm, G, Dm, Am]
PROG_B = [Bb, C, Dm, Dm]

MEL_A = m(".:4 A-2:2 C-3:2 D-3:6 C-3:2 "
          "B-2:4 A-2:2 G-2:2 A-2:8 "
          ".:2 F-2:2 A-2:2 D-3:2 E-3:4 F-3:4 "
          "E-3:6 D-3:2 C-3:4 A-2:4")
MEL_B = m("D-3:4 F-3:4 A-3:6 G-3:2 "
          "G-3:4 E-3:2 C-3:2 E-3:8 "
          "F-3:2 E-3:2 D-3:4 A-2:4 C-3:4 "
          "D-3:12 .:4")
# the drifting line for the breakdown: wide intervals, lots of air
MEL_DRIFT = m("A-3:8 E-3:8 F-3:4 D-3:4 C-3:8 "
              "D-3:8 A-2:8 C-3:6 B-2:2 A-2:8")


def funk_bass(p, bar, root):
    hi = root[:2] + str(int(root[2]) + 1)
    p.melody(1, bass, m("%s:3 %s:1 .:2 %s:2 .:2 %s:2 %s:1 %s:1 %s:2" %
                        (root, root, hi, root, hi, root, hi)), start=bar * 16)


def pads(p, prog):
    for bar, ch in enumerate(prog):
        p.melody(2, vox, [(ch[1], 16)], start=bar * 16, arp=ch[2])


def drums(p, groove=True):
    if groove:
        p.drums(3, "k..hs..hk.kh s.o".replace(" ", "."), {"k": bd, "s": sd, "h": hh, "o": oh})
    else:
        p.drums(3, "k...............", {"k": bd})


def build(mel, prog, groove=True, bassline=True, inst=None):
    p = song.pattern()
    if mel:
        p.melody(0, inst or lead, mel)
    if bassline:
        for bar, ch in enumerate(prog):
            funk_bass(p, bar, ch[0])
    pads(p, prog)
    drums(p, groove)
    return p


build(None, PROG_A, groove=False)                 # 0 intro: pad, bass, heartbeat kick
build(MEL_A, PROG_A)                              # 1 A
build(MEL_B, PROG_B)                              # 2 B
build(MEL_DRIFT, [F, C, Dm, Am], groove=False,
      bassline=False, inst=echo)                  # 3 breakdown
build(MEL_A, PROG_A, inst=echo)                   # 4 A, softer voice
song.order([0, 1, 2, 1, 2, 3, 4, 2])
song.save(takeme("uranus_lander", "uranus.mod"))
