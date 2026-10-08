"""Planet Chomp test (./3do test planet_chomp)."""
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("planet_chomp")
t.boot()
t.wait_log("PLANET: ready cells=486", 600, "maze built")
t.play(frames=40)
t.shot("start", min_colours=6)
for d in ["RIGHT", "UP", "LEFT", "UP", "RIGHT", "DOWN"]:
    t.play(frames=90, hold=[d])
t.play(frames=200)
scores = [int(m.group(1)) for l in t.emu.debug_log for m in [re.search(r"score=(\d+)", l)] if m]
t.check("chomper eats crumbs", bool(scores) and max(scores) > 0, f"scores {scores[-3:]}")
t.shot("roaming", min_colours=6)
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.emu.close()
print(f"{'FAIL' if t.failures else 'PASS'}: planet_chomp ({len(t.failures)} failure(s))")
sys.exit(1 if t.failures else 0)
