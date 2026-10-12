#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""RJ Birthday: "Happy Birthday to You" (Mildred and Patty Hill; public
domain) in our own waltz arrangement, C major, 3/4.

Melody on channel 0, oom-pah-pah bass on 1, chords on 2, a sparkle line on 3.
The tune plays twice (organ, then a brighter voice) and ends on a held chord.
"""
from modlib import (Song, m, pluck, pad, cycle, takeme, place, transpose,
                    MAJOR, SEVENTH)

song = Song("happy birthday waltz", speed=6, bpm=110)
organ = song.instrument("organ", pluck("organ", 32, cycles=60, sustain=0.5, amp=110), 50)
bass = song.instrument("bass", pluck("triangle", 64, cycles=16, sustain=0.4, amp=120), 58)
chord = song.instrument("chord", pluck("square", 32, cycles=10, sustain=0, duty=0.5, amp=90), 24)
bright = song.instrument("bright lead", pluck("square", 32, cycles=50, sustain=0.45, duty=0.25, amp=110), 44)
sparkle = song.instrument("sparkle", pluck("bell", 32, cycles=30, sustain=0, amp=110), 28)
hold = song.instrument("hold", pad("organ", 32, beats=8, detune=1, amp=90), 40)

pats = [song.pattern() for _ in range(4)]

# the melody: two-row-per-eighth, a bar of 3/4 is 12 rows; pickup "Hap-py"
TUNE = m("G-2:3 G-2:1 "
         "A-2:4 G-2:4 C-3:4 "
         "B-2:8 G-2:3 G-2:1 "
         "A-2:4 G-2:4 D-3:4 "
         "C-3:8 G-2:3 G-2:1 "
         "G-3:4 E-3:4 C-3:4 "
         "B-2:4 A-2:4 F-3:3 F-3:1 "
         "E-3:4 C-3:4 D-3:4 "
         "C-3:12")
# one chord per bar (bass root, chord root, arpeggio)
BARS = [("C-2", "C-2", MAJOR), ("G-1", "G-2", SEVENTH), ("G-1", "G-2", SEVENTH),
        ("C-2", "C-2", MAJOR), ("C-2", "C-2", SEVENTH), ("F-1", "F-2", MAJOR),
        ("C-2", "C-2", MAJOR), ("C-2", "C-2", MAJOR)]
PASS = 4 + 8 * 12          # rows in one pass of the tune

for n, voice in enumerate((organ, bright)):
    start = n * PASS
    place(pats, 0, voice, TUNE, start=start)
    for b, (broot, croot, q) in enumerate(BARS):
        row = start + 4 + b * 12
        if b == 6:      # "hap-py birth-day": C, then G7 on the last beat
            place(pats, 1, bass, [(broot, 8), ("G-1", 4)], start=row)
            place(pats, 2, chord, [(None, 4), (croot, 4), ("G-2", 4)], start=row, arp=0x47)
            continue
        place(pats, 1, bass, [(broot, 4), (None, 8)], start=row)
        place(pats, 2, chord, [(None, 4), (croot, 3), (None, 1), (croot, 3), (None, 1)],
              start=row, arp=q)
    if n == 1:          # second time round: a sparkle answering each phrase
        for b in (1, 3, 5, 7):
            row = start + 4 + b * 12 + 8
            place(pats, 3, sparkle, m("C-3:1 E-3:1 G-3:1 C-3:1"), start=row)

# the ending: held C chord with a rising sparkle, then a little silence
end = 2 * PASS
place(pats, 0, hold, [("C-3", 40), ("^", 8)], start=end, arp=MAJOR)
place(pats, 1, bass, [("C-1", 16), ("C-2", 24), ("^", 8)], start=end)
place(pats, 3, sparkle, m("C-2:2 E-2:2 G-2:2 C-3:2 E-3:2 G-3:2 C-3:4"), start=end)

song.order([0, 1, 2, 3])
song.save(takeme("rj_birthday", "birthday.mod"))
