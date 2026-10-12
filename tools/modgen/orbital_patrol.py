#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Orbital Patrol: "Patrol Drive" - an original driving techno track in A minor.

Arpeggio/lead on channel 0 and rolling bass on 1 (the game's music
channels); kick and clap on 2, hats on 3 (shared with the shots and
explosions).
"""
from modlib import (Song, m, mix, pad, pluck, kick, snare, hat, clap, takeme,
                    MAJOR, MINOR)

song = Song("patrol drive", speed=6, bpm=135)
arp = song.instrument("arp", pluck("square", 32, cycles=8, sustain=0.3, duty=0.3, amp=110), 40)
bass = song.instrument("rolling bass", pluck("saw", 64, cycles=6, sustain=0.25, amp=120), 58)
bd = song.instrument("kick", kick(ms=160, f0=170.0, f1=48.0), 64)
kc = song.instrument("kick+clap", mix(kick(ms=160, f0=170.0, f1=48.0), clap(), gains=[0.75, 0.6]), 64)
hh = song.instrument("hat", hat(ms=30), 30)
oh = song.instrument("open hat", hat(ms=30, open_=True, seed=19), 26)
lead = song.instrument("saw lead", pad("saw", 32, beats=8, detune=1, amp=100), 38)
sd = song.instrument("snare", snare(ms=120), 40)
stab = song.instrument("stab", pluck("saw", 32, cycles=10, sustain=0, amp=100), 30)

# chord tones for the arpeggiator: (bass root, [notes for the arp])
Am = ("A-1", ["A-2", "C-3", "E-3", "A-3"])
F = ("F-1", ["F-2", "A-2", "C-3", "F-3"])
G = ("G-1", ["G-2", "B-2", "D-3", "G-3"])
Dm = ("D-1", ["D-2", "F-2", "A-2", "D-3"])
E = ("E-1", ["E-2", "G#2", "B-2", "E-3"])
C = ("C-2", ["C-2", "E-2", "G-2", "C-3"])

# the arpeggiator's step order over a bar of sixteenths
STEPS = [0, 1, 2, 0, 1, 2, 3, 2, 0, 1, 2, 0, 3, 2, 1, 2]

PROG_A = [Am, Am, F, G]
PROG_B = [Dm, F, Am, E]
PROG_C = [F, G, C, E]

LEAD_B = m("D-3:6 F-3:2 A-3:8 "
           "C-3:6 F-3:2 A-3:4 G-3:4 "
           "E-3:6 C-3:2 A-2:8 "
           "B-2:6 E-3:2 G#3:8")
LEAD_C = m("A-3:4 G-3:4 F-3:6 E-3:2 "
           "D-3:4 B-2:4 D-3:4 G-3:4 "
           "E-3:6 G-3:2 C-3:8 "
           "B-2:4 G#2:4 E-2:8")


def rolling_bass(p, bar, root):
    hi = root[:2] + str(int(root[2]) + 1)
    p.melody(1, bass, m(".:2 %s:1 %s:1 .:2 %s:1 %s:1 .:2 %s:1 %s:1 .:2 %s:1 %s:1" %
                        (root, root, root, hi, root, root, root, hi)), start=bar * 16)


def arp_bar(p, bar, chord, twist=0):
    notes = chord[1]
    for i, s in enumerate(STEPS):
        idx = (s + twist) % 4
        p.melody(0, arp, [(notes[idx], 1)], start=bar * 16 + i, cut=3)


def build(prog, kick_on=True, arp_on=False, melody=None, clap_on=True, hats="..h...h...h...h.",
          fill=False, stabs=False):
    p = song.pattern()
    for bar, ch in enumerate(prog):
        rolling_bass(p, bar, ch[0])
        if arp_on:
            arp_bar(p, bar, ch, twist=bar % 2)
        if stabs:
            p.melody(0, stab, [(ch[1][0], 3)], start=bar * 16, arp=MINOR if "A" in ch[0] or "D" in ch[0] else MAJOR)
            p.melody(0, stab, [(ch[1][0], 3)], start=bar * 16 + 6, arp=MINOR if "A" in ch[0] or "D" in ch[0] else MAJOR)
    if melody:
        p.melody(0, lead, melody)
    if kick_on:
        p.drums(2, "k...c...k...c..." if clap_on else "k...k...k...k...", {"k": bd, "c": kc})
    if fill:
        for r in range(56, 64):
            p.melody(2, sd, [("C-3", 1)], start=r, vol=24 + (r - 56) * 5)
    p.drums(3, hats, {"h": hh, "o": oh})
    return p


build(PROG_A, clap_on=False)                                    # 0 intro: kick, bass, hats
build(PROG_A, arp_on=True, hats="..o...o...o...o.")             # 1 arp riff
build(PROG_B, melody=LEAD_B, hats="h.o.h.o.h.o.hho.")           # 2 lead
build(PROG_C, melody=LEAD_C, hats="h.o.h.o.h.o.hho.", fill=True)  # 3 lead, answer
build(PROG_B, kick_on=False, clap_on=False, stabs=True,
      hats="................", fill=True)                        # 4 breakdown
build(PROG_C, arp_on=True, hats="h.o.h.o.h.o.hho.")             # 5 arp over the answer
song.order([0, 1, 1, 2, 3, 4, 1, 5, 2, 3])
song.save(takeme("orbital_patrol", "orbital_patrol.mod"))
