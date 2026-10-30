"""Ridge Runner on the 3DO (./3do test ridge_runner).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), the long
   autopilot race's state hash (tests/vectors.h) among them.
2. The title (the autopilot races behind it), a start with A, the D-pad
   throttle and steering, A boosting, the autopilot (B) racing on through
   the gates; the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("ridge_runner")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=400)
t.keep("title")
t.start(["A"], expect="STATE RACE")
t.play(frames=300)                      # the 3-2-1
s0 = t.st()
t.play(frames=150, hold=["UP"])
s1 = t.st()
t.check(f"up throttles (speed {s0.get('v')} -> {s1.get('v')})", int(s1.get("v", 0)) > int(s0.get("v", 0)) + 20)
t.play(frames=90, hold=["UP", "LEFT"])
t.play(frames=70)
s2 = t.st()
t.check(f"left steers (heading {s1.get('yaw')} -> {s2.get('yaw')})", s2.get("yaw") != s1.get("yaw"))
c = t.emu.log_cursor()
t.play(frames=150, hold=["UP", "A"])
t.check("A boosts", any(s.get("boost") == "1" for s in t.sts(c)), str(t.st()))
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=600)
t.keep("playing")
t.play(frames=1200)
gates = [int(s.get("gate", 0)) for s in t.sts(c)]
t.check(f"the autopilot races through the gates ({gates[:1]} -> {gates[-1:]})", bool(gates) and max(gates) >= 2)
t.fps()
t.quit()
t.finish()
