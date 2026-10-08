"""Planet Chomp test (./3do test planet_chomp)."""
import re
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("planet_chomp")
t.boot()
t.wait_log("PLANET: ready cells=486", 600, "maze built")
t.play(frames=60)
t.shot("title", min_colours=8)
t.start(["A"], expect="state=1 ")                 # READY
t.wait_log("state=2 ", 400, "READY -> PLAYING")
for d in ["RIGHT", "UP", "LEFT", "UP", "RIGHT", "DOWN", "LEFT", "UP"]:
    t.play(frames=70, hold=[d])
scores = [int(m.group(1)) for l in t.emu.debug_log for m in [re.search(r"chomp score=(\d+)", l)] if m]
t.check("chomper eats crumbs", bool(scores) and max(scores) > 0, f"scores {scores[-3:]}")
t.check_game_speed(min_steps=45)
t.report_fps()
t.shot("playing", min_colours=12)
t.quit()
t.emu.close()

# attract mode: left alone on the title, the autopilot plays a demo game
t = PortTest("planet_chomp")
t.boot()
t.wait_log("PLANET: ready cells=486", 600, "maze built")
t.wait_log("PLANET: demo starts", 1200, "demo starts after 15 s on the title")
t.play(frames=300)
t.shot("demo", min_colours=12)
c = t.emu.log_cursor()
t.press(["A"])
t.check("a button leaves the demo", t.emu.run_until_log("state=0 ", 200, since=c), t.log_tail())
t.wait_log("PLANET: demo starts", 1200, "the demo comes back")
c = t.emu.log_cursor()
t.wait_log("PLANET: key: invincible", 9000, "autopilot grabs a key")
t.check("a death or an eaten spook within the demo",
        t.emu.run_until_log("PLANET: eat ", 6000, since=c) or
        any("PLANET: death" in l for l in t.emu.debug_log[c:]), t.log_tail())
t.emu.close()
print(f"{'FAIL' if t.failures else 'PASS'}: planet_chomp ({len(t.failures)} failure(s))")
sys.exit(1 if t.failures else 0)
