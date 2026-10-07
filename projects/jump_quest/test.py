"""Jump Quest port test (./3do test jump_quest)."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "tdo" / "src"))
from tdo.porttest import PortTest

t = PortTest("jump_quest")
t.boot()
t.play(frames=40)
t.shot("title", min_colours=5)
t.start(["A"], expect="JUMP: state=1 level=0")          # STATE_PLAYING on level 1
c = t.emu.log_cursor()
for i in range(25):                                     # run right, hopping
    t.play(frames=12, hold=["RIGHT"] + (["A"] if i % 3 == 0 else []))
t.shot("playing", min_colours=6)
lines = t.emu.debug_log[c:]
t.check("running through level 1 scores or ends a life",
        any(l.startswith("JUMP: score=") for l in lines) or any("state=2" in l for l in lines), t.log_tail())
t.check_speed(min_fps=45)
t.quit()
t.finish()
