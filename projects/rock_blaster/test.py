"""Rock Blaster port test (./3do test rock_blaster)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("rock_blaster")
t.boot()
t.shot("title", min_colours=4)
t.start(["P"], expect="state=PLAYING")
t.play(frames=200, hold=["A", "LEFT"])
t.shot("playing", min_colours=3)
t.check_speed(min_fps=45)
t.quit()
t.finish()
