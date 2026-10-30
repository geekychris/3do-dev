"""Stack Tower on the 3DO (./3do test stack_tower).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc), among them the
   four games played by the browser original (tests/vectors.h) that the
   port must end exactly as it did.
2. The title (the demo plays the tower), a start with A, the D-pad turning
   the tower, up rotating, B rotating back, C holding, A hard-dropping;
   the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("stack_tower")
t.selftest(hash_test="browser vector: the board")

t.boot()
t.wait_ready()
t.play(frames=900)
t.keep("title")
t.check("the title's demo plays", int(t.st().get("pieces", 0)) > 2, str(t.st()))
t.start(["A"], expect="STATE PLAY")
t.play(frames=70)
s0 = t.st()
t.play(frames=12, hold=["RIGHT"])
t.play(frames=70)
s1 = t.st()
t.check(f"right turns the tower under the piece (col {s0.get('col')} -> {s1.get('col')})", s1.get("col") != s0.get("col"))
t.press(["UP"])
t.play(frames=70)
s2 = t.st()
t.check(f"up rotates it (rot {s1.get('rot')} -> {s2.get('rot')})", s2.get("rot") != s1.get("rot"))
t.press(["B"])
t.play(frames=70)
s3 = t.st()
t.check(f"B rotates it back (rot {s2.get('rot')} -> {s3.get('rot')})", s3.get("rot") == s1.get("rot"))
t.press(["C"])
t.play(frames=70)
t.check("C holds the piece", t.st().get("hold") != "-1", str(t.st()))
p0 = int(t.st().get("pieces", 0))
for k in range(6):
    t.press(["LEFT"] if k % 2 else ["RIGHT"], hold=15)
    t.press(["A"], hold=15)
    t.play(frames=40)
t.play(frames=70)
t.check("A hard-drops pieces", int(t.st().get("pieces", 0)) >= p0 + 5, str(t.st()))
t.keep("playing")
t.fps()
t.quit()
t.finish()
