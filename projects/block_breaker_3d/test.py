"""Block Breaker 3D on the 3DO (./3do test block_breaker_3d).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc): the rules and
   the long autopilot game, whose state hash must match tests/vectors.h.
2. The title (the autopilot plays behind it), a start with the pad, the
   paddle moving with the D-pad, the ball served with A, the autopilot (B)
   playing on; the frame rate; X back to the menu.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("block_breaker_3d")
t.selftest()

t.boot()
t.wait_ready()
t.play(frames=300)
t.keep("title")
c = t.emu.log_cursor()
t.start(["A"], expect="STATE PLAY")
t.play(frames=90)
x0 = int(t.st().get("px", 0))
t.play(frames=120, hold=["RIGHT"])
t.play(frames=70)
x1 = int(t.st().get("px", 0))
t.check(f"the D-pad moves the paddle ({x0} -> {x1})", x1 > x0 + 40)
t.press(["A"])
t.play(frames=70)
t.check("A serves the ball", t.st().get("stuck") == "0", str(t.st()))
t.press(["B"])
t.play(frames=70)
t.check("B turns the autopilot on", t.st().get("auto") == "1", str(t.st()))
t.play(frames=600)
t.keep("playing")
fps = t.fps()
t.check("the autopilot plays on (bricks broken)", int(t.st().get("left", 99)) < 32, str(t.st()))
t.quit()
t.finish()
