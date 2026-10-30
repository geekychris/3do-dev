"""Harbour Crane on the 3DO (./3do test harbour_crane).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), the long
   autopilot game's state hash (tests/vectors.h) among them.
2. The title (the autopilot works the crane behind it), a start with A,
   the D-pad running the trolley and the winch, the autopilot (B) loading
   containers; the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("harbour_crane")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=400)
t.keep("title")
t.start(["A"], expect="STATE PLAY")
t.play(frames=70)
s0 = t.st()
t.play(frames=150, hold=["RIGHT"])
t.play(frames=70)
s1 = t.st()
t.check(f"the D-pad runs the trolley (x {s0.get('tx')} -> {s1.get('tx')})", int(s1.get("tx", 0)) > int(s0.get("tx", 0)) + 20)
t.play(frames=120, hold=["DOWN"])
t.play(frames=70)
s2 = t.st()
t.check(f"and the winch (cable {s1.get('len')} -> {s2.get('len')})", int(s2.get("len", 0)) > int(s1.get("len", 0)) + 10)
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=2400)
t.keep("playing")
t.check("the autopilot loads a container", any(int(s.get("loaded", 0)) > 0 for s in t.sts(c)), str(t.st()))
t.fps()
t.quit()
t.finish()
