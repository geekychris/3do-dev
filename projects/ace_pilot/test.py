"""Ace Pilot port test (./3do test ace_pilot)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("ace_pilot")
t.boot()
t.check("Blue Danube loads from disc", any(l.startswith("ACE: Loaded music") for l in t.emu.debug_log), t.log_tail())
t.shot("title", min_colours=4)
t.start(["A"], expect="Game started")
c = t.emu.log_cursor()
for i in range(40):                      # fly around, firing
    t.play(frames=10, hold=["A"] + (["LEFT"] if i % 8 < 3 else []) + (["UP"] if i % 10 == 0 else []))
t.shot("flying", min_colours=3)
t.check_game_speed(min_steps=45)
t.report_fps()
t.press("X")                              # Esc: in game, back to the title...
t.play(frames=30)
t.quit()                                  # ...on the title, exit
t.finish()
