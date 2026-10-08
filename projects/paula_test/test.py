"""Paula emulation test (./3do test paula_test): checks the audio itself.

src/main.c drives the Paula registers through a fixed script, one segment
per 0.5 s; this records the console's audio and checks each segment's
pitch, loudness and timing against what Paula would play.
"""
import re
import sys
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.core import Emulator

HERE = Path(__file__).resolve().parent
RATE = 44100
CLOCK = 3546895
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}" + (f"  ({detail})" if detail else ""))
    if not ok:
        failures.append(name)


def peaks(x, n=1, lo=50, hi=3000):
    """The n strongest spectral peaks (Hz), at least 8 % apart."""
    pad = 1 << 18
    f = np.abs(np.fft.rfft(x * np.hanning(len(x)), pad))
    fr = np.fft.rfftfreq(pad, 1 / RATE)
    band = (fr >= lo) & (fr <= hi)
    f, fr = f[band], fr[band]
    out = []
    for i in np.argsort(f)[::-1]:
        if all(abs(fr[i] - p) > 0.08 * p for p in out):
            out.append(fr[i])
        if len(out) == n:
            break
    return sorted(out)


def near(f, want, tol=0.02):
    return abs(f - want) <= tol * want


def rms(x):
    return float(np.sqrt(np.mean(x ** 2))) if len(x) else 0.0


emu = Emulator()
emu.load(str(HERE / "build" / "paula_test.iso"))
if not emu.run_until_log("PAULA: ready", 3000):
    check("boots", False, "no 'PAULA: ready'")
    sys.exit(1)
dsp = any("DSP voices" in l for l in emu.debug_log)
print(f"  INFO audio backend: {'DSP voices' if dsp else 'software mixer'}")
emu.audio_buffer = bytearray()
emu.audio_capture = True
starts = {}
c = emu.log_cursor()
while True:
    if not emu.run_until_log("PAULA:", 400, since=c):
        break
    c = emu.log_cursor()
    for line in emu.debug_log[-6:]:
        m = re.search(r"PAULA: segment (\d+)", line)
        if m and int(m.group(1)) not in starts:
            starts[int(m.group(1))] = len(emu.audio_buffer) // 4
    if any("PAULA: done" in l for l in emu.debug_log[-6:]):
        break
emu.step(30)
emu.audio_capture = False
st = np.frombuffer(bytes(emu.audio_buffer), dtype="<i2").astype(np.float64).reshape(-1, 2)
emu.close()
w = wave.open(str(HERE / "build" / "paula_test.wav"), "wb")
w.setnchannels(2); w.setsampwidth(2); w.setframerate(RATE)
w.writeframes(st.astype("<i2").tobytes()); w.close()
mono = st.mean(1)
check("all 10 segments ran", len(starts) == 10, f"saw {sorted(starts)}")
if len(starts) < 10:
    sys.exit(1)
SEG = int(0.5 * RATE)


def seg(k, a=0.16, b=0.46):
    """Middle of segment k (the software mixer runs ~80 ms ahead of output)."""
    s = starts[k]
    return mono[s + int(a * RATE): s + int(b * RATE)]


tone = rms(seg(0))
check("segment 0 plays", tone > 500, f"rms {tone:.0f}")
f = peaks(seg(0))[0]
check("looping 64-byte block at period 428: 129.5 Hz", near(f, CLOCK / 428 / 64), f"{f:.1f} Hz")
f = peaks(seg(1))[0]
check("period change while playing: 259 Hz", near(f, CLOCK / 214 / 64), f"{f:.1f} Hz")
r = rms(seg(2)) / max(rms(seg(1)), 1)
check("volume 64 -> 16 quarters the level", 0.18 < r < 0.32, f"ratio {r:.2f}")
r = rms(seg(3)) / tone
check("DMA off is silent", r < 0.01, f"ratio {r:.3f}")
# one-shot: duration of the sound in segment 4
x = np.abs(mono[starts[4] - int(0.1 * RATE): starts[5]])
env = np.array([x[i:i + 220].max() for i in range(0, len(x) - 220, 220)])   # 5 ms
on = np.where(env > 1000)[0]
dur = 0
if len(on):
    end = on[0]                 # first continuous stretch of sound
    while end + 1 < len(env) and env[end + 1] > 1000:
        end += 1
    dur = (end - on[0] + 1) * 220 / RATE
want = 2048 / (CLOCK / 214)
check("one-shot block then the empty word: plays once", abs(dur - want) < 0.025,
      f"{dur*1000:.0f} ms, Paula {want*1000:.0f} ms")
f = peaks(seg(5, 0.25, 0.46))[0]
check("start block then loop block (ProTracker): 443 Hz loop", near(f, CLOCK / 200 / 40), f"{f:.1f} Hz")
f = peaks(seg(6))[0]
check("odd-word loop at a misaligned address: 243.7 Hz", near(f, CLOCK / 428 / 34, 0.01), f"{f:.1f} Hz")
want = [CLOCK / p / 64 for p in (428, 339, 285, 214)]
got = peaks(seg(7), 4, 100, 300)
check("four channels at once", len(got) == 4 and all(near(g, w_) for g, w_ in zip(got, want)),
      " ".join(f"{g:.1f}" for g in got))
x = np.abs(seg(8, 0.1, 0.48))
env = np.array([x[i:i + 44].max() for i in range(0, len(x) - 44, 44)])        # 1 ms
loud = env > 1000
bursts = int(np.sum(loud[1:] & ~loud[:-1]))
per_s = bursts / (len(x) / RATE)
on_ms = loud.mean() * 20
check("retrigger every tick: a one-shot burst per tick", 45 <= per_s <= 55, f"{per_s:.0f} bursts/s")
check("each burst is the 10 ms block", 7 <= on_ms <= 13, f"{on_ms:.1f} ms of each 20 ms")
r = rms(seg(9)) / tone
check("all off is silent", r < 0.01, f"ratio {r:.3f}")
if dsp:
    l, rr = rms(st[starts[0]:starts[1], 0]), rms(st[starts[0]:starts[1], 1])
    check("stereo: channel 0 is left of centre", l > 1.5 * rr, f"L {l:.0f} R {rr:.0f}")
print(f"{'FAIL' if failures else 'PASS'}: paula_test ({len(failures)} failure(s))")
sys.exit(1 if failures else 0)
