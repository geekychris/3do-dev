"""Frank the Frog port test (./3do test frank_the_frog)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("frank_the_frog")
t.boot()
t.shot("title", min_colours=4)
t.start(["A"], expect="state=PLAYING")
c = t.emu.log_cursor()
for _ in range(6):                       # hop up the road; each new row scores 10
    t.press(["UP"], hold=3, release=12)
t.play(frames=20)
t.shot("playing", min_colours=6)
lines = t.emu.debug_log[c:]
t.check("hopping onto the road (scores or gets splatted)",
        any(l.startswith(("FROG: SPLAT", "FROG: SPLASH", "FROG: HOME")) for l in lines)
        or t.emu.run_until_log("state=DYING", 600, since=c), t.log_tail())
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.finish()
