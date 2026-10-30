"""Boing Pinball on the 3DO (./3do test boing_pinball).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), among them the
   browser original's vectors (tests/vectors.h): the ball against
   segments, flippers and bumpers, and a short flight on Comet Tour that
   must end where the browser's does; and bot-played games on every table.
2. The title (the bot plays the table behind it), a start with A, the
   plunger on A, the flippers on L and R, the bot (B) playing on; the
   frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("boing_pinball")
t.selftest(hash_test="a short flight on Comet Tour ends where the browser's does")

t.boot()
t.wait_ready()
t.play(frames=600)
t.keep("title")
t.check("the bot plays the title's demo", t.st().get("demo") == "1", str(t.st()))
t.start(["A"], expect="STATE PLAY")
t.play(frames=70)
c = t.emu.log_cursor()
t.play(frames=90, hold=["A"])
t.check("A pulls the plunger", any(int(s.get("pull", 0)) > 10 for s in t.sts(c)), str(t.sts(c)[-1:]))
t.play(frames=200)
c = t.emu.log_cursor()
t.play(frames=80, hold=["L"])
t.check("L works the left flipper", any(s.get("lflip") == "1" for s in t.sts(c)), str(t.sts(c)[-1:]))
c = t.emu.log_cursor()
t.play(frames=80, hold=["R"])
t.check("R works the right flipper", any(s.get("rflip") == "1" for s in t.sts(c)), str(t.sts(c)[-1:]))
t.press(["B"], hold=10)
t.play(frames=70)
t.check("B lets the bot play", t.st().get("bot") == "1", str(t.st()))
c = t.emu.log_cursor()
t.play(frames=1200)
t.keep("playing")
t.check("the bot scores", any(int(s.get("score", 0)) > 0 for s in t.sts(c)), str(t.st()))
t.fps()
t.quit()
t.finish()
