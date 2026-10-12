#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Rock Blaster: "Rock Storm" - an original driving synth tune in E minor.

The game keeps channels 0 and 1 for music (mt_MusicChannels = 2), so the
lead is on 0 and the bass on 1; drums on 2 and stabs on 3 give way to the
sound effects. Orb Hunter plays the same file.
"""
from modlib import Song, m, pluck, pad, kick, snare, hat, takeme, MINOR, MAJOR

song = Song("rock storm", speed=6, bpm=132)
lead = song.instrument("brass", pluck("saw", 32, cycles=48, sustain=0.55, amp=100), 44)
bass = song.instrument("bass", pluck("square", 64, cycles=18, sustain=0.4, duty=0.3, amp=115), 58)
bd = song.instrument("kick", kick(), 64)
sd = song.instrument("snare", snare(), 46)
hh = song.instrument("hat", hat(), 30)
stab = song.instrument("stab", pluck("square", 32, cycles=8, sustain=0, amp=90), 24)

Em, C, D, Bm, Am, G = (("E-1", "E-2", MINOR), ("C-1", "C-2", MAJOR), ("D-1", "D-2", MAJOR),
                       ("B-1", "B-2", MINOR), ("A-1", "A-2", MINOR), ("G-1", "G-2", MAJOR))

MEL = [
    m("E-2:3 G-2:1 B-2:4 A-2:2 G-2:2 E-2:4 "
      "C-3:3 B-2:1 G-2:4 E-2:2 G-2:2 C-3:4 "
      "D-3:3 C-3:1 A-2:4 F#2:2 A-2:2 D-3:4 "
      "B-2:2 A-2:2 G-2:2 F#2:2 B-2:8"),
    m("E-2:3 G-2:1 B-2:4 E-3:2 D-3:2 B-2:4 "
      "C-3:2 D-3:2 E-3:4 D-3:2 C-3:2 G-2:4 "
      "A-2:2 C-3:2 E-3:4 D-3:2 C-3:2 A-2:4 "
      "B-2:4 D#3:4 E-3:8"),
    m("A-2:2 .:2 A-2:2 C-3:2 E-3:4 C-3:4 "
      "G-2:2 .:2 G-2:2 B-2:2 D-3:4 B-2:4 "
      "F#2:2 A-2:2 D-3:2 F#3:2 E-3:4 D-3:4 "
      "B-2:2 D#3:2 F#3:2 A-3:2 G-3:4 F#3:4"),
]
CHORDS = [[Em, C, D, Bm], [Em, C, Am, Bm], [Am, G, D, Bm]]


def build(i, fill):
    p = song.pattern()
    p.melody(0, lead, MEL[i])
    for bar, ch in enumerate(CHORDS[i]):
        r, hi = ch[0], ch[0][:2] + "2"
        p.melody(1, bass, m("%s:2 %s:2 %s:2 %s:2 %s:2 %s:2 %s:2 %s:2" %
                            (r, r, hi, r, r, hi, r, hi)), start=bar * 16)
        for beat in (2, 10):
            p.melody(3, stab, [(ch[1], 2)], start=bar * 16 + beat, arp=ch[2])
    p.drums(2, "k.h.s.h.k.k.s.h." * 3 + ("k.h.s.h.s.s.ssss" if fill else "k.h.s.h.k.k.s.hh"),
            {"k": bd, "s": sd, "h": hh})
    return p


build(0, False)
build(1, True)
build(2, True)
song.order([0, 1, 0, 1, 2, 2, 0, 1])
# On the 3DO only Orb Hunter plays it (the Amiga Rock Blaster does too).
song.save(takeme("orb_hunter", "rock_blaster.mod"))
