"""Orbital Patrol port test (./3do test orbital_patrol)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("orbital_patrol")
t.boot()
t.check("music loads from disc", any(l.startswith("ORBIT: MOD loaded") for l in t.emu.debug_log), t.log_tail())
t.play(frames=40)
t.shot("title", min_colours=4)
t.start(["A"], expect="state=LEVEL_START")
t.wait_log("state=PLAYING", max_frames=200)
for i in range(30):                       # fly and fire
    t.play(frames=8, hold=["A", "RIGHT" if i % 10 < 6 else "LEFT"] + (["UP"] if i % 4 == 0 else []))
    t.play(frames=4, hold=["RIGHT"])
t.check("shooting scores points", any(l.startswith("ORBIT: score=") for l in t.emu.debug_log), t.log_tail())
t.shot("playing", min_colours=5)
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.finish()
