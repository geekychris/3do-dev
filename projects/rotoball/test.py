"""Rotoball port test (./3do test rotoball): kick-off screen, a full
90-second match (it must last 90 s), frozen full time, restart."""
import re
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("rotoball")
t.boot()
t.check("kick-off screen (state=KICKOFF)",
        t.emu.run_until_log("BALL: state=KICKOFF", 300, since=0), t.log_tail())
sym = [l for l in t.emu.debug_log if "SYMBOL p1" in l]
m = re.search(r"p1 0x([0-9a-f]+) ball 0x([0-9a-f]+)", sym[-1]) if sym else None
p1, ball = (int(m.group(1), 16), int(m.group(2), 16)) if m else (None, None)
pos = lambda a: struct.unpack(">ll", t.emu.read_mem(a, 8))
t.shot("kickoff", min_colours=8)

x0 = pos(p1)
t.play(frames=60, hold=["UP"])
t.check("play is frozen on the kick-off screen", pos(p1) == x0)

t.start(["A"], expect="state=PLAYING score=0:0")
f0 = t.emu.frame_count
x0 = pos(p1)
t.play(frames=100, hold=["UP"])
x1 = pos(p1)
t.check(f"UP drives forward (x {x0[0] / 65536:.1f} -> {x1[0] / 65536:.1f})", x1[0] > x0[0] + 5 * 65536)
t.play(frames=60, hold=["LEFT"])
x2 = pos(p1)
t.play(frames=120, hold=["RIGHT"])
x3 = pos(p1)
t.check("LEFT / RIGHT strafe", x2[1] > x1[1] and x3[1] < x2[1])
t.shot("playing", min_colours=8)
t.check_game_speed(min_steps=45)
t.report_fps()

c = t.emu.log_cursor()
t.check("the match runs to full time (state=FULLTIME)",
        t.emu.run_until_log("state=FULLTIME", 7000, since=c), t.log_tail())
secs = (t.emu.frame_count - f0) / t.emu.fps
t.check(f"the match lasts 90 s ({secs:.1f} emulated s)", 88.0 <= secs <= 93.0)
t.emu.step(30)
t.shot("fulltime", min_colours=8)
a, b = pos(p1), pos(ball)
t.play(frames=100, hold=["UP"])
t.check("play is frozen at full time", pos(p1) == a and pos(ball) == b)

t.start(["A"], expect="state=PLAYING score=0:0")
t.quit()
t.finish()
