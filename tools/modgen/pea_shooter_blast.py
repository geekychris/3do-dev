#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Pea Shooter Blast: "Pea Patrol" - an original upbeat march in D major.

Channels 0 and 1 are the music's (mt_MusicChannels = 2): lead on 0, bass
on 1; drums on 2 and an echo line on 3 make room for the sound effects.
"""
from modlib import Song, m, pluck, kick, snare, hat, takeme

song = Song("pea patrol", speed=5, bpm=125)
lead = song.instrument("lead", pluck("square", 32, cycles=36, sustain=0.4, duty=0.25, amp=100), 44)
bass = song.instrument("bass", pluck("triangle", 64, cycles=20, sustain=0.5, amp=120), 60)
bd = song.instrument("kick", kick(), 64)
sd = song.instrument("snare", snare(), 46)
hh = song.instrument("hat", hat(), 30)
echo = song.instrument("echo", pluck("square", 32, cycles=24, sustain=0.2, duty=0.5, amp=80), 20)

MEL = [
    m("D-2:2 F#2:2 A-2:2 D-3:2 C#3:2 A-2:2 F#2:4 "
      "G-2:2 B-2:2 D-3:2 B-2:2 A-2:4 .:4 "
      "E-2:2 G-2:2 B-2:2 E-3:2 D-3:2 B-2:2 G-2:4 "
      "A-2:2 C#3:2 E-3:2 C#3:2 D-3:8"),
    m("F#3:2 E-3:2 D-3:2 A-2:2 B-2:2 A-2:2 F#2:4 "
      "G-2:2 A-2:2 B-2:2 D-3:2 E-3:4 .:4 "
      "D-3:2 C#3:2 B-2:2 G-2:2 A-2:2 B-2:2 C#3:4 "
      "E-3:2 C#3:2 A-2:2 C#3:2 D-3:8"),
]
ROOTS = [["D-1", "G-1", "E-1", "A-1"], ["D-1", "G-1", "B-1", "A-1"]]


def build(i, fill):
    p = song.pattern()
    p.melody(0, lead, MEL[i])
    p.melody(3, echo, [(None, 2)] + MEL[i][:-1])      # the lead, two rows behind
    for bar, r in enumerate(ROOTS[i]):
        hi = r[:2] + "2"
        p.melody(1, bass, m("%s:2 %s:2 %s:2 %s:2 %s:2 %s:2 %s:2 %s:2" %
                            (r, hi, r, hi, r, hi, r, hi)), start=bar * 16)
    p.drums(2, "k.h.s.h.k.h.s.h." * 3 + ("k.h.s.h.s.ss.sss" if fill else "k.h.s.h.k.hks.h."),
            {"k": bd, "s": sd, "h": hh})
    return p


build(0, False)
build(1, True)
song.order([0, 1, 0, 1])
song.save(takeme("pea_shooter_blast", "music.mod"))
