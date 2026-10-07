"""Lunar Rider port test (./3do test lunar_rider)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("lunar_rider")
t.boot()
t.shot("title", min_colours=4)
t.start(["A"], expect="state=PLAYING")
for i in range(30):                     # drive, jump and shoot
    t.play(frames=8, hold=["RIGHT", "A"] + (["UP"] if i % 3 == 0 else []))
    t.play(frames=4, hold=["RIGHT"])
t.check("riding scores points", any(l.startswith("LUNAR: score=") for l in t.emu.debug_log), t.log_tail())
t.shot("playing", min_colours=5)
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.finish()
