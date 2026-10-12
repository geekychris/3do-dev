#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""StakAttack: "Stack Bounce" - an original bouncy puzzle tune in G major.

Lead on channel 0 and bass on 1 (the two the game keeps for music),
drums on 2 and chord stabs on 3 (shared with the sound effects).
"""
from modlib import (Song, m, cycle, pluck, kick, snare, hat, takeme,
                    MAJOR, MINOR)

song = Song("stack bounce", speed=5, bpm=125)
lead = song.instrument("lead", pluck("square", 32, cycles=40, sustain=0.35, duty=0.25), 46)
bass = song.instrument("bass", pluck("triangle", 64, cycles=20, sustain=0.5, amp=120), 60)
bd = song.instrument("kick", kick(), 64)
sd = song.instrument("snare", snare(), 48)
hh = song.instrument("hat", hat(), 34)
stab = song.instrument("stab", pluck("square", 32, cycles=10, sustain=0, duty=0.5, amp=90), 26)
bell = song.instrument("bell", pluck("bell", 32, cycles=40, sustain=0.2, amp=110), 40)

# (root for the bass, root for the stab, arpeggio)
G, Em, C, D = ("G-1", "G-2", MAJOR), ("E-1", "E-2", MINOR), ("C-1", "C-2", MAJOR), ("D-1", "D-2", MAJOR)
Am, Bm = ("A-1", "A-2", MINOR), ("B-1", "B-2", MINOR)

MEL = [
    # A: G Em C D
    m("D-2:2 G-2:2 B-2:2 A-2:2 G-2:4 D-2:4 "
      "E-2:2 G-2:2 B-2:3 C-3:1 B-2:4 G-2:4 "
      "C-3:2 B-2:2 A-2:2 G-2:2 E-2:2 G-2:2 A-2:4 "
      "F#2:2 A-2:2 D-3:4 C-3:2 A-2:2 F#2:4"),
    # A': G Em C D(->G)
    m("D-2:2 G-2:2 B-2:2 D-3:2 E-3:4 D-3:4 "
      "B-2:2 G-2:2 E-2:4 G-2:2 B-2:2 A-2:4 "
      "G-2:2 A-2:2 B-2:2 C-3:2 B-2:2 A-2:2 G-2:2 E-2:2 "
      "D-2:4 F#2:2 A-2:2 G-2:8"),
    # B: Am D G Em
    m("A-2:3 C-3:1 E-3:4 D-3:2 C-3:2 A-2:4 "
      "F#2:2 A-2:2 D-3:2 F#2:2 A-2:8 "
      "B-2:3 D-3:1 G-3:4 F#3:2 E-3:2 D-3:4 "
      "E-3:2 D-3:2 B-2:2 G-2:2 E-2:8"),
    # B': C D Bm/Em D
    m("C-3:2 E-3:2 G-3:4 E-3:2 C-3:2 G-2:4 "
      "D-3:2 F#3:2 A-3:4 F#3:2 D-3:2 A-2:4 "
      "B-2:2 D-3:2 F#3:4 E-3:2 D-3:2 B-2:4 "
      "D-3:2 C-3:2 B-2:2 A-2:2 F#2:2 A-2:2 D-3:4"),
]
CHORDS = [[G, Em, C, D], [G, Em, C, G], [Am, D, G, Em], [C, D, Bm, D]]
# a little counter-line in the bell for the second time round
BELLS = [
    m(".:8 B-3:4 A-3:4 .:8 G-3:4 E-3:4 .:8 E-3:4 G-3:4 .:8 F#3:4 A-3:4"),
    m(".:8 B-3:4 D-3:4 .:8 B-3:4 G-3:4 .:8 C-3:4 E-3:4 .:4 F#3:4 G-3:8"),
]

DRUMS = {"k": bd, "s": sd, "h": hh}


def bass_bar(p, bar, root):
    hi = root[:2] + "2"
    p.melody(1, bass, m("%s:2 %s:2 %s:2 %s:2 %s:2 %s:2 %s:1 %s:1 %s:2" %
                        (root, hi, root, hi, root, hi, root, root, hi)), start=bar * 16)


def stabs(p, bar, chord):
    for beat in (2, 6, 10, 14):
        p.melody(3, stab, [(chord[1], 2)], start=bar * 16 + beat, arp=chord[2])


def build(i, with_bells, fill):
    p = song.pattern()
    p.melody(0, lead, MEL[i])
    for bar, ch in enumerate(CHORDS[i]):
        bass_bar(p, bar, ch[0])
        if not with_bells:
            stabs(p, bar, ch)
    if with_bells:
        p.melody(3, bell, BELLS[i % 2])
    p.drums(2, "k.h.s.h.k.hks.h." * 3 + ("k.h.s.h.k.s.ssss" if fill else "k.h.s.h.k.hks.hh"), DRUMS)
    return p


build(0, False, False)   # 0
build(1, False, True)    # 1
build(2, False, False)   # 2
build(3, False, True)    # 3
build(0, True, False)    # 4
build(1, True, True)     # 5
song.order([0, 1, 4, 5, 2, 3, 0, 1, 2, 3, 4, 5])
song.save(takeme("stakattack", "stakattack.mod"))
