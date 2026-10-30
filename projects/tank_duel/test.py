"""Tank Duel on the 3DO (./3do test tank_duel).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), the long
   autopilot game's state hash (tests/vectors.h) among them.
2. The title (the autopilot fights behind it), a start with A, the D-pad
   turning and driving the tank, A firing, the autopilot (B) fighting on;
   the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("tank_duel")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=400)
t.keep("title")
t.start(["A"], expect="STATE FIGHT")
t.play(frames=70)
s0 = t.st()
t.play(frames=60, hold=["LEFT"])
t.play(frames=70)
s1 = t.st()
t.check(f"left turns the tank (yaw {s0.get('yaw')} -> {s1.get('yaw')})", s1.get("yaw") != s0.get("yaw"))
t.play(frames=120, hold=["UP"])
t.play(frames=70)
s2 = t.st()
d = abs(int(s2.get("x", 0)) - int(s1.get("x", 0))) + abs(int(s2.get("z", 0)) - int(s1.get("z", 0)))
t.check(f"up drives it ({d} units)", d > 30)
t.press(["A"])
t.play(frames=70)
t.check("A fires", int(t.st().get("shots", 0)) > int(s2.get("shots", 0)), str(t.st()))
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=500)
t.keep("playing")
t.play(frames=1300)
t.check("the autopilot fights (shots fired)", any(int(s.get("shots", 0)) > 3 for s in t.sts(c)), str(t.st()))
t.fps()
t.quit()
t.finish()
