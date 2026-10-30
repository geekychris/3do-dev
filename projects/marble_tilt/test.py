"""Marble Tilt on the 3DO (./3do test marble_tilt).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), the long
   autopilot game's state hash (tests/vectors.h) among them.
2. The title (the autopilot rolls behind it), a start with A, the D-pad
   tilting the board, the autopilot (B) rolling on; the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("marble_tilt")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=300)
t.keep("title")
t.start(["A"], expect="STATE PLAY")
t.play(frames=40)
c = t.emu.log_cursor()
t.play(frames=130, hold=["RIGHT"])
rolls = [int(s.get("roll", 0)) for s in t.sts(c)]
t.check(f"the D-pad tilts the board (roll {rolls})", any(r > 20 for r in rolls))
c = t.emu.log_cursor()
t.play(frames=130, hold=["UP"])
pitches = [int(s.get("pitch", 0)) for s in t.sts(c)]
t.check(f"and up tilts it away (pitch {pitches})", any(p > 20 or p < -20 for p in pitches))
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=1500)
t.keep("playing")
t.check("the autopilot rolls through a level", any(int(s.get("score", 0)) > 0 or s.get("level") != "1" for s in t.sts(c)), str(t.st()))
t.fps()
t.quit()
t.finish()
