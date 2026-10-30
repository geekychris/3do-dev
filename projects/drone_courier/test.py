"""Drone Courier on the 3DO (./3do test drone_courier).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), the long
   autopilot day's state hash (tests/vectors.h) among them.
2. The title (the autopilot flies behind it), a start with A, A climbing,
   the D-pad flying forward and turning, the autopilot (B) delivering;
   the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("drone_courier")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=400)
t.keep("title")
t.start(["A"], expect="STATE FLY")
t.play(frames=70)
s0 = t.st()
t.play(frames=120, hold=["A"])
t.play(frames=10)
s1 = t.st()
t.check(f"A climbs (height {s0.get('y')} -> {s1.get('y')})", int(s1.get("y", 0)) > int(s0.get("y", 0)) + 5)
t.play(frames=60, hold=["LEFT", "A"])
t.play(frames=70)
s2 = t.st()
t.check(f"left turns (heading {s1.get('yaw')} -> {s2.get('yaw')})", s2.get("yaw") != s1.get("yaw"))
t.play(frames=120, hold=["UP", "A"])
t.play(frames=70)
s3 = t.st()
d = abs(int(s3.get("x", 0)) - int(s2.get("x", 0))) + abs(int(s3.get("z", 0)) - int(s2.get("z", 0)))
t.check(f"up flies forward ({d} units)", d > 20)
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=700)
t.keep("playing")
t.play(frames=2500)
t.check("the autopilot delivers", any(int(s.get("done", 0)) > 0 or s.get("carrying") == "1" for s in t.sts(c)), str(t.st()))
t.fps()
t.quit()
t.finish()
