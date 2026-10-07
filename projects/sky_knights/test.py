"""Sky Knights port test (./3do test sky_knights)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("sky_knights")
t.boot()
t.check("flap sample is on the disc",
        any(l.startswith("SKY: samples flap.raw=") and not l.endswith("=0 bytes") for l in t.emu.debug_log), t.log_tail())
t.shot("title", min_colours=4)
t.start(["C"], expect="players=2")
t.wait_log("state=PLAYING", max_frames=400)
for i in range(20):                     # both players flap around
    t.play(frames=10, hold=["A", "RIGHT" if i % 4 < 2 else "LEFT"])
    t.emu.set_buttons(["A"], 1)
    t.play(frames=5)
    t.emu.set_buttons([], 1)
t.shot("playing", min_colours=5)
t.check_speed(min_fps=45)
t.quit()
t.finish()
