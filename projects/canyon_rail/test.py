"""Canyon Rail on the 3DO (./3do test canyon_rail).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), the long
   autopilot ride's state hash (tests/vectors.h) among them.
2. The title (the autopilot rides behind it), a start with A, the D-pad
   moving the sight, A firing, the autopilot (B) shooting its way down
   the canyon; the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("canyon_rail")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=400)
t.keep("title")
t.start(["A"], expect="STATE RIDE")
t.play(frames=70)
s0 = t.st()
t.play(frames=60, hold=["LEFT", "UP"])
t.play(frames=70)
s1 = t.st()
t.check(f"the D-pad moves the sight ({s0.get('sx')},{s0.get('sy')} -> {s1.get('sx')},{s1.get('sy')})",
        int(s1.get("sx", 0)) < int(s0.get("sx", 0)) - 10 and int(s1.get("sy", 0)) < int(s0.get("sy", 0)) - 10)
t.play(frames=60, hold=["A"])
t.play(frames=70)
t.check("A fires", int(t.st().get("shots", 0)) > int(s1.get("shots", 0)), str(t.st()))
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=700)
t.keep("playing")
t.play(frames=1000)
t.check("the autopilot shoots its way down the canyon", any(int(s.get("kills", 0)) > 0 for s in t.sts(c)), str(t.st()))
t.fps()
t.quit()
t.finish()
