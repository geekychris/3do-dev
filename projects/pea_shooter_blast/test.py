"""Pea Shooter Blast port test (./3do test pea_shooter_blast)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("pea_shooter_blast")
t.boot()
t.check("runs without the (unshipped) MOD", any("sound effects only" in l or "Loaded music.mod" in l
                                               for l in t.emu.debug_log), t.log_tail())
t.shot("title", min_colours=4)
t.start(["A"], expect="state=1")
c = t.emu.log_cursor()
for i in range(30):                       # drive right, jump and shoot
    t.play(frames=10, hold=["RIGHT", "A"] + (["B"] if i % 5 == 0 else []))
t.check("shooting scores points", any(l.startswith("PEA: score=") for l in t.emu.debug_log[c:]), t.log_tail())
t.shot("playing", min_colours=6)
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.finish()
