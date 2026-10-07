"""Void Trader port test (./3do test void_trader)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("void_trader")
t.boot()
t.play(frames=30)
t.shot("title", min_colours=3)
t.start(["A"], expect="mode=1")                 # GM_FLIGHT
for i in range(30):                             # thrust, turn and fire
    t.play(frames=10, hold=["B", "A"] + (["LEFT"] if i % 6 < 2 else []))
t.shot("flight", min_colours=4)
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.finish()
