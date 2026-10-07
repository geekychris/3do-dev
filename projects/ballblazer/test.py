"""Ballblazer port test (./3do test ballblazer)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("ballblazer")
t.boot()
t.check("match starts", any(l.startswith("BALL: score=0:0") for l in t.emu.debug_log), t.log_tail())
t.play(frames=100, hold=["UP"])
t.play(frames=60, hold=["UP", "LEFT"])
t.play(frames=60, hold=["DOWN", "RIGHT"])
t.shot("playing", min_colours=8)
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.finish()
