"""Lander 3D on the 3DO (./3do test lander_3d).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc): the physics,
   the scoring, the autopilot landing on every world. Lander 3D has no
   tests/vectors.h: its cross-check is tests/trace_main.c, one autopilot
   flight printed every 2 s of game time - run on the 3DO and natively
   here, and compared line by line.
2. The title (the autopilot flies the demo), a start with A, the pad's
   pitch, roll, thrust and throttle; the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("lander_3d")
t.selftest(hash_test="the autopilot lands with the level's fuel")
t.host_trace(["src/tests/trace_main.c.host", "src/ld_core.c"])

t.boot()
t.wait_ready()
t.play(frames=600)
t.keep("title")
t.check("the autopilot flies the title's demo", t.st().get("demo") == "1", str(t.st()))
t.start(["A"], expect="STATE PLAY")
t.play(frames=200)
s0 = t.st()
c = t.emu.log_cursor()
t.play(frames=90, hold=["UP"])
s1 = t.st()
pitches = [s.get("pitch") for s in t.sts(c)]
c = t.emu.log_cursor()
t.play(frames=60, hold=["LEFT", "A"])
t.play(frames=10)
ss = t.sts(c)
t.check(f"up pitches (pitch {s0.get('pitch')} -> {pitches})", any(p != s0.get("pitch") for p in pitches))
t.check("left rolls", any(s.get("roll") != s1.get("roll") for s in ss), str(ss[-1:]))
t.check("A thrusts", any(int(s.get("throttle", 0)) >= 12 for s in ss), str(ss[-1:]))
t.press(["B"], hold=30)
t.play(frames=70)
t.check("B raises the steady throttle", int(t.st().get("steady", 0)) > 0, str(t.st()))
t.keep("playing")
t.fps()
t.quit()
t.finish()
