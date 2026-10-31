"""Ace Pilot 2 on the 3DO (./3do test ace_pilot2).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), the long
   autopilot campaign's state hash (tests/vectors.h) among them.
2. The title (the autopilot flies a mission behind it), A to the briefing
   and A again to take off, R/L throttle, the D-pad banking and climbing,
   A firing, C the cockpit view, the autopilot (B) flying on; the frame
   rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("ace_pilot2")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=400)
t.keep("title")
t.start(["A"], expect="STATE BRIEF")
t.play(frames=90)
t.keep("briefing")
t.start(["A"], expect="STATE FLYING")
t.play(frames=90)
s0 = t.st()
t.play(frames=40, hold=["L"])
t.play(frames=70)
s1 = t.st()
t.check(f"L closes the throttle ({s0.get('thr')} -> {s1.get('thr')})", int(s1.get("thr", 0)) < int(s0.get("thr", 0)), str(s1))
t.play(frames=40, hold=["R"])
t.play(frames=70)
s2 = t.st()
t.check(f"R opens it ({s1.get('thr')} -> {s2.get('thr')})", int(s2.get("thr", 0)) > int(s1.get("thr", 0)), str(s2))
t.play(frames=20, hold=["R"])
t.play(frames=60, hold=["LEFT"])
t.play(frames=70)
s3 = t.st()
t.check(f"left banks it round (heading {s2.get('yaw')} -> {s3.get('yaw')})", s3.get("yaw") != s2.get("yaw"))
t.play(frames=30, hold=["DOWN"])
t.play(frames=70)
s4 = t.st()
t.check(f"down pulls the nose up (pitch {s3.get('pitch')} -> {s4.get('pitch')})", s4.get("pitch") != s3.get("pitch"))
t.play(frames=30, hold=["A"])
t.play(frames=70)
t.check("A fires the guns", int(t.st().get("shots", 0)) > int(s4.get("shots", 0)), str(t.st()))
t.keep("chase")
t.press(["C"])
t.play(frames=70)
t.check("C: the cockpit view", t.st().get("view") == "1", str(t.st()))
t.keep("cockpit")
t.press(["C"])
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=1800)
t.keep("playing")
sts = t.sts(c)
d = 0
if sts:
    d = abs(int(sts[-1].get("x", 0)) - int(sts[0].get("x", 0))) + abs(int(sts[-1].get("z", 0)) - int(sts[0].get("z", 0)))
t.check(f"the autopilot flies the mission ({d} units, {len(sts)} status lines)",
        len(sts) > 10 and (d > 500 or sts[-1].get("state") != "2"), str(sts[-1:]))
t.fps()
t.quit()
t.finish()
