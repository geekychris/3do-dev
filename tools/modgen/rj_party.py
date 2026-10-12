#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""RJ Birthday: "Party Time" - an original upbeat party tune in C major.

Lead on channel 0 and bass on 1 (the game's music channels); drums on 2
and off-beat chord stabs on 3.
"""
from modlib import (Song, m, pluck, kick, snare, hat, clap, takeme,
                    MAJOR, MINOR)

song = Song("party time", speed=5, bpm=125)
lead = song.instrument("party lead", pluck("square", 32, cycles=36, sustain=0.4, duty=0.375, amp=110), 46)
bass = song.instrument("bass", pluck("triangle", 64, cycles=16, sustain=0.45, amp=120), 60)
bd = song.instrument("kick", kick(), 62)
sd = song.instrument("snare", snare(), 46)
hh = song.instrument("hat", hat(), 30)
cp = song.instrument("clap", clap(), 40)
stab = song.instrument("organ stab", pluck("organ", 32, cycles=12, sustain=0, amp=110), 30)
whistle = song.instrument("whistle", pluck("sine", 32, cycles=50, sustain=0.5, amp=110), 40)

C, Am, F, G = ("C-2", MAJOR), ("A-1", MINOR), ("F-1", MAJOR), ("G-1", MAJOR)
E, Em, Dm = ("E-1", MAJOR), ("E-1", MINOR), ("D-1", MINOR)

SECTIONS = [
    ([C, Am, F, G],
     m("E-2:2 G-2:2 C-3:2 E-3:2 D-3:2 C-3:2 G-2:4 "
       "A-2:2 C-3:2 E-3:4 D-3:2 C-3:2 A-2:4 "
       "F-2:2 A-2:2 C-3:2 F-3:2 E-3:2 D-3:2 C-3:2 A-2:2 "
       "G-2:4 B-2:2 D-3:2 G-3:6 .:2")),
    ([C, E, F, G],
     m("G-3:2 E-3:2 C-3:2 E-3:2 G-3:4 E-3:4 "
       "G#2:2 B-2:2 E-3:4 D-3:2 B-2:2 G#2:4 "
       "A-2:2 C-3:2 F-3:4 E-3:2 D-3:2 C-3:4 "
       "D-3:2 E-3:2 F-3:2 D-3:2 B-2:2 D-3:2 G-2:4")),
    ([F, G, Em, Am],
     m("C-3:3 A-2:1 F-2:4 A-2:2 C-3:2 F-3:4 "
       "D-3:3 B-2:1 G-2:4 B-2:2 D-3:2 G-3:4 "
       "E-3:2 G-3:2 B-3:4 A-3:2 G-3:2 E-3:4 "
       "A-3:4 G-3:2 E-3:2 C-3:2 E-3:2 A-2:4")),
    ([Dm, G, C, C],
     m("F-3:2 E-3:2 D-3:2 A-2:2 D-3:4 F-3:4 "
       "G-3:2 F-3:2 D-3:2 B-2:2 G-2:4 B-2:4 "
       "C-3:4 E-3:2 G-3:2 C-3:2 G-2:2 E-2:4 "
       "C-3:8 .:8")),
]


def fifth(root):
    from modlib import transpose
    return transpose(root, 7)


def build(i, voice, beat):
    prog, mel = SECTIONS[i]
    p = song.pattern()
    p.melody(0, voice, mel)
    for bar, (root, q) in enumerate(prog):
        # oom-pah bass: root, fifth, root, octave
        hi = root[:2] + str(int(root[2]) + 1)
        p.melody(1, bass, [(root, 4), (fifth(root), 4), (root, 4), (hi, 2), (fifth(root), 2)],
                 start=bar * 16)
        for off in (2, 6, 10, 14):
            p.melody(3, stab, [(root[:2] + "2", 2)], start=bar * 16 + off, arp=q)
    p.drums(2, beat, {"k": bd, "s": sd, "h": hh, "c": cp})
    return p


BEAT = "k.h.s.h.k.hks.h."
BEAT2 = "k.h.c.h.k.hkc.hh"
build(0, lead, BEAT)          # 0
build(1, lead, BEAT)          # 1
build(2, lead, BEAT2)         # 2
build(3, lead, BEAT2)         # 3
build(0, whistle, BEAT2)      # 4 the tune again on the whistle
build(1, whistle, BEAT2)      # 5
song.order([0, 1, 2, 3, 4, 5, 2, 3])
song.save(takeme("rj_birthday", "party.mod"))
