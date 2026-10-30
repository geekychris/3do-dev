"""Emberkeep port test (./3do test emberkeep): the recorded run clears level 1, then play on."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("emberkeep")
t.boot()
t.shot("title", min_colours=4)
t.start(["C"], expect="demo: start")
t.emu.step(150)
t.shot("demo", min_colours=5)
c = t.emu.log_cursor()
t.check("demo clears level 1 (state=LEVELWIN, then demo: done level=2)",
        t.emu.run_until_log("state=LEVELWIN", 1500, since=c)
        and t.emu.run_until_log("demo: done, level=2", 300, since=c), t.log_tail())
t.check("level 2 starts", t.emu.run_until_log("state=PLAYING level=2", 300, since=c), t.log_tail())
t.emu.step(30)
t.shot("level2", min_colours=5)
c = t.emu.log_cursor()
for d in ["DOWN", "RIGHT", "UP", "LEFT"]:
    t.play(frames=50, hold=[d])
t.check("still running (status lines)", t.emu.run_until_log("status:", 600, since=c), t.log_tail())
t.shot("playing", min_colours=5)
t.check_speed(min_fps=45)
t.quit()
t.finish()
