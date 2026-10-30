"""Warden Keep port test (./3do test warden_keep): plays the room through."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("warden_keep")
t.boot()
t.shot("title", min_colours=4)
t.start(["A"], expect="state=PLAYING")
# player starts at (3,6); jewels at (2,6), (6,6), (6,3)
c = t.emu.log_cursor()
for d, n in [("LEFT", 1), ("RIGHT", 4), ("UP", 3)]:
    for _ in range(n):
        t.press(d, hold=4, release=8)
    if d == "RIGHT":
        t.shot("playing", min_colours=6)
t.check("all three jewels -> state=WIN", t.emu.run_until_log("state=WIN", 120, since=c), t.log_tail())
t.emu.step(60)
t.shot("win", min_colours=3)
t.check_speed(min_fps=45)
t.quit()
t.finish()
