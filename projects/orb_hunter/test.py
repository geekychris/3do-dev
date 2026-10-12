"""Orb Hunter port test (./3do test orb_hunter)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("orb_hunter")
t.boot()
t.check("music loads from disc", any("Loaded rock_blaster.mod" in l for l in t.emu.debug_log)
        and not any("failed validation" in l for l in t.emu.debug_log), t.log_tail())
t.shot("title", min_colours=4)
t.start(["A"], expect="state=PLAYING")
c = t.emu.log_cursor()
for d in ["RIGHT", "RIGHT", "LEFT", "LEFT", "LEFT", "RIGHT"]:   # explore: run, jump, shoot
    t.play(frames=40, hold=[d, "A"])
    t.play(frames=15, hold=[d, "B"])
t.shot("playing", min_colours=6)
t.check_speed(min_fps=45)
t.quit()
t.finish()
