"""Bullion Dash port test (./3do test bullion_dash)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("bullion_dash")
t.boot()
t.shot("title", min_colours=5)
t.start(["A"], expect="state=PLAYING")
c = t.emu.log_cursor()
for d in ["LEFT", "RIGHT", "RIGHT", "LEFT", "UP", "LEFT"]:     # run around level 1
    t.play(frames=50, hold=[d])
t.play(frames=10, hold=["B"])                                  # dig left
t.play(frames=10, hold=["C"])                                  # dig right
t.shot("playing", min_colours=6)
lines = t.emu.debug_log[c:]
t.check("the runner collects gold or meets a guard",
        any(l.startswith("BULLION: score=") and "gold=0/" not in l for l in lines)
        or any("state=DYING" in l for l in lines), t.log_tail())
t.check_speed(min_fps=45)
t.quit()
t.finish()
