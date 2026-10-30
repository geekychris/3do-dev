"""ScAIry Fun on the 3DO (./3do test scairy_fun).

1. The game's own tests on the 3DO (the A3D_TEST=1 disc): the road, the
   traffic, every hazard and how the autopilot meets it. ScAIry Fun has no
   tests/vectors.h: its cross-check is tests/trace_main.c, route 1 driven
   by the perfect autopilot for 7500 frames with every event printed - run
   on the 3DO and natively here, and compared line by line.
2. The title (the autopilot drives behind it), a start, the autopilot
   driving, A taking over, the D-pad steering and braking, B the view;
   the frame rate; X.
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "sdk" / "amiga3d"))
from a3dtest import A3Test

t = A3Test("scairy_fun")
t.selftest(hash_test="arrives at the destination")
t.host_trace(["src/tests/trace_main.c.host", "src/track.c", "src/world.c"])

t.boot()
t.wait_ready()
t.play(frames=500)
t.keep("title")
t.start(["A"], expect="STATE intro")
t.emu.run_until_log("STATE play", 1500)
t.play(frames=300)
s0 = t.st()
t.check("the autopilot drives", s0.get("ai") == "1" and int(s0.get("v", 0)) > 0, str(s0))
t.keep("playing")
t.press(["A"], hold=12)
t.play(frames=70)
t.check("A takes over", t.st().get("ai") == "0", str(t.st()))
s1 = t.st()
t.play(frames=90, hold=["LEFT"])
t.play(frames=10)
s2 = t.st()
t.check(f"left steers (offset {s1.get('d')} -> {s2.get('d')})", s2.get("d") != s1.get("d"))
t.play(frames=120, hold=["DOWN"])
s3 = t.st()
t.check(f"down brakes (speed {s2.get('v')} -> {s3.get('v')})", int(s3.get("v", 0)) < int(s2.get("v", 0)))
t.press(["B"], hold=12)
t.play(frames=70)
t.check("B changes the view", t.st().get("view") == "1", str(t.st()))
c = t.emu.log_cursor()
t.press(["A"], hold=12)
t.play(frames=70)
said = [l for l in t.emu.debug_log[c:] if "EV " in l]
t.check("A asks for the autopilot back (on, or unavailable off the lane)",
        t.st().get("ai") == "1" or any("unavailable" in l.lower() for l in said), str(said[-3:]))
t.fps()
t.quit()
t.finish()
