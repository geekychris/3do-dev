# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""
A small ProTracker (M.K., 4 channel, 31 sample) writer for the game music.

Every sound is synthesised here - no recorded samples - so the songs are
entirely our own. Based on the generators of the Amiga originals
(amiga_games: stakattack/generate_mod.py, ace_pilot/gen_mod.py).

    song = Song("my tune", speed=6, bpm=125)
    lead = song.instrument("lead", pluck("square", 32, duty=0.25), volume=40)
    p = song.pattern()
    p.melody(0, lead, [("C-2", 4), ("E-2", 2), (None, 2), ...])
    p.drums(2, "k...s...k.k.s...", {"k": kick_i, "s": snare_i})
    song.order([0, 0, 1])
    song.save("path/to/tune.mod")

Notes are ProTracker names "C-1" .. "B-3" ("C#2" for sharps). With a
looped single-cycle sample of L bytes the pitch is 3546895 / (period * L):
L = 32 puts C-2 at middle C, L = 64 an octave lower, L = 16 an octave up.
Drum hits are rendered for the C-3 playback rate (PLAY_RATE) and should be
triggered on C-3.
"""
import math
import random
import struct

ROWS = 64
PLAY_RATE = 3546895 / 214          # Hz of a sample played at C-3

NOTE_NAMES = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
PERIODS = [856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
           428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
           214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113]


def note_index(name):
    """'C-1' -> 0 .. 'B-3' -> 35"""
    return NOTE_NAMES.index(name[:2]) + 12 * (int(name[2]) - 1)


def note_name(index):
    if not 0 <= index < 36:
        raise ValueError("note out of ProTracker range: %d" % index)
    return NOTE_NAMES[index % 12] + str(index // 12 + 1)


def transpose(name, semis):
    return note_name(note_index(name) + semis)


def m(text, unit=1):
    """compact melody: "E-2:4 B-1:2 .:2 ^:2" -> [("E-2", 4), ("B-1", 2),
    (None, 2), ("^", 2)]; '.' rests, '^' cuts the note; ':n' defaults to 1.
    Durations are multiplied by `unit`."""
    out = []
    for tok in text.split():
        n, _, d = tok.partition(":")
        rows = int(d) if d else 1
        out.append((None if n == "." else n, rows * unit))
    return out


# ---------------------------------------------------------------- samples

def _clip8(v):
    v = int(round(v))
    return max(-128, min(127, v))


def _to_bytes(vals):
    vals = [_clip8(v) for v in vals]
    if len(vals) & 1:
        vals.append(0)
    return bytes(v & 0xFF for v in vals)


def _osc(kind, phase, duty=0.5):
    """one sample of a waveform, phase in [0, 1), result in [-1, 1]"""
    phase %= 1.0
    if kind == "sine":
        return math.sin(2 * math.pi * phase)
    if kind == "square":
        return 1.0 if phase < duty else -1.0
    if kind == "saw":
        return 2.0 * phase - 1.0
    if kind == "triangle":
        return 4.0 * phase - 1.0 if phase < 0.5 else 3.0 - 4.0 * phase
    if kind == "organ":      # fundamental + octave + fifth above
        return (math.sin(2 * math.pi * phase) * 0.6 +
                math.sin(4 * math.pi * phase) * 0.3 +
                math.sin(6 * math.pi * phase) * 0.2)
    if kind == "bell":
        return (math.sin(2 * math.pi * phase) * 0.6 +
                math.sin(2 * math.pi * 3 * phase) * 0.25 +
                math.sin(2 * math.pi * 5 * phase) * 0.15)
    raise ValueError(kind)


def cycle(kind, length=32, amp=100, duty=0.5):
    """one looped cycle: a sustained tone. Returns (data, loop_start, loop_len)."""
    d = _to_bytes([amp * _osc(kind, i / length, duty) for i in range(length)])
    return d, 0, len(d)


def pluck(kind, length=32, cycles=24, amp=110, sustain=0.25, duty=0.5, attack=0):
    """`cycles` decaying cycles (exponential down to `sustain` of the start),
    then the last cycle loops so a held note keeps sounding quietly.
    sustain=0 makes a one-shot that dies away."""
    out = []
    total = cycles * length
    floor = max(sustain, 0.001)
    for i in range(total):
        t = i / total
        env = floor ** t
        if attack and i < attack * length:
            env *= i / (attack * length)
        out.append(amp * env * _osc(kind, i / length, duty))
    if sustain <= 0:
        out += [0] * length
        d = _to_bytes(out)
        return d, 0, 0
    d = _to_bytes(out)
    return d, len(d) - length, length


def pad(kind, length=32, beats=16, amp=70, detune=1, duty=0.5):
    """two oscillators `detune` cycles apart over a loop of `beats` cycles:
    a slow chorus that still loops cleanly."""
    n = length * beats
    vals = []
    for i in range(n):
        p = i / n
        vals.append(amp * 0.5 * (_osc(kind, p * beats, duty) +
                                 _osc(kind, p * (beats + detune), duty)))
    d = _to_bytes(vals)
    return d, 0, len(d)


def _env_hit(n, decay):
    return [math.exp(-i / (decay * PLAY_RATE)) for i in range(n)]


def kick(ms=180, amp=127, f0=160.0, f1=45.0):
    n = int(PLAY_RATE * ms / 1000)
    out, ph = [], 0.0
    for i in range(n):
        t = i / n
        f = f1 + (f0 - f1) * math.exp(-t * 7)
        ph += f / PLAY_RATE
        out.append(amp * math.exp(-t * 4) * math.sin(2 * math.pi * ph))
    return _to_bytes(out), 0, 0


def snare(ms=160, amp=110, tone=190.0, seed=7):
    rnd = random.Random(seed)
    n = int(PLAY_RATE * ms / 1000)
    out = []
    for i in range(n):
        t = i / n
        noise = rnd.uniform(-1, 1) * math.exp(-t * 5)
        body = math.sin(2 * math.pi * tone * i / PLAY_RATE) * math.exp(-t * 12)
        out.append(amp * (0.7 * noise + 0.5 * body))
    return _to_bytes(out), 0, 0


def hat(ms=40, amp=70, seed=11, open_=False):
    rnd = random.Random(seed)
    n = int(PLAY_RATE * (ms * (4 if open_ else 1)) / 1000)
    out, prev = [], 0.0
    for i in range(n):
        t = i / n
        x = rnd.uniform(-1, 1)
        hp = x - prev          # crude high-pass: brighter
        prev = x
        out.append(amp * 0.6 * hp * math.exp(-t * (3 if open_ else 6)))
    return _to_bytes(out), 0, 0


def clap(ms=150, amp=100, seed=5):
    rnd = random.Random(seed)
    n = int(PLAY_RATE * ms / 1000)
    out = []
    bursts = [0, 0.012, 0.024]
    for i in range(n):
        t = i / PLAY_RATE
        e = 0.0
        for b in bursts:
            if t >= b:
                e = max(e, math.exp(-(t - b) * 60))
        e = max(e, 0.5 * math.exp(-(t - 0.03) * 18) if t > 0.03 else 0)
        out.append(amp * rnd.uniform(-1, 1) * e)
    return _to_bytes(out), 0, 0


def tom(ms=220, amp=110, f0=220.0, f1=110.0):
    return kick(ms, amp, f0, f1)


def zap(ms=120, amp=90, f0=1800.0, f1=200.0):
    """a falling laser blip (one-shot)"""
    n = int(PLAY_RATE * ms / 1000)
    out, ph = [], 0.0
    for i in range(n):
        t = i / n
        f = f0 * (f1 / f0) ** t
        ph += f / PLAY_RATE
        out.append(amp * (1 - t) * (1.0 if ph % 1 < 0.5 else -1.0))
    return _to_bytes(out), 0, 0


def mix(*samples, gains=None):
    """layer one-shot samples (e.g. kick + clap) into one"""
    gains = gains or [1.0] * len(samples)
    vals = []
    for (data, _, _), g in zip(samples, gains):
        for i, b in enumerate(data):
            v = (b - 256 if b > 127 else b) * g
            if i < len(vals):
                vals[i] += v
            else:
                vals.append(v)
    return _to_bytes(vals), 0, 0


# ---------------------------------------------------------------- song

class Instrument:
    def __init__(self, number, name, data, loop_start, loop_len, volume, finetune=0):
        self.number = number
        self.name = name
        self.data = data
        self.loop_start = loop_start
        self.loop_len = loop_len
        self.volume = volume
        self.finetune = finetune

    def header(self):
        name = self.name.encode("ascii")[:22].ljust(22, b"\0")
        if self.loop_len > 2:
            ls, ll = self.loop_start // 2, self.loop_len // 2
        else:
            ls, ll = 0, 1
        return name + struct.pack(">HBBHH", len(self.data) // 2,
                                  self.finetune & 15, self.volume, ls, ll)


class Pattern:
    def __init__(self):
        self.cells = [[None] * 4 for _ in range(ROWS)]

    def put(self, row, ch, inst=None, note=None, fx=0, prm=0):
        """set a cell; an effect-only cell keeps the note/sample already there"""
        if not 0 <= row < ROWS:
            return
        old = self.cells[row][ch]
        if inst is None and note is None and old is not None:
            self.cells[row][ch] = (old[0], old[1], fx, prm)
        else:
            self.cells[row][ch] = (inst.number if inst else 0, note, fx, prm)

    def melody(self, ch, inst, notes, start=0, vol=None, arp=0, cut=None,
               slide=None, transpose_by=0):
        """notes: [(note or None, rows) ...]; None is a rest.
        vol      - Cxx volume on each note
        arp      - 0xy arpeggio held across each note's rows (chords)
        cut      - ECx: cut each note after x ticks (staccato)
        slide    - Axy volume slide on the rows after each note (fades)"""
        row = start
        for item in notes:
            n, length = item[0], item[1]
            nvol = item[2] if len(item) > 2 else vol
            if n == "^":                           # note off
                if row < ROWS:
                    self.cells[row][ch] = (0, None, 0xC, 0)
            elif n is not None:
                if row < ROWS:
                    name = transpose(n, transpose_by) if transpose_by else n
                    fx, prm = 0, 0
                    if arp:
                        fx, prm = 0, arp
                    elif cut is not None:
                        fx, prm = 0xE, 0xC0 | cut
                    elif nvol is not None:
                        fx, prm = 0xC, nvol
                    self.cells[row][ch] = (inst.number, name, fx, prm)
                    for r in range(row + 1, min(row + length, ROWS)):
                        if arp:
                            self.cells[r][ch] = (0, None, 0, arp)
                        elif slide:
                            self.cells[r][ch] = (0, None, 0xA, slide)
            row += length
        return row

    def drums(self, ch, steps, kit, start=0, every=1):
        """steps: one char per row ('.' = nothing); repeated to fill.
        kit: {char: instrument} or {char: (instrument, volume)}"""
        for i in range(start, ROWS, every):
            c = steps[((i - start) // every) % len(steps)]
            if c in kit:
                k = kit[c]
                if isinstance(k, tuple):
                    self.cells[i][ch] = (k[0].number, "C-3", 0xC, k[1])
                else:
                    self.cells[i][ch] = (k.number, "C-3", 0, 0)

    def fx(self, row, ch, fx, prm):
        self.put(row, ch, None, None, fx, prm)

    def encode(self):
        out = bytearray()
        for row in self.cells:
            for cell in row:
                if cell is None:
                    out += b"\0\0\0\0"
                    continue
                s, n, fx, prm = cell
                per = PERIODS[note_index(n)] if n else 0
                out += bytes([(s & 0xF0) | (per >> 8), per & 0xFF,
                              ((s & 0x0F) << 4) | (fx & 0xF), prm & 0xFF])
        return bytes(out)


class Song:
    def __init__(self, title, speed=6, bpm=125):
        self.title = title
        self.speed = speed
        self.bpm = bpm
        self.instruments = []
        self.patterns = []
        self._order = []

    def instrument(self, name, sample, volume=64, finetune=0):
        data, ls, ll = sample
        inst = Instrument(len(self.instruments) + 1, name, data, ls, ll, volume, finetune)
        self.instruments.append(inst)
        return inst

    def pattern(self):
        p = Pattern()
        self.patterns.append(p)
        return p

    def order(self, seq):
        self._order = list(seq)

    def build(self):
        if not self._order:
            self._order = list(range(len(self.patterns)))
        first = self.patterns[self._order[0]]
        # tempo on row 0 of the first pattern played, in free channels' effect slots
        tempo = [(0xF, self.speed), (0xF, self.bpm)]
        for ch in range(4):
            if not tempo:
                break
            cell = first.cells[0][ch]
            if cell is None or (cell[2] == 0 and cell[3] == 0):
                fx, prm = tempo.pop(0)
                if cell is None:
                    first.cells[0][ch] = (0, None, fx, prm)
                else:
                    first.cells[0][ch] = (cell[0], cell[1], fx, prm)
        if tempo:
            raise ValueError("no free effect slot on row 0 for the tempo")

        out = bytearray(self.title.encode("ascii")[:20].ljust(20, b"\0"))
        for i in range(31):
            if i < len(self.instruments):
                out += self.instruments[i].header()
            else:
                out += b"\0" * 22 + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
        out += bytes([len(self._order), 127])
        out += bytes(self._order).ljust(128, b"\0")
        out += b"M.K."
        for p in self.patterns[:max(self._order) + 1]:
            out += p.encode()
        for inst in self.instruments:
            out += inst.data
        return bytes(out)

    def save(self, path):
        data = self.build()
        with open(path, "wb") as f:
            f.write(data)
        secs = len(self._order) * ROWS * self.speed * 2.5 / self.bpm
        print("%s: %d bytes, %d patterns, %d positions, ~%d s" %
              (path, len(data), max(self._order) + 1, len(self._order), secs))
        return data


# ---------------------------------------------------------------- helpers

MAJOR, MINOR, SEVENTH, SUS4 = 0x47, 0x37, 0x4A, 0x57


def takeme(project, filename):
    """path of a disc file in projects/<project>/takeme/<project>/"""
    import os
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    return os.path.join(root, "projects", project, "takeme", project, filename)


def place(patterns, ch, inst, notes, start=0, **kw):
    """write a note list that runs on across consecutive patterns
    (absolute row = pattern index * 64 + row)"""
    row = start
    for n, length in notes:
        p, r = divmod(row, ROWS)
        if p < len(patterns):
            patterns[p].melody(ch, inst, [(n, min(length, ROWS - r))], start=r, **kw)
        row += length
    return row
