"""Uranus Lander port test (./3do test uranus_lander)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("uranus_lander")
t.boot()
t.wait_log("URANUS: Loaded uranus.mod", max_frames=300, label="music loads from disc")
t.play(frames=60)
t.shot("title", min_colours=5)
t.play(frames=60)                        # the game ignores input for its first 30 loops
t.start(["A"], expect="state=PLAYING")
c = t.emu.log_cursor()
for i in range(40):                      # rotate a little and pulse the thrust
    t.play(frames=6, hold=["A"] + (["LEFT"] if i % 8 == 0 else []))
    t.play(frames=10)
t.shot("playing", min_colours=6)
t.check("the lander lands or crashes", t.emu.run_until_log("state=LANDED", 1500, since=c)
        or any("state=CRASHING" in l for l in t.emu.debug_log[c:]), t.log_tail())
t.check_game_speed(min_steps=45)
t.report_fps()
t.quit()
t.finish()
