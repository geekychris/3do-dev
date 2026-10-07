"""Nova Defense port test (./3do test nova_defense)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("nova_defense")
t.boot()
t.shot("title", min_colours=4)
t.start(["A"], expect="state=PLAYING")
for _ in range(25):                 # fire is edge-triggered: tap A while drifting left
    t.play(frames=6, hold=["A", "LEFT"])
    t.play(frames=6, hold=["LEFT"])
t.check("shooting scores points", any(l.startswith("NOVA: score=") for l in t.emu.debug_log), t.log_tail())
t.shot("playing", min_colours=4)
t.check_speed(min_fps=45)
t.quit()
t.finish()
