"""Dot Chase port test (./3do test dot_chase)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("dot_chase")
t.boot()
t.shot("title", min_colours=4)
t.start(["A"], expect="state=READY")
t.wait_log("state=PLAYING", max_frames=400)
for d in ["LEFT", "UP", "RIGHT", "DOWN", "LEFT", "UP"]:
    t.play(frames=60, hold=[d])
t.check("eating dots scores points", any(l.startswith("DOT: score=") for l in t.emu.debug_log), t.log_tail())
t.shot("playing", min_colours=5)
t.check_speed(min_fps=45)
t.quit()
t.finish()
