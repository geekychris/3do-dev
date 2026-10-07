"""Fractalus port test (./3do test fractalus)."""
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("fractalus")
t.boot()
t.play(frames=60)
t.check("terrain generated", any("terrain: min=" in l for l in t.emu.debug_log), t.log_tail())
t.shot("title", min_colours=8)
t.start(["A"], expect="restart: new mission")
t.play(frames=600, hold=["UP"])                  # thrust
t.play(frames=200, hold=["UP", "RIGHT"])         # and turn
t.shot("flying", min_colours=20)                 # sky gradient + fogged terrain
speeds = [int(m.group(1)) for l in t.emu.debug_log for m in [re.search(r"hb: .* spd=(\d+)", l)] if m]
t.check("ship accelerates", any(s > 0 for s in speeds), f"spd values {speeds[-5:]}")
t.check_game_speed(min_steps=45)
t.report_fps()
c = t.emu.log_cursor()
t.press(["X"])
t.check("X leaves the game", t.emu.run_until_log("AMIGA3DO: exit", 200, since=c), t.log_tail())
t.emu.close()
print(f"{'FAIL' if t.failures else 'PASS'}: fractalus ({len(t.failures)} failure(s))")
sys.exit(1 if t.failures else 0)
