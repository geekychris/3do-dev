"""Orbit Docking on the 3DO (./3do test orbit_docking).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), the long
   autopilot game's state hash (tests/vectors.h) among them.
2. The title (the autopilot docks behind it), a start with A, the D-pad
   moving the craft across, A thrusting in, L rolling, the autopilot (B)
   flying on; the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("orbit_docking")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=400)
t.keep("title")
t.start(["A"], expect="STATE FLY")
t.play(frames=70)
s0 = t.st()
t.play(frames=60, hold=["RIGHT"])
t.play(frames=70)
s1 = t.st()
t.check(f"the D-pad moves the craft across (x {s0.get('x')} -> {s1.get('x')})", s1.get("x") != s0.get("x"))
t.play(frames=60, hold=["A"])
t.play(frames=70)
s2 = t.st()
t.check(f"A thrusts in (z {s1.get('z')} -> {s2.get('z')})", int(s2.get("z", 0)) < int(s1.get("z", 0)) - 3)
t.play(frames=40, hold=["L"])
t.play(frames=70)
s3 = t.st()
t.check(f"L rolls (roll {s2.get('roll')} -> {s3.get('roll')})", s3.get("roll") != s2.get("roll"))
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=700)
t.keep("playing")
t.play(frames=1800)
t.check("the autopilot docks", any(int(s.get("docks", 0)) > 0 or s.get("level") != "1" for s in t.sts(c)), str(t.st()))
t.fps()
t.quit()
t.finish()
